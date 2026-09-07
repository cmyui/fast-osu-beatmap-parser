// Fresh-handle C API loop: dlopen a library, then for each file run
// new/parse(bytes)/view/free repeatedly, reporting the best iteration.
// Loads one library per process; use library_compare for interleaved variants.
//   c_api_loop libfosu.so corpus limit iterations
#include <fosu/c_api.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static int cmp(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    void* dso = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!dso) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    fosu_handle* (*create)(void) = dlsym(dso, "fosu_new");
    void (*release)(fosu_handle*) = dlsym(dso, "fosu_free");
    int (*parse)(fosu_handle*, const char*, size_t, uint32_t) = dlsym(dso, "fosu_parse");
    const fosu_view* (*view)(const fosu_handle*) = dlsym(dso, "fosu_get_view");
    if (!create || !release || !parse || !view) return 1;
    DIR* d = opendir(argv[2]);
    if (!d) return 2;
    size_t n = 0, cap = 16384;
    char** names = malloc(cap * sizeof *names);
    struct dirent* e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 4 || strcmp(e->d_name + l - 4, ".osu")) continue;
        if (n == cap) names = realloc(names, (cap *= 2) * sizeof *names);
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp);
    size_t limit = strtoul(argv[3], 0, 10);
    if (!limit || limit > n) limit = n;
    int iters = atoi(argv[4]);
    double sum_min = 0, sum_all = 0;
    size_t objects = 0;
    char path[4096];
    char* data = malloc(64u << 20);
    for (size_t i = 0; i < limit; ++i) {
        snprintf(path, sizeof path, "%s/%s", argv[2], names[i * n / limit]);
        FILE* f = fopen(path, "rb");
        if (!f) return 1;
        size_t size = fread(data, 1, 64u << 20, f);
        fclose(f);
        uint64_t best = ~0ull;
        for (int it = 0; it < iters; ++it) {
            uint64_t t0 = now();
            fosu_handle* h = create();
            if (!h || parse(h, data, size, FOSU_ALL) != FOSU_OK) return 1;
            const fosu_view* v = view(h);
            objects += v->hit_object_count;
            release(h);
            uint64_t dt = now() - t0;
            if (dt < best) best = dt;
            sum_all += dt;
        }
        sum_min += best;
    }
    printf("%s: files %zu iterations %d: mean of file minima %.3f us, mean of all %.3f us, objects %zu\n",
           argv[1], limit, iters, sum_min / limit / 1000, sum_all / limit / iters / 1000, objects);
    dlclose(dso);
    return 0;
}
