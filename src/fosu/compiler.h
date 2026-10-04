#pragma once

#if defined(_MSC_VER)
#define FOSU_ALWAYS_INLINE __forceinline
#define FOSU_NOINLINE __declspec(noinline)
#define FOSU_UNREACHABLE() __assume(0)
#else
#define FOSU_ALWAYS_INLINE __attribute__((always_inline)) inline
#define FOSU_NOINLINE __attribute__((noinline))
#define FOSU_UNREACHABLE() __builtin_unreachable()
#endif

// osu! rounds every floating-point operation separately. Between these
// markers, compilers may not contract a multiply and an add into one fused
// multiply-add, which rounds once. GCC's pragma is a function attribute, and
// GCC does not inline across differing attributes: keep the post-processing
// code that calls these functions inside the markers too.
#if defined(__clang__)
#define FOSU_FP_CONTRACT_OFF_BEGIN _Pragma("clang fp contract(off)")
#define FOSU_FP_CONTRACT_OFF_END
#elif defined(__GNUC__)
#define FOSU_FP_CONTRACT_OFF_BEGIN \
  _Pragma("GCC push_options") _Pragma("GCC optimize(\"fp-contract=off\")")
#define FOSU_FP_CONTRACT_OFF_END _Pragma("GCC pop_options")
#else
#define FOSU_FP_CONTRACT_OFF_BEGIN
#define FOSU_FP_CONTRACT_OFF_END
#endif

#if defined(_WIN32)
#define FOSU_EXPORT __declspec(dllexport)
#define FOSU_IMPORT __declspec(dllimport)
#else
#define FOSU_EXPORT __attribute__((visibility("default")))
#define FOSU_IMPORT
#endif
