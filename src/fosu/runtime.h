#pragma once
#include <fosu/compiler.h>
#include <fosu/engine/parsing_engine.h>

namespace fosu {
// Select once from FOSU_BACKEND (auto/scalar/avx2/neon). Returns nullptr when
// an explicitly requested engine is unavailable. Valid for the library lifetime.
#if defined(_WIN32) && !defined(FOSU_BUILDING_RUNTIME)
FOSU_IMPORT const ParsingEngine* runtime_engine();
#else
FOSU_EXPORT const ParsingEngine* runtime_engine();
#endif
}  // namespace fosu
