// ESP32-CAM + LCD: Face Lock System with Improved Python Sync

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define CAMERA_MODEL_AI_THINKER
#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#endif

// Pin definitions to match Python expectations
#define BUZZER_PIN 12
#define LCD_SDA 14
#define LCD_SCL 15
#define BUTTON_PIN 13
#define LED_PIN 4
#define RED_LED_PIN 2    // Red LED for locked state
#define GREEN_LED_PIN 16 // Green LED for unlocked state

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Custom characters for lock/unlock display
byte lock0[8] = { B00000, B00000, B00000, B00001, B00001, B00011, B00011, B00011 };
byte lock1[8] = { B00000, B00000, B01110, B11011, B10001, B00000, B00000, B00000 };
byte lock2[8] = { B00000, B00000, B00000, B10000, B10000, B11000, B11000, B11000 };
byte lock3[8] = { B01111, B11111, B11111, B11111, B11111, B11111, B11111, B11111 };
byte lock4[8] = { B11111, B11111, B11111, B10001, B10001, B11011, B11011, B11111 };
byte lock5[8] = { B11110, B11111, B11111, B11111, B11111, B11111, B11111, B11111 };


byte unlock0[8] = { B00000, B00000, B00000, B00000, B00000, B00000, B00000, B00000 };
byte unlock1[8] = { B00000, B00000, B00000, B00000, B00000, B00000, B00000, B00000 };
byte unlock2[8] = { B00000, B00000, B00000, B00000, B00000, B00000, B00000, B00000 };
byte unlock3[8] = { B01111, B11111, B11111, B11111, B11111, B11111, B11111, B11111 };
byte unlock4[8] = { B11111, B11111, B11111, B10001, B10001, B11011, B11011, B11111 };
byte unlock5[8] = { B11110, B11111, B11111, B11111, B11111, B11111, B11111, B11111 };

// Function to load locked emoji
void loadLockedEmoji() {
  lcd.createChar(0, lock0);
  lcd.createChar(1, lock1);
  lcd.createChar(2, lock2);
  lcd.createChar(3, lock3);
  lcd.createChar(4, lock4);
  lcd.createChar(5, lock5);
}

// Function to load unlocked emoji
void loadUnlockedEmoji() {
  lcd.createChar(6, unlock0);
  lcd.createChar(7, unlock1);
  lcd.createChar(8, unlock2);
  lcd.createChar(9, unlock3);
  lcd.createChar(10, unlock4);
  lcd.createChar(11, unlock5);
}

// System state variables
bool currentState = false;  // false = locked, true = unlocked
bool lastState = false;
unsigned long lastButtonPress = 0;
unsigned long lastHeartbeat = 0;
unsigned long lockTimeout = 0;
String currentUser = "";
float lastConfidence = 0.0;

// Timing constants
const unsigned long debounceDelay = 300;
const unsigned long heartbeatInterval = 10000;  // Send status every 2 seconds
const unsigned long autoLockDelay = 30000;     // Auto-lock after 30 seconds
const unsigned long commandTimeout = 1000;     // Command response timeout

// HTTP server handles
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// =============================================================================
// HTML WEB INTERFACE - PASTE YOUR HTML CODE HERE
// =============================================================================
/*
 * TODO: PASTE THE HTML WEB INTERFACE CODE HERE
 * 
 * Replace this comment block with the HTML code from paste-2.txt
 * Make sure to:
 * 1. Remove or comment out any camera stream references to avoid conflicts
 * 2. Keep the existing endpoints (/capture, /health) working
 * 3. Format as a const char* variable for Arduino
 * 
 * Example format:
 * const char index_html[] PROGMEM = R"rawliteral(
 * <!DOCTYPE html>
 * <html>
 * ... your HTML code here ...
 * </html>
 * )rawliteral";
 */
// =============================================================================

