#pragma once

// Scalar/SWAR numeric parsing. Like the rest of the parser, these helpers
// assume the buffer is followed by kBufferPadding readable zero bytes.

#include <fosu/engine/primitives/packed_digits.h>
#include <fosu/engine/third_party/fast_float.h>
#include <fosu/types.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace fosu::internal {

inline bool is_digit(char c) {
  return static_cast<u8>(c - '0') <= 9;
}

// All parse_* helpers return the advanced pointer, or `p` unchanged on failure.

inline const char* parse_u64(const char* p, const char* end, u64& out) {
  const char* start = p;
  u64         v = 0;
  while (p < end && is_digit(*p)) {
    const u64 digit = static_cast<unsigned>(*p - '0');
    if (v > UINT64_MAX / 10 ||
        (v == UINT64_MAX / 10 && digit > UINT64_MAX % 10)) [[unlikely]] {
      do {
        ++p;
      } while (p < end && is_digit(*p));
      out = UINT64_MAX;
      return p;
    }
    v = v * 10 + digit;
    ++p;
  }
  if (p == start)
    return start;
  out = v;
  return p;
}

inline const char* parse_i64(const char* p, const char* end, i64& out) {
  const char* start = p;
  bool        neg = false;
  if (p < end && (*p == '-' || *p == '+')) {
    neg = *p == '-';
    ++p;
  }
  u64         mag;
  const char* q = parse_u64(p, end, mag);
  if (q == p)
    return start;
  // Convert only representable magnitudes; negating INT64_MIN is undefined.
  if (neg)
    out = mag >= u64(INT64_MAX) + 1 ? INT64_MIN : -static_cast<i64>(mag);
  else
    out = mag > u64(INT64_MAX) ? INT64_MAX : static_cast<i64>(mag);
  return q;
}

inline i32 clamp_i32(i64 v) {
  if (v > INT32_MAX)
    return INT32_MAX;
  if (v < INT32_MIN)
    return INT32_MIN;
  return static_cast<i32>(v);
}

inline constexpr f64 kPow10[20] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
};

inline constexpr u64 kPow10u[9] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
};
inline constexpr u64 kMaxExactDoubleInteger = 1ull << 53;

// Fast decimal parse for the values that appear in .osu files. Digit runs
// are consumed 8 at a time with SWAR conversion instead of byte loops.
// Values with exponents or more than 18 significant digits fall back to
// a bounded, locale-independent conversion.
template <auto Fallback>
inline const char* parse_double_impl(const char* p, const char* end, f64& out) {
  const char* start = p;
  bool        neg = false;
  if (p < end && (*p == '-' || *p == '+')) {
    neg = *p == '-';
    ++p;
  }
  u64  mant = 0;
  i32  digits = 0;
  i32  frac = 0;
  bool any = false;
  for (;;) {
    u32 run = digit_run8(p);
    if (run > static_cast<u64>(end - p))
      run = static_cast<u32>(end - p);
    if (!run)
      break;
    any = true;
    if (digits + static_cast<i32>(run) > 18) {
      return Fallback(start, end, out);
    }
    mant = mant * kPow10u[run] + swar_parse_u64(p, run);
    digits += static_cast<i32>(run);
    p += run;
    if (run < 8)
      break;
  }
  if (p < end && *p == '.') {
    ++p;
    for (;;) {
      u32 run = digit_run8(p);
      if (run > static_cast<u64>(end - p))
        run = static_cast<u32>(end - p);
      if (!run)
        break;
      any = true;
      if (digits + static_cast<i32>(run) > 18) {
        return Fallback(start, end, out);
      }
      mant = mant * kPow10u[run] + swar_parse_u64(p, run);
      digits += static_cast<i32>(run);
      frac += static_cast<i32>(run);
      p += run;
      if (run < 8)
        break;
    }
  }
  if (!any)
    return Fallback(start, end, out);
  if (p < end && (*p == 'e' || *p == 'E')) {
    return Fallback(start, end, out);
  }
  // Rounding an inexact integer mantissa before division can move the
  // result by one ULP. The fallback rounds the original decimal once.
  if (mant > kMaxExactDoubleInteger)
    return Fallback(start, end, out);
  f64 v = static_cast<f64>(mant);
  if (frac)
    v /= kPow10[frac];
  out = neg ? -v : v;
  return p;
}

