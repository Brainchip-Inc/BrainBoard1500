// BrainBoard1500 - Arduino library for the BrainBoard1500 (BB15) rev 2.
//
// The BB15 carries a BrainChip AKD1500 neuromorphic processor, a 4 MiB model
// flash, a PI4IOE5V6408 I/O expander and an INA2227 power monitor. This
// library drives it from an Arduino Nicla Vision and runs Akida models with
// the Akida Engine 2.19.3.
//
//   cbb15::Board          power, reset, sleep, SPI link test, flash slots
//   cbb15::Akida          load models (slot 0 = SPI push, 1..4 = flash),
//                         inference, on-chip learning
//   cbb15::PowerMonitor   INA2227: power of the 0.8 V and 1.8 V rails
//   cbb15::Console        every output on Serial and on a 128x64 SSD1306
//   cbb15::Mfcc           speech_commands MFCC front end
//   cbb15::SpeakerEncoder speaker code for the voice learning example
//   cbb15::rgb565ToRgb()  camera frame to model input (rotation, scaling)
//   cbb15::startBoard()   shared start-up of the examples (cbb15_demo.h)
//
// Models exported from the BrainChip Dev Hub: master copies in
// src/cbb15_models/, local copies next to each sketch in examples/<sketch>/.
#pragma once

#include "cbb15/cbb15_akida.h"
#include "cbb15/cbb15_board.h"
#include "cbb15/cbb15_camera_stream.h"
#include "cbb15/cbb15_config.h"
#include "cbb15/cbb15_console.h"
#include "cbb15/cbb15_crc32.h"
#include "cbb15/cbb15_demo.h"
#include "cbb15/cbb15_image.h"
#include "cbb15/cbb15_mfcc.h"
#include "cbb15/cbb15_power.h"
#include "cbb15/cbb15_speaker.h"
#include "cbb15/cbb15_types.h"
