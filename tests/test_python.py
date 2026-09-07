"""Python API contracts, including ownership beyond the original Beatmap."""
import ast
import gc
import os
from pathlib import Path
import subprocess
import sys
import weakref
from concurrent.futures import ThreadPoolExecutor

import pytest
import fosu

MAP = (
    "osu file format v14\n[General]\nAudioFilename: song.mp3\n"
    "LetterboxInBreaks:1\n[Metadata]\nTitle:日本語\nArtist:artist\nVersion:Hard\nBeatmapID:12345\n"
    "[Difficulty]\nOverallDifficulty:9\nCircleSize:4\n"
    "[Events]\n0,0,\"bg.jpg\",0,0\n2,100,200\n"
    "[Colours]\nCombo1 : 12,34,56\n"
    "[TimingPoints]\n1.25,342.857142857142857142857,4,2,1,60,1,0\n"
    "[HitObjects]\n-48,192,1000,1,0,0:0:0:0:\n"
    "512,192,2000,2,14,B|-129088:1726|123:456,2,240,2|0,0:0|0:0,0:0:0:0:\n"
    "256,192,2147483647,12,0,2147483647,0:0:0:0:\n"
).encode()


def test_parse_and_attributes(tmp_path):
    path = tmp_path / "日本語.osu"
    path.write_bytes(MAP)
    for bm in (fosu.parse(MAP), fosu.parse_file(path), fosu.parse_file(os.fsencode(path))):
        assert isinstance(bm, fosu.Beatmap)
        assert (bm.title, bm.artist, bm.version, bm.ar, bm.cs) == ("日本語", "artist", "Hard", 9, 4)
        assert bm.source_size == len(MAP)
        assert bm.letterbox_in_breaks is True
        assert len(bm.hit_objects) == 3
        assert bm.hit_objects[0].time == 1000
        assert bm.hit_objects[0].slider is None
        assert bm.hit_objects[0].slider_index == fosu.NO_SLIDER
        assert bm.hit_objects[0].is_circle
        assert bm.hit_objects[1].is_slider
        assert bm.hit_objects[2].is_spinner
        assert bm.hit_objects[2].end_time == 2147483647
        slider = bm.hit_objects[1].slider
        assert isinstance(slider, fosu.Slider)
        assert (slider.curve_type, slider.length, slider.slides) == ("B", 240, 2)
        assert [(p.x, p.y) for p in slider.points] == [(-129088, 1726), (123, 456)]
        assert slider.edge_sounds == "2|0"
        assert bm.timing_points[0].uninherited is True
        assert bm.breaks[0].start == 100
        assert list(bm.combo_colours) == [0x0C2238]
        assert bm.stats.fast_path_lines + bm.stats.slow_path_lines == 3
        assert "日本語" in repr(bm)
        assert "time=1000" in repr(bm.hit_objects[0])
        with pytest.raises(AttributeError):
            bm.title = "changed"
        with pytest.raises(AttributeError):
            bm.hit_objects[0].time = 0
        with pytest.raises(TypeError):
            bm.hit_objects[0] = None


def test_empty_selective_and_lossless_strings():
    bm = fosu.parse(b"")
    assert bm.sample_set == "Normal" and not bm.hit_objects
    assert bm.title == ""
    bm = fosu.parse(MAP, sections=fosu.Sections.METADATA | fosu.Sections.DIFFICULTY)
    assert bm.title == "日本語" and bm.ar == 9 and not bm.hit_objects
    bm = fosu.parse(MAP, sections=0)
    assert bm.title == "" and not bm.hit_objects
    bm = fosu.parse(b"[Metadata]\nTitle:hi\xff\x00there\n")
    assert bm.title.encode("utf-8", "surrogateescape") == b"hi\xff\x00there"
    assert bm.title is bm.title  # metadata strings are decoded once


def test_sequence_bounds_and_slices():
    values = fosu.parse(MAP).hit_objects
    times = [x.time for x in values]
    assert values[-1].time == times[-1]
    assert [x.time for x in values[::-1]] == times[::-1]
    assert [x.time for x in values[::2][::-1]] == times[::2][::-1]
    assert list(values[500:]) == []
    assert values[0] in values
    assert values.index(values[1]) == 1
    assert values.count(values[1]) == 1
    assert values[0] == values[0] and values[0] != values[1]
    assert hash(values[0]) == hash(values[0])
    assert values[0] != fosu.parse(MAP).hit_objects[0]
    for index in (-4, 3, 100000000000000000):
        with pytest.raises(IndexError):
            values[index]
    with pytest.raises(TypeError):
        values[1.2]
    with pytest.raises(ValueError):
        values[::0]


