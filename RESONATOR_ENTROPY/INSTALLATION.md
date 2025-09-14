# ESP32 Resonator Entropy Reader - Installation Guide

This guide provides detailed step-by-step instructions for setting up the Resonator Entropy Reader on your ESP32 device.

## Prerequisites

### Hardware

- ESP32 development board (ESP32-WROOM, NodeMCU-ESP32, etc.)
- Micro USB cable
- Computer with Arduino IDE installed
- (Optional) External components for improved entropy sources

### Software

- [Arduino IDE](https://www.arduino.cc/en/software) (version 1.8.x or newer)
- ESP32 board support package
- Required libraries:
  - ArduinoJson (version 6.x)
  - SPIFFS file system support
  - DNSServer (included with ESP32 board package)

## Step 1: Install Arduino IDE

1. Download Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Install Arduino IDE following the instructions for your operating system

## Step 2: Install ESP32 Board Support

1. Open Arduino IDE
2. Go to **File > Preferences**
3. In the "Additional Boards Manager URLs" field, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Click "OK" to save preferences
5. Go to **Tools > Board > Boards Manager**
6. Search for "esp32"
7. Install "ESP32 by Espressif Systems" (latest version)
8. Close the Boards Manager

## Step 3: Install Required Libraries

1. Go to **Tools > Manage Libraries**
2. Search for and install "ArduinoJson" by Benoit Blanchon (version 6.x)
3. Close the Library Manager

## Step 4: Install ESP32 Filesystem Uploader

This tool is needed to upload the web interface files to the ESP32's SPIFFS storage.

1. Download the ESP32 Filesystem Uploader from [GitHub](https://github.com/me-no-dev/arduino-esp32fs-plugin/releases/latest)
2. Create a `tools` folder in your Arduino directory if it doesn't exist
   - Windows: `C:\Users\<username>\Documents\Arduino\tools`
   - macOS: `~/Documents/Arduino/tools`
   - Linux: `~/Arduino/tools`
3. Unzip the downloaded file into the `tools` folder
   - The result should be a folder structure like: `Arduino/tools/ESP32FS/tool/esp32fs.jar`
4. Restart Arduino IDE if it's running

## Step 5: Configure Arduino IDE for Your ESP32

1. Connect your ESP32 to your computer via USB
2. Open Arduino IDE
3. Go to **Tools > Board > ESP32 Arduino** and select your specific board model
   - Common choices: "ESP32 Dev Module", "DOIT ESP32 DEVKIT V1", "NodeMCU-32S"
4. Set the following options in the Tools menu:
   - **Upload Speed**: 921600
   - **Flash Frequency**: 80MHz
   - **Flash Mode**: QIO
   - **Partition Scheme**: "Default 4MB with spiffs"
   - **Core Debug Level**: None
   - **Port**: Select the COM port or device path where your ESP32 is connected

## Step 6: Open the Resonator Entropy Reader Project

1. Navigate to the project folder
2. Double-click on `ResonatorEntropy.ino` to open it in Arduino IDE
3. The main sketch should open in Arduino IDE

## Step 7: Upload the Sketch

1. In Arduino IDE, click the "Verify" button (checkmark icon) to compile the sketch
2. If compilation is successful, click the "Upload" button (right arrow icon)
3. Wait for the upload to complete
   - You should see "Done uploading" in the status bar
   - If you encounter errors, check the "Troubleshooting" section below

## Step 8: Upload the Web Interface Files

These steps upload the HTML, CSS, and JavaScript files that make up the web interface.

### Method 1: Using Arduino IDE

1. In Arduino IDE, go to **Tools > ESP32 Sketch Data Upload**
   - If you don't see this option, check that you installed the ESP32 Filesystem Uploader correctly
2. Wait for the upload to complete
   - This may take a minute or two
   - You should see "SPIFFS Image Uploaded" in the console when done

### Method 2: Using the Provided Script (Windows only)

1. Open Command Prompt or Windows Explorer
2. Navigate to your project directory
3. Run `upload_spiffs.bat`
4. Follow the on-screen instructions

## Step 9: Test the Installation

1. After uploading both the sketch and web interface files, press the "Reset" button on your ESP32
2. The ESP32 will start in Access Point mode
   - SSID: "EntropyReader"
   - Password: "entropy123"
3. Connect to this WiFi network from your computer or mobile device
4. Open a web browser and navigate to:
   ```
   http://192.168.4.1
   ```
5. You should see the Resonator Entropy Reader web interface

## Step 10: Configure Your Device

1. In the web interface, click the "Settings" link
2. Enter your WiFi credentials if you want to connect to your home network
3. Adjust other settings as desired
4. Click "Save Configuration"
5. The ESP32 will restart and attempt to connect to your WiFi network
   - If connection is successful, you can access the device via its new IP address (check your router or serial monitor)
   - If connection fails, it will revert to Access Point mode

## Hardware Configuration (Optional)

For better entropy generation, consider the following:

### Basic Setup

- The ESP32 analog pins will generate some entropy even without external components
- Pins will be configured in "floating" mode (not connected to anything)

### Enhanced Entropy Setup

For improved entropy quality, try one of these configurations:

#### Option 1: Simple Floating Inputs
```
PIN_A (GPIO 36) ────── (not connected)
PIN_B (GPIO 39) ────── (not connected)
```

#### Option 2: Reverse-Biased Diode Noise
```
3.3V ──── 100kΩ Resistor ──┬── Diode (Cathode) ──── GND
                            │
                     PIN_A (GPIO 36)

3.3V ──── 100kΩ Resistor ──┬── Diode (Cathode) ──── GND
                            │
                     PIN_B (GPIO 39)
```

#### Option 3: RF Antenna Pickup
```
PIN_A (GPIO 36) ──── 1MΩ Resistor ──── 15cm Wire (antenna)
PIN_B (GPIO 39) ──── 1MΩ Resistor ──── 15cm Wire (antenna)
```

## Troubleshooting

### Upload Fails

- Try a slower upload speed (115200)
- Press and hold the BOOT button while starting the upload
- Check that you have selected the correct board and port
- Some ESP32 boards require you to press the BOOT button when the IDE shows "Connecting..."

### WiFi Connection Issues

- Double check your SSID and password
- Ensure your network is 2.4GHz (ESP32 doesn't support 5GHz)
- Try positioning the ESP32 closer to your router

### Web Interface Not Loading

- Check that both the sketch and SPIFFS data were uploaded successfully
- Verify you're connecting to the correct IP address
- Try clearing your browser cache or using a different browser

### Serial Monitor

For debugging, open the Serial Monitor in Arduino IDE:

1. Set the baud rate to 115200
2. Press the reset button on your ESP32
3. You should see startup messages and diagnostic information

## Updating the Firmware

To update your device with a new version:

1. Download the new version of the code
2. Open the sketch in Arduino IDE
3. Upload the sketch following Step 7 above
4. Upload the web interface files following Step 8 above

## Advanced Customization

See the `DATASHEET.md` file for details on:

- Advanced entropy generation methods
- Memory optimization
- Code extension points
- Performance considerations

## Additional Resources

- ESP32 Documentation: [espressif.com/en/support/download/documents](https://www.espressif.com/en/support/download/documents)
- Arduino ESP32 GitHub: [github.com/espressif/arduino-esp32](https://github.com/espressif/arduino-esp32)
- ArduinoJson Documentation: [arduinojson.org/v6/doc/](https://arduinojson.org/v6/doc/)
- SPIFFS Documentation: [arduino-esp8266.readthedocs.io/en/latest/filesystem.html](https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html)
