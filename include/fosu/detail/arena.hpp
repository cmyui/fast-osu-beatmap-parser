#pragma once
// Arena-backed record storage for the C ABI handle. One anonymous mapping
// holds the padded input copy and every record array, so a parse performs a
// single allocation (none on a reused handle or a recycled arena), and a
// fresh process can be backed by larger folios where the host enables them
// (Linux MADV_HUGEPAGE; the advice is optional and never changes host
// settings). Arrays that outgrow the arena fall back to malloc.
//
// This is process-local memory recycling, not caching of parsed results: no
// bytes of a previous input or result are ever reused.
#include <sys/mman.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace fosu::detail {

struct Arena {
    char* base = nullptr;
    size_t size = 0;
    size_t used = 0;

    static constexpr size_t kAlign = 64;

    // Returns nullptr when the mapping cannot be created.
    static Arena* create(size_t bytes) {
#ifdef FOSU_ARENA_MALLOC  // experiment: heap-backed arena
        void* p = malloc(bytes);
        if (!p) return nullptr;
#else
        void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) return nullptr;
#if defined(MADV_HUGEPAGE) && !defined(FOSU_ARENA_NO_HUGEPAGE)
        madvise(p, bytes, MADV_HUGEPAGE);  // advisory; failure is harmless
#endif
#endif
        auto* a = static_cast<Arena*>(malloc(sizeof(Arena)));
        if (!a) {
#ifdef FOSU_ARENA_MALLOC
            free(p);
#else
            munmap(p, bytes);
#endif
            return nullptr;
        }
        a->base = static_cast<char*>(p);
        a->size = bytes;
        a->used = 0;
        return a;
    }
    static void destroy(Arena* a) {
        if (!a) return;
#ifdef FOSU_ARENA_MALLOC
        free(a->base);
#else
        munmap(a->base, a->size);
#endif
        free(a);
    }
    void* bump(size_t bytes) {
        const size_t start = (used + kAlign - 1) & ~(kAlign - 1);
        if (start > size || bytes > size - start) return nullptr;
        used = start + bytes;
        return base + start;
    }
    bool owns(const void* p) const {
        const auto* c = static_cast<const char*>(p);
        return c >= base && c < base + size;
    }
};

// A minimal vector over arena memory. Growth bumps a new region (the old one
// is reclaimed with the arena) or falls back to malloc when the arena is
// exhausted. Elements are trivially copyable records; resize leaves new
// elements uninitialized, matching how the parser writes every field.
template <typename T>
class ArenaVector {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    using value_type = T;
    ArenaVector() = default;
    ArenaVector(ArenaVector&& o) noexcept { take(o); }
    ArenaVector& operator=(ArenaVector&& o) noexcept {
        if (this != &o) {
            release();
            take(o);
        }
        return *this;
    }
    ArenaVector(const ArenaVector&) = delete;
    ArenaVector& operator=(const ArenaVector&) = delete;
    ~ArenaVector() { release(); }

    void set_arena(Arena* a) { arena_ = a; }
    size_t size() const { return static_cast<size_t>(e_ - b_); }
    size_t capacity() const { return static_cast<size_t>(c_ - b_); }
    bool empty() const { return e_ == b_; }
    T* data() { return b_; }
    const T* data() const { return b_; }
    T* begin() { return b_; }
    T* end() { return e_; }
    const T* begin() const { return b_; }
    const T* end() const { return e_; }
    T& operator[](size_t i) { return b_[i]; }
    const T& operator[](size_t i) const { return b_[i]; }
    T& back() { return e_[-1]; }

    void reserve(size_t n) {
        if (n <= capacity()) return;
        bool on_heap = false;
        T* nb = allocate(n, on_heap);
        const size_t sz = size();
        if (sz) memcpy(static_cast<void*>(nb), b_, sz * sizeof(T));
        release();
        b_ = nb;
        e_ = nb + sz;
        c_ = nb + n;
        heap_ = on_heap;
    }
    void resize(size_t n) {
        if (n > capacity()) reserve(n);
        e_ = b_ + n;
    }
    void set_size(size_t n) { e_ = b_ + n; }
    template <typename... A>
    T& emplace_back(A&&... a) {
        if (e_ == c_) reserve(capacity() ? capacity() * 2 : 16);
        T* p = e_++;
        ::new (static_cast<void*>(p)) T(std::forward<A>(a)...);
        return *p;
    }
    void push_back(const T& v) { emplace_back(v); }
    void pop_back() { --e_; }
    void clear() { e_ = b_; }
    // Drops the storage (arena memory is reclaimed by the arena's owner).
    void release() {
        if (heap_) free(b_);
        b_ = e_ = c_ = nullptr;
        heap_ = false;
    }

private:
    void take(ArenaVector& o) {
        arena_ = o.arena_;
        b_ = o.b_;
        e_ = o.e_;
        c_ = o.c_;
        heap_ = o.heap_;
        o.b_ = o.e_ = o.c_ = nullptr;
        o.heap_ = false;
    }
    T* allocate(size_t n, bool& on_heap) {
        void* p = arena_ ? arena_->bump(n * sizeof(T)) : nullptr;
        on_heap = false;
        if (!p) {
            p = malloc(n * sizeof(T));
            if (!p) throw std::bad_alloc();
            on_heap = true;
        }
        return static_cast<T*>(p);
    }
    Arena* arena_ = nullptr;
    T* b_ = nullptr;
    T* e_ = nullptr;
    T* c_ = nullptr;
    bool heap_ = false;
};

// One recycled arena per process: a released handle parks its arena here
// and the next handle takes it, so repeated fresh-handle parses reuse warm
// pages instead of mapping and faulting new ones. Bounded to kMaxSpareBytes.
struct SpareArena {
    static constexpr size_t kMaxSpareBytes = 8u << 20;
    static std::atomic<Arena*>& slot() {
        static std::atomic<Arena*> spare{nullptr};
        return spare;
    }
    static Arena* take(size_t need) {
        Arena* a = slot().exchange(nullptr, std::memory_order_acq_rel);
        if (a && a->size >= need) {
            a->used = 0;
            return a;
        }
        Arena::destroy(a);
        return nullptr;
    }
    static void give(Arena* a) {
        if (!a) return;
        if (a->size <= kMaxSpareBytes) {
            Arena* expected = nullptr;
            if (slot().compare_exchange_strong(expected, a, std::memory_order_acq_rel)) return;
        }
        Arena::destroy(a);
    }
};

}  // namespace fosu::detail
