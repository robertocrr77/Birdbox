// Included by main.c: one controller task owns ADC, playback and sleep.
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "light_policy.h"

#define PERIPH_EN GPIO_NUM_22
#define LIGHT_POWER GPIO_NUM_14
#define LIGHT_INPUT GPIO_NUM_4
#define LIGHT_ADC_CHANNEL ADC_CHANNEL_4
#define NIGHT_CHECK_SECONDS 30
#define LIGHT_SETTLE_MS 30 // >5 RC constants with 100 nF at ADC node
#define LIGHT_FADE_US 200000LL

static adc_oneshot_unit_handle_t sensing_adc;
static adc_cali_handle_t light_calibration;
static light_policy_t light_policy = {.dark_since = -1};
static bool light_valid;
static int light_mv;
static bool peripherals_on;
static int filtered_volume = -1;
static int64_t last_sense = -1000000;
static int64_t last_light_log = -1000000;
static int64_t last_volume_log;
static RTC_DATA_ATTR unsigned next_track;
static RTC_DATA_ATTR bool saved_night;
static RTC_DATA_ATTR bool armed_for_high;
static RTC_DATA_ATTR uint32_t rtc_cookie;

static void v2_sense(void)
{
    int64_t now = esp_timer_get_time();
    if (now - last_sense < 50000) return;
    last_sense = now;
    int total = 0;
    light_valid = true;
    for (int i = 0; i < 8; ++i) {
        int raw;
        if (adc_oneshot_read(sensing_adc, LIGHT_ADC_CHANNEL, &raw) != ESP_OK) {
            light_valid = false;
            break;
        }
        total += raw;
    }
    if (light_valid && adc_cali_raw_to_voltage(light_calibration, total / 8, &light_mv) != ESP_OK)
        light_valid = false;
    bool previous_night = light_policy.night;
    light_policy_update(&light_policy, light_valid, light_mv, esp_timer_get_time());
    if (previous_night != light_policy.night || now - last_light_log >= 1000000) {
        ESP_LOGI("light", "ADC=%dmV valid=%d night=%d; limits night>%d day<%d mV",
                 light_mv, light_valid, light_policy.night, LIGHT_NIGHT_MV, LIGHT_DAY_MV);
        last_light_log = now;
    }
    if (!peripherals_on) return;
    total = 0;
    for (int i = 0; i < 8; ++i) {
        int raw;
        if (adc_oneshot_read(sensing_adc, VOLUME_ADC_CHANNEL, &raw) != ESP_OK) return;
        total += raw;
    }
    int raw = total / 8;
    filtered_volume = filtered_volume < 0 ? raw : (3 * filtered_volume + raw) / 4;
    int position = filtered_volume <= VOLUME_ADC_MIN ? 0 :
        filtered_volume >= VOLUME_ADC_MAX ? 4095 :
        (filtered_volume - VOLUME_ADC_MIN) * 4095 / (VOLUME_ADC_MAX - VOLUME_ADC_MIN);
    atomic_store(&volume_gain_q15, position * (32768 * VOLUME_PERCENT / 100) / 4095);
    if (ENABLE_VOLUME_DEBUG && now - last_volume_log >= 1000000) {
        ESP_LOGI("volume", "GPIO5 raw=%d filtered=%d gain=%d/%d%%", raw, filtered_volume,
                 atomic_load(&volume_gain_q15) * 100 / 32768, VOLUME_PERCENT);
        last_volume_log = now;
    }
}

static void v2_sensing_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &sensing_adc));
    adc_oneshot_chan_cfg_t channel = {.bitwidth = ADC_BITWIDTH_12, .atten = ADC_ATTEN_DB_12};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(sensing_adc, LIGHT_ADC_CHANNEL, &channel));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(sensing_adc, VOLUME_ADC_CHANNEL, &channel));
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1, .chan = LIGHT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    // No uncalibrated raw-to-mV fallback: it would misrepresent resistance thresholds.
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cal, &light_calibration));
    // Keep the tiny divider powered while awake: no settling delay in audio feed.
    // Both excitation and peripheral supply are held LOW throughout deep sleep.
    ESP_ERROR_CHECK(gpio_set_level(LIGHT_POWER, 1));
    vTaskDelay(pdMS_TO_TICKS(LIGHT_SETTLE_MS));
}

static void v2_safe_interfaces(void)
{
    const gpio_num_t pins[] = {AUDIO_DIN, AUDIO_BCLK, AUDIO_LRC,
                               SD_CS, SD_MOSI, SD_CLK, SD_MISO, GPIO_NUM_5};
    for (unsigned i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i) {
        ESP_ERROR_CHECK(gpio_reset_pin(pins[i]));
        ESP_ERROR_CHECK(gpio_set_direction(pins[i], GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_pull_mode(pins[i], GPIO_FLOATING));
    }
}

static void v2_sleep(bool suppress_motion)
{
    v2_safe_interfaces();
    ESP_ERROR_CHECK(gpio_set_level(PERIPH_EN, 0));
    ESP_ERROR_CHECK(gpio_set_level(LIGHT_POWER, 0));
    ESP_ERROR_CHECK(gpio_hold_en(PERIPH_EN));
    ESP_ERROR_CHECK(gpio_hold_en(LIGHT_POWER));
    // ESP32-C6 supports per-pad deep-sleep hold directly via gpio_hold_en.
    saved_night = light_policy.night;
    rtc_cookie = 0x42425832;
    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(NIGHT_CHECK_SECONDS * 1000000ULL));
    armed_for_high = false;
    if (!suppress_motion) {
        // HIGH sensor: wait for LOW first. LOW sensor: arm the next HIGH.
        armed_for_high = gpio_get_level(PIR_INPUT) == 0;
        ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(1ULL << PIR_INPUT,
            armed_for_high ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW));
    }
    ESP_LOGI(TAG, "Deep sleep: night=%d motion=%s next_track=%u",
             saved_night, suppress_motion ? "disabled" : armed_for_high ? "HIGH" : "LOW rearm",
             next_track + 1);
    esp_deep_sleep_start();
}
