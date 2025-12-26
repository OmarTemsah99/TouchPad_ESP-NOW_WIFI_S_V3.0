// ==================== Sender main.cpp (ESP-NOW + Web Server) ====================
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <U8g2lib.h>

#include "config.h"
#include "client_config.h"
#include "ClientIdentity.h"
#include "filesystem_utils.h"
#include "wifi_manager.h"
#include "web_handlers.h"
#include "sensor_manager.h"

// ========================= RECEIVER MAC ADDRESS =========================
// IMPORTANT: Replace with your receiver's MAC address from Serial Monitor
uint8_t receiverMacAddress[] = {0x24, 0x6F, 0x28, 0x12, 0x34, 0x56};

// ========================= ESP-NOW DATA STRUCTURE =========================
// Must match the receiver structure exactly
typedef struct struct_message
{
  char clientId[32];
  int touchValue;
  float batteryPercent;
} struct_message;

struct_message sensorData;
esp_now_peer_info_t peerInfo;

// ========================= GLOBAL OBJECTS =========================
SensorManager sensorManager;
AsyncWebServer server(WEB_SERVER_PORT);
WiFiManager wifiManager;
ClientConfig clientConfig;
ClientIdentity clientIdentity(&clientConfig);
WebHandlers webHandlers(&server, &sensorManager, &clientIdentity);

// Display object
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/22, /* data=*/21);

// ========================= PIN DEFINITIONS =========================
#define BTN_INC_PIN 4
#define BTN_DEC_PIN 15

// ========================= BUTTON VARIABLES =========================
int buttonStateInc = LOW;
int lastButtonStateInc = LOW;
unsigned long lastDebounceTimeInc = 0;

int buttonStateDec = LOW;
int lastButtonStateDec = LOW;
unsigned long lastDebounceTimeDec = 0;

const unsigned long debounceDelay = 50;

// ========================= TIMING VARIABLES =========================
unsigned long previousMillis_Buttons = 0;
const long interval_Buttons = 200;

unsigned long previousMillis_Display = 0;
const long interval_Display = 500;

unsigned long previousMillis_Send = 0;
const long interval_Send = 500; // Send every 500ms via ESP-NOW

// ========================= BUTTON HANDLER =========================
void handleButton(int &lastState, int &buttonState, unsigned long &lastTime, int pin, int direction)
{
  int reading = digitalRead(pin);

  if (reading != lastState)
  {
    lastTime = millis();
  }

  if ((millis() - lastTime) > debounceDelay)
  {
    if (reading != buttonState)
    {
      buttonState = reading;

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

// ========================= ESP-NOW CALLBACK =========================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// ========================= SEND DATA VIA ESP-NOW =========================
void sendSensorDataViaESPNOW()
{
  // Get sensor readings
  int touchValue = sensorManager.getLocalTouchValue();
  float batteryPercent = sensorManager.getLocalBatteryPercent();
  int clientId = clientIdentity.get();

  // Prepare data structure
  snprintf(sensorData.clientId, sizeof(sensorData.clientId), "%d", clientId);
  sensorData.touchValue = touchValue;
  sensorData.batteryPercent = batteryPercent;

  // Send via ESP-NOW
  esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *)&sensorData, sizeof(sensorData));

  if (result == ESP_OK)
  {
    Serial.printf("[ESP-NOW] Sent - ID: %d, Touch: %d, Battery: %.1f%%\n",
                  clientId, touchValue, batteryPercent);
  }
  else
  {
    Serial.println("[ESP-NOW] Error sending data");
  }
}

// ========================= INITIALIZE ESP-NOW =========================
bool initESPNOW()
{
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }

  Serial.println("ESP-NOW initialized successfully");

  // Register send callback
  esp_now_register_send_cb(OnDataSent);

  // Register peer (receiver)
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
    return false;
  }

  Serial.println("Peer (receiver) added successfully");
  Serial.print("Sending ESP-NOW data to: ");
  for (int i = 0; i < 6; i++)
  {
    Serial.printf("%02X", receiverMacAddress[i]);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println("\n");

  return true;
}

// ========================= INITIALIZE SYSTEM =========================
bool initializeSystem()
{
  Serial.begin(115200);
  Serial.println("\n=== ESP32-S3 Sender (ESP-NOW + Web Server) Starting ===");

  // Initialize client identity
  clientIdentity.begin();
  sensorManager.begin(&clientIdentity);
  Serial.printf("Client ID: %d\n", clientIdentity.get());

  // Initialize filesystem
  if (!FilesystemUtils::initSPIFFS())
  {
    Serial.println("ERROR: Failed to initialize SPIFFS");
    return false;
  }

  FilesystemUtils::listFiles();
  FilesystemUtils::checkIndexFile();

  // Initialize WiFi (connects to network)
  if (!wifiManager.init())
  {
    Serial.println("ERROR: WiFi initialization failed");
    return false;
  }

  // Initialize ESP-NOW after WiFi
  if (!initESPNOW())
  {
    Serial.println("ERROR: ESP-NOW initialization failed");
    return false;
  }

  // Setup web server
  webHandlers.setupRoutes();
  server.begin();

  Serial.println("=== System initialized successfully ===");
  Serial.printf("Web server running on: http://%s\n", WiFi.localIP().toString().c_str());
  Serial.println("ESP-NOW: Sending sensor data to receiver");
  Serial.println("Web Interface: View local sensor data and configure device");

  return true;
}

// ========================= SETUP =========================
void setup()
{
  if (!initializeSystem())
  {
    Serial.println("FATAL: System initialization failed!");
    while (true)
      delay(1000);
  }

  // Initialize pins
  pinMode(BTN_INC_PIN, INPUT_PULLUP);
  pinMode(BTN_DEC_PIN, INPUT_PULLUP);

  // Initialize display
  u8g2.begin();
}

// ========================= LOOP =========================
void loop()
{
  unsigned long currentMillis = millis();

  // Handle WiFi connection and OTA
  wifiManager.handleConnection();

  // Handle button inputs
  if (currentMillis - previousMillis_Buttons >= interval_Buttons)
  {
    handleButton(lastButtonStateInc, buttonStateInc, lastDebounceTimeInc, BTN_INC_PIN, +1);
    handleButton(lastButtonStateDec, buttonStateDec, lastDebounceTimeDec, BTN_DEC_PIN, -1);
    previousMillis_Buttons = currentMillis;
  }

  // Send sensor data via ESP-NOW (not HTTP anymore!)
  if (wifiManager.isConnected())
  {
    if (currentMillis - previousMillis_Send >= interval_Send)
    {
      sendSensorDataViaESPNOW();
      previousMillis_Send = currentMillis;
    }
  }

  // Update display
  if (currentMillis - previousMillis_Display >= interval_Display)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 10, "SomniaSolutions");

    // Get sensor data for display
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

    previousMillis_Display = currentMillis;
  }

  yield();
}