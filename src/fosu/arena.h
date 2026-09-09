#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

#include <fosu/os.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FOSU_ADDRESS_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define FOSU_ADDRESS_SANITIZER 1
#endif
#if defined(FOSU_ADDRESS_SANITIZER)
#include <sanitizer/asan_interface.h>
#endif

namespace fosu {

#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr size_t kCacheLineSize = 128;
#else
inline constexpr size_t kCacheLineSize = 64;
#endif

inline constexpr size_t kDefaultArenaReserve = size_t{64} << 20;
inline constexpr size_t kDefaultArenaCommit = size_t{64} << 10;
inline constexpr size_t kMaxArenaPush = size_t{1} << 46;
inline constexpr size_t kMaxArenaAlignment = size_t{1} << 12;

enum ArenaFlags : uint32_t {
  ArenaFlagChain = 1u << 0,
};

#if defined(FOSU_ARENA_TELEMETRY)
struct ArenaMetrics {
  size_t current_used_bytes;
  size_t peak_used_bytes;
  size_t current_committed_bytes;
  size_t peak_committed_bytes;
  size_t commit_calls;
  size_t chained_blocks;
};
#endif

struct Arena {
  Arena* prev;
  Arena* current;
  size_t base_pos;
  size_t reserve_size;
  size_t commit_size;
  size_t pos;
  size_t committed;
  uint32_t flags;
#if defined(FOSU_ARENA_TELEMETRY)
  ArenaMetrics metrics;
#endif
};

struct ArenaParams {
  size_t reserve_size;
  size_t commit_size;
  uint32_t flags;
};

constexpr size_t align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

constexpr size_t round_up(size_t value, size_t multiple) {
  return ((value + multiple - 1) / multiple) * multiple;
}

inline constexpr size_t kArenaHeaderSize = align_up(sizeof(Arena), kCacheLineSize);

namespace internal {

inline void arena_poison(void* memory, size_t size) {
#if defined(FOSU_ADDRESS_SANITIZER)
  ASAN_POISON_MEMORY_REGION(memory, size);
#else
  (void)memory;
  (void)size;
#endif
}

inline void arena_unpoison(void* memory, size_t size) {
#if defined(FOSU_ADDRESS_SANITIZER)
  ASAN_UNPOISON_MEMORY_REGION(memory, size);
#else
  (void)memory;
  (void)size;
#endif
}

}  // namespace internal

inline bool arena_commit_to(Arena* block, size_t new_pos) {
  if (new_pos <= block->committed)
    return true;

  const size_t target =
      std::min(round_up(new_pos, block->commit_size), block->reserve_size);
  const size_t amount = target - block->committed;
  auto* start = reinterpret_cast<uint8_t*>(block) + block->committed;
  if (!internal::os_commit(start, amount))
    return false;
  internal::arena_poison(start, amount);
  block->committed = target;
  return true;
}

inline Arena* arena_alloc(ArenaParams params) {
  const size_t page_size = internal::os_page_size();
  if (page_size == 0 ||
      params.reserve_size > std::numeric_limits<size_t>::max() - page_size ||
      params.commit_size > std::numeric_limits<size_t>::max() - page_size) {
    return nullptr;
  }
  const size_t minimum = round_up(kArenaHeaderSize, page_size);
  const size_t reserve_size = round_up(std::max(params.reserve_size, minimum), page_size);
  const size_t commit_size =
      std::min(round_up(std::max(params.commit_size, minimum), page_size), reserve_size);
  void* memory = internal::os_reserve(reserve_size);
  if (!memory || !internal::os_commit(memory, commit_size)) {
    if (memory)
      internal::os_release(memory, reserve_size);
    return nullptr;
  }

  internal::arena_poison(memory, commit_size);
  internal::arena_unpoison(memory, kArenaHeaderSize);
  auto* arena = ::new (memory) Arena{
      .prev = nullptr,
      .current = nullptr,
      .base_pos = 0,
      .reserve_size = reserve_size,
      .commit_size = commit_size,
      .pos = kArenaHeaderSize,
      .committed = commit_size,
      .flags = params.flags,
#if defined(FOSU_ARENA_TELEMETRY)
      .metrics =
          {
              .current_used_bytes = 0,
              .peak_used_bytes = 0,
              .current_committed_bytes = commit_size,
              .peak_committed_bytes = commit_size,
              .commit_calls = 1,
              .chained_blocks = 0,
          },
#endif
  };
  arena->current = arena;
  return arena;
}

inline Arena* arena_alloc() {
  return arena_alloc({
      .reserve_size = kDefaultArenaReserve,
      .commit_size = kDefaultArenaCommit,
      .flags = ArenaFlagChain,
  });
}

inline size_t arena_pos(const Arena* arena) {
  if (!arena)
    return 0;
  return arena->current->base_pos + arena->current->pos;
}

#if defined(FOSU_ARENA_TELEMETRY)
inline ArenaMetrics arena_metrics(const Arena* arena) {
  return arena ? arena->metrics : ArenaMetrics{};
}

inline void arena_reset_metrics(Arena* arena) {
  if (!arena)
    return;
  size_t used = 0;
  size_t committed = 0;
  for (const Arena* block = arena->current; block; block = block->prev) {
    used += block->pos - kArenaHeaderSize;
    committed += block->committed;
  }
  arena->metrics = {
      .current_used_bytes = used,
      .peak_used_bytes = used,
      .current_committed_bytes = committed,
      .peak_committed_bytes = committed,
      .commit_calls = 0,
      .chained_blocks = 0,
  };
}
#endif

inline void* arena_push(Arena* arena, size_t size, size_t alignment) {
  if (!arena || size > kMaxArenaPush || alignment == 0 ||
      alignment > kMaxArenaAlignment || (alignment & (alignment - 1)) != 0 ||
      size > std::numeric_limits<size_t>::max() - alignment) {
    return nullptr;
  }

  Arena* current = arena->current;
  size_t pos = align_up(current->pos, alignment);
  if (pos > current->reserve_size || size > current->reserve_size - pos) {
    if (!(current->flags & ArenaFlagChain))
      return nullptr;

    Arena* block = arena_alloc({
        .reserve_size =
            std::max(current->reserve_size, kArenaHeaderSize + alignment + size),
        .commit_size = current->commit_size,
        .flags = current->flags,
    });
    if (!block)
      return nullptr;
    block->prev = current;
    block->base_pos = current->base_pos + current->reserve_size;
    arena->current = current = block;
    pos = align_up(current->pos, alignment);
#if defined(FOSU_ARENA_TELEMETRY)
    arena->metrics.current_committed_bytes += current->committed;
    arena->metrics.peak_committed_bytes = std::max(
        arena->metrics.peak_committed_bytes, arena->metrics.current_committed_bytes);
    ++arena->metrics.commit_calls;
    ++arena->metrics.chained_blocks;
#endif
  }

  const size_t new_pos = pos + size;
#if defined(FOSU_ARENA_TELEMETRY)
  const size_t committed_before = current->committed;
#endif
  if (!arena_commit_to(current, new_pos))
    return nullptr;
#if defined(FOSU_ARENA_TELEMETRY)
  if (current->committed != committed_before) {
    arena->metrics.current_committed_bytes += current->committed - committed_before;
    arena->metrics.peak_committed_bytes = std::max(
        arena->metrics.peak_committed_bytes, arena->metrics.current_committed_bytes);
    ++arena->metrics.commit_calls;
  }
  arena->metrics.current_used_bytes += new_pos - current->pos;
  arena->metrics.peak_used_bytes =
      std::max(arena->metrics.peak_used_bytes, arena->metrics.current_used_bytes);
#endif
  void* result = reinterpret_cast<uint8_t*>(current) + pos;
  internal::arena_unpoison(result, size);
  current->pos = new_pos;
  return result;
}

template <typename T>
inline T* arena_push_array(Arena* arena, size_t count) {
  static_assert(std::is_trivially_copyable_v<T>,
                "arena arrays require trivially copyable elements");
  if (count > std::numeric_limits<size_t>::max() / sizeof(T))
    return nullptr;
  return static_cast<T*>(arena_push(arena, sizeof(T) * count, alignof(T)));
}

inline void arena_release(Arena* arena) {
  if (!arena)
    return;
  for (Arena* block = arena->current; block;) {
    Arena* prev = block->prev;
    const size_t reserve_size = block->reserve_size;
    internal::arena_unpoison(block, block->committed);
    block->~Arena();
    internal::os_release(block, reserve_size);
    block = prev;
  }
}

inline void arena_pop_to(Arena* arena, size_t pos) {
  if (!arena)
    return;
  const size_t target = std::max(kArenaHeaderSize, pos);
  Arena* current = arena->current;
  while (current->prev && current->base_pos >= target) {
    Arena* prev = current->prev;
    const size_t reserve_size = current->reserve_size;
#if defined(FOSU_ARENA_TELEMETRY)
    arena->metrics.current_used_bytes -= current->pos - kArenaHeaderSize;
    arena->metrics.current_committed_bytes -= current->committed;
#endif
    internal::arena_unpoison(current, current->committed);
    current->~Arena();
    internal::os_release(current, reserve_size);
    current = prev;
  }
  arena->current = current;
  const size_t new_pos =
      std::clamp(target - current->base_pos, kArenaHeaderSize, current->pos);
#if defined(FOSU_ARENA_TELEMETRY)
  arena->metrics.current_used_bytes -= current->pos - new_pos;
#endif
  internal::arena_poison(reinterpret_cast<uint8_t*>(current) + new_pos,
                         current->pos - new_pos);
  current->pos = new_pos;
}

inline void arena_clear(Arena* arena) {
  arena_pop_to(arena, kArenaHeaderSize);
}

class TempArena {
 public:
  explicit TempArena(Arena* arena) noexcept : arena_(arena), pos_(arena_pos(arena)) {}

  TempArena(const TempArena&) = delete;
  TempArena& operator=(const TempArena&) = delete;
  TempArena(TempArena&&) = delete;
  TempArena& operator=(TempArena&&) = delete;

  ~TempArena() { arena_pop_to(arena_, pos_); }

  size_t position() const noexcept { return pos_; }

 private:
  Arena* arena_;
  size_t pos_;
};

}  // namespace fosu

#undef FOSU_ADDRESS_SANITIZER
