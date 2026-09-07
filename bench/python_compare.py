"""Measure actual Python calls; warmed input/cache, variants rotated per map."""
import argparse
import gc
import json
from pathlib import Path
from statistics import mean, median
from time import perf_counter_ns

import fosu
from fosu._native import ffi, lib, SIMD_ENABLED


def raw_bytes(data, path):
    h = lib.fosu_new()
    assert h != ffi.NULL
    assert lib.fosu_parse(h, data, len(data), lib.FOSU_ALL) == lib.FOSU_OK
    count = lib.fosu_get_view(h).hit_object_count
    lib.fosu_free(h)
    return count


def raw_file(data, path):
    h = lib.fosu_new()
    assert h != ffi.NULL
    assert lib.fosu_parse_file(h, path, lib.FOSU_ALL) == lib.FOSU_OK
    count = lib.fosu_get_view(h).hit_object_count
    lib.fosu_free(h)
    return count


def python_bytes(data, path):
    bm = fosu.parse(data)
    return len(bm.hit_objects)


def python_file(data, path):
    bm = fosu.parse_file(path)
    return len(bm.hit_objects)


def python_numpy(data, path):
    bm = fosu.parse(data)
    return bm.hit_objects.to_numpy().size


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("corpus", type=Path)
    p.add_argument("--reps", type=int, default=5)
    args = p.parse_args()
    files = sorted(args.corpus.glob("*.osu"))
    assert files and args.reps > 0
    variants = [raw_bytes, raw_file, python_bytes, python_file, python_numpy]
    minima = {f.__name__: [] for f in variants}
    runs = {f.__name__: [] for f in variants}
    # Import NumPy and create dtype caches before timing.
    data, path = files[0].read_bytes(), bytes(files[0])
    for f in variants:
        f(data, path)
    gc.collect()
    for i, file in enumerate(files):
        data, path = file.read_bytes(), bytes(file)
        times = {f.__name__: [] for f in variants}
        for rep in range(args.reps):
            for j in range(len(variants)):
                f = variants[(i + rep + j) % len(variants)]
                start = perf_counter_ns()
                count = f(data, path)
                times[f.__name__].append(perf_counter_ns() - start)
        for name, values in times.items():
            minima[name].append(min(values))
            runs[name].extend(values)
    print(json.dumps({"files": len(files), "reps": args.reps, "simd": SIMD_ENABLED,
                      "variants": {name: {"all_mean_us": mean(runs[name]) / 1000,
                                           "all_p50_us": median(runs[name]) / 1000,
                                           "mean_file_min_us": mean(minima[name]) / 1000}
                                   for name in runs}}, indent=2))


if __name__ == "__main__":
    main()
