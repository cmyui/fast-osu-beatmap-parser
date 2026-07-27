# fosu in hardware

An FPGA port of the parser. The motivation is the one thing a CPU cannot be
made to do: a CPU is a time-multiplexed circuit that must re-instruct itself
every cycle, and fosu's measured ceiling on Zen 4 is ~0.35 bytes/cycle at
3.7 GHz. An FPGA lays the parser out *spatially* — every stage exists as its
own logic and computes every cycle — so a 16-byte-per-cycle datapath at
400 MHz is ~6 GB/s from a chip drawing ~10 W.

The C++ campaign's central result was that the parser's remaining branches are
near-free because they predict, so the floor is "minimal expected cost under
the real branch distribution" (see the README's entropy-payment chapter). That
theorem is CPU-specific. In hardware there is no fetch to steer: the circle
datapath and the slider datapath both physically exist, both compute every
cycle, and a mux picks the winner. Entropy gets paid in **area** instead of
time.

## Layout

```
rtl/                 synthesizable SystemVerilog
sim/                 Verilator testbenches (C++, use fosu as golden model)
```

Verification reuses the discipline that already governs the C++: the parser
*is* the reference, and RTL output must be bit-identical to it on real ranked
maps. On x86 hosts the testbench closes a three-way equivalence — RTL ==
scalar C++ == AVX2 — because the testbench can call the shipping intrinsics
directly.

```sh
make rtl-lint     # structural lint only, fast
make rtl-test     # build + simulate + diff against the golden model
make rtl-test RTL_CORPUS=/path/to/osu/files
```

Needs `brew install verilator` (macOS) or `apt install verilator`. Not part of
`make all`: the C++ build must never depend on an RTL toolchain.

## Modules

| module | what it is |
|---|---|
| `fosu_classify.sv` | combinational byte classifier — `nondigit`/`comma`/`newline` masks, `WIDTH` lanes in parallel |
| `fosu_classify_stage.sv` | the same logic behind one row of flip-flops: a pipeline stage with a `valid` tag |

`fosu_classify` is the hardware form of `nondigit_mask32()`/`comma_mask32()`.
Note what disappears in translation: the C++ biases bytes by 80 and does a
signed compare because AVX2 has no unsigned byte compare. Hardware just writes
the range test. The mask sense (`nondigit`, not `digit`) is kept inverted to
match the C++ exactly, so the golden-model diff is literal rather than
interpreted.

## Reading the RTL as a software engineer

Four inversions that account for most of the confusion:

1. **Nothing is called.** There is no program counter and no dispatch. Every
   module instance is a permanently powered circuit; data flows through it.
   `fosu_classify`'s 32 lanes are 32 physical copies of the comparator logic,
   all evaluating simultaneously — where software loops, hardware replicates.
2. **Parallel is free; serial costs design effort.** The reverse of software.
3. **Flip-flops are a global shutter.** Every FF on the chip samples its input
   on every rising clock edge, unconditionally. Between edges, combinational
   logic ripples and settles; the edge commits. A pipeline is a conveyor belt
   where each stage works on a different line every cycle.
4. **Cost is area and critical path, not instruction count.** One line can
   infer a 2,000-LUT barrel shifter; fifty lines of state machine can
   synthesize to nothing. `assign` = wires and gates, `<=` inside `always_ff`
   = flip-flops.

## Roadmap

- **Phase 0 — toolchain (done).** Classifier + golden-model harness. Proves
  the loop: exhaustive 256-value × 32-lane equivalence, the `valid` contract,
  and 120k real-corpus windows diffed bit-exactly.
- **Phase 1 — prefix core.** The AVX2 hitobject kernel as hardware: 32-byte
  window in, `{x, y, time, type, hitSound}` out over 3-5 stages. The
  `blsr`/`tzcnt` chain becomes priority encoders, the `kLaneMasks` table
  becomes a mux network, the index polynomial becomes an adder tree.
- **Phase 2 — streaming skeleton.** 16 B/cycle ingest, the realignment barrel
  shifter (fixed-width bus → variable-length line records; typically the
  timing bottleneck in any wire-speed parser), line framer, section FSM.
- **Phase 3 — full format.** Timing points (shape-homogeneous, so an easy
  datapath), the slider point sub-pipeline (the rate limiter, exactly as on
  CPU), and a punt-to-host record for structurally odd lines — the scalar
  fallback, in silicon. Target: ≥8 bytes/cycle sustained on real maps.
- **Phase 4 — silicon.** Synthesize for Kria KV260 (Zynq UltraScale+), read
  real timing reports, split stages until 300-400 MHz closes, DMA to the ARM
  side, and measure joules per beatmap.
- **Phase 5 — endgame.** Beatmaps parsed at Ethernet wire speed; an AWS F2
  port; and the prefix core shrunk onto a TinyTapeout shuttle, which would be
  an actual fabricated chip whose only purpose is parsing osu! beatmaps.