@pytest.mark.parametrize("keep", [
    lambda bm: bm.hit_objects[0],
    lambda bm: bm.hit_objects[::-1],
    lambda bm: bm.hit_objects[1].slider,
    lambda bm: bm.sliders[0].points[0],
    lambda bm: bm.stats,
])
def test_records_retain_storage(keep):
    bm = fosu.parse(MAP)
    owner = weakref.ref(bm._owner)
    record = keep(bm)
    del bm
    gc.collect()
    assert owner() is not None
    assert repr(record)
    del record
    gc.collect()
    assert owner() is None


def test_numpy_buffers_and_lifetimes():
    np = pytest.importorskip("numpy")
    for transform in (lambda a: a, lambda a: a[::-1], np.asarray, memoryview,
                      lambda a: a["time"], lambda a: a.view(),
                      lambda a: np.asarray(memoryview(a))):
        bm = fosu.parse(MAP)
        owner = weakref.ref(bm._owner)
        array = bm.hit_objects.to_numpy()
        assert array["time"].tolist() == [x.time for x in bm.hit_objects]
        assert array.dtype.itemsize == 48
        assert array["slider_index"][0] == fosu.NO_SLIDER
        sample = array["hit_sample"][0]
        assert bytes(bm.text[int(sample["offset"]):int(sample["offset"] + sample["length"])]) == b"0:0:0:0:"
        del sample
        with pytest.raises(ValueError):
            array.setflags(write=True)
        kept = transform(array)
        del array, bm
        gc.collect()
        assert owner() is not None
        assert np.asarray(kept).size == 3
        del kept
        gc.collect()
        assert owner() is None


def test_numpy_slices_and_types():
    np = pytest.importorskip("numpy")
    bm = fosu.parse(MAP)
    for name in ("hit_objects", "sliders", "slider_points", "timing_points", "breaks", "combo_colours"):
        sequence = getattr(bm, name)
        expected = sequence.to_numpy()
        for sl in (slice(None), slice(None, None, -1), slice(1, None, 2), slice(50, 100)):
            np.testing.assert_array_equal(sequence[sl].to_numpy(), expected[sl])
        np.testing.assert_array_equal(np.asarray(sequence), expected)
        assert not np.asarray(sequence).flags.writeable
    assert bm.sliders.to_numpy()["curve_type"].tolist() == [b"B"]
    assert bm.sliders[0].points.to_numpy()["x"].tolist() == [-129088, 123]
    empty = fosu.parse(b"").hit_objects.to_numpy()
    assert empty.shape == (0,) and not empty.flags.writeable
    colours = bm.combo_colours
    assert colours.__array__(dtype="f8").dtype == "f8"
    with pytest.raises(ValueError):
        colours.__array__(dtype="f8", copy=False)
    copy = colours.__array__(copy=True)
    assert copy.flags.writeable
    copy[0] = 0
    assert colours[0] == 0x0C2238


def test_errors(tmp_path):
    with pytest.raises(FileNotFoundError) as error:
        fosu.parse_file(tmp_path / "missing.osu")
    assert error.value.filename == str(tmp_path / "missing.osu")
    with pytest.raises(IsADirectoryError):
        fosu.parse_file(tmp_path)
    with pytest.raises(ValueError):
        fosu.parse_file("bad\x00path")
    for value in ("not bytes", 12, None):
        with pytest.raises(TypeError):
            fosu.parse(value)
    for flag in (-1, 1, 1 << 40):
        with pytest.raises(ValueError):
            fosu.parse(MAP, sections=flag)
    with pytest.raises(TypeError):
        fosu.parse(MAP, sections=1.5)


