#pragma once
// Stub de modbus-esp8266 ModbusTCP (client) pour la vérification de syntaxe.
#include "Arduino.h"
#include "ModbusRTU.h"
#ifndef MODBUSTCP_PORT
#define MODBUSTCP_PORT 502
#endif
class ModbusTCP : public Modbus {
 public:
  void client() {}
  void task() {}
  bool isConnected(IPAddress) { return true; }
  bool connect(IPAddress, uint16_t = 0) { return true; }
  bool disconnect(IPAddress) { return true; }
  bool isTransaction(uint16_t) { return false; }
  uint16_t readCoil(IPAddress, uint16_t, bool*, uint16_t = 1, cbTransaction = nullptr, uint8_t = 255) { return 1; }
  uint16_t readIsts(IPAddress, uint16_t, bool*, uint16_t = 1, cbTransaction = nullptr, uint8_t = 255) { return 1; }
  uint16_t readHreg(IPAddress, uint16_t, uint16_t*, uint16_t = 1, cbTransaction = nullptr, uint8_t = 255) { return 1; }
  uint16_t readIreg(IPAddress, uint16_t, uint16_t*, uint16_t = 1, cbTransaction = nullptr, uint8_t = 255) { return 1; }
  uint16_t writeCoil(IPAddress, uint16_t, bool, cbTransaction = nullptr, uint8_t = 255) { return 1; }
  uint16_t writeCoil(IPAddress, uint16_t, bool*, uint16_t = 1, cbTransaction = nullptr, uint8_t = 255) { return 1; }
  uint16_t writeHreg(IPAddress, uint16_t, uint16_t, cbTransaction = nullptr, uint8_t = 255) { return 1; }
  uint16_t writeHreg(IPAddress, uint16_t, uint16_t*, uint16_t = 1, cbTransaction = nullptr, uint8_t = 255) { return 1; }
};
