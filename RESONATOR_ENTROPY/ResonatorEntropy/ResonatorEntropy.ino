#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <math.h>

const char* ssid = "ResonatorEntropy";
const char* password = "entropy123";
WebServer server(80);

#define LED_PIN 2
#define ADC_PIN1 36
#define ADC_PIN2 39
#define ADC_PIN3 34
#define ADC_PIN4 35
#define SAMPLE_SIZE 512
#define BUFFER_SIZE 30

uint8_t entropyBuffer[SAMPLE_SIZE];
float adcHistory[BUFFER_SIZE];
float streamHistory[BUFFER_SIZE];
float sourcesHistory[BUFFER_SIZE * 6];
float bitHistory[BUFFER_SIZE];
float spikeHistory[BUFFER_SIZE];
uint8_t histogram[256] = {0};
float currentEntropy = 0.0;
float threshold = 0.95;
bool alertActive = false;
unsigned long lastCompute = 0;
unsigned long lastAlert = 0;
float adcVar = 0.0, tempVar = 0.0, timingVar = 0.0, rssiVar = 0.0, clockVar = 0.0, touchVar = 0.0;
mbedtls_sha256_context shaCtx;

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
  server.on("/alert", handleAlert);
  server.on("/threshold", handleThreshold);
  server.on("/export", handleExport);
  server.begin();
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
  uint32_t adcSum1 = 0, adcSum2 = 0, adcSum3 = 0, adcSum4 = 0;
  uint32_t adcSqSum1 = 0, adcSqSum2 = 0, adcSqSum3 = 0, adcSqSum4 = 0;
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    uint16_t adc1 = analogRead(ADC_PIN1);
    uint16_t adc2 = analogRead(ADC_PIN2);
    uint16_t adc3 = analogRead(ADC_PIN3);
    uint16_t adc4 = analogRead(ADC_PIN4);
    adcSum1 += adc1; adcSum2 += adc2; adcSum3 += adc3; adcSum4 += adc4;
    adcSqSum1 += (uint32_t)adc1 * adc1; adcSqSum2 += (uint32_t)adc2 * adc2;
    adcSqSum3 += (uint32_t)adc3 * adc3; adcSqSum4 += (uint32_t)adc4 * adc4;
    delayMicroseconds(5 + (esp_random() % 6));
  }

  adcVar = sqrt((float)(adcSqSum1 / SAMPLE_SIZE) - pow((float)adcSum1 / SAMPLE_SIZE, 2));
  adcVar += sqrt((float)(adcSqSum2 / SAMPLE_SIZE) - pow((float)adcSum2 / SAMPLE_SIZE, 2));
  adcVar /= 2;

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
  touchVar = fabs(touch1 - 35.0) + fabs(touch2 - 35.0);
  if (touch1 == 0 && touch2 == 0) touchVar = 0.0;

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
    uint8_t varByte = (uint8_t)(adcVar * 256) ^ (uint8_t)(tempVar * 256) ^ (uint8_t)(timingVar * 256) ^ (uint8_t)(clockVar * 256); // Disabled RSSI and Touch for reduced sensitivity
    entropyBuffer[i] = adcByte ^ randByte ^ varByte;
  }

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
  for (int s = 0; s < 6; s++) {
    shiftHistory(sourcesHistory + s * BUFFER_SIZE, (s == 0 ? adcVar : s == 1 ? tempVar : s == 2 ? timingVar : s == 3 ? rssiVar : s == 4 ? clockVar : touchVar));
  }
  shiftHistory(bitHistory, (currentEntropy > 0.5 ? 1.0 : 0.0));
  shiftHistory(spikeHistory, alertActive ? 1.0 : 0.0);

  Serial.print("Entropy: "); Serial.print(currentEntropy);
  Serial.print(" | ADC: "); Serial.print(adcVar);
  Serial.print(" Temp: "); Serial.print(tempVar);
  Serial.print(" Timing: "); Serial.print(timingVar);
  Serial.print(" RSSI: "); Serial.print(rssiVar);
  Serial.print(" Clock: "); Serial.print(clockVar);
  Serial.print(" Touch: "); Serial.println(touchVar);
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
        .main { flex: 1; display: flex; gap: 10px; }
        .left-panel {
            flex: 1;
            border: 1px solid #cccccc;
            background: rgba(0, 0, 0, 0.8);
            overflow: hidden;
            box-shadow: inset 0 0 20px rgba(255, 255, 255, 0.1);
            border-radius: 5px;
        }
        .canvas-container {
            width: 100%;
            height: 100%;
            position: relative;
        }
        canvas {
            width: 100%;
            height: 100%;
            display: block;
            background: #000;
        }
        .right-panels {
            width: 300px;
            display: flex;
            flex-direction: column;
            gap: 10px;
        }
        .panel {
            border: 1px solid #cccccc;
            background: linear-gradient(135deg, #111111, #222222);
            flex: 1;
            padding: 10px;
            color: #ffffff;
            box-shadow: 0 2px 10px rgba(255, 255, 255, 0.2);
            border-radius: 5px;
            transition: all 0.3s ease;
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
            position: fixed;
            bottom: 0;
            left: 0;
            width: 100%;
            background: rgba(0, 0, 0, 0.9);
            color: #ffffff;
            font-size: 8px;
            white-space: nowrap;
            animation: bit-scroll 12s linear infinite;
            padding: 5px;
            border-top: 1px solid #cccccc;
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
            .main { flex-direction: column; }
            .right-panels { width: 100%; flex-direction: row; flex-wrap: wrap; }
            .panel { min-height: 150px; }
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
                <div class="canvas-container">
                    <canvas id="entropy-canvas"></canvas>
                </div>
            </div>
            <div class="right-panels">
                <div class="panel gauge">
                    <div>SHANNON LEVEL</div>
                    <canvas id="gauge-canvas" width="200" height="150"></canvas>
                </div>
                <div class="panel">
                    <div>REAL-TIME LOG</div>
                    <div id="log-container" style="height: 100px; overflow-y: auto; font-family: monospace; font-size: 10px; background: #000; color: #fff; padding: 5px; border: 1px solid #333;"></div>
                </div>
                <div class="panel">
                    <div>SCROLLING DATA</div>
                    <div style="height: 30px; overflow: hidden;">
                        <div class="numbers" id="scroll-numbers">ENTROPY:0.00 ADC:0.00 TEMP:0.0 TIMING:0.000 RSSI:0 CLOCK:0.000 TOUCH:0.0 BITS:0</div>
                    </div>
                </div>
                <div class="panel">
                    <div>SPECTROGRAM</div>
                    <canvas id="spectro-canvas" width="280" height="120"></canvas>
                </div>
            </div>
        </div>
        <div class="footer">
            <button onclick="togglePlot()">PLOT</button>
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
        let data = { entropy: 0, adc: 0, sources: [0,0,0,0,0,0], bits: 0, spike: false };
        let histories = { entropy: [], adc: [], sources: [] };
        let viewMode = 'graph';
        const BUFFER = 30;
        let entropyCanvas, gaugeCanvas, adcCanvas, spectroCanvas;
        let entropyCtx, gaugeCtx, adcCtx, spectroCtx;

        function initHistory() {
            for (let key in histories) {
                if (key === 'sources') {
                    histories[key] = Array(6).fill().map(() => new Array(BUFFER).fill(0));
                } else {
                    histories[key] = new Array(BUFFER).fill(0);
                }
            }
        }
        initHistory();

        function initCanvases() {
            entropyCanvas = document.getElementById('entropy-canvas');
            gaugeCanvas = document.getElementById('gauge-canvas');
            spectroCanvas = document.getElementById('spectro-canvas');

            entropyCtx = entropyCanvas.getContext('2d');
            gaugeCtx = gaugeCanvas.getContext('2d');
            spectroCtx = spectroCanvas.getContext('2d');
        }

        async function fetchData() {
            try {
                const res = await fetch('/data');
                const json = await res.json();
                data = json;
                updateHistories();
                updateUI();
            } catch (e) {
                console.log('%ERROR: Connection to entropy core lost');
            }
        }

        function updateHistories() {
            histories.entropy.unshift(data.entropy);
            histories.adc.unshift(data.adc);
            for (let i = 0; i < 6; i++) {
                histories.sources[i].unshift(data.sources[i]);
            }
            histories.entropy = histories.entropy.slice(0, BUFFER);
            histories.adc = histories.adc.slice(0, BUFFER);
            for (let i = 0; i < 6; i++) {
                histories.sources[i] = histories.sources[i].slice(0, BUFFER);
            }
        }

        function drawEntropyScope() {
            const width = entropyCanvas.width;
            const height = entropyCanvas.height;
            entropyCtx.clearRect(0, 0, width, height);

            // Background gradient
            const gradient = entropyCtx.createLinearGradient(0, 0, 0, height);
            gradient.addColorStop(0, '#000000');
            gradient.addColorStop(1, '#111111');
            entropyCtx.fillStyle = gradient;
            entropyCtx.fillRect(0, 0, width, height);

            // Grid
            entropyCtx.strokeStyle = '#cccccc';
            entropyCtx.lineWidth = 0.5;
            for (let i = 0; i < width; i += 50) {
                entropyCtx.beginPath();
                entropyCtx.moveTo(i, 0);
                entropyCtx.lineTo(i, height);
                entropyCtx.stroke();
            }
            for (let i = 0; i < height; i += 50) {
                entropyCtx.beginPath();
                entropyCtx.moveTo(0, i);
                entropyCtx.lineTo(width, i);
                entropyCtx.stroke();
            }

            // Waveform
            entropyCtx.strokeStyle = data.entropy > parseFloat(document.getElementById('thresh-val').textContent) ? '#ff0000' : '#ffffff';
            entropyCtx.lineWidth = 2;
            entropyCtx.beginPath();
            for (let i = 0; i < histories.entropy.length; i++) {
                const x = (i / (histories.entropy.length - 1)) * width;
                const y = height - (histories.entropy[i] * height);
                if (i === 0) entropyCtx.moveTo(x, y);
                else entropyCtx.lineTo(x, y);
            }
            entropyCtx.stroke();

            // Title
            entropyCtx.fillStyle = '#ffffff';
            entropyCtx.font = '16px Courier New';
            entropyCtx.fillText('ENTROPY SCOPE', 10, 20);
        }

        function drawGauge() {
            const centerX = gaugeCanvas.width / 2;
            const centerY = gaugeCanvas.height - 20;
            const radius = 60;

            gaugeCtx.clearRect(0, 0, gaugeCanvas.width, gaugeCanvas.height);

            // Background
            gaugeCtx.fillStyle = '#111111';
            gaugeCtx.fillRect(0, 0, gaugeCanvas.width, gaugeCanvas.height);

            // Arc
            gaugeCtx.strokeStyle = '#333';
            gaugeCtx.lineWidth = 10;
            gaugeCtx.beginPath();
            gaugeCtx.arc(centerX, centerY, radius, Math.PI, 0);
            gaugeCtx.stroke();

            // Fill
            const fillAngle = Math.PI + (data.entropy * Math.PI);
            gaugeCtx.strokeStyle = data.entropy > parseFloat(document.getElementById('thresh-val').textContent) ? '#ff0000' : '#ffffff';
            gaugeCtx.beginPath();
            gaugeCtx.arc(centerX, centerY, radius, Math.PI, fillAngle);
            gaugeCtx.stroke();

            // Needle
            const needleAngle = Math.PI + (data.entropy * Math.PI);
            const needleLength = radius - 10;
            gaugeCtx.strokeStyle = '#ffffff';
            gaugeCtx.lineWidth = 2;
            gaugeCtx.beginPath();
            gaugeCtx.moveTo(centerX, centerY);
            gaugeCtx.lineTo(centerX + Math.cos(needleAngle) * needleLength, centerY + Math.sin(needleAngle) * needleLength);
            gaugeCtx.stroke();

            // Value
            gaugeCtx.fillStyle = '#ffffff';
            gaugeCtx.font = '12px Courier New';
            gaugeCtx.textAlign = 'center';
            gaugeCtx.fillText(data.entropy.toFixed(2), centerX, centerY + 20);
        }

        function drawADCWaveform() {
            const width = adcCanvas.width;
            const height = adcCanvas.height;
            adcCtx.clearRect(0, 0, width, height);

            // Background
            adcCtx.fillStyle = '#111111';
            adcCtx.fillRect(0, 0, width, height);

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
            const width = spectroCanvas.width;
            const height = spectroCanvas.height;
            spectroCtx.clearRect(0, 0, width, height);

            // Background
            spectroCtx.fillStyle = '#111111';
            spectroCtx.fillRect(0, 0, width, height);

            // Bars for each source
            const barWidth = width / 6;
            for (let i = 0; i < 6; i++) {
                const barHeight = (histories.sources[i][0] / 10) * height; // Assuming max ~10
                const gradient = spectroCtx.createLinearGradient(0, height - barHeight, 0, height);
                gradient.addColorStop(0, '#ffffff');
                gradient.addColorStop(1, '#444444');
                spectroCtx.fillStyle = gradient;
                spectroCtx.fillRect(i * barWidth, height - barHeight, barWidth - 2, barHeight);
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
            const anomaly = data.entropy > parseFloat(document.getElementById('thresh-val').textContent);

            // Status
            const statusEl = document.getElementById('status');
            statusEl.textContent = anomaly ? 'ANOMALOUS' : 'NOMINAL';
            statusEl.className = 'status' + (anomaly ? ' anom' : '');

            // Scrolling Numbers
            const scrollEl = document.getElementById('scroll-numbers');
            let readings = `ENTROPY:${data.entropy.toFixed(2)} ADC:${data.adc.toFixed(2)} TEMP:${data.sources[1].toFixed(1)} TIMING:${data.sources[2].toFixed(3)} RSSI:${data.sources[3].toFixed(0)} CLOCK:${data.sources[4].toFixed(3)} TOUCH:${data.sources[5].toFixed(1)} BITS:${data.bits}`;
            scrollEl.textContent = readings;
            scrollEl.className = 'numbers' + (anomaly ? ' anom' : '');

            // Alert Message
            document.getElementById('alert-msg').className = 'alert-msg' + (anomaly ? ' active' : '');

            // Log update
            const logContainer = document.getElementById('log-container');
            if (logContainer) {
                const timestamp = new Date().toLocaleTimeString();
                const statusText = anomaly ? 'ANOMALY' : 'NOMINAL';
                const logColor = anomaly ? '#ff0000' : '#ffffff';
                const sourcesStr = data.sources.map(s => s.toFixed(2)).join(' ');
                const logLine = `<div style="color: ${logColor}; margin-bottom: 2px;">[${timestamp}] Entropy:${data.entropy.toFixed(3)} ADC:${data.adc.toFixed(2)} Sources:${sourcesStr} Bits:${data.bits} Status:${statusText}</div>`;
                logContainer.innerHTML += logLine;
                logContainer.scrollTop = logContainer.scrollHeight;
                // Keep only last 50 lines
                const lines = logContainer.children;
                while (lines.length > 50) {
                    logContainer.removeChild(lines[0]);
                }
            }

            // Bitstream speed
            const bitEl = document.getElementById('bitstream');
            bitEl.style.animationDuration = anomaly ? '3s' : '12s';

            // Draw canvases
            if (viewMode === 'graph') {
                drawEntropyScope();
            } else {
                drawWaterfall();
            }
            drawGauge();
            drawSpectrogram();

            // Particles for spikes
            createParticle();
        }

        function togglePlot() {
            viewMode = viewMode === 'graph' ? 'waterfall' : 'graph';
            updateUI();
        }

        function drawWaterfall() {
            const width = entropyCanvas.width;
            const height = entropyCanvas.height;
            entropyCtx.clearRect(0, 0, width, height);

            // Background
            const gradient = entropyCtx.createLinearGradient(0, 0, 0, height);
            gradient.addColorStop(0, '#000000');
            gradient.addColorStop(1, '#111111');
            entropyCtx.fillStyle = gradient;
            entropyCtx.fillRect(0, 0, width, height);

            // Waterfall bars
            const barWidth = width / BUFFER;
            for (let i = 0; i < histories.entropy.length; i++) {
                const barHeight = histories.entropy[i] * height;
                const x = i * barWidth;
                entropyCtx.fillStyle = data.entropy > parseFloat(document.getElementById('thresh-val').textContent) ? '#ff0000' : '#cccccc';
                entropyCtx.fillRect(x, height - barHeight, barWidth - 1, barHeight);
            }

            // Title
            entropyCtx.fillStyle = '#ffffff';
            entropyCtx.font = '16px Courier New';
            entropyCtx.fillText('WATERFALL VIEW', 10, 20);
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
            switch (command) {
                case '/help':
                    response = 'Available commands:<br>settings - adjust threshold<br>/thresh [0-1] - set anomaly detection level<br>snapshot - export current data<br>graph - switch to graph view<br>waterfall - enable waterfall view';
                    break;
                case 'settings':
                    response = 'Use /thresh [value] to adjust threshold (0-1). Current: ' + document.getElementById('thresh-val').textContent;
                    break;
                case '/thresh':
                    response = 'Usage: /thresh [value]';
                    break;
                default:
                    if (command.startsWith('/thresh ')) {
                        const val = parseFloat(command.split(' ')[1]);
                        if (!isNaN(val) && val >= 0 && val <= 1) {
                            document.getElementById('thresh').value = val;
                            document.getElementById('thresh-val').textContent = val.toFixed(2);
                            fetch('/threshold?val=' + val, {method: 'POST'});
                            response = 'Threshold set to ' + val.toFixed(2);
                        } else {
                            response = 'Invalid value. Use 0-1.';
                        }
                    } else if (command === 'snapshot') {
                        exportLog();
                        response = 'Data snapshot exported!';
                    } else if (command === 'graph') {
                        viewMode = 'graph';
                        updateUI();
                        response = 'Switched to graph view';
                    } else if (command === 'waterfall') {
                        viewMode = 'waterfall';
                        updateUI();
                        response = 'Switched to waterfall view';
                    } else {
                        response = 'Unknown command. Type /help for list.';
                    }
                    break;
            }
            output.innerHTML += `<div style="color: #cccccc;">${response}</div>`;
            output.scrollTop = output.scrollHeight;
        }

        // Initialize
        initCanvases();
        setInterval(fetchData, 200);
        fetchData();
    </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleData() {
  DynamicJsonDocument doc(1024);
  doc["entropy"] = currentEntropy;
  doc["adc"] = adcVar;
  JsonArray sources = doc.createNestedArray("sources");
  sources.add(adcVar);
  sources.add(tempVar);
  sources.add(timingVar);
  sources.add(rssiVar);
  sources.add(clockVar);
  sources.add(touchVar);
  doc["bits"] = (currentEntropy > 0.5 ? 1 : 0);
  doc["spike"] = alertActive ? 1.0 : 0.0;
  String json;
  serializeJson(doc, json);
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
  }
  server.send(200, "text/plain", String(threshold));
}

void handleExport() {
  server.send(200, "text/plain", "Export triggered");
}