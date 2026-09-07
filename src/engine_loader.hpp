#pragma once
#include <fosu/parsing_engine.hpp>

namespace fosu::internal {
const ParsingEngine* selected_engine();
bool engine_available(EngineKind kind);
const char* engine_name(EngineKind kind);
void unload_engine();
}  // namespace fosu::internal
