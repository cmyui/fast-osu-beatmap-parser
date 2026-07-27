// Testbench for rtl/examples/and32.sv -- the three-level AND example.
//
// Prints an ASCII timing diagram for each level, which is the point: you learn
// far more from watching valid/ready move cycle by cycle than from reading the
// RTL. This is what a waveform viewer shows, minus the GUI.

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "Vand32_demo.h"
#include "verilated.h"

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

// One rising edge, then settle. Inputs must already be driven.
void cycle(Vand32_demo* dut) {
    dut->clk = 1;
    dut->eval();
    dut->clk = 0;
    dut->eval();
}

// -------------------------------------------------------------------------
// Level 1: no clock involved at all. Inputs change, output follows.
// -------------------------------------------------------------------------
void level1(Vand32_demo* dut) {
    std::printf("LEVEL 1 -- combinational (no clock, no valid, no call)\n");

    struct Case { uint32_t a, b; };
    const Case cases[] = {
        {0xFFFFFFFFu, 0x00000000u},
        {0xFFFFFFFFu, 0xFFFFFFFFu},
        {0xF0F0F0F0u, 0xFF00FF00u},
        {0x0000000Cu, 0x0000000Au},
        {0xDEADBEEFu, 0x0000FFFFu},
    };

    std::printf("          a          b   ->          y     expected\n");
    for (const auto& c : cases) {
        dut->c_a = c.a;
        dut->c_b = c.b;
        dut->eval();  // no clock edge: just let the gates settle
        const uint32_t want = c.a & c.b;
        std::printf("   %08x   %08x   ->   %08x     %08x  %s\n", c.a, c.b,
                    dut->c_y, want, dut->c_y == want ? "ok" : "MISMATCH");
        expect(dut->c_y == want, "combinational AND");
    }

    // Nothing was clocked above, and the output was still correct every time.
    std::mt19937 rng(1234);
    for (int i = 0; i < 100000; ++i) {
        const uint32_t a = rng(), b = rng();
        dut->c_a = a;
        dut->c_b = b;
        dut->eval();
        if (dut->c_y != (a & b)) {
            expect(false, "randomized combinational AND");
            break;
        }
    }
    std::printf("   + 100000 random pairs, zero clock edges: all correct\n\n");
}

// -------------------------------------------------------------------------
// Level 2: latency 1, throughput 1/cycle. Both facts visible at once.
// -------------------------------------------------------------------------
void level2(Vand32_demo* dut) {
    std::printf("LEVEL 2 -- registered (latency 1 cycle, throughput 1/cycle)\n");

    dut->rst_n = 0;
    dut->p_in_valid = 0;
    cycle(dut);
    cycle(dut);
    dut->rst_n = 1;

    // Push a fresh pair every cycle for 5 cycles, then stop offering work.
    const uint32_t as[5] = {0xFF00FF00u, 0x0000FFFFu, 0xAAAAAAAAu, 0x12345678u,
                            0xFFFFFFFFu};
    const uint32_t bs[5] = {0x0F0F0F0Fu, 0x00000F0Fu, 0xFFFF0000u, 0x0000FFFFu,
                            0x80000001u};

    // Outputs are sampled BEFORE each clock edge, so a row shows the inputs
    // being offered this cycle next to the result captured on the previous
    // edge. That one-row skew IS the latency, made visible.
    std::printf("   cyc | in_valid        a        b | out_valid        y\n");
    for (int cyc = 0; cyc < 7; ++cyc) {
        const bool feeding = cyc < 5;
        dut->p_in_valid = feeding;
        dut->p_a = feeding ? as[cyc] : 0;
        dut->p_b = feeding ? bs[cyc] : 0;
        dut->eval();  // settle; registered outputs still hold the last edge

        const int done = cyc - 1;  // the pair that finished
        const bool want_valid = done >= 0 && done < 5;
        if (want_valid) {
            const uint32_t want = as[done] & bs[done];
            expect(dut->p_out_valid == 1, "out_valid should be high");
            expect(dut->p_y == want, "pipelined result value");
        } else {
            expect(dut->p_out_valid == 0, "out_valid should be low");
        }

        if (want_valid)
            std::printf("   %3d |    %d     %08x %08x |     %d      %08x\n",
                        cyc, feeding ? 1 : 0, feeding ? as[cyc] : 0,
                        feeding ? bs[cyc] : 0, dut->p_out_valid, dut->p_y);
        else
            std::printf("   %3d |    %d     %08x %08x |     %d      -------- "
                        "(invalid)\n",
                        cyc, feeding ? 1 : 0, feeding ? as[cyc] : 0,
                        feeding ? bs[cyc] : 0, dut->p_out_valid);

        cycle(dut);
    }
    std::printf("   5 pairs in, 5 results out, one per cycle, shifted by 1.\n\n");
}

