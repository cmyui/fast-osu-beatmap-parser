# Benchmark corpora

Use the versioned performance corpus for routine measurements and the larger
private compatibility corpus for broad correctness checks. Benchmark results
identify a corpus version and content fingerprint; a file count alone is not a
reproducible input.

## Performance v1

`performance-v1` contains 1,024 weighted entries (986 unique beatmaps), 256 per
game mode and 46,029,610 bytes. Repeated beatmaps are deliberate products of
the stratified selection and must remain repeated. The sample covers ordinary
file sizes, slider/hold shares and timing-row counts among popular
ranked/approved maps.

The exact entry order, mode, source beatmap filename, byte count and content
hash are tracked in [`performance-v1.csv`](performance-v1.csv). Materialize the
corpus from its immutable GitHub release asset with:

```sh
python bench/corpus/fetch.py build/corpus/performance-v1
```

The downloader verifies the archive and every extracted map. To verify a local
copy of the release asset instead of downloading it:

```sh
python bench/corpus/fetch.py build/corpus/performance-v1 \
  --archive /path/to/fosu-performance-v1.tar.gz
```

| Property | Value |
|---|---|
| Release | `benchmark-corpus-v1` |
| Archive | `fosu-performance-v1.tar.gz` |
| Archive SHA-256 | `6ee072ee92e1dcc6c0ef92b49cc0ab3734755bc7216d50b75a7a2b1e3730d260` |
| Manifest SHA-256 | `19cac76576a245be37a7260dc50a25482d3eb01941dbd41367abfae2163d10e5` |
| Corpus fingerprint | `1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895` |

The corpus fingerprint hashes each sorted entry name together with the SHA-256
of its bytes. Published `performance-v1` results always mean this exact input.
Any selection or file-content change requires a new corpus version rather than
replacing the release asset.

## Other corpora

The lazer performance snapshot uses 100 real v128 maps: 8 osu!standard, 43
osu!taiko, 18 osu!catch and 31 osu!mania maps, totalling 2,634,547 bytes. This
corpus is intentionally reported separately: its smaller files and different
mode distribution make absolute latency comparisons with the legacy profile
misleading.

The retained compatibility corpus contains 15,952 ranked/approved maps:
9,952 standard and 2,000 each of taiko, catch and mania. Selection used playcount;
it is not a representative traffic-weighted distribution and is not a public
performance input.

Pass the same materialized directory to each benchmark variant. Compare only
runs with the same corpus version, host, compiler and timing boundary. Supply
`performance-v1.csv` to the competitor harness's `--corpus-manifest` option.

See [performance](../../docs/performance.md) for FOSU checks and
[competitor comparisons](../comparison/README.md) for the cross-library harness.
