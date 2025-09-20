/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
// --- ESP32 Voice-Controlled LED Timer Ring ---
// Integrated Speech Recognition + Web Timer System
// Combines ESP-SR voice recognition with Chronos_mini timer functionality

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// ESP-SR Speech Recognition
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

// Networking and Web Server
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"


// LED Control
#include "driver/uart.h"
#include "led_strip.h"
#include "driver/rmt.h"

// Configuration
#define LED_STRIP_GPIO 8
#define LED_RING_LEDS 85  // Changed from 1 to 86 for ring
#define RMT_CHANNEL RMT_CHANNEL_0

// WiFi Configuration
#define WIFI_SSID ".Bird Fern Nest"
#define WIFI_PASS "violinfriend230"
#define WIFI_MAXIMUM_RETRY  5

// HTTP Server Configuration
#define CONFIG_WEB_MOUNT_POINT "/www"

// Global Variables
static const char *TAG = "VOICE_TIMER";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static httpd_handle_t server = NULL;

// Speech Recognition Variables
int detect_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
srmodel_list_t *models = NULL;
static int play_voice = -2;

// LED Variables
led_strip_t *strip = NULL;
static int led_state = 0; // 0=idle, 1=wake_detected, 2=listening, 3=command_detected, 4=timer_active

// Forward declarations
void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b);

// FastLED-style color structure
typedef struct {
    uint8_t r, g, b;
} CRGB;

// FastLED function forward declarations (after CRGB typedef)
CRGB CRGB_create(uint8_t r, uint8_t g, uint8_t b);
void FastLED_show();
void fill_solid(CRGB* leds, int num_leds, CRGB color);

// Integrated Timer State (combining Chronos_mini functionality)
typedef struct {
    bool active;
    bool isCountdown;
    bool useEndTime;
    bool paused;
    unsigned long startTimeMs;
    unsigned long pausedTimeMs;
    unsigned long totalDurationSec;
    CRGB primaryColor;
    CRGB segmentColor;
    CRGB endColor;
    int segments;
    bool useEndColor;
    int lastLedsLit;
    unsigned long lastFlashTime;
    bool flashActive;
    unsigned long endAnimationStartMs;
    bool endAnimationActive;
    char timerName[32];  // "workout", "laundry", etc.
} TimerState;

// Persistent settings structure for NVS storage
typedef struct {
    CRGB primaryColor;
    CRGB segmentColor;
    CRGB endColor;
    int segments;
    bool useEndColor;
    uint8_t brightness;
    char magic[8]; // "TIMER01" to validate settings
} TimerSettings;

// WiFi Event Bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Timer instance
TimerState timer = {0};

// FastLED-style LED array
CRGB leds[LED_RING_LEDS];

// FastLED-style color constants
#define CRGB_WHITE  {255, 255, 255}
#define CRGB_RED    {255, 0, 0}
#define CRGB_BLUE   {0, 0, 255}
#define CRGB_GREEN  {0, 255, 0}
#define CRGB_BLACK  {0, 0, 0}
#define CRGB_GOLD   {255, 215, 0}
#define CRGB_PURPLE {128, 0, 128}
#define CRGB_ORANGE {255, 165, 0}

// Speech Command Mapping (based on commands_en.txt)
typedef struct {
    int id;
    const char* command;
    const char* action;
    int duration_seconds;
    bool is_countdown;
} SpeechCommand;

static const SpeechCommand speech_commands[] = {
    {1, "STOP", "stop", 0, false},
    {2, "START", "start", 0, false},
    {3, "RESET THE TIMER", "reset", 0, false},
    {4, "TIMER ONE MINUTE", "timer", 60, true},
    {5, "TIMER TWO MINUTES", "timer", 120, true},
    {6, "TIMER THREE MINUTES", "timer", 180, true},
    {7, "TIMER FOUR MINUTES", "timer", 240, true},
    {8, "TIMER FIVE MINUTES", "timer", 300, true},
    {9, "TIMER SIX MINUTES", "timer", 360, true},
    {10, "TIMER SEVEN MINUTES", "timer", 420, true},
    {11, "TIMER EIGHT MINUTES", "timer", 480, true},
    {12, "TIMER NINE MINUTES", "timer", 540, true},
    {13, "TIMER TEN MINUTES", "timer", 600, true},
    {14, "TIMER FIFTEEN MINUTES", "timer", 900, true},
    {15, "TIMER TWENTY MINUTES", "timer", 1200, true},
    {16, "TIMER TWENTY FIVE MINUTES", "timer", 1500, true},
    {17, "TIMER THIRTY MINUTES", "timer", 1800, true},
    {18, "TIMER THIRTY FIVE MINUTES", "timer", 2100, true},
    {19, "TIMER FOURTY MINUTES", "timer", 2400, true},
    {20, "TIMER FOURTY FIVE MINUTES", "timer", 2700, true},
    {21, "TIMER FIFTY MINUTES", "timer", 3000, true},
    {22, "TIMER FIFTY FIVE MINUTES", "timer", 3300, true},
    {23, "TIMER ONE HOUR", "timer", 3600, true},
    {24, "TIMER HOUR AND A HALF", "timer", 5400, true},
    {25, "TIMER TWO HOURS", "timer", 7200, true},
    // Count up commands
    {40, "COUNT UP ONE MINUTE", "countup", 60, false},
    {41, "COUNT UP TWO MINUTES", "countup", 120, false},
    {42, "COUNT UP THREE MINUTES", "countup", 180, false},
    {43, "COUNT UP FOUR MINUTES", "countup", 240, false},
    {44, "COUNT UP FIVE MINUTES", "countup", 300, false},
    {45, "COUNT UP SIX MINUTES", "countup", 360, false},
    {46, "COUNT UP SEVEN MINUTES", "countup", 420, false},
    {47, "COUNT UP EIGHT MINUTES", "countup", 480, false},
    {48, "COUNT UP NINE MINUTES", "countup", 540, false},
    {49, "COUNT UP TEN MINUTES", "countup", 600, false},
    {50, "COUNT UP FIFTEEN MINUTES", "countup", 900, false},
    {51, "COUNT UP TWENTY MINUTES", "countup", 1200, false},
    {52, "COUNT UP TWENTY FIVE MINUTES", "countup", 1500, false},
    {53, "COUNT UP THIRTY MINUTES", "countup", 1800, false},
    // Control commands
    {76, "PAUSE", "pause", 0, false},
    {77, "PAUSE THE TIMER", "pause", 0, false},
    {78, "RESUME", "resume", 0, false},
    {79, "RESUME THE TIMER", "resume", 0, false},
    {80, "CONTINUE", "resume", 0, false},
    {81, "CANCEL", "cancel", 0, false},
    {82, "CANCEL THE TIMER", "cancel", 0, false},
    {83, "RESTART", "restart", 0, false},
    {84, "ADD ONE MINUTE", "add", 60, false},
    {85, "ADD FIVE MINUTES", "add", 300, false},
    {86, "ADD TEN MINUTES", "add", 600, false},
    {87, "ADD THIRTY SECONDS", "add", 30, false},
    // Special timers
    {96, "WORKOUT TIMER", "workout", 1800, true}, // 30 min default
    {97, "LAUNDRY TIMER", "laundry", 3600, true}, // 60 min default
    {98, "CLEAR TIMER", "clear", 0, false},
};

