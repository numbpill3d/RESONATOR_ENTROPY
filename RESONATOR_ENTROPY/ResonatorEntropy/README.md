# ESP32 Resonator Entropy Reader

This project turns your ESP32 into an entropy generator and visualizer. It reads analog noise from pins to generate entropy values and provides a web interface for real-time visualization.

## Quick Setup Guide

### 1. Arduino IDE Setup

1. Install Arduino IDE from [arduino.cc/en/software](https://www.arduino.cc/en/software)
2. Add ESP32 board support:
   - Go to **File > Preferences**
   - Add this URL to "Additional Boards Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to **Tools > Board > Boards Manager**
   - Install "ESP32 by Espressif Systems"

3. Install required libraries:
   - Go to **Tools > Manage Libraries**
   - Install "ArduinoJson" (version 6.x or higher)

4. Install ESP32 Filesystem Uploader:
   - Download from: [github.com/me-no-dev/arduino-esp32fs-plugin/releases](https://github.com/me-no-dev/arduino-esp32fs-plugin/releases)
   - Unzip to: `[Arduino directory]/tools/ESP32FS/`
   - Restart Arduino IDE

### 2. Upload the Code

1. Select your ESP32 board: **Tools > Board > ESP32 Arduino > ESP32 Dev Module**
2. Select your port: **Tools > Port > [Your ESP32 port]**
3. Select partition scheme: **Tools > Partition Scheme > Default 4MB with spiffs**
4. Click the Upload button (→) to upload the sketch

### 3. Upload Web Interface Files

1. Go to **Tools > ESP32 Sketch Data Upload**
2. Wait for the upload to complete

### 4. Access the Interface

1. After uploading, your ESP32 will create a WiFi network:
   - SSID: `EntropyReader`
   - Password: `entropy123`
2. Connect to this network
3. Open a web browser and go to: `http://192.168.4.1`
4. You should see the entropy visualization interface

## Pin Configuration

- **PIN_A**: GPIO 36 (ADC1_0) - First analog input for entropy generation
- **PIN_B**: GPIO 39 (ADC1_3) - Second analog input for entropy generation
- **LED**: GPIO 2 - Built-in LED on most ESP32 boards (will light up based on entropy values)

## Features

- Real-time entropy visualization
- Configurable WiFi settings
- AP mode with captive portal
- Adjustable sample rate
- Enhanced entropy generation
- Memory-optimized for ESP32

## Troubleshooting

- If upload fails, try holding the BOOT button while starting the upload
- If web interface doesn't appear, check that both the sketch and SPIFFS data were uploaded
- If connecting to WiFi fails, the ESP32 will revert to AP mode

## License

This project is released under the MIT License.
