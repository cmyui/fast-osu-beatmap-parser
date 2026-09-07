// Process-lifecycle benchmark: spawn <bin> <file> once per run, time from
// posix_spawn to wait4 return (the whole exec -> parse -> exit lifetime),
// and check its stdout against a reference.
//
//   proc_bench ref    <bin> <dir> <ref.tsv>        write name/hash/bytes per file
//   proc_bench verify <bin> <dir> <ref.tsv>        compare stdout hashes
//   proc_bench time   <bin> <dir> [reps] [limit]   stdout -> /dev/null, best-of-reps
//
// Run under taskset; children inherit the affinity.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int cmp_str(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }

static char** list_osu(const char* dir, size_t* n) {
    DIR* d = opendir(dir);
    if (!d) { perror(dir); exit(1); }
    size_t cap = 1024; char** v = malloc(cap * sizeof *v); *n = 0;
    struct dirent* e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 4 || strcmp(e->d_name + l - 4, ".osu") || e->d_name[0] == '.') continue;
        if (*n == cap) v = realloc(v, (cap *= 2) * sizeof *v);
        v[(*n)++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(v, *n, sizeof *v, cmp_str);
    return v;
}

static uint64_t fnv(const unsigned char* p, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}

// Spawns bin path; if hash != NULL captures stdout into *hash/*bytes, else
// sends it to /dev/null. Returns wall microseconds; fills rusage.
static double run_once(const char* bin, const char* path, uint64_t* hash, size_t* bytes,
                       struct rusage* ru, int* status) {
    int pfd[2] = {-1, -1};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (hash) {
        if (pipe(pfd)) { perror("pipe"); exit(1); }
        posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
        posix_spawn_file_actions_addclose(&fa, pfd[0]);
        posix_spawn_file_actions_addclose(&fa, pfd[1]);
    } else {
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    }
    char* argv[] = {(char*)bin, (char*)path, NULL};
    pid_t pid;
    const double t0 = now_us();
    const int rc = posix_spawn(&pid, bin, &fa, NULL, argv, environ);
    if (rc) { fprintf(stderr, "spawn %s: %s\n", bin, strerror(rc)); exit(1); }
    if (hash) {
        close(pfd[1]);
        static unsigned char buf[1 << 16];
        uint64_t h = 0xcbf29ce484222325ull; size_t total = 0;
        for (;;) {
            ssize_t r = read(pfd[0], buf, sizeof buf);
            if (r <= 0) break;
            h = fnv(buf, (size_t)r, h); total += (size_t)r;
        }
        close(pfd[0]);
        *hash = h ^ (total * 0x9E3779B97F4A7C15ull); *bytes = total;
    }
    wait4(pid, status, 0, ru);
    const double t1 = now_us();
    posix_spawn_file_actions_destroy(&fa);
    return t1 - t0;
}

static int cmp_d(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: see header\n"); return 2; }
    const char* mode = argv[1]; const char* bin = argv[2]; const char* dir = argv[3];
    size_t n; char** names = list_osu(dir, &n);
    char path[4096];

    if (!strcmp(mode, "ref") || !strcmp(mode, "verify")) {
        if (argc < 5) { fprintf(stderr, "need ref.tsv\n"); return 2; }
        FILE* f = fopen(argv[4], strcmp(mode, "ref") ? "r" : "w");
        if (!f) { perror(argv[4]); return 1; }
        size_t bad = 0, failed = 0;
        for (size_t i = 0; i < n; ++i) {
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            uint64_t h; size_t b; struct rusage ru; int st;
            run_once(bin, path, &h, &b, &ru, &st);
            if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { ++failed; fprintf(stderr, "FAILED exit: %s\n", names[i]); }
            if (!strcmp(mode, "ref")) {
                fprintf(f, "%s\t%016llx\t%zu\n", names[i], (unsigned long long)h, b);
            } else {
                char rn[512]; unsigned long long rh; size_t rb;
                if (fscanf(f, "%511s\t%llx\t%zu\n", rn, &rh, &rb) != 3 || strcmp(rn, names[i])) {
                    fprintf(stderr, "ref out of sync at %s\n", names[i]); return 1;
                }
                if (rh != h || rb != b) {
                    if (bad < 10) fprintf(stderr, "MISMATCH %s: ref %016llx/%zu got %016llx/%zu\n", names[i], rh, rb, (unsigned long long)h, b);
                    ++bad;
                }
            }
        }
        fclose(f);
        if (!strcmp(mode, "verify")) printf("verify: %zu files, %zu mismatches, %zu failed exits\n", n, bad, failed);
        else printf("ref: %zu files written\n", n);
        return bad || failed ? 1 : 0;
    }

    if (strcmp(mode, "time")) { fprintf(stderr, "unknown mode\n"); return 2; }
    const int reps = argc > 4 ? atoi(argv[4]) : 3;
    const size_t limit = argc > 5 ? (size_t)atol(argv[5]) : n;
    if (limit < n) n = limit;
    double* best = malloc(n * sizeof *best);
    double sum_best = 0, sum_user = 0, sum_sys = 0, sum_flt = 0; size_t total_bytes = 0;
    for (size_t i = 0; i < n; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        struct stat_dummy { long dummy; } sd; (void)sd;
        double b = 1e18; struct rusage bru = {0};
        for (int r = 0; r < reps; ++r) {
            struct rusage ru; int st;
            double w = run_once(bin, path, NULL, NULL, &ru, &st);
            if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { fprintf(stderr, "FAILED exit: %s\n", names[i]); return 1; }
            if (w < b) { b = w; bru = ru; }
        }
        best[i] = b; sum_best += b;
        sum_user += bru.ru_utime.tv_sec * 1e6 + bru.ru_utime.tv_usec;
        sum_sys += bru.ru_stime.tv_sec * 1e6 + bru.ru_stime.tv_usec;
        sum_flt += bru.ru_minflt;
        FILE* fp = fopen(path, "rb"); if (fp) { fseek(fp, 0, SEEK_END); total_bytes += (size_t)ftell(fp); fclose(fp); }
    }
    qsort(best, n, sizeof *best, cmp_d);
    printf("%s: %zu files x best-of-%d\n", bin, n, reps);
    printf("  wall/file  mean %8.1f us   p50 %8.1f   p90 %8.1f   p99 %8.1f   max %8.1f\n",
           sum_best / n, best[n / 2], best[n * 9 / 10], best[n * 99 / 100], best[n - 1]);
    printf("  child rusage/file  user %6.1f us  sys %6.1f us  minflt %5.1f    input %.1f KB/file  -> %.0f MB/s of input per wall second\n",
           sum_user / n, sum_sys / n, sum_flt / n, total_bytes / 1024.0 / n, total_bytes / sum_best);
    return 0;
}
