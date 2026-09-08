"""One public Python API, one process, one timed pass over resident inputs."""
import hashlib
import json
from pathlib import Path
import sys
from time import perf_counter_ns

from python_worker import load_parser


def main():
    name, workload, corpus, summary = sys.argv[1:]
    report = json.loads(Path(summary).read_text())
    excluded = set(report["tables"]["python"]["excluded_files"])
    inputs = [(path, path.read_bytes()) for path in sorted(Path(corpus).glob("*.osu"))
              if path.name not in excluded]
    assert len(inputs) == report["tables"]["python"]["files"]
    fingerprint = hashlib.sha256()
    for path, data in inputs:
        fingerprint.update(path.name.encode() + b"\0" + hashlib.sha256(data).digest())
    parse, count, _ = load_parser(name)
    for index in range(64):
        path, data = inputs[index * len(inputs) // 64]
        for _ in range(3):
            beatmap = parse(data, path, workload)
            count(beatmap)
            del beatmap
    counts = []
    start = perf_counter_ns()
    for path, data in inputs:
        beatmap = parse(data, path, workload)
        counts.append(count(beatmap))
        del beatmap
    elapsed = perf_counter_ns() - start
    print(json.dumps({"elapsed_ns": elapsed, "files": len(inputs),
                      "bytes": sum(len(data) for _, data in inputs),
                      "corpus_sha256": fingerprint.hexdigest(), "counts": counts}))


if __name__ == "__main__":
    main()
