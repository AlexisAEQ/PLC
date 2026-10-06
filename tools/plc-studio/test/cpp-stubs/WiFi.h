#pragma once
#include "Arduino.h"
typedef int WiFiEvent_t;
typedef struct { int x; } WiFiEventInfo_t;
typedef enum { WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED } wl_status_t;
typedef enum { WIFI_MODE_NULL = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA } wifi_mode_t;
class WiFiClass {
 public:
  wl_status_t status() { return WL_CONNECTED; }
  IPAddress localIP() { return IPAddress(); }
  IPAddress softAPIP() { return IPAddress(); }
  String SSID() { return String(); }
  int32_t RSSI() { return 0; }
  bool mode(wifi_mode_t) { return true; }
  wifi_mode_t getMode() { return WIFI_STA; }
  String macAddress() { return String(); }
};
extern WiFiClass WiFi;
