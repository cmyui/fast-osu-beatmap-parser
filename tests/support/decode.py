"""Decode the private canonical stream emitted by tests/reference/native.cc."""

import struct
import sys
from typing import Any


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data = memoryview(data)
        self.pos = 0

    def take(self, size: int) -> memoryview:
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError("truncated stream")
        result = self.data[self.pos : self.pos + size]
        self.pos += size
        return result

    def value(self, code: str) -> Any:
        return struct.unpack("<" + code, self.take(struct.calcsize("<" + code)))[0]

    def string(self) -> bytes:
        return bytes(self.take(self.value("I")))

    def fields(self, target: dict[str, Any], code: str, names: str) -> None:
        for name in names.split():
            target[name] = self.string() if code == "s" else self.value(code)

    def done(self) -> None:
        if self.pos != len(self.data):
            raise ValueError("unexpected trailing bytes")


def decode(data: bytes) -> dict[str, Any]:
    if len(data) < 36 or data[:8] != b"FOSUDMP8":
        raise ValueError("not a FOSUDMP8 stream")
    trailer_size = struct.unpack("<Q", data[-8:])[0]
    start = len(data) - 8 - trailer_size
    if start < 8:
        raise ValueError("invalid trailer length")
    reader = Reader(data[start:-8])
    if reader.take(4) != b"TRLR":
        raise ValueError("missing trailer")
    object_count, slider_count, point_count = (reader.value("I") for _ in range(3))
    if object_count > len(data) // 40 or point_count > len(data) // 8:
        raise ValueError("impossible counts")
    metadata: dict[str, Any] = {}
    reader.fields(metadata, "i", "format_version")
    reader.fields(metadata, "s", "audio_filename")
    reader.fields(metadata, "i", "audio_lead_in preview_time countdown")
    reader.fields(metadata, "s", "sample_set")
    reader.fields(metadata, "d", "stack_leniency")
    reader.fields(metadata, "i", "mode")
    reader.fields(
        metadata,
        "B",
        "letterbox_in_breaks widescreen_storyboard epilepsy_warning special_style use_skin_sprites samples_match_playback_rate",
    )
    reader.fields(metadata, "i", "countdown_offset")
    reader.fields(metadata, "s", "overlay_position skin_preference bookmarks")
    reader.fields(metadata, "d", "distance_spacing")
    reader.fields(metadata, "i", "beat_divisor grid_size")
    reader.fields(metadata, "d", "timeline_zoom")
    reader.fields(
        metadata,
        "s",
        "title title_unicode artist artist_unicode creator version source tags",
    )
    reader.fields(metadata, "q", "beatmap_id beatmap_set_id")
    reader.fields(metadata, "d", "hp cs od ar slider_multiplier slider_tick_rate")
    reader.fields(metadata, "s", "background video")
    breaks = [(reader.value("d"), reader.value("d")) for _ in range(reader.value("I"))]
    colours = [reader.value("I") for _ in range(reader.value("I"))]
    timing = []
    for _ in range(reader.value("I")):
        timing_point: dict[str, Any] = {}
        reader.fields(timing_point, "d", "time beat_length")
        reader.fields(timing_point, "i", "meter sample_set sample_index volume")
        reader.fields(timing_point, "B", "uninherited")
        reader.fields(timing_point, "I", "effects")
        timing.append(timing_point)
    stats: dict[str, Any] = {}
    reader.fields(
        stats, "I", "malformed_lines storyboard_lines fast_path_lines slow_path_lines"
    )
    points: list[tuple[int, int] | None] = [None] * point_count

    def point(index: int, value: tuple[int, int]) -> None:
        if index >= len(points) or points[index] is not None:
            raise ValueError("invalid or overlapping point range")
        points[index] = value

    orphan_count = reader.value("I")
    for _ in range(orphan_count):
        index = reader.value("I")
        point(index, struct.unpack("<ii", reader.take(8)))
    reader.done()
    reader = Reader(data[8:start])
    objects: list[dict[str, Any]] = []
    sliders: list[dict[str, Any]] = [{} for _ in range(slider_count)]
    for _ in range(object_count):
        hit_object: dict[str, Any] = {}
        reader.fields(hit_object, "i", "x y")
        reader.fields(hit_object, "I", "type hitsound")
        reader.fields(hit_object, "d", "time end_time")
        reader.fields(hit_object, "I", "slider")
        reader.fields(hit_object, "B", "new_combo combo_skip")
        sample_size = reader.value("I")
        if hit_object["slider"] != 0xFFFFFFFF:
            if hit_object["slider"] >= slider_count or sliders[hit_object["slider"]]:
                raise ValueError("invalid slider index")
            slider: dict[str, Any] = {}
            reader.fields(slider, "I", "point_begin point_count")
            for i in range(slider["point_count"]):
                point(slider["point_begin"] + i, struct.unpack("<ii", reader.take(8)))
            reader.fields(slider, "i", "slides")
            reader.fields(slider, "d", "length")
            slider["curve_type"] = bytes(reader.take(1))
            reader.fields(slider, "s", "edge_sounds edge_sets")
            sliders[hit_object["slider"]] = slider
        hit_object["hit_sample"] = bytes(reader.take(sample_size))
        objects.append(hit_object)
    reader.done()
    if any(not slider for slider in sliders) or any(p is None for p in points):
        raise ValueError("incomplete slider or point pool")
    return {
        "metadata": metadata,
        "stats": stats,
        "hit_objects": objects,
        "sliders": sliders,
        "slider_points": points,
        "timing_points": timing,
        "breaks": breaks,
        "combo_colours": colours,
    }


if __name__ == "__main__":
    result = decode(sys.stdin.buffer.read())
    print(result["metadata"]["title"].decode("utf-8", errors="replace"))
    print(
        f"{len(result['hit_objects'])} objects, {len(result['sliders'])} sliders, "
        f"{len(result['slider_points'])} points, {len(result['timing_points'])} timing points"
    )
