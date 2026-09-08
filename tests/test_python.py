"""Installed Python API: detached values, native conversion, and input contracts."""

import gc
import math
import os
import pickle
import subprocess
import sys
import weakref
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
from dataclasses import asdict, fields, is_dataclass, replace
from pathlib import Path
from typing import get_type_hints

import fosu
import pytest


def test_fractional_times_and_malformed_numeric_fields():
    import math

    bm = fosu.parse(
        b"[Metadata]\nTitle:ok\nTitlX:no\n[MetadataFake]\nTitle:no\n"
        b"[Difficulty]\nApproachRate:1e309\n"
        b"[TimingPoints]\n0,500\n1,NaN,4,2,1,100,0,0\n2,NaN,4,2,1,100,1,0\n"
        b"[HitObjects]\n256.5,192,1000.5,1,0\n1,2,2000.25,8,0,3000.75\n"
        b"1,2,4000.5,128,0,5000.75:0:0:0:0:\n"
        b"1,2,6000,2,0,B|1.5:2.5,1,2.5e2\n"
        b"1,2,NaN,1,0\n1,2,3,1,0\x00junk\n"
        b"[Events]\n2,1.25,9.75\n"
    )
    assert bm.title == "ok" and bm.ar == 5
    assert len(bm.timing_points) == 2
    assert math.isnan(bm.timing_points[1].beat_length)
    assert not bm.timing_points[1].uninherited
    assert [h.start_time for h in bm.hit_objects] == [1000.5, 2000.25, 4000.5, 6000]
    assert [h.raw_end_time for h in bm.hit_objects] == [0, 3000.75, 5000.75, 0]
    assert bm.hit_objects[0].x == 256
    assert bm.hit_objects[3].length == 250
    assert [(p.x, p.y) for p in bm.hit_objects[3].control_points[1:]] == [(1, 2)]
    assert (bm.breaks[0].start, bm.breaks[0].end) == (1.25, 9.75)
    assert bm.stats.malformed_lines == 4


def test_complete_map(tmp_path):
    data = (
        "osu file format v14\n[General]\nAudioFilename:song.mp3\nMode:3\n"
        "LetterboxInBreaks:1\n[Editor]\nBookmarks:100,-200,300\n"
        "[Metadata]\nTitle:日本語\nArtist:artist\nVersion:Hard\nBeatmapID:12345\n"
        "[Difficulty]\nOverallDifficulty:9\nCircleSize:4\n"
        '[Events]\n0,0,"bg.jpg",0,0\n2,100,200\n'
        "[Colours]\nCombo1:12,34,56\n"
        "[TimingPoints]\n1.25,342.857142857142857142857,4,2,1,60,1,0\n"
        "[HitObjects]\n-48,192,1000,1,0,0:0:0:0:\n"
        "512,192,2000,2,14,B|-129088:1726|123:456,2,240,2|0,0:0|0:0,0:0:0:0:\n"
        "256,192,3000,8,0,4000,0:0:0:0:\n"
        "64,100,5000,128,0,6000:0:0:0:0:\n"
    ).encode()
    path = tmp_path / "日本語.osu"
    path.write_bytes(data)
    b = fosu.parse(data)
    assert b == fosu.parse_file(path) == fosu.parse_file(os.fsencode(path))
    assert (b.title, b.artist, b.version, b.ar, b.cs) == (
        "日本語",
        "artist",
        "Hard",
        9,
        4,
    )
    assert b.mode is fosu.GameMode.MANIA and b.letterbox_in_breaks is True
    assert b.bookmarks == [100, -200, 300]
    assert isinstance(b.hit_objects, list) and len(b.hit_objects) == 4
    circle, slider, spinner, hold = b.hit_objects
    assert isinstance(circle, fosu.Circle) and circle.is_circle
    assert circle.end_time == circle.start_time == 1000
    assert circle.raw_end_time == 0
    assert isinstance(slider, fosu.Slider) and slider.end_time is None
    assert (slider.span_count, slider.curve_type, slider.length) == (2, "B", 240)
    assert (
        slider.hit_sound
        == fosu.HitSound.WHISTLE | fosu.HitSound.FINISH | fosu.HitSound.CLAP
    )
    assert [(p.x, p.y) for p in slider.control_points] == [
        (512, 192),
        (-129088, 1726),
        (123, 456),
    ]
    assert slider.raw_edge_sounds == "2|0"
    assert spinner.is_spinner and spinner.end_time == 4000
    assert hold.is_hold and hold.end_time == 6000
    assert b.timing_points[0].uninherited is True
    assert b.breaks == [fosu.Break(100, 200)]
    assert b.combo_colours == [0x0C2238]
    assert b.stats.fast_path_lines + b.stats.slow_path_lines == 4
    assert "hit_objects=4" in repr(b) and len(repr(b)) < 150


def test_empty_input_defaults():
    b = fosu.parse(b"")
    assert b.title == "" and b.sample_set == "Normal"
    assert b.hit_objects == b.timing_points == b.breaks == b.bookmarks == []
    assert b.mode is fosu.GameMode.OSU
    assert b.beatmap_id is None and b.preview_time is None


