// Verilator testbench for line iteration + section dispatch.
//
// The reference is the C++ parser's own line loop, transcribed here from
// parse_into(): end at '\n' or EOF, strip one trailing '\r' when the body is
// non-empty, skip zero-length lines, '[' selects a section (needs len >= 3),
// "//" is a comment. Every non-empty line of every corpus file must come out of
// the RTL with the same (start, length, kind, section).
//
// The RTL's memory port is modelled the way io.hpp already guarantees to the
// C++ parser: WIN readable bytes at any address, zero past end of file.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Vfosu_line_iter.h"
#include "verilated.h"

namespace {

constexpr int kWin = 64;

// Mirrors C++ `enum class Section`.
enum Sec { NONE = 0, GENERAL, EDITOR, METADATA, DIFF, EVENTS, TIMING, COLOURS,
           HITOBJ, UNKNOWN };

struct Rec {
    uint32_t start, len;
    uint8_t kind, section;
    bool operator==(const Rec& o) const {
        return start == o.start && len == o.len && kind == o.kind &&
               section == o.section;
    }
};

int match_section(const char* p, size_t len) {
    if (len < 3) return UNKNOWN;
    switch (p[1]) {
        case 'G': return GENERAL;
        case 'E': return p[2] == 'd' ? EDITOR : EVENTS;
        case 'M': return METADATA;
        case 'D': return DIFF;
        case 'T': return TIMING;
        case 'C': return COLOURS;
        case 'H': return HITOBJ;
        default:  return UNKNOWN;
    }
}

// The C++ line loop, as the reference.
std::vector<Rec> reference(const std::string& c) {
    std::vector<Rec> out;
    int section = NONE;
    size_t p = 0;
    const size_t end = c.size();
    while (p < end) {
        const void* nl = memchr(c.data() + p, '\n', end - p);
        size_t line_end = nl ? static_cast<size_t>(
                                   static_cast<const char*>(nl) - c.data())
                             : end;
        const size_t next = nl ? line_end + 1 : end;
        if (line_end > p && c[line_end - 1] == '\r') --line_end;
        const size_t len = line_end - p;

        if (len != 0) {
            Rec r{};
            r.start = static_cast<uint32_t>(p);
            r.len = static_cast<uint32_t>(len);
            if (c[p] == '[') {
                r.kind = 0;
                r.section = static_cast<uint8_t>(match_section(c.data() + p, len));
                out.push_back(r);
                if (len >= 3) section = r.section;
            } else if (len >= 2 && c[p] == '/' && c[p + 1] == '/') {
                r.kind = 1;
                r.section = static_cast<uint8_t>(section);
                out.push_back(r);
            } else {
                r.kind = 2;
                r.section = static_cast<uint8_t>(section);
                out.push_back(r);
            }
        }
        p = next;
    }
    return out;
}

const std::string* g_file = nullptr;

void serve_mem(Vfosu_line_iter* dut) {
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

// One clock edge with the memory model kept coherent on both phases.
void cycle(Vfosu_line_iter* dut) {
    serve_mem(dut);
    dut->eval();
    dut->clk = 1;
    dut->eval();
    dut->clk = 0;
    serve_mem(dut);
    dut->eval();
}

uint64_t g_cycles = 0;

// Runs one file through the RTL and returns the emitted records.
std::vector<Rec> run_rtl(Vfosu_line_iter* dut, const std::string& content,
                         uint64_t cycle_budget) {
    g_file = &content;
    std::vector<Rec> out;

    dut->rst_n = 0;
    dut->start = 0;
    dut->file_len = 0;
    for (int i = 0; i < 3; ++i) cycle(dut);
    dut->rst_n = 1;
    dut->file_len = static_cast<uint32_t>(content.size());
    dut->start = 1;
    cycle(dut);
    dut->start = 0;

    uint64_t spent = 0;
    while (!dut->done && spent < cycle_budget) {
        if (dut->rec_valid)
            out.push_back(Rec{dut->rec_start, dut->rec_len,
                              static_cast<uint8_t>(dut->rec_kind),
                              static_cast<uint8_t>(dut->rec_section)});
        cycle(dut);
        ++spent;
        ++g_cycles;
    }
    // `done` and a final record can land on the same edge.
    if (dut->rec_valid)
        out.push_back(Rec{dut->rec_start, dut->rec_len,
                          static_cast<uint8_t>(dut->rec_kind),
                          static_cast<uint8_t>(dut->rec_section)});
    return out;
}

int g_fail = 0;

bool compare(const std::string& name, const std::vector<Rec>& got,
             const std::vector<Rec>& want) {
    if (got.size() != want.size()) {
        if (g_fail < 8)
            std::printf("MISMATCH %s: %zu records, expected %zu\n", name.c_str(),
                        got.size(), want.size());
        ++g_fail;
        // Still show the first differing entry below.
    }
    const size_t n = std::min(got.size(), want.size());
    for (size_t i = 0; i < n; ++i) {
        if (!(got[i] == want[i])) {
            if (g_fail < 8)
                std::printf("MISMATCH %s record %zu:\n"
                            "  rtl  start=%u len=%u kind=%u sec=%u\n"
                            "  ref  start=%u len=%u kind=%u sec=%u\n",
                            name.c_str(), i, got[i].start, got[i].len,
                            got[i].kind, got[i].section, want[i].start,
                            want[i].len, want[i].kind, want[i].section);
            ++g_fail;
            return false;
        }
    }
    return got.size() == want.size();
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const std::string dir = argc > 1 ? argv[1] : "bench/corpus-large";
    const size_t limit = argc > 2 ? std::stoul(argv[2]) : 0;

    auto* dut = new Vfosu_line_iter;
    dut->clk = 0;

    std::printf("line iteration + section dispatch vs the C++ line loop\n");

    // Directed cases: the shapes that make line splitting interesting.
    const std::vector<std::pair<std::string, std::string>> directed = {
        {"empty", ""},
        {"no trailing newline", "[General]\nAudioFilename: a.mp3"},
        {"crlf", "[General]\r\nAudioFilename: a.mp3\r\n"},
        {"blank lines", "\n\n[General]\n\n\nMode: 0\n"},
        {"comment", "[Events]\n//Background and Video events\n0,0,\"bg.jpg\"\n"},
        {"bare cr", "[General]\rMode: 0\n"},
        {"short header", "[X]\n[Q\n[\n"},
        {"editor vs events", "[Editor]\nBookmarks: 1\n[Events]\n//x\n"},
        {"crlf only", "\r\n\r\n"},
        {"lone newline", "\n"},
        {"header at eof no nl", "[HitObjects]"},
        {"cr at eof", "[General]\nMode: 0\r"},
    };
    for (const auto& [name, body] : directed) {
        const auto want = reference(body);
        const auto got = run_rtl(dut, body, 100000);
        compare(name, got, want);
    }
    std::printf("  directed   : %zu cases\n", directed.size());

    // Real corpus files.
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

    uint64_t lines = 0, bytes = 0;
    size_t done_files = 0;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if (content.empty()) continue;
        const auto want = reference(content);
        // Generous budget: one cycle per window scanned plus two per line.
        const uint64_t budget = content.size() / kWin + want.size() * 4 + 1000;
        const auto got = run_rtl(dut, content, budget);
        if (!compare(f.filename().string(), got, want) && g_fail > 8) break;
        lines += want.size();
        bytes += content.size();
        ++done_files;
    }
    std::printf("  corpus     : %zu files, %llu bytes, %llu lines\n", done_files,
                static_cast<unsigned long long>(bytes),
                static_cast<unsigned long long>(lines));
    if (bytes)
        std::printf("  throughput : %.2f bytes/cycle simulated\n",
                    double(bytes) / double(g_cycles));

    dut->final();
    delete dut;
    if (g_fail) {
        std::printf("FAIL: %d mismatches\n", g_fail);
        return 1;
    }
    std::printf("PASS: line iteration matches the C++ loop\n");
    return 0;
}