#define NUM_SPEECH_COMMANDS (sizeof(speech_commands) / sizeof(speech_commands[0]))

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, connecting to %s...", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "WiFi connection failed, retrying... (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed after %d attempts", WIFI_MAXIMUM_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected! IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize WiFi
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished");

    /* Wait until either connection is established (WIFI_CONNECTED_BIT) or connection failed */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi SSID: %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Unexpected WiFi event");
    }
}

// WiFi status monitoring task
void wifi_status_task(void *arg) {
    while (task_flag) {
        wifi_ap_record_t ap_info;
        esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WiFi Status: Connected to %s, RSSI: %d dBm, Channel: %d",
                     ap_info.ssid, ap_info.rssi, ap_info.primary);
        } else {
            ESP_LOGW(TAG, "WiFi Status: Disconnected");
        }

        vTaskDelay(pdMS_TO_TICKS(30000)); // Print status every 30 seconds
    }
    vTaskDelete(NULL);
}

// NVS Settings Management
void save_timer_settings(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("timer_settings", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    TimerSettings settings = {
        .primaryColor = timer.primaryColor,
        .segmentColor = timer.segmentColor,
        .endColor = timer.endColor,
        .segments = timer.segments,
        .useEndColor = timer.useEndColor,
        .brightness = 150, // Default brightness
    };
    strcpy(settings.magic, "TIMER01");

    err = nvs_set_blob(nvs_handle, "settings", &settings, sizeof(settings));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving settings: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Timer settings saved to NVS");
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        }
    }

    nvs_close(nvs_handle);
}

void load_timer_settings(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("timer_settings", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved settings found, using defaults");
        return;
    }

    TimerSettings settings;
    size_t required_size = sizeof(settings);
    err = nvs_get_blob(nvs_handle, "settings", &settings, &required_size);

    if (err == ESP_OK && strcmp(settings.magic, "TIMER01") == 0) {
        timer.primaryColor = settings.primaryColor;
        timer.segmentColor = settings.segmentColor;
        timer.endColor = settings.endColor;
        timer.segments = settings.segments;
        timer.useEndColor = settings.useEndColor;
        ESP_LOGI(TAG, "Timer settings loaded from NVS");
    } else {
        ESP_LOGW(TAG, "Invalid or corrupted settings, using defaults");
    }

    nvs_close(nvs_handle);
}

