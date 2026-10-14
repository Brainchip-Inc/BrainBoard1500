// BrainBoard1500 - target functions required by the Akida Engine (infra/system.h).
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#include "infra/system.h"

extern "C" {

void msleep(uint32_t duration) { delay(duration); }

int64_t time_ms() { return static_cast<int64_t>(millis()); }

void kick_watchdog() { yield(); }

// The engine calls panic() on unrecoverable errors (it is built without
// exceptions). The message goes to the serial port, then the red LED blinks
// forever so that the state is visible without a monitor.
void panic(const char* format, ...) {
  char message[192];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  Serial.print("[AKIDA PANIC] ");
  Serial.println(message);
  Serial.flush();
#ifdef LEDR
  pinMode(LEDR, OUTPUT);
#endif
  for (;;) {
#ifdef LEDR
    digitalWrite(LEDR, LOW);
    delay(150);
    digitalWrite(LEDR, HIGH);
    delay(850);
#else
    delay(1000);
#endif
    Serial.print("[AKIDA PANIC] ");
    Serial.println(message);
  }
}

}  // extern "C"
