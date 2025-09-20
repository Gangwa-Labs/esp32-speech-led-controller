# ESP32 Voice-Controlled LED Timer Ring

A comprehensive ESP32-S3 speech recognition system that combines voice control with web-based timer functionality. Features an 85-LED WS2812 ring that visualizes timers with dynamic color transitions, segments, and real-time progress indication.

## 🎯 Overview

This project integrates ESP-SR speech recognition with a Chronos-inspired LED timer system, creating a powerful voice and web-controlled timer with rich visual feedback. The system recognizes 98 different voice commands and provides a modern web interface for timer customization.

## ✨ Key Features

### 🎤 Advanced Speech Recognition
- **98 Voice Commands**: Complete timer control vocabulary using ESP-SR multinet
- **Wake Word Detection**: Hands-free activation with audio feedback
- **Command Categories**:
  - Timer durations (1 minute to full day)
  - Count-up timers (1 minute to full day)
  - Control commands (pause, resume, stop, cancel)
  - Special timers (workout, laundry)
  - Timer modifications (add time, restart)

### 💫 LED Ring Visualization
- **85-LED WS2812 Ring**: Full 360° timer progress visualization
- **Dynamic Color Transitions**: Smooth color blending from start to end colors
- **Segment Markers**: Configurable segments with flash notifications
- **Multiple Animation States**:
  - Calm white pulse (waiting for wake word)
  - Solid white (wake word detected)
  - Red breathing (listening for commands)
  - Green flash (command confirmed)
  - Timer progress visualization
  - Rainbow completion animation

### 🌐 Web Interface
- **Real-time Timer Control**: Start, pause, resume, stop via web
- **Color Customization**: RGB color pickers for primary, end, and segment colors
- **Timer Configuration**: Adjustable segments, duration, and display options
- **Persistent Settings**: All customizations saved to NVS storage
- **Mobile-Friendly**: Responsive design works on all devices

### 🔧 Technical Features
- **FastLED-Compatible Interface**: Familiar Arduino-style LED control
- **WiFi Connectivity**: Automatic connection with status monitoring
- **NVS Storage**: Persistent settings for colors, segments, and preferences
- **Multi-Core Processing**: Optimized task distribution across CPU cores
- **Memory Management**: Efficient use of PSRAM and internal memory

## 🛠 Hardware Requirements

- **ESP32-S3 Development Board** (with PSRAM recommended)
- **85x WS2812 Addressable LEDs** arranged in a ring configuration
- **Microphone** compatible with ESP-SR (I2S or built-in)
- **Power Supply** capable of driving 85 LEDs (5V, 3-5A recommended)

## 📋 Software Dependencies

- **ESP-IDF v5.5+**: Core framework
- **ESP-SR**: Speech recognition library with multinet models
- **Components**:
  - `led_strip`: WS2812 LED control
  - `esp_http_server`: Web interface
  - `esp_wifi`: Network connectivity
  - `nvs_flash`: Persistent storage
  - `cJSON`: JSON parsing for web API

## 🎯 Voice Commands

### Timer Commands
```
"Timer [1-60] minutes"    - Start countdown timer
"Timer [1-6] hours"       - Start countdown timer
"Timer full day"          - 24-hour countdown
"Count up [duration]"     - Start count-up timer
```

### Control Commands
```
"Stop" / "Cancel"         - Stop current timer
"Pause" / "Pause timer"   - Pause active timer
"Resume" / "Continue"     - Resume paused timer
"Restart"                 - Restart current timer
"Reset the timer"         - Reset and stop timer
```

### Special Timers
```
"Workout timer"           - Special workout mode
"Laundry timer"           - Laundry-specific timer
"Add [1/5/10] minutes"    - Extend active timer
```

## 🌈 LED States & Behaviors

| State | LED Pattern | Color | Description |
|-------|-------------|-------|-------------|
| **Idle** | Slow pulse | White | Waiting for wake word |
| **Wake Detected** | Solid | White | Ready for command |
| **Listening** | Breathing | Red | Processing speech |
| **Command Confirmed** | Flash | Green | Command accepted |
| **Timer Active** | Progress arc | Configurable | Timer visualization |
| **Timer Paused** | Slow pulse | Timer color | Paused state |
| **Timer Complete** | Rainbow cycle | Multi-color | Completion celebration |

## 🌐 Web Interface Features

### Timer Control Panel
- **Start/Stop Buttons**: Instant timer control
- **Duration Selector**: Quick time selection
- **Real-time Display**: Live timer status and remaining time

