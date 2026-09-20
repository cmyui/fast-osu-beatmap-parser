#include <fosu/compiler.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing_engine.h>

// Private ABI: core and engine libraries are shipped as one build.
extern "C" FOSU_EXPORT const fosu::ParsingEngine* fosu_engine_v2() {
  return &fosu::internal::compiled_engine;
}
