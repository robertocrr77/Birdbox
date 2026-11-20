#include <errno.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mp3dec.h"

// Verified exposed header pins; keep synchronized with README.md.
#define AUDIO_DIN GPIO_NUM_1
#define AUDIO_BCLK GPIO_NUM_2
#define AUDIO_LRC GPIO_NUM_3
#define SD_CS GPIO_NUM_18
#define SD_MOSI GPIO_NUM_19
#define SD_CLK GPIO_NUM_20
#define SD_MISO GPIO_NUM_21
#define PIR_INPUT GPIO_NUM_0
#define PIR_POLL_MS 20
// 1 enables motion-triggered audio; 0 runs only the sensor monitor.
#define ENABLE_AUDIO_PLAYBACK 1
#define ENABLE_DECODER_DEBUG 0
#define ENABLE_VOLUME_DEBUG 0 // Raw knob readings once per second while testing.
// Potentiometer full-scale ceiling; keep conservative for the small speaker.
#define VOLUME_PERCENT 40
#define VOLUME_ADC_CHANNEL ADC_CHANNEL_5 // ADC1 channel 5 = exposed GPIO5
#define VOLUME_POLL_MS 50
#define VOLUME_ADC_MIN 40
#define VOLUME_ADC_MAX 4000
#if VOLUME_PERCENT < 0 || VOLUME_PERCENT > 100
#error VOLUME_PERCENT must be between 0 and 100
#endif
// 120 ms at 48 kHz: covers measured ~39 ms SD read stalls with margin.
// 12 x 480 stereo 16-bit frames consume 23,040 bytes of DMA sample memory.
#define DMA_DESCRIPTORS 12
#define DMA_FRAMES 480

#define DECODER_LOGI(...) do { if (ENABLE_DECODER_DEBUG) { ESP_LOGI(__VA_ARGS__); } } while (0)

typedef struct {
    uint64_t read_us, decode_us, prepare_us, pace_us, audio_us;
    uint64_t max_read_us, max_decode_us, max_feed_gap_us;
    unsigned frames, over_budget, long_feed_gaps;
    int64_t last_write_end, last_report;
} timing_t;

static void report_timing(timing_t *t)
{
    if (!t->frames) return;
    DECODER_LOGI("audio_diag", "frames=%u audio/frame=%" PRIu64 "us | SD avg/max=%" PRIu64 "/%" PRIu64
             "us | decode avg/max=%" PRIu64 "/%" PRIu64 "us",
             t->frames, t->audio_us / t->frames, t->read_us / t->frames,
             t->max_read_us, t->decode_us / t->frames, t->max_decode_us);
    DECODER_LOGI("audio_diag", "prepare+pace avg=%" PRIu64 "us | over-budget=%u | max feed gap=%" PRIu64
             "us | gaps exceeding DMA capacity=%u",
             (t->prepare_us + t->pace_us) / t->frames, t->over_budget,
             t->max_feed_gap_us, t->long_feed_gaps);
    int64_t last_write = t->last_write_end;
    *t = (timing_t){.last_write_end = last_write, .last_report = esp_timer_get_time()};
}

static const char *const playlist[] = {
    "/sdcard/1_mono.mp3", "/sdcard/2_mono.mp3", "/sdcard/3_mono.mp3",
    "/sdcard/4_mono.mp3", "/sdcard/5_mono.mp3", "/sdcard/6_mono.mp3",
};
#define PLAYLIST_COUNT (sizeof(playlist) / sizeof(playlist[0]))

static const char *TAG = "birdbox";
static uint8_t input[16384];
static int16_t pcm[2304];
static int16_t stereo[2304];
static i2s_chan_handle_t tx;
static int current_rate = 32000;
static atomic_int volume_gain_q15 = 0; // Start muted until the first valid ADC reading.


#include "v2_sensing.h"

static esp_err_t write_audio(const int16_t *data, size_t bytes)
{
    size_t written = 0;
    esp_err_t err = i2s_channel_write(tx, data, bytes, &written, 2000);
    return err != ESP_OK ? err : (written == bytes ? ESP_OK : ESP_FAIL);
}

static void silence(unsigned seconds)
{
    memset(stereo, 0, sizeof(stereo));
    unsigned remaining = current_rate * seconds;
    while (remaining) {
        unsigned count = remaining > 1152 ? 1152 : remaining;
        esp_err_t err = write_audio(stereo, count * 2 * sizeof(int16_t));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Silence drain failed: %s; continuing shutdown", esp_err_to_name(err));
            break;
        }
        remaining -= count;
    }
}

