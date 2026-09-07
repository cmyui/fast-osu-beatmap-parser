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


def test_parse_and_attributes(tmp_path):
    data = (
        "osu file format v14\n[General]\nAudioFilename: song.mp3\n"
        "LetterboxInBreaks:1\n[Metadata]\nTitle:日本語\nArtist:artist\nVersion:Hard\nBeatmapID:12345\n"
        "[Difficulty]\nOverallDifficulty:9\nCircleSize:4\n"
        '[Events]\n0,0,"bg.jpg",0,0\n2,100,200\n'
        "[Colours]\nCombo1 : 12,34,56\n"
        "[TimingPoints]\n1.25,342.857142857142857142857,4,2,1,60,1,0\n"
        "[HitObjects]\n-48,192,1000,1,0,0:0:0:0:\n"
        "512,192,2000,2,14,B|-129088:1726|123:456,2,240,2|0,0:0|0:0,0:0:0:0:\n"
        "256,192,2147483647,12,0,2147483647,0:0:0:0:\n"
    ).encode()
    path = tmp_path / "日本語.osu"
    path.write_bytes(data)
    for bm in (
        fosu.parse(data),
        fosu.parse_file(path),
        fosu.parse_file(os.fsencode(path)),
    ):
        assert isinstance(bm, fosu.Beatmap)
        assert (bm.title, bm.artist, bm.version, bm.ar, bm.cs) == (
            "日本語",
            "artist",
            "Hard",
            9,
            4,
        )
        assert bm.source_size == len(data)
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


def test_empty_input_uses_defaults():
    bm = fosu.parse(b"")
    assert bm.sample_set == "Normal" and not bm.hit_objects
    assert bm.title == ""


def test_section_selection_omits_unrequested_data():
    data = (
        b"[Metadata]\nTitle:Selected\n"
        b"[Difficulty]\nOverallDifficulty:8\n"
        b"[HitObjects]\n64,96,500,1,0\n"
    )
    bm = fosu.parse(data, sections=fosu.Sections.METADATA | fosu.Sections.DIFFICULTY)
    assert bm.title == "Selected" and bm.ar == 8 and not bm.hit_objects


def test_empty_section_selection_returns_defaults():
    data = b"[Metadata]\nTitle:Not selected\n[HitObjects]\n32,48,600,1,0\n"
    bm = fosu.parse(data, sections=0)
    assert bm.title == "" and not bm.hit_objects


def test_metadata_strings_preserve_invalid_utf8_and_null_bytes():
    bm = fosu.parse(b"[Metadata]\nTitle:hi\xff\x00there\n")
    assert bm.title.encode("utf-8", "surrogateescape") == b"hi\xff\x00there"
    assert bm.title is bm.title  # metadata strings are decoded once


def test_sequence_bounds_and_slices():
    data = b"[HitObjects]\n16,32,100,1,0\n32,64,200,1,0\n48,96,300,1,0\n"
    values = fosu.parse(data).hit_objects
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
    assert values[0] != fosu.parse(data).hit_objects[0]
    for index in (-4, 3, 100000000000000000):
        with pytest.raises(IndexError):
            values[index]
    with pytest.raises(TypeError):
        values[1.2]
    with pytest.raises(ValueError):
        values[::0]


@pytest.mark.parametrize(
    "keep",
    [
        lambda bm: bm.hit_objects[0],
        lambda bm: bm.hit_objects[::-1],
        lambda bm: bm.hit_objects[1].slider,
        lambda bm: bm.sliders[0].points[0],
        lambda bm: bm.stats,
    ],
)
def test_records_retain_storage(keep):
    data = b"[HitObjects]\n64,96,700,1,0\n128,192,800,2,0,B|192:96,1,120\n"
    bm = fosu.parse(data)
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
    data = b"[HitObjects]\n24,48,1000,1,0,0:0:0:0:\n48,96,1100,1,0\n72,144,1200,1,0\n"
    np = pytest.importorskip("numpy")
    for transform in (
        lambda a: a,
        lambda a: a[::-1],
        np.asarray,
        memoryview,
        lambda a: a["time"],
        lambda a: a.view(),
        lambda a: np.asarray(memoryview(a)),
    ):
        bm = fosu.parse(data)
        owner = weakref.ref(bm._owner)
        array = bm.hit_objects.to_numpy()
        assert array["time"].tolist() == [x.time for x in bm.hit_objects]
        assert array.dtype.itemsize == 48
        assert array["slider_index"][0] == fosu.NO_SLIDER
        sample = array["hit_sample"][0]
        assert (
            bytes(
                bm.text[
                    int(sample["offset"]) : int(sample["offset"] + sample["length"])
                ]
            )
            == b"0:0:0:0:"
        )
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
    data = (
        b"[Events]\n2,125,250\n"
        b"[Colours]\nCombo1:12,34,56\n"
        b"[TimingPoints]\n0,500\n"
        b"[HitObjects]\n96,192,1300,2,0,B|-129088:1726|123:456,1,240\n"
    )
    np = pytest.importorskip("numpy")
    bm = fosu.parse(data)
    for name in (
        "hit_objects",
        "sliders",
        "slider_points",
        "timing_points",
        "breaks",
        "combo_colours",
    ):
        sequence = getattr(bm, name)
        expected = sequence.to_numpy()
        for sl in (
            slice(None),
            slice(None, None, -1),
            slice(1, None, 2),
            slice(50, 100),
        ):
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
            fosu.parse(b"", sections=flag)
    with pytest.raises(TypeError):
        fosu.parse(b"", sections=1.5)


