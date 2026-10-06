#pragma once
#include "Arduino.h"
class TwoWire : public Stream {
 public:
  bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission(bool = true) { return 0; }
  uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
  size_t write(uint8_t) override { return 1; }
  void setClock(uint32_t) {}
};
extern TwoWire Wire;
