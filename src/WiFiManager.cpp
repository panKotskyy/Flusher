// WiFiManager.cpp
#include "Arduino.h"
#include "WiFiManager.h"

unsigned long WiFiManager::lastTimeWiFiReconnects = 0;

WiFiManager::WiFiManager(const char* ssid, const char* password):ssid(ssid), password(password) {}

void WiFiManager::connect() {
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(onStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(onStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  // unsigned long startAttemptTime = millis();
  // while (WiFi.status() != WL_CONNECTED) {
  //   Serial.print('.');
  //   delay(1000);

  //   // Check if 30 seconds have elapsed
  //   if (millis() - startAttemptTime >= 30000) {
  //     Serial.println("\nFailed to connect within 30 seconds.");
  //     break;
  //     // ESP.restart(); // Restart the ESP
  //   }
  // }
}

void WiFiManager::onStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Connected to AP successfully!");
}

void WiFiManager::onGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiManager::onStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.wifi_sta_disconnected.reason);
  Serial.println("Trying to Reconnect");

  if ((WiFi.getMode() == WIFI_STA) && (WiFi.status() != WL_CONNECTED)) {
    if (millis() - lastTimeWiFiReconnects >= wifiReconnectInterval) {
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastTimeWiFiReconnects = millis();
    }
  }
}
