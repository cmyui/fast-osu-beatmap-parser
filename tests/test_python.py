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
from enum import Enum
from pathlib import Path
from typing import get_type_hints

import fosu
import pytest


def test_standard_mods_adjust_difficulty_positions_and_gameplay_time():
    data = (
        b"osu file format v14\n[General]\nMode:0\n"
        b"[Difficulty]\nHPDrainRate:4\nCircleSize:4\nOverallDifficulty:4\n"
        b"ApproachRate:4\nSliderMultiplier:1\n"
        b"[Events]\n2,1500,2000\n[TimingPoints]\n0,500\n"
        b"[HitObjects]\n100,100,1000,2,0,L|200:150,1,100\n"
    )
    map = fosu.parse(
        data,
        calculate_slider_events=True,
        mods=fosu.Mods.HARD_ROCK | fosu.Mods.DOUBLE_TIME,
    )
    assert (map.hp, map.cs, map.od, map.ar) == pytest.approx((5.6, 5.2, 5.6, 5.6))
    slider = map.hit_objects[0]
    assert (slider.x, slider.y, slider.time, slider.end_time) == pytest.approx(
        (100, 284, 1000 / 1.5, 1500 / 1.5)
    )
    assert slider.control_points == [fosu.Point(100, 284), fosu.Point(200, 234)]
    assert [event.time for event in slider.events] == pytest.approx(
        [1000 / 1.5, 1464 / 1.5, 1500 / 1.5]
    )
    assert all(
        event.span_start_time == pytest.approx(1000 / 1.5) for event in slider.events
    )
    assert map.timing_points[0].beat_length == pytest.approx(500 / 1.5)
    assert (map.breaks[0].start, map.breaks[0].end) == pytest.approx(
        (1500 / 1.5, 2000 / 1.5)
    )


@pytest.mark.parametrize(
    "mods,rate",
    [
        (fosu.Mods.DOUBLE_TIME, 1.5),
        (fosu.Mods.NIGHTCORE, 1.5),
        (fosu.Mods.HALF_TIME, 0.75),
    ],
)
def test_rate_mods_support_every_mode_and_preserve_inherited_velocity(mods, rate):
    data = (
        b"[General]\nMode:3\n[TimingPoints]\n300,600\n600,-50,4,0,0,100,0,0\n"
        b"[HitObjects]\n0,0,900,128,0,1500:0:0:0:0:\n"
    )
    map = fosu.parse(data, mods=mods)
    assert (map.hit_objects[0].time, map.hit_objects[0].end_time) == pytest.approx(
        (900 / rate, 1500 / rate)
    )
    assert [point.time for point in map.timing_points] == pytest.approx(
        [300 / rate, 600 / rate]
    )
    assert map.timing_points[0].beat_length == pytest.approx(600 / rate)
    assert map.timing_points[1].beat_length == -50


def test_taiko_difficulty_mods_follow_mode_specific_rules():
    data = (
        b"[General]\nMode:1\n[Difficulty]\nHPDrainRate:4\nCircleSize:4\n"
        b"OverallDifficulty:4\nApproachRate:4\nSliderMultiplier:1\n"
        b"[HitObjects]\n100,100,1000,1,0\n"
    )
    easy = fosu.parse(data, mods=fosu.Mods.EASY)
    assert (
        easy.hp,
        easy.cs,
        easy.od,
        easy.ar,
        easy.slider_multiplier,
    ) == pytest.approx((2, 2, 2, 2, 0.8))
    hard_rock = fosu.parse(data, mods=fosu.Mods.HARD_ROCK)
    assert (hard_rock.hp, hard_rock.cs, hard_rock.od, hard_rock.ar) == pytest.approx(
        (5.6, 4, 5.6, 4)
    )
    assert hard_rock.slider_multiplier == pytest.approx(1.4 * 4 / 3)
    assert hard_rock.hit_objects[0].y == 100


@pytest.mark.parametrize("mode", [2, 3])
def test_catch_and_mania_reject_unimplemented_difficulty_mods(mode):
    data = f"[General]\nMode:{mode}\n[Difficulty]\nCircleSize:4\n".encode()
    with pytest.raises(ValueError, match="invalid input"):
        fosu.parse(data, mods=fosu.Mods.EASY)


