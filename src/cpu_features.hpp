#pragma once
#include <cstdint>
#if defined(__x86_64__)
#include <cpuid.h>
#endif

#if defined(__aarch64__) && defined(__linux__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif

namespace fosu_dispatch {
inline bool host_supports_neon() {
#if defined(__aarch64__) && defined(__APPLE__)
    return true;  // Required by the Apple arm64 platform.
#elif defined(__aarch64__) && defined(__linux__)
    return (getauxval(AT_HWCAP) & HWCAP_ASIMD) != 0;
#else
    return false;
#endif
}

// x86-64-v3, including OS support for saving XMM/YMM state.
struct CpuFeatures {
    uint32_t leaf1 = 0, leaf7 = 0, extended = 0;
    uint64_t xcr0 = 0;
};
inline constexpr uint32_t required_leaf1 = (1u << 0) | (1u << 9) | (1u << 12) |
    (1u << 13) | (1u << 19) | (1u << 20) | (1u << 22) | (1u << 23) |
    (1u << 26) | (1u << 27) | (1u << 28) | (1u << 29);
inline constexpr uint32_t required_leaf7 = (1u << 3) | (1u << 5) | (1u << 8);
inline constexpr uint32_t required_extended = (1u << 0) | (1u << 5);
constexpr bool supports_avx2(CpuFeatures f) {
    return (f.leaf1 & required_leaf1) == required_leaf1 &&
           (f.leaf7 & required_leaf7) == required_leaf7 &&
           (f.extended & required_extended) == required_extended && (f.xcr0 & 6) == 6;
}
inline bool host_supports_avx2() {
#if defined(__x86_64__)
    CpuFeatures f;
    unsigned a, b, c, d;
    // Query the maximum basic leaf once: CPUID can cause a costly VM exit.
    if (__get_cpuid_max(0, nullptr) < 7) return false;
    __cpuid(1, a, b, c, d);
    f.leaf1 = c;
    if ((f.leaf1 & required_leaf1) != required_leaf1) return false;
    unsigned lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    f.xcr0 = (uint64_t(hi) << 32) | lo;
    if ((f.xcr0 & 6) != 6) return false;
    __cpuid_count(7, 0, a, b, c, d);
    f.leaf7 = b;
    if ((f.leaf7 & required_leaf7) != required_leaf7) return false;
    if (__get_cpuid(0x80000001, &a, &b, &c, &d)) f.extended = c;
    return supports_avx2(f);
#else
    return false;
#endif
}
}  // namespace fosu_dispatch
