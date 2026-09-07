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
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;
static uint64_t now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static int cmp(const void* a, const void* b) {
    return strcmp(*(char* const*)a, *(char* const*)b);
}
int main(int argc, char** argv) {
    if (argc < 6) {
        fprintf(stderr,
                "usage: process_bench corpus limit reps bin1 bin2 ...\n");
        return 2;
    }
    DIR* d = opendir(argv[1]);
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
    size_t limit = strtoul(argv[2], 0, 10);
    if (!limit || limit > n) limit = n;
    int reps = atoi(argv[3]), bins = argc - 4;
    if (!n || reps <= 0) return 2;
    puts(
        "file,bytes,rep,variant,wall_ns,user_us,sys_us,minor_faults,major_"
        "faults");
    char path[4096], data[65536];
    for (size_t i = 0; i < limit; ++i) {
        // Uniformly spaced over the sorted corpus, not just its first files.
        const char* name = names[i * n / limit];
        snprintf(path, sizeof path, "%s/%s", argv[1], name);
        struct stat st;
        if (stat(path, &st)) return 1;
        // Define cold-process / resident-input measurements without changing
        // global cache policy on the host. No parsed results are retained.
        int fd = open(path, O_RDONLY);
        if (fd < 0) return 1;
        while (read(fd, data, sizeof data) > 0) {
        }
        close(fd);
        for (int r = 0; r < reps; ++r) {
            for (int j = 0; j < bins; ++j) {
                int b = (j + r + i) % bins;
                char* child[] = {argv[4 + b], path, NULL};
                pid_t pid;
                uint64_t start = now();
                int rc =
                    posix_spawn(&pid, child[0], NULL, NULL, child, environ);
                if (rc) {
                    fprintf(stderr, "spawn: %s\n", strerror(rc));
                    return 1;
                }
                int status;
                struct rusage ru;
                while (wait4(pid, &status, 0, &ru) < 0)
                    if (errno != EINTR) return 1;
                uint64_t stop = now();
                if (!WIFEXITED(status) || WEXITSTATUS(status)) {
                    fprintf(stderr, "failed: %s %s status=%d\n", child[0], path,
                            status);
                    return 1;
                }
                printf("%s,%ld,%d,%s,%llu,%ld,%ld,%ld,%ld\n", name,
                       (long)st.st_size, r, child[0],
                       (unsigned long long)(stop - start),
                       ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec,
                       ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec,
                       ru.ru_minflt, ru.ru_majflt);
            }
        }
    }
    return 0;
}
