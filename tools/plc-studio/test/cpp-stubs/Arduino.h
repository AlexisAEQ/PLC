// En-tête Arduino minimal pour une vérification de syntaxe sur PC (pas d'exécution).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <climits>
#include <string>
#include <functional>
#include <mutex>
#include <memory>
#include <map>
#include <vector>
#include <list>
#include <algorithm>

using std::min;
using std::max;
typedef uint8_t byte;
typedef bool boolean;
#define PROGMEM
#define F(x) (x)
#define PSTR(x) (x)
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define FALLING 0x02
#define RISING 0x01
#define CHANGE 0x03
#define SERIAL_8N1 0x800001c
#define SERIAL_8N2 0x800003c
#define SERIAL_8E1 0x800001e
#define SERIAL_8E2 0x800003e
class __FlashStringHelper;

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const String&) = default;
  String(const std::string& x) : s(x) {}
  String(const __FlashStringHelper* c) : s(reinterpret_cast<const char*>(c)) {}
  explicit String(char c) : s(1, c) {}
  explicit String(int v, int base = 10) : s(std::to_string(v)) { (void)base; }
  explicit String(unsigned int v, int base = 10) : s(std::to_string(v)) { (void)base; }
  explicit String(long v, int base = 10) : s(std::to_string(v)) { (void)base; }
  explicit String(unsigned long v, int base = 10) : s(std::to_string(v)) { (void)base; }
  explicit String(float v, int d = 2) : s(std::to_string(v)) { (void)d; }
  explicit String(double v, int d = 2) : s(std::to_string(v)) { (void)d; }
  String& operator=(const String&) = default;
  String& operator=(const char* c) { s = c ? c : ""; return *this; }
  const char* c_str() const { return s.c_str(); }
  unsigned int length() const { return s.size(); }
  bool isEmpty() const { return s.empty(); }
  bool reserve(unsigned int n) { s.reserve(n); return true; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char o) { s += o; return *this; }
  String& operator+=(int o) { s += std::to_string(o); return *this; }
  String& operator+=(unsigned int o) { s += std::to_string(o); return *this; }
  String& operator+=(long o) { s += std::to_string(o); return *this; }
  String& operator+=(unsigned long o) { s += std::to_string(o); return *this; }
  bool concat(const String& o) { s += o.s; return true; }
  bool concat(const char* o) { s += o; return true; }
  bool concat(char o) { s += o; return true; }
  bool equals(const String& o) const { return s == o.s; }
  bool equalsIgnoreCase(const String& o) const { return s == o.s; }
  bool startsWith(const String& o) const { return s.rfind(o.s, 0) == 0; }
  bool endsWith(const String& o) const { return s.size() >= o.s.size() && s.compare(s.size() - o.s.size(), o.s.size(), o.s) == 0; }
  int indexOf(char c, unsigned int from = 0) const { auto p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& c, unsigned int from = 0) const { auto p = s.find(c.s, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { auto p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned int a) const { return String(s.substr(a)); }
  String substring(unsigned int a, unsigned int b) const { return String(s.substr(a, b - a)); }
  long toInt() const { return atol(s.c_str()); }
  float toFloat() const { return atof(s.c_str()); }
  void toLowerCase() {}
  void toUpperCase() {}
  void trim() {}
  void replace(const String&, const String&) {}
  void remove(unsigned int, unsigned int = 1) {}
  char charAt(unsigned int i) const { return s[i]; }
  char operator[](unsigned int i) const { return s[i]; }
  char& operator[](unsigned int i) { return s[i]; }
  void toCharArray(char* buf, unsigned int n) const { strncpy(buf, s.c_str(), n); }
  int compareTo(const String& o) const { return s.compare(o.s); }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator==(const char* o) const { return s == (o ? o : ""); }
  bool operator!=(const String& o) const { return s != o.s; }
  bool operator!=(const char* o) const { return s != (o ? o : ""); }
  bool operator<(const String& o) const { return s < o.s; }
  explicit operator bool() const { return true; }
};
inline String operator+(const String& a, const String& b) { return String(a.s + b.s); }
inline String operator+(const String& a, const char* b) { return String(a.s + b); }
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
inline String operator+(const String& a, char b) { return String(a.s + b); }
inline String operator+(const String& a, int b) { return String(a.s + std::to_string(b)); }
inline String operator+(const String& a, unsigned long b) { return String(a.s + std::to_string(b)); }
inline String operator+(const String& a, long b) { return String(a.s + std::to_string(b)); }
inline String operator+(const String& a, unsigned int b) { return String(a.s + std::to_string(b)); }

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) { return 1; }
  size_t printf(const char*, ...) __attribute__((format(printf, 2, 3))) { return 0; }
  template <typename T> size_t print(const T&) { return 0; }
  template <typename T> size_t print(const T&, int) { return 0; }
  template <typename T> size_t println(const T&) { return 0; }
  template <typename T> size_t println(const T&, int) { return 0; }
  size_t println() { return 0; }
  void flush() {}
};
class Stream : public Print {
 public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  String readStringUntil(char) { return String(); }
  size_t readBytes(char*, size_t) { return 0; }
  size_t readBytes(uint8_t*, size_t) { return 0; }
  void setTimeout(unsigned long) {}
};
class HardwareSerial : public Stream {
 public:
  void begin(unsigned long, uint32_t = SERIAL_8N1, int8_t = -1, int8_t = -1) {}
  void end() {}
  operator bool() const { return true; }
};
extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;

