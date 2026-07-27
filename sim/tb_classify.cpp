// Verilator testbench for the phase-0 byte classifier.
//
// The golden model is fosu itself. The scalar predicates in golden() ARE the
// definition of the masks the C++ parser builds, and on x86 hosts this
// testbench additionally cross-checks against the AVX2 intrinsics in
// <fosu/hitobject_prefix.hpp>, closing the loop
//
//     RTL  ==  scalar C++  ==  AVX2
//
// on real ranked-map bytes. This is the same "accept => bit-identical to the
// reference" discipline the C++ fuzzer uses, pointed at hardware.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "Vfosu_classify_stage.h"
#include "verilated.h"

// Self-guarded: FOSU_SIMD_X86 is 0 on arm64, so this include is portable and
// the AVX2 cross-check below simply compiles out.
#include <fosu/hitobject_prefix.hpp>

// Must match the RTL's WIDTH parameter.
static constexpr int kWidth = 32;

namespace {

struct Masks {
    uint32_t nondigit = 0;
    uint32_t comma = 0;
    uint32_t newline = 0;
};

// The golden model: a byte-at-a-time statement of what each mask means.
Masks golden(const uint8_t* b) {
    Masks m;
    for (int i = 0; i < kWidth; ++i) {
        if (b[i] < '0' || b[i] > '9') m.nondigit |= 1u << i;
        if (b[i] == ',') m.comma |= 1u << i;
        if (b[i] == '\n') m.newline |= 1u << i;
    }
    return m;
}

// bytes_in is 8*WIDTH bits wide, so Verilator exposes it as an array of
// 32-bit words: word w holds bytes 4w..4w+3, little-endian.
void drive_bytes(Vfosu_classify_stage* dut, const uint8_t* b) {
    for (int w = 0; w < kWidth / 4; ++w) {
        dut->bytes_in[w] = static_cast<uint32_t>(b[4 * w + 0]) |
                           static_cast<uint32_t>(b[4 * w + 1]) << 8 |
                           static_cast<uint32_t>(b[4 * w + 2]) << 16 |
                           static_cast<uint32_t>(b[4 * w + 3]) << 24;
    }
}

// Advance one clock edge: the flip-flops capture whatever the inputs and the
// combinational cloud currently show, so outputs are readable after this.
void cycle(Vfosu_classify_stage* dut) {
    dut->clk = 1;
    dut->eval();
    dut->clk = 0;
    dut->eval();
}

uint64_t g_cycles = 0;
uint64_t g_windows = 0;
int g_failures = 0;

// Compares one already-clocked window and reports the first few mismatches.
bool check(Vfosu_classify_stage* dut, const uint8_t* win, const char* where) {
    const Masks g = golden(win);
    const bool ok = dut->nondigit_mask == g.nondigit &&
                    dut->comma_mask == g.comma &&
                    dut->newline_mask == g.newline && dut->valid_out == 1;
    if (!ok && g_failures < 10) {
        std::printf("MISMATCH (%s)\n", where);
        std::printf("  bytes    :");
        for (int i = 0; i < kWidth; ++i) std::printf(" %02x", win[i]);
        std::printf("\n  nondigit : rtl %08x  golden %08x\n",
                    dut->nondigit_mask, g.nondigit);
        std::printf("  comma    : rtl %08x  golden %08x\n", dut->comma_mask,
                    g.comma);
        std::printf("  newline  : rtl %08x  golden %08x\n", dut->newline_mask,
                    g.newline);
        std::printf("  valid_out: %u (expected 1)\n", dut->valid_out);
    }
    if (!ok) ++g_failures;
    return ok;
}

// Drives one window through the stage and checks the result.
void run_window(Vfosu_classify_stage* dut, const uint8_t* win,
                const char* where) {
    drive_bytes(dut, win);
    dut->valid_in = 1;
    cycle(dut);
    ++g_cycles;
    ++g_windows;
    check(dut, win, where);
}

void reset(Vfosu_classify_stage* dut) {
    dut->rst_n = 0;
    dut->valid_in = 0;
    std::memset(&dut->bytes_in, 0, sizeof(dut->bytes_in));
    for (int i = 0; i < 4; ++i) {
        cycle(dut);
        ++g_cycles;
    }
    dut->rst_n = 1;
}

// Test 1: every byte value in every lane. A wrong truth table fails this, and
// so does a mis-wired lane reading its neighbour's byte -- the surrounding
// bytes are randomized specifically so lane crossing cannot hide.
void test_exhaustive(Vfosu_classify_stage* dut) {
    std::mt19937 rng(0xF05Cu);
    uint8_t win[kWidth];
    for (int v = 0; v < 256; ++v) {
        for (int lane = 0; lane < kWidth; ++lane) {
            for (int i = 0; i < kWidth; ++i)
                win[i] = static_cast<uint8_t>(rng() & 0xFF);
            win[lane] = static_cast<uint8_t>(v);
            run_window(dut, win, "exhaustive value x lane");
        }
    }
    std::printf("  exhaustive : 256 values x %d lanes = %d windows\n", kWidth,
                256 * kWidth);
}

// Test 2: valid_out must track valid_in with exactly one cycle of latency,
// and must be low out of reset. This pins the stream contract every later
// stage depends on.
void test_valid_pipelining(Vfosu_classify_stage* dut) {
    reset(dut);
    if (dut->valid_out != 0) {
        std::printf("MISMATCH: valid_out high after reset\n");
        ++g_failures;
    }

    uint8_t win[kWidth];
    std::memset(win, '0', sizeof win);
    drive_bytes(dut, win);

    dut->valid_in = 1;
    cycle(dut);
    ++g_cycles;
    if (dut->valid_out != 1) {
        std::printf("MISMATCH: valid_in=1 did not appear as valid_out\n");
        ++g_failures;
    }

    dut->valid_in = 0;
    cycle(dut);
    ++g_cycles;
    if (dut->valid_out != 0) {
        std::printf("MISMATCH: valid_out did not fall with valid_in\n");
        ++g_failures;
    }
    std::printf("  valid      : reset low, 1-cycle latency, falls with input\n");
}

// Test 3: real corpus bytes, streamed as consecutive windows the way the
// ingest bus will feed them. Tail is zero-padded, which is exactly the
// kBufferPadding guarantee the C++ parser already requires of its callers.
void test_corpus(Vfosu_classify_stage* dut, const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string name = e.path().filename().string();
        if (name.rfind("._", 0) == 0) continue;  // macOS AppleDouble litter
        if (e.path().extension() == ".osu") files.push_back(e.path());
    }
    if (ec || files.empty()) {
        std::printf("  corpus     : SKIPPED (no .osu files in %s)\n",
                    dir.c_str());
        return;
    }
    std::sort(files.begin(), files.end());