def test_buffer_inputs(tmp_path):
    import array
    import mmap
    for data in (bytearray(MAP), memoryview(MAP), memoryview(bytearray(MAP)),
                 array.array("B", MAP)):
        bm = fosu.parse(data)
        assert bm.title == "日本語" and bytes(bm.text) == MAP
    source = bytearray(MAP)
    bm = fosu.parse(source)
    source[:] = b"x"
    assert bytes(bm.text) == MAP
    with pytest.raises(BufferError):
        fosu.parse(memoryview(MAP)[::2])
    path = tmp_path / "mapped.osu"
    path.write_bytes(MAP)
    with path.open("rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            bm = fosu.parse(data)
    assert bytes(bm.text) == MAP
    # Count bytes rather than elements for buffers with a wider element type.
    padded = MAP + b"\n" * (-len(MAP) % 4)
    words = memoryview(padded).cast("I")
    assert bytes(fosu.parse(words).text) == padded


def test_independent_results_and_threads():
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(fosu.parse, [MAP] * 32))
    assert all(bm.title == "日本語" and bm.hit_objects[1].time == 2000 for bm in results)
    results[0]._strings = None
    assert results[1].title == "日本語"


def test_source_buffer_lifetime():
    bm = fosu.parse(MAP)
    owner = weakref.ref(bm._owner)
    data = bm.text
    del bm
    gc.collect()
    assert owner() is not None and bytes(data) == MAP and data.readonly
    with pytest.raises(TypeError):
        data[0] = 0
    del data
    gc.collect()
    assert owner() is None


def test_lightweight_import_and_scalar_fallback():
    code = "import sys, fosu; assert 'numpy' not in sys.modules; assert fosu.parse(b'').sample_set == 'Normal'"
    subprocess.run([sys.executable, "-c", code], check=True)
    env = dict(os.environ, FOSU_FORCE_SCALAR="1")
    code += "; from fosu._native import SIMD_ENABLED; assert not SIMD_ENABLED; assert 'fosu._native_avx2' not in sys.modules"
    subprocess.run([sys.executable, "-c", code], env=env, check=True)


def test_stubs_cover_the_public_properties():
    stub = Path(fosu.__file__).with_name("__init__.pyi")
    tree = ast.parse(stub.read_text())
    classes = {item.name: item for item in tree.body if isinstance(item, ast.ClassDef)}
    for name in ("Beatmap", "HitObject", "Slider", "Point", "TimingPoint", "Break", "ParseStats"):
        cls = getattr(fosu, name)
        properties = {n for n, v in vars(cls).items() if isinstance(v, property)}
        declared = {n.name for n in classes[name].body if isinstance(n, ast.FunctionDef)}
        assert properties <= declared, (name, properties - declared)


def test_fractional_times_and_malformed_numeric_fields():
    import math
    bm = fosu.parse(
        b'[Metadata]\nTitle:ok\nTitlX:no\n[MetadataFake]\nTitle:no\n'
        b'[Difficulty]\nApproachRate:1e309\n'
        b'[TimingPoints]\n0,500\n1,NaN,4,2,1,100,0,0\n2,NaN,4,2,1,100,1,0\n'
        b'[HitObjects]\n256.5,192,1000.5,1,0\n1,2,2000.25,8,0,3000.75\n'
        b'1,2,4000.5,128,0,5000.75:0:0:0:0:\n'
        b'1,2,6000,2,0,B|1.5:2.5,1,2.5e2\n'
        b'1,2,NaN,1,0\n1,2,3,1,0\x00junk\n'
        b'[Events]\n2,1.25,9.75\n')
    assert bm.title == 'ok' and bm.ar == 5
    assert len(bm.timing_points) == 2
    assert math.isnan(bm.timing_points[1].beat_length)
    assert not bm.timing_points[1].uninherited
    assert [h.time for h in bm.hit_objects] == [1000.5, 2000.25, 4000.5, 6000]
    assert [h.end_time for h in bm.hit_objects] == [0, 3000.75, 5000.75, 0]
    assert bm.hit_objects[0].x == 256
    assert bm.sliders[0].length == 250
    assert [(p.x, p.y) for p in bm.sliders[0].points] == [(1, 2)]
    assert (bm.breaks[0].start, bm.breaks[0].end) == (1.25, 9.75)
    assert bm.stats.malformed_lines == 4
    assert bm.hit_objects.to_numpy()['time'].dtype.kind == 'f'


def test_file_size_limit(tmp_path):
    path = tmp_path / 'oversize.osu'
    with path.open('wb') as f:
        f.truncate(64 * 1024 * 1024 + 1)
    with pytest.raises(ValueError):
        fosu.parse_file(path)
