#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <math.h>
#include <EEPROM.h>

const char* ssid = "ResonatorEntropy";
const char* password = "entropy123";
WebServer server(80);

#define LED_PIN 2
#define ADC_PIN1 36
#define ADC_PIN2 39
#define ADC_PIN3 34
#define ADC_PIN4 35
#define SAMPLE_SIZE 1024
#define BUFFER_SIZE 30

#define HALL_READ_PIN 4

uint8_t entropyBuffer[SAMPLE_SIZE];
float adcHistory[BUFFER_SIZE];
float streamHistory[BUFFER_SIZE];
float sourcesHistory[BUFFER_SIZE * 7];
float bitHistory[BUFFER_SIZE];
float spikeHistory[BUFFER_SIZE];
uint8_t histogram[256] = {0};
float currentEntropy = 0.0;
float threshold = 0.95;
bool alertActive = false;
unsigned long lastCompute = 0;
unsigned long lastAlert = 0;
float adcVar = 0.0, tempVar = 0.0, timingVar = 0.0, rssiVar = 0.0, clockVar = 0.0, touchVar = 0.0, hallVar = 0.0;
mbedtls_sha256_context shaCtx;

bool adcEnabled = true;
bool tempEnabled = true;
bool timingEnabled = true;
bool rssiEnabled = true;
bool clockEnabled = true;
bool touchEnabled = true;
bool hallEnabled = true;

float rawAdcSamples[BUFFER_SIZE];

String logBuffer[50];
int logIndex = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_0db);

  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  Serial.println("Server ready, data endpoint set");
  computeEntropy();  // initial population
  server.on("/alert", handleAlert);
  server.on("/threshold", handleThreshold);
  server.on("/export", handleExport);
  server.on("/toggle", handleToggle);
  server.on("/status", handleStatus);
  server.on("/export_log", handleExportLog);
  server.on("/reset_threshold", handleResetThreshold);
  server.begin();
  EEPROM.begin(512);
  uint8_t threshByte = EEPROM.read(0);
  threshold = (float)threshByte / 100.0f;
  if (threshold > 1.0f || threshold < 0.0f) {
    threshold = 0.95f;
  }
  Serial.println("Server started");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (now - lastCompute > 100) {
    computeEntropy();
    lastCompute = now;
  }

  if (alertActive && now - lastAlert > 200) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastAlert = now;
  } else if (!alertActive) {
    digitalWrite(LED_PIN, LOW);
  }
}

void computeEntropy() {
  // Collect 30 recent raw ADC samples into circular buffer
  static bool initialized = false;
  if(!initialized) {
    for(int i=0; i<30; i++) {
      float avg = (analogRead(36) + analogRead(39) + analogRead(34) + analogRead(35)) / (4.0f * 4095.0f);
      rawAdcSamples[i] = avg;
    }
    initialized = true;
    // Force non-zero if all zero (though init should prevent)
    bool allZero = true;
    for(int j=0; j<30; j++) {
      if(rawAdcSamples[j] > 0) allZero = false;
    }
    if(allZero) {
      for(int j=0; j<30; j++) {
        float avg = (analogRead(36) + analogRead(39) + analogRead(34) + analogRead(35)) / (4.0f * 4095.0f);
        rawAdcSamples[j] = avg > 0 ? avg : 0.01;  // minimal non-zero
      }
    }
  } else {
    static int index = 0;
    float avg = (analogRead(36) + analogRead(39) + analogRead(34) + analogRead(35)) / (4.0f * 4095.0f);
    rawAdcSamples[index] = avg;
    Serial.print("ADC avg: ");
    Serial.println(avg);
    index = (index + 1) % 30;
  }

  double adcSum1 = 0, adcSum2 = 0, adcSum3 = 0, adcSum4 = 0;
  double adcSqSum1 = 0, adcSqSum2 = 0, adcSqSum3 = 0, adcSqSum4 = 0;
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    uint16_t adc1 = analogRead(ADC_PIN1);
    uint16_t adc2 = analogRead(ADC_PIN2);
    uint16_t adc3 = analogRead(ADC_PIN3);
    uint16_t adc4 = analogRead(ADC_PIN4);
    adcSum1 += adc1; adcSum2 += adc2; adcSum3 += adc3; adcSum4 += adc4;
    adcSqSum1 += (double)adc1 * adc1; adcSqSum2 += (double)adc2 * adc2;
    adcSqSum3 += (double)adc3 * adc3; adcSqSum4 += (double)adc4 * adc4;
    delayMicroseconds(5 + (esp_random() % 6));
  }

  double var1 = (adcSqSum1 / SAMPLE_SIZE) - pow(adcSum1 / SAMPLE_SIZE, 2);
  double var2 = (adcSqSum2 / SAMPLE_SIZE) - pow(adcSum2 / SAMPLE_SIZE, 2);
  double var3 = (adcSqSum3 / SAMPLE_SIZE) - pow(adcSum3 / SAMPLE_SIZE, 2);
  double var4 = (adcSqSum4 / SAMPLE_SIZE) - pow(adcSum4 / SAMPLE_SIZE, 2);
  adcVar = (sqrt(var1) + sqrt(var2) + sqrt(var3) + sqrt(var4)) / 4.0;

  float temp = temperatureRead() / 10.0;
  tempVar = fabs(temp - 25.0);

  unsigned long tStart = micros();
  for (int i = 0; i < 100; i++) {
    esp_random();
  }
  unsigned long tEnd = micros();
  timingVar = fabs((tEnd - tStart) - 50000.0) / 10000.0;

  clockVar = 0.0;
  for (int i = 0; i < 50; i++) {
    unsigned long start = millis();
    delayMicroseconds(100);
    unsigned long end = millis();
    clockVar += fabs((end - start) - 0.1);
  }
  clockVar /= 50.0;

  int touch1 = touchRead(0);
  int touch2 = touchRead(3);
  touchVar = (fabs(touch1 - 35.0) + fabs(touch2 - 35.0)) / 100.0f;
  if (touch1 == 0 && touch2 == 0) touchVar = 0.0f;

  hallVar = analogRead(HALL_READ_PIN) / 4095.0f;

  int rssi = WiFi.RSSI();
  static int lastRssi = 0;
  rssiVar = fabs(rssi - lastRssi);
  lastRssi = rssi;

  static bool seeded = false;
  if (!seeded) {
    uint32_t chipId = 0;
    for (int i = 0; i < 17; i = i + 8) {
      chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    entropyBuffer[0] = (uint8_t)(chipId & 0xFF);
    seeded = true;
  }

  memset(entropyBuffer, 0, SAMPLE_SIZE);
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    uint8_t adcByte = ((analogRead(ADC_PIN1) ^ analogRead(ADC_PIN2)) & 0xFF);
    uint8_t randByte = (uint8_t)esp_random() ^ (uint8_t)esp_random() ^ (uint8_t)esp_random();
    uint8_t varByte = 0;
    if (adcEnabled) varByte ^= (uint8_t)(adcVar * 256);
    if (tempEnabled) varByte ^= (uint8_t)(tempVar * 256);
    if (timingEnabled) varByte ^= (uint8_t)(timingVar * 256);
    if (rssiEnabled) varByte ^= (uint8_t)(rssiVar * 256);
    if (clockEnabled) varByte ^= (uint8_t)(clockVar * 256);
    if (touchEnabled) varByte ^= (uint8_t)(touchVar * 256);
    if (hallEnabled) varByte ^= (uint8_t)(hallVar * 256);
    entropyBuffer[i] = adcByte ^ randByte ^ varByte;
  }

  // Von Neumann debiasing
  uint8_t debiased[SAMPLE_SIZE];
  memset(debiased, 0, SAMPLE_SIZE);
  int debiasedBitIdx = 0;
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    for (int b = 0; b < 8; b += 2) {
      if (b + 1 >= 8) break;
      bool bit1 = (entropyBuffer[i] & (1 << (7 - b))) != 0;
      bool bit2 = (entropyBuffer[i] & (1 << (7 - b - 1))) != 0;
      if (bit1 != bit2) {
        bool outputBit = bit1;  // Output bit1 when differing (01 -> 0, 10 -> 1)
        int outByte = debiasedBitIdx / 8;
        int outBitPos = 7 - (debiasedBitIdx % 8);
        if (outByte < SAMPLE_SIZE) {
          if (outputBit) {
            debiased[outByte] |= (1 << outBitPos);
          }
        }
        debiasedBitIdx++;
      }
    }
  }
  memcpy(entropyBuffer, debiased, SAMPLE_SIZE);

  static unsigned long lastSha = 0;
  if (millis() - lastSha > 1000) {
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts(&shaCtx, 0);
    mbedtls_sha256_update(&shaCtx, entropyBuffer, SAMPLE_SIZE);
    uint8_t hash[32];
    mbedtls_sha256_finish(&shaCtx, hash);
    memcpy(entropyBuffer, hash, 32);
    lastSha = millis();
  }

  memset(histogram, 0, 256);
  float total = 0.0;
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    histogram[entropyBuffer[i]]++;
    total += 1.0;
  }
  float shannon = 0.0;
  for (int i = 0; i < 256; i++) {
    float p = histogram[i] / total;
    if (p > 0) shannon -= p * log2(p);
  }
  currentEntropy = shannon / 8.0;

  alertActive = (currentEntropy > threshold);
  shiftHistory(adcHistory, adcVar);
  shiftHistory(streamHistory, currentEntropy);
  for (int s = 0; s < 7; s++) {
    float var;
    if (s == 0) var = adcVar;
    else if (s == 1) var = tempVar;
    else if (s == 2) var = timingVar;
    else if (s == 3) var = rssiVar;
    else if (s == 4) var = clockVar;
    else if (s == 5) var = touchVar;
    else var = hallVar;
    shiftHistory(sourcesHistory + s * BUFFER_SIZE, var);
  }
  shiftHistory(bitHistory, (currentEntropy > 0.5 ? 1.0 : 0.0));
  shiftHistory(spikeHistory, alertActive ? 1.0 : 0.0);

  Serial.print("Entropy: "); Serial.print(currentEntropy);
  Serial.print(" | ADC: "); Serial.print(adcVar);
  Serial.print(" Temp: "); Serial.print(tempVar);
  Serial.print(" Timing: "); Serial.print(timingVar);
  Serial.print(" RSSI: "); Serial.print(rssiVar);
  Serial.print(" Clock: "); Serial.print(clockVar);
  Serial.print(" Touch: "); Serial.print(touchVar);
  Serial.print(" Hall: "); Serial.println(hallVar);

  // Buffer log entry
  char logLine[256];
  sprintf(logLine, "Entropy:%.3f ADC:%.2f Temp:%.1f Timing:%.3f RSSI:%.0f Clock:%.3f Touch:%.1f Hall:%.3f",
          currentEntropy, adcVar, tempVar, timingVar, rssiVar, clockVar, touchVar, hallVar);
  logBuffer[logIndex] = String(logLine);
  logIndex = (logIndex + 1) % 50;
}

