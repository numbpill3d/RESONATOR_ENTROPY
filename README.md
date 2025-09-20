# ESP32 Resonator Entropy Reader

![screenshot of web dashboard](https://github.com/numbpill3d/RESONATOR_ENTROPY/raw/fd18fc07d32ef5b5f1aff1525287040c53ab7675/RESONATOR_ENTROPY/msedge_tOfScD6nCJ.gif)


This project turns your ESP32 into an entropy generator and visualizer. It reads analog noise from pins to generate entropy values and provides a web interface for real-time visualization.

## Features

- **Entropy Generation**: Captures and processes analog signals to generate entropy values
- **Real-time Visualization**: Web-based interface with a dynamic graph of entropy values
- **WiFi Configuration**: Connect to your own WiFi network or use the built-in access point
- **Responsive Design**: Works on mobile and desktop browsers
- **Configuration Interface**: Adjust sample rate and other settings through the web UI
- **Memory Optimized**: Designed to work within the constraints of ESP32 memory

## Hardware Requirements

- ESP32 development board (e.g., ESP32-WROOM, ESP32-DevKitC, NodeMCU-ESP32, etc.)
- USB cable for programming
- (Optional) External sensors or noise sources connected to analog pins

## Pin Configuration

- **PIN_A**: GPIO 36 (ADC1_0) - First analog input for entropy generation
- **PIN_B**: GPIO 39 (ADC1_3) - Second analog input for entropy generation
- **LED**: GPIO 2 - Built-in LED on most ESP32 boards (will light up based on entropy values)

## Setup Instructions

### 1. Install Required Libraries

In Arduino IDE, go to **Tools > Manage Libraries** and install the following libraries:

- ArduinoJson (version 6.x or higher)
- DNSServer (usually included with ESP32 board support)
- SPIFFS (included with ESP32 board support)

### 2. ESP32 Board Support

If you haven't already, install the ESP32 board support in Arduino IDE:

1. Go to **File > Preferences**
2. Add the following URL to the "Additional Boards Manager URLs" field:
