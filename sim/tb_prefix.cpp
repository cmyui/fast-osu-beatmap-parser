// Verilator testbench for the hitobject prefix core.
//
// Enforces the same invariant the C++ fuzzer does, pointed at hardware:
//
//     RTL accepts  =>  fields are bit-identical to the scalar reference
//
// plus, on x86 hosts, the stronger claim that the RTL's accepted DOMAIN
// matches the AVX2 fast path exactly -- same lines accepted, same lines
// rejected, same next-offset. A domain mismatch is not a correctness bug (the
// host fallback would catch it) but it is a throughput bug, so it is pinned.
//
// Inputs come from real ranked maps: every line of every [HitObjects] section
// in the corpus, plus a mutation fuzzer over those lines.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "Vfosu_prefix.h"
#include "verilated.h"

#include <fosu/hitobject_prefix.hpp>

namespace {

constexpr int kWindow = 32;
constexpr int kBytes = kWindow + 8;  // must match the RTL BYTES parameter

int g_fail = 0;
uint64_t g_lines = 0, g_accept = 0, g_reject = 0;

void drive(Vfosu_prefix* dut, const uint8_t* b) {
    for (int w = 0; w < kBytes / 4; ++w)
        dut->bytes_in[w] = static_cast<uint32_t>(b[4 * w + 0]) |
                           static_cast<uint32_t>(b[4 * w + 1]) << 8 |
                           static_cast<uint32_t>(b[4 * w + 2]) << 16 |
                           static_cast<uint32_t>(b[4 * w + 3]) << 24;
}

// Presents one candidate line to the RTL and checks it against the C++.
// `text` need not be NUL-terminated; the window is zero-padded, which is the
// same guarantee io.hpp gives the C++ parser.
void check_line(Vfosu_prefix* dut, const char* text, size_t len,
                const char* where) {
    uint8_t win[kBytes];
    std::memset(win, 0, sizeof win);
    std::memcpy(win, text, std::min(len, static_cast<size_t>(kBytes)));

    drive(dut, win);
    dut->eval();
    ++g_lines;

    const bool rtl_ok = dut->ok != 0;

    // The scalar reference accepts a superset of the fast path's domain, so it
    // is the arbiter of field values whenever the RTL commits to a result.
    // It must be given the TRUE line length, not the padded window size: it
    // bounds-checks against `end` and treats a NUL inside the buffer as a
    // stray character, whereas the SIMD path relies on padding and accepts NUL
    // as a terminator. Passing the padded length makes it reject everything.
    // It must also stop at the first CR/LF. The SIMD path treats those as
    // end-of-line terminators (they are, in a file); the scalar path only
    // accepts ',' or the buffer end, so an embedded newline would make it
    // reject a line the fast path rightly accepts. In the real parser this
    // never arises because the FRAMER owns line termination and hands the
    // field parser a body with no newline in it -- so the harness models the
    // same split.
    fosu::HitObject ref{};
    size_t ref_len = std::min(len, static_cast<size_t>(kBytes));
    for (size_t i = 0; i < ref_len; ++i)
        if (win[i] == '\r' || win[i] == '\n') { ref_len = i; break; }
    const int ref_next = fosu::detail::scalar_parse_prefix(
        reinterpret_cast<const char*>(win), ref_len, ref);

    if (rtl_ok) {
        ++g_accept;
        bool bad = false;
        if (ref_next < 0) {
            bad = true;  // RTL accepted something the reference rejects
        } else {
            bad = static_cast<int32_t>(dut->x) != ref.x ||
                  static_cast<int32_t>(dut->y) != ref.y ||
                  static_cast<int32_t>(dut->time_ms) != ref.time ||
                  dut->obj_type != ref.type || dut->hitsound != ref.hitsound ||
                  static_cast<int>(dut->next_off) != ref_next;
        }
        if (bad) {
            if (g_fail < 10) {
                // Escape non-printables: rendering them as '.' once hid an
                // embedded CR and sent me hunting a nonexistent RTL bug.
                std::printf("MISMATCH vs scalar reference (%s)\n  line: \"", where);
                for (size_t i = 0; i < std::min(len, size_t(48)); ++i) {
                    const unsigned char c = static_cast<unsigned char>(text[i]);
                    if (c >= 32 && c < 127) std::printf("%c", c);
                    else                    std::printf("\\x%02x", c);
                }
                std::printf("\"  (ref_len=%zu)\n", ref_len);
                std::printf("  rtl: x=%d y=%d t=%d type=%u hs=%u next=%u ok=1\n",
                            static_cast<int32_t>(dut->x),
                            static_cast<int32_t>(dut->y),
                            static_cast<int32_t>(dut->time_ms), dut->obj_type,
                            dut->hitsound, dut->next_off);
                if (ref_next < 0)
                    std::printf("  ref: REJECTED\n");
                else
                    std::printf("  ref: x=%d y=%d t=%d type=%u hs=%u next=%d\n",
                                ref.x, ref.y, ref.time, ref.type, ref.hitsound,
                                ref_next);
            }
            ++g_fail;
        }
    } else {
        ++g_reject;
    }

#if FOSU_SIMD_X86
    // Domain equality against the shipping AVX2 path.
    fosu::HitObject fh{};
    uint32_t nl = 0;
    const int fast_next =
        fosu::detail::fast_parse_prefix(reinterpret_cast<const char*>(win), fh, nl);
    const bool fast_ok = fast_next >= 0;
    bool dom_bad = fast_ok != rtl_ok;
    if (fast_ok && rtl_ok)
        dom_bad = dom_bad || static_cast<int>(dut->next_off) != fast_next ||
                  static_cast<int32_t>(dut->x) != fh.x ||
                  static_cast<int32_t>(dut->y) != fh.y ||
                  static_cast<int32_t>(dut->time_ms) != fh.time ||
                  dut->obj_type != fh.type || dut->hitsound != fh.hitsound;
    if (dom_bad) {
        if (g_fail < 10)
            std::printf("DOMAIN MISMATCH vs AVX2 (%s): rtl_ok=%d avx_ok=%d "
                        "rtl_next=%u avx_next=%d\n",
                        where, rtl_ok, fast_ok, dut->next_off, fast_next);
        ++g_fail;
    }
    if (rtl_ok && dut->newline_mask != nl) {
        if (g_fail < 10)
            std::printf("NEWLINE MASK MISMATCH (%s): rtl %08x avx %08x\n", where,
                        dut->newline_mask, nl);
        ++g_fail;
    }
#endif
}

// Collects every [HitObjects] line from a corpus of .osu files.
std::vector<std::string> load_hitobject_lines(const std::string& dir,
                                              size_t& files_seen) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().filename().string().rfind("._", 0) == 0) continue;
        if (e.path().extension() == ".osu") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    files_seen = files.size();

    std::vector<std::string> lines;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        const size_t sec = content.find("[HitObjects]");
        if (sec == std::string::npos) continue;
        size_t pos = content.find('\n', sec);
        while (pos != std::string::npos && pos + 1 < content.size()) {
            const size_t start = pos + 1;
            pos = content.find('\n', start);
            const size_t end = pos == std::string::npos ? content.size() : pos;
            std::string line = content.substr(start, end - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            if (line.empty()) continue;
            if (line[0] == '[') break;  // next section
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const std::string dir = argc > 1 ? argv[1] : "bench/corpus-large";

    auto* dut = new Vfosu_prefix;

    std::printf("hitobject prefix core vs fosu golden model\n");

    // --- 1. Directed cases, including the shapes that bound the domain. ---
    struct Case { const char* line; };
    const Case directed[] = {
        {"256,192,1000,1,0"},                       // minimal circle
        {"0,0,0,1,0"},                              // single-digit everything
        {"999,999,9999999,128,15"},                 // max real widths
        {"256,192,1000,1,0,0:0:0:0:"},              // with hit sample
        {"128,64,52836,2,0,B|136:36|200:76,1,140"}, // slider
        {"256,192,90000,12,0,92839,0:0:0:0:"},      // spinner
        {"1234,192,1000,1,0"},                      // 4-digit x -> reject
        {"-1,192,1000,1,0"},                        // negative -> reject
        {"256,192,1000,1"},                         // missing hitSound -> reject
        {"256,192,1000,1,"},                        // empty hitSound -> reject
        {"256,192,1000,1,0abc"},                    // bad terminator -> reject
        {"256,,1000,1,0"},                          // empty field -> reject
        {"256,192,99999999999,1,0"},                // 11-digit time -> reject
        {"256,192,3000000000,1,0"},                 // > INT32_MAX -> reject
        {"256,192,2147483647,1,0"},                 // exactly INT32_MAX
        {"256.5,192,1000,1,0"},                     // decimal -> reject
    };
    for (const auto& c : directed)
        check_line(dut, c.line, std::strlen(c.line), "directed");
    std::printf("  directed   : %zu cases\n", std::size(directed));

    // --- 2. Every real hitobject line in the corpus. ---
    size_t files_seen = 0;
    const auto lines = load_hitobject_lines(dir, files_seen);
    if (lines.empty()) {
        std::printf("  corpus     : SKIPPED (no .osu files in %s)\n", dir.c_str());
    } else {
        const uint64_t before_acc = g_accept;
        for (const auto& l : lines) check_line(dut, l.data(), l.size(), "corpus");
        std::printf("  corpus     : %zu files, %zu hitobject lines, "
                    "%llu accepted by the fast path (%.1f%%)\n",
                    files_seen, lines.size(),
                    static_cast<unsigned long long>(g_accept - before_acc),
                    100.0 * double(g_accept - before_acc) / double(lines.size()));
    }

    // --- 3. Mutation fuzz over real lines: substitute, extend, truncate.
    // Length mutations matter -- an aliasing bug in the C++ shape-cache work
    // was invisible to substitution-only fuzzing. ---
    if (!lines.empty()) {
        std::mt19937 rng(0xB0A7C0DEu);
        const char pool[] = "0123456789,.:|-\r\n abcABC";
        uint64_t n = 0;
        for (int iter = 0; iter < 200000; ++iter) {
            std::string s = lines[rng() % lines.size()];
            const int kind = rng() % 3;
            if (kind == 0 && !s.empty()) {  // substitute
                const int muts = 1 + rng() % 3;
                for (int m = 0; m < muts; ++m)
                    s[rng() % s.size()] = pool[rng() % (sizeof pool - 1)];
            } else if (kind == 1) {  // extend
                const int add = 1 + rng() % 4;
                for (int m = 0; m < add; ++m)
                    s.push_back(pool[rng() % (sizeof pool - 1)]);
            } else if (!s.empty()) {  // truncate
                s.resize(rng() % s.size());
            }
            check_line(dut, s.data(), s.size(), "fuzz");
            ++n;
        }
        std::printf("  fuzz       : %llu mutated lines (substitute/extend/truncate)\n",
                    static_cast<unsigned long long>(n));
    }

    dut->final();
    delete dut;

    std::printf("\n%llu candidates: %llu accepted, %llu rejected\n",
                static_cast<unsigned long long>(g_lines),
                static_cast<unsigned long long>(g_accept),
                static_cast<unsigned long long>(g_reject));
#if FOSU_SIMD_X86
    std::printf("domain checked against AVX2 fast_parse_prefix on every candidate\n");
#else
    std::printf("scalar reference only (not an x86 host); domain vs AVX2 unchecked\n");
#endif

    if (g_fail) {
        std::printf("FAIL: %d mismatches\n", g_fail);
        return 1;
    }
    std::printf("PASS: prefix core matches the golden model\n");
    return 0;
}
