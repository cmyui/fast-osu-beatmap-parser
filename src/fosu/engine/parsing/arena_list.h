#pragma once

#include <cstddef>
#include <cstring>
#include <new>
#include <span>
#include <type_traits>

#include <fosu/arena.h>
#include <fosu/result.h>

namespace fosu::internal {

inline constexpr size_t kArenaListChunkBytes = 16384;

template <typename T>
struct ArenaListChunk {
  static_assert(std::is_trivially_copyable_v<T>);
  static constexpr size_t capacity =
      sizeof(T) < kArenaListChunkBytes ? kArenaListChunkBytes / sizeof(T) : 1;

  ArenaListChunk* next = nullptr;
  size_t count = 0;
  T values[capacity];
};

template <typename T>
struct ArenaList {
  ArenaListChunk<T>* first = nullptr;
  ArenaListChunk<T>* last = nullptr;
  T* next = nullptr;
  T* end = nullptr;
  size_t count = 0;
};

template <typename T>
struct ArenaListPosition {
  ArenaListChunk<T>* last;
  T* next;
  size_t count;
};

template <typename T>
__attribute__((noinline)) inline T* arena_list_grow(Arena* arena, ArenaList<T>& list) {
  auto* chunk = arena_push_array<ArenaListChunk<T>>(arena, 1);
  if (!chunk)
    return nullptr;
  ::new (chunk) ArenaListChunk<T>;
  if (list.last) {
    list.last->count = static_cast<size_t>(list.next - list.last->values);
    list.last->next = chunk;
  } else {
    list.first = chunk;
  }
  list.last = chunk;
  list.next = chunk->values;
  list.end = chunk->values + ArenaListChunk<T>::capacity;
  return list.next;
}

template <typename T>
__attribute__((always_inline)) inline T* arena_list_push(Arena* arena,
                                                         ArenaList<T>& list,
                                                         const T& value) {
  if (list.next == list.end) [[unlikely]]
    if (!arena_list_grow(arena, list))
      return nullptr;
  T* destination = list.next++;
  ::new (destination) T(value);
  ++list.count;
  return destination;
}

template <typename T>
inline const T* arena_list_back(const ArenaList<T>& list) {
  return list.count ? list.next - 1 : nullptr;
}

template <typename T>
inline ArenaListPosition<T> arena_list_position(const ArenaList<T>& list) {
  return {
      .last = list.last,
      .next = list.next,
      .count = list.count,
  };
}

template <typename T>
inline void arena_list_pop_to(ArenaList<T>& list, ArenaListPosition<T> position) {
  if (!position.last) {
    list = {};
    return;
  }
  position.last->next = nullptr;
  list.last = position.last;
  list.next = position.next;
  list.end = position.last->values + ArenaListChunk<T>::capacity;
  list.count = position.count;
}

template <typename T>
inline Result<std::span<T>> flatten_arena_list(Arena* arena, const ArenaList<T>& list) {
  if (!list.count)
    return std::span<T>{};
  T* values = arena_push_array<T>(arena, list.count);
  if (!values)
    return Error{ErrorCode::AllocationFailure};

  T* destination = values;
  for (const auto* chunk = list.first; chunk; chunk = chunk->next) {
    const size_t count = chunk == list.last
                             ? static_cast<size_t>(list.next - chunk->values)
                             : chunk->count;
    std::memcpy(destination, chunk->values, count * sizeof(T));
    destination += count;
  }
  return std::span<T>{values, list.count};
}

}  // namespace fosu::internal
