# ESP32 Resonator Entropy Reader - Technical Data Sheet

## Overview

The ESP32 Resonator Entropy Reader is a specialized device designed to capture and visualize entropy generated from analog inputs. This document provides technical specifications, entropy generation methodology, and advanced configuration options.

## Hardware Specifications

### Recommended ESP32 Boards

| Board Type | Compatibility | Notes |
|------------|---------------|-------|
| ESP32-WROOM | Excellent | Default test platform |
| ESP32-DevKitC | Excellent | Standard development board |
| NodeMCU-ESP32 | Good | May need pin remapping |
| ESP32-WROVER | Good | Additional PSRAM available |
| ESP32-S2/S3 | Limited | Requires code modification |

### Power Requirements

- Operating Voltage: 3.3V
- Current Consumption: 
  - ~80mA during WiFi transmission
  - ~40mA during normal operation
  - ~5mA in deep sleep (not implemented in base code)

### Analog Input Specifications

- ADC Resolution: 12-bit (0-4095 range)
- Input Impedance: ~40kΩ
- Maximum Input Voltage: 3.3V (DO NOT exceed this value)
- Recommended Signal Level: Floating or <100mV noise signals

## Entropy Generation Methodology

### Source Signal Processing

The entropy is derived from multiple sources:

1. **Analog Noise Capture**:
   - PIN_A (GPIO 36): Primary analog noise input
   - PIN_B (GPIO 39): Secondary analog noise input
   
2. **Signal Combination**:
   ```cpp
   unsigned long rawValue = (a << 16) | b;
   ```
   - Combines both 12-bit readings into a 32-bit value
   
3. **Entropy Enhancement**:
   ```cpp
   unsigned long entropyValue = rawValue ^ (rawValue >> 8);
   ```
   - XOR operation distributes bit patterns
   - Bit shifting creates dependencies between different regions

4. **Timing Integration**:
   ```cpp
   entropyValue ^= micros();
   ```
   - Incorporates system timing jitter
   - Adds unpredictability based on execution timing

### Entropy Quality Factors

The quality of entropy depends on several factors:

- **Source Noise**: Higher-quality entropy comes from truly random physical processes
- **Sampling Rate**: Faster sampling may capture correlated values
- **Environmental Factors**: Temperature, electromagnetic interference, and power supply stability affect readings
- **Load Variation**: System load affects timing-based entropy contributions

## Memory Optimization

### Memory Usage Breakdown

| Component | Approximate Usage |
|-----------|-------------------|
| Base Code | ~200KB (Flash) |
| Web Interface | ~60KB (Flash) |
| Runtime Variables | ~20KB (RAM) |
| Entropy History Buffer | ~400B (RAM) |
| WiFi & Networking | ~30KB (RAM) |

### Optimization Techniques

1. **PROGMEM Usage**:
   - Web interface HTML stored in program memory
   - Static content stored in flash instead of RAM

2. **Buffer Sizing**:
   - Entropy history buffer size is configurable via `HISTORY_SIZE`
   - Default value (100) balances history depth vs. memory usage

3. **JSON Generation**:
   - Direct string construction instead of ArduinoJson for history data
   - ArduinoJson used only for settings and status to limit memory fragmentation

4. **Memory Monitoring**:
   - Free heap reported via status API endpoint
   - Can be monitored through web interface

## Advanced Configuration

### Compiler Flags

Add these to your platformio.ini or Arduino IDE preferences for customization:

```
-DHISTORY_SIZE=200       // Increase history buffer (default: 100)
-DAP_SSID="CustomName"   // Custom AP mode SSID
-DAP_PASSWORD="secret"   // Custom AP mode password
-DLED_INVERTED           // Invert LED behavior
-DDEBUG_LEVEL=1          // Enable more serial debugging (0-3)
```

### WiFi Configuration

The system supports two operation modes:

1. **Station Mode** (Client):
   - Connects to existing WiFi network
   - Lower power consumption
   - Potentially wider accessibility across network

2. **Access Point Mode**:
   - Creates its own WiFi network
   - Default SSID: "EntropyReader"
   - Default Password: "entropy123"
   - Default IP: 192.168.4.1
   - Includes captive portal for easy access

## Advanced Entropy Source Configurations

### External Analog Sources

For improved entropy quality, consider these external sources:

1. **Reverse-Biased PN Junction**:
   - Connect a reverse-biased diode to analog input pins
   - Circuit: 3.3V → 100kΩ resistor → Diode (reverse) → GND
   - Tap signal from diode-resistor junction to ADC input

2. **Amplified Thermal Noise**:
   - Use high-gain op-amp to amplify resistor thermal noise
   - Suggested circuit: 
     * LM358 op-amp with 10kΩ input resistor
     * Gain of ~100x
     * AC-couple to ADC input

3. **RF Pickup Antenna**:
   - 10-20cm wire connected to ADC input through 1MΩ resistor
   - Acts as an antenna for picking up RF noise
   - Extremely sensitive to environmental EMI

### Digital Enhancement

Additional techniques to improve entropy quality:

1. **Post-processing Options**:
   - Von Neumann debiasing
   - Hash functions (SHA256)
   - LFSR mixing

2. **Sampling Considerations**:
   - Randomize sampling intervals
   - Use hardware timer interrupts for more precise timing
   - Sample during varying system loads

## Performance Metrics

### ESP32 DevKitC Test Results

| Metric | Value | Notes |
|--------|-------|-------|
| Entropy Generation Rate | ~10 samples/sec | With 100ms default interval |
| Web Interface Responsiveness | <100ms | Typical response time |
| Maximum Clients | 4-5 | Before noticeable performance degradation |
| Flash Wear | Minimal | Settings saved only when changed |
| Power Consumption | ~120mA | During active web serving |
| Temperature Sensitivity | Moderate | ADC readings vary with temperature |

## Extending The Project

### Integration Options

1. **MQTT Publication**:
   - Publish entropy values to MQTT broker
   - Enable remote monitoring and data collection

2. **Data Logging**:
   - Log entropy values to SD card
   - Enable long-term statistical analysis

3. **True Random Number Generation**:
   - Implement TRNG algorithms using the entropy source
   - Export via API for external consumption

4. **Entropy Visualization Modes**:
   - Frequency distribution
   - 3D visualization
   - Spectrogram view

### Code Extension Points

Key functions designed for customization:

1. `readEntropy()` - Modify entropy collection algorithm
2. `updateLED()` - Change visualization behavior
3. `handleRoot()` - Extend web interface
4. `setupServer()` - Add new API endpoints
