#pragma once

#include <fosu/parse_options.h>
#include <fosu/types.h>

namespace fosu {

// The rules a map's format version implies for the target client. Its
// "osu file format vN" line fixes them before any section is read, so parsing
// chooses one Format per map and passes it as a template argument: inside the
// parse, every rule is a compile-time constant. Other differences between the
// clients are separate; see Client in parse_options.h.
struct Format {
  // v128+, the format lazer writes: segmented slider syntax, unrounded
  // coordinates and any number of velocity presets.
  bool lazer_format = false;
  // Below v5: timestamps are shifted 24 ms later.
  i32  time_offset = 0;
};

inline constexpr Format kFormatBeforeV5{.time_offset = 24};
inline constexpr Format kFormatV5{};  // v5 and later, but v128 in lazer
inline constexpr Format kFormatV128{.lazer_format = true};

// stable has no rules past v14, its latest format, so it reads a v128 map
// like any other v5+ map. The map still reports version 128.
inline constexpr bool reads_lazer_format(i32 version, Client client) {
  return version >= 128 && client == Client::Lazer;
}

// Calls parse.template operator()<F>() with the Format `client` reads
// `version` with.
template <typename Parse>
decltype(auto) with_format(i32 version, Client client, Parse&& parse) {
  if (reads_lazer_format(version, client))
    return parse.template operator()<kFormatV128>();
  if (version >= 5)
    return parse.template operator()<kFormatV5>();
  return parse.template operator()<kFormatBeforeV5>();
}

}  // namespace fosu