@pytest.mark.parametrize(
    "mods",
    [
        fosu.Mods.EASY | fosu.Mods.HARD_ROCK,
        fosu.Mods.DOUBLE_TIME | fosu.Mods.HALF_TIME,
        fosu.Mods.NIGHTCORE | fosu.Mods.HALF_TIME,
        fosu.Mods(1 << 30),
    ],
)
def test_invalid_mod_combinations_are_rejected(mods):
    with pytest.raises(ValueError, match="invalid input"):
        fosu.parse(b"", mods=mods)


def test_difficulty_mods_require_general_and_difficulty_sections():
    with pytest.raises(ValueError, match="invalid input"):
        fosu.parse(
            b"[HitObjects]\n0,0,0,1,0\n",
            sections=fosu.Sections.HIT_OBJECTS,
            mods=fosu.Mods.HARD_ROCK,
        )


@pytest.mark.parametrize(
    "curve,length,distance",
    [
        ("L|110:20", 150, 150),
        ("L|110:20", 50, 50),
        ("L|110:20|110:20", 150, 100),
        ("B|10:20", 100, 0),
        ("B|110:120|210:20", 200, 200),
        ("P|110:120|210:20", 200, 200),
        ("C|110:120|210:20", 200, 200),
    ],
)
def test_retained_slider_paths(tmp_path, curve, length, distance):
    data = f"osu file format v14\n[HitObjects]\n10,20,1000,2,0,{curve},2,{length}\n".encode()
    source = tmp_path / "path.osu"
    source.write_bytes(data)
    raw = fosu.parse(data).hit_objects[0]
    assert raw.path is None
    map = fosu.parse(data, calculate_slider_paths=True)
    assert map == fosu.parse_file(source, calculate_slider_paths=True)
    slider = map.hit_objects[0]
    path = slider.path
    assert path is not None and path.distance() == pytest.approx(distance)
    assert slider.end_time == 0
    assert len(path.points) == len(path.cumulative_lengths)
    assert path.cumulative_lengths == sorted(path.cumulative_lengths)
    assert fosu.slider_position_at(path, -1) == path.points[0]
    assert fosu.slider_position_at(path, 2) == path.points[-1]
    assert pickle.loads(pickle.dumps(map)) == map
    assert deepcopy(map) == map
    timed = fosu.parse(data, calculate_slider_end_times=True)
    both = fosu.parse(
        data, calculate_slider_paths=True, calculate_slider_end_times=True
    )
    assert timed.hit_objects[0].end_time == both.hit_objects[0].end_time


def test_path_query_is_detached_and_does_not_mutate():
    data = b"[HitObjects]\n10,20,0,2,0,L|110:20,1,100\n"
    path = fosu.parse(data, calculate_slider_paths=True).hit_objects[0].path
    before = deepcopy(path)
    point = fosu.slider_position_at(path, 0.5)
    assert point == fosu.PathPoint(50.0, 0.0)
    point.x = 999
    fosu.parse(b"")
    assert path == before


@pytest.mark.parametrize("version,tick_count", [(7, 6), (8, 2)])
def test_slider_event_ticks_repeats_and_versions(tmp_path, version, tick_count):
    data = (
        f"osu file format v{version}\n[Difficulty]\nSliderMultiplier:1\nSliderTickRate:1\n"
        "[TimingPoints]\n0,500\n0,-50,4,0,0,100,0,0\n"
        "[HitObjects]\n0,0,1000,2,0,L|400:0,2,400\n"
    ).encode()
    path = tmp_path / "events.osu"
    path.write_bytes(data)
    assert fosu.parse(data).hit_objects[0].events == []
    map = fosu.parse(data, calculate_slider_events=True)
    assert map == fosu.parse_file(path, calculate_slider_events=True)
    slider = map.hit_objects[0]
    events = slider.events
    assert events[0].type is fosu.SliderEventType.HEAD
    assert events[-1].type is fosu.SliderEventType.TAIL
    assert events[-1].time == slider.end_time == 3000
    assert events[-1].position == fosu.PathPoint(0, 0)
    legacy = [
        event for event in events if event.type is fosu.SliderEventType.LEGACY_LAST_TICK
    ]
    assert len(legacy) == 1
    assert legacy[0].time == 2964
    assert legacy[0].path_progress == pytest.approx(0.036)
    assert (legacy[0].position.x, legacy[0].position.y) == pytest.approx((14.4, 0))
    assert sum(e.type is fosu.SliderEventType.TICK for e in events) == tick_count
    repeats = [e for e in events if e.type is fosu.SliderEventType.REPEAT]
    assert len(repeats) == 1 and repeats[0].time == 2000
    assert repeats[0].position == fosu.PathPoint(400, 0)
    path_events = [
        event
        for event in events
        if event.type is not fosu.SliderEventType.LEGACY_LAST_TICK
    ]
    assert [event.time for event in path_events] == sorted(
        event.time for event in path_events
    )
    assert pickle.loads(pickle.dumps(map)) == map
    for e in events:
        position = fosu.slider_position_at(slider.path, e.path_progress)
        assert (e.position.x, e.position.y) == pytest.approx((position.x, position.y))


