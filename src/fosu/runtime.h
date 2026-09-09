#pragma once
#include <fosu/engine/parsing_engine.h>

namespace fosu {
// Select once from FOSU_BACKEND (auto/scalar/avx2/neon). Returns nullptr when
// an explicitly requested engine is unavailable. Valid for the library lifetime.
__attribute__((visibility("default"))) const ParsingEngine* runtime_engine();
}  // namespace fosu
