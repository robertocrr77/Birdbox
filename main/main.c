#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
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
#define PIR_INPUT GPIO_NUM_22
#define PIR_POLL_MS 20
// 1 enables motion-triggered audio; 0 runs only the sensor monitor.
#define ENABLE_AUDIO_PLAYBACK 1
#define VOLUME_PERCENT 20
#define ENABLE_DECODER_DEBUG 0
// 120 ms at 48 kHz: covers measured ~39 ms SD read stalls with margin.
// 12 x 480 stereo 16-bit frames consume 23,040 bytes of DMA sample memory.
#define DMA_DESCRIPTORS 12
#define DMA_FRAMES 480

// Keep operational sensor/playback messages and all errors visible.
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
static SemaphoreHandle_t playback_ready;
static TaskHandle_t player_handle;


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
        ESP_ERROR_CHECK(write_audio(stereo, count * 2 * sizeof(int16_t)));
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
    timing_t timing = {.last_report = esp_timer_get_time()};
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        ESP_LOGE(TAG, "Cannot allocate Helix decoder");
        fclose(file);
        return false;
    }
    ESP_LOGI(TAG, "Playing %s (volume %d%%)", path, VOLUME_PERCENT);
    while (ok) {
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
        for (int i = 0; i < samples; ++i) {
            // Mix to mono and duplicate so either amplifier channel works.
            int32_t sample = pcm[i * info.nChans];
            if (info.nChans == 2) sample = (sample + pcm[2 * i + 1]) / 2;
            int16_t quiet = sample * VOLUME_PERCENT / 100;
            stereo[2 * i] = quiet;
            stereo[2 * i + 1] = quiet;
        }
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

static void player_task(void *arg)
{
    (void)arg;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 4000; // Conservative for jumper-wire prototypes.
    spi_bus_config_t bus = {
        .mosi_io_num = SD_MOSI, .miso_io_num = SD_MISO, .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA));
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_CS;
    slot.host_id = host.slot;
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    ESP_LOGI(TAG, "SD: CS=18 MOSI=19 CLK=20 MISO=21");
    esp_err_t err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s. Check FAT32 card, 3V3, wiring and module pull-ups; then reset.",
                 esp_err_to_name(err));
        spi_bus_free(host.slot);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "SD mounted (card contents are never auto-formatted)");
    sdmmc_card_print_info(stdout, card);
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel.dma_desc_num = DMA_DESCRIPTORS;
    channel.dma_frame_num = DMA_FRAMES;
    channel.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&channel, &tx, NULL));
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(32000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = AUDIO_BCLK, .ws = AUDIO_LRC,
            .dout = AUDIO_DIN, .din = I2S_GPIO_UNUSED, .invert_flags = {0},
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &config));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_LOGI(TAG, "Audio: DIN=1 BCLK=2 LRC=3. Preparing motion-triggered playback.");
    DECODER_LOGI("audio_diag", "Decoder=Helix fixed-point; no artificial per-frame delay");
    DECODER_LOGI("audio_diag", "Timing every 5s; tick=%ums, DMA=%u frames. Feed gaps are estimates, not hardware underrun counts.",
             (unsigned)portTICK_PERIOD_MS, DMA_DESCRIPTORS * DMA_FRAMES);
    silence(2);
    size_t track = 0;
    while (true) {
        ESP_LOGI(TAG, "Ready for motion; next track %u/%u", (unsigned)(track + 1), (unsigned)PLAYLIST_COUNT);
        // A single readiness token prevents queuing triggers while busy.
        xSemaphoreGive(playback_ready);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "Motion accepted: track %u/%u", (unsigned)(track + 1), (unsigned)PLAYLIST_COUNT);
        bool ok = play_mp3(playlist[track]);
        if (!ok) ESP_LOGW(TAG, "Skipping failed track: %s", playlist[track]);
        silence(ok ? 3 : 5); // Flush queued audio and keep the cooldown silent.
        track = (track + 1) % PLAYLIST_COUNT;
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << PIR_INPUT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    int previous = gpio_get_level(PIR_INPUT);
    uint32_t triggers = 0;
    ESP_LOGI("pir", "Monitoring GPIO22; initial level=%d. Startup transitions may occur while the sensor settles.", previous);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(PIR_POLL_MS));
        int level = gpio_get_level(PIR_INPUT);
        if (level && !previous) {
            ESP_LOGI("pir", "Motion detected: trigger #%" PRIu32 " (GPIO22 LOW -> HIGH)", ++triggers);
            if (ENABLE_AUDIO_PLAYBACK) {
                if (xSemaphoreTake(playback_ready, 0) == pdTRUE) {
                    xTaskNotifyGive(player_handle);
                } else {
                    ESP_LOGI("pir", "Trigger ignored: audio is busy, cooling down, or not ready");
                }
            }
        }
        previous = level;
    }
}

void app_main(void)
{
    ESP_LOGI("birdbox", "Motion playback enabled=%d; volume=%d%%", ENABLE_AUDIO_PLAYBACK, VOLUME_PERCENT);
    playback_ready = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(playback_ready ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t sensor_result = xTaskCreate(sensor_task, "pir_monitor", 3072, NULL, 3, NULL);
    ESP_ERROR_CHECK(sensor_result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    if (!ENABLE_AUDIO_PLAYBACK) {
        ESP_LOGI("pir", "Sensor-only test: audio playback and decoder diagnostics paused");
        return;
    }
    // Decoder scratch arrays need more stack than the default app_main task.
    BaseType_t result = xTaskCreate(player_task, "mp3_player", 24576, NULL, 5, &player_handle);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
