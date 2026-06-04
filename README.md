# AI Grader Python

This project utilizes the power of the ESP32 S3, ESP32-CAM, and Google Gemini AI to create an automated grading system. It converts the microcontroller modules into Web Servers, allowing them to communicate over a network.

The system captures an image of a quiz or document using the ESP32-CAM, processes it using a Python script powered by Gemini, and displays the resulting grade on a 2.8-inch Touchscreen connected to the ESP32 S3.

## Features
* **AI-Powered Grading:** Uses Google Gemini to analyze and grade images.
* **Visual Feedback:** Displays scores directly on an ILI9341 Touchscreen.
* **Easy WiFi Configuration:** Uses a custom Android app to provision WiFi credentials via AP mode (Hotspot).
* **Dual Module Sync:** Synchronizes the ESP32 S3 and ESP32-CAM on the same network.

## Hardware Requirements
To build this project, you will need the following components:
* **Microcontroller:** ESP32 S3 N16R8
* **Camera:** ESP32-CAM module (OV2640)
* **Display:** 2.8-inch Touchscreen (must use ILI9341 driver)
* **Tools:** Programmer board for the CAM module (for USB connection to PC)

## Software & Library Requirements

### Arduino IDE Libraries
Ensure the following libraries are installed in your Arduino IDE before compiling the code:
* `Wifi.h`
* `WebServer.h`
* `ESPmDNS.h`
* `HTTPClient.h`
* `Adafruit_ILI9341.h`
* `XPT2046_Touchscreen.h`
* `Adafruit_GFX.h`
* `ArduinoJson.h`

### Python Environment
The grading logic runs on a Python script (`grader.py`).
1.  Install Python on your system.
2.  It is highly recommended to use a Virtual Environment (especially on Linux).
3.  Refer to the included `V Environment.txt` file for specific commands on how to create and activate your virtual environment.

## Project Setup

### 1. Pin Configuration
The pin definitions are already set in the source code.
> **Note:** If your wiring differs from the default setup, please update the pin definitions in the code to match your specific hardware configuration.

### 2. Android Companion App
To connect your ESP32 modules to your local WiFi network, download the Wifi Connect Android app:

[**Download WiFi Connect App (APK)**](https://github.com/devahmadpk/Wifi-Connect/releases/download/v1.0/app-release.apk)

### 3. Connection Instructions
1.  **Power On:** Power up both the ESP32 S3 and the ESP32-CAM.
2.  **Connect via App:** Open the Android app. You must connect your phone to the Hotspot created by each device separately to send them your home/office WiFi credentials.
3.  **Network Sync:** Ensure both the ESP32 modules and the computer running the Python script are connected to the **same 2.4 GHz WiFi network**.
4.  **Verify:** You can use the app to check if the devices have successfully connected to the network.

## Usage
1.  Run the `grader.py` script on your computer (ensure your virtual environment is active).
2.  Use the system to capture an image via the ESP32-CAM.
3.  The image is sent to the Python script, processed by Gemini, and the grade is sent back.
4.  View the final score on the ESP32 S3 Touchscreen.

> **Warning regarding Displays:** This code is specifically written for screens using the **ILI9341 driver**. If you are using a different display driver, you must install the compatible library and modify the display-related code, or the project will not function correctly.
