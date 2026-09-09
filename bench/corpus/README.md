# Benchmark corpus

Use the [routine profiles](../../docs/validation.md) for everyday compatibility
and performance checks. The full corpus described here is the source population
and the broader validation/publication workload.

The default corpus preserves the original 10,000 maps and adds roughly 2,000
popular ranked/approved difficulties from each native non-standard mode:
taiko (1), catch (2), and mania (3). Standard is mode 0. This is a mixed-mode
workload, not an equally weighted average of four modes.

Selection uses Akatsuki playcount descending, with beatmap ID ascending to break
ties. `select_non_standard.sql` selects surplus candidates in a read-only
transaction. `expand.py` walks each mode's ranking in order, skips unavailable
or invalid stored files, and stops at 2,000 usable files (or the available
candidate count). Native mode is checked against the file's `[General]` section;
omitted `Mode` defaults to standard. Converted gameplay modes are not new files.

Files already in the baseline count toward the mode's selection without being
duplicated. The original files are never replaced. Neither player records nor
scores are exported. Raw `.osu` files are not distributed with the repository.

## Acquire and verify

Run the SQL using authorized read-only database access and save its headerless,
tab-separated output. No transaction stays open while files are downloaded.

```sh
python -m pip install boto3==1.34.46
python bench/corpus/expand.py /path/to/original-10k candidates.tsv /path/to/new-corpus
python -m unittest discover -s bench/corpus -p 'test_*.py'
```

The fetcher requires `AWS_S3_ENDPOINT_URL`, `AWS_S3_REGION_NAME`,
`AWS_S3_ACCESS_KEY_ID`, `AWS_S3_SECRET_ACCESS_KEY`, and `AWS_S3_BUCKET_NAME` in its
environment. Do not put credentials in commands, manifests, or version control.
It uses at most eight concurrent object reads. `--cache /path/to/prior/files`
reuses downloads from an interrupted attempt; the output directory must be new.

The output contains `files/`, `manifest.csv`, and `summary.json`. The manifest
records each file's native mode, bytes, SHA-256, selection origin, and ranking
metadata. The summary records skipped candidates and the corpus fingerprint:
SHA-256 over sorted filenames, each followed by a NUL byte and its raw SHA-256
digest. A matching file count alone does not identify the corpus.

Use `files/` for all full-corpus parser validation and measurements. First-use
benchmarks select evenly spaced subsets from the same sorted collection. Supply
`manifest.csv` to the comparison driver's `--corpus-manifest` option to validate
file contents and retain mode coverage in reports. Historical measurements from
the original 10k corpus are not directly comparable to the mixed-mode corpus.