def test_omitted_general_uses_defaults():
    b = fosu.parse(b"[Metadata]\nTitle:Only metadata\n")
    assert b.title == "Only metadata" and b.audio_filename == ""
    assert b.mode is fosu.GameMode.OSU


def test_omitted_metadata_uses_defaults():
    b = fosu.parse(b"[General]\nAudioLeadIn:450\n")
    assert b.audio_lead_in == 450 and b.title == "" and b.tags == []


def test_omitted_hitobjects_is_empty_list():
    b = fosu.parse(b"[Difficulty]\nApproachRate:8\n")
    assert b.ar == 8 and b.hit_objects == []


def test_explicit_empty_hitobject_section():
    b = fosu.parse(b"[HitObjects]\n")
    assert b.hit_objects == [] and b.stats.malformed_lines == 0


def test_zero_and_other_negative_values_are_preserved():
    b = fosu.parse(
        b"[Metadata]\nBeatmapID:0\nBeatmapSetID:-2\n[General]\nPreviewTime:0\n"
    )
    assert b.beatmap_id == 0 and b.beatmap_set_id == -2 and b.preview_time == 0


def test_tags_and_raw_text_are_eager():
    b = fosu.parse(
        b"[Metadata]\nTags:alpha  beta alpha\n[Editor]\nBookmarks:12, -30,40\n"
    )
    assert b.tags == ["alpha", "beta", "alpha"]
    assert b.raw_tags == "alpha  beta alpha" and b.raw_bookmarks == "12, -30,40"
    b.tags.append("new")
    assert b.tags[-1] == "new"


def test_invalid_bookmarks_follow_official_decoder():
    b = fosu.parse(
        b"[Editor]\nBookmarks:10,bad,20,2147483648,-2147483648,1_000,,+30,4e1\n"
    )
    assert b.bookmarks == [10, 20, -2147483648, 30]
    assert "bad" in b.raw_bookmarks and b.stats.malformed_lines == 0


def test_utf8_and_embedded_nul_roundtrip():
    data = b"[Metadata]\nTitle:hello\xff\x00world\n"
    b = fosu.parse(data)
    assert b.title.encode("utf-8", "surrogateescape") == b"hello\xff\x00world"


def test_native_nan_and_signed_zero_bits_survive():
    import struct

    b = fosu.parse(
        b"[TimingPoints]\n0,500\n10,NaN,4,1,0,100,0,0\n[HitObjects]\n1,2,-0,1,0\n"
    )
    assert math.isnan(b.timing_points[1].beat_length)
    assert struct.pack("<d", b.hit_objects[0].start_time) == struct.pack("<d", -0.0)


def test_kind_precedence_and_combo_flags():
    b = fosu.parse(
        b"[HitObjects]\n1,2,3,11,0\n10,20,30,10,0,L|40:50,1,60\n50,60,70,53,0\n"
    )
    a, s, c = b.hit_objects
    assert isinstance(a, fosu.Circle) and isinstance(s, fosu.Slider)
    assert c.is_new_combo is True and c.combo_skip == 3 and c.raw_type == 53


def test_detached_values_remain_valid_after_reuse():
    data = bytearray(b"[Metadata]\nTitle:Retained\n[HitObjects]\n32,48,600,1,0\n")
    b = fosu.parse(data)
    note = b.hit_objects[0]
    data[:] = b"x" * len(data)
    b.title = "Edited"
    note.x = 99
    assert b.hit_objects[0] is note and b.hit_objects[0].x == 99
    for _ in range(100):
        fosu.parse(b"[HitObjects]\n4,5,6,1,0\n")
    del b
    gc.collect()
    assert (note.x, note.y, note.start_time) == (99, 48, 600)


def test_points_are_independent_between_results():
    data = b"[HitObjects]\n10,20,30,2,0,L|40:50,1,60\n"
    b = fosu.parse(data)
    other = fosu.parse(data)
    b.hit_objects[0].control_points[1].x = 70
    assert b.hit_objects[0].control_points[1].x == 70
    assert other.hit_objects[0].control_points[1].x == 40


def test_standard_python_copy_and_export():
    b = fosu.parse(b"[Metadata]\nTitle:Copy\n[HitObjects]\n2,4,6,2,0,B|8:10,1,12\n")
    restored = pickle.loads(pickle.dumps(b))
    copied = deepcopy(b)
    assert b == restored == copied
    copied.hit_objects[0].x = 100
    assert b.hit_objects[0].x == 2
    assert replace(b, title="Replaced").title == "Replaced"
    result = asdict(b)
    assert result["hit_objects"][0]["control_points"] == [
        {"x": 2, "y": 4},
        {"x": 8, "y": 10},
    ]


