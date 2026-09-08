"""Compare acceptance with osu!'s unmodified legacy decoder, not a third-party parser.

Build the reference with sh tests/reference/official/build.sh, then run using
an interpreter with fosu installed. --corpus audits local files without copying
or printing their contents; --report may contain local paths and stays private.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

import fosu

BASE = 'osu file format v14\n[Difficulty]\nOverallDifficulty:7\n'
VALUES = (
    '0', '-1', '+1', '1.25', '1e1', '1e309', '1e10', '1e-400',
    '2147483647', '2147483648', '-2147483648', '9223372036854775808',
    'NaN', 'Infinity', '  7  ', '\t7\t', '7junk', '+-1', '',
)


def fixtures():
    for value in ('', '1,2,3', '10,bad,20', '2147483647,2147483648,-2147483648,-2147483649',
                  '1_000,1e3,+3,--4,', ' 1 , \t-2\t ,+3', '٠١,１２,1',
                  '\u00a01\u00a0', '\u00a01,2,3\u00a0', '1,\u00a02\u00a0,3',
                  '1\0,2\0\0,3\0 ', '\v1\f,2', '1,00000000000000000000000000000002'):
        yield f'Editor.Bookmarks={value!r}', BASE + '[Editor]\nBookmarks:' + value + '\n'
    for section, keys in (
        ('Difficulty', 'HPDrainRate CircleSize OverallDifficulty ApproachRate SliderMultiplier SliderTickRate'),
        ('General', 'StackLeniency AudioLeadIn PreviewTime CountdownOffset Mode LetterboxInBreaks WidescreenStoryboard EpilepsyWarning SpecialStyle SamplesMatchPlaybackRate'),
        ('Metadata', 'BeatmapID BeatmapSetID'),
        ('Editor', 'DistanceSpacing BeatDivisor GridSize TimelineZoom'),
    ):
        for key in keys.split():
            for value in VALUES:
                yield f'{section}.{key}={value!r}', BASE + f'[{section}]\n{key}:{value}\n'
    for key in ('Countdown', 'SampleSet'):
        for value in VALUES + ('None', 'Normal', 'HalfSpeed', 'DoubleSpeed', 'Soft', 'Drum', ' Normal ', 'Normal, None', 'Normal,', 'normal'):
            yield f'General.{key}={value!r}', BASE + '[General]\n' + key + ':' + value + '\n'
    for value in VALUES + ('131072', '131073', '-131073', '131072.001', '2147483776'):
        for index, name in enumerate(('x', 'y', 'time', 'type', 'hitsound')):
            fields = ['256', '192', '1000', '1', '0']
            fields[index] = value
            yield f'object.{name}={value!r}', BASE + '[HitObjects]\n' + ','.join(fields) + '\n'
        yield f'slider.length={value!r}', BASE + '[HitObjects]\n256,192,1000,2,0,L|300:192,1,' + value + '\n'
        yield f'slider.point={value!r}', BASE + '[HitObjects]\n256,192,1000,2,0,L|' + value + ':192,1,100\n'
    for value in VALUES + ('+NaN', '-NaN', 'nan', '0junk', '1junk'):
        for index, name in enumerate(('time', 'beat_length', 'meter', 'sample_set', 'sample_index', 'volume', 'uninherited', 'effects')):
            fields = ['1', '-100', '4', '2', '1', '100', '0', '0']
            fields[index] = value
            yield f'timing.{name}={value!r}', BASE + '[TimingPoints]\n0,500\n' + ','.join(fields) + '\n'
    for value in ('-1', '0', '1', '9000', '9001', '2147483647', '-2147483648', ' 1 ', '1.0'):
        yield f'slider.repeats={value!r}', BASE + '[HitObjects]\n256,192,1000,2,0,L|300:192,' + value + ',100\n'
    for value in VALUES + ('0:0', '0:0:0:0:', '0:0:bad:0:', '0:0:0:0:a.wav', '0:0:', '0:0:0:0:a,b', '9:8:7:6:', '/:0:0:0:', ':0:0:0:0', '0:0:0:x:', '0:0|9:9', '0:0|/:0'):
        for head in ('256,192,1000,1,0,', '256,192,1000,8,0,1500,', '256,192,1000,128,0,1500:'):
            yield f'sample={head + value!r}', BASE + '[HitObjects]\n' + head + value + '\n'
        yield f'slider.sample={value!r}', BASE + '[HitObjects]\n256,192,1000,2,0,L|300:192,1,100,,,' + value + '\n'
        yield f'slider.edge_set={value!r}', BASE + '[HitObjects]\n256,192,1000,2,0,L|300:192,1,100,,' + value + '\n'
    # Exercise every printable byte in each digit lane of the common sample
    # shape against the real decoder, independently of the SWAR implementation.
    for index in (0, 2, 4, 6):
        for byte in range(32, 127):
            sample = list('0:0:0:0:')
            sample[index] = chr(byte)
            value = ''.join(sample)
            yield f'sample.lane{index}={value!r}', BASE + '[HitObjects]\n256,192,1000,1,0,' + value + '\n'
    for section in ('TimingPoints', 'HitObjects'):
        for value in (' ', '\t', '  // comment', '\t// comment'):
            yield f'blank.{section}={value!r}', BASE + '[' + section + ']\n' + value + '\n'
    for line in (
        '256,192,1000,2,0,L|300:192,1',
        '256,192,1000,8,0,1e309',
        '256,192,1000,8,0,2147483647',
        '256,192,1000,128,0,1500:0:0:0:0:',
        '256,192,1000,128,0',
        '256,192,1000,128,0,',
        '256,192,1000,8,0,1500:0:0',
    ):
        yield f'object={line}', BASE + '[HitObjects]\n' + line + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', default='dotnet build/official-reference/OfficialReference.dll')
    parser.add_argument('--corpus', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    counts = Counter()
    gaps = []
    with subprocess.Popen(shlex.split(args.reference), stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True) as reference:
        def check(name, path, data=None):
            reference.stdin.write(json.dumps({'path': str(path.resolve())}) + '\n')
            reference.stdin.flush()
            line = reference.stdout.readline()
            if not line:
                raise RuntimeError('Official decoder exited without a response')
            expected = json.loads(line)
            actual = fosu.parse_file(path) if data is None else fosu.parse(data.encode())
            ours = (True, actual.stats.malformed_lines, len(actual.hit_objects))
            theirs = (expected['ok'], len(expected.get('rejected', [])), expected.get('objects', 0))
            counts['files'] += 1
            counts['official_rejected_lines'] += theirs[1]
            counts['fosu_rejected_lines'] += ours[1]
            if ours != theirs or actual.bookmarks != expected.get('bookmarks', []):
                gaps.append({'case': name, 'official': theirs, 'fosu': ours,
                             'official_rejections': expected.get('rejected', []),
                             'official_bookmarks': expected.get('bookmarks', []),
                             'fosu_bookmarks': actual.bookmarks})
            if counts['files'] % 1000 == 0:
                print(f"Checked {counts['files']} files; {len(gaps)} differences", flush=True)
        if args.corpus:
            for path in sorted(args.corpus.rglob('*.osu')):
                check(str(path), path)
        else:
            with tempfile.TemporaryDirectory(prefix='fosu-official-') as directory:
                path = Path(directory) / 'case.osu'
                for name, data in fixtures():
                    path.write_text(data)
                    check(name, path, data)
        reference.stdin.close()
        if reference.wait() != 0:
            raise RuntimeError('Official decoder failed')
    if not counts['files']:
        raise RuntimeError('No input files were checked')
    print(json.dumps(dict(counts, differences=len(gaps)), sort_keys=True))
    if args.report:
        args.report.write_text(json.dumps({'counts': dict(counts), 'gaps': gaps}, indent=2) + '\n')
    elif not args.corpus:
        for gap in gaps:
            print(json.dumps(gap))
    if gaps:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
