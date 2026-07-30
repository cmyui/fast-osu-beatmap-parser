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
| `fosu_ffs.sv` | find-first-set via prefix-OR (log depth, not a borrow chain) |
| `fosu_first4.sv` | positions of the first four set bits — the `blsr`/`tzcnt` chain, as a parallel-prefix rank network |
| `fosu_shift_right.sv` | barrel shifter, so "count digits from offset k" becomes "from 0" |
| `fosu_prefix.sv` | `x,y,time,type,hitSound` → integer fields; bit-exact and domain-exact vs the AVX2 path |
| `fosu_numscan.sv` | the one shared numeric scanner: serves `parse_i64` and `parse_double` from the same window |
| `fosu_kv_key.sv` | the C++ 4-byte key dispatch as a mux; all 37 key/value fields |
| `fosu_line_iter.sv` | cursor-based line iteration + section dispatch, over a valid/ready handshake |
| `fosu_engine.sv` | the parser: composes the above and parses every section |

### How the doubles come out exact

`TimingPoint.time`/`beat_length`, `Slider.length`, break times and the decimal
key/value fields are all C++ `double`. There is no floating-point unit in this
design and none is needed. `parse_double` finishes with exactly two operations:

```cpp
double v = (double)mant;  if (frac) v /= kPow10[frac];  out = neg ? -v : v;
```

so the RTL emits `(mant, frac, neg)` — *parse_double's own inputs* — and the host
performs those two operations. The result is **bit-identical**, and the
testbench compares raw 64-bit patterns rather than tolerances. Values the C++
itself hands to `strtod` (over 18 significant digits, or an exponent) are not
approximated either: the line is emitted as a PUNT record carrying its span and
the host parses it, the same division of labour as the C++ scalar fallback.

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

### Verified 1:1 on the full production corpus

`fosu::parse` is the reference; every field it fills is produced by the RTL and
diffed, with doubles compared as **raw 64-bit patterns** rather than tolerances.
Run across 8 shards on the benchmark box:

| | count |
|---|---|
| files | 10,011 |
| bytes | 437,853,165 |
| hit objects | 8,730,940 |
| sliders | 2,673,764 |
| slider control points | 6,136,191 |
| timing points | 1,158,886 |
| key/value fields | 294,997 |
| storyboard lines counted | 891,818 |
| combo colours | 36,494 |
| background / video strings | 11,319 |
| breaks | 8,142 |
| format-version lines | 10,011 |
| malformed lines | 9 |
| lines deferred to the host | 7 |

**Zero mismatches.** The hit-object total matches the corpus census exactly. The
7 deferred lines are decimals with more than 18 significant digits — the same
values the C++ itself hands to `strtod` — emitted as PUNT records carrying their
span, which is the scalar fallback's job done in the same division of labour.

`make rtl-test-engine RTL_CORPUS=<dir>`, or `RTL_CORPUS=sim/fixtures` for the
one-second regression set.

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
- **Phase 2 — line iteration and section dispatch (done).** Cursor-based, over a
  memory-resident buffer, 1:1 with the C++ line loop on the full 10k corpus:
  10,011 files, 437,853,165 bytes, 11,298,709 lines.
- **Phase 3 — all sections (done).** `[TimingPoints]`, `[HitObjects]` (circles,
  sliders with control points and extras, spinners, mania holds), the four
  key/value sections, `[Events]` (background, video, breaks, storyboard counts),
  `[Colours]`, and the format-version line. Every field `fosu::parse` fills is
  produced and diffed, doubles compared as raw 64-bit patterns.
- **Phase 3b — throughput.** The engine currently sustains ~3.2 bytes/cycle in
  simulation. That is a *behavioural* design: one memory port shared between the
  line iterator and the content parser, a combinational read whose latency is
  not pipelined in, and a control-point walk costing ~5 cycles per point. The
  work to raise it is known and independent of correctness -- register the
  memory path, give the iterator its own port so line scanning overlaps content
  parsing, and widen the point walk.
- **Phase 4 — silicon.** Synthesize for Kria KV260 (Zynq UltraScale+), read
  real timing reports, split stages until 300-400 MHz closes, DMA to the ARM
  side, and measure joules per beatmap.
- **Phase 5 — endgame.** Beatmaps parsed at Ethernet wire speed; an AWS F2
  port; and the prefix core shrunk onto a TinyTapeout shuttle, which would be
  an actual fabricated chip whose only purpose is parsing osu! beatmaps.