// Web Interface HTML (inspired by Chronos_mini)
static const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Voice LED Timer Ring</title>
    <style>
        :root{--bg-color:#1a1a1a;--card-color:#2b2b2b;--text-color:#f0f0f0;--primary-color:#007bff;--border-color:#444;--input-bg:#333;}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background-color:var(--bg-color);color:var(--text-color);margin:0;padding:1rem;display:flex;justify-content:center;align-items:flex-start;min-height:100vh;}
        .container{width:100%;max-width:500px;background-color:var(--card-color);border-radius:12px;padding:1.5rem;box-shadow:0 4px 20px rgba(0,0,0,0.25);}
        header{text-align:center;margin-bottom:1.5rem;border-bottom:1px solid var(--border-color);padding-bottom:1rem;}
        h1{margin:0;} #status{font-size:0.9rem;color:#888;margin-top:0.5rem;}
        .control-group{margin-bottom:1.5rem;border:1px solid var(--border-color);border-radius:8px;padding:1rem;}
        .control-group legend{padding:0 0.5rem;font-weight:bold;color:var(--primary-color);}
        .form-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:1rem;flex-wrap:wrap;}
        label{flex-basis:40%;margin-bottom:0.5rem;}
        input[type="number"],input[type="time"],input[type="color"],select{flex-basis:50%;padding:0.6rem;background-color:var(--input-bg);border:1px solid var(--border-color);color:var(--text-color);border-radius:6px;box-sizing:border-box;}
        input[type="color"]{height:45px;padding:0.2rem;} .radio-group{display:flex;gap:1rem;}
        button{width:100%;padding:0.8rem;font-size:1rem;font-weight:bold;border:none;border-radius:8px;cursor:pointer;transition:background-color 0.2s;margin-bottom:0.5rem;}
        .btn-start{background-color:var(--primary-color);color:white;} .btn-start:hover{background-color:#0056b3;}
        .btn-stop{background-color:#dc3545;color:white;} .btn-stop:hover{background-color:#c82333;}
        .btn-pause{background-color:#ffc107;color:black;} .btn-pause:hover{background-color:#e0a800;}
        .speech-commands{background-color:#28a745;color:white;font-size:0.9rem;padding:0.5rem;text-align:center;border-radius:6px;margin-top:1rem;}
        @media (max-width:480px){.form-row{flex-direction:column;align-items:stretch;} label,input{flex-basis:100%;} label{margin-bottom:0.5rem;}}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>🎤 Voice LED Timer Ring</h1>
            <div id="status">Ready for voice commands</div>
        </header>
        <main>
            <div class="control-group">
                <legend>Quick Timer</legend>
                <div class="form-row">
                    <label for="duration">Duration (minutes)</label>
                    <input type="number" id="duration" value="5" min="1" max="360">
                </div>
                <div class="form-row">
                    <div class="radio-group">
                        <input type="radio" id="modeCountdown" name="mode" value="countdown" checked>
                        <label for="modeCountdown">Countdown</label>
                    </div>
                    <div class="radio-group">
                        <input type="radio" id="modeCountup" name="mode" value="countup">
                        <label for="modeCountup">Count Up</label>
                    </div>
                </div>
            </div>

            <div class="control-group">
                <legend>LED Appearance</legend>
                <div class="form-row">
                    <label for="primaryColor">Primary Color</label>
                    <input type="color" id="primaryColor" value="#0066ff">
                </div>
                <div class="form-row">
                    <label for="endColor">End Color</label>
                    <input type="color" id="endColor" value="#ff0000">
                </div>
                <div class="form-row">
                    <label for="segmentColor">Segment Color</label>
                    <input type="color" id="segmentColor" value="#ffd700">
                </div>
                <div class="form-row">
                    <label for="segments">Segments</label>
                    <select id="segments">
                        <option value="1">1 Segment</option>
                        <option value="2">2 Segments</option>
                        <option value="4" selected>4 Segments</option>
                        <option value="6">6 Segments</option>
                        <option value="8">8 Segments</option>
                    </select>
                </div>
                <div class="form-row">
                    <label for="useEndColor">Color Gradient</label>
                    <input type="checkbox" id="useEndColor" checked>
                </div>
                <button onclick="saveSettings()" style="background-color:#17a2b8;color:white;">💾 Save Settings</button>
            </div>

            <button class="btn-start" onclick="startTimer()">▶️ Start Timer</button>
            <button class="btn-pause" onclick="pauseTimer()">⏸️ Pause/Resume</button>
            <button class="btn-stop" onclick="stopTimer()">⏹️ Stop Timer</button>

            <div class="speech-commands">
                🎙️ Say "Timer 5 minutes", "Pause", "Resume", "Stop", "Add 1 minute"<br>
                Also try: "Workout timer", "Laundry timer", "Count up 10 minutes"
            </div>
        </main>
    </div>

    <script>
        function hexToRgb(hex) {
            const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
            return result ? {
                r: parseInt(result[1], 16),
                g: parseInt(result[2], 16),
                b: parseInt(result[3], 16)
            } : null;
        }

        function startTimer() {
            const data = {
                command: "start",
                mode: document.querySelector('input[name="mode"]:checked').value,
                duration: parseInt(document.getElementById('duration').value),
                primaryColor: hexToRgb(document.getElementById('primaryColor').value),
                endColor: hexToRgb(document.getElementById('endColor').value),
                segmentColor: hexToRgb(document.getElementById('segmentColor').value),
                segments: parseInt(document.getElementById('segments').value),
                useEndColor: document.getElementById('useEndColor').checked
            };

            fetch('/api/timer', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            }).then(response => response.json())
              .then(data => console.log('Timer started:', data));
        }

        function pauseTimer() {
            fetch('/api/pause', {method: 'POST'})
                .then(response => response.json())
                .then(data => console.log('Timer paused/resumed:', data));
        }

        function stopTimer() {
            fetch('/api/stop', {method: 'POST'})
                .then(response => response.json())
                .then(data => console.log('Timer stopped:', data));
        }

        function saveSettings() {
            const data = {
                primaryColor: hexToRgb(document.getElementById('primaryColor').value),
                endColor: hexToRgb(document.getElementById('endColor').value),
                segmentColor: hexToRgb(document.getElementById('segmentColor').value),
                segments: parseInt(document.getElementById('segments').value),
                useEndColor: document.getElementById('useEndColor').checked
            };

            fetch('/api/settings', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            }).then(response => response.json())
              .then(data => {
                  console.log('Settings saved:', data);
                  document.getElementById('status').textContent = 'Settings saved to EEPROM!';
                  setTimeout(() => {
                      document.getElementById('status').textContent = 'Ready for voice commands';
                  }, 3000);
              });
        }

        // Load settings on page load
        fetch('/api/settings')
            .then(response => response.json())
            .then(data => {
                if (data.primaryColor) {
                    document.getElementById('primaryColor').value =
                        '#' + ((1 << 24) + (data.primaryColor.r << 16) + (data.primaryColor.g << 8) + data.primaryColor.b).toString(16).slice(1);
                }
                if (data.endColor) {
                    document.getElementById('endColor').value =
                        '#' + ((1 << 24) + (data.endColor.r << 16) + (data.endColor.g << 8) + data.endColor.b).toString(16).slice(1);
                }
                if (data.segmentColor) {
                    document.getElementById('segmentColor').value =
                        '#' + ((1 << 24) + (data.segmentColor.r << 16) + (data.segmentColor.g << 8) + data.segmentColor.b).toString(16).slice(1);
                }
                if (data.segments) {
                    document.getElementById('segments').value = data.segments;
                }
                if (data.useEndColor !== undefined) {
                    document.getElementById('useEndColor').checked = data.useEndColor;
                }
            });
    </script>
</body>
</html>
)rawliteral";

// HTTP Request Handlers
esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, strlen(index_html));
    return ESP_OK;
}

