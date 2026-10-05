#pragma once
#include <fosu/arena.h>
#include <fosu/beatmap.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/io.h>
#include <fosu/legacy_rules.h>
#include <fosu/mods.h>
#include <fosu/parse_options.h>
#include <fosu/slider_events.h>
#include <fosu/slider_geometry.h>
#include <fosu/slider_timing.h>
#include <fosu/stacking.h>
#include <fosu/types.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#elif defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace fosu {

inline constexpr size_t kBufferPadding = 128;

namespace internal {

template <typename T>
inline bool push_span(Arena* arena, std::span<T>& out, size_t capacity) {
  T* values = arena_push_array<T>(arena, capacity);
  out = {values, values ? capacity : 0};
  return values;
}

// Lazer accepts any number of velocity presets; beyond this many, the line
// is malformed.
inline constexpr size_t kMaxVelocityPresets = 64;

struct ByteCounts {
  size_t lines = 1;  // line-ending bytes + 1
  size_t pipes = 0;
};

// Counts the bytes that bound the per-line and per-point arrays, copying the
// input to `copy` in the same pass when it is non-null. '\n', '\v', '\f' and
// '\r' all count as line endings: a safe over-count with one range compare.
//
// SSE2 and NEON are baseline on x86-64 and arm64, so this needs no dispatch.
// Each 64-byte step subtracts the sum of four 0/-1 match masks from byte
// counters, which are widened every 63 steps, before they can overflow.
inline ByteCounts count_bytes(const char* p, size_t n, char* copy) {
  ByteCounts counts;
#if defined(__aarch64__) || defined(_M_ARM64)
  const uint8x16_t newline = vdupq_n_u8('\n'), span = vdupq_n_u8('\r' - '\n'),
                   pipe = vdupq_n_u8('|');
  while (n >= 64) {
    uint8x16_t lines = vdupq_n_u8(0), pipes = lines;
    for (size_t steps = std::min<size_t>(n / 64, 63); steps;
         --steps, p += 64, n -= 64) {
      const uint8x16x4_t v = vld1q_u8_x4(reinterpret_cast<const u8*>(p));
      if (copy) {
        vst1q_u8_x4(reinterpret_cast<u8*>(copy), v);
        copy += 64;
      }
      uint8x16_t l[4], q[4];
      for (int i = 0; i < 4; ++i) {
        l[i] = vcleq_u8(vsubq_u8(v.val[i], newline), span);
        q[i] = vceqq_u8(v.val[i], pipe);
      }
      lines =
          vsubq_u8(lines, vaddq_u8(vaddq_u8(l[0], l[1]), vaddq_u8(l[2], l[3])));
      pipes =
          vsubq_u8(pipes, vaddq_u8(vaddq_u8(q[0], q[1]), vaddq_u8(q[2], q[3])));
    }
    counts.lines += vaddlvq_u8(lines);
    counts.pipes += vaddlvq_u8(pipes);
  }
#elif defined(__x86_64__) || defined(_M_X64)
  // Biased, '\n'..'\r' become the four most negative signed bytes.
  const __m128i bias = _mm_set1_epi8(0x80 - '\n'),
                below = _mm_set1_epi8(-0x80 + ('\r' - '\n') + 1),
                pipe = _mm_set1_epi8('|'), zero = _mm_setzero_si128();
  while (n >= 64) {
    __m128i lines = zero, pipes = zero;
    for (size_t steps = std::min<size_t>(n / 64, 63); steps;
         --steps, p += 64, n -= 64) {
      __m128i l[4], q[4];
      for (int i = 0; i < 4; ++i) {
        const __m128i v =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(p) + i);
        if (copy)
          _mm_storeu_si128(reinterpret_cast<__m128i*>(copy) + i, v);
        l[i] = _mm_cmpgt_epi8(below, _mm_add_epi8(v, bias));
        q[i] = _mm_cmpeq_epi8(v, pipe);
      }
      if (copy)
        copy += 64;
      lines = _mm_sub_epi8(lines, _mm_add_epi8(_mm_add_epi8(l[0], l[1]),
                                               _mm_add_epi8(l[2], l[3])));
      pipes = _mm_sub_epi8(pipes, _mm_add_epi8(_mm_add_epi8(q[0], q[1]),
                                               _mm_add_epi8(q[2], q[3])));
    }
    const __m128i l = _mm_sad_epu8(lines, zero), q = _mm_sad_epu8(pipes, zero);
    counts.lines += size_t(_mm_cvtsi128_si64(l) + _mm_extract_epi16(l, 4));
    counts.pipes += size_t(_mm_cvtsi128_si64(q) + _mm_extract_epi16(q, 4));
  }
#endif
  if (copy && n)
    std::memcpy(copy, p, n);
  for (; n; ++p, --n) {
    counts.lines += u8(*p - '\n') <= '\r' - '\n';
    counts.pipes += *p == '|';
  }
  return counts;
}

