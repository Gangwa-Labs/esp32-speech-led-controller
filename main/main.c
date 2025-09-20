/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_board_init.h"
#include "speech_commands_action.h"
#include "model_path.h"
#include "esp_process_sdkconfig.h"
#include "driver/uart.h"
#include "led_strip.h"
#include "driver/rmt.h"

#define LED_STRIP_GPIO 8
#define LED_STRIP_LENGTH 1
#define RMT_CHANNEL RMT_CHANNEL_0

int detect_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
srmodel_list_t *models = NULL;
static int play_voice = -2;

led_strip_t *strip = NULL;
static int led_state = 0; // 0=idle, 1=wake_detected, 2=listening, 3=command_detected, 4=countdown_red, 5=countup_blue

// Forward declaration
void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b);

// FastLED-style color structure
typedef struct {
    uint8_t r, g, b;
} CRGB;

// FastLED-style LED array
CRGB leds[LED_STRIP_LENGTH];

// FastLED-style color constants
#define CRGB_WHITE  {255, 255, 255}
#define CRGB_RED    {255, 0, 0}
#define CRGB_BLUE   {0, 0, 255}
#define CRGB_GREEN  {0, 255, 0}
#define CRGB_BLACK  {0, 0, 0}

// FastLED-style functions
void FastLED_show() {
    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
        ESP_ERROR_CHECK(strip->set_pixel(strip, i, leds[i].r, leds[i].g, leds[i].b));
    }
    ESP_ERROR_CHECK(strip->refresh(strip, 100));
}

void FastLED_setBrightness(uint8_t brightness) {
    // Apply brightness scaling to all LEDs
    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
        ESP_ERROR_CHECK(strip->set_pixel(strip, i,
            (leds[i].r * brightness) / 255,
            (leds[i].g * brightness) / 255,
            (leds[i].b * brightness) / 255));
    }
    ESP_ERROR_CHECK(strip->refresh(strip, 100));
}

CRGB CHSV_to_CRGB(uint8_t hue, uint8_t sat, uint8_t val) {
    CRGB rgb;
    hsv_to_rgb(hue, sat, val, &rgb.r, &rgb.g, &rgb.b);
    return rgb;
}

// Additional FastLED-style helper functions
void fill_solid(CRGB* leds, int num_leds, CRGB color) {
    for (int i = 0; i < num_leds; i++) {
        leds[i] = color;
    }
}

void fadeToBlackBy(CRGB* leds, int num_leds, uint8_t fadeBy) {
    for (int i = 0; i < num_leds; i++) {
        leds[i].r = (leds[i].r * (255 - fadeBy)) / 255;
        leds[i].g = (leds[i].g * (255 - fadeBy)) / 255;
        leds[i].b = (leds[i].b * (255 - fadeBy)) / 255;
    }
}

// Helper to create CRGB color from RGB values
CRGB CRGB_create(uint8_t r, uint8_t g, uint8_t b) {
    CRGB color = {r, g, b};
    return color;
}

void FastLED_begin()
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(LED_STRIP_GPIO, RMT_CHANNEL);
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(LED_STRIP_LENGTH, (led_strip_dev_t)config.channel);
    strip = led_strip_new_rmt_ws2812(&strip_config);

    if (!strip)
    {
        printf("install WS2812 driver failed\n");
        return;
    }

    // Initialize LED array to black
    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
        leds[i] = (CRGB)CRGB_BLACK;
    }

    // Clear LED strip (turn off all LEDs)
    FastLED_show();
}

// HSV to RGB conversion
void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region, remainder, p, q, t;

    if (s == 0)
    {
        *r = v;
        *g = v;
        *b = v;
        return;
    }

    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region)
    {
    case 0:
        *r = v;
        *g = t;
        *b = p;
        break;
    case 1:
        *r = q;
        *g = v;
        *b = p;
        break;
    case 2:
        *r = p;
        *g = v;
        *b = t;
        break;
    case 3:
        *r = p;
        *g = q;
        *b = v;
        break;
    case 4:
        *r = t;
        *g = p;
        *b = v;
        break;
    default:
        *r = v;
        *g = p;
        *b = q;
        break;
    }
}

