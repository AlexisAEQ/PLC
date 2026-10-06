#pragma once
#include "Arduino.h"
class Modbus {
 public:
  enum ResultCode { EX_SUCCESS = 0x00, EX_ILLEGAL_FUNCTION = 0x01, EX_ILLEGAL_ADDRESS = 0x02, EX_ILLEGAL_VALUE = 0x03, EX_SLAVE_FAILURE = 0x04, EX_ACKNOWLEDGE = 0x05, EX_SLAVE_DEVICE_BUSY = 0x06, EX_MEMORY_PARITY_ERROR = 0x08, EX_PATH_UNAVAILABLE = 0x0A, EX_DEVICE_FAILED_TO_RESPOND = 0x0B, EX_GENERAL_FAILURE = 0xE1, EX_DATA_MISMACH = 0xE2, EX_UNEXPECTED_RESPONSE = 0xE3, EX_TIMEOUT = 0xE4, EX_CONNECTION_LOST = 0xE5, EX_CANCEL = 0xE6, EX_PASSTHROUGH = 0xE7, EX_FORCE_PROCESS = 0xE8 };
};
typedef bool (*cbTransaction)(Modbus::ResultCode, uint16_t, void*);
class ModbusRTU : public Modbus {
 public:
  bool begin(HardwareSerial*, int16_t = -1, bool = true) { return true; }
  void client() {}
  void task() {}
  bool slave() { return false; }
  void setTimeout(uint32_t) {}
};
