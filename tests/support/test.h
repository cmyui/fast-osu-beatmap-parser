#pragma once
#include <fosu/beatmap.h>
#include <fosu/parse_options.h>
#include <fosu/parser.h>
#include <tests/support/scalar_engine.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                          \
    }                                                        \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
// For curved path geometry, which platform math libraries may round
// differently, and for decimal approximations of binary results.
#define CHECK_NEAR(a, b, tolerance) CHECK(std::abs((a) - (b)) <= (tolerance))

inline int test_result() {
  if (g_failures)
    printf("%d FAILURES\n", g_failures);
  else
    puts("all tests passed");
  return g_failures ? 1 : 0;
}

namespace fosu_test {
// Internal scanners may read up to kBufferPadding bytes past their end
// pointer; the parser guarantees that padding, so direct calls must too.
inline std::string padded(std::string_view text) {
  std::string buffer(text);
  buffer.resize(text.size() + fosu::kBufferPadding);
  return buffer;
}
}  // namespace fosu_test

[[maybe_unused]] static fosu::Beatmap& require_parse(fosu::Beatmap* parsed) {
  CHECK(parsed);
  if (!parsed)
    std::abort();
  return *parsed;
}

// The result borrows the parser's memory: the next call with the same engine
// invalidates its spans.
[[maybe_unused]] static fosu::Beatmap parse_str(
    const std::string& s,
    bool               use_simd = true,
    fosu::ParseOptions options = {}) {
  static fosu::Parser native_parser;
  static fosu::Parser scalar_parser(fosu_test::scalar_engine());
  auto&               parser = use_simd ? native_parser : scalar_parser;
  return require_parse(parser.parse(s, options));
}
