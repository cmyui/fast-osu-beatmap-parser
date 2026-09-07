#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

#include "internal/os.hpp"

namespace fosu {

#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr size_t kCacheLineSize = 128;
#else
inline constexpr size_t kCacheLineSize = 64;
#endif

inline constexpr size_t kDefaultArenaReserve = size_t{1} << 30;
inline constexpr size_t kDefaultArenaCommit = size_t{64} << 10;
inline constexpr size_t kMaxArenaPush = size_t{1} << 46;

enum ArenaFlags : uint32_t {
    ArenaFlagLock = 1u << 0,
    ArenaFlagChain = 1u << 1,
};

struct Arena {
    Arena* prev;
    Arena* current;
    size_t base_pos;
    size_t reserve_size;
    size_t commit_size;
    size_t pos;
    size_t committed;
    uint32_t flags;
};

struct TempArena {
    Arena* arena;
    size_t pos;
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

inline constexpr size_t kArenaHeaderSize =
    align_up(sizeof(Arena), kCacheLineSize);

inline bool arena_commit_to(Arena* block, size_t new_pos) {
    if (new_pos <= block->committed) return true;

    const size_t target = std::min(
        round_up(new_pos, block->commit_size), block->reserve_size);
    const size_t amount = target - block->committed;
    auto* start = reinterpret_cast<uint8_t*>(block) + block->committed;
    if (!internal::os_commit(start, amount)) return false;
    if (block->flags & ArenaFlagLock) {
        if (!internal::os_lock(start, amount)) return false;
    }
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
    const size_t reserve_size = round_up(
        std::max(params.reserve_size, minimum), page_size);
    const size_t commit_size = std::min(
        round_up(std::max(params.commit_size, minimum), page_size),
        reserve_size);
    void* memory = internal::os_reserve(reserve_size);
    if (!memory || !internal::os_commit(memory, commit_size)) {
        if (memory) internal::os_release(memory, reserve_size);
        return nullptr;
    }

    auto* arena = ::new (memory) Arena{
        .prev = nullptr,
        .current = nullptr,
        .base_pos = 0,
        .reserve_size = reserve_size,
        .commit_size = commit_size,
        .pos = kArenaHeaderSize,
        .committed = commit_size,
        .flags = params.flags,
    };
    arena->current = arena;
    if (arena->flags & ArenaFlagLock) {
        if (!internal::os_lock(arena, commit_size)) {
            internal::os_release(arena, reserve_size);
            return nullptr;
        }
    }
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
    if (!arena) return 0;
    return arena->current->base_pos + arena->current->pos;
}

inline void* arena_push(Arena* arena, size_t size, size_t alignment) {
    if (!arena || size > kMaxArenaPush || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 ||
        size > std::numeric_limits<size_t>::max() - alignment) {
        return nullptr;
    }
    alignment = std::min(std::max(size_t{1}, alignment), size_t{4096});

    Arena* current = arena->current;
    size_t pos = align_up(current->pos, alignment);
    if (pos > current->reserve_size ||
        size > current->reserve_size - pos) {
        if (!(current->flags & ArenaFlagChain)) return nullptr;

        Arena* block = arena_alloc({
            .reserve_size = std::max(
                current->reserve_size,
                kArenaHeaderSize + alignment + size),
            .commit_size = current->commit_size,
            .flags = current->flags,
        });
        if (!block) return nullptr;
        block->prev = current;
        block->base_pos = current->base_pos + current->reserve_size;
        arena->current = current = block;
        pos = align_up(current->pos, alignment);
    }

    const size_t new_pos = pos + size;
    if (!arena_commit_to(current, new_pos)) return nullptr;
    void* result = reinterpret_cast<uint8_t*>(current) + pos;
    current->pos = new_pos;
    return result;
}

template <typename T>
inline T* arena_push_array(Arena* arena, size_t count) {
    if (count > std::numeric_limits<size_t>::max() / sizeof(T))
        return nullptr;
    return static_cast<T*>(
        arena_push(arena, sizeof(T) * count, alignof(T)));
}

inline void arena_release(Arena* arena) {
    if (!arena) return;
    for (Arena* block = arena->current; block;) {
        Arena* prev = block->prev;
        const size_t reserve_size = block->reserve_size;
        block->~Arena();
        internal::os_release(block, reserve_size);
        block = prev;
    }
}

inline void arena_pop_to(Arena* arena, size_t pos) {
    if (!arena) return;
    const size_t target = std::max(kArenaHeaderSize, pos);
    Arena* current = arena->current;
    while (current->prev && current->base_pos >= target) {
        Arena* prev = current->prev;
        const size_t reserve_size = current->reserve_size;
        current->~Arena();
        internal::os_release(current, reserve_size);
        current = prev;
    }
    arena->current = current;
    current->pos = std::clamp(
        target - current->base_pos, kArenaHeaderSize, current->pos);
}

inline void arena_clear(Arena* arena) {
    arena_pop_to(arena, kArenaHeaderSize);
}

inline TempArena temp_begin(Arena* arena) {
    return {arena, arena_pos(arena)};
}

inline void temp_end(TempArena temp) {
    arena_pop_to(temp.arena, temp.pos);
}

}  // namespace fosu
