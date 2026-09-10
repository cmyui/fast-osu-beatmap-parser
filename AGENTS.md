# Working on FOSU

FOSU is in active, pre-release development with no production consumers.
Interfaces may break. Optimize for useful code changes, not release-level polish.

## Design and scope

- Prefer readable, domain-accurate code, explicit ownership, and simple inputs
  and results. Favor returning parsed values over mutable output parameters
  where practical. Near-zero-cost abstractions should earn their complexity.
- Prefer straightforward code in which each statement performs one obvious
  operation. Keep state explicit and derive it from one source of truth. Extra
  lines are preferable to compressed control flow, synchronized cached state,
  clever syntax, or helpers that combine several conceptually distinct steps.
- Start with the simplest literal implementation of the domain operation. Add
  specialized machinery only after measurement shows that it matters, and keep
  the simple data flow recognizable when optimizing it.
- Change interfaces directly and update affected callers. Do not preserve aliases,
  old formats, or speculative extension points unless explicitly requested.
- Before editing, define the question and briefly trace the affected implementation,
  callers, tests, and build boundaries. Do not turn focused work into a repo audit.
- Preserve unrelated user changes. Do not add optional refinements while preparing
  a passing change for merge; mention worthwhile follow-ups separately.

## Verification proportional to the change

- During iteration, build the affected target and run focused tests.
- For internal parsing refactors, start with focused tests and the small all-mode
  compatibility sample. Use broader official-parser/full-corpus checks for
  meaningful acceptance, normalization, or representation changes once settled.
- Keep correctness tests, strict Python typing, and memory-safety checks. Arena,
  lifetime, bounds, and SIMD-load changes warrant relevant sanitizer checks.
- Test the relevant platform first. Reserve cross-platform and installed-package
  checks for settled candidates whose changed boundaries warrant them, or releases.
- Docs-only changes need a content/link review, not builds, wheel installs,
  compatibility sweeps, or performance measurements.
- Reuse still-valid results. Re-run affected checks after fixes and report skipped
  checks honestly; a sample is not proof of full compatibility.

## Performance experiments

- Screen one hypothesis cheaply on the relevant CPU with the small representative
  all-mode performance corpus. Keep pathological cases in correctness tests.
- Reject clear losers early. Use reversed-order runs, confirmation samples, the
  second machine, and combinations only for promising candidates.
- Compare identical API boundaries, outputs, corpora, build settings, and hosts.
  Separate native parsing from eager Python conversion. Build before timing and
  avoid competing builds, tests, or benchmarks on the measurement host.
- Do not chase tiny uncertain wins indefinitely, especially if they add complexity.
  Report uncertainty; cleaner code can be worth more than a marginal speedup.
- Preserve competitor benchmarks. Rerun and publish comparisons at meaningful
  milestones or when requested, not after every internal optimization.
- Keep experimental binaries and raw reports in ignored build directories.
  Rejected experiments need a concise finding, not permanent tooling or documents.

## Documentation and tooling budget

- Maintain a short README, working usage examples, and important contracts and
  limitations. Update docs only when needed to avoid misleading users or broken
  instructions; do not synchronize implementation narratives after every refactor.
- Do not refresh public timing tables, expand API field inventories, or create
  architecture/history documents as routine completion work. Clearly label stale
  measurements; do not present them as current.
- Put experiment history and migration details in PR descriptions, not maintained
  docs. Prefer deleting redundant prose to repeatedly updating it.
- Reuse existing tooling. Add automation only for demonstrated recurring friction;
  prefer one small entry point over a framework or overlapping scripts.
