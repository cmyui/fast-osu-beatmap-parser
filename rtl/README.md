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
make rtl-stat     # Yosys: gate counts and longest path (RTL_TOP=<module>)
```

Needs `brew install verilator` (macOS) or `apt install verilator`, plus
`yosys` for `rtl-stat`. Not part of `make all`: the C++ build must never depend
on an RTL toolchain.

The RTL is **portable Verilog** — no SystemVerilog size casts or other
constructs Yosys's frontend rejects. Vivado would accept the sugar, but staying
portable keeps the fast area/depth measurement loop and the open-source ASIC
flow (TinyTapeout) available, which is worth more than the syntax.

## Modules

| module | what it is |
|---|---|
| `fosu_classify.sv` | combinational byte classifier — `nondigit`/`comma`/`newline` masks, `WIDTH` lanes in parallel |
| `fosu_classify_stage.sv` | the same logic behind one row of flip-flops: a pipeline stage with a `valid` tag |
| `fosu_first4.sv` | positions of the first four set bits — the `blsr`/`tzcnt` chain, as a parallel-prefix rank network |
| `fosu_prefix.sv` | `x,y,time,type,hitSound` → integer fields; bit-exact and domain-exact vs the AVX2 path |

### What the prefix core does *not* need

The C++ carries a 64-byte-per-entry `consteval` table of `vpermd` + `vpshufb`
masks, indexed by the mixed-radix length signature
`60*p0 + 27*p1 + 2*p2 + p3 - 158`. Its entire job is sliding each
variable-width field into a fixed lane so one `maddubs`/`madd` chain can
convert everything at once — necessary only because AVX2 cannot shift each lane
by its own amount.

In hardware a digit at a computed index is just a mux, so the table, the index
polynomial, the permute and the shuffle all disappear. Fields are extracted
right-aligned (ones digit adjacent to the delimiter), which makes a
variable-length field a fixed set of muxes with unused high digits forced to
zero — no normalization step at all. The hardware port is *simpler* than the
code it replaces.

### Measured (Yosys, generic 2-input gate mapping)

| version | cells | longest path |
|---|---|---|
| first working version | 8,735 | 135 levels |
| + parallel-prefix rank | 9,402 | 116 levels |
| + time adder tree | 9,376 | **99 levels** |

Two lessons, both from measuring rather than reasoning. The rank ripple looked
like the obvious culprit and was worth only 19 levels; the real chain was
`conv_time` accumulating ten 34-bit terms in a loop, which pairwise summation
cuts from 10 dependent adds to 4. 99 levels is still far too deep for 400 MHz
in one cycle — that is what pipelining is for, and where the register
boundaries go is the next design decision, not a combinational problem.

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
- **Phase 1 — prefix core (done, combinational).** `x,y,time,type,hitSound`
  from a 32-byte window. Verified on 270,748 candidates: all 70,732 real
  hitobject lines in the corpus (100% fast-path acceptance), 16 directed
  domain-boundary cases, and 200k mutation-fuzzed lines. Two invariants hold --
  *accepts => fields bit-identical to the scalar reference*, and on x86 the
  accepted **domain** matches `fast_parse_prefix` exactly, line for line,
  including `next_off` and the newline mask.
- **Phase 1b — pipeline the prefix core.** Split the 99 combinational levels
  across register stages and re-measure. Natural boundaries:
  classify+positions / digit selection / conversion+validate.
- **Phase 2 — streaming skeleton.** 16 B/cycle ingest, the realignment barrel
  shifter (fixed-width bus -> variable-length line records; typically the
  timing bottleneck in any wire-speed parser), line framer, section FSM.
  Note the framer owns line termination: the prefix harness had to model this
  explicitly, because the SIMD path treats CR/LF as terminators while the
  scalar reference does not, and only a framer that ends lines at the newline
  makes the two agree.
- **Phase 3 — full format.** Timing points, the slider point sub-pipeline (the
  rate limiter, exactly as on CPU), key/value fields for
  General/Metadata/Difficulty, and a punt-to-host record for structurally odd
  lines -- the scalar fallback, in silicon. Target: >=8 bytes/cycle sustained.
  - Open design question: `TimingPoint.time`/`beat_length` and `Slider.length`
    are C++ `double`. Exact decimal->binary64 conversion in hardware needs wide
    integer work for no benefit here, so the intended design is to emit the
    decimal mantissa and exponent as integers and let the host finish the float
    conversion -- parse the structure in silicon, defer the FP. That also keeps
    the golden-model diff exact on integers instead of approximate on floats.
- **Phase 4 — silicon.** Synthesize for Kria KV260 (Zynq UltraScale+), read
  real timing reports, split stages until 300-400 MHz closes, DMA to the ARM
  side, and measure joules per beatmap.
- **Phase 5 — endgame.** Beatmaps parsed at Ethernet wire speed; an AWS F2
  port; and the prefix core shrunk onto a TinyTapeout shuttle, which would be
  an actual fabricated chip whose only purpose is parsing osu! beatmaps.
