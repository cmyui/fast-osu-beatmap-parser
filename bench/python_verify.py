"""Walk every public Python value against a canonical native reference dump.

Run on the corpus host; no beatmap data is written out by this verifier.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys

import fosu
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "examples"))
from decode_oneshot import decode


def compare(actual, expected):
    if isinstance(expected, bytes):
        assert actual.encode("utf-8", "surrogateescape") == expected
    elif isinstance(expected, float):
        assert struct.pack("<d", actual) == struct.pack("<d", expected)
    else:
        assert actual == expected


def check(bm, ref):
    for name, expected in ref["metadata"].items():
        compare(getattr(bm, name), expected)
    for name, expected in ref["stats"].items():
        compare(getattr(bm.stats, name), expected)
    for group in ("hit_objects", "sliders", "timing_points"):
        items = getattr(bm, group)
        assert len(items) == len(ref[group])
        for actual, expected in zip(items, ref[group]):
            for name, value in expected.items():
                compare(getattr(actual, "slider_index" if group == "hit_objects" and name == "slider" else name), value)
    assert [(p.x, p.y) for p in bm.slider_points] == ref["slider_points"]
    assert [(b.start, b.end) for b in bm.breaks] == ref["breaks"]
    assert list(bm.combo_colours) == ref["combo_colours"]
    for note in bm.hit_objects:
        if note.slider is not None:
            expected = ref["sliders"][note.slider_index]
            start, count = expected["point_begin"], expected["point_count"]
            assert [(p.x, p.y) for p in note.slider.points] == ref["slider_points"][start:start + count]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--shard-count", type=int, default=1)
    args = parser.parse_args()
    if args.shard_count < 1 or not 0 <= args.shard_index < args.shard_count:
        parser.error("shard index must be in [0, shard-count), with a positive shard count")
    files = sorted(args.corpus.glob("*.osu"))[args.shard_index::args.shard_count]
    assert files, "empty corpus"
    for count, path in enumerate(files, 1):
        ref = decode(subprocess.check_output([str(args.reference.resolve()), str(path)]))
        try:
            check(fosu.parse_file(path), ref)
            check(fosu.parse(path.read_bytes()), ref)
        except AssertionError:
            raise AssertionError(f"Python/native mismatch at corpus file #{count}") from None
        if count % 1000 == 0:
            print(f"Verified {count} maps", flush=True)
    print(f"All Python-visible values match across {len(files)} maps (file and bytes APIs)")


if __name__ == "__main__":
    main()