@pytest.mark.parametrize("curve,length", [("L|100:0", 100), ("B|0:0", 0)])
def test_slider_events_without_ticks(curve, length):
    data = (
        "[TimingPoints]\n0,500\n0,NaN,4,0,0,100,0,0\n"
        f"[HitObjects]\n0,0,0,2,0,{curve},1,{length}\n"
    ).encode()
    events = fosu.parse(data, calculate_slider_events=True).hit_objects[0].events
    assert [e.type for e in events] == [
        fosu.SliderEventType.HEAD,
        fosu.SliderEventType.LEGACY_LAST_TICK,
        fosu.SliderEventType.TAIL,
    ]
    assert all(math.isfinite(e.time) and math.isfinite(e.path_progress) for e in events)


def test_short_slider_legacy_last_tick_clamps_to_half_duration():
    data = (
        b"[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n0,500\n"
        b"[HitObjects]\n0,0,1000,2,0,L|10:0,1,10\n"
    )
    slider = fosu.parse(data, calculate_slider_events=True).hit_objects[0]
    legacy = next(
        event
        for event in slider.events
        if event.type is fosu.SliderEventType.LEGACY_LAST_TICK
    )
    assert slider.end_time == 1050
    assert (legacy.time, legacy.path_progress, legacy.position.x) == pytest.approx(
        (1025, 0.5, 5)
    )


@pytest.mark.parametrize(
    "version,heights", [(5, [2, 1, 0]), (6, [2, 1, 0]), (14, [2, 1, 0])]
)
def test_circle_stacking_applies_positions_and_recovers_raw_position(
    tmp_path, version, heights
):
    data = (
        f"osu file format v{version}\n[HitObjects]\n"
        "100,100,1000,1,0\n100,100,1100,1,0\n100,100,1200,1,0\n"
    ).encode()
    assert all(h.stacking is None for h in fosu.parse(data).hit_objects)
    source = tmp_path / "stacks.osu"
    source.write_bytes(data)
    map = fosu.parse(data, apply_stacking=True)
    assert map == fosu.parse_file(source, apply_stacking=True)
    assert [h.stacking.stack_height for h in map.hit_objects] == heights
    assert all(h.raw_position() == pytest.approx((100, 100)) for h in map.hit_objects)
    assert map.hit_objects[0].x == pytest.approx(93.597376, abs=1e-5)
    assert map.hit_objects[0].y == map.hit_objects[0].x
    assert map.hit_objects[0].stacking.stack_offset.x == pytest.approx(
        -6.402624, abs=1e-5
    )
    assert pickle.loads(pickle.dumps(map)) == map
    assert deepcopy(map) == map
    map.hit_objects[0].x = 123.5
    assert map.hit_objects[0].raw_position() == pytest.approx(
        (123.5 - map.hit_objects[0].stacking.stack_offset.x, 100)
    )


def test_stacked_slider_control_points_and_relative_geometry():
    data = (
        b"osu file format v14\n[Difficulty]\nSliderMultiplier:1\n"
        b"[TimingPoints]\n0,500\n[HitObjects]\n"
        b"100,100,1000,2,0,L|200:100,2,100\n100,100,1100,2,0,L|200:100,1,100\n"
    )
    raw = fosu.parse(data, calculate_slider_events=True)
    stacked = fosu.parse(data, calculate_slider_events=True, apply_stacking=True)
    a, b = stacked.hit_objects
    assert a.stacking.stack_height == 1
    assert a.raw_position() == pytest.approx((100, 100))
    assert a.x < 100 and a.y < 100
    assert a.control_points[0] == fosu.Point(a.x, a.y)
    assert a.control_points[1].x == pytest.approx(
        200 + a.stacking.stack_offset.x, abs=1e-5
    )
    assert a.control_points[1].y == a.y
    assert a.path == raw.hit_objects[0].path
    assert a.events == raw.hit_objects[0].events
    assert b.raw_position() == (b.x, b.y)
    assert isinstance(a.x, float) and isinstance(raw.hit_objects[0].x, float)
    assert pickle.loads(pickle.dumps(stacked)) == stacked


