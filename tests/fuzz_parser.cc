// libFuzzer + ASan/UBSan: exercise full files and force arbitrary bytes through
// the hit-object and timing-point parsers, comparing all materialized fields.
#include <fosu/parser.h>
#include <cassert>
#include <string>
#include "support/canonical_dump.h"
#include "support/scalar_engine.h"

static void check(std::string_view data) {
  auto input = fosu::make_padded(data);
  fosu::Parser scalar_parser(fosu_test::scalar_engine());
  fosu::Parser simd_parser;
  auto scalar = scalar_parser.parse(input);
  auto simd = simd_parser.parse(input);
  assert(scalar && simd);
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
  std::string text(reinterpret_cast<const char*>(data), size);
  check(text);
  check("[HitObjects]\n" + text);
  check("[TimingPoints]\n" + text);
  return 0;
}
