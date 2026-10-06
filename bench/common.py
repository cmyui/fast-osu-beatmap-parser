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


def shuffled_order(count: int, seed: int) -> list[int]:
    """A pass's entry order: Fisher-Yates over splitmix64, as feature_matrix.cc."""
    mask = (1 << 64) - 1
    order = list(range(count))
    for i in range(count, 1, -1):
        seed = (seed + 0x9E3779B97F4A7C15) & mask
        z = ((seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9) & mask
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & mask
        z ^= z >> 31
        j = z % i
        order[i - 1], order[j] = order[j], order[i - 1]
    return order
