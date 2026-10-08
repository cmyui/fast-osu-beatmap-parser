# Corpora

Use a small representative all-mode sample for routine performance comparisons,
and the full compatibility corpus for broad correctness checks. Raw maps and
generated reports stay outside version control.

The current performance snapshot uses 1,024 entries (986 unique beatmaps), 256
per mode and 46,029,610 bytes. Selection is stratified by file size,
slider/hold share and timing-row count from popular ranked/approved Akatsuki
maps; repeated entries are deliberate products of that selection. Its SHA-256 is
`1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895`.

The lazer performance snapshot uses 100 real v128 maps: 8 osu!standard, 43
osu!taiko, 18 osu!catch and 31 osu!mania maps, totalling 2,634,547 bytes. Its
SHA-256 is `478a7c7753242f37a87093e919d25fced19833013578c47dbb6e184c4c8b65f2`.
This corpus is intentionally reported separately: its smaller files and different
mode distribution make absolute latency comparisons with the legacy profile
misleading.

The retained compatibility corpus contains 15,952 ranked/approved maps:
9,952 standard and 2,000 each of taiko, catch and mania. Selection used playcount;
it is not a representative traffic-weighted distribution.

Pass the same directory to each benchmark variant. Compare only runs with the
same files, host, compiler and timing boundary. Supply a corpus manifest to the
competitor harness's `--corpus-manifest` option when available.

See [performance](../../docs/performance.md) for FOSU checks and
[competitor comparisons](../comparison/README.md) for the cross-library harness.
