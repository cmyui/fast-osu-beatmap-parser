// Verilator testbench for the parser engine.
//
// Runs whole .osu files through the RTL and compares the emitted records
// against what fosu::parse() produced for the same file. Currently checks
// [TimingPoints], where the doubles are the interesting part: the RTL emits
// (mant, frac, neg) and this testbench finishes them with the same two
// operations parse_double uses, then compares RAW 64-BIT PATTERNS. Nothing is
// approximated.
//
// A line the RTL declines (over 18 significant digits, or an exponent) arrives
// as a PUNT record carrying its span; the testbench parses that line with the
// C++ and counts it, which is exactly the host's job in the real system.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Vfosu_engine.h"
#include "verilated.h"

#include <fosu/parser.hpp>

namespace {

constexpr int kWin = 64;

constexpr int TAG_TIMING = 1;
constexpr int TAG_MALFORMED = 2;
constexpr int TAG_PUNT = 3;

const std::string* g_file = nullptr;
uint64_t g_cycles = 0;
int g_fail = 0;
uint64_t g_tp = 0, g_punt = 0, g_malformed = 0;

void serve_mem(Vfosu_engine* dut) {
    uint8_t w[kWin];
    std::memset(w, 0, sizeof w);
    const uint32_t a = dut->mem_addr;
    if (g_file && a < g_file->size()) {
        const size_t n = std::min(static_cast<size_t>(kWin), g_file->size() - a);
        std::memcpy(w, g_file->data() + a, n);
    }
    for (int i = 0; i < kWin / 4; ++i)
        dut->mem_data[i] = static_cast<uint32_t>(w[4 * i + 0]) |
                           static_cast<uint32_t>(w[4 * i + 1]) << 8 |
                           static_cast<uint32_t>(w[4 * i + 2]) << 16 |
                           static_cast<uint32_t>(w[4 * i + 3]) << 24;
}

void cycle(Vfosu_engine* dut) {
    serve_mem(dut);
    dut->eval();
    dut->clk = 1;
    dut->eval();
    dut->clk = 0;
    serve_mem(dut);
    dut->eval();
}

uint64_t d_to_bits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

// Rebuild a double from what the RTL emitted, using parse_double's own
// arithmetic so the comparison can be exact.
double rebuild(uint64_t mant, unsigned frac, bool neg) {
    double v = static_cast<double>(mant);
    if (frac) v /= fosu::detail::kPow10[frac];
    return neg ? -v : v;
}

struct TP {
    double time, beat_length;
    int32_t meter, sample_set, sample_index, volume;
    bool uninherited;
    uint32_t effects;
};

bool tp_equal(const TP& a, const fosu::TimingPoint& b) {
    return d_to_bits(a.time) == d_to_bits(b.time) &&
           d_to_bits(a.beat_length) == d_to_bits(b.beat_length) &&
           a.meter == b.meter && a.sample_set == b.sample_set &&
           a.sample_index == b.sample_index && a.volume == b.volume &&
           a.uninherited == b.uninherited && a.effects == b.effects;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const std::string dir = argc > 1 ? argv[1] : "bench/corpus-large";
    const size_t limit = argc > 2 ? std::stoul(argv[2]) : 0;

    auto* dut = new Vfosu_engine;
    dut->clk = 0;
    dut->rec_ready = 1;

    std::printf("engine [TimingPoints] vs fosu::parse\n");

    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().filename().string().rfind("._", 0) == 0) continue;
        if (e.path().extension() == ".osu") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    if (limit && files.size() > limit) files.resize(limit);

    uint64_t bytes = 0;
    size_t nfiles = 0;

    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        if (raw.empty()) continue;

        // The C++ reference over the same bytes.
        auto padded = fosu::make_padded(raw);
        fosu::Beatmap ref = fosu::parse(padded);

        g_file = &raw;
        std::vector<TP> got;

        dut->rst_n = 0;
        dut->start = 0;
        dut->file_len = 0;
        for (int i = 0; i < 3; ++i) cycle(dut);
        dut->rst_n = 1;
        dut->file_len = static_cast<uint32_t>(raw.size());
        dut->start = 1;
        cycle(dut);
        dut->start = 0;

