// Isolated DSO entry points for paired in-process measurements. Build each
// version with hidden visibility so C++ inline symbols cannot interpose.
#define EXPORT extern "C" __attribute__((visibility("default")))
#include <fosu/parser.h>
using Result = fosu::Parser;
EXPORT void* fosu_bench_new() {
  return new Result;
}
EXPORT void fosu_bench_free(void* p) {
  delete static_cast<Result*>(p);
}
EXPORT void fosu_bench_parse(void* p, const char* data, size_t size, int reuse) {
  if (reuse) {
    auto& parser = *static_cast<Result*>(p);
    auto parsed = parser.parse(data, size);
    if (!parsed)
      std::abort();
    const auto& bm = *parsed.value();
    __asm__ volatile("" : : "g"(&bm) : "memory");
  } else {
    Result parser;
    auto parsed = parser.parse(data, size);
    if (!parsed)
      std::abort();
    const auto& bm = *parsed.value();
    __asm__ volatile("" : : "g"(&bm) : "memory");
  }
}
