#include "fosu/format.h"

#include <fosu/arena.h>
#include <fosu/beatmap.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/engine/timing_points/point.h>
#include <fosu/slider_geometry.h>
#include <fosu/types.h>
#include <tests/support/test.h>

#include <bit>
#include <cinttypes>  // IWYU pragma: keep (SIMD-only test)
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>  // IWYU pragma: keep (SIMD-only test)
#include <vector>

// Independent numeric oracle: libc conversion over a bounded copy, rather
// than a second copy of the parser's mantissa arithmetic.
static const char* reference_parse_double(const char* p,
                                          const char* end,
                                          double&     out) {
  std::string bounded(p, end);
  char*       next;
  out = strtod(bounded.c_str(), &next);
  return std::isfinite(out) ? p + (next - bounded.c_str()) : p;
}

static uint64_t rng_state = 0x243F6A8885A308D3ull;
static uint64_t rng() {
  rng_state += 0x9E3779B97F4A7C15ull;
  uint64_t z = rng_state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

// The official flattening, transcribed directly: split each piece in half
// until it is flat, depth-first, then emit its start and smoothed midpoints.
static std::vector<fosu::internal::CurvePoint> reference_bezier(
    std::span<const fosu::internal::CurvePoint> points) {
  using fosu::internal::CurvePoint;
  const size_t                         n = points.size();
  std::vector<CurvePoint>              output;
  std::vector<std::vector<CurvePoint>> pending{{points.begin(), points.end()}};
  while (!pending.empty()) {
    const auto parent = pending.back();
    pending.pop_back();
    bool flat = true;
    for (size_t i = 1; i + 1 < n; ++i)
      flat =
          flat &&
          !((parent[i - 1] - parent[i] * 2 + parent[i + 1]).squared_length() >
            0.25f);
    std::vector<CurvePoint> left(n), right(n), mid = parent;
    for (size_t i = 0; i < n; ++i) {
      left[i] = mid[0];
      right[n - i - 1] = mid[n - i - 1];
      for (size_t j = 0; j < n - i - 1; ++j)
        mid[j] = (mid[j] + mid[j + 1]) * 0.5f;
    }
    if (flat) {
      left.insert(left.end(), right.begin() + 1, right.end());
      output.push_back(parent[0]);
      for (size_t i = 1; i + 1 < n; ++i)
        output.push_back((left[2 * i - 1] + left[2 * i] * 2 + left[2 * i + 1]) *
                         0.25f);
    } else {
      pending.push_back(right);
      pending.push_back(left);
    }
  }
  output.push_back(points.back());
  return output;
}

// Quadratic and cubic segments have their own code; every count must emit
// exactly the official vertices.
static void test_bezier_flattening() {
  using fosu::internal::CurvePoint;
  struct Recorded {
    std::vector<CurvePoint> points;
    void begin_segment() {}
    void append(CurvePoint point) { points.push_back(point); }
  };
  fosu::Arena* arena = fosu::arena_alloc();
  for (size_t count = 3; count <= 10; ++count) {
    for (fosu::i32 sample = 0; sample < 1000; ++sample) {
      CurvePoint points[10];
      for (size_t i = 0; i < count; ++i) {
        auto coordinate = [] {
          const uint32_t bits = static_cast<uint32_t>(rng() & 0x807fffffu) |
                                (static_cast<uint32_t>(126 + rng() % 10) << 23);
          return std::bit_cast<float>(bits);
        };
        points[i] = {coordinate(), coordinate()};
      }
      Recorded                          curve;
      const fosu::TempArena             temp{arena};
      const std::span<const CurvePoint> span{points, count};
      CHECK(fosu::internal::bezier_distance(span, curve, arena));
      const auto expected = reference_bezier(span);
      CHECK(curve.points.size() == expected.size() &&
            std::memcmp(curve.points.data(), expected.data(),
                        expected.size() * sizeof(CurvePoint)) == 0);
    }
  }
  fosu::arena_release(arena);
}

// Compare decimal conversion against libc, including significands that
// require correct rounding rather than rounding an intermediate integer.
static void test_fuzz_parse_double() {
  char buf[96];
  for (fosu::i32 iter = 0; iter < 300000; ++iter) {
    const fosu::i32 int_digits = 1 + (fosu::i32)(rng() % 9);
    const fosu::i32 frac_digits = (fosu::i32)(rng() % 18);
    fosu::i32       len = 0;
    if (rng() % 3 == 0)
      buf[len++] = '-';
    for (fosu::i32 i = 0; i < int_digits; ++i)
      buf[len++] =
          char('0' + (i == 0 ? rng() % 9 + (int_digits > 1) : rng() % 10));
    if (frac_digits || rng() % 4 == 0) {
      buf[len++] = '.';
      for (fosu::i32 i = 0; i < frac_digits; ++i)
        buf[len++] = char('0' + rng() % 10);
    }
    const char*     tail = ",4,2\r\n";
    // Sometimes end the field inside the number: digits past it must not count.
    const fosu::i32 payload =
        rng() % 8 ? len : 1 + (fosu::i32)(rng() % (uint64_t)len);
    for (const char* t = tail; *t; ++t)
      buf[len++] = *t;
    memset(buf + len, 0, sizeof(buf) - (size_t)len);

    double      got = -1, want = -2;
    const char* gp = fosu::internal::parse_double(buf, buf + payload, got);
    const char* wp = reference_parse_double(buf, buf + payload, want);
    CHECK_EQ(gp - buf, wp - buf);
    if (gp != buf)
      CHECK(got == want);
    if (g_failures) {
      printf("  failing double: %.*s\n", payload, buf);
      return;
    }
  }
  // The bounded fast_float fallback must agree with the independent libc
  // result.
  const char* long_cases[] = {"342.857142857142857142857",
                              "123456789012345678901", "0.6999999999999999556"};
  for (const char* c : long_cases) {
    std::string padded(c);
    padded.append(64, '\0');
    double got = 0;
    fosu::internal::parse_double(padded.data(), padded.data() + strlen(c), got);
    CHECK(got == strtod(c, nullptr));
  }
}

static void test_fuzz_parse_osu_float_integers() {
  char buf[96];
  for (fosu::i32 iter = 0; iter < 100000; ++iter) {
    fosu::i32 len = 0;
    if (rng() % 4 == 0)
      buf[len++] = ' ';
    if (rng() % 3 == 0)
      buf[len++] = rng() % 2 ? '-' : '+';
    const fosu::i32 digits = 1 + static_cast<fosu::i32>(rng() % 10);
    for (fosu::i32 i = 0; i < digits; ++i)
      buf[len++] = static_cast<char>('0' + rng() % 10);
    if (rng() % 4 == 0)
      buf[len++] = ' ';
    buf[len++] = ',';
    memset(buf + len, 0, sizeof(buf) - static_cast<size_t>(len));

    float       got = 0;
    const char* next = fosu::internal::parse_osu_float(buf, buf + len, got);
    std::string bounded(buf, buf + len);
    char*       reference_end;
    const float want = strtof(bounded.c_str(), &reference_end);
    const char* want_next = buf + (reference_end - bounded.c_str());
    while (want_next < buf + len &&
           fosu::internal::is_numeric_space(*want_next))
      ++want_next;
    const bool valid = std::abs(want) <= float(INT32_MAX);
    CHECK_EQ(next - buf, valid ? want_next - buf : 0);
    if (valid)
      CHECK_EQ(std::bit_cast<uint32_t>(got), std::bit_cast<uint32_t>(want));
    if (g_failures) {
      printf("  failing float integer: %.*s\n", len, buf);
      return;
    }
  }
}

#if FOSU_SIMD
// Every byte value at every lane: SIMD masks must preserve exact positions,
// including NUL, high-bit bytes and the boundary between vector registers.
static void test_byte_masks() {
  using namespace fosu::internal;
  alignas(32) char text[32];
  for (unsigned lane = 0; lane < 32; ++lane) {
    for (unsigned value = 0; value < 256; ++value) {
      memset(text, '5', sizeof(text));
      text[lane] = static_cast<char>(value);
      const auto     v = load32(text);
      const uint32_t bit = uint32_t(1) << lane;
      CHECK_EQ(nondigit_mask32(v), value >= '0' && value <= '9' ? 0u : bit);
      CHECK_EQ(comma_mask32(v), value == ',' ? bit : 0u);
      CHECK_EQ(equal_mask32(v, broadcast_byte<'\n'>()),
               value == '\n' ? bit : 0u);
    }
  }
}

static std::string hitobject_document(std::string_view line) {
  return "osu file format v128\n\n[HitObjects]\n" + std::string(line) + '\n';
}

static void check_same_hitobject(const fosu::Beatmap& fast,
                                 const fosu::Beatmap& scalar) {
  CHECK_EQ(fast.hit_objects.size(), scalar.hit_objects.size());
  CHECK_EQ(fast.stats.malformed_lines, scalar.stats.malformed_lines);
  if (fast.hit_objects.empty() || scalar.hit_objects.empty())
    return;
  const auto& got = fast.hit_objects[0];
  const auto& want = scalar.hit_objects[0];
  CHECK_EQ(got.x, want.x);
  CHECK_EQ(got.y, want.y);
  CHECK_EQ(got.time, want.time);
  CHECK_EQ(got.type, want.type);
  CHECK_EQ(got.hitsound, want.hitsound);
  CHECK_EQ(got.hit_sample, want.hit_sample);
}

static void check_same_slider(const fosu::Beatmap& fast,
                              const fosu::Beatmap& scalar) {
  check_same_hitobject(fast, scalar);
  CHECK_EQ(fast.sliders.size(), scalar.sliders.size());
  CHECK_EQ(fast.slider_points.size(), scalar.slider_points.size());
  if (fast.sliders.empty() || scalar.sliders.empty())
    return;
  const auto& got = fast.sliders[0];
  const auto& want = scalar.sliders[0];
  CHECK_EQ(got.slides, want.slides);
  CHECK(memcmp(&got.length, &want.length, sizeof(got.length)) == 0);
  CHECK_EQ(got.point_count, want.point_count);
  for (uint32_t i = 0; i < got.point_count; ++i) {
    CHECK_EQ(fast.slider_points[got.point_begin + i].x,
             scalar.slider_points[want.point_begin + i].x);
    CHECK_EQ(fast.slider_points[got.point_begin + i].y,
             scalar.slider_points[want.point_begin + i].y);
  }
}

static void test_fuzz_slider_points() {
  for (fosu::i32 iter = 0; iter < 30000; ++iter) {
    std::string    coordinate;
    const uint64_t kind = rng() % 16;
    if (kind == 0)
      coordinate += '-';
    if (kind != 1) {
      const fosu::i32 digits =
          1 + static_cast<fosu::i32>(rng() % (kind < 12 ? 4 : 8));
      for (fosu::i32 i = 0; i < digits; ++i)
        coordinate += static_cast<char>('0' + rng() % 10);
    }
    const std::string document =
        hitobject_document("0,0,0,2,0,B|" + coordinate + ":2,1,10");
    check_same_slider(parse_str(document), parse_str(document, false));
    if (g_failures) {
      printf("  failing slider coordinate: %s\n", coordinate.c_str());
      return;
    }
  }
}

static void test_hitobject_field_shapes() {
  for (unsigned x = 1; x <= 3; ++x)
    for (unsigned y = 1; y <= 3; ++y)
      for (unsigned t = 1; t <= 10; ++t)
        for (const char* type : {"1", "17", "129"})
          for (const char* sound :
               {"0", "9", "00", "01", "10", "15", "42", "99"}) {
            std::string line = std::string(x, '1') + ',' + std::string(y, '2') +
                               ',' + std::string(t, '1') + ',' + type + ',' +
                               sound;
            const std::string document = hitobject_document(line);
            const auto        fast = parse_str(document);
            const auto        scalar = parse_str(document, false);
            // Editor shapes always use the fixed-width loops.
            if (t >= 4 && t <= 7 && strlen(type) <= 3 && strlen(sound) <= 2)
              CHECK_EQ(fast.stats.fast_path_lines, 1u);
            check_same_hitobject(fast, scalar);
          }
}

static void test_hitobject_timestamp_boundaries() {
  for (const char* time : {"99999999", "100000000", "2147483647", "2147483648",
                           "000000001", "0000000001"}) {
    std::string       line = std::string("123,45,") + time + ",1,42";
    const std::string document = hitobject_document(line);
    check_same_hitobject(parse_str(document), parse_str(document, false));
  }
}

// Exercise both eight-digit chunks, the optional fraction, and a second
// fractional chunk through the resulting Slider domain object.
static void test_fuzz_slider_length() {
  for (fosu::i32 iter = 0; iter < 30000; ++iter) {
    std::string length = std::to_string(rng() % 131074);
    length.insert(0, rng() % (9 - length.size()), '0');
    if (rng() % 4 != 0) {
      length += '.';
      const auto digits = rng() % 14;
      for (uint64_t i = 0; i < digits; ++i)
        length += static_cast<char>('0' + rng() % 10);
    }
    const std::string document =
        hitobject_document("0,0,0,2,0,B|1:2,1," + length + ",0:0");
    check_same_slider(parse_str(document), parse_str(document, false));
    if (g_failures) {
      printf("  failing slider length: %s\n", length.c_str());
      return;
    }
  }
}

// Fuzz the common timing point fast path against the general parser:
// whenever it accepts a line, every field must be bitwise identical.
// Shapes: 8-field editor lines plus old 2..7-field forms, decimal and
// negative offsets, integer and long-fraction beatLengths, and injected
// junk bytes (a '|' posing as the decimal point caught a real bug here).
static void test_fuzz_timing_point() {
  char   buf[256];
  size_t accepted = 0;
  for (fosu::i32 iter = 0; iter < 400000; ++iter) {
    fosu::i32      len = 0;
    const uint64_t shape = rng() % 10;
    if (shape == 9)
      buf[len++] = '-';
    const fosu::i32 od = 1 + (fosu::i32)(rng() % 11);
    for (fosu::i32 i = 0; i < od; ++i)
      buf[len++] = char('0' + rng() % 10);
    if (shape == 8) {
      buf[len++] = '.';
      for (fosu::i32 i = 0; i < 3; ++i)
        buf[len++] = char('0' + rng() % 10);
    }
    buf[len++] = ',';
    if (rng() % 2)
      buf[len++] = '-';
    const fosu::i32 bi = 1 + (fosu::i32)(rng() % 4);
    for (fosu::i32 i = 0; i < bi; ++i)
      buf[len++] = char('0' + rng() % 10);
    if (rng() % 2) {
      buf[len++] = '.';
      const fosu::i32 bf = 1 + (fosu::i32)(rng() % 15);
      for (fosu::i32 i = 0; i < bf; ++i)
        buf[len++] = char('0' + rng() % 10);
    }
    const fosu::i32 nf = shape == 7 ? (fosu::i32)(rng() % 6) : 6;
    for (fosu::i32 f = 0; f < nf; ++f) {
      buf[len++] = ',';
      if (f == 1) {
        buf[len++] = char('0' + rng() % 4);
        continue;
      }
      // The editor writes one-digit meter, uninherited and effects fields,
      // a one- or two-digit sample index and up to three volume digits.
      const fosu::i32 widest = f == 3 ? 3 : f == 2 ? 2 : rng() % 8 ? 1 : 3;
      const fosu::i32 fd = 1 + (fosu::i32)(rng() % (uint64_t)widest);
      for (fosu::i32 i = 0; i < fd; ++i)
        buf[len++] = char('0' + rng() % 10);
    }
    if (rng() % 3 == 0) {
      const char junk[] = {'-', '.', ',', 'x', ' ', ':', '|', ','};
      buf[rng() % (uint64_t)len] = junk[rng() % 8];
    }
    memset(buf + len, 0, sizeof(buf) - (size_t)len);

    // Any carried time width must give the same result.
    uint32_t          time_digits = 1 + (uint32_t)(rng() % 8);
    fosu::TimingPoint tp;
    const char*       next =
        fosu::internal::parse_common_timing_point<fosu::kFormatV5>(
            buf, buf + len, time_digits, tp);
    if (!next)
      continue;
    CHECK(next == buf + len);
    ++accepted;
    const auto reference =
        fosu::internal::parse_timing_point<fosu::kFormatV5>(buf, buf + len);
    CHECK(reference.has_value());
    if (reference) {
      const auto& w = *reference;
      CHECK(memcmp(&tp.time, &w.time, 8) == 0);
      CHECK(memcmp(&tp.beat_length, &w.beat_length, 8) == 0);
      CHECK_EQ(tp.meter, w.meter);
      CHECK_EQ(tp.sample_set, w.sample_set);
      CHECK_EQ(tp.sample_index, w.sample_index);
      CHECK_EQ(tp.volume, w.volume);
      CHECK_EQ(tp.uninherited, w.uninherited);
      CHECK_EQ(tp.effects, w.effects);
    }
    if (g_failures) {
      printf("  failing timing line: %.*s\n", len, buf);
      return;
    }
  }
  printf("  timing fuzz: fast path accepted %zu lines\n", accepted);
  CHECK(accepted > 40000);
}

// Generate a value that renders with exactly `digits` decimal digits.
static uint64_t value_with_digits(fosu::i32 digits, uint64_t max) {
  const uint64_t lo = digits == 1 ? 0
                      : fosu::internal::kPow10[digits - 1] < 1e19
                          ? (uint64_t)fosu::internal::kPow10[digits - 1]
                          : 0;
  uint64_t       hi = (uint64_t)fosu::internal::kPow10[digits] - 1;
  if (hi > max)
    hi = max;
  if (lo > hi)
    return hi;
  return lo + rng() % (hi - lo + 1);
}

static void test_fuzz_hitobject_fields() {
  char      buf[128];
  fosu::i32 fast_taken = 0;
  for (fosu::i32 iter = 0; iter < 30000; ++iter) {
    const fosu::i32 lx = 1 + (fosu::i32)(rng() % 3);
    const fosu::i32 ly = 1 + (fosu::i32)(rng() % 3);
    const fosu::i32 lt = 1 + (fosu::i32)(rng() % 10);
    const fosu::i32 lty = 1 + (fosu::i32)(rng() % 3);
    const uint64_t  x = value_with_digits(lx, 999);
    const uint64_t  y = value_with_digits(ly, 999);
    const uint64_t  t = value_with_digits(lt, 9999999999ull);
    const uint64_t  ty = value_with_digits(lty, 255);
    const uint64_t  hs = rng() % 100;
    int len = snprintf(buf, sizeof buf,
                       "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                       ",%" PRIu64 ",0:0:0:0:",
                       x, y, t, ty, hs);
    memset(buf + len, 0, sizeof(buf) - (size_t)len);

    // Randomly corrupt some lines; the invariant is that whenever the
    // fast path accepts, it must agree exactly with the scalar path.
    if (rng() % 4 == 0) {
      const fosu::i32 pos = (fosu::i32)(rng() % (uint64_t)len);
      const char      junk[] = {'-', '.', ',', 'x', ' ', '|'};
      buf[pos] = junk[rng() % sizeof junk];
    }

    const std::string document = hitobject_document(buf);
    const auto        fast = parse_str(document);
    const auto        scalar = parse_str(document, false);
    fast_taken += fast.stats.fast_path_lines != 0;
    check_same_hitobject(fast, scalar);
    if (g_failures) {
      printf("  failing line: %s\n", buf);
      return;
    }
  }
  printf("  fuzz: fast path accepted %d lines\n", fast_taken);
  CHECK(fast_taken > 5000);
}
#endif

int main() {
  test_fuzz_parse_double();
  test_fuzz_parse_osu_float_integers();
#if FOSU_SIMD
  test_byte_masks();
  test_hitobject_field_shapes();
  test_hitobject_timestamp_boundaries();
  test_fuzz_slider_points();
  test_fuzz_slider_length();
  test_fuzz_hitobject_fields();
  test_fuzz_timing_point();
  puts("SIMD path: enabled");
#else
  puts("SIMD path: not built");
#endif
  test_bezier_flattening();
  return test_result();
}