        const uint64_t budget = raw.size() / kWin + raw.size() / 4 + 100000;
        uint64_t spent = 0;
        while (!dut->done && spent < budget) {
            serve_mem(dut);
            dut->eval();
            if (dut->rec_valid && dut->rec_ready) {
                if (dut->rec_tag == TAG_TIMING) {
                    TP t{};
                    t.time = rebuild(dut->tp_time_mant, dut->tp_time_frac,
                                     dut->tp_time_neg);
                    t.beat_length = rebuild(dut->tp_bl_mant, dut->tp_bl_frac,
                                            dut->tp_bl_neg);
                    t.meter = static_cast<int32_t>(dut->tp_meter);
                    t.sample_set = static_cast<int32_t>(dut->tp_sample_set);
                    t.sample_index = static_cast<int32_t>(dut->tp_sample_index);
                    t.volume = static_cast<int32_t>(dut->tp_volume);
                    t.uninherited = dut->tp_uninherited != 0;
                    t.effects = dut->tp_effects;
                    got.push_back(t);
                    ++g_tp;
                } else if (dut->rec_tag == TAG_PUNT) {
                    // The host finishes this line, exactly as it would in
                    // hardware. Its result still has to land in order.
                    fosu::Beatmap one;
                    const std::string body =
                        raw.substr(dut->rec_start, dut->rec_len);
                    auto pb = fosu::make_padded(body);
                    fosu::detail::parse_timing_point_line(one, pb.data.get(),
                                                          body.size());
                    if (one.timing_points.size() == 1) {
                        const auto& t = one.timing_points[0];
                        got.push_back(TP{t.time, t.beat_length, t.meter,
                                         t.sample_set, t.sample_index, t.volume,
                                         t.uninherited, t.effects});
                    }
                    ++g_punt;
                } else if (dut->rec_tag == TAG_MALFORMED) {
                    ++g_malformed;
                }
            }
            cycle(dut);
            ++spent;
            ++g_cycles;
        }
        if (spent >= budget) {
            std::printf("TIMEOUT on %s after %llu cycles\n",
                        f.filename().string().c_str(),
                        static_cast<unsigned long long>(spent));
            ++g_fail;
            break;
        }

        if (got.size() != ref.timing_points.size()) {
            if (g_fail < 8)
                std::printf("COUNT MISMATCH %s: rtl %zu timing points, ref %zu\n",
                            f.filename().string().c_str(), got.size(),
                            ref.timing_points.size());
            ++g_fail;
        }
        const size_t n = std::min(got.size(), ref.timing_points.size());
        for (size_t i = 0; i < n; ++i) {
            if (!tp_equal(got[i], ref.timing_points[i])) {
                if (g_fail < 8) {
                    const auto& a = got[i];
                    const auto& b = ref.timing_points[i];
                    std::printf("TP MISMATCH %s index %zu\n"
                                "  rtl time=%.17g (%016llx) bl=%.17g (%016llx) "
                                "meter=%d ss=%d si=%d vol=%d unin=%d eff=%u\n"
                                "  ref time=%.17g (%016llx) bl=%.17g (%016llx) "
                                "meter=%d ss=%d si=%d vol=%d unin=%d eff=%u\n",
                                f.filename().string().c_str(), i, a.time,
                                (unsigned long long)d_to_bits(a.time),
                                a.beat_length,
                                (unsigned long long)d_to_bits(a.beat_length),
                                a.meter, a.sample_set, a.sample_index, a.volume,
                                a.uninherited, a.effects, b.time,
                                (unsigned long long)d_to_bits(b.time),
                                b.beat_length,
                                (unsigned long long)d_to_bits(b.beat_length),
                                b.meter, b.sample_set, b.sample_index, b.volume,
                                b.uninherited, b.effects);
                }
                ++g_fail;
                break;
            }
        }
        bytes += raw.size();
        ++nfiles;
        if (g_fail > 8) break;
    }

    std::printf("  corpus     : %zu files, %llu bytes\n", nfiles,
                static_cast<unsigned long long>(bytes));
    std::printf("  records    : %llu timing points, %llu punted to host, "
                "%llu malformed\n",
                static_cast<unsigned long long>(g_tp),
                static_cast<unsigned long long>(g_punt),
                static_cast<unsigned long long>(g_malformed));
    if (bytes)
        std::printf("  throughput : %.2f bytes/cycle simulated\n",
                    double(bytes) / double(g_cycles));

    dut->final();
    delete dut;
    if (g_fail) {
        std::printf("FAIL: %d mismatches\n", g_fail);
        return 1;
    }
    std::printf("PASS: timing points bit-identical to fosu::parse\n");
    return 0;
}
