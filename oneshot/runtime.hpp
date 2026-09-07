#pragma once
// Freestanding runtime for the one-shot parser: raw Linux x86-64 syscalls,
// the four mem* routines the compiler may emit calls to, and _start.
// No libc, no libstdc++, no dynamic loader.
#include <immintrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>  // declarations only; the definitions below are ours (build with -D_FORTIFY_SOURCE=0)
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rt {

inline long sys1(long n, long a) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return r;
}
inline long sys2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}
inline long sys3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}
inline long sys6(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}

enum : long { SYS_read = 0, SYS_write = 1, SYS_open = 2, SYS_fstat = 5, SYS_mmap = 9, SYS_madvise = 28, SYS_exit_group = 231 };

inline long read(int fd, void* p, size_t n) { return sys3(SYS_read, fd, (long)p, (long)n); }
inline long write(int fd, const void* p, size_t n) { return sys3(SYS_write, fd, (long)p, (long)n); }
inline long madvise(void* p, size_t n, int adv) { return sys3(SYS_madvise, (long)p, (long)n, adv); }
[[noreturn]] inline void exit(int code) {
    for (;;) sys1(SYS_exit_group, code);
}

}  // namespace rt

extern "C" {
// POSIX surface used by Parser and its arena OS layer. The one-shot process
// has one thread, so errno needs no TLS runtime.
int* __errno_location() noexcept {
    static int value;
    return &value;
}

static long posix_result(long result) {
    if (result < 0 && result >= -4095) {
        errno = static_cast<int>(-result);
        return -1;
    }
    return result;
}

int open(const char* path, int flags, ...) {
    return static_cast<int>(posix_result(rt::sys3(2, (long)path, flags, 0)));
}
int close(int fd) {
    return static_cast<int>(posix_result(rt::sys1(3, fd)));
}
int fstat(int fd, struct stat* info) noexcept {
    return static_cast<int>(posix_result(rt::sys2(5, fd, (long)info)));
}
ssize_t read(int fd, void* data, size_t size) {
    return posix_result(rt::read(fd, data, size));
}
ssize_t write(int fd, const void* data, size_t size) {
    return posix_result(rt::write(fd, data, size));
}
long sysconf(int name) noexcept {
    if (name == _SC_PAGESIZE) return 4096;  // Linux x86-64 base page size.
    errno = EINVAL;
    return -1;
}
void* mmap(void* address, size_t size, int protection, int flags, int fd, off_t offset) noexcept {
    return reinterpret_cast<void*>(posix_result(
        rt::sys6(9, (long)address, size, protection, flags, fd, offset)));
}
int mprotect(void* address, size_t size, int protection) noexcept {
    return static_cast<int>(posix_result(rt::sys3(10, (long)address, size, protection)));
}
int munmap(void* address, size_t size) noexcept {
    return static_cast<int>(posix_result(rt::sys2(11, (long)address, size)));
}
int madvise(void* address, size_t size, int advice) noexcept {
    return static_cast<int>(posix_result(rt::madvise(address, size, advice)));
}
int mlock(const void* address, size_t size) noexcept {
    return static_cast<int>(posix_result(rt::sys2(149, (long)address, size)));
}

// Small copies dominate (hit_sample and edge strings, 3-16 bytes each, a
// thousand-plus per file); rep movsb costs ~30 cycles of startup per call,
// so sizes up to 64 go through overlapping loads/stores instead.
void* memcpy(void* d, const void* s, size_t n) {
    char* dd = static_cast<char*>(d);
    const char* ss = static_cast<const char*>(s);
    if (n <= 16) {
        if (n >= 8) {
            uint64_t a, b;
            __builtin_memcpy(&a, ss, 8);
            __builtin_memcpy(&b, ss + n - 8, 8);
            __builtin_memcpy(dd, &a, 8);
            __builtin_memcpy(dd + n - 8, &b, 8);
        } else if (n >= 4) {
            uint32_t a, b;
            __builtin_memcpy(&a, ss, 4);
            __builtin_memcpy(&b, ss + n - 4, 4);
            __builtin_memcpy(dd, &a, 4);
            __builtin_memcpy(dd + n - 4, &b, 4);
        } else if (n) {
            dd[0] = ss[0];
            dd[n / 2] = ss[n / 2];
            dd[n - 1] = ss[n - 1];
        }
        return d;
    }
    if (n <= 32) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ss));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ss + n - 16));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dd), a);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dd + n - 16), b);
        return d;
    }
    if (n <= 64) {
        __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ss));
        __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ss + n - 32));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dd), a);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dd + n - 32), b);
        return d;
    }
    void* r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}
void* memmove(void* d, const void* s, size_t n) {
    if (d <= s || (const char*)d >= (const char*)s + n) return memcpy(d, s, n);
    // Backward copy: with DF set, rep movsb starts at the addresses given
    // and decrements, so they must point at the LAST byte, not one past it.
    char* dd = (char*)d + n - 1;
    const char* ss = (const char*)s + n - 1;
    __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(dd), "+S"(ss), "+c"(n) : : "memory");
    return d;
}
void* memset(void* d, int c, size_t n) {
    if (n <= 32) {
        char* dd = static_cast<char*>(d);
        const __m128i v = _mm_set1_epi8(static_cast<char>(c));
        if (n >= 16) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dd), v);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dd + n - 16), v);
        } else {
            for (size_t i = 0; i < n; ++i) dd[i] = static_cast<char>(c);
        }
        return d;
    }
    void* r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
}
// memchr is declared as C++ overloads by <cstring>; the symbol itself is
// provided by the parser TU through an asm label (see main.cpp).