static bool skip_id3(FILE *file)
{
    uint8_t header[10];
    size_t count = fread(header, 1, sizeof(header), file);
    if (count == sizeof(header) && memcmp(header, "ID3", 3) == 0) {
        if ((header[6] | header[7] | header[8] | header[9]) & 0x80) return false;
        long size = ((long)header[6] << 21) | ((long)header[7] << 14)
                    | ((long)header[8] << 7) | header[9];
        if (header[3] == 4 && (header[5] & 0x10)) size += 10;
        return fseek(file, 10 + size, SEEK_SET) == 0;
    }
    return fseek(file, 0, SEEK_SET) == 0;
}

static bool play_mp3(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Cannot open %s: %s. Check the card root directory.",
                 path, strerror(errno));
        return false;
    }
    bool ok = skip_id3(file);
    size_t available = 0;
    unsigned frames = 0;
    int previous_gain = 0;
    timing_t timing = {.last_report = esp_timer_get_time()};
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        ESP_LOGE(TAG, "Cannot allocate Helix decoder");
        fclose(file);
        return false;
    }
    ESP_LOGI(TAG, "Playing %s (potentiometer volume; maximum %d%%)", path, VOLUME_PERCENT);
    int64_t fade_start = -1;
    int fade_gain = 0;
    while (ok) {
        v2_sense();
        if ((light_policy.night || !light_valid) && fade_start < 0) {
            fade_start = esp_timer_get_time();
            fade_gain = previous_gain;
            ESP_LOGI(TAG, "Fading out for darkness or light ADC failure");
        }
        int64_t cycle_start = esp_timer_get_time();
        // Read ahead in larger chunks instead of a small SD read every frame.
        if (available < 4096 && !feof(file)) {
            available += fread(input + available, 1, sizeof(input) - available, file);
        }
        uint64_t read_us = esp_timer_get_time() - cycle_start;
        timing.read_us += read_us;
        if (read_us > timing.max_read_us) timing.max_read_us = read_us;
        if (ferror(file)) {
            ESP_LOGE(TAG, "SD read error; check card and wiring");
            ok = false;
            break;
        }
        if (available < 4) break;
        int sync = MP3FindSyncWord(input, (int)available);
        if (sync < 0) {
            if (feof(file)) break; // Trailing metadata is not audio.
            // Retain a possible partial sync word across the next refill.
            input[0] = input[available - 1];
            available = 1;
            continue;
        }
        if (sync) {
            available -= sync;
            memmove(input, input + sync, available);
        }
        if (available < 4) break;
        MP3FrameInfo info = {0};
        int header_error = MP3GetNextFrameInfo(decoder, &info, input);
        if (header_error != ERR_MP3_NONE) {
            --available;
            memmove(input, input + 1, available);
            continue;
        }
        if (info.bitrate <= 0 || info.samprate <= 0 || info.layer != 3) {
            ESP_LOGE(TAG, "Unsupported MP3 format (free-bitrate files are not supported)");
            ok = false;
            break;
        }
        // Ensure a complete standard Layer III frame before calling Helix;
        // its low-level side-info parser assumes enough input is present.
        size_t frame_bytes = (info.version == MPEG1 ? 144ULL : 72ULL)
                             * info.bitrate / info.samprate + ((input[2] >> 1) & 1);
        if (available < frame_bytes) {
            ESP_LOGE(TAG, "Truncated MP3 frame at end of file");
            ok = false;
            break;
        }
        unsigned char *next = input;
        int remaining = (int)available;
        int64_t decode_start = esp_timer_get_time();
        int decode_error = MP3Decode(decoder, &next, &remaining, pcm, 0);
        uint64_t decode_us = esp_timer_get_time() - decode_start;
        timing.decode_us += decode_us;
        if (decode_us > timing.max_decode_us) timing.max_decode_us = decode_us;
        if (remaining < 0 || remaining >= (int)available) {
            ESP_LOGE(TAG, "Helix decoder made no valid progress (%d)", decode_error);
            ok = false;
            break;
        }
        available = remaining;
        memmove(input, next, available);
        if (decode_error == ERR_MP3_MAINDATA_UNDERFLOW) continue;
        if (decode_error != ERR_MP3_NONE) {
            ESP_LOGE(TAG, "Helix MP3 decode error: %d", decode_error);
            ok = false;
            break;
        }
        MP3GetLastFrameInfo(decoder, &info);
        if ((info.nChans != 1 && info.nChans != 2) || info.bitsPerSample != 16
            || info.samprate < 8000 || info.samprate > 48000
            || info.outputSamps <= 0 || info.outputSamps > 2304) {
            ESP_LOGE(TAG, "Unsupported MP3 frame format");
            ok = false;
            break;
        }
        if (frames == 0) DECODER_LOGI(TAG, "MP3: %d Hz, %d channel(s), %d kbps",
                                 info.samprate, info.nChans, info.bitrate / 1000);
        if (current_rate != info.samprate) {
            ESP_ERROR_CHECK(i2s_channel_disable(tx));
            i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(info.samprate);
            ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx, &clock));
            ESP_ERROR_CHECK(i2s_channel_enable(tx));
            current_rate = info.samprate;
            timing.last_write_end = 0; // Clock changes intentionally interrupt DMA.
        }
        int samples = info.outputSamps / info.nChans;
        int target_gain = atomic_load_explicit(&volume_gain_q15, memory_order_relaxed);
        if (fade_start >= 0) {
            int64_t elapsed = esp_timer_get_time() - fade_start;
            target_gain = elapsed >= LIGHT_FADE_US ? 0 :
                (int)((int64_t)fade_gain * (LIGHT_FADE_US - elapsed) / LIGHT_FADE_US);
        }
        for (int i = 0; i < samples; ++i) {
            // Mix to mono and duplicate so either amplifier channel works.
            int32_t sample = pcm[i * info.nChans];
            if (info.nChans == 2) sample = (sample + pcm[2 * i + 1]) / 2;
            // Ramp over this frame to avoid clicks when moving the knob.
            int gain = previous_gain + (target_gain - previous_gain) * (i + 1) / samples;
            int16_t quiet = sample * gain / 32768;
            stereo[2 * i] = quiet;
            stereo[2 * i + 1] = quiet;
        }
        previous_gain = target_gain;
        int64_t write_start = esp_timer_get_time();
        uint64_t prepare_us = write_start - cycle_start;
        if (timing.last_write_end) {
            uint64_t gap = write_start - timing.last_write_end;
            if (gap > timing.max_feed_gap_us) timing.max_feed_gap_us = gap;
            // Conservative warning, not a hardware underrun counter: the real
            // queue may contain less than the maximum configured DMA capacity.
            uint64_t capacity_us = (uint64_t)DMA_DESCRIPTORS * DMA_FRAMES * 1000000 / current_rate;
            if (gap > capacity_us) ++timing.long_feed_gaps;
        }
        esp_err_t err = write_audio(stereo, samples * 2 * sizeof(int16_t));
        timing.last_write_end = esp_timer_get_time();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
            ok = false;
            break;
        }
        ++frames;
        if (fade_start >= 0 && target_gain == 0) break;
        // Blocking I2S writes pace playback and let the idle task run once
        // DMA fills; no extra 10 ms sleep is needed after each audio frame.
        uint64_t pace_us = 0;
        uint64_t audio_us = (uint64_t)samples * 1000000 / info.samprate;
        timing.prepare_us += prepare_us;
        timing.pace_us += pace_us;
        timing.audio_us += audio_us;
        ++timing.frames;
        if (prepare_us + pace_us > audio_us) ++timing.over_budget;
        if (esp_timer_get_time() - timing.last_report >= 5000000) report_timing(&timing);
    }
    report_timing(&timing);
    MP3FreeDecoder(decoder);
    fclose(file);
    if (frames == 0) {
        ESP_LOGE(TAG, "No playable MP3 frames found");
        ok = false;
    }
    ESP_LOGI(TAG, "Playback %s (%u frames)", ok ? "finished" : "failed", frames);
    return ok;
}

