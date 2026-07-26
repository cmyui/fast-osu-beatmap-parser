// Benchmark: fosu (SIMD), fosu (scalar), and a typical getline+sscanf
// baseline over either a synthetic corpus or a directory of real .osu files.
//
//   ./bench                 synthetic corpus (default 40 maps)
//   ./bench <dir>           every *.osu under <dir> (non-recursive)

#include <dirent.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <fosu/parser.hpp>

namespace {

uint64_t rng_state = 0x9E3779B97F4A7C15ull;
uint64_t rng() {
    rng_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::string generate_map(int n_objects) {
    std::string out;
    out.reserve(static_cast<size_t>(n_objects) * 48 + 4096);
    out +=
        "osu file format v14\r\n\r\n[General]\r\nAudioFilename: audio.mp3\r\n"
        "AudioLeadIn: 0\r\nPreviewTime: 40000\r\nCountdown: 0\r\n"
        "SampleSet: Soft\r\nStackLeniency: 0.7\r\nMode: 0\r\n"
        "LetterboxInBreaks: 0\r\nWidescreenStoryboard: 1\r\n\r\n"
        "[Editor]\r\nDistanceSpacing: 1.1\r\nBeatDivisor: 4\r\nGridSize: 32\r\n"
        "TimelineZoom: 2\r\n\r\n"
        "[Metadata]\r\nTitle:Synthetic Benchmark\r\nTitleUnicode:Synthetic "
        "Benchmark\r\nArtist:fosu\r\nArtistUnicode:fosu\r\nCreator:bench\r\n"
        "Version:Expert\r\nSource:\r\nTags:benchmark corpus\r\n"
        "BeatmapID:1\r\nBeatmapSetID:1\r\n\r\n"
        "[Difficulty]\r\nHPDrainRate:5\r\nCircleSize:4\r\n"
        "OverallDifficulty:9\r\nApproachRate:9.4\r\nSliderMultiplier:1.8\r\n"
        "SliderTickRate:1\r\n\r\n"
        "[Events]\r\n0,0,\"bg.jpg\",0,0\r\n\r\n[TimingPoints]\r\n";

    char line[512];
    int t = 800 + static_cast<int>(rng() % 2000);
    const int tp_count = 20 + static_cast<int>(rng() % 60);
    int tp_time = t;
    for (int i = 0; i < tp_count; ++i) {
        if (i % 5 == 0) {
            snprintf(line, sizeof line, "%d,%.13f,4,2,1,%d,1,0\r\n", tp_time,
                     280.0 + double(rng() % 2000) / 7.0,
                     40 + int(rng() % 60));
        } else {
            snprintf(line, sizeof line, "%d,-%.10f,4,2,1,%d,0,%d\r\n", tp_time,
                     50.0 + double(rng() % 1500) / 10.0, 40 + int(rng() % 60),
                     int(rng() % 2));
        }
        out += line;
        tp_time += 4000 + static_cast<int>(rng() % 20000);
    }

    out += "\r\n[HitObjects]\r\n";
    const char* samples[] = {"", ",0:0:0:0:", ",0:0:0:0:", ",2:0:0:0:"};
    for (int i = 0; i < n_objects; ++i) {
        const int x = static_cast<int>(rng() % 512);
        const int y = static_cast<int>(rng() % 384);
        const int hs_pool[] = {0, 0, 0, 2, 4, 8, 12, 6};
        const int hs = hs_pool[rng() % 8];
        const bool combo = rng() % 4 == 0;
        const uint64_t kind = rng() % 100;
        if (kind < 60) {
            snprintf(line, sizeof line, "%d,%d,%d,%d,%d%s\r\n", x, y, t,
                     combo ? 5 : 1, hs, samples[rng() % 4]);
            out += line;
        } else if (kind < 96) {
            const char curves[] = {'B', 'P', 'L', 'B'};
            const int n_pts = 1 + static_cast<int>(rng() % 6);
            int off = snprintf(line, sizeof line, "%d,%d,%d,%d,%d,%c", x, y, t,
                               combo ? 6 : 2, hs, curves[rng() % 4]);
            for (int p = 0; p < n_pts; ++p)
                off += snprintf(line + off, sizeof line - off, "|%d:%d",
                                int(rng() % 640), int(rng() % 480));
            off += snprintf(line + off, sizeof line - off, ",%d,%.2f%s\r\n",
                            1 + int(rng() % 3),
                            30.0 + double(rng() % 12000) / 20.0,
                            samples[rng() % 4]);
            out += line;
        } else {
            snprintf(line, sizeof line, "256,192,%d,12,%d,%d%s\r\n", t, hs,
                     t + 800 + int(rng() % 3000), samples[rng() % 4]);
            out += line;
        }
        t += 120 + static_cast<int>(rng() % 500);
    }
    return out;
}

// Checksum over integer fields only, so SIMD/scalar/naive results are
// exactly comparable.
uint64_t checksum(const fosu::Beatmap& bm) {
    uint64_t sum = bm.hit_objects.size() * 1000003 + bm.timing_points.size();
    for (const auto& h : bm.hit_objects) {
        sum = sum * 31 + static_cast<uint32_t>(h.x);
        sum = sum * 31 + static_cast<uint32_t>(h.y);
        sum = sum * 31 + static_cast<uint32_t>(h.time);
        sum = sum * 31 + h.type + h.hitsound;
    }
    return sum;
}

// Representative "typical parser": stringstream + getline + sscanf.
uint64_t naive_parse(const std::string& content) {
    std::istringstream in(content);
    std::string line;
    std::string section;
    uint64_t sum = 0;
    uint64_t objects = 0, tps = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '/') continue;
        if (line[0] == '[') {
            section = line;
            continue;
        }
        if (section == "[HitObjects]") {
            int x, y, t, ty, hs;
            if (sscanf(line.c_str(), "%d,%d,%d,%d,%d", &x, &y, &t, &ty, &hs) == 5) {
                ++objects;
                sum = sum * 31 + static_cast<uint32_t>(x);
                sum = sum * 31 + static_cast<uint32_t>(y);
                sum = sum * 31 + static_cast<uint32_t>(t);
                sum = sum * 31 + static_cast<uint32_t>(ty) + static_cast<uint32_t>(hs);
                if (ty & 2) {  // slider: pull out slides/length like fosu does
                    const char* c = strchr(line.c_str(), '|');
                    if (c) {
                        int px, py;
                        while (sscanf(c, "|%d:%d", &px, &py) == 2) {
                            c = strchr(c + 1, '|');
                            if (!c) break;
                        }
                    }
                }
            }
        } else if (section == "[TimingPoints]") {
            double time, beat;
            if (sscanf(line.c_str(), "%lf,%lf", &time, &beat) == 2) ++tps;
        }
    }
    return objects * 1000003 + tps + sum;
}

