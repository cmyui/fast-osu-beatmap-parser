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

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace fosu {

inline constexpr size_t kBufferPadding = 128;

namespace internal {

template <typename T>
inline bool push_span(Arena* arena, std::span<T>& out, size_t capacity) {
  T* values = arena_push_array<T>(arena, capacity);
  out = {values, values ? capacity : 0};
  return values;
}

inline bool allocate_beatmap_arrays(Arena* arena, Beatmap& beatmap, size_t n) {
  auto bound = [n](std::string_view shortest) {
    return n / shortest.size() + 1;
  };
  return push_span(arena, beatmap.breaks, bound("2,0,0")) &&
         push_span(arena, beatmap.combo_colours, bound("Combo1:0,0,0")) &&
         // At least three, for the default presets:
         push_span(arena, beatmap.velocity_presets, bound("0,") + 2) &&
         push_span(arena, beatmap.timing_points, bound("0,0")) &&
         push_span(arena, beatmap.hit_objects, bound("0,0,0,1,0")) &&
         push_span(arena, beatmap.sliders, bound("0,0,0,2,0,L,0")) &&
         push_span(arena, beatmap.slider_segments, bound("|L|0:0")) &&
         push_span(arena, beatmap.slider_points, bound("|0:0"));
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

  Beatmap* parse(std::string_view input, ParseOptions opts = {}) noexcept {
    reset();
    if (invalid_options(opts))
      return nullptr;
    char* buffer = reserve_input(input.size());
    if (!buffer)
      return nullptr;
    if (!input.empty())
      std::memcpy(buffer, input.data(), input.size());
    return parse_input({buffer, input.size()}, opts);
  }

  Beatmap* parse_file(const char* path, ParseOptions opts = {}) noexcept {
    reset();
    if (invalid_options(opts))
      return nullptr;
    const std::span<const char> input = load_file(path);
    if (!input.data())
      return nullptr;
    return parse_input(input, opts);
  }

 private:
  Beatmap* parse_input(std::span<const char> input,
                       ParseOptions          opts) noexcept {
    if (!input.empty() && !internal::allocate_beatmap_arrays(
                              result_arena_, beatmap_, input.size())) {
      reset();
      return nullptr;
    }
    engine_->parse_document(input, beatmap_, opts);
    if (!post_process(opts)) {
      reset();
      return nullptr;
    }
    return &beatmap_;
  }

  bool post_process(ParseOptions opts) noexcept {
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
      return false;
    if (!apply_mods_before_calculations(map, opts.mods))
      return false;
    if (stacking && !push_span(scratch, stacking_end_times, map.sliders.size()))
      return false;
    if (paths && !set_slider_paths(map, result, scratch))
      return false;
    if (events && !set_slider_events(map, result, scratch, stacking_end_times))
      return false;
    if (end_times &&
        !set_slider_end_times(map, scratch, {}, stacking_end_times))
      return false;
    if (stacking && !apply_stacking(map, result, stacking_end_times))
      return false;
    apply_clock_rate(map, opts.mods);
    return true;
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

  void reset() noexcept {
    arena_clear(result_arena_);
    arena_clear(scratch_arena_);
    beatmap_ = {};
  }

  Arena*               result_arena_;
  Arena*               scratch_arena_;
  const ParsingEngine* engine_;
  Beatmap              beatmap_{};
};

}  // namespace fosu
