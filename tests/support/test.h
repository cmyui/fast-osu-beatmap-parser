#pragma once
#include <unistd.h>

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <fosu/parser.h>
#include "scalar_engine.h"

static int g_failures = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                          \
    }                                                        \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

inline int test_result() {
  if (g_failures)
    printf("%d FAILURES\n", g_failures);
  else
    puts("all tests passed");
  return g_failures ? 1 : 0;
}

[[maybe_unused]] static fosu::Beatmap& require_parse(
    fosu::Result<fosu::Beatmap*> parsed) {
  CHECK(parsed);
  if (!parsed)
    std::abort();
  return *parsed.value();
}

[[maybe_unused]] static fosu::Beatmap parse_str(const std::string& s,
                                                bool use_simd = true) {
  static fosu::Parser native_parser;
  static fosu::Parser scalar_parser(fosu_test::scalar_engine());
  auto& parser = use_simd ? native_parser : scalar_parser;
  return require_parse(parser.parse(s.data(), s.size()));
}
