#include "cbb15_spi_fast.h"

#include <string.h>

#if CBB15_SPI_FAST && defined(STM32H747xx) && defined(CORE_CM7)
#define CBB15_SPI_FAST_SUPPORTED 1
#include "stm32h7xx_hal.h"
#else
#define CBB15_SPI_FAST_SUPPORTED 0
#endif

namespace cbb15 {
namespace spi_fast {

#if !CBB15_SPI_FAST_SUPPORTED

bool begin(SPIClass&) { return false; }
void end() {}
bool available() { return false; }
bool configure(uint32_t) { return false; }
bool write(const uint8_t*, size_t) { return false; }
bool transfer(const uint8_t*, uint8_t*, size_t) { return false; }
bool read(uint8_t*, size_t) { return false; }
uint32_t kernelHz() { return 0; }
uint32_t sckHz() { return 0; }
const char* name() { return "off"; }

#else

namespace {

constexpr uint32_t kTimeoutMs = 200;
// TSIZE is 16 bit; the engine's bursts are at most 132 B, flash pages 260 B.
constexpr size_t kMaxChunk = 1024;
constexpr size_t kZeroChunk = 256;
uint8_t zeros[kZeroChunk];

// HAL_SPI_Transmit switches CFG2.COMM to simplex, TransmitReceive back to
// full duplex. Ignore that field when checking whether someone else (mbed)
// reprogrammed the peripheral.
constexpr uint32_t kCfg2Mask = ~static_cast<uint32_t>(SPI_CFG2_COMM_Msk);

SPI_HandleTypeDef hspi;
SPI_TypeDef* instance = nullptr;
const char* instanceName = "off";
uint32_t kernel = 0;
uint32_t sck = 0;
uint32_t requested = 0;
uint32_t lastCfg1 = 0;
uint32_t lastCfg2 = 0;
bool configured = false;

const uint32_t kPrescalers[8] = {
    SPI_BAUDRATEPRESCALER_2,   SPI_BAUDRATEPRESCALER_4,
    SPI_BAUDRATEPRESCALER_8,   SPI_BAUDRATEPRESCALER_16,
    SPI_BAUDRATEPRESCALER_32,  SPI_BAUDRATEPRESCALER_64,
    SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256};

bool program(uint32_t clockHz) {
  // Highest clock not above the request, as mbed does: kernel >> (i + 1).
  uint32_t prescaler = kPrescalers[7];
  sck = kernel >> 8;
  for (uint32_t i = 0; i < 8; ++i) {
    if ((kernel >> (i + 1)) <= clockHz) {
      prescaler = kPrescalers[i];
      sck = kernel >> (i + 1);
      break;
    }
  }

  memset(&hspi.Init, 0, sizeof(hspi.Init));
  hspi.Instance = instance;
  hspi.Init.Mode = SPI_MODE_MASTER;
  hspi.Init.Direction = SPI_DIRECTION_2LINES;
  hspi.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi.Init.CLKPolarity = SPI_POLARITY_LOW;  // SPI_MODE0
  hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi.Init.NSS = SPI_NSS_SOFT;
  hspi.Init.BaudRatePrescaler = prescaler;
  hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi.Init.CRCPolynomial = 7;
  hspi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  // The HAL disables the peripheral after every transfer while the chip
  // select is still low. With KEEP_IO_STATE the pins hold their idle level
  // (SCK low) instead of floating.
  hspi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
  hspi.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi.State = HAL_SPI_STATE_RESET;
  if (HAL_SPI_Init(&hspi) != HAL_OK) {
    configured = false;
    return false;
  }
  requested = clockHz;
  lastCfg1 = instance->CFG1;
  lastCfg2 = instance->CFG2 & kCfg2Mask;
  configured = true;
  return true;
}

}  // namespace

bool begin(SPIClass& spi) {
  end();
  if (&spi == &SPI) {
    instance = SPI4;
    instanceName = "SPI4";
  }
#if SPI_HOWMANY > 1
  else if (&spi == &SPI1) {
    instance = SPI5;
    instanceName = "SPI5";
  }
#endif
  if (instance == nullptr) {
    return false;
  }
  // SPI4 and SPI5 share the SPI45SEL clock selector. The HAL in the mbed core
  // returns 0 for its reset value (PCLK2, used on the Nicla Vision), so take
  // PCLK2 directly in that case.
  kernel = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI45);
  if (kernel == 0 && (RCC->D2CCIP1R & RCC_D2CCIP1R_SPI45SEL_Msk) == 0) {
    kernel = HAL_RCC_GetPCLK2Freq();
  }
  if (kernel == 0) {
    end();
    return false;
  }
  memset(&hspi, 0, sizeof(hspi));
  hspi.Instance = instance;
  return true;
}

void end() {
  instance = nullptr;
  instanceName = "off";
  kernel = 0;
  sck = 0;
  requested = 0;
  configured = false;
}

bool available() { return instance != nullptr; }

bool configure(uint32_t clockHz) {
  if (!available()) {
    return false;
  }
  if (configured && clockHz == requested && instance->CFG1 == lastCfg1 &&
      (instance->CFG2 & kCfg2Mask) == lastCfg2) {
    return true;
  }
  return program(clockHz);
}

bool write(const uint8_t* tx, size_t n) {
  if (!configured) {
    return false;
  }
  while (n > 0) {
    const size_t c = n < kMaxChunk ? n : kMaxChunk;
    if (HAL_SPI_Transmit(&hspi, const_cast<uint8_t*>(tx),
                         static_cast<uint16_t>(c), kTimeoutMs) != HAL_OK) {
      return false;
    }
    tx += c;
    n -= c;
  }
  return true;
}

bool transfer(const uint8_t* tx, uint8_t* rx, size_t n) {
  if (!configured) {
    return false;
  }
  while (n > 0) {
    const size_t c = n < kMaxChunk ? n : kMaxChunk;
    if (HAL_SPI_TransmitReceive(&hspi, const_cast<uint8_t*>(tx), rx,
                                static_cast<uint16_t>(c),
                                kTimeoutMs) != HAL_OK) {
      return false;
    }
    tx += c;
    rx += c;
    n -= c;
  }
  return true;
}

bool read(uint8_t* rx, size_t n) {
  // The SPI slave expects zeros on COPI during the read phase.
  while (n > 0) {
    const size_t c = n < kZeroChunk ? n : kZeroChunk;
    if (!transfer(zeros, rx, c)) {
      return false;
    }
    rx += c;
    n -= c;
  }
  return true;
}

uint32_t kernelHz() { return kernel; }
uint32_t sckHz() { return sck; }
const char* name() { return instanceName; }

#endif  // CBB15_SPI_FAST_SUPPORTED

}  // namespace spi_fast
}  // namespace cbb15
