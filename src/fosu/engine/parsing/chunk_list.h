#pragma once

#include <cstddef>
#include <cstring>
#include <new>
#include <span>
#include <type_traits>

#include <fosu/arena.h>
#include <fosu/result.h>

namespace fosu::internal {

inline constexpr size_t kChunkTargetBytes = 16384;

template <typename T>
struct ChunkNode {
  static_assert(std::is_trivially_copyable_v<T>);
  static constexpr size_t capacity =
      sizeof(T) < kChunkTargetBytes ? kChunkTargetBytes / sizeof(T) : 1;

  ChunkNode* next = nullptr;
  size_t count = 0;
  T values[capacity];
};

template <typename T>
struct ChunkList {
  ChunkNode<T>* first = nullptr;
  ChunkNode<T>* last = nullptr;
  size_t count = 0;
};

template <typename T>
struct ChunkListPosition {
  ChunkNode<T>* last;
  size_t last_count;
  size_t count;
};

template <typename T>
inline T* chunk_list_push(Arena* arena, ChunkList<T>& list, const T& value) {
  auto* chunk = list.last;

  if (!chunk || chunk->count == chunk->capacity) {
    chunk = arena_push_array<ChunkNode<T>>(arena, 1);
    if (!chunk)
      return nullptr;
    // Begin the node's C++ lifetime without zeroing its value storage.
    ::new (chunk) ChunkNode<T>;

    if (list.last)
      list.last->next = chunk;
    else
      list.first = chunk;

    list.last = chunk;
  }

  T* result = &chunk->values[chunk->count];
  *result = value;

  ++chunk->count;
  ++list.count;
  return result;
}

template <typename T>
inline const T* chunk_list_back(const ChunkList<T>& list) {
  if (!list.last)
    return nullptr;
  return &list.last->values[list.last->count - 1];
}

template <typename T>
inline ChunkListPosition<T> chunk_list_position(const ChunkList<T>& list) {
  return {
      .last = list.last,
      .last_count = list.last ? list.last->count : 0,
      .count = list.count,
  };
}

template <typename T>
inline void chunk_list_pop_to(ChunkList<T>& list, ChunkListPosition<T> position) {
  if (!position.last) {
    list = {};
    return;
  }

  position.last->next = nullptr;
  position.last->count = position.last_count;
  list.last = position.last;
  list.count = position.count;
}

template <typename T>
inline Result<std::span<T>> flatten_chunk_list(Arena* arena, const ChunkList<T>& list) {
  if (!list.count)
    return std::span<T>{};

  T* values = arena_push_array<T>(arena, list.count);
  if (!values)
    return Error{ErrorCode::AllocationFailure};

  T* destination = values;
  for (const auto* chunk = list.first; chunk; chunk = chunk->next) {
    std::memcpy(destination, chunk->values, chunk->count * sizeof(T));
    destination += chunk->count;
  }

  return std::span<T>{values, list.count};
}

}  // namespace fosu::internal
