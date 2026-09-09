# Measuring performance

Use the small representative all-mode corpus for routine experiments. Keep
pathological cases in correctness tests and the full compatibility corpus.
See [corpora](../bench/corpus/README.md) and [iteration guidance](../AGENTS.md).

## Native and Python checks

Build separate whole revisions with their own headers and matching flags:

```sh
cmake -S . -B build/native
cmake --build build/native --target bench-build -j4
build/native/library_compare /path/to/maps 3 \
  /path/to/baseline/library_native.so /path/to/candidate/library_native.so \
  > build/native.csv
python bench/summarize.py build/native.csv

python bench/python_compare.py /path/to/maps \
  /path/to/baseline-package /path/to/candidate-package --reps 3 \
  > build/python.csv
python bench/summarize.py build/python.csv
```

Use `.dylib` for native modules on macOS. Python package directories must contain
their matching installed extensions. Reuse the same interpreter and dependencies.
`library_compare` measures fresh and reused parsers; `python_compare.py` measures
the eager Python boundary, including result cleanup.

Build before timing. Serialize runs per host, avoid competing work, and pin a
CPU with `taskset` on Linux. Screen cheaply; repeat promising small differences
with reversed variant order and a confirmation sample. Do not infer a speedup
from noise or compare different corpora, API boundaries, or summary statistics.

`summarize.py` reports per-file minima and all-call statistics. Minima are a
lower-envelope estimate, not average user latency. `bench/run.py` can capture
commands, build configuration, and corpus hashes alongside output.

## Profiling and competitors

`profile_parse` preloads a subset and repeats rounds for profiling:

```sh
perf stat -e cycles,instructions,branches,branch-misses -- \
  build/native/profile_parse /path/to/maps 1000 20 fresh
```

Keep [competitor comparisons](../bench/comparison/README.md) for milestone
measurements of practical value. Their API scopes and result shapes differ;
retain the relevant caveats when publishing numbers. Refresh public tables at
milestones or on request, not after every internal optimization.

Generated artifacts belong under ignored `build/` directories. Public timings
in the README and [comparison report](comparison.md) are historical snapshots,
not measurements of the current revision.
