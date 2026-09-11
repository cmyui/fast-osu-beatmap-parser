# Comparing public parser APIs

These adapters measure practical public entry points, not a shared internal
representation. Comparisons are organized by requested outcome; see the
[comparison report](../../docs/comparison.md) for the exact result contracts.

## Reproduce on Linux/x86-64

Requirements: CPython 3.12, GCC 13.3, Node 20/npm 10, Rust 1.98.1 and .NET SDK
10.0.400; `curl`, `sha256sum`, `taskset`, and an AVX2-capable CPU. The benchmark
dependencies are isolated from FOSU's runtime dependencies. Package versions
and Cargo/npm/NuGet dependency graphs are pinned here. Use an isolated benchmark
environment: some historical libraries bring old transitive dependencies.

From the source revision being measured:

```sh
b="$PWD/build/comparison"
bash bench/comparison/prepare.sh "$b"
python3 -m unittest discover -s bench/comparison -p 'test_*.py'

# Smoke test all adapters before the long run. Outputs must not already exist.
taskset -c 3 "$b/venv/bin/python" bench/comparison/run.py /path/to/corpus \
  "$b/variants.json" "$b/smoke.jsonl" --limit 20 --warmup 8 --rounds 1
python3 bench/comparison/report.py "$b/smoke.jsonl" "$b/smoke-summary.json"

# No builds or competing benchmarks while this runs.
taskset -c 3 "$b/venv/bin/python" bench/comparison/run.py /path/to/corpus \
  "$b/variants.json" "$b/results.jsonl" --warmup 64 --rounds 2 --reps 1 \
  --corpus-manifest /path/to/manifest.csv
python3 bench/comparison/report.py "$b/results.jsonl" "$b/summary.json"

# Python headline: one API per fresh process, two complete batch passes.
taskset -c 3 "$b/venv/bin/python" bench/comparison/python_batch.py /path/to/corpus \
  "$b/variants.json" "$b/summary.json" "$b/python-batch.json" --table python_all_modes
```

The driver hashes sorted filenames and each file's SHA-256 to identify the
corpus. To repeat the published numbers, match that fingerprint, not just its
file count. A different local `.osu` collection is useful but is a different
benchmark. The default mixed-mode corpus is described in
[the corpus guide](../corpus/README.md); it is not bundled.
The `.corpus.csv.gz` manifest publishes file IDs, sizes and content hashes so
that downloaded copies can be checked against the measured inputs.

The reporter also writes a compressed `.samples.csv.gz` next to the summary.
It preserves every timing, count, traversal checksum and error type, but omits
exception messages and local executable paths. Its `ns` cells contain JSON
arrays, one element per repetition. Both successful and excluded maps remain
available for independent analysis.

`variants.json` contains local executable paths and backend selections. Edit it
to run a subset, but retain `fosu-python-avx2` with the same workloads as the
other variants: the reporter uses its decoded object counts as a *cohort filter*,
not as proof that another parser is wrong. Each table intersects matching,
successful files across every included variant and both rounds.

When a mode manifest is supplied, coverage includes separate standard, taiko,
catch, and mania counts. The report adds `_all_modes` and `_mode_0` through
`_mode_3` tables. A variant's optional `modes` list declares supported native
modes. Unsupported variants are excluded from the corresponding mode tables,
not silently allowed to eliminate every map in that mode. All variants are still
attempted in the raw sweep. A table with no common successful maps has no timings.
Every nonempty table uses one shared cohort; different tables may have different
cohorts and must not be mixed for speedups.

Use `python_batch.py --table python_mode_0` for a standard-only comparison.
`--table python_all_modes` excludes mode-limited libraries and uses the same
mixed-mode cohort for every remaining Python API.

### Geometry-ready Python comparison

Use a standard-only corpus and retain only `fosu-python-avx2`,
`fosu-python-scalar` and `slider` in a copy of `variants.json`. Set
`FOSU_BENCH_PROFILE=geometry` in each FOSU variant's environment. This makes
FOSU calculate slider end times and paths; slider already calculates end times
and constructs curve objects during its normal parse. Stacking remains disabled
for both parsers.

