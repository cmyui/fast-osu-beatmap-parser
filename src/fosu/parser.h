#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstring>

#include <fosu/beatmap.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/io.h>
#include <fosu/legacy_rules.h>
#include <fosu/slider_events.h>
#include <fosu/slider_timing.h>
#include <fosu/stacking.h>

namespace fosu {

namespace internal {

// Parser construction is common in convenience and Python APIs. Retain one
// inactive mapping without ever sharing a live arena between parser instances.
inline std::atomic<Arena*> parser_arena_pool{};

inline Arena* acquire_parser_arena() {
  Arena* arena = parser_arena_pool.exchange(nullptr, std::memory_order_acq_rel);
  if (!arena)
    return arena_alloc();
  arena_clear(arena);
  return arena;
}

inline void recycle_parser_arena(Arena* arena) {
  if (!arena)
    return;
  arena_clear(arena);
  Arena* empty = nullptr;
  if (!parser_arena_pool.compare_exchange_strong(empty, arena,
                                                 std::memory_order_acq_rel)) {
    arena_release(arena);
  }
}

inline void clear_parser_arena_pool() {
  arena_release(parser_arena_pool.exchange(nullptr, std::memory_order_acq_rel));
}

#ifndef FOSU_MANAGED_ARENA_CLEANUP
struct ParserArenaPoolCleanup {
  ~ParserArenaPoolCleanup() { clear_parser_arena_pool(); }
};
inline ParserArenaPoolCleanup parser_arena_pool_cleanup;
#endif

struct BeatmapArraySizes {
  size_t breaks = 0;
  size_t colours = 0;
  size_t timing_points = 0;
  size_t hit_objects = 0;
  size_t sliders = 0;
  size_t slider_points = 0;
};

// Derive safe upper bounds from the shortest accepted spelling of each
// record. Virtual arena space is cheap; only pages containing accepted records
// are touched. This avoids a sizing pass over the input.
inline BeatmapArraySizes maximum_beatmap_array_sizes(size_t size,
                                                     uint32_t selected_sections) {
  BeatmapArraySizes sizes;
  if (selected_sections & kSectionEvents)
    sizes.breaks = size / 5 + 1;  // 2,0,0
  if (selected_sections & kSectionColours)
    sizes.colours = size / 11 + 1;  // Combo:0,0,0
  if (selected_sections & kSectionTimingPoints)
    sizes.timing_points = size / 3 + 1;  // 0,0
  if (selected_sections & kSectionHitObjects) {
    sizes.hit_objects = size / 9 + 1;    // 0,0,0,1,0
    sizes.sliders = size / 14 + 1;       // 0,0,0,2,0,L,0
    sizes.slider_points = size / 4 + 1;  // |0:0
  }
  return sizes;
}

template <typename T>
constexpr size_t arena_array_bytes(size_t count) {
  return count ? count * sizeof(T) + alignof(T) - 1 : 0;
}

inline bool allocate_beatmap_arrays(Arena* arena,
                                    Beatmap& beatmap,
                                    const BeatmapArraySizes& sizes) noexcept {
  const size_t bytes = arena_array_bytes<Break>(sizes.breaks) +
                       arena_array_bytes<uint32_t>(sizes.colours) +
                       arena_array_bytes<TimingPoint>(sizes.timing_points) +
                       arena_array_bytes<HitObject>(sizes.hit_objects) +
                       arena_array_bytes<Slider>(sizes.sliders) +
                       arena_array_bytes<SliderPoint>(sizes.slider_points);
  if (!bytes)
    return true;

  auto* cursor =
      static_cast<uint8_t*>(arena_push(arena, bytes, alignof(std::max_align_t)));
  if (!cursor)
    return false;
  auto take = [&]<typename T>(size_t count) -> std::span<T> {
    if (!count)
      return {};
    cursor = reinterpret_cast<uint8_t*>(
        align_up(reinterpret_cast<uintptr_t>(cursor), alignof(T)));
    auto* values = reinterpret_cast<T*>(cursor);
    cursor += count * sizeof(T);
    return {values, count};
  };

  beatmap.breaks = take.template operator()<Break>(sizes.breaks);
  beatmap.combo_colours = take.template operator()<uint32_t>(sizes.colours);
  beatmap.timing_points = take.template operator()<TimingPoint>(sizes.timing_points);
  beatmap.hit_objects = take.template operator()<HitObject>(sizes.hit_objects);
  beatmap.sliders = take.template operator()<Slider>(sizes.sliders);
  beatmap.slider_points = take.template operator()<SliderPoint>(sizes.slider_points);
  return true;
}

}  // namespace internal

class Parser;

namespace internal {
struct ParserStorage {
  Arena* arena;
  const char* input;
  size_t input_size;
  size_t input_storage_size;
};

ParserStorage parser_storage(Parser& parser);
}  // namespace internal

class Parser {
 public:
  explicit Parser(const ParsingEngine& engine = internal::compiled_engine) noexcept
      : arena_(internal::acquire_parser_arena()), engine_(&engine) {}

  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;
  Parser(Parser&&) = delete;
  Parser& operator=(Parser&&) = delete;

