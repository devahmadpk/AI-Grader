#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

/* ================= CAMERA PINS (AI THINKER) ================= */
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
/* ============================================================ */

WebServer server(80);
Preferences preferences;

/* ================= STATE ================= */
bool cameraReady = false;
bool isAPMode = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_TIMEOUT = 10000;

/* ================= CAMERA SETUP ================= */
/* ================= CAMERA SETUP (BALANCED XGA) ================= */
void startCamera() {
  if (cameraReady) return;

  Serial.println("📷 Initializing camera...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
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
  
  // 20MHz is usually fine for XGA, but if you get glitches, go back to 10000000
  config.xclk_freq_hz = 20000000; 
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    Serial.println("✅ PSRAM Found: Enabling UXGA");
    
    // CHANGED: XGA (1024x768) - Good balance for text & speed
    config.frame_size = FRAMESIZE_UXGA; 
    
    // QUALITY: 10-12 is best for text. 
    // If it's still "too large" (slow transfer), increase this to 20-25.
    config.jpeg_quality = 12;           
    config.fb_count = 2; 
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Without PSRAM, XGA might fail. Stick to SVGA.
    Serial.println("⚠️ No PSRAM: Using SVGA");
    config.frame_size = FRAMESIZE_SVGA; 
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed with error 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }

  // OPTIONAL: Keep these settings for better text contrast
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 1);   
  s->set_contrast(s, 1);     
  s->set_saturation(s, -2);  

  Serial.println("✅ Camera initialized successfully");
  cameraReady = true;
}

/* ================= ACCESS POINT ================= */
void startAPMode() {
  if (isAPMode) return;
  Serial.println("🟡 Starting AP Mode (ESP32 Cam)");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32 Cam");
  Serial.print("🔵 AP IP Address: ");
  Serial.println(WiFi.softAPIP());
  isAPMode = true;
}

/* ================= WIFI ================= */
bool connectWiFi() {
  preferences.begin("wifi-config", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (ssid == "") {
    startAPMode();
    return false;
  }

  Serial.print("🔄 Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Important for consistent streaming
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected");
    Serial.println(WiFi.localIP());
    if (MDNS.begin("esp32-cam")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("🌐 mDNS started: esp32-cam.local");
    }
    isAPMode = false;
    startCamera();
    return true;
  }

  Serial.println("\n❌ WiFi connection failed");
  startAPMode();
  return false;
}

/* ================= CAPTURE HANDLER (ROBUST) ================= */
void handleCapture() {
  Serial.println("📸 Capture Requested");

  if (!cameraReady) {
    server.send(503, "text/plain", "Camera not ready");
    return;
  }
  
  // 1. Flush & Capture
  delay(100); 
  camera_fb_t *fb = esp_camera_fb_get(); // Discard stale frame
  if (fb) {
      esp_camera_fb_return(fb); 
      delay(50); 
  }

  fb = esp_camera_fb_get(); // Get fresh frame
  if (!fb) {
      server.send(500, "text/plain", "Capture failed");
      return;
  }

  Serial.printf("📤 Sending %u bytes...\n", fb->len);

  // 2. Send Headers
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  
  // 3. Send Data with RETRY LOGIC
  WiFiClient client = server.client();
  uint8_t *data = fb->buf;
  size_t remaining = fb->len;
  size_t blockSize = 1024; // Small chunk size is safer
  size_t totalSent = 0;
  
  while (remaining > 0) {
    if (!client.connected()) {
      Serial.println("❌ Client disconnected unexpectedly");
      break;
    }

    size_t toSend = (remaining < blockSize) ? remaining : blockSize;
    size_t sent = 0;
    int retries = 0;
    
    // Aggressive Retry: Try 50 times with small delays
    while (sent == 0 && retries < 50) {
      sent = client.write(data, toSend);
      if (sent == 0) {
        retries++;
        delay(20); 
        yield();
      }
    }

    if (sent != toSend) {
      Serial.printf("❌ Write mismatch! Tried: %u, Sent: %u\n", toSend, sent);
      break;
    }

    totalSent += sent;
    data += sent;
    remaining -= sent;
    
    yield(); 
  }

  client.flush();
  
  if (totalSent == fb->len) {
    Serial.println("✅ Image Sent Successfully!");
  } else {
    Serial.printf("❌ Sent %u / %u bytes (Incomplete)\n", totalSent, fb->len);
  }

  esp_camera_fb_return(fb);
}

/* ================= CONFIG HANDLER ================= */
void handleConfigure() {
  preferences.begin("wifi-config", false);
  preferences.putString("ssid", server.arg("ssid"));
  preferences.putString("pass", server.arg("pass"));
  preferences.end();
  server.send(200, "text/plain", "Saved. Restarting...");
  delay(1000);
  ESP.restart();
}

/* ================= SETUP ================= */
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);

  // Boost WiFi Power
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  Serial.println("\n🚀 ESP32-CAM Booting...");
  connectWiFi();

  server.on("/configure", handleConfigure);
  server.on("/capture", handleCapture);
  
  server.begin();
  Serial.println("🌐 HTTP server started");
}

/* ================= LOOP ================= */
void loop() {
  server.handleClient();

  // Auto Reconnect
  if (!isAPMode && WiFi.status() != WL_CONNECTED && millis() - lastWiFiCheck > WIFI_TIMEOUT) {
    lastWiFiCheck = millis();
    startAPMode();
  }
}
