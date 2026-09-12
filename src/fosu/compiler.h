#pragma once

#if defined(_MSC_VER)
#define FOSU_ALWAYS_INLINE __forceinline
#define FOSU_NOINLINE __declspec(noinline)
#define FOSU_EXPORT __declspec(dllexport)
#define FOSU_IMPORT __declspec(dllimport)
#define FOSU_UNREACHABLE() __assume(0)
#else
#define FOSU_ALWAYS_INLINE __attribute__((always_inline)) inline
#define FOSU_NOINLINE __attribute__((noinline))
#define FOSU_EXPORT __attribute__((visibility("default")))
#define FOSU_IMPORT
#define FOSU_UNREACHABLE() __builtin_unreachable()
#endif
