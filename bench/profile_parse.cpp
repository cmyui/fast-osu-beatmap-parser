// In-process profiling driver: parse an evenly spaced corpus subset
// repeatedly so perf can sample a steady loop. Inputs are read before the
// loop; each round parses every file once. Modes: fresh (new result per
// parse, including destruction) or reuse (one retained result).
//
//   profile_parse corpus limit rounds fresh|reuse [sections-mask]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <fosu/parser.hpp>
static long faults() { rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_minflt; }

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: profile_parse corpus limit rounds fresh|reuse [sections]\n"); return 2; }
    std::vector<std::string> files;
    for (const auto& f : std::filesystem::directory_iterator(argv[1]))
        if (f.path().extension() == ".osu") files.push_back(f.path());
    std::sort(files.begin(), files.end());
    size_t limit = strtoul(argv[2], nullptr, 10);
    if (!limit || limit > files.size()) limit = files.size();
    const int rounds = atoi(argv[3]);
    if (!limit || rounds < 1 || (strcmp(argv[4], "fresh") && strcmp(argv[4], "reuse"))) return 2;
    const bool reuse = strcmp(argv[4], "reuse") == 0;
    const uint32_t sections = argc > 5 ? static_cast<uint32_t>(strtoul(argv[5], nullptr, 0)) : fosu::kAllSections;
    std::vector<fosu::FileBuffer> inputs;
    size_t bytes = 0;
    for (size_t i = 0; i < limit; ++i) {
        inputs.push_back(fosu::read_file_padded(files[i * files.size() / limit].c_str()));
        if (!inputs.back()) return 1;
        bytes += inputs.back().size;
    }
    fosu::Parser retained;
    size_t objects = 0;
    double best = 1e30, total = 0;
    long fault_total = 0;
    for (int r = 0; r < rounds; ++r) {
        const long f0 = faults();
        const auto t0 = std::chrono::steady_clock::now();
        for (const auto& in : inputs) {
            if (reuse) {
                const auto& beatmap = retained.parse(
                    in, {.sections = sections});
                __asm__ volatile("" : : "g"(&beatmap) : "memory");
                objects += beatmap.hit_objects.size();
            } else {
                fosu::Parser parser;
                const auto& beatmap = parser.parse(
                    in, {.sections = sections});
                __asm__ volatile("" : : "g"(&beatmap) : "memory");
                objects += beatmap.hit_objects.size();
            }
        }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        best = std::min(best, us);
        total += us;
        fault_total += faults() - f0;
    }
    printf("files %zu bytes %zu rounds %d mode %s sections 0x%x: best round %.1f us (%.3f us/file, %.1f MB/s), mean round %.1f us, objects %zu, minor faults/round %.1f\n",
           limit, bytes, rounds, reuse ? "reuse" : "fresh", sections, best, best / limit,
           bytes / best, total / rounds, objects, (double)fault_total / rounds);
    return 0;
}
