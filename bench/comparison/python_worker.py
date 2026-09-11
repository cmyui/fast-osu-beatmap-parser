"""Persistent worker; JSON and IPC are outside the measured intervals."""

import json
import os
import sys
from pathlib import Path
from time import perf_counter_ns


def load_parser(name):
    visit = None
    if name == "fosu":
        import fosu

        profile = os.environ.get("FOSU_BENCH_PROFILE", "decode")
        if profile == "decode":
            options = {}
        elif profile == "geometry":
            options = {
                "calculate_slider_end_times": True,
                "calculate_slider_paths": True,
            }
        else:
            raise ValueError(f"unknown FOSU benchmark profile: {profile}")

        def parse(data, path, workload):
            return (
                fosu.parse_file(path, **options)
                if workload == "file"
                else fosu.parse(data, **options)
            )

        def count(beatmap):
            return len(beatmap.hit_objects)

        def visit(beatmap):
            return sum(obj.time for obj in beatmap.hit_objects)

    elif name == "slider":
        import slider

        def parse(data, path, workload):
            return (
                slider.Beatmap.from_path(path)
                if workload == "file"
                else slider.Beatmap.parse(data.decode("utf-8-sig"))
            )

        def count(beatmap):
            # Do not request stacking, mods, difficulty, or curve evaluation.
            return len(beatmap.hit_objects(stacking=False))

        def visit(beatmap):
            return sum(
                obj.time.total_seconds() * 1000
                for obj in beatmap.hit_objects(stacking=False)
            )

    elif name == "osupyparser":
        from osupyparser import OsuFile

        def parse(data, path, workload):
            assert workload == "file"  # Published API has no in-memory beatmap decoder.
            return OsuFile(str(path)).parse_file()

        def count(beatmap):
            return len(beatmap.hit_objects)

    else:
        raise ValueError(name)
    return parse, count, visit


def main():
    parse, count, visit = load_parser(sys.argv[1])
    for line in sys.stdin:
        request = json.loads(line)
        try:
            path = Path(request["path"])
            data = path.read_bytes()
            workload = request["workload"]
            checksum = None
            samples = []
            for _ in range(request["reps"]):
                start = perf_counter_ns()
                beatmap = parse(data, path, workload)
                objects = count(beatmap)
                if workload == "visit":
                    checksum = visit(beatmap)
                del beatmap  # Include refcount/native-result cleanup.
                samples.append(perf_counter_ns() - start)
            response = {"count": objects, "checksum": checksum, "ns": samples}
        except Exception as error:
            response = {"error": type(error).__name__, "detail": str(error)[:240]}
        print(json.dumps(response, allow_nan=False), flush=True)


if __name__ == "__main__":
    main()