def test_raw_position_without_stacking_and_after_other_normalization():
    notes = fosu.parse(b"[HitObjects]\n10,20,100,1,0\n0,0,200,8,0,300\n").hit_objects
    assert notes[0].raw_position() == (10.0, 20.0)
    assert notes[1].raw_position() == (256.0, 192.0)
    notes[0].x = -1.5
    assert notes[0].raw_position() == (-1.5, 20.0)


@pytest.mark.parametrize("version", [5, 6, 14])
def test_slider_tail_negative_stacks(version):
    data = (
        f"osu file format v{version}\n[Difficulty]\nSliderMultiplier:1\n"
        "[TimingPoints]\n0,500\n[HitObjects]\n"
        "0,0,1000,2,0,L|100:0,1,100\n100,0,1550,1,0\n100,0,1600,1,0\n"
    ).encode()
    map = fosu.parse(data, apply_stacking=True)
    assert [h.stacking.stack_height for h in map.hit_objects] == [0, -1, -2]
    assert map.hit_objects[0].end_time > 0
    assert map.hit_objects[0].events == []


@pytest.mark.parametrize(
    "fixture,heights",
    [
        ("stacking-slider-end-precision.osu", [0, -1, 0, -1, -2]),
        ("stacking-zero-leniency.osu", [0, -1]),
    ],
)
def test_stacking_uses_standard_slider_end_time_precision(fixture, heights):
    source = Path(__file__).parent / "fixtures" / "official" / fixture
    map = fosu.parse_file(source, apply_stacking=True)
    assert [object.stacking.stack_height for object in map.hit_objects] == heights


@pytest.mark.parametrize("mode", [1, 2, 3])
def test_stacking_does_not_convert_other_modes(mode):
    data = (
        f"[General]\nMode:{mode}\n[HitObjects]\n100,100,0,1,0\n100,100,1,1,0\n".encode()
    )
    assert fosu.parse(data, apply_stacking=True) == fosu.parse(data)


def test_modern_stacking_time_distance_and_spinner_boundaries():
    data = (
        b"osu file format v14\n[Difficulty]\nApproachRate:10\n"
        b"[HitObjects]\n100,100,0,1,0\n100,100,316,1,0\n103,100,400,1,0\n"
        b"0,0,410,8,0,420\n103,100,500,1,0\n"
    )
    map = fosu.parse(data, apply_stacking=True)
    assert [h.stacking.stack_height for h in map.hit_objects] == [0, 0, 1, 0, 0]


@pytest.mark.parametrize("file", [False, True])
def test_selected_sections(tmp_path, file):
    data = (
        b"osu file format v14\n[General]\nMode:3\n"
        b"[Editor]\nBookmarks:100,200\n"
        b"[Metadata]\nTitle:selected\n"
        b"[Difficulty]\nCircleSize:14\nOverallDifficulty:8\n"
        b"[Events]\n2,100,200\n[TimingPoints]\n0,500\n"
        b"[Colours]\nCombo1:12,34,56\n[HitObjects]\n1,2,3,1,0\n"
    )
    path = tmp_path / "selected.osu"
    path.write_bytes(data)

    def parse(sections):
        if file:
            return fosu.parse_file(path, sections=sections)
        return fosu.parse(data, sections=sections)

    full = parse(fosu.Sections.ALL)
    assert full == (fosu.parse_file(path) if file else fosu.parse(data))
    fields_by_section = {
        fosu.Sections.GENERAL: ("mode",),
        fosu.Sections.EDITOR: ("bookmark_list",),
        fosu.Sections.METADATA: ("title",),
        fosu.Sections.DIFFICULTY: ("od", "ar"),
        fosu.Sections.EVENTS: ("breaks",),
        fosu.Sections.TIMING_POINTS: ("timing_points",),
        fosu.Sections.COLOURS: ("combo_colours",),
        fosu.Sections.HIT_OBJECTS: ("hit_objects",),
    }
    defaults = fosu.parse(b"")
    for section in fields_by_section:
        result = parse(section)
        for other_section, names in fields_by_section.items():
            expected = full if other_section == section else defaults
            for name in names:
                assert getattr(result, name) == getattr(expected, name)
    listing = parse(fosu.Sections.GENERAL | fosu.Sections.DIFFICULTY)
    assert listing.cs == 14 and listing.mode == fosu.GameMode.MANIA
    assert listing.hit_objects == []
    empty = parse(fosu.Sections(0))
    assert empty.title == defaults.title and empty.hit_objects == []
    assert empty.format_version == 14


