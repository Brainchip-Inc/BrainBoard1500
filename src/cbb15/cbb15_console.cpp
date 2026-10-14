#include "cbb15_console.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace cbb15 {

namespace {
constexpr int kWidth = 128;
constexpr int kHeight = 64;
// Stepwise update: 8 pages of 8 pixel rows, each sent in two halves.
constexpr uint8_t kStepBytes = 64;
constexpr uint8_t kSteps = (kWidth / kStepBytes) * (kHeight / 8);
constexpr size_t kI2cChunk = 31;  // + control byte = 32 B, the Wire buffer
}  // namespace

bool Console::begin(TwoWire& wire, uint8_t address, unsigned long baud,
                    uint32_t serialWaitMs) {
  Serial.begin(baud);
  wire_ = &wire;
  address_ = address;
  const uint32_t start = millis();
  while (!Serial && millis() - start < serialWaitMs) {
    delay(10);
  }
  if (display_ == nullptr) {
    // One driver object for the lifetime of the sketch (Adafruit_SSD1306 has
    // no virtual destructor, so it is never deleted through a base pointer).
    static Adafruit_SSD1306 display(kWidth, kHeight, &wire, -1);
    // The library switches the bus to 400 kHz for its own transfers and back
    // to 100 kHz afterwards, which keeps the BB15 reference I2C clock.
    if (display.begin(SSD1306_SWITCHCAPVCC, address)) {
      display_ = &display;
      display_->setTextSize(1);
      display_->setTextWrap(false);
      display_->cp437(true);
    }
  }
  if (display_ != nullptr) {
    // Rotation 2 = 180 degrees (Adafruit_GFX::setRotation).
    display_->setRotation(rotate180_ ? 2 : 0);
  }
  clearRows();
  render();
  return display_ != nullptr;
}

void Console::setTitle(const char* title) {
  snprintf(rows_[0], sizeof(rows_[0]), "%s", title);
  render();
}

void Console::logf(const char* format, ...) {
  char line[128];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  emitLine(line);
}

void Console::emitLine(const char* line) {
  Serial.println(line);
  // The OLED shows kColumns characters per row: longer lines are wrapped,
  // preferably at a space. Every piece scrolls rows 1..7 up by one.
  size_t length = strlen(line);
  do {
    size_t take = length;
    if (take > kColumns) {
      take = kColumns;
      for (size_t i = kColumns; i > kColumns / 2; --i) {
        if (line[i] == ' ') {
          take = i;
          break;
        }
      }
    }
    for (uint8_t r = 1; r + 1 < kRows; ++r) {
      memcpy(rows_[r], rows_[r + 1], sizeof(rows_[r]));
    }
    memcpy(rows_[kRows - 1], line, take);
    rows_[kRows - 1][take] = '\0';
    line += take;
    length -= take;
    while (length > 0 && *line == ' ') {
      ++line;
      --length;
    }
  } while (length > 0);
  render();
}

size_t Console::write(uint8_t c) {
  if (c == '\r') {
    return 1;
  }
  if (c == '\n' || static_cast<size_t>(pendingLength_) + 1 >= sizeof(pending_)) {
    pending_[pendingLength_] = '\0';
    emitLine(pending_);
    pendingLength_ = 0;
    if (c == '\n') {
      return 1;
    }
  }
  pending_[pendingLength_++] = static_cast<char>(c);
  return 1;
}

void Console::setRow(uint8_t row, const char* format, ...) {
  if (row == 0 || row >= kRows) {
    return;
  }
  va_list args;
  va_start(args, format);
  vsnprintf(rows_[row], sizeof(rows_[row]), format, args);
  va_end(args);
}

void Console::clearRows() {
  for (uint8_t r = 1; r < kRows; ++r) {
    rows_[r][0] = '\0';
  }
}

void Console::publish(bool force, uint32_t minIntervalMs) {
  if (serialEcho_) {
    bool first = true;
    for (uint8_t r = 1; r < kRows; ++r) {
      if (rows_[r][0] == '\0') {
        continue;
      }
      if (!first) {
        Serial.print(" | ");
      }
      Serial.print(rows_[r]);
      first = false;
    }
    if (!first) {
      Serial.println();
    }
  }
  if (force || !paged_) {
    if (force || millis() - lastRenderMs_ >= minIntervalMs) {
      render();
    }
    return;
  }
  if (display_ == nullptr) {
    return;
  }
  if (nextStep_ < kSteps) {
    sendStep(nextStep_++);
  } else if (millis() - lastRenderMs_ >= minIntervalMs) {
    lastRenderMs_ = millis();
    draw();
    nextStep_ = 0;
    sendStep(nextStep_++);
  }
}

void Console::render() {
  lastRenderMs_ = millis();
  nextStep_ = kSteps;
  if (display_ == nullptr) {
    return;
  }
  draw();
  display_->display();
}

void Console::sendStep(uint8_t step) {
  // One half page: 8 pixel rows x 64 columns. The commands and the data go
  // out at the current bus clock - no Wire.setClock(), no
  // Adafruit_SSD1306::ssd1306_command(), which would switch the clock twice
  // per byte.
  const uint8_t page = step / 2;
  const uint8_t column = (step % 2) * kStepBytes;
  wire_->beginTransmission(address_);
  wire_->write(static_cast<uint8_t>(0x00));  // Co = 0, D/C = 0: commands
  wire_->write(static_cast<uint8_t>(SSD1306_PAGEADDR));
  wire_->write(page);
  wire_->write(page);
  wire_->write(static_cast<uint8_t>(SSD1306_COLUMNADDR));
  wire_->write(column);
  wire_->write(static_cast<uint8_t>(column + kStepBytes - 1));
  wire_->endTransmission();
  const uint8_t* data = display_->getBuffer() + page * kWidth + column;
  for (size_t sent = 0; sent < kStepBytes;) {
    const size_t n = min(kI2cChunk, static_cast<size_t>(kStepBytes) - sent);
    wire_->beginTransmission(address_);
    wire_->write(static_cast<uint8_t>(0x40));  // Co = 0, D/C = 1: data
    wire_->write(data + sent, n);
    wire_->endTransmission();
    sent += n;
  }
}

void Console::draw() {
  display_->clearDisplay();
  // Title bar (inverted).
  display_->fillRect(0, 0, kWidth, 8, SSD1306_WHITE);
  display_->setTextColor(SSD1306_BLACK);
  display_->setCursor(1, 0);
  display_->print(rows_[0]);
  display_->setTextColor(SSD1306_WHITE);
  for (uint8_t r = 1; r < kRows; ++r) {
    display_->setCursor(0, r * 8);
    display_->print(rows_[r]);
  }
}

}  // namespace cbb15
