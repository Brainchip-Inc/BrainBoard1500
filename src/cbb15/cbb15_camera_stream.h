// BrainBoard1500 - double-buffered camera capture on the Nicla Vision.
//
// Why: Camera::grabFrame() starts a DCMI snapshot and blocks until it is
// complete. The snapshot waits for the next frame start, so any processing
// between two grabFrame() calls that is longer than the sensor's blanking
// gap misses a whole frame. Measured on the GC2145 at 320x240 RGB565: one
// frame every 46.4 ms, readout about 33 ms, gap about 13 ms. A loop that
// spends 8 ms on preprocessing and 16 ms on inference therefore only gets
// every second frame (92.7 ms).
//
// CameraStream arms the next snapshot into a second buffer as soon as a
// frame is complete, so the sensor reads out the next frame while the sketch
// processes the current one. As long as the processing stays below one frame
// period, every frame is used.
//
// It drives the DCMI and its DMA stream (DMA2 stream 3) through their
// registers. Pins, clocks, the DCMI configuration and the DMA request routing
// all come from Camera::begin(), which must run first. Do not call
// Camera::grabFrame() while a CameraStream is running.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cbb15 {

class CameraStream {
 public:
  // Starts the first capture into bufferA. Both buffers hold frameBytes
  // (width * height * 2 for RGB565), are 32-byte aligned and must lie in
  // AXI SRAM or SRAM1..3 (a global array does), not in DTCM.
  bool begin(uint8_t* bufferA, uint8_t* bufferB, size_t frameBytes);
  void end();

  // Waits for the frame in progress, starts the capture of the next one into
  // the other buffer and returns the finished frame (nullptr on timeout).
  // The returned buffer stays valid until the next call of next().
  const uint8_t* next(uint32_t timeoutMs);

  // True when a finished frame is waiting, so next() would not block.
  bool ready() const;

  // A finished frame waits until next() takes it, so after a long pause it
  // is as old as the pause. skipFinished() drops such a frame and starts a
  // new capture; the next frame returned by next() then started no earlier
  // than this call minus one readout. Does not block.
  void skipFinished();

  // How long the last next() waited for the frame, in microseconds.
  uint32_t waitUs() const { return waitUs_; }

 private:
  void arm(uint8_t* buffer);

  uint8_t* buffers_[2] = {nullptr, nullptr};
  size_t frameBytes_ = 0;
  int active_ = 0;  // index of the buffer being filled
  bool running_ = false;
  uint32_t waitUs_ = 0;
};

}  // namespace cbb15
