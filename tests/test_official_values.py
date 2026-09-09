"""Audit all represented raw fields using official acceptance and numeric helpers.

This is a corpus tool, not a gameplay-model equivalence claim. The official
bridge retains accepted source fields before sorting, clamping, sample
resolution, and slider path conversion discard the raw representation.
Reports contain local paths and input-derived values; keep them private.
"""
import argparse
from collections import Counter
from dataclasses import fields
from enum import Enum
import json
import math
from pathlib import Path
import re
import shlex
import struct
import subprocess

import fosu

def value(obj):
    if isinstance(obj, Enum):
        return obj.value
    if isinstance(obj, list):
        return [value(item) for item in obj]
    if hasattr(obj, "__dataclass_fields__"):
        return {field.name: value(getattr(obj, field.name)) for field in fields(obj)}
    return obj


def differences(expected, actual, path=""):
    if isinstance(expected, dict):
        for key, item in expected.items():
            if key not in actual:
                yield path + "." + key, item, "<missing>"
            else:
                yield from differences(item, actual[key], path + "." + key)
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            yield path + ".length", len(expected), len(actual)
        else:
            for index, (left, right) in enumerate(zip(expected, actual)):
                yield from differences(left, right, f"{path}[{index}]")
    elif expected == "NaN" and isinstance(actual, float) and math.isnan(actual):
        return
    elif isinstance(expected, float) or isinstance(actual, float):
        if ".path" in path and math.isclose(float(expected), float(actual), rel_tol=1e-6, abs_tol=1e-3):
            return
        if struct.pack("d", float(expected)) != struct.pack("d", float(actual)):
            yield path, expected, actual
    elif expected != actual:
        yield path, expected, actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", default="dotnet build/official-reference/OfficialReference.dll")
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=0, help="Debug a prefix; zero audits the whole corpus")
    args = parser.parse_args()
    counts = Counter()
    by_mode = Counter()
    gaps = []
    with subprocess.Popen(shlex.split(args.reference), stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True) as reference:
        paths = sorted(args.corpus.glob("*.osu"))
        if args.limit:
            paths = paths[:args.limit]
        for path in paths:
            reference.stdin.write(json.dumps({"path": str(path.resolve()), "values": True}) + "\n")
            reference.stdin.flush()
            expected = json.loads(reference.stdout.readline(), parse_int=lambda s: -0.0 if s == "-0" else int(s))
            counts["files"] += 1
            if not expected["ok"] or expected["projection_errors"]:
                counts["reference_errors"] += 1
                gaps.append({"file": str(path), "reference_error": expected.get("error"),
                             "projection_errors": expected.get("projection_errors")})
                continue
            beatmap = fosu.parse_file(path, calculate_slider_end_times=True, calculate_slider_paths=True)
            by_mode[int(beatmap.mode)] += 1
            actual = value(beatmap)
            for obj, record in zip(beatmap.hit_objects, actual["hit_objects"]):
                if isinstance(obj, fosu.Slider) and obj.path is not None:
                    record["path_distance"] = obj.path.distance()
                    record["path_samples"] = [value(fosu.slider_position_at(obj.path, p)) for p in (0, 0.1, 0.5, 0.9, 1)]
                record["kind"] = ("circle" if obj.is_circle else "slider" if obj.is_slider
                                  else "spinner" if obj.is_spinner else "hold")
            raw = expected["values"]
            wanted = dict(raw["fields"], format_version=expected["format"], mode=expected["mode"],
                          hit_objects=raw["hit_objects"], timing_points=raw["timing_points"],
                          breaks=raw["breaks"], combo_colours=raw["combo_colours"])
            wanted["stats"] = {"malformed_lines": len(expected["rejected"]) + len(raw["policy_rejections"])}
            counts["official_rejected_lines"] += len(expected["rejected"])
            counts["intentional_policy_rejections"] += len(raw["policy_rejections"])
            counts["hit_objects"] += len(raw["hit_objects"])
            counts["timing_points"] += len(raw["timing_points"])
            problems = list(differences(wanted, actual))
            if problems:
                counts["files_with_differences"] += 1
                for field, _, _ in problems:
                    counts["difference:" + re.sub(r"\[\d+\]", "[]", field)] += 1
                gaps.append({"file": str(path), "differences": problems[:20], "difference_count": len(problems)})
            if counts["files"] % 1000 == 0:
                print(f"Checked {counts['files']} files; {len(gaps)} gaps", flush=True)
        reference.stdin.close()
        if reference.wait() != 0:
            raise RuntimeError("Official reference failed")
    if not counts["files"]:
        raise ValueError("Empty corpus")
    report = {"backend": fosu.backend, "counts": dict(counts), "modes": dict(by_mode), "gaps": gaps}
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps({"counts": dict(counts), "modes": dict(by_mode)}, sort_keys=True))
    if gaps:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
