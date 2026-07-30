// Verilator testbench for the shared numeric scanner.
//
// Verified directly against the C++ primitives it has to reproduce:
//   parse_i64   -> (neg, val19, int_len)
//   parse_double-> (mant, frac, neg, needs_host, dec_len)
//
// The decimal check is BIT-EXACT, not approximate: the testbench finishes the
// RTL's (mant, frac, neg) with the same two operations parse_double uses --
// v = (double)mant; if (frac) v /= kPow10[frac]; out = neg ? -v : v -- and
// compares the raw 64-bit patterns. That is the whole point of emitting a
// mantissa and exponent instead of building a float unit in silicon.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "Vfosu_numscan.h"
#include "verilated.h"

#include <fosu/scalar_parse.hpp>

namespace {

constexpr int kWin = 64;
int g_fail = 0;
uint64_t g_cases = 0, g_int_ok = 0, g_dec_ok = 0, g_host = 0;

void drive(Vfosu_numscan* dut, const uint8_t* b) {
    for (int w = 0; w < kWin / 4; ++w)
        dut->bytes_in[w] = static_cast<uint32_t>(b[4 * w + 0]) |
                           static_cast<uint32_t>(b[4 * w + 1]) << 8 |
                           static_cast<uint32_t>(b[4 * w + 2]) << 16 |
                           static_cast<uint32_t>(b[4 * w + 3]) << 24;
}

double bits_to_d(uint64_t u) {
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}
uint64_t d_to_bits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

void check(Vfosu_numscan* dut, const std::string& text, const char* where) {
    uint8_t win[kWin];
    std::memset(win, 0, sizeof win);
    const size_t n = std::min(text.size(), static_cast<size_t>(kWin));
    std::memcpy(win, text.data(), n);

    drive(dut, win);
    dut->eval();
    ++g_cases;

    const char* p = reinterpret_cast<const char*>(win);
    // The field's own extent: the scanner sees a window, and the C++ sees a
    // buffer bounded by the caller. Use the window as the bound in both.
    const char* end = p + kWin;

    // ---- integer form ----
    int64_t iref = 0;
    const char* iq = fosu::detail::parse_i64(p, end, iref);
    const size_t iconsumed = static_cast<size_t>(iq - p);
    if (!dut->int_run_full) {
        const bool rtl_any = dut->int_any != 0;
        const uint64_t mag = dut->val19;
        const int64_t ival = dut->neg ? -static_cast<int64_t>(mag)
                                      : static_cast<int64_t>(mag);
        bool bad = (rtl_any != (iconsumed != 0)) ||
                   (static_cast<size_t>(dut->int_len) != iconsumed);
        if (rtl_any && !bad) bad = (ival != iref);
        if (bad) {
            if (g_fail < 10)
                std::printf("INT MISMATCH (%s) \"%s\"\n"
                            "  rtl any=%d len=%u neg=%d val=%lld\n"
                            "  ref consumed=%zu val=%lld\n",
                            where, text.substr(0, 40).c_str(), rtl_any,
                            dut->int_len, dut->neg, (long long)ival, iconsumed,
                            (long long)iref);
            ++g_fail;
        } else if (rtl_any) {
            ++g_int_ok;
        }
    }

    // ---- decimal form ----
    double dref = 0;
    const char* dq = fosu::detail::parse_double(p, end, dref);
    const size_t dconsumed = static_cast<size_t>(dq - p);

    if (dut->needs_host) {
        // The RTL declines; the C++ must have taken its strtod path, i.e. more
        // than 18 significant digits or an exponent. Confirm independently.
        size_t dig = 0, i = (win[0] == '-') ? 1 : 0;
        while (i < kWin && win[i] >= '0' && win[i] <= '9') { ++dig; ++i; }
        bool dot = i < kWin && win[i] == '.';
        if (dot) { ++i; while (i < kWin && win[i] >= '0' && win[i] <= '9') { ++dig; ++i; } }
        const bool exp = i < kWin && (win[i] == 'e' || win[i] == 'E');
        if (!(dig > 18 || exp)) {
            if (g_fail < 10)
                std::printf("HOST-FLAG MISMATCH (%s) \"%s\": rtl asked for host "
                            "but digits=%zu exp=%d\n",
                            where, text.substr(0, 40).c_str(), dig, exp);
            ++g_fail;
        }
        ++g_host;
    } else if (dut->dec_any) {
        double v = static_cast<double>(dut->mant);
        if (dut->frac) v /= fosu::detail::kPow10[dut->frac];
        const double got = dut->neg ? -v : v;
        bool bad = d_to_bits(got) != d_to_bits(dref) ||
                   static_cast<size_t>(dut->dec_len) != dconsumed;
        if (bad) {
            if (g_fail < 10)
                std::printf("DEC MISMATCH (%s) \"%s\"\n"
                            "  rtl mant=%llu frac=%u neg=%d len=%u -> %.17g "
                            "(bits %016llx)\n"
                            "  ref consumed=%zu -> %.17g (bits %016llx)\n",
                            where, text.substr(0, 40).c_str(),
                            (unsigned long long)dut->mant, dut->frac, dut->neg,
                            dut->dec_len, got, (unsigned long long)d_to_bits(got),
                            dconsumed, dref,
                            (unsigned long long)d_to_bits(dref));
            ++g_fail;
        } else {
            ++g_dec_ok;
        }
    } else {
        // No digits: the C++ must also have consumed nothing.
        if (dconsumed != 0) {
            if (g_fail < 10)
                std::printf("DEC EMPTY MISMATCH (%s) \"%s\": rtl none, ref "
                            "consumed %zu\n",
                            where, text.substr(0, 40).c_str(), dconsumed);
            ++g_fail;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const std::string dir = argc > 1 ? argv[1] : "bench/corpus-large";

    auto* dut = new Vfosu_numscan;
    std::printf("numeric scanner vs parse_i64 / parse_double\n");

    // Directed: the shapes that pin the semantics.
    const char* directed[] = {
        "0", "1", "9", "42", "256", "-1", "-256", "2147483647", "-2147483648",
        "999999999999999999",            // 18 digits
        "9999999999999999999",           // 19 digits: int keeps them, dec punts
        "12345678901234567890123",       // 23 digits: dec punts
        "0.5", ".5", "-.5", "5.", "-5.", ".", "-", "-.", "",
        "1.0", "0.7", "3.14159", "-0.75", "1.7320508075688772",
        "140.5", "0.0001", "100000000000000000.5",   // 19 sig digits: punts
        "1e5", "1E5", "1.5e3", "-1.5E-3",            // exponents: punt
        "0.000000000000000001",          // 18 frac digits
        "abc", "x1", ",", ":", "|", "\r", "\n",
        "00000000000000000000000001",    // leading zeros, 26 digits
        "-0", "-0.0", "007", "1.230000",
    };
    for (const char* s : directed) check(dut, s, "directed");
    std::printf("  directed   : %zu cases\n", std::size(directed));

    // A digit run that fills the window: the caller must loop, so only the
    // run_full flag and the first-19-digit value are meaningful.
    {
        std::string all(kWin, '7');
        uint8_t win[kWin];
        std::memcpy(win, all.data(), kWin);
        drive(dut, win);
        dut->eval();
        if (!dut->int_run_full) {
            std::printf("RUN-FULL MISMATCH: 64 digits did not set int_run_full\n");
            ++g_fail;
        }
        uint64_t want = 0;
        for (int i = 0; i < 19; ++i) want = want * 10 + 7;
        if (dut->val19 != want) {
            std::printf("RUN-FULL VALUE: rtl %llu want %llu\n",
                        (unsigned long long)dut->val19,
                        (unsigned long long)want);
            ++g_fail;
        }
        std::printf("  run-full   : 64-digit run flagged, first 19 digits kept\n");
    }

    // Every numeric-looking token from real corpus files.
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().filename().string().rfind("._", 0) == 0) continue;
        if (e.path().extension() == ".osu") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    uint64_t tokens = 0;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string c((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
        // Start a candidate at every position that begins a number, plus every
        // delimiter+1, so field starts are covered as the engine will see them.
        for (size_t i = 0; i < c.size(); ++i) {
            const char ch = c[i];
            const bool starts = (ch >= '0' && ch <= '9') || ch == '-' || ch == '.';
            const bool after_delim = i && (c[i-1] == ',' || c[i-1] == ':' ||
                                           c[i-1] == '|' || c[i-1] == ' ');
            if (!starts && !after_delim) continue;
            check(dut, c.substr(i, kWin), "corpus");
            ++tokens;
            if (g_fail > 10) break;
        }
        if (g_fail > 10) break;
    }
    std::printf("  corpus     : %zu files, %llu numeric candidates\n",
                files.size(), (unsigned long long)tokens);

    // Random fuzz over numeric-ish alphabets.
    {
        std::mt19937 rng(0x9E3779B9u);
        const char pool[] = "0123456789.-,:|eE+ \r\nabc";
        for (int i = 0; i < 300000; ++i) {
            const int len = 1 + rng() % 24;
            std::string s;
            for (int j = 0; j < len; ++j) s.push_back(pool[rng() % (sizeof pool - 1)]);
            check(dut, s, "fuzz");
            if (g_fail > 10) break;
        }
        std::printf("  fuzz       : 300000 random numeric-ish strings\n");
    }

    dut->final();
    delete dut;
    std::printf("\n%llu candidates: %llu integer, %llu decimal verified, "
                "%llu deferred to host\n",
                (unsigned long long)g_cases, (unsigned long long)g_int_ok,
                (unsigned long long)g_dec_ok, (unsigned long long)g_host);
    if (g_fail) {
        std::printf("FAIL: %d mismatches\n", g_fail);
        return 1;
    }
    std::printf("PASS: numeric scanner matches the C++ primitives\n");
    return 0;
}
