"""Measure public Python parsing profiles from resident bytes."""

import argparse
import csv
import gc
from pathlib import Path
import sys
from time import perf_counter_ns

import fosu

from common import select_files


PROFILES = {
    "decode": {},
    "hit-objects-only": {"sections": fosu.Sections.HIT_OBJECTS},
    "end-times": {"calculate_slider_end_times": True},
    "paths": {"calculate_slider_paths": True},
    "geometry": {
        "calculate_slider_end_times": True,
        "calculate_slider_paths": True,
    },
    "events": {"calculate_slider_events": True},
    "stacking": {"apply_stacking": True},
    "gameplay": {
        "calculate_slider_events": True,
        "apply_stacking": True,
    },
    "double-time": {"mods": fosu.Mods.DOUBLE_TIME},
}

STANDARD_PROFILES = {
    "decode": {},
    "double-time": {"mods": fosu.Mods.DOUBLE_TIME},
    "hard-rock-double-time": {
        "mods": fosu.Mods.HARD_ROCK | fosu.Mods.DOUBLE_TIME,
    },
    "full-hard-rock-double-time": {
        "calculate_slider_events": True,
        "apply_stacking": True,
        "mods": fosu.Mods.HARD_ROCK | fosu.Mods.DOUBLE_TIME,
    },
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--variant", default=f"python-{fosu.backend}")
    parser.add_argument("--suite", choices=["all-modes", "standard"], required=True)
    args = parser.parse_args()
    files = select_files(parser, args.corpus, args.reps, args.limit)
    inputs = [(file, file.read_bytes()) for file in files]

    selected_profiles = PROFILES if args.suite == "all-modes" else STANDARD_PROFILES
    for options in selected_profiles.values():
        fosu.parse(inputs[0][1], **options)
    gc.collect()

    writer = csv.writer(sys.stdout)
    writer.writerow(["file", "bytes", "rep", "variant", "workload", "wall_ns"])
    profiles = list(selected_profiles.items())
    for file_index, (file, data) in enumerate(inputs):
        for rep in range(args.reps):
            for job_index in range(len(profiles)):
                profile, options = profiles[
                    (file_index + rep + job_index) % len(profiles)
                ]
                begin = perf_counter_ns()
                beatmap = fosu.parse(data, **options)
                del beatmap
                elapsed = perf_counter_ns() - begin
                writer.writerow(
                    [file.name, len(data), rep, args.variant, profile, elapsed]
                )


if __name__ == "__main__":
    main()