unsigned long millis();
unsigned long micros();
void delay(uint32_t);
void delayMicroseconds(uint32_t);
void yield();
void pinMode(uint8_t, uint8_t);
int digitalRead(uint8_t);
void digitalWrite(uint8_t, uint8_t);
int analogRead(uint8_t);
void attachInterrupt(uint8_t, void (*)(void), int);
void detachInterrupt(uint8_t);
uint8_t digitalPinToInterrupt(uint8_t);
long random(long);
long random(long, long);

class IPAddress {
 public:
  uint8_t b[4] = {0, 0, 0, 0};
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t c, uint8_t d, uint8_t e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  IPAddress(uint32_t) {}
  IPAddress(const char*) {}
  bool fromString(const char*) { return true; }
  bool fromString(const String&) { return true; }
  String toString() const { return String(); }
  operator uint32_t() const { return 0; }
  uint8_t operator[](int i) const { return b[i]; }
  uint8_t& operator[](int i) { return b[i]; }
  bool operator==(const IPAddress& o) const { return memcmp(b, o.b, 4) == 0; }
  bool operator!=(const IPAddress& o) const { return !(*this == o); }
};
extern const IPAddress INADDR_NONE;

class EspClass {
 public:
  uint32_t getFreeHeap() { return 0; }
  uint32_t getHeapSize() { return 0; }
  uint32_t getMinFreeHeap() { return 0; }
  uint32_t getMaxAllocHeap() { return 0; }
  uint32_t getPsramSize() { return 0; }
  uint32_t getFreePsram() { return 0; }
  uint32_t getMaxAllocPsram() { return 0; }
  uint32_t getMinFreePsram() { return 0; }
  uint32_t getFlashChipSize() { return 0; }
  uint32_t getSketchSize() { return 0; }
  uint32_t getFreeSketchSpace() { return 0; }
  uint32_t getCpuFreqMHz() { return 240; }
  uint64_t getEfuseMac() { return 0; }
  const char* getChipModel() { return ""; }
  const char* getSdkVersion() { return ""; }
  void restart() {}
};
extern EspClass ESP;

// FreeRTOS
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef void* TimerHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef struct { int x; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portMAX_DELAY 0xffffffffUL
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define portTICK_PERIOD_MS 1
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
#define taskENTER_CRITICAL(x)
#define taskEXIT_CRITICAL(x)
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
void vTaskDelay(TickType_t);
TaskHandle_t xTaskGetCurrentTaskHandle();
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
UBaseType_t uxTaskGetNumberOfTasks();
TickType_t xTaskGetTickCount();
const char* pcTaskGetName(TaskHandle_t);
BaseType_t xTaskCreate(void (*)(void*), const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*);
BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, BaseType_t);
void vTaskDelete(TaskHandle_t);
BaseType_t xPortGetCoreID();

// ESP-IDF
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
const char* esp_err_to_name(esp_err_t);
typedef enum { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO } esp_reset_reason_t;
esp_reset_reason_t esp_reset_reason();
uint32_t esp_get_free_heap_size();
uint32_t esp_get_minimum_free_heap_size();
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
void* heap_caps_malloc(size_t, uint32_t);
void* heap_caps_calloc(size_t, size_t, uint32_t);
void* heap_caps_realloc(void*, size_t, uint32_t);
void heap_caps_free(void*);
size_t heap_caps_get_free_size(uint32_t);
size_t heap_caps_get_largest_free_block(uint32_t);
size_t heap_caps_get_minimum_free_size(uint32_t);
size_t heap_caps_get_total_size(uint32_t);
bool heap_caps_check_integrity_all(bool);
void* ps_malloc(size_t);
bool psramFound();
uint32_t crc32_le(uint32_t, const uint8_t*, uint32_t);
void esp_backtrace_print(int);
esp_err_t esp_task_wdt_reset();
