# Working on FOSU

Optimize for useful, reviewable changes with minimal iteration overhead.
Readability, domain correctness, and performance all matter; prefer simple,
near-zero-cost improvements over machinery that does not earn its complexity.

## Before editing

- Define the question this iteration answers and what would make it acceptable.
- Briefly trace affected boundaries: implementation, callers, Python bindings,
  tests, CMake/install rules, CI, benchmarks, and documentation. Inspect only
  relevant paths; do not turn a focused change into a whole-repo audit.
- Check the current branch and worktree. Preserve unrelated user changes.
- Use domain-accurate names, explicit ownership, and clear inputs and results.
  Prefer returning parsed values over mutable output parameters where practical.
  Do not introduce compatibility layers or speculative abstractions without a
  concrete requirement.

## Iterate cheaply, verify progressively

1. **During implementation:** build the affected target and run focused tests.
   Avoid full builds, wheel installs, and corpus sweeps after every small edit.
2. **Once the approach works:** run relevant regression tests and the small
   all-mode compatibility sample. Parsing changes must preserve supported
   behavior or explicitly account for intended differences.
3. **Before submission:** run the checks warranted by the changed boundaries.
   Packaging changes need installed-consumer checks; shared engine changes need
   scalar/SIMD coverage; platform-dependent changes need the affected platforms.
   Run broad official-parser/full-corpus validation for meaningful parsing or
   normalization changes, after the candidate settles—not for docs-only edits.

Reuse valid results when subsequent edits cannot affect what they checked.
Re-run affected checks after a fix. State what was tested and what was not;
never substitute a build or a small sample for broader compatibility proof.

## Performance experiments

- Start with one hypothesis and a cheap comparison on the relevant CPU, using
  the small representative all-mode performance corpus. Keep edge-case coverage
  in correctness tests rather than distorting the performance workload.
- Reject clear losers early. Reserve reversed-order runs, confirmation samples,
  and the second machine for promising candidates. Test promising combinations
  after understanding their individual effects.
- Compare the same API boundary, outputs, corpus, build settings, and host.
  Separate native parsing from eager Python conversion. Build before timing;
  serialize benchmarks per host and avoid competing builds or tests.
- Treat small differences as uncertain until repeated. Spend more effort on a
  tiny readable improvement than an equally marginal, complicated optimization.
- Keep competitor benchmarks available and rerun them for meaningful public
  comparisons, not every internal experiment. Never mix incompatible timings.
- Keep experimental binaries, raw reports, and rejected prototypes under ignored
  build directories. Failed experiments need a concise finding, not permanent
  production code, scripts, or historical documents.

## Finish without expanding scope

- Update maintained docs in one pass once the interface settles. Describe the
  current system; put experiment history and migration notes in the PR.
- Do not add optional refinements while preparing a passing change for merge.
  Record worthwhile follow-ups separately.
- Reuse existing test and benchmark entry points. If repeated orchestration
  genuinely warrants automation, prefer one small entry point over a framework
  or another collection of overlapping scripts.
- Report the outcome, relevant evidence, and remaining uncertainty concisely.
  Do not claim a performance improvement from noise or unmeasured intuition.