// -------------------------------------------------------------------------
// Level 3: the handshake. The producer wants to send constantly; the consumer
// stalls in the middle. Nothing may be lost or duplicated.
// -------------------------------------------------------------------------
void level3(Vand32_demo* dut) {
    std::printf("LEVEL 3 -- valid/ready handshake (backpressure, nothing lost)\n");

    dut->rst_n = 0;
    dut->s_in_valid = 0;
    dut->s_out_ready = 0;
    cycle(dut);
    cycle(dut);
    dut->rst_n = 1;

    // 4 jobs to push. The producer always has more to offer.
    const uint32_t as[4] = {0x0000000Fu, 0x000000F0u, 0x00000F00u, 0x0000F000u};
    const uint32_t bs[4] = {0x000000FFu, 0x000000FFu, 0x00000FFFu, 0x0000FFFFu};

    std::size_t sent = 0;
    std::vector<uint32_t> got;

    std::printf("   cyc | in_v in_r        a | out_v out_r        y | note\n");
    for (int cyc = 0; cyc < 12 && got.size() < 4; ++cyc) {
        // Consumer refuses to take anything on cycles 2..5 -- a stall.
        const bool consumer_ready = !(cyc >= 2 && cyc <= 5);

        dut->s_in_valid = sent < 4;
        dut->s_a = sent < 4 ? as[sent] : 0;
        dut->s_b = sent < 4 ? bs[sent] : 0;
        dut->s_out_ready = consumer_ready;
        dut->eval();  // settle so in_ready/out_valid reflect this cycle

        // Sample the handshakes BEFORE the edge: a transfer happens on this
        // cycle iff both sides agree right now.
        const bool in_fire = dut->s_in_valid && dut->s_in_ready;
        const bool out_fire = dut->s_out_valid && dut->s_out_ready;
        const uint32_t out_y = dut->s_y;
        const int in_v = dut->s_in_valid, in_r = dut->s_in_ready;
        const int out_v = dut->s_out_valid, out_r = dut->s_out_ready;
        const uint32_t shown_a = dut->s_a;

        const char* note = "";
        if (in_fire && out_fire) note = "accept + drain same cycle";
        else if (in_fire) note = "accepted";
        else if (out_fire) note = "drained";
        else if (in_v && !in_r) note = "STALLED: producer blocked";
        else if (!consumer_ready) note = "consumer not ready";

        if (out_fire) got.push_back(out_y);
        if (in_fire) ++sent;

        std::printf("   %3d |  %d    %d    %08x |   %d     %d    %08x | %s\n",
                    cyc, in_v, in_r, shown_a, out_v, out_r, out_y, note);

        cycle(dut);
    }

    std::printf("   received %zu results: ", got.size());
    for (uint32_t v : got) std::printf("%08x ", v);
    std::printf("\n");

    expect(got.size() == 4, "all 4 results survived the stall");
    for (std::size_t i = 0; i < got.size() && i < 4; ++i)
        expect(got[i] == (as[i] & bs[i]), "handshake result in order");
    std::printf("   nothing lost, nothing duplicated, order preserved.\n\n");
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    auto* dut = new Vand32_demo;
    dut->clk = 0;
    dut->rst_n = 0;
    dut->eval();

    level1(dut);
    level2(dut);
    level3(dut);

    dut->final();
    delete dut;

    if (g_failures) {
        std::printf("FAIL: %d checks failed\n", g_failures);
        return 1;
    }
    std::printf("PASS: all three levels behave as documented\n");
    return 0;
}
