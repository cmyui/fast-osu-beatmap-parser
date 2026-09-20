// libFuzzer + ASan/UBSan: exercise full files and force arbitrary bytes through
// the hit-object and timing-point parsers, comparing all materialized fields.
#include <fosu/io.h>
#include <fosu/parse_options.h>
#include <fosu/parser.h>
#include <tests/support/canonical_dump.h>
#include <tests/support/scalar_engine.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

static void check(std::string_view data, fosu::ParseOptions options) {
  auto         input = fosu::make_padded(data);
  fosu::Parser scalar_parser(fosu_test::scalar_engine());
  fosu::Parser simd_parser;
  auto         scalar = scalar_parser.parse(input, options);
  auto         simd = simd_parser.parse(input, options);
  assert(bool(scalar) == bool(simd));
  if (!scalar) {
    assert(scalar.error().code == simd.error().code);
    return;
  }
  auto a = *scalar.value();
  auto b = *simd.value();
  a.stats.fast_path_lines = a.stats.slow_path_lines = 0;
  b.stats.fast_path_lines = b.stats.slow_path_lines = 0;
  std::string x, y;
  fosu_dump::dump(a, x);
  fosu_dump::dump(b, y);
  assert(x == y);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 65536)
    return 0;
  std::string              text(reinterpret_cast<const char*>(data), size);
  const fosu::ParseOptions timing{.calculate_slider_end_times = true};
  check(text, timing);
  check("[HitObjects]\n" + text, timing);
  check("[TimingPoints]\n" + text, timing);
  if (size <= 4096) {
    const fosu::ParseOptions all{.calculate_slider_events = true,
                                 .apply_stacking = true};
    check(text, all);
    check("[HitObjects]\n" + text, all);
    check("[TimingPoints]\n" + text, all);
  }
  return 0;
}