// A record is at least one line, so the line count bounds the per-line
// arrays. Slider points and segments each start with '|'.
inline bool prealloc_beatmap_arrays(Arena*     arena,
                                    Beatmap&   beatmap,
                                    ByteCounts counts) {
  const auto [lines, pipes] = counts;
  return push_span(arena, beatmap.breaks, lines) &&
         push_span(arena, beatmap.combo_colours, lines) &&
         push_span(arena, beatmap.velocity_presets, kMaxVelocityPresets) &&
         push_span(arena, beatmap.timing_points, lines) &&
         push_span(arena, beatmap.hit_objects, lines) &&
         push_span(arena, beatmap.sliders, lines) &&
         push_span(arena, beatmap.slider_segments, pipes) &&
         push_span(arena, beatmap.slider_points, pipes);
}

}  // namespace internal

class Parser {
 public:
  explicit Parser(
      const ParsingEngine& engine = internal::compiled_engine) noexcept
      : result_arena_(arena_alloc()),
        scratch_arena_(arena_alloc()),
        engine_(&engine) {}

  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;
  Parser(Parser&&) = delete;
  Parser& operator=(Parser&&) = delete;

  ~Parser() {
    arena_release(result_arena_);
    arena_release(scratch_arena_);
  }

  // Returns null on failure; error() then says why.
  Beatmap* parse(std::string_view input, ParseOptions opts = {}) noexcept {
    reset_working_state();
    if (invalid_options(opts))
      return fail(ParseErrorCode::InvalidOptions);
    char* buffer = reserve_input(input.size());
    if (!buffer)
      return fail(ParseErrorCode::OutOfMemory);
    const auto counts =
        internal::count_bytes(input.data(), input.size(), buffer);
    return parse_input({buffer, input.size()}, counts, opts);
  }

  Beatmap* parse_file(const char* path, ParseOptions opts = {}) noexcept {
    reset_working_state();
    if (invalid_options(opts))
      return fail(ParseErrorCode::InvalidOptions);
    const std::span<const char> input = load_file(path);
    if (!input.data())
      return fail(errno ? ParseErrorCode::ReadFailed
                        : ParseErrorCode::OutOfMemory);
    const auto counts =
        internal::count_bytes(input.data(), input.size(), nullptr);
    return parse_input(input, counts, opts);
  }

  // Why the last parse failed; code is None after a success.
  ParseError error() const noexcept { return error_; }

 private:
  Beatmap* fail(ParseErrorCode code, u32 line = 0) noexcept {
    reset_working_state();
    error_ = {code, line};
    return nullptr;
  }

  Beatmap* parse_input(std::span<const char> input,
                       internal::ByteCounts  counts,
                       ParseOptions          opts) noexcept {
    if (!input.empty() &&
        !internal::prealloc_beatmap_arrays(result_arena_, beatmap_, counts))
      return fail(ParseErrorCode::OutOfMemory);
    if (const ParseError error = engine_->parse_document(input, beatmap_, opts);
        error.code != ParseErrorCode::None)
      return fail(error.code, error.line);
    if (const ParseErrorCode code = post_process(opts);
        code != ParseErrorCode::None)
      return fail(code);
    return &beatmap_;
  }

