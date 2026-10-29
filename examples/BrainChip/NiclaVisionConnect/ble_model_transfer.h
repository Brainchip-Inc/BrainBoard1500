#ifndef NICLA_VISION_CONNECT_BLE_MODEL_TRANSFER_H_
#define NICLA_VISION_CONNECT_BLE_MODEL_TRANSFER_H_

#include <Arduino.h>
#include <BrainBoard1500.h>

namespace model {

/** @brief Longest model name the phone app's metadata carries. */
constexpr size_t kMaxNameLength = 64u;

/**
 * @brief The demos this firmware carries, one BB15 flash slot each.
 *
 * Another demo is one more value here, one more input shape, and one more
 * slot in ble_model_transfer.cpp.
 */
enum class App : uint8_t {
  Keyword = 0,
  Vision = 1,
};

/** @brief Number of demos, and so of flash slots this sketch writes. */
constexpr size_t kAppCount = 2u;

/** @brief What one slot holds, as the application list needs to describe it. */
struct Installed {
  bool present = false;
  size_t programBytes = 0u;
  uint8_t classCount = 0u;
  uint8_t silenceClass = 0u;
  uint8_t unknownClass = 0u;
  float mfccFullScale = 0.0f;
  char name[kMaxNameLength] = {0};
};

/** @brief A model that is present in BrainBoard flash and loaded. */
struct Loaded {
  bool valid = false;
  App app = App::Keyword;
  size_t programBytes = 0u;
  size_t programInfoBytes = 0u;
  uint8_t classCount = 0u;
  uint8_t silenceClass = 0u;
  uint8_t unknownClass = 0u;
  float mfccFullScale = 0.0f;
  char name[kMaxNameLength] = {0};
};

/**
 * @brief Called whenever the model the board runs changes.
 *
 * It carries a valid model once one is loaded and ready to score with, and an
 * invalid one when the board has stopped having a model to run, which happens
 * as soon as a new transfer starts overwriting the one in flash.
 */
using LoadedHandler = void (*)(const Loaded& loaded);

/** @brief Called when a transfer starts and when it ends, for LED feedback. */
using ActivityHandler = void (*)(bool busy);

/**
 * @brief Publish the model transfer service and take the board to write with.
 *
 * Must be called before BLE.advertise(), because ArduinoBLE builds its
 * attribute table as services are added.
 *
 * @param board    Board whose external flash the model is written to.
 * @param runtime  Akida runtime the model is loaded into.
 */
void begin(cbb15::Board& board, cbb15::Akida& runtime);

/**
 * @brief Register the callbacks this module reports through.
 *
 * @param onLoaded    Called when the loaded model changes, from poll().
 * @param onActivity  Called with true while a transfer is running.
 */
void setHandlers(LoadedHandler onLoaded, ActivityHandler onActivity);

/**
 * @brief Read what every slot holds, without loading anything.
 *
 * Only the sector at the head of each slot is read, which is what the phone
 * needs to list the applications. Nothing reaches the Akida fabric until the
 * phone asks for an application to run, because only one program fits in it.
 *
 * @return Number of slots holding a model.
 */
size_t readInstalled();

/** @brief What one slot holds, whose `present` says whether it holds anything.
 */
const Installed& installed(App app);

/**
 * @brief Put one slot's model into the Akida fabric, and prove it runs.
 *
 * Only one program fits in the fabric, so this replaces whatever was loaded.
 * Only the program info is held in memory; the AKD1500 reads the data half
 * out of flash itself.
 *
 * @param app  Slot to load from.
 * @return True when that slot's model is loaded and has scored something.
 */
bool load(App app);

/**
 * @brief Do the work a transfer has queued: write flash, answer, install.
 *
 * Committing a block takes the best part of a second, so it happens here
 * rather than inside the Bluetooth write callback that asked for it.
 */
void poll();

/** @brief Say whether a transfer is under way. */
bool transferInProgress();

/** @brief The model currently loaded, whose `valid` says whether there is one.
 */
const Loaded& loaded();

}  // namespace model

#endif  // NICLA_VISION_CONNECT_BLE_MODEL_TRANSFER_H_
