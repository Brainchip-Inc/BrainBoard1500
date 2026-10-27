#include <Arduino.h>
#include <ArduinoBLE.h>
#include <BrainBoard1500.h>
#include <STM32H747_System.h>

#include "ble_model_transfer.h"
#include "ble_protocol.h"
#include "device_status.h"
#include "kws_pipeline.h"

#ifndef ARDUINO_NICLA_VISION
#error "NiclaVisionConnect requires Arduino Nicla Vision."
#endif

namespace {

constexpr uint32_t kSerialBaud = 115200u;
constexpr uint32_t kSerialWaitMs = 3000u;
constexpr uint32_t kBootSettleMs = 250u;

constexpr const char* kSketchName = "NiclaVisionConnect";
constexpr const char* kLogPrefix = "[NiclaVisionConnect]";

cbb15::Board g_board;
cbb15::Akida g_runtime;
bool g_was_connected = false;

// A spare RTC backup register, which survives a reset but not a power cycle.
// It carries a cookie saying whether the previous run ended by asking to
// restart, which is the only way to tell a commanded restart from a crash: the
// bootloader clears RCC_RSR before the sketch runs, and the reset reason it is
// meant to leave behind in DR8 is never written.
constexpr RTCBackup kRunStateRegister = RTCBackup::DR9;
constexpr uint32_t kRunStateRunning = 0x42423135u;
constexpr uint32_t kRunStateCommanded = 0x42423100u;

/**
 * @brief Say how the previous run ended, then mark this one as under way.
 *
 * @return "commanded" when the last run asked to restart, "unexpected" when it
 *         stopped without asking, and "cold" when there was no previous run.
 */
const char* take_previous_run_outcome() {
  const uint32_t cookie = STM32H747::readBackupRegister(kRunStateRegister);
  STM32H747::writeBackupRegister(kRunStateRegister, kRunStateRunning);

  switch (cookie) {
    case kRunStateCommanded:
      return "commanded";
    case kRunStateRunning:
      return "unexpected";
    default:
      return "cold";
  }
}

/** @brief Give a host a moment to open the USB serial port. */
void wait_for_serial() {
  const uint32_t start_ms = millis();
  while (!Serial && (millis() - start_ms) < kSerialWaitMs) {
  }
}

/**
 * @brief Bring the BrainBoard up ready to be given a model.
 *
 * No model is loaded here: it arrives over Bluetooth, or is read back from the
 * board's own flash by the transfer module. The flash is probed in full, as
 * the library's model flasher does, because this sketch writes it.
 *
 * @return What the library reported, Ok when the board and its runtime are
 *         ready.
 */
cbb15::Status prepare_board() {
  // The defaults fit a BB15 rev 2, including its power enable on pin 15.
  cbb15::BoardConfig config;
  config.assumeFlashReady = false;
  const cbb15::Status status = g_board.begin(config, &Serial);
  if (status != cbb15::Status::Ok) {
    return status;
  }
  return g_runtime.begin(g_board);
}

/** @brief Stop the demo, whatever it was doing. */
void stop_pipelines() {
  kws::setInferenceRunning(false);
  kws::setWaveformStreaming(false);
}

/**
 * @brief Follow the model the transfer module reports.
 *
 * @param loaded  Description of the model now on the BrainBoard, or an invalid
 *                one when the board has stopped having a model to run.
 */
void on_model_loaded(const model::Loaded& loaded) {
  if (!loaded.valid) {
    kws::setModel(nullptr, kws::ModelParameters());
    Serial.print(kLogPrefix);
    Serial.println(" nothing is in the Akida fabric now");
    return;
  }

  bool ready = false;
  switch (loaded.app) {
    case model::App::Keyword: {
      kws::ModelParameters parameters;
      parameters.classCount = loaded.classCount;
      parameters.silenceClass = loaded.silenceClass;
      parameters.unknownClass = loaded.unknownClass;
      parameters.mfccFullScale = loaded.mfccFullScale;
      ready = kws::setModel(&g_runtime, parameters);
      break;
    }
  }

  Serial.print(kLogPrefix);
  Serial.print(" model=");
  Serial.print(loaded.name);
  Serial.print(" classes=");
  Serial.print(loaded.classCount);
  Serial.print(" program_bytes=");
  Serial.print(static_cast<unsigned long>(loaded.programBytes));
  Serial.print(" program_info_bytes=");
  Serial.print(static_cast<unsigned long>(loaded.programInfoBytes));
  Serial.print(" ready=");
  Serial.println(ready ? 1 : 0);
}

/**
 * @brief Show a model transfer on the LED and keep audio out of its way.
 *
 * @param busy  True while a transfer is running.
 */
void on_transfer_activity(bool busy) {
  if (busy) {
    stop_pipelines();
    device::setLedMode(device::LedMode::ModelTransfer);
    return;
  }
  device::setLedMode(BLE.connected() ? device::LedMode::Connected
                                     : device::LedMode::Advertising);
}

/**
 * @brief Start or stop one application.
 *
 * Only one program fits in the Akida fabric, so starting an application that
 * is not the loaded one is a model swap.
 *
 * @param app      Application the phone named.
 * @param running  True to start it, false to stop it.
 */
void on_deploy(model::App app, bool running) {
  stop_pipelines();
  if (!running) {
    return;
  }
  if (!model::load(app)) {
    // Present but unusable and simply absent are different problems, and a
    // reader who cannot tell them apart will read a failed load as a model
    // that has been overwritten.
    Serial.print(kLogPrefix);
    Serial.print(model::installed(app).present
                     ? " deploy failed: that model is in flash but would not "
                       "load, and it is still there"
                     : " deploy refused: no model has been sent for that "
                       "application");
    Serial.println();
    return;
  }
  switch (app) {
    case model::App::Keyword:
      kws::setInferenceRunning(true);
      break;
  }
}

/**
 * @brief Open or close one application's live stream.
 *
 * @param app        Application the phone named.
 * @param streaming  True to send it, false to stop.
 */
void on_stream(model::App app, bool streaming) {
  switch (app) {
    case model::App::Keyword:
      kws::setWaveformStreaming(streaming);
      break;
  }
}

/** @brief Restart the board, as the phone's reset command asks. */
void on_reset() {
  STM32H747::writeBackupRegister(kRunStateRegister, kRunStateCommanded);
  NVIC_SystemReset();
}

/**
 * @brief Send one completed audio block to the phone.
 *
 * @param values  Interleaved minimum and maximum samples.
 */
void on_waveform(const int16_t* values) {
  protocol::sendWaveform(values, kws::kWaveformValues);
}

/**
 * @brief Report a keyword the model has detected.
 *
 * @param detection  Class and score the pipeline decided on.
 */
void on_detection(const kws::Detection& detection) {
  device::flashDetection();
  protocol::sendDetection(kws::classLabel(detection.classIndex),
                          detection.confidence);
  Serial.print(kLogPrefix);
  Serial.print(" keyword=");
  Serial.println(kws::classLabel(detection.classIndex));
}

/** @brief Follow the connection on the LED and stop work on a disconnect. */
void track_connection() {
  const bool connected = BLE.connected();
  if (connected == g_was_connected) {
    return;
  }
  g_was_connected = connected;

  if (connected) {
    device::setLedMode(device::LedMode::Connected);
    Serial.print(kLogPrefix);
    Serial.println(" connected");
    return;
  }

  stop_pipelines();
  device::setLedMode(device::LedMode::Advertising);
  Serial.print(kLogPrefix);
  Serial.println(" disconnected");
}

}  // namespace

