#pragma once
#include <fosu/c_api.h>

namespace fosu_dispatch {
struct Backend {
    const char* name;
    void* (*create)();
    void (*destroy)(void*);
    const fosu_view* (*view)(const void*);
    int (*parse)(void*, const char*, size_t, uint32_t);
    int (*parse_file)(void*, const char*, uint32_t);
    void (*cleanup)();
};
const Backend& scalar_backend();
const Backend& avx2_backend();
}  // namespace fosu_dispatch
