// First shared-library use in a fresh C process, including dlopen, input I/O,
// parse, view acquisition, free and dlclose. Excludes process startup itself.
// Output columns match coldstart.cpp for library_first_compare.py; only bytes,
// object count and the fourth (timed region) column are populated.
#include <fosu/c_api.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef FOSU_DEFAULT_LIBRARY
#define FOSU_DEFAULT_LIBRARY "build/libfosu.so"
#endif

static uint64_t now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const char* path = getenv("FOSU_LIBRARY");
    if (!path) path = FOSU_DEFAULT_LIBRARY;
    uint64_t start = now();
    void* dso = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!dso) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    uint32_t (*version)(void) = dlsym(dso, "fosu_abi_version");
    fosu_handle* (*create)(void) = dlsym(dso, "fosu_new");
    void (*release)(fosu_handle*) = dlsym(dso, "fosu_free");
    int (*parse)(fosu_handle*, const char*, uint32_t) = dlsym(dso, "fosu_parse_file");
    const fosu_view* (*view)(const fosu_handle*) = dlsym(dso, "fosu_get_view");
    if (!version || !create || !release || !parse || !view || version() != FOSU_ABI_VERSION) return 1;
    fosu_handle* h = create();
    if (!h || parse(h, argv[1], FOSU_ALL) != FOSU_OK) return 1;
    const fosu_view* result = view(h);
    if (!result) return 1;
    size_t bytes = result->source_size, objects = result->hit_object_count;
    __asm__ volatile("" : : "g"(result) : "memory");
    release(h);
    dlclose(dso);
    uint64_t elapsed = now() - start;
    printf("%zu\t%zu\t0\t%llu\t0\t0\t0\n", bytes, objects, (unsigned long long)elapsed);
    return 0;
}
