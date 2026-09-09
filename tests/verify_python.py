"""Walk every public Python value against a canonical native reference dump.

Run on the corpus host; no beatmap data is written out by this verifier.
"""

import argparse
from enum import Enum
import struct
import subprocess
import sys
from pathlib import Path

import fosu

from support.decode import decode


def compare(actual, expected):
    if isinstance(actual, Enum):
        actual = actual.value
    if isinstance(expected, bytes):
        assert actual.encode("utf-8", "surrogateescape") == expected
    elif isinstance(expected, float):
        assert struct.pack("<d", actual) == struct.pack("<d", expected)
    else:
        assert actual == expected


def check(bm, ref):
    for name, expected in ref["metadata"].items():
        actual = getattr(bm, name)
        if name == "sample_set":
            expected = fosu.SampleSet[expected.decode().upper()].value
        if name in ("beatmap_id", "beatmap_set_id", "preview_time") and actual is None:
            actual = -1
        compare(actual, expected)
    for name, expected in ref["stats"].items():
        compare(getattr(bm.stats, name), expected)
    for group in ("hit_objects", "timing_points"):
        items = getattr(bm, group)
        assert len(items) == len(ref[group])
        for actual, expected in zip(items, ref[group]):
            for name, value in expected.items():
                if name == "slider":
                    continue
                if name == "end_time" and isinstance(actual, fosu.Slider):
                    assert actual.end_time is None and value == 0
                else:
                    compare(getattr(actual, name), value)
    assert [(b.start, b.end) for b in bm.breaks] == ref["breaks"]
    assert list(bm.combo_colours) == ref["combo_colours"]
    for note, expected in zip(bm.hit_objects, ref["hit_objects"]):
        flags = expected["type"]
        kind = (
            fosu.Circle
            if flags & 1
            else fosu.Slider
            if flags & 2
            else fosu.Spinner
            if flags & 8
            else fosu.HoldNote
        )
        assert type(note) is kind
        compare(note.time, expected["time"])
        compare(note.type, flags)
        compare(int(note.hitsound), expected["hitsound"])
        compare(note.hit_sample, expected["hit_sample"])
        assert note.new_combo == bool(expected["new_combo"])
        assert note.combo_skip == expected["combo_skip"]
        if isinstance(note, fosu.Slider):
            assert note.end_time is None
            params = ref["sliders"][expected["slider"]]
            start, count = params["point_begin"], params["point_count"]
            assert [(p.x, p.y) for p in note.control_points] == [
                (expected["x"], expected["y"])
            ] + ref["slider_points"][start : start + count]
            for public, raw in (
                ("slides", "slides"),
                ("length", "length"),
                ("curve_type", "curve_type"),
                ("edge_sounds", "edge_sounds"),
                ("edge_sets", "edge_sets"),
            ):
                compare(getattr(note, public), params[raw])
        else:
            compare(
                note.end_time,
                expected["time"]
                if isinstance(note, fosu.Circle)
                else expected["end_time"],
            )
    for name in ("beatmap_id", "beatmap_set_id", "preview_time"):
        value = ref["metadata"][name]
        assert getattr(bm, name) == (None if value == -1 else value)
    assert int(bm.mode) == ref["metadata"]["mode"]
    assert bm.tag_list == bm.tags.split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--shard-count", type=int, default=1)
    args = parser.parse_args()
    if args.shard_count < 1 or not 0 <= args.shard_index < args.shard_count:
        parser.error(
            "shard index must be in [0, shard-count), with a positive shard count"
        )
    files = sorted(args.corpus.glob("*.osu"))[args.shard_index :: args.shard_count]
    assert files, "empty corpus"
    for count, path in enumerate(files, 1):
        ref = decode(
            subprocess.check_output([str(args.reference.resolve()), str(path)])
        )
        try:
            result = fosu.parse_file(path)
            check(result, ref)
            check(fosu.parse(path.read_bytes()), ref)
        except AssertionError:
            raise AssertionError(
                f"Python/native mismatch at corpus file #{count}"
            ) from None
        if count % 1000 == 0:
            print(f"Verified {count} maps", flush=True)
    print(
        f"All Python-visible values match across {len(files)} maps (file and bytes APIs)"
    )


if __name__ == "__main__":
    main()
