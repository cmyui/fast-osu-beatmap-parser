"""Persistent worker; JSON and IPC are outside the measured intervals."""
import json
import io
import sys
from pathlib import Path
from time import perf_counter_ns


def load_parser(name):
    visit = None
    if name == "fosu":
        import fosu

        def parse(data, path, workload):
            return fosu.parse_file(path) if workload == "file" else fosu.parse(data)

        def count(beatmap):
            return len(beatmap.hit_objects)

        def visit(beatmap):
            return sum(obj.start_time for obj in beatmap.hit_objects)

    elif name == "slider":
        import slider

        def parse(data, path, workload):
            return (slider.Beatmap.from_path(path) if workload == "file"
                    else slider.Beatmap.parse(data.decode("utf-8-sig")))

        def count(beatmap):
            # Do not request stacking, mods, difficulty, or curve evaluation.
            return len(beatmap.hit_objects(stacking=False))

        def visit(beatmap):
            return sum(obj.time.total_seconds() * 1000
                       for obj in beatmap.hit_objects(stacking=False))

    elif name == "rosu-pp-py":
        import rosu_pp_py

        def parse(data, path, workload):
            return (rosu_pp_py.Beatmap(path=str(path)) if workload == "file"
                    else rosu_pp_py.Beatmap(bytes=data))

        def count(beatmap):
            return beatmap.n_objects

    elif name == "osupyparser":
        from osupyparser import OsuFile

        def parse(data, path, workload):
            assert workload == "file"  # Published API has no in-memory beatmap decoder.
            return OsuFile(str(path)).parse_file()

        def count(beatmap):
            return len(beatmap.hit_objects)

    elif name == "pyttanko":
        import pyttanko

        decoder = pyttanko.parser()

        def parse(data, path, workload):
            source = (path.open(encoding="utf-8-sig") if workload == "file"
                      else io.StringIO(data.decode("utf-8-sig")))
            with source:
                return decoder.map(source)

        def count(beatmap):
            return len(beatmap.hitobjects)

        def visit(beatmap):
            return sum(obj.time for obj in beatmap.hitobjects)

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
