// Минимальная заглушка Arduino API для прогона .ino на обычном ПК.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>

#define HIGH 1
#define LOW  0

#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2

extern uint32_t g_millis;

inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
void analogWrite(uint8_t pin, int value);

int digitalReadPin(uint8_t pin);
int analogReadPin(uint8_t pin);

struct MockSerial {
  std::string rx;
  size_t pos = 0;
  long baud = 0;
  void begin(long b) { baud = b; }
  void setTimeout(long) {}
  int available() { return (int)(rx.size() - pos); }
  int read() { return (pos < rx.size()) ? (uint8_t)rx[pos++] : -1; }
  void feed(const char *s) { rx += s; }
  void feed(std::string s) { rx += s; }
};

extern MockSerial Serial;
