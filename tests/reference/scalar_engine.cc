#include <fosu/compiler.h>
#include <fosu/engine/parse_document.h>

static_assert(!FOSU_SIMD);

// Separate DSO: its hidden inline functions cannot interpose with the SIMD
// engine compiled into the test executable.
extern "C" FOSU_EXPORT const fosu::ParsingEngine* fosu_test_scalar_engine() {
  return &fosu::internal::compiled_engine;
}
