// Prefix-only microbenchmark. It does not parse complete beatmaps.
#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <fosu/parser.h>

uint64_t rng_state = 0x9E3779B97F4A7C15ull;
uint64_t rng() {
    rng_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

int main() {
#if FOSU_SIMD
    std::vector<std::string> lines;
    char buf[96];
    for (int i = 0; i < 4096; ++i) {
        snprintf(buf, sizeof buf, "%d,%d,%d,%d,%d,0:0:0:0:",
                 int(rng() % 512), int(rng() % 384), int(rng() % 100000000),
                 int(rng() % 4) == 0 ? 5 : 1, int(rng() % 16));
        lines.emplace_back(buf);
        lines.back().append(fosu::kBufferPadding, '\0');
    }
    constexpr int iterations = 100;
    uint64_t sink = 0;
    double best[2] = {1e30, 1e30};
    for (int rep = 0; rep < 9; ++rep) for (int j = 0; j < 2; ++j) {
        const int simd = (rep + j) % 2;
        auto start = std::chrono::steady_clock::now();
        for (int it = 0; it < iterations; ++it) for (const auto& line : lines) {
            if (simd) {
                const auto prefix =
                    fosu::internal::try_parse_hitobject_prefix_fast(line.data());
                if (prefix) sink += static_cast<uint32_t>(prefix->value.time);
            } else {
                const auto prefix = fosu::internal::parse_hitobject_prefix_scalar(
                    line.data(), line.size());
                if (prefix) sink += static_cast<uint32_t>(prefix->value.time);
            }
        }
        const double ns = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count();
        best[simd] = std::min(best[simd], ns / (iterations * lines.size()));
    }
    printf("prefix only: scalar %.2f ns, SIMD %.2f ns; checksum %llu\n",
           best[0], best[1], static_cast<unsigned long long>(sink));
#else
    puts("prefix comparison requires a SIMD backend");
#endif
}
