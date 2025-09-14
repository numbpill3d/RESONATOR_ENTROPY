/*
 * Resonator Entropy Reader for ESP32
 * Generates entropy readings from analog pins and provides visualization through a web interface
 * Supports WiFi configuration via AP mode
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Pin definitions
const int PIN_A = 36;  // Analog input pin
const int PIN_B = 39;  // Analog input pin
const int LED = 2;     // Built-in LED on most ESP32 boards

// WiFi configuration
const char* AP_SSID = "EntropyReader";  // Default AP mode SSID
const char* AP_PASS = "entropy123";     // Default AP mode password
bool apMode = true;                     // Start in AP mode by default

// Server and DNS for captive portal
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Buffer to store entropy history (limited size due to ESP32 memory constraints)
const int HISTORY_SIZE = 100;
unsigned long entropyHistory[HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;
unsigned long lastEntropy = 0;
unsigned long entropySum = 0;
unsigned long entropyCount = 0;

// Settings structure
struct Settings {
  char ssid[32] = "";
  char password[64] = "";
  bool loggingEnabled = false;
  int sampleRate = 100; // ms
} settings;

// Forward declarations for handler functions
void handleRoot();
void sendIndexHtml();
void handleEntropy();
void handleHistory();
void handleConfigPage();
void handleConfigSave();
void handleStatus();
void handleNotFound();

void setup() {
  // Initialize pins
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  
  // Initialize serial
  Serial.begin(115200);
  Serial.println("\nResonator Entropy Reader");
  
  // Initialize SPIFFS
  if(!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  
  // Load settings
  loadSettings();
  
  // Setup WiFi
  setupWiFi();
  
  // Setup server routes
  setupServer();
  
  Serial.println("Setup complete");
}

void loop() {
  // Handle DNS if in AP mode
  if (apMode) {
    dnsServer.processNextRequest();
  }
  
  // Handle web clients
  server.handleClient();
  
  // Read entropy from pins
  readEntropy();
  
  // Update LED based on entropy
  updateLED();
  
  // Wait based on configured sample rate
  delay(settings.sampleRate);
}

void readEntropy() {
  // Read analog values
  int a = analogRead(PIN_A);
  int b = analogRead(PIN_B);
  
  // Apply more sophisticated entropy calculation
  // XOR the bits to improve randomness
  unsigned long rawValue = (a << 16) | b;
  unsigned long entropyValue = rawValue ^ (rawValue >> 8);
  
  // Further mix with timing noise
  entropyValue ^= micros();
  
  // Store the new value
  lastEntropy = entropyValue;
  
  // Add to history buffer (circular buffer implementation)
  entropyHistory[historyIndex] = entropyValue;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
  
  // Update statistics
  entropySum += entropyValue;
  entropyCount++;
  
  // Log to serial if enabled
  if (settings.loggingEnabled) {
    Serial.print("Entropy: ");
    Serial.println(entropyValue);
  }
}

void updateLED() {
  // Threshold can be adjusted based on observed entropy values
  if (lastEntropy > 800000) {
    digitalWrite(LED, HIGH);
  } else {
    digitalWrite(LED, LOW);
  }
}

void loadSettings() {
  if (SPIFFS.exists("/settings.json")) {
    File file = SPIFFS.open("/settings.json", "r");
    if (file) {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        strlcpy(settings.ssid, doc["ssid"] | "", sizeof(settings.ssid));
        strlcpy(settings.password, doc["password"] | "", sizeof(settings.password));
        settings.loggingEnabled = doc["logging"] | false;
        settings.sampleRate = doc["sampleRate"] | 100;
      }
      file.close();
    }
  }
}

void saveSettings() {
  File file = SPIFFS.open("/settings.json", "w");
  if (file) {
    StaticJsonDocument<256> doc;
    doc["ssid"] = settings.ssid;
    doc["password"] = settings.password;
    doc["logging"] = settings.loggingEnabled;
    doc["sampleRate"] = settings.sampleRate;
    serializeJson(doc, file);
    file.close();
  }
}

void setupWiFi() {
  // If we have stored credentials, try to connect to WiFi
  if (strlen(settings.ssid) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.ssid, settings.password);
    
    // Wait up to 10 seconds for connection
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      apMode = false;
      return;
    }
    
    Serial.println("\nFailed to connect to WiFi");
  }
  
  // If we get here, either there are no stored credentials or connection failed
  // Start in AP mode with captive portal
  Serial.println("Starting AP mode");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Setup DNS server for captive portal
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  apMode = true;
}

void setupServer() {
  // Route for root page
  server.on("/", HTTP_GET, handleRoot);
  
  // Route for entropy data
  server.on("/entropy", HTTP_GET, handleEntropy);
  
  // Route for entropy history
  server.on("/history", HTTP_GET, handleHistory);
  
  // Route for WiFi configuration
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config", HTTP_POST, handleConfigSave);
  
  // Route for status information
  server.on("/status", HTTP_GET, handleStatus);
  
  // Handle not found
  server.onNotFound(handleNotFound);
  
  // Start server
  server.begin();
}

void handleRoot() {
  // Serve the index page from SPIFFS
  if (SPIFFS.exists("/index.html")) {
    File file = SPIFFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  } else {
    // Fallback if file doesn't exist
    sendIndexHtml();
  }
}

void sendIndexHtml() {
  const char indexHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Entropy Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      background: #000; 
      color: #0f0; 
      font-family: Arial, sans-serif; 
      text-align: center; 
      margin: 0;
      padding: 20px;
    }
    .container { max-width: 800px; margin: 0 auto; }
    #entropy { 
      font-size: 3em; 
      margin: 20px 0;
      font-family: monospace;
    }
    #canvas-container {
      width: 100%;
      height: 200px;
      background: #111;
      margin: 20px 0;
    }
    canvas {
      width: 100%;
      height: 100%;
    }
    .nav {
      display: flex;
      justify-content: center;
      gap: 15px;
      margin: 20px 0;
    }
    .nav a {
      color: #0f0;
      text-decoration: none;
      padding: 5px 10px;
      border: 1px solid #0f0;
      border-radius: 3px;
    }
    .nav a:hover {
      background: #0f0;
      color: #000;
    }
    .stats {
      display: flex;
      justify-content: space-around;
      margin: 20px 0;
      flex-wrap: wrap;
    }
    .stat-box {
      background: #111;
      padding: 10px;
      border-radius: 5px;
      min-width: 120px;
      margin: 5px;
    }
    h3 { margin: 5px 0; }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32 Entropy Monitor</h1>
    
    <div class="nav">
      <a href="/">Home</a>
      <a href="/config">Settings</a>
    </div>
    
    <div id="entropy">--</div>
    
    <div id="canvas-container">
      <canvas id="entropyCanvas"></canvas>
    </div>
    
    <div class="stats">
      <div class="stat-box">
        <h3>Average</h3>
        <div id="average">--</div>
      </div>
      <div class="stat-box">
        <h3>Samples</h3>
        <div id="count">--</div>
      </div>
      <div class="stat-box">
        <h3>WiFi</h3>
        <div id="wifi">--</div>
      </div>
    </div>
  </div>

  <script>
    // Graph setup
    const canvas = document.getElementById('entropyCanvas');
    const ctx = canvas.getContext('2d');
    let entropyData = [];
    let maxValue = 1000000;  // Initial max value, will adjust dynamically
    
    // Ensure canvas has correct size
    function resizeCanvas() {
      canvas.width = canvas.offsetWidth;
      canvas.height = canvas.offsetHeight;
    }
    window.addEventListener('resize', resizeCanvas);
    resizeCanvas();
    
    // Update entropy value
    function updateEntropy() {
      fetch("/entropy")
        .then(r => r.text())
        .then(txt => {
          document.getElementById('entropy').innerText = txt;
        });
    }
    
    // Update history and stats
    function updateHistory() {
      fetch("/history")
        .then(r => r.json())
        .then(data => {
          entropyData = data.history;
          document.getElementById('average').innerText = data.average;
          document.getElementById('count').innerText = data.count;
          drawGraph();
        });
    }
    
    // Update device status
    function updateStatus() {
      fetch("/status")
        .then(r => r.json())
        .then(data => {
          document.getElementById('wifi').innerText = 
            `${data.mode}: ${data.ip}`;
        });
    }
    
    // Draw the entropy graph
    function drawGraph() {
      if (!entropyData || entropyData.length === 0) return;
      
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      
      // Find max value for scaling (with 10% headroom)
      const currentMax = Math.max(...entropyData) * 1.1;
      maxValue = Math.max(maxValue, currentMax);
      
      // Draw background grid
      ctx.strokeStyle = '#333';
      ctx.lineWidth = 1;
      
      // Horizontal lines
      const gridLines = 5;
      for (let i = 0; i <= gridLines; i++) {
        const y = canvas.height - (i * canvas.height / gridLines);
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(canvas.width, y);
        ctx.stroke();
      }
      
      // Draw entropy line
      ctx.strokeStyle = '#0f0';
      ctx.lineWidth = 2;
      ctx.beginPath();
      
      for (let i = 0; i < entropyData.length; i++) {
        const x = i * canvas.width / (entropyData.length - 1);
        const y = canvas.height - (entropyData[i] / maxValue * canvas.height);
        
        if (i === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      }
      
      ctx.stroke();
    }
    
    // Update data regularly
    setInterval(updateEntropy, 500);
    setInterval(updateHistory, 2000);
    setInterval(updateStatus, 5000);
    
    // Initial updates
    updateEntropy();
    updateHistory();
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", indexHtml);
}

void handleEntropy() {
  // Return current entropy value
  server.send(200, "text/plain", String(lastEntropy));
}

void handleHistory() {
  // Create JSON with entropy history
  String response = "{\"history\":[";
  
  // Add history data
  for (int i = 0; i < historyCount; i++) {
    int idx = (historyIndex - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
    response += String(entropyHistory[idx]);
    if (i < historyCount - 1) response += ",";
  }
  
  // Add statistics
  response += "],\"average\":";
  response += String(entropyCount > 0 ? entropySum / entropyCount : 0);
  response += ",\"count\":";
  response += String(entropyCount);
  response += ",\"latest\":";
  response += String(lastEntropy);
  response += "}";
  
  server.send(200, "application/json", response);
}

void handleConfigPage() {
  // Configuration page HTML
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Entropy Monitor - Config</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      background: #000; 
      color: #0f0; 
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 20px;
    }
    .container { max-width: 600px; margin: 0 auto; }
    .nav {
      display: flex;
      justify-content: center;
      gap: 15px;
      margin: 20px 0;
    }
    .nav a {
      color: #0f0;
      text-decoration: none;
      padding: 5px 10px;
      border: 1px solid #0f0;
      border-radius: 3px;
    }
    .nav a:hover {
      background: #0f0;
      color: #000;
    }
    h1, h2 { text-align: center; }
    form {
      background: #111;
      padding: 20px;
      border-radius: 5px;
    }
    .form-group {
      margin-bottom: 15px;
    }
    label {
      display: block;
      margin-bottom: 5px;
    }
    input[type="text"], input[type="password"], input[type="number"] {
      width: 100%;
      padding: 8px;
      background: #222;
      border: 1px solid #333;
      color: #0f0;
      border-radius: 3px;
    }
    input[type="checkbox"] {
      margin-right: 10px;
    }
    button {
      background: #0f0;
      color: #000;
      border: none;
      padding: 10px 15px;
      border-radius: 3px;
      cursor: pointer;
      font-weight: bold;
    }
    .status {
      margin-top: 20px;
      padding: 10px;
      border-radius: 5px;
      text-align: center;
      display: none;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32 Entropy Monitor</h1>
    <h2>Configuration</h2>
    
    <div class="nav">
      <a href="/">Home</a>
      <a href="/config">Settings</a>
    </div>
    
    <form id="configForm">
      <div class="form-group">
        <label for="ssid">WiFi SSID:</label>
        <input type="text" id="ssid" name="ssid" value=")rawliteral";
  
  html += settings.ssid;
  
  html += R"rawliteral(">
      </div>
      <div class="form-group">
        <label for="password">WiFi Password:</label>
        <input type="password" id="password" name="password" value=")rawliteral";
  
  html += settings.password;
  
  html += R"rawliteral(">
      </div>
      <div class="form-group">
        <label for="sampleRate">Sample Rate (ms):</label>
        <input type="number" id="sampleRate" name="sampleRate" min="10" max="1000" value=")rawliteral";
  
  html += String(settings.sampleRate);
  
  html += R"rawliteral(">
      </div>
      <div class="form-group">
        <label>
          <input type="checkbox" id="logging" name="logging")rawliteral";
  
  if (settings.loggingEnabled) {
    html += " checked";
  }
  
  html += R"rawliteral(">
          Enable Serial Logging
        </label>
      </div>
      <button type="submit">Save Settings</button>
    </form>
    
    <div id="status" class="status"></div>
  </div>

  <script>
    document.getElementById('configForm').addEventListener('submit', function(e) {
      e.preventDefault();
      
      const form = e.target;
      const formData = new FormData(form);
      
      // Show saving status
      const status = document.getElementById('status');
      status.style.display = 'block';
      status.style.backgroundColor = '#004400';
      status.textContent = 'Saving settings...';
      
      fetch('/config', {
        method: 'POST',
        body: formData
      })
      .then(response => response.text())
      .then(text => {
        status.style.backgroundColor = '#004400';
        status.textContent = text;
        
        // Device will restart, redirect to home after a delay
        setTimeout(() => {
          window.location.href = '/';
        }, 5000);
      })
      .catch(error => {
        status.style.backgroundColor = '#440000';
        status.textContent = 'Error saving settings: ' + error;
      });
    });
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleConfigSave() {
  // Update settings from form submission
  if (server.hasArg("ssid")) {
    server.arg("ssid").toCharArray(settings.ssid, sizeof(settings.ssid));
  }
  
  if (server.hasArg("password")) {
    server.arg("password").toCharArray(settings.password, sizeof(settings.password));
  }
  
  settings.loggingEnabled = server.hasArg("logging");
  
  if (server.hasArg("sampleRate")) {
    settings.sampleRate = server.arg("sampleRate").toInt();
    if (settings.sampleRate < 10) settings.sampleRate = 10;
    if (settings.sampleRate > 1000) settings.sampleRate = 1000;
  }
  
  // Save settings
  saveSettings();
  
  // Send response
  server.send(200, "text/plain", "Settings saved. Restarting device...");
  
  // Restart ESP32 to apply new settings
  delay(1000);
  ESP.restart();
}

void handleStatus() {
  String response = "{";
  
  // WiFi status
  response += "\"mode\":\"" + String(apMode ? "AP" : "Station") + "\",";
  response += "\"ip\":\"" + String(apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  response += "\"ssid\":\"" + String(apMode ? AP_SSID : WiFi.SSID()) + "\",";
  
  // System status
  response += "\"uptime\":" + String(millis() / 1000) + ",";
  response += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  response += "\"sampleRate\":" + String(settings.sampleRate) + ",";
  response += "\"logging\":" + String(settings.loggingEnabled ? "true" : "false");
  
  response += "}";
  server.send(200, "application/json", response);
}

void handleNotFound() {
  // For captive portal, redirect all requests to the root
  if (apMode) {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}
