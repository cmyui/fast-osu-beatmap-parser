"""Rotate fresh-result Python calls across workloads and optional wheel directories.

Input bytes and imports are warm. Every call releases its result. Path workloads
include reading the file; iterate parses then sums all hitobject start times; bytes workloads start with bytes already in Python.
"""

import argparse
import csv
import gc
import importlib.util
import sys
from pathlib import Path
from time import perf_counter_ns
from types import ModuleType

from common import select_files


def load(path: Path, name: str) -> ModuleType:
    spec = importlib.util.spec_from_file_location(
        name, path / "fosu/__init__.py", submodule_search_locations=[str(path / "fosu")]
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def call(package: ModuleType, kind: str, data: bytes, path: Path) -> int | float:
    bm = package.parse_file(path) if kind == "file" else package.parse(data)
    if kind == "slider-lengths":
        return sum(
            note.length for note in bm.hit_objects if isinstance(note, package.Slider)
        )
    if kind == "iterate":
        return sum(note.start_time for note in bm.hit_objects)
    return len(bm.hit_objects)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("corpus", type=Path)
    p.add_argument(
        "packages",
        nargs="*",
        type=Path,
        help="extracted wheels; default: installed fosu",
    )
    p.add_argument("--reps", type=int, default=5)
    p.add_argument("--limit", type=int, default=0)
    p.add_argument(
        "--workloads",
        nargs="+",
        choices=[
            "bytes",
            "file",
            "iterate",
            "slider-lengths",
        ],
        default=["bytes", "file"],
    )
    args = p.parse_args()
    files = select_files(p, args.corpus, args.reps, args.limit)
    if args.packages:
        packages = [
            (str(path.resolve()), load(path.resolve(), f"fosu_variant_{i}"))
            for i, path in enumerate(args.packages)
        ]
    else:
        import fosu

        packages = [("installed", fosu)]
    variants = [
        (name, package, kind) for name, package in packages for kind in args.workloads
    ]
    # Initialize parser selection and Python allocation consistently across packages.
    for _, package, kind in variants:
        call(package, kind, files[0].read_bytes(), files[0])
    gc.collect()
    out = csv.writer(sys.stdout)
    out.writerow(["file", "bytes", "rep", "variant", "workload", "wall_ns"])
    for i, file in enumerate(files):
        data = file.read_bytes()
        for rep in range(args.reps):
            for j in range(len(variants)):
                name, package, kind = variants[(i + rep + j) % len(variants)]
                start = perf_counter_ns()
                call(package, kind, data, file)
                ns = perf_counter_ns() - start
                out.writerow([file.name, len(data), rep, name, kind, ns])


if __name__ == "__main__":
    main()
