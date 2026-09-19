#pragma once
#include <fosu/engine/parsing_engine.h>

namespace fosu::internal {
const ParsingEngine* selected_engine();
bool engine_available(EngineKind kind);
const char* engine_name(EngineKind kind);
const EngineKind* find_engine_kind(std::string_view name);
void unload_engine();
}  // namespace fosu::internal