void led_idle_animation()
{
    static uint8_t brightness = 0;
    static int8_t direction = 5;
    static uint32_t last_update = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (current_time - last_update >= 20)
    {
        brightness += direction;
        if (brightness >= 150 || brightness <= 10)
        {
            direction = -direction;
        }

        leds[0].r = brightness;
        leds[0].g = brightness;
        leds[0].b = brightness;
        FastLED_show();

        last_update = current_time;
    }
}

void led_wake_detected_animation()
{
    leds[0] = (CRGB)CRGB_WHITE;
    FastLED_show();
}

void led_listening_animation()
{
    static uint8_t brightness = 50;
    static int8_t direction = 5;

    brightness += direction;
    if (brightness >= 255 || brightness <= 50)
    {
        direction = -direction;
    }

    // Red for listening with varying brightness
    leds[0].r = brightness;
    leds[0].g = 0;
    leds[0].b = 0;
    FastLED_show();
}

void led_command_detected_animation()
{
    leds[0] = (CRGB)CRGB_GREEN;
    FastLED_show();
}

void led_countdown_red_animation()
{
    leds[0] = (CRGB)CRGB_RED;
    FastLED_show();
}

void led_countup_blue_animation()
{
    leds[0] = (CRGB)CRGB_BLUE;
    FastLED_show();
}

void led_task(void *arg)
{
    while (task_flag)
    {
        switch (led_state)
        {
        case 0: // idle
            led_idle_animation();
            break;
        case 1: // wake detected
            led_wake_detected_animation();
            break;
        case 2: // listening
            led_listening_animation();
            break;
        case 3: // command detected
            led_command_detected_animation();
            break;
        case 4: // countdown red
            led_countdown_red_animation();
            break;
        case 5: // countup blue
            led_countup_blue_animation();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

void play_music(void *arg)
{
    while (task_flag)
    {
        switch (play_voice)
        {
        case -2:
            vTaskDelay(10);
            break;
        case -1:
            wake_up_action();
            play_voice = -2;
            break;
        default:
            speech_commands_action(play_voice);
            play_voice = -2;
            break;
        }
    }
    vTaskDelete(NULL);
}

void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    assert(nch <= feed_channel);
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);

    while (task_flag)
    {
        esp_get_feed_data(false, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);

        afe_handle->feed(afe_data, i2s_buff);
    }
    if (i2s_buff)
    {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;

    // STEP 1: Define the lookup table for your command strings.
    // This array maps the command ID (0, 1, 2...) to its text.
    const char *command_strings[] = {
        "Unknown Command",                     // ID 0
        "Count down 10 minutes",               // ID 1
        "Count up 20 minutes"                  // ID 2
    };
    const int num_commands = sizeof(command_strings) / sizeof(command_strings[0]);

    // Standard model initialization
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    printf("multinet:%s\n", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    esp_mn_commands_update_from_sdkconfig(multinet, model_data);
    assert(mu_chunksize == afe_chunksize);
    multinet->print_active_speech_commands(model_data);

    printf("------------detect start------------\n");
    while (task_flag)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL)
        {
            printf("fetch error!\n");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            printf("WAKEWORD DETECTED\n");
            led_state = 1; // Wake detected
            multinet->clean(model_data);
        }
        else if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED)
        {
            play_voice = -1;
            detect_flag = 1;
            led_state = 2; // Listening for commands
            printf("AFE_FETCH_CHANNEL_VERIFIED, channel index: %d\n", res->trigger_channel_id);
        }

        if (detect_flag == 1)
        {
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING)
            {
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED)
            {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                if (mn_result && mn_result->num > 0)
                {

                    // STEP 2: Get the Command ID from the result.
                    int top_command_id = mn_result->command_id[0];
                    const char *top_command_string = "Unknown Command";

                    // STEP 3: Safely find the string in our array using the ID.
                    if (top_command_id >= 0 && top_command_id < num_commands)
                    {
                        top_command_string = command_strings[top_command_id];
                    }

                    // STEP 4: Print the string FROM OUR ARRAY, NOT from mn_result->string.
                    // This is the new, correct printf statement.
                    printf("---------------- DETECTED ----------------\n");
                    printf("TOP 1: ID: %d, String: \"%s\", Probability: %.2f%%\n",
                           top_command_id,
                           top_command_string, // Using our safe string
                           mn_result->prob[0] * 100);
                    printf("------------------------------------------\n");

                    play_voice = top_command_id;

                    if (top_command_id == 1) {
                        led_state = 4; // countdown red for "count down 10 minutes"
                    } else if (top_command_id == 2) {
                        led_state = 5; // countup blue for "count up 20 minutes"
                    } else {
                        led_state = 3; // default command detected
                    }
                }
                printf("-----------listening-----------\n");
                vTaskDelay(pdMS_TO_TICKS(2000)); // Show command LED for 2 seconds
                led_state = 0;                   // Back to idle
                detect_flag = 0;
                afe_handle->enable_wakenet(afe_data);
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT)
            {
                printf("timeout\n");
                led_state = 0; // Back to idle
                afe_handle->enable_wakenet(afe_data);
                detect_flag = 0;
                printf("\n-----------awaits to be waken up-----------\n");
            }
        }
    }

    // Cleanup
    if (model_data)
    {
        multinet->destroy(model_data);
        model_data = NULL;
    }
    printf("detect exit\n");
    vTaskDelete(NULL);
}

