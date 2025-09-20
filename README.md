# ESP32 Speech Recognition LED Controller

An ESP32-based speech recognition system that controls an external WS2812 LED on GPIO8 based on voice commands. The device responds to wake words and specific timer commands with different LED colors and patterns.

## Features

- **Wake Word Detection**: Device listens for wake words and responds with LED feedback
- **Speech Commands**: Recognizes two timer commands:
  - "Count down 10 minutes" - LED turns red
  - "Count up 20 minutes" - LED turns blue
- **LED States**:
  - Waiting for wake word: Slowly blinking white LED
  - Wake word detected: Solid white LED
  - Command recognized: Command-specific color for 2 seconds
- **FastLED-Style Interface**: Uses FastLED-compatible syntax for easy LED control

## Hardware Requirements

- ESP32-S3 development board
- WS2812 addressable LED connected to GPIO8
- Microphone (compatible with ESP-SR)

## Software Dependencies

- ESP-IDF framework
- ESP-SR (Speech Recognition) library
- led_strip component
- hardware_driver component

## LED Behavior

| State | LED Pattern | Color |
|-------|-------------|-------|
| Idle (waiting for wake word) | Slow blinking | White |
| Wake word detected | Solid | White |
| "Count down 10 minutes" | Solid for 2s | Red |
| "Count up 20 minutes" | Solid for 2s | Blue |
| Unknown command | Solid for 2s | Green |

## Installation

1. Clone this repository
2. Set up ESP-IDF environment
3. Configure the project: `idf.py menuconfig`
4. Build the project: `idf.py build`
5. Flash to device: `idf.py flash`

## Configuration

The LED is configured on GPIO8 by default. To change this, modify the `LED_STRIP_GPIO` define in `main/main.c`:

```c
#define LED_STRIP_GPIO 8
```

## FastLED-Style Interface

The project includes a FastLED-compatible interface for easy LED control:

```c
// Set LED to specific color
leds[0] = (CRGB)CRGB_RED;
FastLED_show();

// Use custom colors
leds[0] = CRGB_create(128, 64, 255);
FastLED_show();

// Fill with solid color
fill_solid(leds, LED_STRIP_LENGTH, (CRGB)CRGB_BLUE);
FastLED_show();
```

## Speech Commands

The system recognizes these commands after wake word detection:
- "Count down 10 minutes" (ID: 1)
- "Count up 20 minutes" (ID: 2)

## Build Instructions

### Set Target

```bash
idf.py set-target esp32s3
```

### Configure

Select the default sdkconfig according to your development board:

```bash
cp sdkconfig.defaults.esp32s3 sdkconfig
```

### Build & Flash

Build the project and flash it to the board:

```bash
idf.py -b 2000000 flash monitor
```

(To exit the serial monitor, type `Ctrl-]`)

## Development

This project was developed using ESP-IDF and includes:
- Robust speech recognition using ESP-SR
- Real-time LED control with FastLED-style syntax
- Error handling and timeout management
- Configurable LED behaviors

## License

This project is in the Public Domain (or CC0 licensed, at your option).
