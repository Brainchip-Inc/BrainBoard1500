#include "cbb15_camera_stream.h"

#include <Arduino.h>

#if defined(STM32H747xx) && defined(CORE_CM7)
#include "stm32h7xx_hal.h"
#define CBB15_CAMERA_STREAM_SUPPORTED 1
#else
#define CBB15_CAMERA_STREAM_SUPPORTED 0
#endif

namespace cbb15 {

#if CBB15_CAMERA_STREAM_SUPPORTED

namespace {

// The stream the Arduino camera library uses for the DCMI.
DMA_Stream_TypeDef* const kStream = DMA2_Stream3;
// Stream 3 flags in DMA2->LISR / LIFCR: FEIF3, DMEIF3, TEIF3, HTIF3, TCIF3.
constexpr uint32_t kStreamFlags =
    (1u << 22) | (1u << 24) | (1u << 25) | (1u << 26) | (1u << 27);
// Interrupt enables left over from HAL_DMA_Start_IT(). The stream runs
// without interrupts here: the HAL handlers would act on a handle whose state
// does not match.
constexpr uint32_t kStreamIrqBits =
    DMA_SxCR_TCIE | DMA_SxCR_HTIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;

bool streamIdle() { return (kStream->CR & DMA_SxCR_EN) == 0; }

}  // namespace

void CameraStream::arm(uint8_t* buffer) {
  // Stop a previous transfer and clear its flags.
  kStream->CR &= ~DMA_SxCR_EN;
  while (!streamIdle()) {
  }
  DMA2->LIFCR = kStreamFlags;
  kStream->CR &= ~kStreamIrqBits;
  kStream->FCR &= ~DMA_SxFCR_FEIE;
  // PAR (= &DCMI->DR), direction, data sizes and the request routing are
  // still set from the camera library's first capture.
  kStream->PAR = reinterpret_cast<uint32_t>(&DCMI->DR);
  kStream->M0AR = reinterpret_cast<uint32_t>(buffer);
  kStream->NDTR = frameBytes_ / 4;
  kStream->CR |= DMA_SxCR_EN;

  // Snapshot mode: capture exactly one frame, starting at the next VSYNC.
  DCMI->CR |= DCMI_CR_CM;
  DCMI->CR |= DCMI_CR_ENABLE;
  DCMI->CR |= DCMI_CR_CAPTURE;
}

bool CameraStream::begin(uint8_t* bufferA, uint8_t* bufferB,
                         size_t frameBytes) {
  if (bufferA == nullptr || bufferB == nullptr || frameBytes == 0 ||
      (frameBytes % 4) != 0 || (frameBytes / 4) > 0xFFFF ||
      (reinterpret_cast<uintptr_t>(bufferA) & 31u) != 0 ||
      (reinterpret_cast<uintptr_t>(bufferB) & 31u) != 0) {
    return false;
  }
  buffers_[0] = bufferA;
  buffers_[1] = bufferB;
  frameBytes_ = frameBytes;
  active_ = 0;
  arm(buffers_[active_]);
  running_ = true;
  return true;
}

void CameraStream::end() {
  if (!running_) {
    return;
  }
  DCMI->CR &= ~DCMI_CR_CAPTURE;
  kStream->CR &= ~DMA_SxCR_EN;
  while (!streamIdle()) {
  }
  DMA2->LIFCR = kStreamFlags;
  running_ = false;
}

bool CameraStream::ready() const {
  return running_ && (DCMI->CR & DCMI_CR_CAPTURE) == 0 && streamIdle();
}

void CameraStream::skipFinished() {
  // Recapture into the same buffer: the other one is the frame the sketch
  // got from the last next() and may still be reading.
  if (ready()) {
    arm(buffers_[active_]);
  }
}

const uint8_t* CameraStream::next(uint32_t timeoutMs) {
  if (!running_) {
    return nullptr;
  }
  // The snapshot is complete when the DCMI has cleared CAPTURE and the DMA
  // has moved the last word (the stream disables itself at NDTR = 0).
  const uint32_t start = micros();
  const uint32_t startMs = millis();
  while ((DCMI->CR & DCMI_CR_CAPTURE) != 0 || !streamIdle()) {
    if (millis() - startMs > timeoutMs) {
      arm(buffers_[active_]);  // restart the same capture
      return nullptr;
    }
  }
  waitUs_ = micros() - start;

  uint8_t* done = buffers_[active_];
  active_ ^= 1;
  arm(buffers_[active_]);

  // The DMA wrote around the D-cache.
  SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(done),
                               static_cast<int32_t>(frameBytes_));
  return done;
}

#else  // !CBB15_CAMERA_STREAM_SUPPORTED

void CameraStream::arm(uint8_t*) {}
bool CameraStream::begin(uint8_t*, uint8_t*, size_t) { return false; }
void CameraStream::end() {}
bool CameraStream::ready() const { return false; }
void CameraStream::skipFinished() {}
const uint8_t* CameraStream::next(uint32_t) { return nullptr; }

#endif

}  // namespace cbb15