def test_plain_lists_support_normal_mutations():
    b = fosu.parse(b"[HitObjects]\n7,8,9,1,0\n10,11,12,1,0\n")
    first = b.hit_objects.pop(0)
    b.hit_objects.insert(1, first)
    assert [h.start_time for h in b.hit_objects] == [12, 9]
    assert b.hit_objects[::-1][0] is first


def test_user_created_cycles_are_collected():
    class Marker:
        pass

    b = fosu.parse(b"")
    marker = Marker()
    ref = weakref.ref(marker)
    b.title = marker
    b.hit_objects.append(b)
    del b, marker
    gc.collect()
    assert ref() is None


def test_buffer_inputs():
    from array import array

    data = b"[Metadata]\nTitle:Buffer\n"
    interleaved = b"".join(bytes((c, 0)) for c in data)
    for buffer in (
        bytearray(data),
        memoryview(data),
        array("B", data),
        memoryview(interleaved)[::2],
    ):
        assert fosu.parse(buffer).title == "Buffer"
    with pytest.raises(TypeError):
        fosu.parse("not a buffer")


def test_file_errors(tmp_path):
    with pytest.raises(FileNotFoundError) as error:
        fosu.parse_file(tmp_path / "missing.osu")
    assert (
        error.value.filename == str(tmp_path / "missing.osu")
        or error.value.filename == tmp_path / "missing.osu"
    )
    with pytest.raises(IsADirectoryError):
        fosu.parse_file(tmp_path)
    with pytest.raises(ValueError):
        fosu.parse_file("bad\0path")


def test_memory_map_and_wide_buffers_are_detached(tmp_path):
    import mmap

    data = b"[Metadata]\nTitle:Detached\n[HitObjects]\n1,2,3,1,0\n"
    path = tmp_path / "mapped.osu"
    path.write_bytes(data)
    with path.open("rb") as source:
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            beatmap = fosu.parse(mapped)
    path.unlink()
    assert beatmap.title == "Detached"
    assert beatmap.hit_objects[0].start_time == 3

    padded = data + b"\n" * (-len(data) % 4)
    assert fosu.parse(memoryview(padded).cast("I")) == beatmap


def test_size_limit(tmp_path):
    with pytest.raises(ValueError):
        fosu.parse(bytes(64 * 1024 * 1024 + 1))
    with pytest.raises(ValueError):
        fosu.parse(bytearray(64 * 1024 * 1024 + 1))
    path = tmp_path / "oversized.osu"
    with path.open("wb") as output:
        output.truncate(64 * 1024 * 1024 + 1)
    with pytest.raises(ValueError):
        fosu.parse_file(path)


def test_concurrent_calls_return_independent_objects():
    inputs = [
        f"[Metadata]\nTitle:{i}\n[HitObjects]\n1,2,{i},1,0\n".encode()
        for i in range(24)
    ]
    with ThreadPoolExecutor(max_workers=4) as executor:
        results = list(executor.map(fosu.parse, inputs))
    assert [b.title for b in results] == [str(i) for i in range(24)]
    assert [b.hit_objects[0].start_time for b in results] == list(range(24))


def test_backend_errors_in_fresh_process():
    result = subprocess.run(
        [sys.executable, "-c", "import fosu"],
        env={**os.environ, "FOSU_BACKEND": "invalid"},
        capture_output=True,
        check=False,
    )
    assert result.returncode != 0 and b"ImportError" in result.stderr
    result = subprocess.check_output(
        [sys.executable, "-c", "import fosu; print(fosu.backend)"],
        env={**os.environ, "FOSU_BACKEND": "scalar"},
    )
    assert result.strip() == b"scalar"


def test_all_fields_are_detached_python_values():
    b = fosu.parse(b"[HitObjects]\n1,2,3,2,0,L|4:5,1,6\n")
    seen = set()

    def visit(value):
        if id(value) in seen:
            return
        seen.add(id(value))
        if is_dataclass(value):
            for f in fields(value):
                visit(getattr(value, f.name))
        elif isinstance(value, list):
            for item in value:
                visit(item)
        else:
            assert isinstance(value, (int, float, str, bytes, type(None)))

    visit(b)


def test_runtime_annotations():
    assert get_type_hints(fosu.parse)["return"] is fosu.Beatmap
    assert get_type_hints(fosu.parse_file)["return"] is fosu.Beatmap
    assert get_type_hints(fosu.Slider)["end_time"] is type(None)


def test_public_typing_contract():
    subprocess.run(
        [
            sys.executable,
            "-m",
            "mypy",
            "--strict",
            "--no-incremental",
            str(Path(__file__).with_name("typing") / "python_api.py"),
        ],
        check=True,
    )


def test_bookmark_field_trimming_and_internal_whitespace():
    b = fosu.parse("[Editor]\nBookmarks:\u00a01,\u00a02\u00a0,3\u3000\n".encode())
    assert b.bookmarks == [1, 3]


def test_bookmark_trailing_nuls_follow_official_decoder():
    b = fosu.parse(b"[Editor]\nBookmarks:1\0,2\0\0,3\0 \n")
    assert b.bookmarks == [1, 2, 3]