esp_err_t timer_api_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST) {
        char buf[1024];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        buf[ret] = '\0';

        cJSON *json = cJSON_Parse(buf);
        if (json) {
            cJSON *command = cJSON_GetObjectItem(json, "command");
            if (command && strcmp(command->valuestring, "start") == 0) {
                // Extract timer parameters from JSON
                cJSON *mode = cJSON_GetObjectItem(json, "mode");
                cJSON *duration = cJSON_GetObjectItem(json, "duration");
                cJSON *primaryColor = cJSON_GetObjectItem(json, "primaryColor");
                cJSON *endColor = cJSON_GetObjectItem(json, "endColor");
                cJSON *segmentColor = cJSON_GetObjectItem(json, "segmentColor");
                cJSON *segments = cJSON_GetObjectItem(json, "segments");
                cJSON *useEndColor = cJSON_GetObjectItem(json, "useEndColor");

                // Start timer with web parameters
                timer.active = true;
                timer.isCountdown = (mode && strcmp(mode->valuestring, "countdown") == 0);
                timer.paused = false;
                timer.totalDurationSec = duration ? duration->valueint * 60 : 300; // Default 5 min
                timer.startTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;

                // Apply colors if provided
                if (primaryColor) {
                    cJSON *r = cJSON_GetObjectItem(primaryColor, "r");
                    cJSON *g = cJSON_GetObjectItem(primaryColor, "g");
                    cJSON *b = cJSON_GetObjectItem(primaryColor, "b");
                    if (r && g && b) {
                        timer.primaryColor = CRGB_create(r->valueint, g->valueint, b->valueint);
                    }
                }
                if (endColor) {
                    cJSON *r = cJSON_GetObjectItem(endColor, "r");
                    cJSON *g = cJSON_GetObjectItem(endColor, "g");
                    cJSON *b = cJSON_GetObjectItem(endColor, "b");
                    if (r && g && b) {
                        timer.endColor = CRGB_create(r->valueint, g->valueint, b->valueint);
                    }
                }
                if (segmentColor) {
                    cJSON *r = cJSON_GetObjectItem(segmentColor, "r");
                    cJSON *g = cJSON_GetObjectItem(segmentColor, "g");
                    cJSON *b = cJSON_GetObjectItem(segmentColor, "b");
                    if (r && g && b) {
                        timer.segmentColor = CRGB_create(r->valueint, g->valueint, b->valueint);
                    }
                }

                timer.segments = segments ? segments->valueint : 4;
                timer.useEndColor = useEndColor ? cJSON_IsTrue(useEndColor) : true;

                strncpy(timer.timerName, "web_timer", sizeof(timer.timerName) - 1);
                led_state = 4; // Timer active

                ESP_LOGI(TAG, "Web timer started: %lu seconds, mode: %s",
                         timer.totalDurationSec, timer.isCountdown ? "countdown" : "countup");
            }
            cJSON_Delete(json);
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", 15);
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pause_api_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST) {
        if (timer.active) {
            if (timer.paused) {
                // Resume
                unsigned long pauseDuration = (xTaskGetTickCount() * portTICK_PERIOD_MS) - timer.pausedTimeMs;
                timer.startTimeMs += pauseDuration;
                timer.paused = false;
                ESP_LOGI(TAG, "Web timer resumed");
            } else {
                // Pause
                timer.paused = true;
                timer.pausedTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "Web timer paused");
            }
        }

        httpd_resp_set_type(req, "application/json");
        const char* response = timer.paused ? "{\"status\":\"paused\"}" : "{\"status\":\"resumed\"}";
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t stop_api_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST) {
        timer.active = false;
        timer.paused = false;
        timer.endAnimationActive = false;
        fill_solid(leds, LED_RING_LEDS, (CRGB)CRGB_BLACK);
        FastLED_show();
        led_state = 0; // back to idle
        ESP_LOGI(TAG, "Web timer stopped");

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"stopped\"}", 18);
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t settings_api_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST) {
        // Save settings
        char buf[1024];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        buf[ret] = '\0';

        cJSON *json = cJSON_Parse(buf);
        if (json) {
            cJSON *primaryColor = cJSON_GetObjectItem(json, "primaryColor");
            cJSON *endColor = cJSON_GetObjectItem(json, "endColor");
            cJSON *segmentColor = cJSON_GetObjectItem(json, "segmentColor");
            cJSON *segments = cJSON_GetObjectItem(json, "segments");
            cJSON *useEndColor = cJSON_GetObjectItem(json, "useEndColor");

            // Update timer settings
            if (primaryColor) {
                cJSON *r = cJSON_GetObjectItem(primaryColor, "r");
                cJSON *g = cJSON_GetObjectItem(primaryColor, "g");
                cJSON *b = cJSON_GetObjectItem(primaryColor, "b");
                if (r && g && b) {
                    timer.primaryColor = CRGB_create(r->valueint, g->valueint, b->valueint);
                }
            }
            if (endColor) {
                cJSON *r = cJSON_GetObjectItem(endColor, "r");
                cJSON *g = cJSON_GetObjectItem(endColor, "g");
                cJSON *b = cJSON_GetObjectItem(endColor, "b");
                if (r && g && b) {
                    timer.endColor = CRGB_create(r->valueint, g->valueint, b->valueint);
                }
            }
            if (segmentColor) {
                cJSON *r = cJSON_GetObjectItem(segmentColor, "r");
                cJSON *g = cJSON_GetObjectItem(segmentColor, "g");
                cJSON *b = cJSON_GetObjectItem(segmentColor, "b");
                if (r && g && b) {
                    timer.segmentColor = CRGB_create(r->valueint, g->valueint, b->valueint);
                }
            }
            if (segments) {
                timer.segments = segments->valueint;
            }
            if (useEndColor) {
                timer.useEndColor = cJSON_IsTrue(useEndColor);
            }

            // Save to NVS
            save_timer_settings();

            cJSON_Delete(json);
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"saved\"}", 17);
        return ESP_OK;

    } else if (req->method == HTTP_GET) {
        // Load and return current settings
        cJSON *response = cJSON_CreateObject();

        cJSON *primaryColor = cJSON_CreateObject();
        cJSON_AddNumberToObject(primaryColor, "r", timer.primaryColor.r);
        cJSON_AddNumberToObject(primaryColor, "g", timer.primaryColor.g);
        cJSON_AddNumberToObject(primaryColor, "b", timer.primaryColor.b);
        cJSON_AddItemToObject(response, "primaryColor", primaryColor);

        cJSON *endColor = cJSON_CreateObject();
        cJSON_AddNumberToObject(endColor, "r", timer.endColor.r);
        cJSON_AddNumberToObject(endColor, "g", timer.endColor.g);
        cJSON_AddNumberToObject(endColor, "b", timer.endColor.b);
        cJSON_AddItemToObject(response, "endColor", endColor);

        cJSON *segmentColor = cJSON_CreateObject();
        cJSON_AddNumberToObject(segmentColor, "r", timer.segmentColor.r);
        cJSON_AddNumberToObject(segmentColor, "g", timer.segmentColor.g);
        cJSON_AddNumberToObject(segmentColor, "b", timer.segmentColor.b);
        cJSON_AddItemToObject(response, "segmentColor", segmentColor);

        cJSON_AddNumberToObject(response, "segments", timer.segments);
        cJSON_AddBoolToObject(response, "useEndColor", timer.useEndColor);

        char *json_string = cJSON_Print(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));

        free(json_string);
        cJSON_Delete(response);
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