void shiftHistory(float* hist, float val) {
  for (int i = BUFFER_SIZE - 1; i > 0; i--) {
    hist[i] = hist[i - 1];
  }
  hist[0] = val;
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ControlCore Entropy</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Courier New', monospace; }
        body {
            background: linear-gradient(135deg, #000000 0%, #111111 50%, #000000 100%);
            color: #ffffff;
            height: 100vh;
            overflow: hidden;
            font-size: 12px;
            box-shadow: inset 0 0 100px rgba(255, 255, 255, 0.1);
        }
        .hack-terminal {
            height: 100vh;
            display: flex;
            flex-direction: column;
            padding: 10px;
            background: rgba(0, 0, 0, 0.9);
            border: 1px solid #cccccc;
            box-shadow: 0 0 20px rgba(255, 255, 255, 0.3);
            overflow: hidden;
        }
        .header {
            height: 40px;
            background: linear-gradient(90deg, #111111, #222222);
            border: 1px solid #cccccc;
            text-align: center;
            color: #ffffff;
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 0 10px;
            box-shadow: 0 2px 10px rgba(255, 255, 255, 0.2);
            transition: all 0.3s ease;
        }
        .header-left { color: #ff0000; }
        .status {
            font-weight: bold;
            color: #ffffff;
            transition: all 0.3s ease;
        }
        .status.anom {
            color: #ff0000;
            animation: alert-glitch 0.5s infinite, pulse 1s infinite;
            text-shadow: 0 0 10px #ff0000;
        }
        @keyframes alert-glitch {
            0%, 100% { transform: translate(0); }
            20% { transform: translate(-2px, 2px); }
            40% { transform: translate(-2px, -2px); }
            60% { transform: translate(2px, 2px); }
            80% { transform: translate(2px, -2px); }
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.7; }
        }
        .main { flex: 1; display: flex; gap: 10px; overflow-y: auto; }
        .left-panel {
            flex: 1;
            border: 1px solid #cccccc;
            background: rgba(0, 0, 0, 0.8);
            overflow-y: auto;
            box-shadow: inset 0 0 20px rgba(255, 255, 255, 0.1);
            border-radius: 5px;
            display: flex;
            flex-direction: column;
        }
        .canvas-container {
            width: 100%;
            flex: 1;
            position: relative;
            min-height: 150px;
        }
        canvas {
            width: 100%;
            height: 100%;
            display: block;
            background: #000;
            border: 1px solid #666; /* Brighter border for visibility */
        }
        .gauge {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            text-align: center;
        }
        .gauge > div:first-child {
            order: 2;
            margin-top: 5px;
            font-size: 0.8em;
        }
        .panel .numbers {
            font-size: 1.4em;
            height: 60px;
            line-height: 60px;
        }
        .panel:nth-child(4) {  /* Spectrogram panel */
            flex: 1.2;
        }
        .right-panels {
            width: 350px; /* Increased for better visibility */
            display: flex;
            flex-direction: column;
            gap: 10px;
            overflow-y: auto;
        }
        .panel {
            border: 1px solid #cccccc;
            background: linear-gradient(135deg, #111111, #222222);
            flex: 1 1 auto;
            min-height: 80px; /* Minimum height to ensure content visible */
            padding: 10px;
            color: #ffffff;
            box-shadow: 0 2px 10px rgba(255, 255, 255, 0.2);
            border-radius: 5px;
            transition: all 0.3s ease;
            overflow: visible; /* Ensure no hidden overflow */
        }
        .panel:hover {
            box-shadow: 0 4px 20px rgba(255, 255, 255, 0.4);
        }
        .gauge { text-align: center; }
        .numbers {
            white-space: nowrap;
            animation: num-scroll 8s linear infinite;
            color: #ffffff;
            font-size: 10px;
            height: 20px;
            line-height: 20px;
            overflow: hidden;
        }
        .numbers.anom {
            color: #ff0000;
            animation-duration: 3s;
            text-shadow: 0 0 5px #ff0000;
        }
        @keyframes num-scroll {
            from { transform: translateX(100%); }
            to { transform: translateX(-100%); }
        }
        .footer {
            height: 50px;
            background: linear-gradient(90deg, #111111, #222222);
            border: 1px solid #cccccc;
            display: flex;
            justify-content: space-around;
            align-items: center;
            color: #ffffff;
            box-shadow: 0 -2px 10px rgba(255, 255, 255, 0.2);
            border-radius: 5px;
            flex-shrink: 0;
        }
        button {
            background: linear-gradient(135deg, #000, #222);
            border: 1px solid #cccccc;
            color: #ffffff;
            cursor: pointer;
            transition: all 0.3s ease;
            padding: 5px 10px;
            border-radius: 3px;
        }
        button:hover {
            background: linear-gradient(135deg, #ffffff, #222);
            color: #000;
            box-shadow: 0 0 10px rgba(255, 255, 255, 0.5);
        }
        input {
            background: #000;
            border: 1px solid #cccccc;
            color: #ffffff;
            border-radius: 3px;
        }
        .bitstream {
            position: absolute;
            bottom: 60px;
            left: 0;
            width: 100%;
            background: rgba(0, 0, 0, 0.9);
            color: #ffffff;
            font-size: 8px;
            white-space: nowrap;
            animation: bit-scroll 12s linear infinite;
            padding: 5px;
            border-top: 1px solid #cccccc;
            z-index: 10;
        }
        @keyframes bit-scroll {
            from { transform: translateX(100%); }
            to { transform: translateX(-100%); }
        }
        .alert-msg {
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            font-size: 24px;
            color: #ff0000;
            animation: alert-pulse 0.5s infinite;
            z-index: 10;
            display: none;
            text-shadow: 0 0 20px #ff0000;
            background: rgba(0, 0, 0, 0.8);
            padding: 20px;
            border: 2px solid #ff0000;
            border-radius: 10px;
        }
        .alert-msg.active { display: block; }
        @keyframes alert-pulse {
            0%, 100% { opacity: 1; transform: translate(-50%, -50%) scale(1); }
            50% { opacity: 0.8; transform: translate(-50%, -50%) scale(1.05); }
        }
        .cursor { animation: blink 1s infinite; }
        @keyframes blink { 0%, 50% { opacity: 1; } 51%, 100% { opacity: 0; } }
        .particle {
            position: absolute;
            width: 2px;
            height: 2px;
            background: #ff0000;
            border-radius: 50%;
            animation: particle-float 2s linear infinite;
        }
        @keyframes particle-float {
            from { transform: translateY(100vh) rotate(0deg); opacity: 1; }
            to { transform: translateY(-100px) rotate(360deg); opacity: 0; }
        }
        .terminal-icon {
            position: fixed;
            bottom: 10px;
            left: 10px;
            width: 30px;
            height: 30px;
            background: #800080;
            color: white;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            font-size: 14px;
            z-index: 100;
        }
        .terminal-overlay {
            position: fixed;
            bottom: 50px;
            left: 10px;
            width: 300px;
            height: 200px;
            background: rgba(0, 0, 0, 0.9);
            border: 1px solid #cccccc;
            display: none;
            flex-direction: column;
            z-index: 101;
        }
        .terminal-header {
            background: #222;
            padding: 5px;
            display: flex;
            justify-content: space-between;
        }
        .close-terminal {
            cursor: pointer;
        }
        .terminal-output {
            flex: 1;
            overflow-y: auto;
            padding: 5px;
            font-family: monospace;
            font-size: 12px;
        }
        .terminal-input {
            display: flex;
            padding: 5px;
            border-top: 1px solid #cccccc;
        }
        .terminal-input input {
            flex: 1;
            background: transparent;
            border: none;
            color: white;
            outline: none;
        }
        @media (max-width: 768px) {
            .hack-terminal { padding: 5px; }
            .main { flex-direction: column; gap: 5px; padding: 0; }
            .left-panel { min-height: 150px; }
            .right-panels { width: 100%; flex-direction: column; gap: 5px; }
            .panel { min-height: 50px; }
            .canvas-container { min-height: 60px; }
            .footer { height: 40px; padding: 0 5px; }
            .bitstream { bottom: 50px; font-size: 6px; }
        }
    </style>
</head>
<body>
    <div class="hack-terminal">
        <div class="header">
            <div class="header-left">[root@controlcore]# tail -f /var/entropy/log<span class="cursor">_</span></div>
            <div class="status" id="status">NOMINAL</div>
        </div>
        <div class="main">
            <div class="left-panel">
                <div class="canvas-container" style="flex: 1;">
                    <canvas id="entropy-canvas" style="border:1px solid #ccc; background:#000; display:block;"></canvas>
                </div>
                <div class="canvas-container" style="flex: 1;">
                    <canvas id="adc-canvas" style="border:1px solid #ccc; background:#000; display:block;"></canvas>
                </div>
            </div>
            <div class="right-panels">
                <div class="panel gauge">
                    <div>SHANNON LEVEL</div>
                    <canvas id="gauge-canvas" style="border:1px solid #ccc; background:#000; display:block;"></canvas>
                </div>
                <div class="panel">
                    <div>REAL-TIME LOG</div>
                    <div id="log-container" style="height: 150px; overflow-y: auto; font-family: monospace; font-size: 10px; background: #000; color: #fff; padding: 5px; border: 1px solid #666;"></div>
                </div>
                <div class="panel">
                    <div>SCROLLING DATA</div>
                    <div style="height: 30px; overflow: hidden;">
                        <div class="numbers" id="scroll-numbers">ENTROPY:0.00 ADC:0.00 TEMP:0.0 TIMING:0.000 RSSI:0 CLOCK:0.000 TOUCH:0.0 BITS:0</div>
                    </div>
                </div>
                <div class="panel">
                    <div>SPECTROGRAM</div>
                    <canvas id="spectro-canvas" style="border:1px solid #ccc; background:#000; display:block;"></canvas>
                </div>
            </div>
        </div>
        <div class="footer">
            <button onclick="toggleView()">WATERFALL</button>
            <button onclick="exportLog()">EXPORT</button>
            <button onclick="alertTest()">ALERT</button>
            <div>THRESH:<input type="range" id="thresh" min="0" max="1" step="0.05" value="0.95"> <span id="thresh-val">0.95</span></div>
        </div>
        <div class="bitstream" id="bitstream">0101010111010110010101011101010110010101 0101010111010110010101011101010110010101 0101010111010110010101011101010110010101</div>
        <div class="alert-msg" id="alert-msg">ALERT: ANOMALOUS ENTROPY DETECTED!</div>
    </div>
    <div class="terminal-icon" onclick="toggleTerminal()">-:::</div>
    <div id="terminal-overlay" class="terminal-overlay">
        <div class="terminal-header">
            <span>Terminal</span>
            <span class="close-terminal" onclick="toggleTerminal()">X</span>
        </div>
        <div id="terminal-output" class="terminal-output"></div>
        <div class="terminal-input">
            <input id="terminal-command" type="text" onkeypress="handleCommand(event)" placeholder="Enter command...">
        </div>
    </div>
    <script>
        function generateSigil(e) { return e > 0.8 ? ' ***' : ''; }
        let data = { entropy: 0, adc: 0, sources: [0,0,0,0,0,0,0], bits: 0, spike: false, adcSamples: [], hexStream: "" };
        let histories = { entropy: [], adc: [], sources: [] };
        let viewMode = 'graph';
        const BUFFER = 30;
        let entropyCanvas, gaugeCanvas, adcCanvas, spectroCanvas;
        let entropyCtx, gaugeCtx, adcCtx, spectroCtx;
        let animId = null;
        let columnY = [];
        let speeds = [];
        let numColumns = 0;
        let columnCharIndices = [];
        let charWidth = 12;
        let fontSize = 12;

        function initHistory() {
            for (let key in histories) {
                if (key === 'sources') {
                    histories[key] = Array(7).fill().map(() => new Array(BUFFER).fill(0));
                } else {
                    histories[key] = new Array(BUFFER).fill(0);
                }
            }
        }
        initHistory();

        function initCanvases() {
            entropyCanvas = document.getElementById('entropy-canvas');
            if(!entropyCanvas) console.error('entropy-canvas missing');
            else console.log('Canvas initialized: entropy-canvas - size:', entropyCanvas.width, 'x', entropyCanvas.height);
            gaugeCanvas = document.getElementById('gauge-canvas');
            if(!gaugeCanvas) console.error('gauge-canvas missing');
            else console.log('Canvas initialized: gauge-canvas - size:', gaugeCanvas.width, 'x', gaugeCanvas.height);
            spectroCanvas = document.getElementById('spectro-canvas');
            if(!spectroCanvas) console.error('spectro-canvas missing');
            else console.log('Canvas initialized: spectro-canvas - size:', spectroCanvas.width, 'x', spectroCanvas.height);
            adcCanvas = document.getElementById('adc-canvas');
            if(!adcCanvas) console.error('adc-canvas missing');
            else console.log('Canvas initialized: adc-canvas - size:', adcCanvas.width, 'x', adcCanvas.height);
        
            const dpr = window.devicePixelRatio || 1;
            function resizeCanvas(canvas) {
                const rect = canvas.parentElement.getBoundingClientRect();
                if (rect.width < 10) rect.width = 400;
                if (rect.height < 10) rect.height = 200;
                console.log('Resizing canvas', canvas.id, 'parent size:', rect.width, 'x', rect.height);
                canvas.style.width = '100%';
                canvas.style.height = '100%';
                canvas.width = rect.width * dpr;
                canvas.height = rect.height * dpr;
                const ctx = canvas.getContext('2d');
                ctx.scale(dpr, dpr);
                console.log('Canvas resized:', canvas.id, '->', canvas.width, 'x', canvas.height);
            }
            resizeCanvas(entropyCanvas);
            resizeCanvas(gaugeCanvas);
            resizeCanvas(spectroCanvas);
            resizeCanvas(adcCanvas);
            entropyCtx = entropyCanvas.getContext('2d');
            gaugeCtx = gaugeCanvas.getContext('2d');
            spectroCtx = spectroCanvas.getContext('2d');
            adcCtx = adcCanvas.getContext('2d');
            // Resize on window resize
            window.addEventListener('resize', () => {
                resizeCanvas(entropyCanvas);
                resizeCanvas(gaugeCanvas);
                resizeCanvas(spectroCanvas);
                resizeCanvas(adcCanvas);
                updateUI();
            });
        }

        async function fetchData() {
            try {
                const res = await fetch('/data');
                if (!res.ok) {
                    console.error('Fetch failed:', res.status, res.statusText);
                    return;
                }
                const json = await res.json();
                console.log('Fetched data:', json);
                data = json;
                updateHistories();
                updateUI();
            } catch (e) {
                console.error('Fetch error', e);
            }
        }

        function updateHistories() {
            if (!data || data.entropy === undefined) return;
            
            histories.entropy.unshift(data.entropy);
            histories.adc.unshift(data.adc);
            
            if (data.sources && data.sources.length >= 7) {
                for (let i = 0; i < 7; i++) {
                    histories.sources[i].unshift(data.sources[i]);
                }
            }
            
            histories.entropy = histories.entropy.slice(0, BUFFER);
            histories.adc = histories.adc.slice(0, BUFFER);
            
            if (histories.sources && histories.sources.length >= 7) {
                for (let i = 0; i < 7; i++) {
                    histories.sources[i] = histories.sources[i].slice(0, BUFFER);
                }
            }
        }

        function drawEntropyScope() {
            console.log('Scope: samples len', data.adcSamples?.length);
            const dpr = window.devicePixelRatio || 1;
            const w = entropyCanvas.width / dpr;
            const h = entropyCanvas.height / dpr;
            if(!data || !data.adcSamples || data.adcSamples.length < 2) {
                console.log('No data yet, skipping draw');
                return; // Don't draw until data available
            }
            console.log('Using live adcSamples');
            let samples = data.adcSamples;
            let min = Math.min(...samples);
            let max = Math.max(...samples);
            let range = max - min;
            if(range < 0.01) range = 0.01;
            entropyCtx.clearRect(0,0,w,h);
            // grid:
            entropyCtx.strokeStyle = '#333';
            entropyCtx.lineWidth = 1;
            const vGridStep = Math.max(50, w / 8);
            for(let x=0; x<w; x+=vGridStep) {
                entropyCtx.beginPath();
                entropyCtx.moveTo(x,0);
                entropyCtx.lineTo(x,h);
                entropyCtx.stroke();
            }
            const hGridStep = Math.max(50, h / 4);
            for(let y=0; y<h; y+=hGridStep) {
                entropyCtx.beginPath();
                entropyCtx.moveTo(0,y);
                entropyCtx.lineTo(w,y);
                entropyCtx.stroke();
            }
            // line
            entropyCtx.strokeStyle = data.spike ? '#fff' : '#888';
            entropyCtx.lineWidth = data.spike ? 3 : 2;
            entropyCtx.beginPath();
            entropyCtx.moveTo(0, h - ((samples[0] - min) / range * h));
            for(let i=1; i<samples.length; i++) {
                let x = (i / (samples.length - 1)) * w;
                let y = h - ((samples[i] - min) / range * h);
                entropyCtx.lineTo(x, y);
            }
            entropyCtx.stroke();
            // labels
            entropyCtx.fillStyle = '#888';
            entropyCtx.font = `${Math.max(10, 12 / dpr)}px Courier New`;
            entropyCtx.fillText('ADC Noise Live (0-1)', 10, 20);
        }

        function drawGauge() {
            console.log('Gauge: entropy', data.entropy);
            const dpr = window.devicePixelRatio || 1;
            const w = gaugeCanvas.width / dpr;
            const h = gaugeCanvas.height / dpr;
            const centerX = w / 2;
            const centerY = h / 2;
            const radius = Math.min(w, h) * 0.6;

            gaugeCtx.clearRect(0, 0, w, h);

            // Background
            gaugeCtx.fillStyle = '#111111';
            gaugeCtx.fillRect(0, 0, w, h);

            // Background arc full circle
            gaugeCtx.strokeStyle = '#333';
            gaugeCtx.lineWidth = Math.max(1, 12 / dpr);
            gaugeCtx.beginPath();
            gaugeCtx.arc(centerX, centerY, radius, 0, 2 * Math.PI);
            gaugeCtx.stroke();

            // Fill arc from 0 to entropy * 2PI
            const fillAngle = data.entropy * 2 * Math.PI;
            gaugeCtx.strokeStyle = data.entropy > parseFloat(document.getElementById('thresh-val').textContent) ? '#ff0000' : '#ffffff';
            gaugeCtx.lineWidth = Math.max(1, 12 / dpr);
            gaugeCtx.beginPath();
            gaugeCtx.arc(centerX, centerY, radius, 0, fillAngle);
            gaugeCtx.stroke();

            // Needle
            const needleAngle = fillAngle - Math.PI / 2;  // Start from top
            const needleLength = radius - Math.max(5, 10 / dpr);
            gaugeCtx.strokeStyle = '#ffffff';
            gaugeCtx.lineWidth = Math.max(1, 4 / dpr);
            gaugeCtx.beginPath();
            gaugeCtx.moveTo(centerX, centerY);
            gaugeCtx.lineTo(centerX + Math.cos(needleAngle) * needleLength, centerY + Math.sin(needleAngle) * needleLength);
            gaugeCtx.stroke();

            // Value and label - larger and centered
            gaugeCtx.fillStyle = '#ffffff';
            gaugeCtx.textAlign = 'center';
            gaugeCtx.textBaseline = 'middle';
            gaugeCtx.font = `bold ${Math.max(16, 50 / dpr)}px Courier New`;
            gaugeCtx.fillText(data.entropy.toFixed(2), centerX, centerY);
            gaugeCtx.font = `bold ${Math.max(8, 24 / dpr)}px Courier New`;
            gaugeCtx.fillText('Entropy', centerX, centerY + radius + 10);
        }

        function drawADCWaveform() {
            console.log('Drawing ADC waveform');
            const width = adcCanvas.width;
            const height = adcCanvas.height;
            console.log('ADC canvas size:', width, 'x', height);
            adcCtx.clearRect(0, 0, width, height);

            // Background
            adcCtx.fillStyle = '#111111';
            adcCtx.fillRect(0, 0, width, height);

            // Check if history data is available
            if (!histories || !histories.adc || histories.adc.length === 0) {
                console.log('No ADC history data available');
                // Draw placeholder flat line
                adcCtx.strokeStyle = '#666';
                adcCtx.lineWidth = 1;
                adcCtx.beginPath();
                adcCtx.moveTo(0, height / 2);
                adcCtx.lineTo(width, height / 2);
                adcCtx.stroke();
                return;
            }

            // Dynamic scaling
            const maxAdc = Math.max(...histories.adc, 1);

            // Waveform
            adcCtx.strokeStyle = '#ffffff';
            adcCtx.lineWidth = 1;
            adcCtx.beginPath();
            for (let i = 0; i < histories.adc.length; i++) {
                const x = (i / (histories.adc.length - 1)) * width;
                const y = height - (histories.adc[i] / maxAdc * height * 0.8);
                if (i === 0) adcCtx.moveTo(x, y);
                else adcCtx.lineTo(x, y);
            }
            adcCtx.stroke();

            // Grid
            adcCtx.strokeStyle = '#333';
            adcCtx.lineWidth = 0.5;
            for (let i = 0; i < width; i += 50) {
                adcCtx.beginPath();
                adcCtx.moveTo(i, 0);
                adcCtx.lineTo(i, height);
                adcCtx.stroke();
            }
        }

        function drawSpectrogram() {
            console.log('Drawing spectrogram');
            console.log('Spectro: sources len', data?.sources?.length);
            const dpr = window.devicePixelRatio || 1;
            const w = spectroCanvas.width / dpr;
            const h = spectroCanvas.height / dpr;
            console.log('Spectro canvas size:', w, 'x', h);
            spectroCtx.clearRect(0, 0, w, h);

            // Background
            spectroCtx.fillStyle = '#111111';
            spectroCtx.fillRect(0, 0, w, h);

            // Check if data is available
            if (!data || !data.sources || data.sources.length < 7) {
                console.log('No data available for spectrogram');
                // Draw placeholder low bars
                const barWidth = w / 7;
                for (let i = 0; i < 7; i++) {
                    const barHeight = h * 0.3;
                    spectroCtx.fillStyle = '#444';
                    const x = i * barWidth;
                    spectroCtx.fillRect(x, h - barHeight, barWidth - 2, barHeight);
                }
                return;
            }

            // Bars for each source - 2x height scaling
            const barWidth = w / 7;
            let maxSource = Math.max(...data.sources, 0.01);
            for (let i = 0; i < 7; i++) {
                let barHeight = (data.sources[i] / maxSource) * h * 2;  // 2x larger
                barHeight = Math.min(barHeight, h);  // Cap
                const gradient = spectroCtx.createLinearGradient(0, h - barHeight, 0, h);
                gradient.addColorStop(0, '#ffffff');
                gradient.addColorStop(1, '#444444');
                spectroCtx.fillStyle = gradient;
                const x = (w - (7 * barWidth)) / 2;  // Center bars
                spectroCtx.fillRect(x + i * barWidth, h - barHeight, barWidth - 2, barHeight);
            }
        }

        function createParticle() {
            if (data.spike) {
                const particle = document.createElement('div');
                particle.className = 'particle';
                particle.style.left = Math.random() * 100 + '%';
                document.body.appendChild(particle);
                setTimeout(() => particle.remove(), 2000);
            }
        }

        document.getElementById('thresh').addEventListener('input', (e) => {
            document.getElementById('thresh-val').textContent = e.target.value;
            fetch('/threshold?val=' + e.target.value, {method: 'POST'});
        });
function updateUI() {
    console.log('UpdateUI: data keys', Object.keys(data));
    if(!data) return;
    console.log('Data fetched, entropy:', data.entropy);
    const anomaly = data.entropy > parseFloat(document.getElementById('thresh-val').textContent);

    // Status
    const statusEl = document.getElementById('status');
    statusEl.textContent = anomaly ? 'ANOMALOUS' : 'NOMINAL';
    statusEl.className = 'status' + (anomaly ? ' anom' : '');

    // Scrolling Numbers
    const scrollEl = document.getElementById('scroll-numbers');
    let readings = `ENTROPY:${data.entropy?.toFixed(2) || '0.00'} ADC:${data.adc?.toFixed(2) || '0.00'} `;
    
    if (data.sources && data.sources.length >= 7) {
        readings += `TEMP:${data.sources[1]?.toFixed(1) || '0.0'} TIMING:${data.sources[2]?.toFixed(3) || '0.000'} RSSI:${data.sources[3]?.toFixed(0) || '0'} CLOCK:${data.sources[4]?.toFixed(3) || '0.000'} TOUCH:${data.sources[5]?.toFixed(1) || '0.0'} HALL:${data.sources[6]?.toFixed(3) || '0.000'} `;
    }
    
    readings += `BITS:${data.bits || '0'}`;
    scrollEl.textContent = readings + generateSigil(data.entropy || 0);
    scrollEl.className = 'numbers' + (anomaly ? ' anom' : '');
    scrollEl.style.fontSize = '1.2em';

    // Alert Message
    document.getElementById('alert-msg').className = 'alert-msg' + (anomaly ? ' active' : '');

    // Log update - only if data available
    const logContainer = document.getElementById('log-container');
    if (logContainer) {
        if (data && data.entropy !== undefined) {
            console.log('Adding log entry');
            const timestamp = new Date().toLocaleTimeString();
            const statusText = anomaly ? 'ANOMALY' : 'NOMINAL';
            const logColor = anomaly ? '#ff0000' : '#ffffff';
            const sourcesStr = (data.sources && data.sources.length >= 7) ? data.sources.slice(0,7).map(s => s.toFixed(2)).join(' ') : 'N/A';
            const logLine = `<div style="color: ${logColor}; margin-bottom: 2px;">[${timestamp}] Entropy:${data.entropy.toFixed(3)} ADC:${data.adc.toFixed(2)} Sources:${sourcesStr} Bits:${data.bits || 0} Status:${statusText}</div>`;
            logContainer.innerHTML += logLine;
            logContainer.scrollTop = logContainer.scrollHeight;
            // Keep only last 50 lines
            const lines = logContainer.children;
            while (lines.length > 50) {
                logContainer.removeChild(lines[0]);
            }
        } else if (logContainer.children.length === 0) {
            // Initial placeholder
            const initLog = `<div style="color: #666; margin-bottom: 2px;">[${new Date().toLocaleTimeString()}] Initializing entropy reader...</div>`;
            logContainer.innerHTML = initLog;
        }
    }

    // Bitstream
    const bitEl = document.getElementById('bitstream');
    if (data.bitStream && data.bitStream.length > 0) {
        let streamText = data.bitStream.substring(0, 200);
        bitEl.textContent = (streamText + ' ').repeat(5).substring(0, 800);
        bitEl.style.animationDuration = anomaly ? '6s' : '12s';
    } else {
        bitEl.textContent = 'Initializing bitstream...';
    }

    // Draw canvases
    console.log('Updating UI with viewMode:', viewMode);
    if (viewMode === 'graph') {
        drawEntropyScope();
        console.log('Entropy scope drawn');
    } else if (viewMode === 'waterfall') {
        drawWaterfall();
        console.log('Waterfall drawn');
    }
    drawGauge();
    console.log('Gauge drawn');
    drawADCWaveform();
    console.log('ADC waveform drawn');
    drawSpectrogram();
    console.log('Spectrogram drawn');

    // Particles for spikes
    createParticle();
}

        function toggleView() {
            viewMode = viewMode === 'graph' ? 'waterfall' : 'graph';
            if (viewMode === 'graph') {
                if (animId) {
                    cancelAnimationFrame(animId);
                    animId = null;
                }
                drawEntropyScope();
                // Trigger resize to ensure canvases update
                window.dispatchEvent(new Event('resize'));
            } else {
                initMatrix();
                startMatrixAnim();
            }
            updateUI();
        }

        function initMatrix() {
            const width = entropyCanvas.width;
            const height = entropyCanvas.height;
            if (width === 0 || height === 0) {
                // Fallback sizes if not rendered yet
                numColumns = 60;
                columnY = new Array(numColumns).fill(0);
                speeds = new Array(numColumns).fill(1);
                columnCharIndices = new Array(numColumns).fill(0);
            } else {
                numColumns = Math.floor(width / charWidth);
                columnY = new Array(numColumns).fill(0);
                speeds = new Array(numColumns).fill(1);
                columnCharIndices = new Array(numColumns).fill(0);
            }
            for (let i = 0; i < numColumns; i++) {
                columnY[i] = Math.random() * -height;
                speeds[i] = 0.5 + Math.random() * 1.5;
                columnCharIndices[i] = Math.floor(Math.random() * 100);
            }
        }

        function drawWaterfall() {
            const width = entropyCanvas.width;
            const height = entropyCanvas.height;
            const ctx = entropyCtx;
            const thresholdVal = parseFloat(document.getElementById('thresh').value);
            const anomaly = data.entropy > thresholdVal;

            // Fade trail
            ctx.fillStyle = 'rgba(0, 0, 0, 0.04)';
            ctx.fillRect(0, 0, width, height);

            // Modulate speeds by source variances
            for (let col = 0; col < numColumns; col++) {
                let sourceIdx = col % 7;
                let sourceVar = data.sources[sourceIdx];
                speeds[col] = 0.5 + sourceVar * 3; // Faster with higher variance
            }

            const speedMultiplier = 1 + data.entropy * 1.5;
            const trailLength = Math.floor(5 + data.entropy * 20 + data.sources[0] * 10); // Density based on entropy and ADC
            const hexChars = "0123456789ABCDEF";

            for (let col = 0; col < numColumns; col++) {
                let y = columnY[col];

                // Advance char index
                let streamLen = data.hexStream ? data.hexStream.length : 128;
                columnCharIndices[col] = (columnCharIndices[col] + 1) % streamLen;

                // Update position
                columnY[col] += speeds[col] * speedMultiplier;
                if (columnY[col] > height + trailLength * fontSize) {
                    columnY[col] = Math.random() * -height;
                }

                // Draw trail
                for (let j = 0; j < trailLength; j++) {
                    let charY = y - j * fontSize;
                    if (charY > height) continue;

                    let idx = (columnCharIndices[col] - j + streamLen) % streamLen;
                    let char = data.hexStream && data.hexStream.length > 0 ? data.hexStream[idx] : hexChars[Math.floor(Math.random() * 16)];

                    let alpha = (j / trailLength) * 0.9 + 0.1;
                    let color;
                    if (anomaly && Math.random() < 0.1) {
                        color = `rgba(255, 0, 0, ${alpha})`; // Red anomaly
                    } else if (j < trailLength * 0.2) {
                        color = `rgba(128, 0, 128, ${alpha})`; // Purple head
                    } else {
                        color = `rgba(75, 0, 130, ${alpha * 0.7})`; // Indigo tail
                    }

                    ctx.fillStyle = color;
                    ctx.font = `${fontSize}px 'Courier New', monospace`;
                    ctx.fillText(char, col * charWidth, charY + fontSize);
                }
            }

            // Title
            ctx.fillStyle = '#ffffff';
            ctx.font = '16px Courier New';
            ctx.fillText('ENTROPY WATERFALL', 10, 20);
        }

        function startMatrixAnim() {
            if (animId) {
                cancelAnimationFrame(animId);
            }
            function animate() {
                drawWaterfall();
                animId = requestAnimationFrame(animate);
            }
            animate();
        }

        function exportLog() {
            let log = 'ControlCore Entropy Log:\n';
            log += `Time: ${new Date().toISOString()}\nEntropy: ${data.entropy}\nADC: ${data.adc}\nSources: ${data.sources.join(',')}\nBits: ${data.bits}\n`;
            const blob = new Blob([log], {type: 'text/plain'});
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'controlcore_log_' + Date.now() + '.txt';
            a.click();
        }

        function alertTest() {
            fetch('/alert');
        }

        function toggleTerminal() {
            const overlay = document.getElementById('terminal-overlay');
            overlay.style.display = overlay.style.display === 'flex' ? 'none' : 'flex';
            if (overlay.style.display === 'flex') {
                document.getElementById('terminal-command').focus();
            }
        }

        function handleCommand(e) {
            if (e.key !== 'Enter') return;
            const command = document.getElementById('terminal-command').value.trim().toLowerCase();
            const output = document.getElementById('terminal-output');
            output.innerHTML += `<div style="color: #ffffff;">> ${command}</div>`;
            document.getElementById('terminal-command').value = '';
            let response = '';
            const parts = command.split(' ');
            const cmd = parts[0];
            if (cmd === '/toggle_source' && parts[1]) {
                const src = parts[1];
                if (['adc', 'temp', 'timing', 'rssi', 'clock', 'touch', 'hall'].includes(src)) {
                    fetch('/toggle?source=' + src)
                        .then(res => res.json())
                        .then(data => {
                            if (data.success) {
                                output.innerHTML += `<div style="color: #cccccc;">Source ${src} ${data.enabled ? 'enabled' : 'disabled'}</div>`;
                            } else {
                                output.innerHTML += `<div style="color: #ff0000;">${data.error || 'Toggle failed'}</div>`;
                            }
                            output.scrollTop = output.scrollHeight;
                        })
                        .catch(err => {
                            output.innerHTML += `<div style="color: #ff0000;">Toggle failed</div>`;
                            output.scrollTop = output.scrollHeight;
                        });
                    return;
                } else {
                    response = 'Invalid source. Valid: adc, temp, timing, rssi, clock, touch, hall';
                }
            } else if (cmd === '/export_log') {
                fetch('/export_log')
                    .then(res => res.blob())
                    .then(blob => {
                        const url = URL.createObjectURL(blob);
                        const a = document.createElement('a');
                        a.href = url;
                        a.download = 'entropy_log.txt';
                        a.click();
                        URL.revokeObjectURL(url);
                        output.innerHTML += `<div style="color: #cccccc;">Log exported successfully</div>`;
                        output.scrollTop = output.scrollHeight;
                    })
                    .catch(err => {
                        output.innerHTML += `<div style="color: #ff0000;">Export failed</div>`;
                        output.scrollTop = output.scrollHeight;
                    });
                return;
            } else if (cmd === '/reset_threshold') {
                fetch('/reset_threshold')
                    .then(res => res.text())
                    .then(text => {
                        document.getElementById('thresh').value = 0.95;
                        document.getElementById('thresh-val').textContent = '0.95';
                        output.innerHTML += `<div style="color: #cccccc;">${text}</div>`;
                        output.scrollTop = output.scrollHeight;
                    })
                    .catch(err => {
                        output.innerHTML += `<div style="color: #ff0000;">Reset failed</div>`;
                        output.scrollTop = output.scrollHeight;
                    });
                return;
            } else if (cmd === '/status') {
                fetch('/status')
                    .then(res => res.text())
                    .then(text => {
                        output.innerHTML += `<div style="color: #cccccc;">${text}</div>`;
                        output.scrollTop = output.scrollHeight;
                    })
                    .catch(err => {
                        output.innerHTML += `<div style="color: #ff0000;">Status fetch failed</div>`;
                        output.scrollTop = output.scrollHeight;
                    });
                return;
            }
            // Sync commands
            switch (command) {
                case '/help':
                    response = 'Available commands:<br>/help - show this list<br>/thresh [0-1] - set anomaly threshold<br>/toggle_source [adc/temp/timing/rssi/clock/touch/hall] - toggle entropy source<br>/export_log - download full log file<br>/reset_threshold - revert threshold to 0.95<br>/status - show current config and stats<br>snapshot - export current data snapshot<br>graph - switch to graph view<br>waterfall - switch to waterfall view<br>settings - show threshold info';
                    break;
                case 'settings':
                    response = 'Use /thresh [value] to adjust threshold (0-1). Current: ' + document.getElementById('thresh-val').textContent;
                    break;
                case '/thresh':
                    response = 'Usage: /thresh [value between 0 and 1]';
                    break;
                case 'snapshot':
                    exportLog();
                    response = 'Data snapshot exported!';
                    break;
                case 'graph':
                    viewMode = 'graph';
                    if (animId) {
                        cancelAnimationFrame(animId);
                        animId = null;
                    }
                    drawEntropyScope();
                    response = 'Switched to graph view';
                    break;
                case 'waterfall':
                    viewMode = 'waterfall';
                    initMatrix();
                    startMatrixAnim();
                    response = 'Switched to waterfall view';
                    break;
                default:
                    if (command.startsWith('/thresh ')) {
                        const val = parseFloat(parts[1]);
                        if (!isNaN(val) && val >= 0 && val <= 1) {
                            document.getElementById('thresh').value = val;
                            document.getElementById('thresh-val').textContent = val.toFixed(2);
                            fetch('/threshold?val=' + val, {method: 'POST'});
                            response = 'Threshold set to ' + val.toFixed(2);
                        } else {
                            response = 'Invalid value. Use 0-1.';
                        }
                    } else {
                        response = 'Unknown command. Type /help for list.';
                    }
                    break;
            }
            if (response) {
                output.innerHTML += `<div style="color: #cccccc;">${response}</div>`;
                output.scrollTop = output.scrollHeight;
            }
        }

        // Initialize
        initCanvases();
        // Initial loading state
        document.getElementById('status').textContent = 'INITIALIZING';
        document.getElementById('scroll-numbers').textContent = 'Loading data...';
        document.getElementById('bitstream').textContent = 'Initializing bitstream...';
        drawEntropyScope();
        setInterval(fetchData, 200);
        fetchData();
    </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleData() {
  DynamicJsonDocument doc(2048);
  doc["entropy"] = currentEntropy;
  doc["adc"] = adcVar;
  JsonArray sources = doc.createNestedArray("sources");
  sources.add(adcVar);
  sources.add(tempVar);
  sources.add(timingVar);
  sources.add(rssiVar);
  sources.add(clockVar);
  sources.add(touchVar);
  sources.add(hallVar);
  doc["bits"] = (currentEntropy > 0.5 ? 1 : 0);
  doc["spike"] = alertActive ? 1.0 : 0.0;

  // adcSamples
  JsonArray adcSamps = doc.createNestedArray("adcSamples");
  for (int i = 0; i < BUFFER_SIZE; i++) {
    adcSamps.add(rawAdcSamples[i]);
  }

  // hexStream: last 64 bytes as hex string
  String hexStream = "";
  int startIdx = SAMPLE_SIZE > 64 ? SAMPLE_SIZE - 64 : 0;
  for (int i = startIdx; i < SAMPLE_SIZE; i++) {
    char buf[3];
    sprintf(buf, "%02X", entropyBuffer[i]);
    hexStream += buf;
  }
  doc["hexStream"] = hexStream;

  // Generate bitStream from debiased entropyBuffer (most recent first, up to 1024 bits)
  String bitStream = "";
  int bitCount = 0;
  const int MAX_BITS = 1024;
  bool done = false;
  for (int i = SAMPLE_SIZE - 1; i >= 0 && !done; i--) {
    uint8_t byteVal = entropyBuffer[i];
    for (int b = 7; b >= 0 && !done; b--) {
      bitStream += (byteVal & (1 << b)) ? '1' : '0';
      bitCount++;
      if (bitCount >= MAX_BITS) {
        done = true;
      }
    }
  }
  doc["bitStream"] = bitStream;

  String json;
  serializeJson(doc, json);
  Serial.print("JSON: entropy=" + String(currentEntropy) + ", adcSamples[0]=" + String(rawAdcSamples[0]) + ", sources[0]=" + String(adcVar) + ", bitStream len=" + String(bitStream.length()) + ", hexStream len=" + String(hexStream.length()));
  Serial.println("Sending data: " + json);
  server.send(200, "application/json", json);
}

void handleAlert() {
  alertActive = !alertActive;
  server.send(200, "text/plain", alertActive ? "Alert ON" : "Alert OFF");
}

void handleThreshold() {
  if (server.hasArg("val")) {
    threshold = server.arg("val").toFloat();
    EEPROM.write(0, (uint8_t)(threshold * 100));
    EEPROM.commit();
  }
  server.send(200, "text/plain", String(threshold));
}

void handleExport() {
  server.send(200, "text/plain", "Export triggered");
}

void handleToggle() {
  if (server.hasArg("source")) {
    String src = server.arg("source");
    src.toLowerCase();
    bool enabled = false;
    if (src == "adc") {
      adcEnabled = !adcEnabled;
      enabled = adcEnabled;
    } else if (src == "temp") {
      tempEnabled = !tempEnabled;
      enabled = tempEnabled;
    } else if (src == "timing") {
      timingEnabled = !timingEnabled;
      enabled = timingEnabled;
    } else if (src == "rssi") {
      rssiEnabled = !rssiEnabled;
      enabled = rssiEnabled;
    } else if (src == "clock") {
      clockEnabled = !clockEnabled;
      enabled = clockEnabled;
    } else if (src == "touch") {
      touchEnabled = !touchEnabled;
      enabled = touchEnabled;
    } else if (src == "hall") {
      hallEnabled = !hallEnabled;
      enabled = hallEnabled;
    } else {
      server.send(200, "application/json", "{\"success\":false,\"error\":\"Invalid source\"}");
      return;
    }
    DynamicJsonDocument doc(128);
    doc["success"] = true;
    doc["source"] = src;
    doc["enabled"] = enabled;
    String jsonStr;
    serializeJson(doc, jsonStr);
    server.send(200, "application/json", jsonStr);
  } else {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"No source specified\"}");
  }
}

void handleStatus() {
  String statusStr = "Active sources: ";
  if (adcEnabled) statusStr += "adc ";
  if (tempEnabled) statusStr += "temp ";
  if (timingEnabled) statusStr += "timing ";
  if (rssiEnabled) statusStr += "rssi ";
  if (clockEnabled) statusStr += "clock ";
  if (touchEnabled) statusStr += "touch ";
  if (hallEnabled) statusStr += "hall ";
  statusStr += "| Threshold: " + String(threshold, 2) + " | Entropy: " + String(currentEntropy, 3);
  server.send(200, "text/plain", statusStr);
}

void handleExportLog() {
  String logContent = "Resonator Entropy Log\nTimestamp: " + String(millis() / 1000) + "s\n\n";
  // Recent logs first (last 50)
  for (int i = 0; i < 50; i++) {
    int idx = (logIndex - 1 - i + 50 * 2) % 50;
    if (logBuffer[idx].length() > 0) {
      logContent += logBuffer[idx] + "\n";
    }
  }
  server.sendHeader("Content-Disposition", "attachment; filename=entropy_log_" + String(millis()) + ".txt");
  server.send(200, "text/plain", logContent);
}

void handleResetThreshold() {
  threshold = 0.95f;
  EEPROM.write(0, (uint8_t)(threshold * 100));
  EEPROM.commit();
  server.send(200, "text/plain", "Threshold reset to 0.95");
}