struct BenchResult {
    double best_seconds;
    uint64_t check;
};

template <typename F>
BenchResult run_bench(F&& parse_all, int reps) {
    BenchResult r{1e100, 0};
    for (int i = 0; i < reps; ++i) {
        const auto start = std::chrono::steady_clock::now();
        r.check = parse_all();
        const auto stop = std::chrono::steady_clock::now();
        const double s = std::chrono::duration<double>(stop - start).count();
        if (s < r.best_seconds) r.best_seconds = s;
    }
    return r;
}

void report(const char* name, const BenchResult& r, size_t total_bytes,
            uint64_t total_objects) {
    printf("%-22s %8.2f MB/s  %8.1f ns/object  %10.2f Mobj/s  (checksum %016llx)\n",
           name, static_cast<double>(total_bytes) / r.best_seconds / 1e6,
           r.best_seconds * 1e9 / static_cast<double>(total_objects),
           static_cast<double>(total_objects) / r.best_seconds / 1e6,
           static_cast<unsigned long long>(r.check));
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> raw;      // for the naive parser
    std::vector<fosu::FileBuffer> padded;

    if (argc > 1) {
        DIR* d = opendir(argv[1]);
        if (!d) {
            fprintf(stderr, "cannot open %s\n", argv[1]);
            return 1;
        }
        std::string dir = argv[1];
        if (dir.back() != '/') dir += '/';
        while (dirent* e = readdir(d)) {
            const size_t n = strlen(e->d_name);
            if (n < 4 || strcmp(e->d_name + n - 4, ".osu") != 0) continue;
            if (e->d_name[0] == '.') continue;  // macOS AppleDouble files
            auto buf = fosu::read_file_padded((dir + e->d_name).c_str());
            if (!buf) continue;
            raw.emplace_back(buf.data.get(), buf.size);
            padded.push_back(std::move(buf));
        }
        closedir(d);
        printf("loaded %zu .osu files from %s\n", padded.size(), argv[1]);
        if (padded.empty()) return 1;
    } else {
        const int sizes[] = {400, 900, 1600, 2600, 5200};
        for (int i = 0; i < 40; ++i) {
            raw.push_back(generate_map(sizes[i % 5]));
            padded.push_back(fosu::make_padded(raw.back()));
        }
        printf("generated %zu synthetic maps\n", padded.size());
    }

    size_t total_bytes = 0;
    for (const auto& b : padded) total_bytes += b.size;

    uint64_t total_objects = 0;
    for (const auto& b : padded)
        total_objects += fosu::parse(b).hit_objects.size();
    printf("corpus: %.2f MB, %llu hitobjects\n\n",
           static_cast<double>(total_bytes) / 1e6,
           static_cast<unsigned long long>(total_objects));

    constexpr int kReps = 9;

#if FOSU_SIMD_X86
    const auto simd = run_bench(
        [&] {
            uint64_t sum = 0;
            for (const auto& b : padded)
                sum ^= checksum(fosu::parse(b, {.use_simd = true}));
            return sum;
        },
        kReps);
    report("fosu (AVX2)", simd, total_bytes, total_objects);

    fosu::Beatmap reused;
    const auto simd_reuse = run_bench(
        [&] {
            uint64_t sum = 0;
            for (const auto& b : padded) {
                fosu::parse_into(b, reused, {.use_simd = true});
                sum ^= checksum(reused);
            }
            return sum;
        },
        kReps);
    report("fosu (AVX2, reuse)", simd_reuse, total_bytes, total_objects);
    if (simd_reuse.check != simd.check) {
        printf("CHECKSUM MISMATCH between fresh and reused Beatmap!\n");
        return 1;
    }
#endif

    const auto scalar = run_bench(
        [&] {
            uint64_t sum = 0;
            for (const auto& b : padded)
                sum ^= checksum(fosu::parse(b, {.use_simd = false}));
            return sum;
        },
        kReps);
    report("fosu (scalar)", scalar, total_bytes, total_objects);

#if FOSU_SIMD_X86
    if (simd.check != scalar.check) {
        printf("CHECKSUM MISMATCH between SIMD and scalar paths!\n");
        return 1;
    }
#endif

    const auto naive = run_bench(
        [&] {
            uint64_t sum = 0;
            for (const auto& s : raw) sum ^= naive_parse(s);
            return sum;
        },
        kReps);
    report("getline+sscanf", naive, total_bytes, total_objects);

#if FOSU_SIMD_X86
    printf("\nAVX2 vs scalar: %.2fx    AVX2 vs naive: %.1fx\n",
           scalar.best_seconds / simd.best_seconds,
           naive.best_seconds / simd.best_seconds);

    // Microbenchmark: hitobject prefix only, isolating the SIMD win from
    // parser overhead (line splitting, vector growth, slider params).
    {
        std::vector<std::string> lines;
        char buf[96];
        for (int i = 0; i < 4096; ++i) {
            snprintf(buf, sizeof buf, "%d,%d,%d,%d,%d,0:0:0:0:",
                     int(rng() % 512), int(rng() % 384),
                     int(rng() % 100000000), int(rng() % 4) == 0 ? 5 : 1,
                     int(rng() % 16));
            lines.emplace_back(buf);
            lines.back().append(64, '\0');
        }
        constexpr int kIters = 2000;
        fosu::HitObject h{};
        uint64_t sink = 0;

        auto t0 = std::chrono::steady_clock::now();
        for (int it = 0; it < kIters; ++it)
            for (const auto& l : lines) {
                uint32_t nl_mask;
                fosu::detail::fast_parse_prefix(l.data(), h, nl_mask);
                sink += static_cast<uint32_t>(h.time);
            }
        auto t1 = std::chrono::steady_clock::now();
        for (int it = 0; it < kIters; ++it)
            for (const auto& l : lines) {
                fosu::detail::scalar_parse_prefix(l.data(), l.size(), h);
                sink += static_cast<uint32_t>(h.time);
            }
        auto t2 = std::chrono::steady_clock::now();

        const double n = double(kIters) * double(lines.size());
        printf("\nprefix microbench:  AVX2 %.2f ns/line   scalar %.2f ns/line   (sink %llx)\n",
               std::chrono::duration<double>(t1 - t0).count() * 1e9 / n,
               std::chrono::duration<double>(t2 - t1).count() * 1e9 / n,
               static_cast<unsigned long long>(sink));
    }
#endif
    return 0;
}