@pytest.mark.parametrize("sections", [1, 1 << 9, 1 << 32, 1 << 100, -1])
def test_invalid_section_masks(tmp_path, sections):
    with pytest.raises((ValueError, OverflowError)):
        fosu.parse(b"", sections=sections)
    with pytest.raises((ValueError, OverflowError)):
        fosu.parse_file(tmp_path / "missing.osu", sections=sections)


def test_skipped_sections_do_not_count_malformed_records():
    data = b"[Metadata]\nTitle:ok\n[HitObjects]\ninvalid\n"
    assert fosu.parse(data).stats.malformed_lines == 1
    selected = fosu.parse(data, sections=fosu.Sections.METADATA)
    assert selected.title == "ok" and selected.stats.malformed_lines == 0


@pytest.mark.parametrize(
    "curve,length,distance",
    [
        ("L|100:0", "140", 140),
        ("L|100:0", "0", 100),
        ("L|100:0", None, 100),
        ("L|100:0|100:0", "200", 100),
        ("B|0:0", "200", 0),
    ],
)
def test_slider_end_time_uses_effective_distance(curve, length, distance):
    tail = "" if length is None else f",{length}"
    data = (
        "[Difficulty]\nSliderMultiplier:1\n"
        "[TimingPoints]\n0,500\n0,-50,4,0,0,100,0,0\n"
        f"[HitObjects]\n0,0,1000,2,0,{curve},2{tail}\n"
    ).encode()
    slider = fosu.parse(data, calculate_slider_end_times=True).hit_objects[0]
    assert isinstance(slider, fosu.Slider)
    assert slider.end_time == 1000 + 2 * distance / (200 / 500)


def test_slider_end_time_without_timing_sections():
    data = b"[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n0,500\n[HitObjects]\n0,0,0,2,0,L|100:0,1,140\n"
    assert (
        fosu.parse(data, calculate_slider_end_times=True).hit_objects[0].end_time == 700
    )
    # Selected sections alone determine the result; omitted settings use defaults.
    assert fosu.parse(
        data, sections=fosu.Sections.HIT_OBJECTS, calculate_slider_end_times=True
    ).hit_objects[0].end_time == pytest.approx(1000)


def test_slider_end_time_opt_out(tmp_path):
    data = (
        b"[HitObjects]\n0,0,1000,1,0\n0,0,2000,2,0,L|100:0,1,140\n"
        b"0,0,3000,8,0,4000\n0,0,5000,128,0,6000\n"
    )
    path = tmp_path / "map.osu"
    path.write_bytes(data)
    skipped = fosu.parse(data, calculate_slider_end_times=False)
    assert skipped == fosu.parse_file(path, calculate_slider_end_times=False)
    assert [h.end_time for h in skipped.hit_objects] == [1000, 0, 4000, 6000]
    assert fosu.parse(data) == skipped
    assert fosu.parse_file(path) == skipped
    assert (
        fosu.parse(data, calculate_slider_end_times=True).hit_objects[1].end_time > 2000
    )
    assert (
        fosu.parse_file(path, calculate_slider_end_times=True).hit_objects[1].end_time
        > 2000
    )
    for copied in (skipped, deepcopy(skipped), pickle.loads(pickle.dumps(skipped))):
        assert copied.hit_objects[1].end_time == 0
    assert asdict(skipped)["hit_objects"][1]["end_time"] == 0


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
    assert [h.time for h in bm.hit_objects] == [1000.5, 2000.25, 4000.5, 6000]
    assert [h.end_time for h in bm.hit_objects] == [1000.5, 3000.75, 5000.75, 0]
    assert bm.hit_objects[0].x == 256
    assert bm.hit_objects[3].length == 250
    assert [(p.x, p.y) for p in bm.hit_objects[3].control_points[1:]] == [(1, 2)]
    assert (bm.breaks[0].start, bm.breaks[0].end) == (1.25, 9.75)
    assert bm.stats.malformed_lines == 4


