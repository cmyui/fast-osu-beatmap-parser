"""Corpus selection shared by Python benchmark drivers."""

import argparse
from pathlib import Path


def select_files(
    parser: argparse.ArgumentParser,
    corpus: Path,
    reps: int,
    limit: int = 0,
) -> list[Path]:
    files = sorted(corpus.glob("*.osu"))
    if not files or reps < 1 or limit < 0:
        parser.error("nonempty corpus, positive reps and nonnegative limit required")
    if 0 < limit < len(files):
        files = [files[i * len(files) // limit] for i in range(limit)]
    return files