static sdmmc_card_t *active_card;
static int active_host;
static bool spi_initialized;
static bool tx_enabled;

static bool v2_audio_start(void)
{
    ESP_ERROR_CHECK(gpio_set_level(PERIPH_EN, 1));
    peripherals_on = true;
    filtered_volume = -1;
    atomic_store(&volume_gain_q15, 0);
    vTaskDelay(pdMS_TO_TICKS(200)); // Provisional rail/SD stabilization; verify hardware.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 4000; // Conservative for jumper-wire prototypes.
    spi_bus_config_t bus = {
        .mosi_io_num = SD_MOSI, .miso_io_num = SD_MISO, .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI initialization failed: %s", esp_err_to_name(err));
        return false;
    }
    active_host = host.slot;
    spi_initialized = true;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_CS;
    slot.host_id = host.slot;
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    active_card = NULL;
    ESP_LOGI(TAG, "SD: CS=18 MOSI=19 CLK=20 MISO=21");
    err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &active_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s. Check FAT32, 3V3 and wiring; next motion retries.",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "SD mounted (card contents are never auto-formatted)");
    sdmmc_card_print_info(stdout, active_card);
    active_host = host.slot;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel.dma_desc_num = DMA_DESCRIPTORS;
    channel.dma_frame_num = DMA_FRAMES;
    channel.auto_clear = true;
    err = i2s_new_channel(&channel, &tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S allocation failed: %s", esp_err_to_name(err));
        return false;
    }
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(32000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = AUDIO_BCLK, .ws = AUDIO_LRC,
            .dout = AUDIO_DIN, .din = I2S_GPIO_UNUSED, .invert_flags = {0},
        },
    };
    err = i2s_channel_init_std_mode(tx, &config);
    if (err == ESP_OK) err = i2s_channel_enable(tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed: %s", esp_err_to_name(err));
        return false;
    }
    tx_enabled = true;
    current_rate = 32000;
    ESP_LOGI(TAG, "Audio: DIN=1 BCLK=2 LRC=3. Preparing motion-triggered playback.");
    DECODER_LOGI("audio_diag", "Decoder=Helix fixed-point; no artificial per-frame delay");
    DECODER_LOGI("audio_diag", "Timing every 5s; tick=%ums, DMA=%u frames. Feed gaps are estimates, not hardware underrun counts.",
             (unsigned)portTICK_PERIOD_MS, DMA_DESCRIPTORS * DMA_FRAMES);
    return true;
}

