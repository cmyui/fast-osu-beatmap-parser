# Routine validation and performance experiments

Use small, fixed profiles for iteration. Correctness tests exercise edge cases;
performance measurements represent commonly played maps. Do not turn the timing
workload into a checklist of malformed inputs or rare format features.

## Fixed profiles

The [routine selection](../bench/corpus/routine-v1.json) pins source files and
hashes. Materialize it once using Python 3.11+:

```sh
python bench/corpus/routine.py materialize bench/corpus/routine-v1.json \
  /path/to/full-corpus build/routine
```

| Profile | Size | Purpose |
|---|---|---|
| `performance` | 1,024 play slots; 256 per mode | Routine paired before/after timing |
| `confirmation` | 1,024 independent play slots; 256 per mode | Confirm without tuning against one sample |
| `compatibility` | 128 distinct maps; 32 per mode | Real-file smoke checks alongside unit tests |

Performance slots sample **Akatsuki lifetime playcounts within the popular
ranked/approved corpus**, separately by native mode. This is a popularity proxy,
not current global osu! traffic, unique players, or every FOSU deployment. The
source pool is bounded: roughly 10,000 standard maps and 2,000 per other mode.

Within each mode, maps are grouped by file-size bands, slider/hold-share bands,
and timing-row-count bands, then ordered by size. Size/count bands use powers of
two; object-share bands use quarters. Cumulative playcount is divided into 256
equal-probability intervals; a fixed seeded draw selects one play per interval.
Bands group similar parsing workloads, but get slots according to their actual
play frequency, not equal quotas. Timing rows are counted from source text,
independently of FOSU. This samples the naturally occurring mix without forcing
equal numbers of short/long maps or rare object patterns. Popular maps may fill
multiple slots; **do not deduplicate them**. Confirmation can overlap the primary
profile: independent selection is not a disjoint holdout. Neither selection
looks at parser timings or parser success.

Report **each mode separately**. The equally sized mode buckets produce an
equal-mode average, not player-traffic-weighted latency. Do not infer traffic
shares from differently truncated corpus buckets. Compatibility instead uses
distinct, size-spread maps; it does not determine performance weights.

The [v1 calibration](../bench/corpus/routine-v1-quality.json) compares both samples
with play-weighted population means. Byte sizes and object counts are within
about 4.1%, timing-row counts within about 5.1%, and standard/catch slider and
mania hold means within about 3.9%. Rare taiko sliders have larger relative
variation (fewer than one per map); they are not given an artificial quota.
These are workload-mix checks, not confidence intervals for execution time.

## Iteration ladder

1. **Build incrementally**, only affected targets. Keep old/new artifacts in
   separate directories. Never replace a library loaded by a running process.
   Reuse the official decoder build and development Python build instead of
   rebuilding/reinstalling them for every experiment.
2. **Check correctness first.** Run native unit tests, including scalar/SIMD
   equivalence. For Python changes, run its tests and strict mypy. Use
   `compatibility` for native/Python full-value checks and, when semantics change,
   the official audit. Synthetic fixtures, fuzzing and sanitizers cover malformed
   input, ordering, lifetimes, boundaries and unusual valid records. They do not
   need artificial representation in the performance profile.
3. **Screen the changed boundary** on `performance`: usually C++ fresh/reused
   parsing on the affected ISA, not every language/backend/machine. Use seven
   native repetitions, rotating baseline/candidate within maps, then reverse
   their order in a second process. For binding changes add eager Python
   bytes/file calls with three repetitions and normal GC; C changes need C API
   measurements. Target seconds of timing and under a minute for ordinary smoke
   checks after incremental builds, not a mandatory large sweep.
4. **Confirm promising changes** on `confirmation` with the same paired method.
   Check every mode and fresh/reused parsing. Before accepting shared hot-loop
   changes, confirm ARM, x86 and scalar, and check public interfaces once.
   Use identical-code controls for marginal wins. Below roughly 1%, consider
   results inconclusive unless repeated controls support them; a larger corpus
   cannot repair a biased/noisy setup.
5. **Escalate deliberately.** Run full-corpus compatibility once for a completed
   semantic/memory-lifetime change group before merge, not each scheduling
   experiment. Run full-corpus performance for published README/release claims,
   deliberate workload refreshes, or disagreement between small profiles. Do not
   repeat the matrix for formatting, documentation or test-only changes.

Finish builds/audits before timing; serialize benchmarks per host and pin Linux
runs to a core. Retain revisions/artifact hashes, selection hash, compiler flags,
Python version, repetitions and both orders. Report mean per-slot minima and
retain all-sample statistics. These paired diagnostics are not interchangeable
with the independent batch method used by published third-party comparisons.

## Commands

Existing drivers accept the materialized directories. With before/after builds
prepared (use `.dylib` on macOS and `taskset -c <core>` on Linux):

```sh
build/after/library_compare build/routine/performance 7 \
  build/before/library_native.so build/after/library_native.so > build/forward.csv
build/after/library_compare build/routine/performance 7 \
  build/after/library_native.so build/before/library_native.so > build/reverse.csv
python bench/summarize.py build/forward.csv \
  --corpus-manifest build/routine/performance.csv
python bench/summarize.py build/reverse.csv \
  --corpus-manifest build/routine/performance.csv
PYTHONPATH=/path/to/candidate python tests/verify_python.py \
  build/after/reference_native build/routine/compatibility
PYTHONPATH=/path/to/candidate python tests/test_official_values.py \
  --corpus build/routine/compatibility --report build/official-smoke.json
```

Use `confirmation` and its matching manifest when confirming a candidate. Keep
the native reference/Python backend consistent for path-counter comparisons.
The official decoder must already be built; its comparison scope is documented
in the [reference audit](../tests/reference/official/README.md).

## Refresh policy

Do not resample per experiment or search seeds for favourable results. Refresh
the versioned selection only with a deliberate workload refresh. Export
`beatmap_id`, native `mode`, and lifetime `playcount` read-only for the source
manifest, filtering to ranked/approved records. No player/score records are
needed. Missing, mismatched or nonpositive counts are reported, not fabricated.

```sh
python bench/corpus/routine.py freeze /path/to/manifest.csv \
  /private/playcounts.tsv /path/to/full-corpus /path/to/new-selection.json
```

Review weighted size/object mix against the population, then pin the selection.
Keep previous results tied to their original profile. Raw maps/database exports
stay outside git; the selection contains IDs, hashes and the resulting slots.
