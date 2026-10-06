#pragma once
#include "Arduino.h"
class AsyncWebSocketClient {
 public:
  uint32_t id() { return 0; }
  bool canSend() { return true; }
  void text(const char*) {}
  void text(const String&) {}
  void close(uint16_t = 0, const char* = nullptr) {}
  IPAddress remoteIP() { return IPAddress(); }
  int status() { return 1; }
  size_t queueLen() { return 0; }
  bool queueIsFull() { return false; }
};
typedef enum { WS_EVT_CONNECT, WS_EVT_DISCONNECT, WS_EVT_PING, WS_EVT_PONG, WS_EVT_ERROR, WS_EVT_DATA } AwsEventType;
typedef struct { uint8_t message_opcode; uint32_t num; uint8_t final; uint8_t masked; uint8_t opcode; uint64_t len; uint8_t mask[4]; uint64_t index; } AwsFrameInfo;
#define WS_TEXT 0x01
class AsyncWebSocket {
 public:
  AsyncWebSocket(const char*) {}
  size_t count() const { return 0; }
  void cleanupClients(uint16_t = 1) {}
  void textAll(const char*) {}
  void textAll(const String&) {}
  AsyncWebSocketClient* client(uint32_t) { return nullptr; }
  void closeAll(uint16_t = 0, const char* = nullptr) {}
};
class AsyncWebServerRequest {};
class AsyncWebServer {
 public:
  AsyncWebServer(uint16_t) {}
  void begin() {}
};