  // Calculated values follow lazer for both clients; stable's own end times,
  // paths, events and stacking are not modelled yet.
  ParseErrorCode post_process(ParseOptions opts) noexcept {
    const bool stacking = opts.apply_stacking && beatmap_.mode == 0;
    const bool events = opts.calculate_slider_events;
    const bool paths = opts.calculate_slider_paths || events || stacking;

    const bool end_times =
        (opts.calculate_slider_end_times || stacking) && !events;

    std::span<f64> stacking_end_times;

    auto&          map = beatmap_;
    auto*          result = result_arena_;
    auto*          scratch = scratch_arena_;

    using namespace internal;
    if (!apply_legacy_rules(map, scratch))
      return ParseErrorCode::OutOfMemory;
    // Difficulty mods catch and mania do not implement.
    if (!apply_mods_before_calculations(map, opts.mods))
      return ParseErrorCode::InvalidOptions;
    if (stacking && !push_span(scratch, stacking_end_times, map.sliders.size()))
      return ParseErrorCode::OutOfMemory;
    if (paths && !set_slider_paths(map, opts.client, result, scratch))
      return ParseErrorCode::OutOfMemory;
    if (events && !set_slider_events(map, opts.client, result, scratch,
                                     stacking_end_times))
      return ParseErrorCode::OutOfMemory;
    if (end_times && !set_slider_end_times(map, opts.client, scratch, {},
                                           stacking_end_times))
      return ParseErrorCode::OutOfMemory;
    if (stacking && !apply_stacking(map, result, stacking_end_times))
      return ParseErrorCode::OutOfMemory;
    apply_clock_rate(map, opts.mods);
    return ParseErrorCode::None;
  }

  // Reads the whole file into the result arena. Returns a span with a null
  // data() on failure, leaving errno set.
  std::span<const char> load_file(const char* path) noexcept {
    const internal::InputFile file = internal::open_input_file(path);
    if (file == internal::kInvalidInputFile)
      return {};
    u64   size = 0;
    char* buffer = nullptr;
    if (internal::input_file_size(file, size))
      buffer = reserve_input(size);
    for (u64 done = 0; buffer && done < size;) {
      const ptrdiff_t count =
          internal::read_input_file(file, buffer + done, size - done);
      if (count < 0 && errno == EINTR)
        continue;
      if (count == 0)
        errno = EIO;  // The file shrank after its size was read.
      if (count <= 0)
        buffer = nullptr;
      else
        done += static_cast<u64>(count);
    }
    const int error = errno;
    internal::close_input_file(file);
    errno = error;
    if (!buffer)
      return {};
    return {buffer, static_cast<size_t>(size)};
  }

  static bool invalid_options(ParseOptions opts) noexcept {
    return (opts.sections & ~kAllSections) ||
           internal::invalid_mods(opts.mods) ||
           (internal::has_difficulty_mod(opts.mods) &&
            (opts.sections & (kSectionGeneral | kSectionDifficulty)) !=
                (kSectionGeneral | kSectionDifficulty));
  }

  char* reserve_input(u64 size) noexcept {
    if (size > std::numeric_limits<size_t>::max() - kBufferPadding) {
      errno = EFBIG;
      return nullptr;
    }
    auto* buffer = static_cast<char*>(arena_push(
        result_arena_, static_cast<size_t>(size) + kBufferPadding, 1));
    if (buffer)
      std::memset(buffer + size, 0, kBufferPadding);
    return buffer;
  }

  void reset_working_state() noexcept {
    arena_clear(result_arena_);
    arena_clear(scratch_arena_);
    beatmap_ = {};
    error_ = {};
  }

  Arena*               result_arena_;
  Arena*               scratch_arena_;
  const ParsingEngine* engine_;
  Beatmap              beatmap_{};
  ParseError           error_{};
};

}  // namespace fosu
