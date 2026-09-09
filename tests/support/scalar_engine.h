#pragma once

#include <fosu/engine/parsing_engine.h>

extern "C" const fosu::ParsingEngine* fosu_test_scalar_engine();

namespace fosu_test {
inline const fosu::ParsingEngine& scalar_engine() {
  return *fosu_test_scalar_engine();
}
}  // namespace fosu_test
