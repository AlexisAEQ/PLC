#pragma once
#include "Arduino.h"
class TwoWire;
class PCF8574 {
 public:
  PCF8574(uint8_t, TwoWire* = nullptr) {}
  bool begin(uint8_t = 0xFF) { return true; }
  bool isConnected() { return true; }
  uint8_t read8() { return 0; }
  uint8_t read(uint8_t) { return 0; }
  void write8(uint8_t) {}
  void write(uint8_t, uint8_t) {}
  int lastError() { return 0; }
};
