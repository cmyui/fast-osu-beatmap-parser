#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>

#include <fosu/beatmap.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/io.h>
#include <fosu/legacy_rules.h>
#include <fosu/mods.h>
#include <fosu/slider_events.h>
#include <fosu/slider_timing.h>
#include <fosu/stacking.h>

namespace fosu {

namespace internal {

// Parser construction is common in convenience and Python APIs. Retain one
// inactive result/scratch pair without ever sharing live arenas between parsers.
struct ParserArenaPool {
  std::atomic<Arena*> result{};
  std::atomic<Arena*> scratch{};
};

inline ParserArenaPool parser_arena_pool{};

inline Arena* acquire_parser_arena(std::atomic<Arena*>& pool) {
  Arena* arena = pool.exchange(nullptr, std::memory_order_acquire);
  if (!arena)
    return arena_alloc();
  return arena;
}

inline void recycle_parser_arena(std::atomic<Arena*>& pool, Arena* arena) {
  if (!arena)
    return;
  arena_clear(arena);
  Arena* empty = nullptr;
  if (!pool.compare_exchange_strong(empty, arena, std::memory_order_release,
                                    std::memory_order_relaxed)) {
    arena_release(arena);
  }
}

inline void clear_parser_arena_pool() {
  arena_release(parser_arena_pool.result.exchange(nullptr, std::memory_order_acquire));
  arena_release(parser_arena_pool.scratch.exchange(nullptr, std::memory_order_acquire));
}

#ifndef FOSU_MANAGED_ARENA_CLEANUP
struct ParserArenaPoolCleanup {
  ~ParserArenaPoolCleanup() { clear_parser_arena_pool(); }
};
inline ParserArenaPoolCleanup parser_arena_pool_cleanup;
#endif

}  // namespace internal

class Parser;

namespace internal {
struct ParserStorage {
  Arena* result_arena;
  Arena* scratch_arena;
  const char* input;
  size_t input_size;
  size_t input_storage_size;
};

ParserStorage parser_storage(Parser& parser);
}  // namespace internal

class Parser {
 public:
  explicit Parser(const ParsingEngine& engine = internal::compiled_engine) noexcept
      : result_arena_(internal::acquire_parser_arena(internal::parser_arena_pool.result)),
        scratch_arena_(
            internal::acquire_parser_arena(internal::parser_arena_pool.scratch)),
        engine_(&engine) {}

  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;
  Parser(Parser&&) = delete;
  Parser& operator=(Parser&&) = delete;

  ~Parser() {
    internal::recycle_parser_arena(internal::parser_arena_pool.result, result_arena_);
    internal::recycle_parser_arena(internal::parser_arena_pool.scratch, scratch_arena_);
  }

  Result<Beatmap*> parse(const char* data, size_t size, ParseOptions opts = {}) noexcept {
    reset_working_result();
    if ((!data && size) || invalid_options(opts))
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
    if (!path || invalid_options(opts))
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
    if (static_cast<uint64_t>(info.st_size) >
        std::numeric_limits<size_t>::max() - kBufferPadding) {
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

  static bool invalid_options(ParseOptions opts) noexcept {
    if (invalid_sections(opts.sections) || internal::invalid_mods(opts.mods))
      return true;
    return internal::has_difficulty_mod(opts.mods) &&
           (opts.sections & (kSectionGeneral | kSectionDifficulty)) !=
               (kSectionGeneral | kSectionDifficulty);
  }

  Result<char*> prepare_input(size_t size, const char* data) noexcept {
    if (!can_pad_input(size))
      return Error{ErrorCode::InputTooLarge};
    if (!result_arena_)
      result_arena_ = internal::acquire_parser_arena(internal::parser_arena_pool.result);
    if (!scratch_arena_)
      scratch_arena_ =
          internal::acquire_parser_arena(internal::parser_arena_pool.scratch);
    if (!result_arena_ || !scratch_arena_)
      return Error{ErrorCode::AllocationFailure};
    input_ = static_cast<char*>(arena_push(result_arena_, size + kBufferPadding, 1));
    if (!input_)
      return Error{ErrorCode::AllocationFailure};
    if (data && size)
      std::memmove(input_, data, size);
    std::memset(input_ + size, 0, kBufferPadding);
    input_size_ = size;
    return input_;
  }

  Result<Beatmap*> finish_parse(ParseOptions opts) noexcept {
    if (!engine_->parse_document({input_, input_size_}, beatmap_, result_arena_,
                                 scratch_arena_, opts)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if (!internal::apply_legacy_rules(beatmap_, scratch_arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if (!internal::apply_mods_before_calculations(beatmap_, opts.mods)) {
      reset_working_result();
      return Error{ErrorCode::InvalidInput};
    }
    const bool stacking = opts.apply_stacking && beatmap_.mode == 0;
    if ((opts.calculate_slider_paths || opts.calculate_slider_events || stacking) &&
        !internal::set_slider_paths(beatmap_, result_arena_, scratch_arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if (opts.calculate_slider_events &&
        !internal::set_slider_events(beatmap_, result_arena_, scratch_arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if ((opts.calculate_slider_end_times || stacking) && !opts.calculate_slider_events &&
        !internal::set_slider_end_times(beatmap_, scratch_arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    if (stacking && !internal::apply_stacking(beatmap_, result_arena_)) {
      reset_working_result();
      return Error{ErrorCode::AllocationFailure};
    }
    internal::apply_clock_rate(beatmap_, opts.mods);
    return &beatmap_;
  }

  void reset_working_result() noexcept {
    arena_clear(result_arena_);
    arena_clear(scratch_arena_);
    input_ = nullptr;
    input_size_ = 0;
    beatmap_ = {};
  }

  Arena* result_arena_;
  Arena* scratch_arena_;
  const ParsingEngine* engine_;
  char* input_ = nullptr;
  size_t input_size_ = 0;
  Beatmap beatmap_{};
};

namespace internal {
inline ParserStorage parser_storage(Parser& parser) {
  return {
      .result_arena = parser.result_arena_,
      .scratch_arena = parser.scratch_arena_,
      .input = parser.input_,
      .input_size = parser.input_size_,
      .input_storage_size = parser.input_size_ + kBufferPadding,
  };
}
}  // namespace internal

}  // namespace fosu