// Start web server
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Root handler
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        // Timer API
        httpd_uri_t timer_uri = {
            .uri = "/api/timer",
            .method = HTTP_POST,
            .handler = timer_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &timer_uri);

        // Pause API
        httpd_uri_t pause_uri = {
            .uri = "/api/pause",
            .method = HTTP_POST,
            .handler = pause_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &pause_uri);

        // Stop API
        httpd_uri_t stop_uri = {
            .uri = "/api/stop",
            .method = HTTP_POST,
            .handler = stop_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &stop_uri);

        // Settings API (GET and POST)
        httpd_uri_t settings_get_uri = {
            .uri = "/api/settings",
            .method = HTTP_GET,
            .handler = settings_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &settings_get_uri);

        httpd_uri_t settings_post_uri = {
            .uri = "/api/settings",
            .method = HTTP_POST,
            .handler = settings_api_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &settings_post_uri);

        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    }
    return server;
}

// FastLED-style functions
void FastLED_show() {
    for (int i = 0; i < LED_RING_LEDS; i++) {
        ESP_ERROR_CHECK(strip->set_pixel(strip, i, leds[i].r, leds[i].g, leds[i].b));
    }
    ESP_ERROR_CHECK(strip->refresh(strip, 100));
}

void FastLED_setBrightness(uint8_t brightness) {
    // Apply brightness scaling to all LEDs
    for (int i = 0; i < LED_RING_LEDS; i++) {
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

CRGB blend(CRGB color1, CRGB color2, uint8_t ratio) {
    CRGB result;
    result.r = ((color1.r * (255 - ratio)) + (color2.r * ratio)) / 255;
    result.g = ((color1.g * (255 - ratio)) + (color2.g * ratio)) / 255;
    result.b = ((color1.b * (255 - ratio)) + (color2.b * ratio)) / 255;
    return result;
}

void fill_rainbow(CRGB* leds, int num_leds, uint8_t initial_hue, uint8_t delta_hue) {
    for (int i = 0; i < num_leds; i++) {
        leds[i] = CHSV_to_CRGB(initial_hue + (i * delta_hue), 255, 255);
    }
}

// Helper to create CRGB color from RGB values
CRGB CRGB_create(uint8_t r, uint8_t g, uint8_t b) {
    CRGB color = {r, g, b};
    return color;
}

// Speech Command Processing
const SpeechCommand* find_speech_command(int command_id) {
    for (int i = 0; i < NUM_SPEECH_COMMANDS; i++) {
        if (speech_commands[i].id == command_id) {
            return &speech_commands[i];
        }
    }
    return NULL;
}

void process_speech_command(int command_id) {
    const SpeechCommand* cmd = find_speech_command(command_id);
    if (!cmd) {
        ESP_LOGW(TAG, "Unknown command ID: %d", command_id);
        return;
    }

    ESP_LOGI(TAG, "Processing command: %s (%s)", cmd->command, cmd->action);

    if (strcmp(cmd->action, "timer") == 0) {
        // Start countdown timer
        timer.active = true;
        timer.isCountdown = true;
        timer.paused = false;
        timer.totalDurationSec = cmd->duration_seconds;
        timer.startTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
        timer.primaryColor = (CRGB)CRGB_BLUE;
        timer.endColor = (CRGB)CRGB_RED;
        timer.useEndColor = true;
        timer.segments = 4; // Default 4 segments
        timer.segmentColor = (CRGB)CRGB_GOLD;
        strncpy(timer.timerName, "voice_timer", sizeof(timer.timerName) - 1);
        led_state = 4; // timer_active
        ESP_LOGI(TAG, "Started %d second countdown timer", cmd->duration_seconds);

    } else if (strcmp(cmd->action, "countup") == 0) {
        // Start count-up timer
        timer.active = true;
        timer.isCountdown = false;
        timer.paused = false;
        timer.totalDurationSec = cmd->duration_seconds;
        timer.startTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
        timer.primaryColor = (CRGB)CRGB_GREEN;
        timer.endColor = (CRGB)CRGB_PURPLE;
        timer.useEndColor = true;
        timer.segments = 4;
        timer.segmentColor = (CRGB)CRGB_GOLD;
        strncpy(timer.timerName, "voice_countup", sizeof(timer.timerName) - 1);
        led_state = 4;
        ESP_LOGI(TAG, "Started %d second count-up timer", cmd->duration_seconds);

    } else if (strcmp(cmd->action, "pause") == 0) {
        if (timer.active && !timer.paused) {
            timer.paused = true;
            timer.pausedTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
            ESP_LOGI(TAG, "Timer paused");
        }

    } else if (strcmp(cmd->action, "resume") == 0) {
        if (timer.active && timer.paused) {
            // Adjust start time to account for pause duration
            unsigned long pauseDuration = (xTaskGetTickCount() * portTICK_PERIOD_MS) - timer.pausedTimeMs;
            timer.startTimeMs += pauseDuration;
            timer.paused = false;
            ESP_LOGI(TAG, "Timer resumed");
        }

    } else if (strcmp(cmd->action, "stop") == 0 || strcmp(cmd->action, "cancel") == 0 || strcmp(cmd->action, "clear") == 0) {
        timer.active = false;
        timer.paused = false;
        timer.endAnimationActive = false;
        fill_solid(leds, LED_RING_LEDS, (CRGB)CRGB_BLACK);
        FastLED_show();
        led_state = 0; // back to idle
        ESP_LOGI(TAG, "Timer stopped/cancelled");

    } else if (strcmp(cmd->action, "add") == 0) {
        if (timer.active) {
            timer.totalDurationSec += cmd->duration_seconds;
            ESP_LOGI(TAG, "Added %d seconds to timer", cmd->duration_seconds);
        }

    } else if (strcmp(cmd->action, "workout") == 0) {
        // Special workout timer with orange theme
        timer.active = true;
        timer.isCountdown = true;
        timer.paused = false;
        timer.totalDurationSec = cmd->duration_seconds;
        timer.startTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
        timer.primaryColor = (CRGB)CRGB_ORANGE;
        timer.endColor = (CRGB)CRGB_RED;
        timer.useEndColor = true;
        timer.segments = 6; // More segments for workout
        timer.segmentColor = (CRGB)CRGB_WHITE;
        strncpy(timer.timerName, "workout", sizeof(timer.timerName) - 1);
        led_state = 4;
        ESP_LOGI(TAG, "Started workout timer: %d seconds", cmd->duration_seconds);

    } else if (strcmp(cmd->action, "laundry") == 0) {
        // Special laundry timer with blue theme
        timer.active = true;
        timer.isCountdown = true;
        timer.paused = false;
        timer.totalDurationSec = cmd->duration_seconds;
        timer.startTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
        timer.primaryColor = (CRGB)CRGB_BLUE;
        timer.endColor = (CRGB)CRGB_GREEN;
        timer.useEndColor = true;
        timer.segments = 4;
        timer.segmentColor = (CRGB)CRGB_WHITE;
        strncpy(timer.timerName, "laundry", sizeof(timer.timerName) - 1);
        led_state = 4;
        ESP_LOGI(TAG, "Started laundry timer: %d seconds", cmd->duration_seconds);
    }
}

void FastLED_begin()
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(LED_STRIP_GPIO, RMT_CHANNEL);
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(LED_RING_LEDS, (led_strip_dev_t)config.channel);
    strip = led_strip_new_rmt_ws2812(&strip_config);

    if (!strip)
    {
        ESP_LOGE(TAG, "Failed to install WS2812 driver");
        return;
    }

    // Initialize LED array to black
    fill_solid(leds, LED_RING_LEDS, (CRGB)CRGB_BLACK);
    FastLED_show();

    ESP_LOGI(TAG, "FastLED initialized with %d LEDs on GPIO %d", LED_RING_LEDS, LED_STRIP_GPIO);
}

// LED Ring Timer Visualization (adapted from Chronos_mini)
void update_timer_leds() {
    if (!timer.active || timer.endAnimationActive) return;

    // Handle pause state
    if (timer.paused) {
        // Slow pulse effect when paused
        static uint8_t pulse_brightness = 0;
        static int8_t pulse_direction = 5;
        pulse_brightness += pulse_direction;
        if (pulse_brightness >= 200 || pulse_brightness <= 50) {
            pulse_direction = -pulse_direction;
        }

        for (int i = 0; i < LED_RING_LEDS; i++) {
            leds[i].r = (timer.primaryColor.r * pulse_brightness) / 255;
            leds[i].g = (timer.primaryColor.g * pulse_brightness) / 255;
            leds[i].b = (timer.primaryColor.b * pulse_brightness) / 255;
        }
        FastLED_show();
        return;
    }

    // Handle flash state for segment markers
    if (timer.flashActive) {
        if ((xTaskGetTickCount() * portTICK_PERIOD_MS) - timer.lastFlashTime > 1000) {
            timer.flashActive = false;
        } else {
            return; // Hold flash color
        }
    }

    unsigned long elapsedMs = (xTaskGetTickCount() * portTICK_PERIOD_MS) - timer.startTimeMs;
    float progress = (float)elapsedMs / (float)(timer.totalDurationSec * 1000);
    if (progress > 1.0f) progress = 1.0f;

    int ledsToShow = (int)round(progress * LED_RING_LEDS);

    // Segment Flash Trigger Logic
    if (ledsToShow > timer.lastLedsLit) {
        for (int i = 1; i < timer.segments; i++) {
            int segmentLedIndex = (LED_RING_LEDS * i) / timer.segments;
            if (timer.lastLedsLit < segmentLedIndex && ledsToShow >= segmentLedIndex) {
                timer.flashActive = true;
                timer.lastFlashTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
                fill_solid(leds, LED_RING_LEDS, timer.segmentColor);
                FastLED_show();
                break;
            }
        }
    }
    timer.lastLedsLit = ledsToShow;

    if (timer.flashActive) return; // Don't redraw if we just started a flash

    // Normal LED Drawing Logic
    for (int i = 0; i < LED_RING_LEDS; i++) {
        CRGB currentColor = timer.primaryColor;
        if (timer.useEndColor) {
            currentColor = blend(timer.primaryColor, timer.endColor, progress * 255);
        }

        // Check if this LED is a segment marker
        bool isSegmentMarker = false;
        for (int j = 1; j < timer.segments; j++) {
            if (i == (LED_RING_LEDS * j) / timer.segments) {
                isSegmentMarker = true;
                break;
            }
        }

        if (timer.isCountdown) {
            // Countdown: LEDs turn off as time progresses
            leds[i] = (i < ledsToShow) ? (CRGB)CRGB_BLACK :
                      (isSegmentMarker ? timer.segmentColor : currentColor);
        } else {
            // Count up: LEDs turn on as time progresses
            leds[i] = (i < ledsToShow) ?
                      (isSegmentMarker ? timer.segmentColor : currentColor) :
                      (CRGB)CRGB_BLACK;
        }
    }
    FastLED_show();
}

void handle_timer_end_animation() {
    if (!timer.endAnimationActive) return;

    unsigned long elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - timer.endAnimationStartMs;
    if (elapsed > 5000) { // 5 second animation
        timer.active = false;
        timer.endAnimationActive = false;
        fill_solid(leds, LED_RING_LEDS, (CRGB)CRGB_BLACK);
        FastLED_show();
        led_state = 0; // back to idle
        ESP_LOGI(TAG, "Timer completed and reset");
        return;
    }

    // Rainbow animation
    fill_rainbow(leds, LED_RING_LEDS, (elapsed / 20) % 255, 7);
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
    static int8_t direction = 2;
    static uint32_t last_update = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (current_time - last_update >= 50) // Slower update rate for calmer effect
    {
        brightness += direction;
        if (brightness >= 80 || brightness <= 5) // Lower max brightness for calm effect
        {
            direction = -direction;
        }

        // Calm white pulse on all LEDs with gentle breathing effect
        for (int i = 0; i < LED_RING_LEDS; i++) {
            leds[i].r = brightness;
            leds[i].g = brightness;
            leds[i].b = brightness;
        }
        FastLED_show();

        last_update = current_time;
    }
}

void led_wake_detected_animation()
{
    // Solid white ring to indicate wake word detected
    fill_solid(leds, LED_RING_LEDS, (CRGB)CRGB_WHITE);
    FastLED_show();
}

void led_listening_animation()
{
    static uint8_t brightness = 50;
    static int8_t direction = 8;

    brightness += direction;
    if (brightness >= 200 || brightness <= 30)
    {
        direction = -direction;
    }

    // Red breathing effect while listening for commands
    for (int i = 0; i < LED_RING_LEDS; i++) {
        leds[i].r = brightness;
        leds[i].g = 0;
        leds[i].b = 0;
    }
    FastLED_show();
}

void led_command_detected_animation()
{
    // Quick green flash to indicate command was recognized
    fill_solid(leds, LED_RING_LEDS, (CRGB)CRGB_GREEN);
    FastLED_show();
}

void led_task(void *arg)
{
    while (task_flag)
    {
        switch (led_state)
        {
        case 0: // idle - slow white breathing on a few LEDs
            // Clear all LEDs first
            fill_solid(leds, LED_RING_LEDS, (CRGB)CRGB_BLACK);
            led_idle_animation();
            break;
        case 1: // wake detected - solid white ring
            led_wake_detected_animation();
            break;
        case 2: // listening - red breathing
            led_listening_animation();
            break;
        case 3: // command detected - green flash
            led_command_detected_animation();
            break;
        case 4: // timer active - use timer visualization
            if (timer.endAnimationActive) {
                handle_timer_end_animation();
            } else {
                update_timer_leds();
            }
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

    // Standard model initialization
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    ESP_LOGI(TAG, "Using multinet model: %s", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    esp_mn_commands_update_from_sdkconfig(multinet, model_data);
    assert(mu_chunksize == afe_chunksize);
    multinet->print_active_speech_commands(model_data);

    ESP_LOGI(TAG, "Speech detection started - %d commands available", NUM_SPEECH_COMMANDS);
    while (task_flag)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL)
        {
            ESP_LOGE(TAG, "AFE fetch error!");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            ESP_LOGI(TAG, "WAKE WORD DETECTED");
            led_state = 1; // Wake detected - solid white
            multinet->clean(model_data);
        }
        else if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED)
        {
            play_voice = -1;
            detect_flag = 1;
            led_state = 2; // Listening for commands - red breathing
            ESP_LOGI(TAG, "Channel verified, listening for commands (channel: %d)", res->trigger_channel_id);
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
                    int top_command_id = mn_result->command_id[0];
                    float confidence = mn_result->prob[0] * 100;

                    // Find command in our comprehensive list
                    const SpeechCommand* cmd = find_speech_command(top_command_id);
                    const char* command_name = cmd ? cmd->command : "Unknown Command";

                    ESP_LOGI(TAG, "COMMAND DETECTED: ID=%d, Command='%s', Confidence=%.1f%%",
                             top_command_id, command_name, confidence);

                    play_voice = top_command_id;

                    // Process the speech command using our integrated system
                    if (cmd) {
                        process_speech_command(top_command_id);
                        led_state = 3; // Command detected - green flash
                        vTaskDelay(pdMS_TO_TICKS(1000)); // Show green for 1 second
                    } else {
                        ESP_LOGW(TAG, "Unrecognized command ID: %d", top_command_id);
                        led_state = 3; // Unknown command - green flash
                        vTaskDelay(pdMS_TO_TICKS(500));
                    }
                }

                // Return to appropriate state
                if (timer.active) {
                    led_state = 4; // Timer is active
                } else {
                    led_state = 0; // Back to idle
                }

                detect_flag = 0;
                afe_handle->enable_wakenet(afe_data);
                ESP_LOGI(TAG, "Ready for next wake word");
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

// Timer monitoring task
void timer_monitor_task(void *arg) {
    while (task_flag) {
        if (timer.active && !timer.endAnimationActive && !timer.paused) {
            unsigned long elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - timer.startTimeMs;
            if (elapsed >= timer.totalDurationSec * 1000) {
                ESP_LOGI(TAG, "Timer '%s' completed! Starting end animation", timer.timerName);
                timer.endAnimationActive = true;
                timer.endAnimationStartMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }
    vTaskDelete(NULL);
}

void app_main()
{
    ESP_LOGI(TAG, "Starting Voice-Controlled LED Timer Ring");

    // Initialize NVS for WiFi and timer settings storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized for WiFi and timer settings");

    // Initialize FastLED
    FastLED_begin();

    // Initialize default timer state
    timer.primaryColor = (CRGB)CRGB_BLUE;
    timer.endColor = (CRGB)CRGB_RED;
    timer.segmentColor = (CRGB)CRGB_GOLD;
    timer.segments = 4;
    timer.useEndColor = true;

    // Load saved settings from NVS
    load_timer_settings();

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_init_sta();

    // Start web server
    ESP_LOGI(TAG, "Starting web server...");
    start_webserver();

    ESP_LOGI(TAG, "Initializing ESP-SR models");
    models = esp_srmodel_init("model");
    ESP_ERROR_CHECK(esp_board_init(AUDIO_HAL_16K_SAMPLES, 2, 16));
    uart_driver_delete(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0);

#if defined CONFIG_ESP32_KORVO_V1_1_BOARD
    led_init();
#endif

#if CONFIG_IDF_TARGET_ESP32
    ESP_LOGE(TAG, "This demo only supports ESP32S3");
    return;
#else
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
#endif

    ESP_LOGI(TAG, "Configuring audio front-end");
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

    ESP_LOGI(TAG, "Starting tasks...");
    task_flag = 1;

    // Core tasks
    xTaskCreatePinnedToCore(&detect_Task, "speech_detect", 8 * 1024, (void *)afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(&feed_Task, "audio_feed", 8 * 1024, (void *)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(&led_task, "led_control", 4 * 1024, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(&timer_monitor_task, "timer_monitor", 2 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(&wifi_status_task, "wifi_status", 4 * 1024, NULL, 1, NULL, 1);

#if defined CONFIG_ESP32_S3_KORVO_1_V4_0_BOARD
    xTaskCreatePinnedToCore(&led_Task, "led", 2 * 1024, NULL, 5, NULL, 0);
#endif
#if defined CONFIG_ESP32_S3_KORVO_1_V4_0_BOARD || CONFIG_ESP32_S3_KORVO_2_V3_0_BOARD || CONFIG_ESP32_KORVO_V1_1_BOARD || CONFIG_ESP32_S3_BOX_BOARD
    xTaskCreatePinnedToCore(&play_music, "play", 4 * 1024, NULL, 5, NULL, 1);
#endif

    ESP_LOGI(TAG, "Voice-Controlled LED Timer Ring ready!");
    ESP_LOGI(TAG, "Say wake word to start. Available commands: %d", NUM_SPEECH_COMMANDS);
    ESP_LOGI(TAG, "LED Ring: %d LEDs on GPIO %d", LED_RING_LEDS, LED_STRIP_GPIO);
}