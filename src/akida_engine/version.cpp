#include "akida/version.h"

// BrainBoard1500 patch: the upstream CMake build injects AKIDA_VERSION as a
// compile definition for this file only. The Arduino build system has no
// per-file definitions, so the value from akida-engine.cmake is set here.
#ifndef AKIDA_VERSION
#define AKIDA_VERSION "2.19.3"
#endif

namespace akida {

const char* version() {
  // Akida version is generated in CMake and added as define to this file
  return AKIDA_VERSION;
}

}  // namespace akida
