#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <HTTPClient.h>

/* ================= PIN DEFINITIONS ================= */
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_MOSI  11
#define TFT_CLK   12
#define TFT_MISO  13 

#define TOUCH_CS   4
#define TOUCH_IRQ  5
#define TOUCH_MOSI 17
#define TOUCH_CLK  16
#define TOUCH_MISO 18

/* ================= GLOBALS ================= */
SPIClass touchSpi(HSPI); 
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

WebServer server(80);
Preferences preferences;

// STATE VARIABLES
bool isAPMode = false;
bool isScanReady = false;   
bool isReviewMode = false; // "Image Captured" Screen
bool waitingResult = false; 
bool isScanning = false;    

unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_TIMEOUT = 10000; 

String lastS3IP = "Offline";
String lastCamIP = "Offline";
String serverIP = ""; 

unsigned long lastTouchTime = 0;

// BUTTONS
#define BTN_X 40
#define BTN_Y 120
#define BTN_W 240
#define BTN_H 60

/* ================= UI FUNCTIONS ================= */

void drawBoot() {
  tft.fillScreen(ILI9341_BLACK); 
  tft.setTextColor(ILI9341_CYAN); 
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.println("Booting Device...");
}

void drawScanReadyScreen() {
  tft.fillScreen(ILI9341_WHITE);
  tft.fillRect(BTN_X, BTN_Y, BTN_W, BTN_H, ILI9341_GREEN);
  tft.drawRect(BTN_X, BTN_Y, BTN_W, BTN_H, ILI9341_BLACK);
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(BTN_X + 15, BTN_Y + 22);
  tft.println("Scan New Document");
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(60, 280);
  tft.println("Touch top to cancel");
}

// *** REVIEW SCREEN (No Image, Just Text) ***
void drawReviewScreen() {
  tft.fillScreen(ILI9341_BLACK);
  
  // Big "OK" Icon
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(4);
  tft.setCursor(120, 60);
  tft.println("OK");
  
  // Text
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(45, 120);
  tft.println("Image Captured!");
  
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(60, 150);
  tft.println("Ready to Grade?");

  // Draw Buttons at Bottom
  // "Back" Button (Red)
  tft.fillRect(10, 260, 100, 40, ILI9341_RED);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(30, 272);
  tft.println("BACK");
  
  // "Okay" Button (Green)
  tft.fillRect(130, 260, 100, 40, ILI9341_GREEN);
  tft.setTextColor(ILI9341_BLACK);
  tft.setCursor(150, 272);
  tft.println("OKAY");
}

void drawMessageScreen(String lines[], int count) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  int y = 50;
  for (int i = 0; i < count; i++) {
    tft.setCursor(10, y);
    tft.println(lines[i]);
    y += 40;
  }
}

void drawDashboard(String s3IP, String camIP) {
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 320, 40, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("DASHBOARD");

  tft.setCursor(10, 60);
  tft.println("ESP32-S3:");
  tft.setTextColor(s3IP == "Offline" ? ILI9341_RED : ILI9341_GREEN);
  tft.println(s3IP);

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 130);
  tft.println("ESP32-CAM:");
  tft.setTextColor(camIP == "Offline" ? ILI9341_RED : ILI9341_GREEN);
  tft.println(camIP);

  tft.setCursor(10, 200);
  tft.setTextSize(1);
  if (serverIP == "") {
      tft.setTextColor(ILI9341_ORANGE);
      tft.println("Waiting for Python Script...");
  } else {
      tft.setTextColor(ILI9341_CYAN);
      tft.println("System Ready - Server Connected");
  }
}

void drawResultScreen(String totalScore, JsonArray questions, int page) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, 10);
  tft.print("Score: "); tft.println(totalScore);
  
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(160, 15);
  tft.print("Page: "); tft.println(page);

  tft.drawFastHLine(0, 40, 240, ILI9341_DARKGREY);

  int startY = 55; int rowHeight = 35;
  int col1_x = 10; int col2_x = 130; 

  for (int i = 0; i < questions.size(); i++) {
    JsonObject q = questions[i];
    String qNum = q["q"].as<String>();
    String opt = q["opt"].as<String>();
    bool correct = q["correct"];

    tft.setTextColor(correct ? ILI9341_GREEN : ILI9341_RED);
    
    int xPos, yPos;
    if (i < 5) { xPos = col1_x; yPos = startY + (i * rowHeight); } 
    else { xPos = col2_x; yPos = startY + ((i - 5) * rowHeight); }

    tft.setCursor(xPos, yPos);
    tft.setTextSize(2);
    tft.print(qNum + ": " + opt);
  }
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(10, 300);
  tft.println("Tap anywhere to return");
}

void drawHotspotMode() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_RED); 
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("Connection Failed");
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 80);
  tft.println("Connect to Hotspot:");
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(10, 100);
  tft.println("'ESP32 Screen'");
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 130);
  tft.println("Go to: 192.168.4.1");
}

/* ================= ACTIONS ================= */
void triggerScan() {
  if (isScanning || serverIP == "") return;
  isScanning = true;
  
  tft.fillRect(BTN_X, BTN_Y, BTN_W, BTN_H, ILI9341_DARKGREEN);
  tft.setCursor(BTN_X + 15, BTN_Y + 22);
  tft.setTextColor(ILI9341_WHITE);
  tft.println("Requesting...");

  HTTPClient http;
  String url = "http://" + serverIP + ":5000/start_scan"; 
  http.begin(url);
  int code = http.GET();
  http.end();
  
  if(code <= 0) {
     isScanning = false;
     drawScanReadyScreen(); 
  }
}