<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Face Lock System</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            color: #333;
        }
        .container { max-width: 1200px; margin: 0 auto; padding: 20px; }
        .header { text-align: center; margin-bottom: 30px; color: white; }
        .header h1 { font-size: 2.5rem; margin-bottom: 10px; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); }
        .status-badge {
            display: inline-block; padding: 8px 20px; border-radius: 25px;
            font-weight: bold; font-size: 1.1rem; text-transform: uppercase;
            letter-spacing: 1px; animation: pulse 2s infinite;
        }
        .status-locked { background: linear-gradient(45deg, #ff6b6b, #ee5a24); color: white; box-shadow: 0 4px 15px rgba(255, 107, 107, 0.4); }
        .status-unlocked { background: linear-gradient(45deg, #00d2d3, #54a0ff); color: white; box-shadow: 0 4px 15px rgba(0, 210, 211, 0.4); }
        @keyframes pulse { 0% { transform: scale(1); } 50% { transform: scale(1.05); } 100% { transform: scale(1); } }
        .main-grid { display: grid; grid-template-columns: 1fr 300px; gap: 20px; margin-bottom: 20px; }
        .camera-section, .controls-section, .info-card {
            background: rgba(255, 255, 255, 0.95); border-radius: 15px; padding: 20px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.1); backdrop-filter: blur(10px);
        }
        .camera-container { position: relative; border-radius: 10px; overflow: hidden; background: #000; aspect-ratio: 4/3; }
        #camera-stream { width: 100%; height: 100%; object-fit: cover; }
        .camera-overlay {
            position: absolute; top: 10px; left: 10px; background: rgba(0,0,0,0.7);
            color: white; padding: 8px 12px; border-radius: 5px; font-size: 0.9rem;
        }
        .control-button {
            width: 100%; padding: 12px; margin: 8px 0; border: none; border-radius: 8px;
            font-weight: bold; cursor: pointer; transition: all 0.3s ease;
            text-transform: uppercase; letter-spacing: 1px;
        }
        .btn-unlock { background: linear-gradient(45deg, #00d2d3, #54a0ff); color: white; }
        .btn-unlock:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(0, 210, 211, 0.4); }
        .btn-lock { background: linear-gradient(45deg, #ff6b6b, #ee5a24); color: white; }
        .btn-lock:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(255, 107, 107, 0.4); }
        .btn-capture { background: linear-gradient(45deg, #feca57, #ff9ff3); color: white; }
        .btn-capture:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(254, 202, 87, 0.4); }
        .info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; }
        .info-card h3 { color: #5f27cd; margin-bottom: 15px; font-size: 1.2rem; display: flex; align-items: center; gap: 10px; }
        .status-item { display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-bottom: 1px solid #eee; }
        .status-item:last-child { border-bottom: none; }
        .status-value { font-weight: bold; color: #5f27cd; }
        .activity-log { max-height: 200px; overflow-y: auto; background: #f8f9fa; border-radius: 8px; padding: 10px; }
        .log-entry { padding: 5px 0; font-size: 0.9rem; border-bottom: 1px solid #eee; }
        .log-entry:last-child { border-bottom: none; }
        .log-timestamp { color: #666; font-size: 0.8rem; }
        .connection-status { position: fixed; top: 20px; right: 20px; padding: 10px 15px; border-radius: 25px; font-weight: bold; font-size: 0.9rem; z-index: 1000; }
        .connected { background: #2ed573; color: white; }
        .disconnected { background: #ff3838; color: white; }
        .user-info { background: linear-gradient(45deg, #a8edea, #fed6e3); padding: 10px; border-radius: 8px; margin-top: 10px; }
        .confidence-bar { background: #e0e0e0; border-radius: 10px; overflow: hidden; height: 8px; margin-top: 5px; }
        .confidence-fill { height: 100%; background: linear-gradient(45deg, #00d2d3, #54a0ff); transition: width 0.3s ease; }
        @media (max-width: 768px) { .main-grid { grid-template-columns: 1fr; } .info-grid { grid-template-columns: 1fr; } .header h1 { font-size: 2rem; } }
    </style>
</head>
<body>
    <div class="connection-status connected" id="connectionStatus">✅ Connected</div>
    
    <div class="container">
        <div class="header">
            <h1>🔐 Face Lock System</h1>
            <div class="status-badge status-locked" id="systemStatus">🔒 LOCKED</div>
        </div>

        <div class="main-grid">
            <div class="camera-section">
                <h3>📹 Live Camera Feed</h3>
                <div class="camera-container">
                    <img id="camera-stream" src="/stream" alt="Camera Feed">
                    <div class="camera-overlay">
                        <div>FPS: <span id="fps">--</span></div>
                        <div>Status: <span id="streamStatus">Active</span></div>
                    </div>
                </div>
            </div>

            <div class="controls-section">
                <h3>🎮 Controls</h3>
                <button class="control-button btn-unlock" onclick="sendCommand('unlock')">🔓 Unlock System</button>
                <button class="control-button btn-lock" onclick="sendCommand('lock')">🔒 Lock System</button>
                <button class="control-button btn-capture" onclick="captureImage()">📸 Capture Image</button>
                
                <div class="user-info" id="userInfo" style="display: none;">
                    <div><strong>Current User:</strong> <span id="currentUser">-</span></div>
                    <div><strong>Confidence:</strong> <span id="confidence">-</span>%</div>
                    <div class="confidence-bar">
                        <div class="confidence-fill" id="confidenceFill" style="width: 0%"></div>
                    </div>
                </div>
            </div>
        </div>

        <div class="info-grid">
            <div class="info-card">
                <h3>📊 System Status</h3>
                <div class="status-item"><span>System State:</span><span class="status-value" id="lockState">LOCKED</span></div>
                <div class="status-item"><span>Uptime:</span><span class="status-value" id="uptime">--</span></div>
                <div class="status-item"><span>Free Memory:</span><span class="status-value" id="freeMemory">--</span></div>
                <div class="status-item"><span>Last Update:</span><span class="status-value" id="lastUpdate">--</span></div>
            </div>

            <div class="info-card">
                <h3>📝 Activity Log</h3>
                <div class="activity-log" id="activityLog">
                    <div class="log-entry">
                        <div class="log-timestamp" id="initTime"></div>
                        <div>System initialized</div>
                    </div>
                </div>
            </div>

            <div class="info-card">
                <h3>📡 Network Info</h3>
                <div class="status-item"><span>ESP32 IP:</span><span class="status-value">192.168.4.1</span></div>
                <div class="status-item"><span>WiFi Network:</span><span class="status-value">F4C3_TR4CK3R</span></div>
                <div class="status-item"><span>Connected Clients:</span><span class="status-value" id="clientCount">1</span></div>
                <div class="status-item"><span>Stream URL:</span><span class="status-value">:80/stream</span></div>
            </div>
        </div>
    </div>

    <script>
        let activityLog = [];
        let frameCount = 0;
        let lastFpsTime = Date.now();

        document.addEventListener('DOMContentLoaded', function() {
            document.getElementById('initTime').textContent = new Date().toLocaleTimeString();
            startHealthCheck();
            monitorCameraFeed();
        });

        function monitorCameraFeed() {
            const img = document.getElementById('camera-stream');
            img.onload = function() {
                frameCount++;
                const now = Date.now();
                if (now - lastFpsTime >= 1000) {
                    const fps = Math.round(frameCount * 1000 / (now - lastFpsTime));
                    document.getElementById('fps').textContent = fps;
                    frameCount = 0;
                    lastFpsTime = now;
                }
            };
        }

        function startHealthCheck() {
            setInterval(checkHealth, 2000);
            checkHealth();
        }

        async function checkHealth() {
            try {
                const response = await fetch('/api/status');
                const data = await response.json();
                updateSystemStatus(data);
            } catch (error) {
                console.log('Health check failed:', error);
            }
        }

        function updateSystemStatus(data) {
            const isLocked = data.locked;
            const systemStatus = document.getElementById('systemStatus');
            const lockState = document.getElementById('lockState');
            
            if (isLocked) {
                systemStatus.className = 'status-badge status-locked';
                systemStatus.innerHTML = '🔒 LOCKED';
                lockState.textContent = 'LOCKED';
                lockState.style.color = '#ff3838';
                document.getElementById('userInfo').style.display = 'none';
            } else {
                systemStatus.className = 'status-badge status-unlocked';
                systemStatus.innerHTML = '🔓 UNLOCKED';
                lockState.textContent = 'UNLOCKED';
                lockState.style.color = '#2ed573';
                
                if (data.user && data.user !== '') {
                    document.getElementById('userInfo').style.display = 'block';
                    document.getElementById('currentUser').textContent = data.user;
                    document.getElementById('confidence').textContent = data.confidence;
                    document.getElementById('confidenceFill').style.width = data.confidence + '%';
                }
            }
            
            document.getElementById('uptime').textContent = formatUptime(data.uptime);
            document.getElementById('freeMemory').textContent = formatBytes(data.freeMemory);
            document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
            
            if (data.lastActivity && data.lastActivity !== activityLog[0]?.message) {
                addLogEntry(data.lastActivity);
            }
        }

        async function sendCommand(command) {
            try {
                const response = await fetch('/api/command', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ command: command })
                });
                
                if (response.ok) {
                    addLogEntry(`Command sent: ${command.toUpperCase()}`);
                    setTimeout(checkHealth, 500);
                } else {
                    addLogEntry(`Command failed: ${command}`);
                }
            } catch (error) {
                addLogEntry('Command error: ' + error.message);
            }
        }

        async function captureImage() {
            try {
                addLogEntry('Capturing image...');
                const response = await fetch('/capture');
                if (response.ok) {
                    const blob = await response.blob();
                    const url = URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = `face_capture_${Date.now()}.jpg`;
                    a.click();
                    addLogEntry('Image captured and downloaded');
                    URL.revokeObjectURL(url);
                } else {
                    throw new Error('Capture failed');
                }
            } catch (error) {
                addLogEntry('Image capture failed: ' + error.message);
            }
        }

        function addLogEntry(message) {
            const timestamp = new Date().toLocaleTimeString();
            activityLog.unshift({ timestamp, message });
            if (activityLog.length > 10) activityLog = activityLog.slice(0, 10);
            updateActivityLog();
        }

        function updateActivityLog() {
            const logContainer = document.getElementById('activityLog');
            logContainer.innerHTML = activityLog.map(entry => `
                <div class="log-entry">
                    <div class="log-timestamp">${entry.timestamp}</div>
                    <div>${entry.message}</div>
                </div>
            `).join('');
        }

        function formatUptime(milliseconds) {
            const seconds = Math.floor(milliseconds / 1000);
            const minutes = Math.floor(seconds / 60);
            const hours = Math.floor(minutes / 60);
            const days = Math.floor(hours / 24);
            
            if (days > 0) return `${days}d ${hours % 24}h ${minutes % 60}m`;
            if (hours > 0) return `${hours}h ${minutes % 60}m`;
            if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
            return `${seconds}s`;
        }

        function formatBytes(bytes) {
            if (bytes === 0) return '0 B';
            const k = 1024;
            const sizes = ['B', 'KB', 'MB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
        }
    </script>
</body>
</html>
)rawliteral";




void buzzPattern(int pattern) {
  switch(pattern) {
    case 1: // Unlock - happy beeps
      for (int i = 0; i < 2; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(50);
      }
      break;
    case 2: // Lock - single beep
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      break;
    case 3: // Error/Registration - three short beeps
      for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(100);
      }
      break;
    case 4: // System ready - ascending beeps
      int frequencies[] = {100, 150, 200};
      for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(frequencies[i]);
        digitalWrite(BUZZER_PIN, LOW);
        delay(50);
      }
      break;
  }
}

void updateLED() {
  if (currentState) {
    // Unlocked state - Green LED on, Red LED off
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
    
    // Main LED slow pulse when unlocked
    int brightness = (sin(millis() / 500.0) + 1) * 127;
    analogWrite(LED_PIN, brightness);
  } else {
    // Locked state - Red LED on, Green LED off
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, LOW);
    
    // Main LED off when locked
    digitalWrite(LED_PIN, LOW);
  }
}
void showLocked() {
  loadLockedEmoji();
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("LOCKED");
  lcd.setCursor(12, 0);
  lcd.write(byte(0)); lcd.write(byte(1)); lcd.write(byte(2));
  lcd.setCursor(12, 1);
  lcd.write(byte(3)); lcd.write(byte(4)); lcd.write(byte(5));
  lcd.setCursor(0, 1);
  lcd.print("System Ready");
}

void showUnlocked(String username = "", float confidence = 0.0) {
  loadUnlockedEmoji();
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("UNLOCKED");
  lcd.setCursor(12, 0);
  lcd.write(byte(6)); lcd.write(byte(7)); lcd.write(byte(8));
  lcd.setCursor(12, 1);
  lcd.write(byte(9)); lcd.write(byte(10)); lcd.write(byte(11));

  lcd.setCursor(0, 1);
  
  if (username != "") {
    lcd.print(username.length() > 11 ? username.substring(0, 11) : username);
  } else {
    lcd.print("Access OK");
  }

  lockTimeout = millis() + autoLockDelay;
}

void showStatus(String message, bool temporary = true) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Status:");
  lcd.setCursor(0, 1);
  
  if (message.length() > 16) {
    lcd.print(message.substring(0, 16));
  } else {
    lcd.print(message);
  }
  
  if (temporary) {
    delay(2000);
    if (currentState) {
      showUnlocked(currentUser, lastConfidence);
    } else {
      showLocked();
    }
  }
}

void showRegistration() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("REGISTERING...");
  lcd.setCursor(0, 1);
  lcd.print("Look at camera");
}

// Enhanced stream handler with better error handling
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[128];

  res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "30");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
      break;
    }

    if (fb->format != PIXFORMAT_JPEG) {
      bool jpeg_converted = frame2jpg(fb, 85, &_jpg_buf, &_jpg_buf_len);
      esp_camera_fb_return(fb);
      fb = NULL;
      if (!jpeg_converted) {
        Serial.println("JPEG compression failed");
        res = ESP_FAIL;
        break;
      }
    } else {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, "--frame\r\n", 9);
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, 128, 
        "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %lu\r\n\r\n", 
        _jpg_buf_len, millis());
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, "\r\n", 2);
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }

    if (res != ESP_OK) {
      Serial.println("Stream connection closed");
      break;
    }
    
    // Small delay to prevent overwhelming the connection
    delay(30);
  }

  return res;
}

// Single capture handler
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=face_capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  
  Serial.println("Image captured and sent");
  return res;
}

// Health check endpoint
static esp_err_t health_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  String response = "{\"status\":\"OK\",\"locked\":" + String(currentState ? "false" : "true") + 
                   ",\"uptime\":" + String(millis()) + "}";
  
  httpd_resp_send(req, response.c_str(), response.length());
  return ESP_OK;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_open_sockets = 7;
  config.task_priority = 5;

  httpd_uri_t health_uri = {
    .uri       = "/health",
    .method    = HTTP_GET,
    .handler   = health_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  Serial.printf("Starting camera server on port: %d\n", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &health_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    httpd_register_uri_handler(camera_httpd, &index_uri);
    Serial.println("Camera server started successfully");
  } else {
    Serial.println("Failed to start camera server");
  }
}

void handleSerialCommand(String command) {
  command.trim();
  command.toLowerCase();
  
  Serial.println("CMD_RECV: " + command);
  
  if (command == "ping") {
    Serial.println("READY");
  }
  else if (command == "lock_off" || command == "unlock") {
    currentState = true;
    showUnlocked();
    buzzPattern(1);
    lockTimeout = millis() + autoLockDelay;
    Serial.println("ACK_UNLOCK");
  }
  else if (command == "lock_on" || command == "lock") {
    currentState = false;
    showLocked();
    buzzPattern(2);
    lockTimeout = 0;
    currentUser = "";
    lastConfidence = 0.0;
    Serial.println("ACK_LOCK");
  }
  else if (command.startsWith("unlock_user:")) {
    String userData = command.substring(12);  // Remove "unlock_user:"
    int colonPos = userData.indexOf(':');
    
    if (colonPos > 0) {
      currentUser = userData.substring(0, colonPos);
      lastConfidence = userData.substring(colonPos + 1).toFloat();
    } else {
      currentUser = userData;
      lastConfidence = 0.0;
    }
    
    currentState = true;
    showUnlocked(currentUser, lastConfidence);
    buzzPattern(1);
    lockTimeout = millis() + autoLockDelay;
    Serial.println("ACK_UNLOCK_USER:" + currentUser);
  }
  else if (command == "register_start") {
    showRegistration();
    buzzPattern(3);
    Serial.println("ACK_REGISTER_START");
  }
  else if (command == "register_complete") {
    showStatus("Registration OK", true);
    buzzPattern(1);
    Serial.println("ACK_REGISTER_COMPLETE");
  }
  else if (command == "register_failed") {
    showStatus("Reg. Failed", true);
    buzzPattern(3);
    Serial.println("ACK_REGISTER_FAILED");
  }
  else if (command == "reset") {
    currentState = false;
    currentUser = "";
    lastConfidence = 0.0;
    lockTimeout = 0;
    showStatus("System Reset", true);
    buzzPattern(4);
    Serial.println("ACK_RESET");
  }
  else if (command == "status") {
    String statusMsg = "STATUS:" + String(currentState ? "UNLOCKED" : "LOCKED");
    if (currentUser != "") {
      statusMsg += ":" + currentUser + ":" + String(lastConfidence, 1);
    }
    Serial.println(statusMsg);
  }
  else {
    Serial.println("ERROR_UNKNOWN_COMMAND");
  }
}

void sendHeartbeat() {
  String heartbeat = "HEARTBEAT:" + String(currentState ? "UNLOCKED" : "LOCKED") + 
                    ":" + String(millis()) + ":" + String(ESP.getFreeHeap());
  Serial.println(heartbeat);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.setDebugOutput(false);  // Reduce debug spam
  
  // Wait for serial connection
  delay(1000);
  Serial.println();
  Serial.println("ESP32_FACE_LOCK_INIT");

  // Initialize I2C and LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();

  // Initialize pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // Camera configuration for face detection optimization
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  // Optimized settings for face recognition
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;     // 640x480 - good for face detection
    config.jpeg_quality = 12;              // Balance quality vs speed
    config.fb_count = 2;                   // Double buffering
  } else {
    config.frame_size = FRAMESIZE_CIF;     // 352x288 - smaller for DRAM
    config.jpeg_quality = 15;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("CAMERA_INIT_FAILED:0x%x\n", err);
    showStatus("Camera Failed!", false);
    while (true) delay(1000);
  }

  // Optimize camera settings for face detection
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_vflip(s, 0);        // Flip vertically
    s->set_hmirror(s, 0);      // Mirror horizontally
    s->set_whitebal(s, 1);     // Auto white balance
    s->set_awb_gain(s, 1);     // Auto white balance gain
    s->set_wb_mode(s, 0);      // Auto white balance mode
    s->set_exposure_ctrl(s, 1); // Auto exposure
    s->set_aec2(s, 0);         // Auto exposure correction
    s->set_ae_level(s, 0);     // Auto exposure level
    s->set_aec_value(s, 300);  // Auto exposure value
    s->set_gain_ctrl(s, 1);    // Auto gain control
    s->set_agc_gain(s, 0);     // Auto gain value
    s->set_gainceiling(s, (gainceiling_t)0); // Gain ceiling
    s->set_brightness(s, 0);   // Brightness
    s->set_contrast(s, 0);     // Contrast
    s->set_saturation(s, 0);   // Saturation
    s->set_special_effect(s, 0); // No special effects
    s->set_colorbar(s, 0);     // No color bar
  }

  // Setup WiFi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_FaceLock", "facelock123");
  
  // Configure AP settings for better stability
  WiFi.softAPConfig(
    IPAddress(192, 168, 4, 1),    // IP
    IPAddress(192, 168, 4, 1),    // Gateway
    IPAddress(255, 255, 255, 0)   // Subnet
  );
  
  IPAddress IP = WiFi.softAPIP();
  Serial.println("WIFI_AP_READY:" + IP.toString());

  // Start camera server
  startCameraServer();
  
  showStatus("WiFi: " + IP.toString(), false);
  delay(2000);
  showLocked();
  
  // System ready notification
  buzzPattern(4);
  Serial.println("SYSTEM_READY");
  
  lastHeartbeat = millis();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Handle Serial Commands from Python
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    handleSerialCommand(command);
  }

  // Handle manual button press
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (currentTime - lastButtonPress > debounceDelay) {
      currentState = !currentState;
      
      if (currentState) {
        showUnlocked("Manual", 100.0);
        buzzPattern(1);
        lockTimeout = millis() + autoLockDelay;
        Serial.println("MANUAL_UNLOCK");
      } else {
        showLocked();
        buzzPattern(2);
        lockTimeout = 0;
        currentUser = "";
        Serial.println("MANUAL_LOCK");
      }
      
      lastButtonPress = currentTime;
    }
  }

  // Handle auto-lock timeout
  if (currentState && lockTimeout > 0 && currentTime > lockTimeout) {
    currentState = false;
    showLocked();
    buzzPattern(2);
    lockTimeout = 0;
    currentUser = "";
    lastConfidence = 0.0;
    Serial.println("AUTO_LOCK");
  }

  // Send periodic heartbeat
  if (currentTime - lastHeartbeat > heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeat = currentTime;
  }

  // Update LED state
  updateLED();

  // State change detection
  if (currentState != lastState) {
    Serial.println("STATE_CHANGE:" + String(currentState ? "UNLOCKED" : "LOCKED"));
    lastState = currentState;
  }

  delay(50);  // Prevent excessive CPU usage
}