static void v2_audio_stop(void)
{
    if (tx) {
        // Drain the DMA with silence before disabling the amplifier rail.
        if (tx_enabled) {
            silence(1);
            ESP_ERROR_CHECK(i2s_channel_disable(tx));
            tx_enabled = false;
        }
        ESP_ERROR_CHECK(i2s_del_channel(tx));
        tx = NULL;
    }
    if (active_card) {
        ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount("/sdcard", active_card));
        active_card = NULL;
    }
    if (spi_initialized) {
        ESP_ERROR_CHECK(spi_bus_free(active_host));
        spi_initialized = false;
    }
    v2_safe_interfaces();
    ESP_ERROR_CHECK(gpio_set_level(PERIPH_EN, 0));
    peripherals_on = false;
    atomic_store(&volume_gain_q15, 0);
}

static void v2_controller(void *arg)
{
    (void)arg;
    bool resumed = esp_reset_reason() == ESP_RST_DEEPSLEEP && rtc_cookie == 0x42425832;
    bool motion = resumed && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1
                  && armed_for_high;
    if (!resumed) { next_track = 0; saved_night = false; }
    if (next_track >= PLAYLIST_COUNT) next_track = 0;
    light_policy.night = saved_night;
    // Prepare OFF latch values before releasing deep-sleep holds.
    ESP_ERROR_CHECK(gpio_set_direction(PERIPH_EN, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(PERIPH_EN, 0));
    ESP_ERROR_CHECK(gpio_set_direction(LIGHT_POWER, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LIGHT_POWER, 0));
    ESP_ERROR_CHECK(gpio_hold_dis(PERIPH_EN));
    ESP_ERROR_CHECK(gpio_hold_dis(LIGHT_POWER));
    // EXT1 leaves its pad in RTC mode; restore digital access before rearming.
    ESP_ERROR_CHECK(rtc_gpio_deinit(PIR_INPUT));
    gpio_config_t pir = {.pin_bit_mask = 1ULL << PIR_INPUT, .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&pir));
    v2_safe_interfaces();
    v2_sensing_init();
    // Cold boot only: preserve silent startup and discard initial sensor edges.
    if (!resumed) vTaskDelay(pdMS_TO_TICKS(2000));
    v2_sense();
    // Confirm a new dark condition before allowing any SD/audio startup.
    while (light_valid && !light_policy.night && light_policy.dark_since >= 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        v2_sense();
    }
    if (light_policy.night || !light_valid) v2_sleep(true);
    if (!motion || !ENABLE_AUDIO_PLAYBACK) v2_sleep(false);
    unsigned track = next_track;
    // One accepted motion consumes one track, including file/init failures.
    next_track = (next_track + 1) % PLAYLIST_COUNT;
    ESP_LOGI(TAG, "Motion accepted: track %u/%u", track + 1, (unsigned)PLAYLIST_COUNT);
    bool ready = v2_audio_start();
    bool ok = false;
    if (ready) {
        // Startup/cooldown motion is discarded; no sensor task queues it.
        int64_t startup_until = esp_timer_get_time() + 2000000;
        while (esp_timer_get_time() < startup_until) {
            v2_sense();
            if (light_policy.night || !light_valid) break;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!light_policy.night && light_valid) ok = play_mp3(playlist[track]);
    }
    // Cooldown remains three seconds after success, five after failure.
    int64_t cooldown_until = esp_timer_get_time() + (ok ? 3000000 : 5000000);
    v2_audio_stop(); // DMA drain is included in cooldown.
    while (esp_timer_get_time() < cooldown_until) {
        v2_sense();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    v2_sleep(light_policy.night || !light_valid);
}

void app_main(void)
{
    ESP_LOGI(TAG, "FW=birdbox-v2-light built %s %s; PIR=0 PERIPH_EN=22 LDR=4 EXCITE=14; max_volume=%d%%",
             __DATE__, __TIME__, VOLUME_PERCENT);
    if (!ENABLE_DECODER_DEBUG) esp_log_level_set("audio_diag", ESP_LOG_NONE);
    BaseType_t result = xTaskCreate(v2_controller, "v2_controller", 24576, NULL, 5, NULL);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
