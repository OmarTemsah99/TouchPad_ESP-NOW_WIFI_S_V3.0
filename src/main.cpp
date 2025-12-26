// ==================== main.cpp ====================
#include <Arduino.h>
#include <ESPAsyncWebServer.h> // Changed from WebServer.h
#include <AsyncTCP.h>          // Required for ESPAsyncWebServer
#include <HTTPClient.h>
#include <U8g2lib.h>

// Project headers
#include "config.h"
#include "sensor_manager.h"
#include "web_handlers.h"
#include "wifi_manager.h"
#include "filesystem_utils.h"
#include "client_config.h"
#include "ClientIdentity.h"

// ========================= GLOBAL OBJECTS =========================
SensorManager sensorManager;
AsyncWebServer server(WEB_SERVER_PORT); // Changed from WebServer
WiFiManager wifiManager;
ClientConfig clientConfig;
ClientIdentity clientIdentity(&clientConfig);
WebHandlers webHandlers(&server, &sensorManager, &clientIdentity);

// object for the display control
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/22, /* data=*/21);

// ========================= GLOBAL VARIABLES =========================
#define BTN_INC_PIN 4
#define BTN_DEC_PIN 15

int buttonStateInc = LOW;
int lastButtonStateInc = LOW;
unsigned long lastDebounceTimeInc = 0;

int buttonStateDec = LOW;
int lastButtonStateDec = LOW;
unsigned long lastDebounceTimeDec = 0;

const unsigned long debounceDelay = 50;

/* update rate for button */
unsigned long previousMillis_Buttons = 0;
const long interval_Buttons = 200;

/* update rate for display */
unsigned long previousMillis_Display = 0;
const long interval_Display = 500;

// ========================= CLIENT CONFIGURATION =========================
const char *SERVER_URL = "http://192.168.0.200/sensor";
const unsigned long SEND_INTERVAL = 500; // ms

// ========================= TIMING VARIABLES =========================
unsigned long lastSensorSend = 0;
unsigned long lastLocalDisplay = 0;

// ========================= HELPER FUNCTIONS =========================
void handleButton(int &lastState, int &buttonState, unsigned long &lastTime, int pin, int direction)
{
  int reading = digitalRead(pin);

  if (reading != lastState)
  {
    lastTime = millis(); // Reset the debounce timer
  }

  if ((millis() - lastTime) > debounceDelay)
  {
    if (reading != buttonState)
    {
      buttonState = reading;

      // Only trigger on button press (LOW for pull-up or HIGH for pull-down)
      if (buttonState == HIGH)
      {
        int id = clientIdentity.get();
        id += direction;
        id = constrain(id, 0, 15);
        clientIdentity.set(id);
        Serial.printf("[BUTTON] Client ID %s to %d\n", (direction > 0 ? "increased" : "decreased"), id);
      }
    }
  }

  lastState = reading;
}

void sendSensorDataToServer()
{
  int touchValue = sensorManager.getLocalTouchValue();
  float batteryPercent = sensorManager.getLocalBatteryPercent();
  int clientId = clientIdentity.get();

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "clientId=" + String(clientId) +
                    "&touch=" + String(touchValue) +
                    "&batteryPercent=" + String(batteryPercent, 1);
  int responseCode = http.POST(postData);

  if (responseCode == 200)
  {
    Serial.printf("[SEND] ID: %d, Touch: %d, Battery: %.2fV (%.1f%%)\n", clientId, touchValue, batteryPercent);
  }
  else
  {
    Serial.printf("[SEND ERROR] %d: %s\n", responseCode, http.errorToString(responseCode).c_str());
  }

  http.end();
}

void displayLocalSensorData()
{
  int touchValue = sensorManager.getLocalTouchValue();
  float batteryPercent = sensorManager.getLocalBatteryPercent();
  Serial.printf("Local Touch: %d, Battery: %.2fV (%.1f%%)\n", touchValue, batteryPercent);
}

bool initializeSystem()
{
  Serial.begin(115200);
  Serial.println("\n=== ESP32-S3 Client Starting ===");

  clientIdentity.begin();
  sensorManager.begin(&clientIdentity);

  if (!FilesystemUtils::initSPIFFS())
  {
    Serial.println("ERROR: Failed to initialize SPIFFS");
    return false;
  }

  FilesystemUtils::listFiles();
  FilesystemUtils::checkIndexFile();

  if (!wifiManager.init())
  {
    Serial.println("ERROR: WiFi initialization failed");
    return false;
  }

  webHandlers.setupRoutes();
  server.begin();

  Serial.println("=== System initialized successfully ===");
  Serial.printf("Web server running on: http://%s\n", WiFi.localIP().toString().c_str());
  return true;
}

// ========================= ARDUINO FUNCTIONS =========================

void setup()
{
  if (!initializeSystem())
  {
    Serial.println("FATAL: System initialization failed!");
    while (true)
      delay(1000);
  }

  pinMode(BTN_INC_PIN, INPUT_PULLUP);
  pinMode(BTN_DEC_PIN, INPUT_PULLUP);

  u8g2.begin();
}

void loop()
{
  unsigned long currentTime = millis();
  unsigned long currentMillis_Display = millis();
  unsigned long currentMillis_Button = millis();

  if (currentMillis_Button - previousMillis_Buttons >= interval_Buttons)
  {

    handleButton(lastButtonStateInc, buttonStateInc, lastDebounceTimeInc, BTN_INC_PIN, +1);
    handleButton(lastButtonStateDec, buttonStateDec, lastDebounceTimeDec, BTN_DEC_PIN, -1);
    previousMillis_Buttons = currentMillis_Button;
  }

  wifiManager.handleConnection();

  if (wifiManager.isConnected())
  {
    // No need to call server.handleClient() with async server
    if (currentTime - lastSensorSend >= SEND_INTERVAL)
    {
      sendSensorDataToServer();
      lastSensorSend = currentTime;
    }
  }

  /* Interface Update */
  if (currentMillis_Display - previousMillis_Display >= interval_Display)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 10, "SomniaSoltions");

    // Get sensor data
    int displayId = clientIdentity.get();
    int displayTouch = sensorManager.getLocalTouchValue();
    float displayBatteryPercent = sensorManager.getLocalBatteryPercent();

    // ID
    u8g2.drawStr(5, 25, "ID: ");
    u8g2.setCursor(25, 25);
    u8g2.print(displayId);

    // Touch State
    u8g2.drawStr(5, 40, "State: ");
    u8g2.setCursor(36, 40);
    u8g2.print(displayTouch);

    // Battery Percentage
    u8g2.drawStr(5, 55, "Battery: ");
    u8g2.setCursor(50, 55);
    u8g2.print(displayBatteryPercent, 1);
    u8g2.print("%");

    u8g2.sendBuffer();

    previousMillis_Display = currentMillis_Display;
  }

  yield();
}