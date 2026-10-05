#pragma once

#include <fosu/beatmap.h>
#include <fosu/parse_options.h>
#include <fosu/types.h>

#include <span>

namespace fosu {

enum class EngineKind : u8 {
  Scalar,
  Avx2,
  Neon,
};

// Private core/engine ABI. Input excludes its readable zero padding.
// Beatmap spans initially describe writable capacity; the engine fills them
// and shortens them to actual counts. No allocation, I/O, or retained state.
// The only error an engine reports is ParseErrorCode::Unloadable.
struct ParsingEngine {
  EngineKind kind;
  ParseError (*parse_document)(std::span<const char> input,
                               Beatmap&              beatmap,
                               ParseOptions          options) noexcept;
};

}  // namespace fosu
