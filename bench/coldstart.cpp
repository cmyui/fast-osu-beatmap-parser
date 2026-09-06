// Cold-start single-beatmap benchmark.
//
//   ./coldstart <file.osu> [warm_reps]
//
// A fresh process reads one file and times ONE fosu::parse(): cold
// instruction cache, cold branch predictors, cold heap (every result page
// is first-touched during the parse). It then times warm_reps further
// fresh parses in the same process and reports the best of those, so the
// cold penalty is measured directly rather than inferred. Minor page
// faults are counted around each parse via getrusage; with FOSU_PERF=1
// (needs perf permissions) hardware counters are read around the cold
// parse and the last warm parse via perf_event_open.
//
// Output: one tab-separated line; bench/coldstart.sh runs this once per
// process across a corpus and aggregates.

#include <malloc.h>
#include <sys/mman.h>
#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fosu/parser.hpp>

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

long minor_faults() {
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Hardware counters for one measured region. Every counter is optional:
// a VM may not expose all of them, and a failed open just reports 0.
struct Counters {
    static constexpr int kN = 7;
    static constexpr const char* names[kN] = {
        "cycles", "instructions", "branch_misses", "l1i_misses",
        "itlb_misses", "dtlb_misses", "l1d_misses"};
    int fd[kN];
    uint64_t val[kN];

    Counters() {
        for (int i = 0; i < kN; ++i) {
            fd[i] = -1;
            val[i] = 0;
        }
    }

    void open_all() {
#if defined(__linux__)
        struct Spec {
            uint32_t type;
            uint64_t config;
        };
        const Spec specs[kN] = {
            {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
            {PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
            {PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
            {PERF_TYPE_HW_CACHE,
             PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                 (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)},
            {PERF_TYPE_HW_CACHE,
             PERF_COUNT_HW_CACHE_ITLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                 (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)},
            {PERF_TYPE_HW_CACHE,
             PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                 (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)},
            {PERF_TYPE_HW_CACHE,
             PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                 (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)},
        };
        for (int i = 0; i < kN; ++i) {
            perf_event_attr attr;
            memset(&attr, 0, sizeof attr);
            attr.type = specs[i].type;
            attr.size = sizeof attr;
            attr.config = specs[i].config;
            attr.disabled = 1;
            attr.exclude_kernel = 0;  // page-fault handling is part of cold cost
            attr.exclude_hv = 1;
            fd[i] = static_cast<int>(
                syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
            if (fd[i] < 0)
                fprintf(stderr, "perf: %s unavailable (%s)\n", names[i],
                        strerror(errno));
        }
#endif
    }

    void start() {
#if defined(__linux__)
        for (int i = 0; i < kN; ++i)
            if (fd[i] >= 0) {
                ioctl(fd[i], PERF_EVENT_IOC_RESET, 0);
                ioctl(fd[i], PERF_EVENT_IOC_ENABLE, 0);
            }
#endif
    }

    void stop() {
#if defined(__linux__)
        for (int i = 0; i < kN; ++i)
            if (fd[i] >= 0) {
                ioctl(fd[i], PERF_EVENT_IOC_DISABLE, 0);
                uint64_t v = 0;
                if (read(fd[i], &v, sizeof v) == static_cast<ssize_t>(sizeof v))
                    val[i] = v;
            }
#endif
    }
};

struct Sample {
    uint64_t ns = 0;
    long faults = 0;
    size_t objects = 0;
    Counters ctr;
};

// Decomposition experiments: FOSU_COLD_MODE is a comma-separated set of
// flags applied before the cold parse (default: none — the true
// fresh-process measurement).
//   prefault  heap pages already resident and never trimmed: removes the
//             result-vector page faults and heap-growth syscalls.
//   warmcode  parse a tiny in-memory map first: code pages, icache, BTB
//             and the used LaneMasks entries are warm; heap still cold.
//   ptable    software-prefetch the whole LaneMasks table into L2.
//   pcode     software-prefetch the executable's text segment into L2.
bool has_flag(const char* mode, const char* flag) {
    const size_t n = strlen(flag);
    for (const char* p = mode; (p = strstr(p, flag)); p += n)
        if ((p == mode || p[-1] == ',') && (p[n] == 0 || p[n] == ','))
            return true;
    return false;
}

extern "C" char __executable_start[];
extern "C" char etext[];

void apply_cold_mode(const char* mode) {
    if (!mode) return;
    if (has_flag(mode, "warmcode")) {
        static const char kTiny[] =
            "osu file format v14\r\n[General]\r\nAudioFilename: a.mp3\r\n"
            "Mode: 0\r\n[Editor]\r\nBeatDivisor: 4\r\n[Metadata]\r\nTitle:t\r\n"
            "BeatmapID:1\r\n[Difficulty]\r\nHPDrainRate:5\r\nApproachRate:9\r\n"
            "[Events]\r\n0,0,\"bg.jpg\",0,0\r\n2,100,200\r\n"
            "[TimingPoints]\r\n1000,300.5,4,2,1,60,1,0\r\n2000,-100,4,2,1,60,0,0\r\n"
            "[Colours]\r\nCombo1 : 1,2,3\r\n"
            "[HitObjects]\r\n100,200,1000,1,0,0:0:0:0:\r\n"
            "100,200,1500,2,0,B|150:250|200:300,1,100,0|0,0:0|0:0,0:0:0:0:\r\n"
            "256,192,2000,12,0,3000,0:0:0:0:\r\n100,200,3500,5,2\r\n";
        fosu::FileBuffer tiny = fosu::make_padded({kTiny, sizeof kTiny - 1});
        volatile size_t sink = fosu::parse(tiny).hit_objects.size();
        (void)sink;
    }
    if (has_flag(mode, "prefault")) {
#if defined(__linux__)
        // No mmap chunks at all (a threshold above 32 MB is rejected by
        // glibc), never trim, and grow the heap generously.
        mallopt(M_MMAP_MAX, 0);
        mallopt(M_TRIM_THRESHOLD, 1 << 30);
        mallopt(M_TOP_PAD, 1 << 24);
#endif
        // Touch one byte per page through a volatile pointer: gcc removes
        // a plain malloc/memset/free triple as dead stores.
        constexpr size_t kBytes = 16u << 20;
        char* p = static_cast<char*>(malloc(kBytes));
        volatile char* vp = p;
        for (size_t i = 0; i < kBytes; i += 4096) vp[i] = 1;
        __asm__ volatile("" : : "r"(p) : "memory");
        free(p);
    }
#if FOSU_SIMD_X86
    if (has_flag(mode, "ptable")) {
        const char* t = reinterpret_cast<const char*>(fosu::detail::kLaneMasks.data());
        for (size_t i = 0; i < sizeof fosu::detail::kLaneMasks; i += 64)
            _mm_prefetch(t + i, _MM_HINT_T1);
    }
    if (has_flag(mode, "pcode")) {
        for (const char* c = __executable_start; c < etext; c += 64)
            _mm_prefetch(c, _MM_HINT_T1);
    }
#endif
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: coldstart <file.osu> [warm_reps]\n");
        return 2;
    }
    const int warm_reps = argc > 2 ? atoi(argv[2]) : 8;
    const bool use_perf = getenv("FOSU_PERF") != nullptr;

    const uint64_t r0 = now_ns();
    fosu::FileBuffer buf = fosu::read_file_padded(argv[1]);
    const uint64_t r1 = now_ns();
    if (!buf) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    apply_cold_mode(getenv("FOSU_COLD_MODE"));

    Sample cold;
    if (use_perf) cold.ctr.open_all();
    {
        const long f0 = minor_faults();
        cold.ctr.start();
        const uint64_t t0 = now_ns();
        fosu::Beatmap bm = fosu::parse(buf);
        const uint64_t t1 = now_ns();
        cold.ctr.stop();
        cold.faults = minor_faults() - f0;
        cold.ns = t1 - t0;
        cold.objects = bm.hit_objects.size();
    }

    // Warm: best of warm_reps further fresh parses (same process, heap
    // and caches already primed), counters from the last one.
    Sample warm;
    warm.ns = ~0ull;
    if (use_perf) warm.ctr.open_all();
    for (int i = 0; i < warm_reps; ++i) {
        const long f0 = minor_faults();
        warm.ctr.start();
        const uint64_t t0 = now_ns();
        fosu::Beatmap bm = fosu::parse(buf);
        const uint64_t t1 = now_ns();
        warm.ctr.stop();
        if (bm.hit_objects.size() != cold.objects) {
            fprintf(stderr, "object count changed between parses\n");
            return 1;
        }
        const long faults = minor_faults() - f0;
        if (t1 - t0 < warm.ns) {
            warm.ns = t1 - t0;
            warm.faults = faults;
        }
    }

    // bytes objects read_ns cold_ns cold_flt warm_ns warm_flt, then the
    // seven cold counters and the seven warm counters.
    printf("%zu\t%zu\t%llu\t%llu\t%ld\t%llu\t%ld", buf.size, cold.objects,
           static_cast<unsigned long long>(r1 - r0),
           static_cast<unsigned long long>(cold.ns), cold.faults,
           static_cast<unsigned long long>(warm.ns), warm.faults);
    for (int i = 0; i < Counters::kN; ++i)
        printf("\t%llu", static_cast<unsigned long long>(cold.ctr.val[i]));
    for (int i = 0; i < Counters::kN; ++i)
        printf("\t%llu", static_cast<unsigned long long>(warm.ctr.val[i]));
    printf("\n");
    return 0;
}
