#pragma once

#include <fosu/types.h>

namespace fosu {

// The version-dependent rules of the .osu format. A map's "osu file format vN"
// line fixes them before any section is read, so parsing chooses one Format
// per map and passes it as a template argument: inside the parse, every rule
// is a compile-time constant.
struct Format {
  // v128+ (lazer): segmented slider syntax, unrounded coordinates and any
  // number of velocity presets.
  bool lazer = false;
  // Below v5: timestamps are shifted 24 ms later.
  i32  time_offset = 0;
};

inline constexpr Format kLegacyFormat{.time_offset = 24};  // below v5
inline constexpr Format kStableFormat{};                   // v5 to v127
inline constexpr Format kLazerFormat{.lazer = true};       // v128+

// Calls parse.template operator()<F>() with the Format of `version`.
template <typename Parse>
decltype(auto) with_format(i32 version, Parse&& parse) {
  if (version >= 128)
    return parse.template operator()<kLazerFormat>();
  if (version >= 5)
    return parse.template operator()<kStableFormat>();
  return parse.template operator()<kLegacyFormat>();
}

}  // namespace fosu