void confirmGrading() {
  HTTPClient http;
  String url = "http://" + serverIP + ":5000/process_grading"; 
  http.begin(url);
  http.GET();
  http.end();
}

/* ================= TOUCH LOGIC ================= */
void checkTouch() {
  if (ts.touched() && (millis() - lastTouchTime > 300)) {
    lastTouchTime = millis();
    TS_Point p = ts.getPoint();
    int y = map(p.y, 3800, 200, 0, 320); 
    int x = map(p.x, 3800, 200, 0, 240);
    
    if (isAPMode) return;

    if (!isScanReady && !waitingResult && !isReviewMode) {
       isScanReady = true;
       drawScanReadyScreen();
       return;
    }

    if (isScanReady) {
      if (x > BTN_X && x < (BTN_X + BTN_W) && y > BTN_Y && y < (BTN_Y + BTN_H)) {
        triggerScan(); 
      } else if (y < 50) { 
        isScanReady = false;
        drawDashboard(lastS3IP, lastCamIP);
      }
      return;
    }

    if (isReviewMode) {
      if (y > 260) {
        if (x < 120) { // BACK
          isReviewMode = false; isScanning = false;
          isScanReady = true; drawScanReadyScreen(); 
        } else { // OKAY
          isReviewMode = false;
          tft.fillScreen(ILI9341_BLACK);
          tft.setCursor(10, 100);
          tft.setTextColor(ILI9341_WHITE);
          tft.println("Grading...");
          confirmGrading(); 
        }
      }
      return;
    }

    if (waitingResult) {
      waitingResult = false;
      isScanning = false;
      drawDashboard(lastS3IP, lastCamIP);
    }
  }
}

/* ================= SERVER HANDLERS ================= */
void handleConfigure() {
  if (!server.hasArg("ssid") || !server.hasArg("pass")) { server.send(400); return; }
  preferences.begin("wifi_config", false);
  preferences.putString("ssid", server.arg("ssid"));
  preferences.putString("pass", server.arg("pass"));
  preferences.end();
  server.send(200, "text/plain", "Saved.");
  delay(1000); ESP.restart();
}

void handleUpdate() {
  if (!server.hasArg("plain")) { server.send(400); return; }
  String json = server.arg("plain");
  
  // SAFE MEMORY ALLOCATION (HEAP)
  DynamicJsonDocument doc(4096); 
  DeserializationError err = deserializeJson(doc, json);

  if (err) { server.send(400); return; }

  String screen = doc["screen"];

  if (screen == "dashboard") {
    lastS3IP = doc["rec_ip"].as<String>();
    lastCamIP = doc["cam_ip"].as<String>();
    if (doc.containsKey("server_ip")) serverIP = doc["server_ip"].as<String>();
    if (!isScanReady && !waitingResult && !isReviewMode) drawDashboard(lastS3IP, lastCamIP);
  } 
  else if (screen == "loading") {
    isScanReady = false; isReviewMode = false; waitingResult = false;
    String lines[2] = {doc["msg"], doc["sub"]};
    drawMessageScreen(lines, 2);
  } 
  else if (screen == "review") {
     isScanReady = false; waitingResult = false;
     isReviewMode = true;
     drawReviewScreen();
  }
  else if (screen == "result") {
    waitingResult = true; isReviewMode = false; isScanning = false;
    String total = doc["total"];
    int page = doc["page"] | 1;
    JsonArray questions = doc["questions"].as<JsonArray>();
    drawResultScreen(total, questions, page);
  }
  else if (screen == "error") {
     waitingResult = true; isReviewMode = false; isScanning = false;
     String lines[2] = {doc["msg"], doc["sub"]};
     drawMessageScreen(lines, 2);
  }
  server.send(200, "text/plain", "OK");
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  Serial.println("\n🚀 System Starting...");

  SPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  
  tft.begin(40000000); 
  tft.setRotation(0);
  
  ts.begin(touchSpi); 
  ts.setRotation(0); 

  drawBoot();

  preferences.begin("wifi_config", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  // 1. Try to Connect to Saved WiFi
  if (ssid != "") {
    Serial.print("Connecting to: "); Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    unsigned long s = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - s < 10000) {
      delay(500); Serial.print(".");
    }
  }

  // 2. Fallback Logic: If not connected, start AP
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    if (MDNS.begin("esp32-screen")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("✅ mDNS started: esp32-screen.local");
    }
    drawDashboard(WiFi.localIP().toString(), "Offline");
  } else {
    Serial.println("\n❌ WiFi Failed - Starting Hotspot");
    WiFi.mode(WIFI_AP); // Explicitly set AP mode
    WiFi.softAP("ESP32 Screen");
    drawHotspotMode();
    isAPMode = true;
  }

  server.on("/update", HTTP_POST, handleUpdate);
  
  // FIX: Allow Browser Config (HTTP_ANY) to bypass app restrictions
  server.on("/configure", HTTP_ANY, handleConfigure); 
  
  server.begin();
  Serial.println("🌐 Server Started");
}

void loop() {
  server.handleClient();
  checkTouch();
  
  // Auto-reconnect ONLY if we are supposed to be in Station Mode
  if (!isAPMode && WiFi.status() != WL_CONNECTED && millis() - lastWiFiCheck > 10000) {
    lastWiFiCheck = millis();
    WiFi.reconnect();
  }
}
