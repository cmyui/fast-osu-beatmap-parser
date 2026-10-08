#pragma once

#include <fosu/os.h>
#include <fosu/types.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>  // IWYU pragma: keep (placement new)
#include <type_traits>

namespace fosu {

// An arena is one virtual address reservation followed by a bump pointer.
// Pages are committed only as the position first reaches them, so the
// reservation costs address space, not memory. Pushes beyond it fail.
inline constexpr size_t kArenaReserve = size_t{1} << 30;
inline constexpr size_t kArenaCommitStep = size_t{64} << 10;

struct Arena {
  size_t reserved;
  size_t committed;
  size_t pos;
};

constexpr size_t align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

inline constexpr size_t kArenaHeaderSize = align_up(sizeof(Arena), 64);

inline Arena* arena_alloc(size_t reserve = kArenaReserve) {
  void* memory = internal::os_reserve(reserve);
  if (!memory)
    return nullptr;
  if (!internal::os_commit(memory, kArenaCommitStep)) {
    internal::os_release(memory, reserve);
    return nullptr;
  }
  return ::new (memory) Arena{
      .reserved = reserve,
      .committed = kArenaCommitStep,
      .pos = kArenaHeaderSize,
  };
}

inline void arena_release(Arena* arena) {
  if (arena)
    internal::os_release(arena, arena->reserved);
}

inline size_t arena_pos(const Arena* arena) {
  return arena ? arena->pos : 0;
}

// Returns nullptr when the arena is missing or the reservation is exhausted.
// alignment must be a power of two.
inline void* arena_push(Arena* arena, size_t size, size_t alignment) {
  if (!arena) [[unlikely]]
    return nullptr;
  const size_t pos = align_up(arena->pos, alignment);
  if (pos > arena->reserved || size > arena->reserved - pos) [[unlikely]]
    return nullptr;
  const size_t end = pos + size;
  if (end > arena->committed) [[unlikely]] {
    const size_t target =
        std::min(align_up(end, kArenaCommitStep), arena->reserved);
    auto* start = reinterpret_cast<u8*>(arena) + arena->committed;
    if (!internal::os_commit(start, target - arena->committed))
      return nullptr;
    arena->committed = target;
  }
  arena->pos = end;
  return reinterpret_cast<u8*>(arena) + pos;
}

template <typename T>
inline T* arena_push_array(Arena* arena, size_t count) {
  static_assert(std::is_trivially_copyable_v<T>,
                "arena arrays require trivially copyable elements");
  if (count > std::numeric_limits<size_t>::max() / sizeof(T)) [[unlikely]]
    return nullptr;
  return static_cast<T*>(arena_push(arena, sizeof(T) * count, alignof(T)));
}

inline void arena_pop_to(Arena* arena, size_t pos) {
  if (arena)
    arena->pos = std::clamp(pos, kArenaHeaderSize, arena->pos);
}

inline void arena_clear(Arena* arena) {
  arena_pop_to(arena, kArenaHeaderSize);
}

class TempArena {
 public:
  explicit TempArena(Arena* arena) noexcept
      : arena_(arena), pos_(arena_pos(arena)) {}

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
