#pragma once

#include <span>
#include "beatmap.hpp"
#include "parse_options.hpp"

namespace fosu {

enum class EngineKind : uint8_t {
    Scalar,
    Avx2,
    Neon,
};

// Private core/engine ABI. Input excludes its readable zero padding.
// Beatmap spans initially describe writable capacity; the engine fills them
// and shortens them to actual counts. No allocation, I/O, or retained state.
struct ParsingEngine {
    EngineKind kind;
    void (*parse_document)(
        std::span<const char> input,
        Beatmap& beatmap,
        ParseOptions options
    ) noexcept;
};

}  // namespace fosu