Run `run.py` and `report.py` normally, then use the resulting `python` cohort
with `python_batch.py`. Do not set the profile for slider: it names a FOSU option
bundle, not a cross-library switch. The report must describe the comparable
outcome and the different path representations.

### Refresh only FOSU

Keep the same build settings and corpus, and retain only the FOSU variants in
`variants.json`. Use the original report to fix each table's cohort, rather than
expanding to more maps because fewer libraries are participating:

```sh
taskset -c 3 "$b/venv/bin/python" bench/comparison/run.py /path/to/corpus \
  "$b/variants.json" "$b/refresh.jsonl" --warmup 64 --rounds 2 --reps 1
python3 bench/comparison/report.py "$b/refresh.jsonl" "$b/refresh-summary.json" \
  --cohorts /path/to/original-summary.json
taskset -c 3 "$b/venv/bin/python" bench/comparison/python_batch.py /path/to/corpus \
  "$b/variants.json" /path/to/original-summary.json "$b/refresh-batch.json" --table python_all_modes
```

The reporter rejects a changed corpus or any new failure/count mismatch within
the fixed cohort. It records the original report's hash. Keep the new evidence
separate and disclose retained competitor measurements: a FOSU-only interleaved
run has different cache interference from the multi-runtime sweep, even though
the timed API, corpus and summary statistic are unchanged.

## Measurement contract

The Python headline uses `python_batch.py`, not the interleaved per-call means.
Each library/API/pass gets a fresh process, preloads the common Python cohort,
warms on 64 maps three times, then times one complete loop. Normal GC and loop
bookkeeping are included. Imports, preload, warmup and process teardown are
excluded. All per-map object counts must agree across batches. The second pass
reverses API order; both pass times and the cohort fingerprint are retained.
Bytes and file workloads never warm one another in the same process. The
interleaved experiment exposed a substantial order effect between those calls;
its Python measurements remain available as evidence, not the headline.

The following describes the supplementary interleaved `run.py` experiment:

- Persistent, independent worker processes inherit the driver's CPU affinity.
  Imports, JIT warm-up, JSON IPC, input pre-reading, and report serialization
  are outside the timer. Each worker warms on 64 evenly spaced maps, three
  parses each. File order is sorted; worker/workload order rotates per map and
  reverses in the second pass.
  Interleaving separate runtimes does not keep each runtime's CPU caches hot;
  these measurements are distinct from isolated tight-loop benchmarks.
- `bytes`: decode resident input, read the object count, and release the result.
  Required UTF-8 conversion, native boundary copies, allocation and immediate
  cleanup are inside the timer. No result is cached between calls. Native
  parsers are freshly constructed; libraries' normal internal pools remain on.
- `file`: the library's public file API, including opening/reading/closing a
  **warm page-cache** file. This is not cold disk performance. Published
  OsuPyParser only offers this input boundary; no artificial bytes API is added.
- `visit`: `bytes` plus summing every hitobject's start time using public Python
  record access. FOSU's records are already eager Python values. `slider`
  stacking is explicitly disabled. No PP or difficulty calculation is requested.
  The optional FOSU geometry profile affects parsing but not this traversal.
- Normal garbage collection remains enabled. We retain **all** timed samples,
  including slow ones and collection that occurs during measured work. We do
  not force a collection after each parse; deferred collections outside the
  timer are not attributed to it. This is warm per-call latency, not total
  end-to-end batch wall time or a memory-footprint benchmark.
- A decode exception is a failed map, not a fast sample. A worker exceeding
  10 seconds per request or exiting is recorded as a failure and restarted.
  This is a practical runtime budget, not proof the parser could never finish.
  It can be increased with `--timeout`; the configured budget is recorded.
  Restarts are disclosed in coverage; subsequent calls must warm the new process
  naturally. Unexpected protocol errors abort the run. Incomplete runs cannot
  produce a publishable report.
- Means use all samples on each table's common cohort, with equal weight per
  map. Throughput is total cohort bytes divided by total measured time. Per-pass
  means expose run variation; they are not confidence intervals. The reporter
  also preserves full-corpus failures and count/checksum disagreements.

Matching counts (and the traversal checksum) are only a basic sanity check.
They do not establish identical metadata, numeric precision, acceptance rules,
or rich slider representations. Do not turn these results into an unqualified
claim that the libraries perform identical work.
