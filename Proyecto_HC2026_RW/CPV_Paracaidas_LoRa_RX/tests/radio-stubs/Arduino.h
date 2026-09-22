#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <deque>
#include <vector>
#include <functional>
#include <algorithm>
#include <cctype>
#include <tuple>
inline uint32_t testClock = 0, stopAt = 0;
inline std::function<void()> onTick;
struct StopTask {};
inline uint32_t millis() { return testClock; }
inline void vTaskDelay(unsigned n) { testClock += n; if(onTick)onTick(); if(testClock >= stopAt)throw StopTask{}; }
inline void delay(unsigned n) { vTaskDelay(n); }
using TickType_t = uint32_t;
using BaseType_t = int;
constexpr int pdTRUE = 1, pdPASS = 1, LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1;
#define pdMS_TO_TICKS(ms) (ms)
inline TickType_t xTaskGetTickCount() { return testClock; }
inline void vTaskDelayUntil(TickType_t *wake, unsigned period) { *wake += period; vTaskDelay(*wake > testClock ? *wake-testClock : 0); }
inline int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,int,void *,int) { return pdPASS; }
struct TestQueue { size_t capacity,size; std::deque<std::vector<uint8_t>> values; };
using QueueHandle_t = TestQueue *;
inline QueueHandle_t xQueueCreate(size_t n,size_t size) { return new TestQueue{n,size,{}}; }
inline int xQueueSend(QueueHandle_t q,const void *p,int) { if(q->values.size()==q->capacity)return 0; const auto b=static_cast<const uint8_t *>(p);q->values.emplace_back(b,b+q->size);return 1; }
inline int xQueuePeek(QueueHandle_t q,void *p,int) { if(q->values.empty())return 0;memcpy(p,q->values.front().data(),q->size);return 1; }
inline int xQueueReceive(QueueHandle_t q,void *p,int n) { if(!xQueuePeek(q,p,n))return 0;q->values.pop_front();return 1; }
inline int xQueueOverwrite(QueueHandle_t q,const void *p) { q->values.clear();return xQueueSend(q,p,0); }
inline int pins[40]{};
inline std::vector<std::tuple<uint32_t,int,int>> pinChanges;
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int value) { if(pins[pin]!=value)pinChanges.emplace_back(testClock,pin,value);pins[pin]=value; }
int digitalRead(int);
class String {
  std::string value;
public:
  String(const char *s=""):value(s) {}
  void trim() { const auto a=value.find_first_not_of(" \r\n\t"); if(a==std::string::npos)value.clear();else value=value.substr(a,value.find_last_not_of(" \r\n\t")-a+1); }
  void toUpperCase() { for(char &c:value)c=char(std::toupper(static_cast<unsigned char>(c))); }
  const char *c_str()const { return value.c_str(); }
  unsigned length()const { return unsigned(value.size()); }
};
struct TestSerial {
  std::deque<char> input;
  std::string output;
  void begin(int) {}
  int available() { return int(input.size()); }
  int read() { const char c=input.front();input.pop_front();return c; }
  void println(const char *s) { output+=s;output+='\n'; }
};
inline TestSerial Serial;
