"""Decode a FOSUDMP4/FOSUDMP5 stream into Python values, including the complete point pool.

    build/fosu_oneshot map.osu | python3 examples/decode_oneshot.py

This example consumes trusted parser output. Strings remain bytes; decode only
at the application boundary. For zero-copy array access use the C API instead.
"""
import struct
import sys


class Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.pos = 0

    def take(self, size):
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError('truncated stream')
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def value(self, code):
        return struct.unpack('<' + code, self.take(struct.calcsize('<' + code)))[0]

    def string(self):
        return bytes(self.take(self.value('I')))

    def fields(self, target, code, names):
        for name in names.split():
            target[name] = self.string() if code == 's' else self.value(code)

    def done(self):
        if self.pos != len(self.data):
            raise ValueError('unexpected trailing bytes')


def decode(data):
    if len(data) < 36 or data[:8] not in (b'FOSUDMP4', b'FOSUDMP5'):
        raise ValueError('not a FOSUDMP4/FOSUDMP5 stream')
    time_code = 'd' if data[:8] == b'FOSUDMP5' else 'i'
    trailer_size = struct.unpack('<Q', data[-8:])[0]
    start = len(data) - 8 - trailer_size
    if start < 8:
        raise ValueError('invalid trailer length')
    r = Reader(data[start:-8])
    if r.take(4) != b'TRLR':
        raise ValueError('missing trailer')
    object_count, slider_count, point_count = (r.value('I') for _ in range(3))
    if object_count > len(data) // 32 or point_count > len(data) // 8:
        raise ValueError('impossible counts')
    m = {}
    r.fields(m, 'i', 'format_version')
    r.fields(m, 's', 'audio_filename')
    r.fields(m, 'i', 'audio_lead_in preview_time countdown')
    r.fields(m, 's', 'sample_set')
    r.fields(m, 'd', 'stack_leniency')
    r.fields(m, 'i', 'mode')
    r.fields(m, 'B', 'letterbox_in_breaks widescreen_storyboard epilepsy_warning special_style use_skin_sprites samples_match_playback_rate')
    r.fields(m, 'i', 'countdown_offset')
    r.fields(m, 's', 'overlay_position skin_preference bookmarks')
    r.fields(m, 'd', 'distance_spacing')
    r.fields(m, 'i', 'beat_divisor grid_size')
    r.fields(m, 'd', 'timeline_zoom')
    r.fields(m, 's', 'title title_unicode artist artist_unicode creator version source tags')
    r.fields(m, 'q', 'beatmap_id beatmap_set_id')
    r.fields(m, 'd', 'hp cs od ar slider_multiplier slider_tick_rate')
    r.fields(m, 's', 'background video')
    breaks = [(r.value(time_code), r.value(time_code)) for _ in range(r.value('I'))]
    colours = [r.value('I') for _ in range(r.value('I'))]
    timing = []
    for _ in range(r.value('I')):
        t = {}
        r.fields(t, 'd', 'time beat_length')
        r.fields(t, 'i', 'meter sample_set sample_index volume')
        r.fields(t, 'B', 'uninherited')
        r.fields(t, 'I', 'effects')
        timing.append(t)
    stats = {}
    r.fields(stats, 'I', 'malformed_lines storyboard_lines fast_path_lines slow_path_lines')
    points = [None] * point_count

    def point(index, value):
        if index >= len(points) or points[index] is not None:
            raise ValueError('invalid or overlapping point range')
        points[index] = value

    orphan_count = r.value('I')
    for _ in range(orphan_count):
        index = r.value('I')
        point(index, struct.unpack('<ii', r.take(8)))
    r.done()
    r = Reader(data[8:start])
    objects, sliders = [], []
    for _ in range(object_count):
        h = {}
        r.fields(h, 'i', 'x y')
        r.fields(h, 'I', 'type hitsound')
        r.fields(h, time_code, 'time end_time')
        r.fields(h, 'I', 'slider')
        sample_size = r.value('I')
        if h['slider'] != 0xFFFFFFFF:
            if h['slider'] != len(sliders):
                raise ValueError('invalid slider index')
            s = {}
            r.fields(s, 'I', 'point_begin point_count')
            for i in range(s['point_count']):
                point(s['point_begin'] + i, struct.unpack('<ii', r.take(8)))
            r.fields(s, 'i', 'slides')
            r.fields(s, 'd', 'length')
            s['curve_type'] = bytes(r.take(1))
            r.fields(s, 's', 'edge_sounds edge_sets')
            sliders.append(s)
        h['hit_sample'] = bytes(r.take(sample_size))
        objects.append(h)
    r.done()
    if len(sliders) != slider_count or any(p is None for p in points):
        raise ValueError('incomplete slider or point pool')
    return dict(metadata=m, stats=stats, hit_objects=objects, sliders=sliders,
                slider_points=points, timing_points=timing, breaks=breaks,
                combo_colours=colours)


if __name__ == '__main__':
    result = decode(sys.stdin.buffer.read())
    print(result['metadata']['title'].decode('utf-8', errors='replace'))
    print(f"{len(result['hit_objects'])} objects, {len(result['sliders'])} sliders, "
          f"{len(result['slider_points'])} points, {len(result['timing_points'])} timing points")
