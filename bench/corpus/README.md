# Corpora

Use a small representative all-mode sample for routine performance comparisons,
and the full compatibility corpus for broad correctness checks. Raw maps and
generated reports stay outside version control.

The retained compatibility corpus contains 15,952 ranked/approved maps:
9,952 standard and 2,000 each of taiko, catch and mania. Selection used playcount;
it is not a representative traffic-weighted distribution.

Pass the same directory to each benchmark variant. Compare only runs with the
same files, host, compiler and timing boundary. Supply a corpus manifest to the
competitor harness's `--corpus-manifest` option when available.

See [performance](../../docs/performance.md) for FOSU checks and
[competitor comparisons](../comparison/README.md) for the cross-library harness.