  ~Parser() { internal::recycle_parser_arena(arena_); }

  Result<Beatmap*> parse(const char* data, size_t size, ParseOptions opts = {}) noexcept {
    reset_working_result();
    if ((!data && size) || invalid_sections(opts.sections))
      return Error{ErrorCode::InvalidInput};
    auto prepared = prepare_input(size, data);
    if (!prepared) {
      const Error error = prepared.error();
      reset_working_result();
      return error;
    }
    return finish_parse(opts);
  }

  Result<Beatmap*> parse(std::span<const char> input, ParseOptions opts = {}) noexcept {
    return parse(input.data(), input.size(), opts);
  }

  Result<Beatmap*> parse(const FileBuffer& input, ParseOptions opts = {}) noexcept {
    return parse(input.data.get(), input.size, opts);
  }

  Result<Beatmap*> parse_file(const char* path, ParseOptions opts = {}) noexcept {
    reset_working_result();
    if (!path || invalid_sections(opts.sections))
      return Error{ErrorCode::InvalidInput};

    const int file = open(path, O_RDONLY);
    if (file < 0)
      return Error{ErrorCode::IoFailure, kNoErrorOffset, errno};

    struct stat info;
    const int stat_result = fstat(file, &info);
    if (stat_result || info.st_size < 0) {
      const int error = stat_result ? errno : EIO;
      close(file);
      return Error{ErrorCode::IoFailure, kNoErrorOffset, error};
    }
    if (static_cast<uint64_t>(info.st_size) > kMaxInputSize) {
      close(file);
      return Error{ErrorCode::InputTooLarge};
    }

    const size_t size = static_cast<size_t>(info.st_size);
    auto prepared = prepare_input(size, nullptr);
    if (!prepared) {
      const Error error = prepared.error();
      close(file);
      reset_working_result();
      return error;
    }
    size_t bytes_read = 0;
    while (bytes_read < size) {
      const ssize_t count = read(file, input_ + bytes_read, size - bytes_read);
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0) {
        const int error = count < 0 ? errno : EIO;
        close(file);
        reset_working_result();
        return Error{ErrorCode::IoFailure, kNoErrorOffset, error};
      }
      bytes_read += static_cast<size_t>(count);
    }
    close(file);

    return finish_parse(opts);
  }

 private:
  friend internal::ParserStorage internal::parser_storage(Parser& parser);

  static constexpr uint32_t kValidSections = 0x1FEu;

  static bool invalid_sections(uint32_t sections) noexcept {
    return sections & ~kValidSections;
  }

  Result<char*> prepare_input(size_t size, const char* data) noexcept {
    if (size > kMaxInputSize)
      return Error{ErrorCode::InputTooLarge};
    if (!arena_)
      arena_ = internal::acquire_parser_arena();
    if (!arena_)
      return Error{ErrorCode::AllocationFailure};
    input_ = static_cast<char*>(arena_push(arena_, size + kBufferPadding, 1));
    if (!input_)
      return Error{ErrorCode::AllocationFailure};
    if (data && size)
      std::memmove(input_, data, size);
    std::memset(input_ + size, 0, kBufferPadding);
    input_size_ = size;
    return input_;
  }

  Result<Beatmap*> finish_parse(ParseOptions opts) noexcept {
    if (input_size_ != 0) {
      const auto sizes =
          internal::maximum_beatmap_array_sizes(input_size_, opts.sections);
      if (!internal::allocate_beatmap_arrays(arena_, beatmap_, sizes)) {
        reset_working_result();
        return Error{ErrorCode::AllocationFailure};
      }
    }
    engine_->parse_document({input_, input_size_}, beatmap_, opts);
    if (!internal::apply_legacy_rules(beatmap_, arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    const bool stacking = opts.apply_stacking && beatmap_.mode == 0;
    if ((opts.calculate_slider_paths || opts.calculate_slider_events || stacking) &&
        !internal::set_slider_paths(beatmap_, arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if (opts.calculate_slider_events && !internal::set_slider_events(beatmap_, arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if ((opts.calculate_slider_end_times || stacking) && !opts.calculate_slider_events &&
        !internal::set_slider_end_times(beatmap_, arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if (stacking && !internal::apply_stacking(beatmap_, arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    return &beatmap_;
  }

  void reset_working_result() noexcept {
    arena_clear(arena_);
    input_ = nullptr;
    input_size_ = 0;
    beatmap_ = {};
  }

  Arena* arena_;
  const ParsingEngine* engine_;
  char* input_ = nullptr;
  size_t input_size_ = 0;
  Beatmap beatmap_{};
};

namespace internal {
inline ParserStorage parser_storage(Parser& parser) {
  return {
      .arena = parser.arena_,
      .input = parser.input_,
      .input_size = parser.input_size_,
      .input_storage_size = parser.input_size_ + kBufferPadding,
  };
}
}  // namespace internal

}  // namespace fosu