inline const char* bounded_double(const char* start,
                                  const char* end,
                                  f64&        value) {
  const char* p = start;
  while (p < end && (*p == ' ' || *p == '\t'))
    ++p;
  if (p < end && *p == '+') {
    ++p;
    if (p < end && (*p == '+' || *p == '-'))
      return start;
  }
  const auto r = fast_float::from_chars(p, end, value);
  // .NET's numeric parser accepts underflow rounded to signed zero.
  return r.ec == std::errc() ||
                 (r.ec == std::errc::result_out_of_range && value == 0)
             ? r.ptr
             : start;
}

inline const char* parse_double(const char* p, const char* end, f64& out) {
  const char* q = parse_double_impl<bounded_double>(p, end, out);
  return q != p && std::isfinite(out) ? q : p;
}

inline bool is_numeric_space(char c) {
  return c == ' ' || (static_cast<unsigned char>(c) - 9u <= 4u);
}

inline const char* skip_numeric_space(const char* p, const char* end) {
  // The input is padded even at end. Digits and field separators take one
  // byte comparison; uncommon control/space bytes use the bounded loop.
  if (static_cast<unsigned char>(*p) > 32)
    return p;
  while (p < end && is_numeric_space(*p))
    ++p;
  return p;
}

// Numeric acceptance follows osu.Game Parsing, independently of gameplay
// clamping or our raw-field storage types. These run only outside digit-only
// fast paths whose field widths already prove the same limits.
inline const char* parse_osu_int(const char* p, const char* end, i64& out) {
  const char* first = skip_numeric_space(p, end);
  const char* q = parse_i64(first, end, out);
  if (q == first || out < -INT32_MAX || out > INT32_MAX) [[unlikely]]
    return p;
  return skip_numeric_space(q, end);
}

inline const char* parse_osu_double(const char* p,
                                    const char* end,
                                    f64&        out,
                                    f64         limit = INT32_MAX) {
  const char* first = skip_numeric_space(p, end);
  const char* q = parse_double_impl<bounded_double>(first, end, out);
  // One absolute-value bound also rejects infinities and NaN.
  if (q == first || !(std::abs(out) <= limit)) [[unlikely]]
    return p;
  return skip_numeric_space(q, end);
}

inline const char* parse_osu_float(const char* p,
                                   const char* end,
                                   f32&        out,
                                   f32         limit = f32(INT32_MAX)) {
  const char* number = skip_numeric_space(p, end);
  if (number < end && *number == '+') {
    ++number;
    if (number < end && (*number == '+' || *number == '-'))
      return p;
  }

  const bool  negative = number < end && *number == '-';
  const char* magnitude = number + negative;
  const u32   digits = digit_run8(magnitude);
  if (digits && digits <= static_cast<size_t>(end - magnitude) &&
      (digits < 8 || !is_digit(magnitude[8])) && magnitude[digits] != '.' &&
      magnitude[digits] != 'e' && magnitude[digits] != 'E') {
    const u64 integer = digits <= 4 ? swar_parse_u32(magnitude, digits)
                                    : swar_parse_u64(magnitude, digits);
    const f32 value = static_cast<f32>(integer);
    out = negative ? -value : value;
    if (!(std::abs(out) <= limit)) [[unlikely]]
      return p;
    return skip_numeric_space(magnitude + digits, end);
  }

  const auto r = fast_float::from_chars(number, end, out);
  if (r.ec != std::errc() &&
      !(r.ec == std::errc::result_out_of_range && out == 0)) [[unlikely]]
    return p;
  if (!(std::abs(out) <= limit)) [[unlikely]]
    return p;
  return skip_numeric_space(r.ptr, end);
}

}  // namespace fosu::internal
