// WiFiManager.h

#ifndef WiFiManager_h
#define WiFiManager_h

#include <WiFi.h>

class WiFiManager {
private:
  const char* ssid;
  const char* password;

  static unsigned long lastTimeWiFiReconnects;
  static const long wifiReconnectInterval = 10000;

  static void onStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);
  static void onGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
  static void onStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);

public:
  WiFiManager(const char* ssid, const char* password);
  void connect();
};

#endif
