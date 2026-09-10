#pragma once

#include <fosu/arena.h>
#include <fosu/beatmap.h>
#include <fosu/parse_options.h>
#include <span>

namespace fosu {

enum class EngineKind : uint8_t {
  Scalar,
  Avx2,
  Neon,
};

// Private core/engine ABI. Input excludes its readable zero padding. The parser
// owns both arenas; the engine uses scratch chunks while parsing, then publishes
// exact contiguous arrays to the result arena. It owns no memory or retained state.
struct ParsingEngine {
  EngineKind kind;
  bool (*parse_document)(std::span<const char> input,
                         Beatmap& beatmap,
                         Arena* result_arena,
                         Arena* scratch_arena,
                         ParseOptions options) noexcept;
};

}  // namespace fosu