void setup() {
  // Serial first: the fuel gauge and the BrainBoard are both slow to answer,
  // and a board whose USB is not up yet cannot be reflashed without the button.
  Serial.begin(kSerialBaud);
  wait_for_serial();
  delay(kBootSettleMs);

  Serial.println();
  Serial.println(kSketchName);
  Serial.print(kLogPrefix);
  Serial.print(" previous_run=");
  Serial.println(take_previous_run_outcome());

  device::begin();
  const device::Power power = device::readPower();
  Serial.print(kLogPrefix);
  Serial.print(" serial=");
  Serial.print(device::serialNumber());
  Serial.print(" usb=");
  Serial.print(power.usbPowered ? 1 : 0);
  Serial.print(" battery=");
  Serial.print(power.percent);
  Serial.print("% state=");
  Serial.println(static_cast<int>(power.state));

  const cbb15::Status board_status = prepare_board();
  if (board_status != cbb15::Status::Ok) {
    device::setLedMode(device::LedMode::Failed);
    Serial.print(kLogPrefix);
    Serial.print(" result=FAIL stage=board detail=");
    Serial.println(cbb15::statusName(board_status));
    return;
  }
  Serial.print(kLogPrefix);
  Serial.print(" akida_ready ip_version=0x");
  Serial.println(g_board.readIpVersion(), HEX);

  if (!kws::begin()) {
    device::setLedMode(device::LedMode::Failed);
    Serial.print(kLogPrefix);
    Serial.println(" result=FAIL stage=microphone");
    return;
  }
  kws::setHandlers(on_waveform, on_detection);

  if (!BLE.begin()) {
    device::setLedMode(device::LedMode::Failed);
    Serial.print(kLogPrefix);
    Serial.println(" result=FAIL stage=ble");
    return;
  }

  model::setHandlers(on_model_loaded, on_transfer_activity);
  model::begin(g_board, g_runtime);
  protocol::setHandlers(on_deploy, on_stream, on_reset);
  if (!protocol::begin()) {
    device::setLedMode(device::LedMode::Failed);
    Serial.print(kLogPrefix);
    Serial.println(" result=FAIL stage=advertise");
    return;
  }

  if (model::readInstalled() == 0u) {
    Serial.print(kLogPrefix);
    Serial.println(" no model in flash, waiting for one over bluetooth");
  }

  device::setLedMode(device::LedMode::Advertising);
  Serial.print(kLogPrefix);
  Serial.print(" advertising as ");
  Serial.print(protocol::kDeviceName);
  Serial.print(" at ");
  Serial.println(BLE.address());
}

void loop() {
  BLE.poll();
  track_connection();
  protocol::poll();
  model::poll();
  device::updateLed();

  if (!model::transferInProgress()) {
    kws::poll();
  }
}