### Customization Options
- **Primary Color**: Main timer color (RGB picker)
- **End Color**: Color at timer completion (RGB picker)
- **Segment Color**: Marker color for time segments (RGB picker)
- **Segment Count**: Number of visual segments (1-12)
- **Gradient Mode**: Enable smooth color transitions
- **Brightness Control**: LED intensity adjustment

### Settings Persistence
All web interface customizations are automatically saved to NVS storage and persist across reboots.

## 🚀 Installation & Setup

### 1. Clone Repository
```bash
git clone [repository-url]
cd esp32-voice-timer-ring
```

### 2. ESP-IDF Setup
```bash
# Install ESP-IDF v5.5+
# Set up environment
. $HOME/esp/esp-idf/export.sh
```

### 3. Configure Project
```bash
# Set target
idf.py set-target esp32s3

# Copy default configuration
cp sdkconfig.defaults.esp32s3 sdkconfig

# Configure WiFi credentials
idf.py menuconfig
# Navigate to: Component config → WiFi Configuration
```

### 4. Build & Flash
```bash
# Build project
idf.py build

# Flash and monitor
idf.py -b 2000000 flash monitor
```

## ⚙️ Configuration

### LED Configuration
```c
// In main/main.c
#define LED_STRIP_GPIO 8      // GPIO pin for LED data
#define LED_RING_LEDS 85      // Number of LEDs in ring
```

### WiFi Configuration
Update WiFi credentials in `main/main.c`:
```c
#define WIFI_SSID "your-network-name"
#define WIFI_PASS "your-password"
```

### Speech Recognition Models
The project uses the multinet models included with ESP-SR for comprehensive command recognition.

## 🏗 Project Structure

```
├── main/
│   ├── main.c                 # Main application code
│   ├── speech_commands_action.c # Speech command processing
│   └── CMakeLists.txt         # Build configuration
├── partitions.csv             # Flash partition table
├── sdkconfig.defaults.esp32s3 # Default ESP32-S3 config
└── README.md                  # This documentation
```

## 🔧 API Endpoints

### REST API
- `GET /` - Main web interface
- `POST /api/timer` - Start timer with JSON config
- `POST /api/pause` - Pause/resume timer
- `POST /api/stop` - Stop current timer
- `GET/POST /api/settings` - Timer customization settings

### JSON Configuration Example
```json
{
  "duration": 300,
  "isCountdown": true,
  "primaryColor": {"r": 0, "g": 100, "b": 255},
  "endColor": {"r": 255, "g": 0, "b": 0},
  "segments": 4,
  "useEndColor": true
}
```

## 🎨 FastLED-Style Interface

The project includes a complete FastLED-compatible API for easy LED programming:

```c
// Basic LED control
leds[0] = CRGB_create(255, 0, 0);  // Set LED to red
FastLED_show();                    // Update display

// Advanced effects
fill_solid(leds, LED_RING_LEDS, CRGB_BLUE);
fill_rainbow(leds, LED_RING_LEDS, 0, 7);
fadeToBlackBy(leds, LED_RING_LEDS, 64);

// Color blending
CRGB blended = blend(CRGB_RED, CRGB_BLUE, 128);
```

## 🚨 Troubleshooting

### Common Issues

**Boot Loop**: Ensure NVS partition is properly configured in `partitions.csv`

**WiFi Connection Issues**: Verify credentials and check signal strength

**Speech Recognition Not Working**: Confirm microphone connections and ESP-SR model installation

**LED Issues**: Check power supply capacity and GPIO connections

**Web Interface Not Loading**: Verify WiFi connection and check serial output for IP address

### Debug Output
Enable verbose logging in `menuconfig`:
```
Component config → Log output → Default log verbosity → Debug
```

## 🔮 Future Enhancements

- **Multiple Timer Support**: Run concurrent timers
- **MQTT Integration**: Smart home integration
- **Mobile App**: Dedicated mobile application
- **Sound Effects**: Audio feedback for timer events
- **Custom Wake Words**: Personalized activation phrases

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

## 📄 License

This project is in the Public Domain (CC0 licensed). Feel free to use, modify, and distribute as needed.

## 🙏 Acknowledgments

- **ESP-SR Team**: Excellent speech recognition framework
- **Chronos Mini**: Inspiration for timer visualization
- **FastLED Community**: LED control paradigms
- **ESP-IDF Team**: Robust embedded framework

---

**Project Status**: ✅ Production Ready
**Last Updated**: September 2025
**ESP-IDF Version**: 5.5+
**Hardware Tested**: ESP32-S3-DevKitC-1