def test_buffer_inputs(tmp_path):
    original = "[Metadata]\nTitle:日本語\nArtist:Buffer ownership\n".encode()
    import array
    import mmap

    for data in (
        bytearray(original),
        memoryview(original),
        memoryview(bytearray(original)),
        array.array("B", original),
    ):
        bm = fosu.parse(data)
        assert bm.title == "日本語" and bytes(bm.text) == original
    source = bytearray(original)
    bm = fosu.parse(source)
    source[:] = b"x"
    assert bytes(bm.text) == original
    with pytest.raises(BufferError):
        fosu.parse(memoryview(original)[::2])
    path = tmp_path / "mapped.osu"
    path.write_bytes(original)
    with path.open("rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            bm = fosu.parse(data)
    assert bytes(bm.text) == original
    # Count bytes rather than elements for buffers with a wider element type.
    padded = original + b"\n" * (-len(original) % 4)
    words = memoryview(padded).cast("I")
    assert bytes(fosu.parse(words).text) == padded


def test_independent_results_and_threads():
    data = (
        b"[Metadata]\nTitle:Threaded result\n"
        b"[HitObjects]\n128,64,1500,1,0\n192,96,2000,1,0\n"
    )
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(fosu.parse, [data] * 32))
    assert all(
        bm.title == "Threaded result" and bm.hit_objects[1].time == 2000
        for bm in results
    )
    results[0]._strings = None
    assert results[1].title == "Threaded result"


def test_source_buffer_lifetime():
    original = b"[Metadata]\nTitle:Retained source bytes\n"
    bm = fosu.parse(original)
    owner = weakref.ref(bm._owner)
    data = bm.text
    del bm
    gc.collect()
    assert owner() is not None and bytes(data) == original and data.readonly
    with pytest.raises(TypeError):
        data[0] = 0
    del data
    gc.collect()
    assert owner() is None


def test_lightweight_import_and_scalar_fallback():
    code = "import sys, fosu; assert 'numpy' not in sys.modules; assert fosu.parse(b'').sample_set == 'Normal'"
    subprocess.run([sys.executable, "-c", code], check=True)
    env = dict(os.environ, FOSU_FORCE_SCALAR="1")
    env.pop("FOSU_BACKEND", None)
    code += "; from fosu._native import SIMD_ENABLED; assert not SIMD_ENABLED; assert fosu.backend == 'scalar'"
    subprocess.run([sys.executable, "-c", code], env=env, check=True)


def test_stubs_cover_the_public_properties():
    stub = Path(fosu.__file__).with_name("__init__.pyi")
    tree = ast.parse(stub.read_text())
    classes = {item.name: item for item in tree.body if isinstance(item, ast.ClassDef)}
    for name in (
        "Beatmap",
        "HitObject",
        "Slider",
        "Point",
        "TimingPoint",
        "Break",
        "ParseStats",
    ):
        cls = getattr(fosu, name)
        properties = {n for n, v in vars(cls).items() if isinstance(v, property)}
        declared = {
            n.name for n in classes[name].body if isinstance(n, ast.FunctionDef)
        }
        assert properties <= declared, (name, properties - declared)


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
    assert [h.end_time for h in bm.hit_objects] == [0, 3000.75, 5000.75, 0]
    assert bm.hit_objects[0].x == 256
    assert bm.sliders[0].length == 250
    assert [(p.x, p.y) for p in bm.sliders[0].points] == [(1, 2)]
    assert (bm.breaks[0].start, bm.breaks[0].end) == (1.25, 9.75)
    assert bm.stats.malformed_lines == 4
    assert bm.hit_objects.to_numpy()["time"].dtype.kind == "f"


def test_file_size_limit(tmp_path):
    path = tmp_path / "oversize.osu"
    with path.open("wb") as f:
        f.truncate(64 * 1024 * 1024 + 1)
    with pytest.raises(ValueError):
        fosu.parse_file(path)


def test_backend_selection():
    code = "import fosu; print(fosu.backend); assert len(fosu.parse(b'[HitObjects]\\n1,2,3,1,0\\n').hit_objects) == 1"
    from fosu._native import lib

    # Each import has a fresh selection, independent of the parent test process.
    for backend in ("auto", "scalar", "avx2", "invalid"):
        available = backend == "auto" or bool(lib.fosu_backend_available(backend.encode()))
        result = subprocess.run([sys.executable, "-c", code],
                                env=dict(os.environ, FOSU_BACKEND=backend),
                                capture_output=True, text=True)
        assert (result.returncode == 0) == available, result.stderr
        if available:
            expected = ("avx2" if lib.fosu_backend_available(b"avx2") else "scalar") if backend == "auto" else backend
            assert result.stdout.strip() == expected
        else:
            assert "ImportError: FOSU_BACKEND" in result.stderr