void app_main()
{
    // Initialize FastLED
    FastLED_begin();

    models = esp_srmodel_init("model");
    ESP_ERROR_CHECK(esp_board_init(AUDIO_HAL_16K_SAMPLES, 2, 16));
    uart_driver_delete(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0);
#if defined CONFIG_ESP32_KORVO_V1_1_BOARD
    led_init();
#endif

#if CONFIG_IDF_TARGET_ESP32
    printf("This demo only support ESP32S3\n");
    return;
#else
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
#endif

    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.wakenet_init = true;
    afe_config.aec_init = false;
    afe_config.pcm_config.total_ch_num = 2;
    afe_config.pcm_config.mic_num = 2;
    afe_config.pcm_config.ref_num = 0;
    afe_config.pcm_config.sample_rate = 16000;
    afe_config.wakenet_mode = DET_MODE_2CH_95;
    afe_config.afe_mode = SR_MODE_HIGH_PERF;
    afe_config.vad_mode = VAD_MODE_4;

    afe_config.wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);

#if defined CONFIG_ESP32_S3_BOX_BOARD || defined CONFIG_ESP32_S3_EYE_BOARD
    afe_config.aec_init = false;
#if defined CONFIG_ESP32_S3_EYE_BOARD
    afe_config.pcm_config.total_ch_num = 2;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 1;
#endif
#endif
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(&afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, (void *)afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void *)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(&led_task, "fastled", 2 * 1024, NULL, 3, NULL, 0);
#if defined CONFIG_ESP32_S3_KORVO_1_V4_0_BOARD
    xTaskCreatePinnedToCore(&led_Task, "led", 2 * 1024, NULL, 5, NULL, 0);
#endif
#if defined CONFIG_ESP32_S3_KORVO_1_V4_0_BOARD || CONFIG_ESP32_S3_KORVO_2_V3_0_BOARD || CONFIG_ESP32_KORVO_V1_1_BOARD || CONFIG_ESP32_S3_BOX_BOARD
    xTaskCreatePinnedToCore(&play_music, "play", 4 * 1024, NULL, 5, NULL, 1);
#endif
}