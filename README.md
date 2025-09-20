# ESP32 Resonator Entropy Reader

![screenshot of web dashboard](./RESONATOR_ENTROPY/RESONATOR_ENTROPY/msedge_tOfScD6nCJ.gif)



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
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools > Board > Boards Manager**
4. Search for "esp32" and install the "ESP32 by Espressif Systems"

### 3. Configure Arduino IDE Settings

1. Connect your ESP32 to your computer via USB
2. In Arduino IDE, select:
   - **Tools > Board > ESP32 Arduino > ESP32 Dev Module** (or your specific ESP32 board)
   - **Tools > Port > [Select the COM port for your ESP32]**
   - **Tools > Flash Size > 4MB (32Mb)**
   - **Tools > Partition Scheme > Default 4MB with spiffs**
   - **Tools > Upload Speed > 921600** (or lower if you experience issues)

### 4. Upload the Sketch

1. Open the `ResonatorEntropy.ino` file in Arduino IDE
2. Click the upload button (right arrow) to compile and upload to your ESP32
3. Open the Serial Monitor (**Tools > Serial Monitor**) and set the baud rate to 115200 to see debugging information

## Using the Entropy Reader

### First-time Setup

When first powered on, the ESP32 will create a WiFi access point:

- **SSID**: EntropyReader
- **Password**: entropy123

Connect to this network with your computer or mobile device, and then open a web browser and navigate to:

```
http://192.168.4.1
```

### Web Interface

The web interface provides:

1. **Home Page**:
   - Real-time entropy value display
   - Dynamic graph showing entropy history
   - Statistics such as average entropy and sample count
   - WiFi connection status

2. **Configuration Page** (Click "Settings" link):
   - WiFi credentials setup
   - Sample rate adjustment
   - Serial logging toggle

### Connecting to Your WiFi Network

1. Navigate to the Settings page
2. Enter your WiFi SSID and password
3. Click "Save Settings"
4. The ESP32 will restart and attempt to connect to your network
5. If connection is successful, you can access the web interface via the IP address shown in the serial monitor
6. If connection fails, the ESP32 will revert to Access Point mode

## Customization Options

### Adjusting Entropy Sensitivity

If you want to change how the LED responds to entropy values, modify the threshold in the `updateLED()` function:

```cpp
void updateLED() {
  // Adjust this threshold value based on your observations
  if (lastEntropy > 800000) {
    digitalWrite(LED, HIGH);
  } else {
    digitalWrite(LED, LOW);
  }
}
```

### Adding External Sensors

For better entropy generation, you can connect external noise sources to the analog pins:

- PIN_A (GPIO 36): Connect to a floating input or noise source
- PIN_B (GPIO 39): Connect to a second floating input or noise source

Some options for noise sources:
- Unconnected (floating) pins
- Reverse-biased zener diodes
- Amplified thermal noise from resistors
- Radio frequency interference pickup

## Troubleshooting

### ESP32 Won't Connect to WiFi

- Double check your SSID and password in the settings page
- Ensure your WiFi network is 2.4GHz (ESP32 doesn't support 5GHz networks)
- Try positioning the ESP32 closer to your router

### Web Interface Doesn't Load

- Verify you're connected to the correct network
- Try accessing the device via its IP address
- If using AP mode, ensure you're connected to the "EntropyReader" network
- Restart the ESP32 by pressing the reset button or cycling power

### Upload Fails

- Select the correct board and port in Arduino IDE
- Hold the BOOT button on the ESP32 while starting the upload
- Try a slower upload speed
- Ensure you have a good quality USB cable connected

### Memory Issues

If you experience crashes or instability:
- Reduce the `HISTORY_SIZE` constant (default: 100)
- Simplify the HTML/CSS in the web interface
- Use the "Debug" level when selecting ESP32 boards to see memory usage

## Technical Details

### Entropy Generation Method

The entropy is generated by:
1. Reading two analog inputs (potentially capturing electrical noise)
2. Combining them with bit manipulation
3. XORing with timing data for additional randomness

While not cryptographically secure, this provides varying values suitable for visualization and experimentation.

### Memory Usage Considerations

The ESP32 has limited RAM (around 320KB), and this project is optimized to work within those constraints:

- Circular buffer for entropy history with configurable size
- Efficient JSON generation without dynamic memory allocation
- HTML/CSS/JS stored in PROGMEM to save RAM
- Minimal use of String objects to avoid heap fragmentation

## License

This project is released under the MIT License.

## Acknowledgments

- ESP32 Arduino Core developers
- ArduinoJson library by Benoît Blanchon
