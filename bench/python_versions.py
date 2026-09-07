"""Pair fresh-result Python calls from isolated wheel directories in one process.

Imports and input page-cache misses are outside timing. Each call allocates and
releases its result; no parser handle or map result is reused between calls.
"""
import argparse
import gc
import importlib.util
import json
from pathlib import Path
from statistics import mean, median
import sys
from time import perf_counter_ns


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path / 'fosu/__init__.py',
                                                 submodule_search_locations=[str(path / 'fosu')])
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('corpus', type=Path)
    p.add_argument('packages', nargs='+', type=Path)
    p.add_argument('--reps', type=int, default=5)
    args = p.parse_args()
    assert args.reps > 0
    packages = [(str(path), load(path.resolve(), f'fosu_variant_{i}'))
                for i, path in enumerate(args.packages)]
    variants = [(name + '/' + kind, package, kind) for name, package in packages
                for kind in ('bytes', 'file')]
    files = sorted(args.corpus.glob('*.osu'))
    assert files
    runs, minima = ({name: [] for name, _, _ in variants} for _ in range(2))
    gc.collect()
    for i, file in enumerate(files):
        data = file.read_bytes()
        times = {name: [] for name, _, _ in variants}
        for rep in range(args.reps):
            for j in range(len(variants)):
                name, package, kind = variants[(i + rep + j) % len(variants)]
                start = perf_counter_ns()
                bm = package.parse(data) if kind == 'bytes' else package.parse_file(file)
                count = len(bm.hit_objects)
                del bm
                times[name].append(perf_counter_ns() - start)
        for name, values in times.items():
            runs[name].extend(values)
            minima[name].append(min(values))
    print(json.dumps({'files': len(files), 'reps': args.reps, 'variants': {
        name: {'all_mean_us': mean(values) / 1000, 'all_p50_us': median(values) / 1000,
               'mean_file_min_us': mean(minima[name]) / 1000}
        for name, values in runs.items()}}, indent=2))


if __name__ == '__main__':
    main()
