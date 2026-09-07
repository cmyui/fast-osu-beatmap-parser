"""Rotate fresh-result Python calls across workloads and optional wheel directories.

Input bytes and imports are warm. Every call releases its result. Path workloads
include reading the file; bytes workloads start with bytes already in Python.
"""
import argparse
import csv
import gc
import importlib.util
from pathlib import Path
import sys
from time import perf_counter_ns

from common import select_files


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path / 'fosu/__init__.py',
                                                 submodule_search_locations=[str(path / 'fosu')])
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def call(package, kind, data, path):
    if kind.startswith('raw-'):
        native = sys.modules[package.__name__ + '._native']
        ffi, lib = native.ffi, native.lib
        handle = lib.fosu_new()
        if handle == ffi.NULL:
            raise MemoryError('fosu_new')
        try:
            status = (lib.fosu_parse(handle, data, len(data), lib.FOSU_ALL) if kind == 'raw-bytes'
                      else lib.fosu_parse_file(handle, bytes(path), lib.FOSU_ALL))
            if status != lib.FOSU_OK:
                raise RuntimeError(status)
            return lib.fosu_get_view(handle).hit_object_count
        finally:
            lib.fosu_free(handle)
    bm = package.parse_file(path) if kind == 'file' else package.parse(data)
    return bm.hit_objects.to_numpy().size if kind == 'numpy' else len(bm.hit_objects)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('corpus', type=Path)
    p.add_argument('packages', nargs='*', type=Path, help='extracted wheels; default: installed fosu')
    p.add_argument('--reps', type=int, default=5)
    p.add_argument('--limit', type=int, default=0)
    p.add_argument('--workloads', nargs='+', choices=['bytes', 'file', 'raw-bytes', 'raw-file', 'numpy'],
                   default=['bytes', 'file'])
    args = p.parse_args()
    files = select_files(p, args.corpus, args.reps, args.limit)
    if args.packages:
        packages = [(str(path.resolve()), load(path.resolve(), f'fosu_variant_{i}'))
                    for i, path in enumerate(args.packages)]
    else:
        import fosu
        packages = [('installed', fosu)]
    variants = [(name, package, kind) for name, package in packages for kind in args.workloads]
    # Initialize Python view/dtype caches consistently across packages.
    for _, package, kind in variants:
        call(package, kind, files[0].read_bytes(), files[0])
    gc.collect()
    out = csv.writer(sys.stdout)
    out.writerow(['file', 'bytes', 'rep', 'variant', 'workload', 'wall_ns'])
    for i, file in enumerate(files):
        data = file.read_bytes()
        for rep in range(args.reps):
            for j in range(len(variants)):
                name, package, kind = variants[(i + rep + j) % len(variants)]
                start = perf_counter_ns()
                call(package, kind, data, file)
                ns = perf_counter_ns() - start
                out.writerow([file.name, len(data), rep, name, kind, ns])


if __name__ == '__main__':
    main()
