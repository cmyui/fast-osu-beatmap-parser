#include "backend.hpp"
#include "cpu_features.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {
using fosu_dispatch::Backend;
// Remains alive after teardown, including for a host's later exit callbacks.
constinit std::atomic<const Backend*> selected{nullptr};
struct Cleanup {
    ~Cleanup() {
        if (const auto* b = selected.load(std::memory_order_relaxed)) b->cleanup();
    }
};
Cleanup cleanup;

bool avx2_available() {
#ifdef FOSU_HAS_AVX2
    static const bool supported = fosu_dispatch::host_supports_avx2();
    return supported;
#else
    return false;
#endif
}
bool neon_available() {
#ifdef FOSU_HAS_NEON
    static const bool supported = fosu_dispatch::host_supports_neon();
    return supported;
#else
    return false;
#endif
}
const Backend* choose() {
    const char* request = std::getenv("FOSU_BACKEND");
    if (!request) {
        const char* scalar = std::getenv("FOSU_FORCE_SCALAR");
        request = scalar && std::strcmp(scalar, "1") == 0 ? "scalar" : FOSU_DEFAULT_BACKEND;
    }
    const Backend* b = nullptr;
    if (std::strcmp(request, "scalar") == 0) b = &fosu_dispatch::scalar_backend();
    else if (std::strcmp(request, "auto") == 0 || std::strcmp(request, "avx2") == 0 ||
             std::strcmp(request, "neon") == 0) {
#ifdef FOSU_HAS_AVX2
        if (std::strcmp(request, "neon") != 0 && avx2_available()) b = &fosu_dispatch::avx2_backend();
#endif
#ifdef FOSU_HAS_NEON
        if (std::strcmp(request, "avx2") != 0 && neon_available()) b = &fosu_dispatch::neon_backend();
#endif
        if (!b && std::strcmp(request, "auto") == 0) b = &fosu_dispatch::scalar_backend();
    }
    selected.store(b, std::memory_order_relaxed);
    return b;
}
const Backend* backend() {
    static const Backend* const b = choose();
    return b;
}
}

extern "C" uint32_t fosu_abi_version() { return FOSU_ABI_VERSION; }
extern "C" const char* fosu_backend_name() {
    const auto* b = backend();
    return b ? b->name : nullptr;
}
extern "C" int fosu_backend_available(const char* name) {
    return name && (std::strcmp(name, "scalar") == 0 ||
                   (std::strcmp(name, "avx2") == 0 && avx2_available()) ||
                   (std::strcmp(name, "neon") == 0 && neon_available()));
}
extern "C" fosu_handle* fosu_new() {
    const auto* b = backend();
    return b ? static_cast<fosu_handle*>(b->create()) : nullptr;
}
extern "C" void fosu_free(fosu_handle* h) {
    if (const auto* b = backend()) b->destroy(h);
}
extern "C" const fosu_view* fosu_get_view(const fosu_handle* h) {
    const auto* b = backend();
    return b ? b->view(h) : nullptr;
}
extern "C" int fosu_parse(fosu_handle* h, const char* data, size_t size, uint32_t sections) {
    const auto* b = backend();
    return b ? b->parse(h, data, size, sections) : FOSU_INVALID_ARGUMENT;
}
extern "C" int fosu_parse_file(fosu_handle* h, const char* path, uint32_t sections) {
    const auto* b = backend();
    return b ? b->parse_file(h, path, sections) : FOSU_INVALID_ARGUMENT;
}
