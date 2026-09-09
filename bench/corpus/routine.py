"""Freeze and materialize the routine performance and compatibility profiles."""

import argparse
from bisect import bisect_right
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import shutil

PERFORMANCE_SLOTS = 256  # Per mode, not distinct files: popular maps can repeat.
COMPATIBILITY_MAPS = 32  # Distinct files per mode.


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def performance_sample(maps: list[dict], seed: str) -> list[str]:
    """Equal-probability draws grouped by file size and timing workload."""
    selected = []
    for mode in range(4):
        ordered = sorted((m for m in maps if m["mode"] == mode),
                         key=lambda m: (m["bytes"].bit_length(),
                                        m["complexity_band"],
                                        (m["timing_rows"] + 1).bit_length(),
                                        m["bytes"], m["file"]))
        cumulative = []
        total = 0
        for m in ordered:
            total += m["playcount"]
            cumulative.append(total)
        if not total:
            raise ValueError(f"No positive playcounts for mode {mode}")
        for slot in range(PERFORMANCE_SLOTS):
            jitter = int.from_bytes(hashlib.sha256(f"{seed}:{mode}:{slot}".encode()).digest()[:8], "big")
            position = ((slot * 2**64 + jitter) * total) // (PERFORMANCE_SLOTS * 2**64)
            selected.append(ordered[bisect_right(cumulative, position)]["file"])
    return selected


def freeze(manifest: Path, playcounts: Path, corpus: Path) -> dict:
    with playcounts.open() as source:
        plays = {}
        for file_id, mode, count in csv.reader(source, delimiter="\t"):
            name = f"{int(file_id)}.osu"
            if name in plays:
                raise ValueError(f"Duplicate playcount row: {name}")
            plays[name] = (int(mode), int(count))
    maps, excluded = [], []
    with manifest.open() as source:
        for row in csv.DictReader(source):
            name, mode = row["file"], int(row["mode"])
            if name not in plays or plays[name][0] != mode or plays[name][1] <= 0:
                excluded.append(name)
                continue
            data = (corpus / name).read_bytes()
            if len(data) != int(row["bytes"]) or hashlib.sha256(data).hexdigest() != row["sha256"]:
                raise ValueError(f"Corpus content differs: {name}")
            section = b""
            timing_rows = object_rows = complex_rows = 0
            for line in data.splitlines():
                line = line.strip()
                if line.startswith(b"["):
                    section = line
                elif section == b"[TimingPoints]" and line and not line.startswith(b"//"):
                    timing_rows += 1
                elif section == b"[HitObjects]" and line and not line.startswith(b"//"):
                    object_rows += 1
                    fields = line.split(b",", 4)
                    try:
                        kind = int(fields[3])
                    except (ValueError, IndexError):
                        continue
                    # Sliders and holds cost different work from circles. This
                    # describes source shape, not accepted/valid parser output.
                    complex_rows += not (kind & 1) and bool(kind & (2 | 128))
            maps.append(dict(file=name, mode=mode, bytes=int(row["bytes"]),
                             sha256=row["sha256"], playcount=plays[name][1], timing_rows=timing_rows,
                             complexity_band=4 * complex_rows // max(1, object_rows)))
    profiles = {"performance": performance_sample(maps, "fosu-routine-v1"),
                "confirmation": performance_sample(maps, "fosu-confirmation-v1")}
    smoke = []
    for mode in range(4):
        ordered = sorted((m for m in maps if m["mode"] == mode),
                         key=lambda m: (m["bytes"], m["file"]))
        if len(ordered) < COMPATIBILITY_MAPS:
            raise ValueError(f"Not enough distinct maps for mode {mode}")
        smoke.extend(ordered[(2 * i + 1) * len(ordered) // (2 * COMPATIBILITY_MAPS)]["file"]
                     for i in range(COMPATIBILITY_MAPS))
    profiles["compatibility"] = smoke
    selected = set().union(*profiles.values())
    return {
        "schema": 1,
        "ordering": "power-of-two byte-size band, quarter-width slider/hold share band, power-of-two timing-row band, bytes, filename",
        "population": "Popular ranked/approved maps in the mixed-mode corpus; Akatsuki lifetime plays",
        "manifest_sha256": digest(manifest), "playcounts_sha256": digest(playcounts),
        "eligible_maps": dict(sorted(Counter(m["mode"] for m in maps).items())),
        "excluded_without_matching_positive_playcount": sorted(excluded),
        "maps": {m["file"]: {k: m[k] for k in ("mode", "bytes", "sha256")}
                 for m in sorted(maps, key=lambda m: m["file"]) if m["file"] in selected},
        "profiles": profiles,
    }


def materialize(selection: dict, corpus: Path, output: Path) -> None:
    if selection["schema"] != 1:
        raise ValueError("Unknown selection schema")
    # Verify once before creating anything. A stale/missing map is not a license
    # to silently change the benchmark population.
    for name, m in selection["maps"].items():
        if name != f"{int(Path(name).stem)}.osu":
            raise ValueError("Expected a numeric beatmap filename")
        path = corpus / name
        if path.stat().st_size != m["bytes"] or digest(path) != m["sha256"]:
            raise ValueError(f"Corpus content differs: {name}")
    output.mkdir()
    for profile, names in selection["profiles"].items():
        if profile not in ("performance", "confirmation", "compatibility"):
            raise ValueError("Unknown profile")
        directory = output / profile
        directory.mkdir()
        with (output / f"{profile}.csv").open("w", newline="") as destination:
            writer = csv.DictWriter(destination, fieldnames=["file", "source_file", "mode", "bytes", "sha256"])
            writer.writeheader()
            for slot, name in enumerate(names):
                m = selection["maps"][name]
                filename = f"{slot:04d}-{name}"
                shutil.copyfile(corpus / name, directory / filename)
                writer.writerow(dict(file=filename, source_file=name, **m))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest="command", required=True)
    create = commands.add_parser("freeze", help="Refresh deliberately, never for an individual experiment")
    create.add_argument("manifest", type=Path)
    create.add_argument("playcounts", type=Path, help="Headerless TSV: beatmap_id, native mode, lifetime playcount")
    create.add_argument("corpus", type=Path)
    create.add_argument("output", type=Path)
    build = commands.add_parser("materialize")
    build.add_argument("selection", type=Path)
    build.add_argument("corpus", type=Path)
    build.add_argument("output", type=Path)
    args = p.parse_args()
    if args.command == "freeze":
        data = freeze(args.manifest, args.playcounts, args.corpus)
        with args.output.open("x") as output:
            # Keep the frozen registry readable without expanding every map
            # into six lines of generated data.
            maps = data.pop("maps")
            profiles = data.pop("profiles")
            output.write(json.dumps(data, indent=2)[:-2] + ',\n  "maps": {\n')
            output.write(",\n".join(f"    {json.dumps(name)}: {json.dumps(m)}" for name, m in maps.items()))
            output.write('\n  },\n  "profiles": ' + json.dumps(profiles, indent=2) + "\n}\n")
    else:
        materialize(json.loads(args.selection.read_text()), args.corpus, args.output)


if __name__ == "__main__":
    main()
