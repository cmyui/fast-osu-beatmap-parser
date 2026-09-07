// Isolated DSO entry points for paired in-process measurements. Build each
// version with hidden visibility so C++ inline symbols cannot interpose.
#define EXPORT extern "C" __attribute__((visibility("default")))
#ifdef FOSU_BENCH_CAPI
#include <fosu/c_api.h>
#include <cstdlib>
EXPORT void* fosu_bench_new() { return fosu_new(); }
EXPORT void fosu_bench_free(void* p) { fosu_free(static_cast<fosu_handle*>(p)); }
EXPORT void fosu_bench_parse(void* p, const char* data, size_t size, int reuse) {
    auto* handle = reuse ? static_cast<fosu_handle*>(p) : fosu_new();
    if (!handle || fosu_parse(handle, data, size, FOSU_ALL) != FOSU_OK) std::abort();
    const auto* result = fosu_get_view(handle);
    __asm__ volatile("" : : "g"(result) : "memory");
    if (!reuse) fosu_free(handle);
}
#else
#include <fosu/parser.hpp>
#ifdef FOSU_BENCH_OFFSET
#include <fosu/offset_beatmap.hpp>
using Result = fosu::OffsetBeatmap;
#else
using Result = fosu::Beatmap;
#endif
EXPORT void* fosu_bench_new() { return new Result; }
EXPORT void fosu_bench_free(void* p) { delete static_cast<Result*>(p); }
EXPORT void fosu_bench_parse(void* p, const char* data, size_t size, int reuse) {
    if (reuse) {
        auto& bm = *static_cast<Result*>(p);
        fosu::parse_into(data, size, bm);
        __asm__ volatile("" : : "g"(&bm) : "memory");
    } else {
        Result bm;
        fosu::parse_into(data, size, bm);
        __asm__ volatile("" : : "g"(&bm) : "memory");
    }
}

#endif
