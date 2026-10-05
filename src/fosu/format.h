#pragma once

#include <fosu/types.h>

namespace fosu {

// The rules a map's format version implies. Its "osu file format vN" line
// fixes them before any section is read, so parsing chooses one Format per
// map and passes it as a template argument: inside the parse, every rule is a
// compile-time constant. Which client's behaviour to follow is separate; see
// Client in parse_options.h.
struct Format {
  // v128+, the format lazer writes: segmented slider syntax, unrounded
  // coordinates and any number of velocity presets.
  bool lazer_format = false;
  // Below v5: timestamps are shifted 24 ms later.
  i32  time_offset = 0;
};

inline constexpr Format kFormatBeforeV5{.time_offset = 24};
inline constexpr Format kFormatV5{};  // v5 to v127
inline constexpr Format kFormatV128{.lazer_format = true};

// Calls parse.template operator()<F>() with the Format of `version`.
template <typename Parse>
decltype(auto) with_format(i32 version, Parse&& parse) {
  if (version >= 128)
    return parse.template operator()<kFormatV128>();
  if (version >= 5)
    return parse.template operator()<kFormatV5>();
  return parse.template operator()<kFormatBeforeV5>();
}

}  // namespace fosu