def test_complete_map(tmp_path):
    data = (
        "osu file format v14\n[General]\nAudioFilename:song.mp3\nMode:3\n"
        "LetterboxInBreaks:1\nSampleVolume:73\n[Editor]\n"
        "Bookmarks:100,-200,300\nVelocityPresets:1,1.5,2\n"
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
    b = fosu.parse(data, calculate_slider_end_times=True)
    assert (
        b
        == fosu.parse_file(path, calculate_slider_end_times=True)
        == fosu.parse_file(os.fsencode(path), calculate_slider_end_times=True)
    )
    assert (b.title, b.artist, b.version, b.ar, b.cs) == (
        "日本語",
        "artist",
        "Hard",
        9,
        4,
    )
    assert b.mode is fosu.GameMode.MANIA and b.letterbox_in_breaks is True
    assert b.sample_volume == 73
    assert b.bookmark_list == [100, -200, 300]
    assert b.velocity_presets == [1, 1.5, 2]
    assert isinstance(b.hit_objects, list) and len(b.hit_objects) == 4
    circle, slider, spinner, hold = b.hit_objects
    assert isinstance(circle, fosu.Circle) and circle.is_circle
    assert circle.end_time == circle.time == 1000
    assert isinstance(slider, fosu.Slider)
    assert slider.end_time == pytest.approx(
        2000 + 480 / (140 / b.timing_points[0].beat_length)
    )
    assert (slider.slides, slider.curve_type, slider.length) == (
        2,
        fosu.CurveType.BEZIER,
        240,
    )
    assert (
        slider.hitsound
        == fosu.HitSound.WHISTLE | fosu.HitSound.FINISH | fosu.HitSound.CLAP
    )
    assert [(p.x, p.y) for p in slider.control_points] == [
        (512, 192),
        (-129088, 1726),
        (123, 456),
    ]
    assert slider.edge_sounds == "2|0"
    assert spinner.is_spinner and spinner.end_time == 4000
    assert hold.is_hold and hold.end_time == 6000
    assert b.timing_points[0].uninherited is True
    assert b.breaks == [fosu.Break(100, 200)]
    assert b.combo_colours == [0x0C2238]
    assert b.stats.fast_path_lines + b.stats.slow_path_lines == 4
    assert "hit_objects=4" in repr(b) and len(repr(b)) < 150


def test_modern_curve_segments_drive_path_calculation():
    slider = fosu.parse(
        b"osu file format v128\n[HitObjects]\n"
        b"10,20,100,2,0,B2|30.5:40.25|50:60|L|70.75:80.5,1,100\n",
        calculate_slider_paths=True,
    ).hit_objects[0]
    assert isinstance(slider, fosu.Slider)
    assert [(segment.type, segment.degree) for segment in slider.curve_segments] == [
        (fosu.CurveType.BEZIER, 2),
        (fosu.CurveType.LINEAR, None),
    ]
    assert [
        [(point.x, point.y) for point in segment.control_points]
        for segment in slider.curve_segments
    ] == [
        [(10, 20), (30.5, 40.25), (50, 60), (70.75, 80.5)],
        [(70.75, 80.5)],
    ]
    assert slider.path is not None
    assert len(slider.path.points) > 2

    degree = fosu.parse(
        b"osu file format v128\n[HitObjects]\n"
        b"0,0,100,2,0,B2|100:0|100:100|0:100,1,300\n"
    ).hit_objects[0]
    assert isinstance(degree, fosu.Slider)
    assert [(segment.type, segment.degree) for segment in degree.curve_segments] == [
        (fosu.CurveType.BEZIER, 2)
    ]

    legacy = fosu.parse(
        b"osu file format v14\n[HitObjects]\n10,20,100,2,0,B|30.5:40.25,1,100\n"
    ).hit_objects[0]
    assert isinstance(legacy, fosu.Slider)
    assert legacy.curve_segments == []


@pytest.mark.parametrize("length", ["300", "0", ""])
def test_lazer_slider_timing_matches_retained_path(length):
    slider_tail = f",1,{length}" if length else ",1"
    data = (
        "osu file format v128\n[Difficulty]\nSliderMultiplier:1.4\n"
        "[TimingPoints]\n0,500\n[HitObjects]\n"
        f"0,0,1000,2,0,B2|100:0|100:100|100:100{slider_tail}\n"
    ).encode()
    timed = fosu.parse(data, calculate_slider_end_times=True).hit_objects[0]
    retained = fosu.parse(
        data, calculate_slider_paths=True, calculate_slider_end_times=True
    ).hit_objects[0]
    with_events = fosu.parse(data, calculate_slider_events=True).hit_objects[0]
    assert timed.end_time == retained.end_time == with_events.end_time


def test_empty_input_defaults():
    b = fosu.parse(b"")
    assert b.title == "" and b.sample_set is fosu.SampleSet.NORMAL
    assert b.hit_objects == b.timing_points == b.breaks == b.bookmark_list == []
    assert b.mode is fosu.GameMode.OSU
    assert b.beatmap_id is None and b.preview_time is None


def test_omitted_general_uses_defaults():
    b = fosu.parse(b"[Metadata]\nTitle:Only metadata\n")
    assert b.title == "Only metadata" and b.audio_filename == ""
    assert b.mode is fosu.GameMode.OSU
    assert b.velocity_presets == [0.75, 1, 1.5]


def test_omitted_metadata_uses_defaults():
    b = fosu.parse(b"[General]\nAudioLeadIn:450\n")
    assert b.audio_lead_in == 450 and b.title == "" and b.tag_list == []


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
    assert b.tag_list == ["alpha", "beta", "alpha"]
    assert b.tags == "alpha  beta alpha" and b.bookmarks == "12, -30,40"
    b.tag_list.append("new")
    assert b.tag_list[-1] == "new"


def test_invalid_bookmarks_follow_official_decoder():
    b = fosu.parse(
        b"[Editor]\nBookmarks:10,bad,20,2147483648,-2147483648,1_000,,+30,4e1\n"
    )
    assert b.bookmark_list == [10, 20, -2147483648, 30]
    assert "bad" in b.bookmarks and b.stats.malformed_lines == 0


def test_utf8_and_embedded_nul_roundtrip():
    data = b"[Metadata]\nTitle:hello\xff\x00world\n"
    b = fosu.parse(data)
    assert b.title.encode("utf-8", "surrogateescape") == b"hello\xff\x00world"


def test_inherited_nan_survives_and_time_uses_official_offset_arithmetic():
    import struct

    b = fosu.parse(
        b"[TimingPoints]\n0,500\n10,NaN,4,1,0,100,0,0\n[HitObjects]\n1,2,-0,1,0\n"
    )
    assert math.isnan(b.timing_points[1].beat_length)
    assert struct.pack("<d", b.hit_objects[0].time) == struct.pack("<d", 0.0)


def test_kind_precedence_and_combo_flags():
    b = fosu.parse(
        b"[HitObjects]\n1,2,3,11,0\n10,20,30,10,0,L|40:50,1,60\n50,60,70,53,0\n"
    )
    a, s, c = b.hit_objects
    assert isinstance(a, fosu.Circle) and isinstance(s, fosu.Slider)
    assert c.new_combo is True and c.combo_skip == 3 and c.type == 53


def test_stable_time_order_preserves_slider_data():
    b = fosu.parse(
        b"[HitObjects]\n10,20,300,2,0,L|40:50,1,60\n"
        b"60,70,100,2,0,B|80:90|100:110,2,120\n"
        b"120,130,100,1,0\n"
    )
    first, circle, last = b.hit_objects
    assert isinstance(first, fosu.Slider) and isinstance(last, fosu.Slider)
    assert isinstance(circle, fosu.Circle)
    assert [h.time for h in b.hit_objects] == [100, 100, 300]
    assert first.length == 120 and first.slides == 2
    assert first.control_points == [
        fosu.Point(60, 70),
        fosu.Point(80, 90),
        fosu.Point(100, 110),
    ]
    assert last.length == 60 and last.slides == 1
    assert last.control_points == [fosu.Point(10, 20), fosu.Point(40, 50)]


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
    assert (note.x, note.y, note.time) == (99, 48, 600)


def test_points_are_independent_between_results():
    data = b"[HitObjects]\n10,20,30,2,0,L|40:50,1,60\n"
    b = fosu.parse(data)
    other = fosu.parse(data)
    b.hit_objects[0].control_points[1].x = 70
    assert b.hit_objects[0].control_points[1].x == 70
    assert other.hit_objects[0].control_points[1].x == 40


def test_repeated_timestamp_fields_remain_independently_assignable():
    b = fosu.parse(
        b"[HitObjects]\n1,2,1000.5,1,0\n1,2,2000.5,8,0,3000.5\n"
        b"1,2,4000.5,128,0,5000.5\n"
    )
    circle, spinner, hold = b.hit_objects
    circle.end_time = 10
    assert circle.time == 1000.5
    for note, start in ((spinner, 2000.5), (hold, 4000.5)):
        note.end_time = 20
        assert note.time == start


def test_hitsound_flags_preserve_combinations_and_unknown_bits():
    values = [*range(16), 32, 33]
    data = b"[HitObjects]\n" + b"".join(
        f"1,2,{i},1,{value}\n".encode() for i, value in enumerate(values)
    )
    b = fosu.parse(data)
    assert b.stats.malformed_lines == 0
    assert [int(note.hitsound) for note in b.hit_objects] == values
    assert all(isinstance(note.hitsound, fosu.HitSound) for note in b.hit_objects)


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
    assert [h.time for h in b.hit_objects] == [12, 9]
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


def test_parse_file_accepts_unicode_paths(tmp_path):
    path = tmp_path / "日本語.osu"
    path.write_bytes(b"[Metadata]\nTitle:Unicode path\n")
    assert fosu.parse_file(path).title == "Unicode path"


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
    assert beatmap.hit_objects[0].time == 3

    padded = data + b"\n" * (-len(data) % 4)
    assert fosu.parse(memoryview(padded).cast("I")) == beatmap


def test_concurrent_calls_return_independent_objects():
    inputs = [
        f"[Metadata]\nTitle:{i}\n[HitObjects]\n1,2,{i},1,0\n".encode()
        for i in range(24)
    ]
    with ThreadPoolExecutor(max_workers=4) as executor:
        results = list(executor.map(fosu.parse, inputs))
    assert [b.title for b in results] == [str(i) for i in range(24)]
    assert [b.hit_objects[0].time for b in results] == list(range(24))


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
            assert isinstance(value, (int, float, str, bytes, Enum, type(None)))

    visit(b)


def test_runtime_annotations():
    assert get_type_hints(fosu.parse)["return"] is fosu.Beatmap
    assert get_type_hints(fosu.parse_file)["return"] is fosu.Beatmap
    assert get_type_hints(fosu.Slider)["end_time"] is float


def test_public_typing_contract():
    subprocess.run(
        [
            sys.executable,
            "-m",
            "mypy",
            "--config-file",
            str(Path(__file__).resolve().parents[1] / "pyproject.toml"),
            "--python-version",
            f"{sys.version_info.major}.{sys.version_info.minor}",
            "--no-incremental",
            str(Path(__file__).with_name("typing") / "python_api.py"),
        ],
        check=True,
    )


def test_native_stub_contract():
    subprocess.run(
        [
            sys.executable,
            "-m",
            "mypy.stubtest",
            "--mypy-config-file",
            str(Path(__file__).resolve().parents[1] / "pyproject.toml"),
            "fosu._core",
        ],
        check=True,
    )


def test_enum_values_and_malformed_records():
    b = fosu.parse(
        b"[General]\nSampleSet:2\nSampleSet:99\n"
        b"[TimingPoints]\n0,500,4,0,0,100,1,0\n1,500,4,9,0,100,1,0\n"
        b"[HitObjects]\n0,0,1,2,0,X|1:2,1,30\n0,0,2,2,0,P|1:2,1,30\n"
    )
    assert b.sample_set is fosu.SampleSet.SOFT
    assert b.stats.malformed_lines == 3
    assert len(b.timing_points) == len(b.hit_objects) == 1
    assert b.timing_points[0].sample_set is fosu.SampleSet.NONE
    assert b.hit_objects[0].curve_type is fosu.CurveType.PERFECT_CURVE
    assert fosu.parse(b"[General]\nSampleSet:None\n").sample_set is fosu.SampleSet.NONE


def test_record_constructors_are_keyword_only():
    b = fosu.parse(
        b"[TimingPoints]\n0,500\n[HitObjects]\n0,0,1,1,0\n"
        b"0,0,2,2,0,L|1:2,1,30\n0,0,3,8,0,4\n0,0,5,128,0,6\n"
    )
    for record in [b, b.stats, *b.timing_points, *b.hit_objects]:
        values = {field.name: getattr(record, field.name) for field in fields(record)}
        assert type(record)(**values) == record
        with pytest.raises(TypeError):
            type(record)(*values.values())


def test_bookmark_field_trimming_and_internal_whitespace():
    b = fosu.parse("[Editor]\nBookmarks:\u00a01,\u00a02\u00a0,3\u3000\n".encode())
    assert b.bookmark_list == [1, 3]


def test_bookmark_trailing_nuls_follow_official_decoder():
    b = fosu.parse(b"[Editor]\nBookmarks:1\0,2\0\0,3\0 \n")
    assert b.bookmark_list == [1, 2, 3]
