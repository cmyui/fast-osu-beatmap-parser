# Comparing public parser APIs

These adapters measure practical public entry points, not a shared internal
representation. See [the comparison report](../../docs/comparison.md) for the
scope differences that must accompany the numbers.

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
  "$b/variants.json" "$b/results.jsonl" --warmup 64 --rounds 2 --reps 1
python3 bench/comparison/report.py "$b/results.jsonl" "$b/summary.json"

# Python headline: one API per fresh process, two complete batch passes.
taskset -c 3 "$b/venv/bin/python" bench/comparison/python_batch.py /path/to/corpus \
  "$b/variants.json" "$b/summary.json" "$b/python-batch.json"
```

The driver hashes sorted filenames and each file's SHA-256 to identify the
corpus. To repeat the published numbers, match that fingerprint, not just its
file count. A different local `.osu` collection is useful but is a different
benchmark. The existing 10k corpus is described in
[performance.md](../../docs/performance.md#method-and-target); it is not bundled.
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
  stacking is explicitly disabled; no PP or difficulty calculation is requested.
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