    uint64_t bytes = 0;
    uint64_t avx2_checked = 0;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
        if (data.empty()) continue;
        bytes += data.size();
        data.resize(data.size() + kWidth, 0);  // padding, as fosu requires

        for (size_t off = 0; off + kWidth <= data.size(); off += kWidth) {
            run_window(dut, data.data() + off, f.filename().string().c_str());

#if FOSU_SIMD_X86
            // Third leg of the equivalence: the actual shipping AVX2 code.
            const __m256i v = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(data.data() + off));
            const uint32_t nd = fosu::detail::nondigit_mask32(v);
            const uint32_t cm = fosu::detail::comma_mask32(v);
            if (dut->nondigit_mask != nd || dut->comma_mask != cm) {
                if (g_failures < 10)
                    std::printf(
                        "MISMATCH vs AVX2 (%s +%zu): nd rtl %08x avx %08x, "
                        "cm rtl %08x avx %08x\n",
                        f.filename().string().c_str(), off, dut->nondigit_mask,
                        nd, dut->comma_mask, cm);
                ++g_failures;
            }
            ++avx2_checked;
#endif
        }
    }
    std::printf("  corpus     : %zu files, %llu bytes, %llu windows\n",
                files.size(), static_cast<unsigned long long>(bytes),
                static_cast<unsigned long long>(bytes / kWidth));
    if (avx2_checked)
        std::printf("  vs AVX2    : %llu windows cross-checked against "
                    "nondigit_mask32/comma_mask32\n",
                    static_cast<unsigned long long>(avx2_checked));
    else
        std::printf("  vs AVX2    : skipped (not an x86 host; scalar golden "
                    "model only)\n");
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const std::string corpus = argc > 1 ? argv[1] : "bench/corpus-large";

    auto* dut = new Vfosu_classify_stage;

    std::printf("phase 0: fosu_classify_stage vs fosu golden model\n");
    reset(dut);
    test_exhaustive(dut);
    test_valid_pipelining(dut);
    reset(dut);
    test_corpus(dut, corpus);

    dut->final();
    delete dut;

    // Throughput is trivially WIDTH bytes/cycle for a pure classifier -- the
    // number only becomes interesting once framing and the variable-length
    // slider walk can stall. The accounting is here so later phases inherit it.
    std::printf("\n%llu windows in %llu simulated cycles (%d bytes/cycle)\n",
                static_cast<unsigned long long>(g_windows),
                static_cast<unsigned long long>(g_cycles), kWidth);

    if (g_failures) {
        std::printf("FAIL: %d mismatches\n", g_failures);
        return 1;
    }
    std::printf("PASS: RTL is bit-identical to the golden model\n");
    return 0;
}
