#include <fosu/engine/parse_document.h>

// Private ABI: core and engine libraries are shipped as one build.
extern "C" __attribute__((visibility("default"))) const fosu::ParsingEngine*
fosu_engine_v2() {
  return &fosu::internal::native_engine;
}
