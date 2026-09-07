// Minimal runtime for the standalone Linux x86-64 executable only.
// No constructors, libc startup, TLS, allocator bookkeeping, or atexit.
#include <cstddef>
#include <cstdint>
#include <new>
#include <sys/stat.h>
#include <immintrin.h>
#include "third_party/fast_float.h"

#if defined(__clang__)
#define FOSU_RUNTIME_SYMBOL __attribute__((used))
#else
#define FOSU_RUNTIME_SYMBOL __attribute__((used, externally_visible))
#endif

static long call(long n, long a = 0, long b = 0, long c = 0, long d = 0,
                 long e = 0, long f = 0) {
    register long r10 asm("r10") = d;
    register long r8 asm("r8") = e;
    register long r9 asm("r9") = f;
    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}
extern "C" __attribute__((noreturn)) void _exit(int code) {
    call(60, code);
    __builtin_unreachable();
}
extern "C" __attribute__((noreturn)) void abort() noexcept { _exit(70); }
extern "C" __attribute__((noreturn)) void __assert_fail(const char*,
                                                        const char*, unsigned,
                                                        const char*) noexcept {
    abort();
}
extern "C" int open(const char* p, int flags, ...) {
    return call(2, (long)p, flags);
}
extern "C" int close(int fd) { return call(3, fd); }
extern "C" int fstat(int fd, struct stat* st) noexcept {
    return call(5, fd, (long)st);
}
extern "C" long read(int fd, void* p, size_t n) {
    return call(0, fd, (long)p, n);
}
extern "C" void* mmap(void* a, size_t n, int prot, int flags, int fd,
                      long offset) noexcept {
    return (void*)call(9, (long)a, n, prot, flags, fd, offset);
}
extern "C" long write(int fd, const void* p, size_t n) {
    return call(1, fd, (long)p, n);
}

// One process owns the arena for its entire lifetime; exit reclaims it.
alignas(4096) static unsigned char arena[128u << 20];
static size_t used;
void* operator new(size_t n) {
    size_t at = (used + 15) & ~size_t(15);
    if (n > sizeof(arena) - at) _exit(71);
    used = at + n;
    return arena + at;
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, size_t) noexcept {}
void operator delete[](void*, size_t) noexcept {}
namespace std {
__attribute__((noreturn)) void __throw_bad_alloc() { abort(); }
__attribute__((noreturn)) void __throw_bad_array_new_length() { abort(); }
__attribute__((noreturn)) void __throw_length_error(const char*) { abort(); }
__attribute__((noreturn)) void __throw_logic_error(const char*) { abort(); }
__attribute__((noreturn)) void __throw_out_of_range_fmt(const char*, ...) {
    abort();
}
}  // namespace std

extern "C" FOSU_RUNTIME_SYMBOL void* memcpy(void* dst, const void* src,
                                            size_t n) noexcept {
    void* out = dst;
    asm volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return out;
}
extern "C" FOSU_RUNTIME_SYMBOL void* memmove(void* dst, const void* src,
                                             size_t n) noexcept {
    if ((uintptr_t)dst <= (uintptr_t)src ||
        (uintptr_t)dst - (uintptr_t)src >= n)
        return memcpy(dst, src, n);
    auto* d = static_cast<unsigned char*>(dst);
    auto* s = static_cast<const unsigned char*>(src);
    while (n) {
        --n;
        d[n] = s[n];
    }
    return dst;
}
extern "C" FOSU_RUNTIME_SYMBOL void* memset(void* dst, int c,
                                            size_t n) noexcept {
    void* out = dst;
    asm volatile("rep stosb"
                 : "+D"(dst), "+c"(n)
                 : "a"((unsigned char)c)
                 : "memory");
    return out;
}
extern "C" size_t strlen(const char* p) noexcept {
    const char* s = p;
    while (*p) ++p;
    return p - s;
}
extern "C" int memcmp(const void* a, const void* b, size_t n) noexcept {
    auto* x = static_cast<const unsigned char*>(a);
    auto* y = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i]) return int(x[i]) - int(y[i]);
    return 0;
}
extern "C" int bcmp(const void* a, const void* b, size_t n) noexcept {
    return memcmp(a, b, n);
}
// Avoid overreading memory not covered by the caller's span.
extern "C" void* fosu_memchr(const void*, int, size_t) noexcept asm("memchr");
extern "C" void* fosu_memchr(const void* p, int c, size_t n) noexcept {
    auto* s = static_cast<const unsigned char*>(p);
    const __m512i needle = _mm512_set1_epi8(c);
    while (n >= 64) {
        uint64_t match = _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(s), needle);
        if (match)
            return const_cast<unsigned char*>(s + __builtin_ctzll(match));
        s += 64;
        n -= 64;
    }
    if (n) {
        uint64_t mask = (1ull << n) - 1;
        uint64_t match = _mm512_mask_cmpeq_epi8_mask(
            mask, _mm512_maskz_loadu_epi8(mask, s), needle);
        if (match)
            return const_cast<unsigned char*>(s + __builtin_ctzll(match));
    }
    return nullptr;
}
extern "C" double strtod(const char* s, char** end) noexcept {
    const char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p == '+') ++p;
    const char* last = p;
    while ((*last >= '0' && *last <= '9') || *last == '.' || *last == '-' ||
           *last == '+' || *last == 'e' || *last == 'E')
        ++last;
    double value = 0;
    auto result = fast_float::from_chars(p, last, value);
    *end = const_cast<char*>(result.ptr == p ? s : result.ptr);
    return value;
}
extern int main(int, char**);
extern "C" int fosu_start(uintptr_t* stack) {
    return main(int(stack[0]), reinterpret_cast<char**>(stack + 1));
}
