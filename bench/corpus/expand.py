"""Preserve a baseline corpus and add the top available non-standard difficulties."""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil

def mode_of(data: bytes) -> int:
    if not data.removeprefix(b"\xef\xbb\xbf").startswith(b"osu file format"):
        raise ValueError("not an osu file")
    section = b""
    mode = 0
    for line in data.splitlines():
        if line.startswith(b"["):
            section = line
        elif section == b"[General]" and re.match(rb"Mode[ \t]*:", line):
            mode = int(line.split(b":", 1)[1].strip())
    if mode not in range(4):
        raise ValueError("invalid game mode")
    return mode


def main() -> None:
    import boto3
    from botocore.config import Config
    from botocore.exceptions import ClientError

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("candidates", type=Path, help="Headerless output of select_non_standard.sql")
    parser.add_argument("output", type=Path, help="New output directory; must not exist")
    parser.add_argument("--cache", type=Path, help="Reuse previously downloaded files")
    args = parser.parse_args()
    args.output.mkdir()
    files = args.output / "files"
    files.mkdir()
    manifest = {}
    for source in sorted(args.base.glob("*.osu")):
        data = source.read_bytes()
        manifest[int(source.stem)] = {"mode": mode_of(data), "source": "original-10k"}
        shutil.copyfile(source, files / source.name)
    if len(manifest) != 10000:
        raise ValueError("Expected the original 10,000 distinct files")

    s3 = boto3.client(
        "s3", endpoint_url=os.environ["AWS_S3_ENDPOINT_URL"],
        region_name=os.environ["AWS_S3_REGION_NAME"],
        aws_access_key_id=os.environ["AWS_S3_ACCESS_KEY_ID"],
        aws_secret_access_key=os.environ["AWS_S3_SECRET_ACCESS_KEY"],
        config=Config(max_pool_connections=8, retries={"max_attempts": 4}),
    )
    bucket = os.environ["AWS_S3_BUCKET_NAME"]
    candidates = {mode: [] for mode in (1, 2, 3)}
    with args.candidates.open() as source:
        for row in csv.reader(source, delimiter="\t"):
            beatmap_id, mode, playcount, ranked = map(int, row)
            candidates[mode].append((beatmap_id, mode, playcount, ranked))

    def fetch(row):
        beatmap_id, mode, _, _ = row
        existing = files / f"{beatmap_id}.osu"
        if not existing.exists() and args.cache:
            existing = args.cache / existing.name
        if existing.exists():
            data = existing.read_bytes()
        else:
            for key in (f"/beatmaps/{beatmap_id}.osu", f"beatmaps/{beatmap_id}.osu"):
                try:
                    response = s3.get_object(Bucket=bucket, Key=key)
                    with response["Body"] as body:
                        data = body.read()
                    break
                except ClientError as error:
                    if error.response["Error"]["Code"] not in ("NoSuchKey", "404"):
                        raise
            else:
                return row, None, "missing"
        try:
            actual_mode = mode_of(data)
        except ValueError:
            return row, None, "invalid_file"
        if actual_mode != mode:
            return row, None, "mode_mismatch"
        return row, data, None

    rejected = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        for mode, rows in candidates.items():
            kept = 0
            for offset in range(0, len(rows), 100):
                for rank, (row, data, error) in enumerate(pool.map(fetch, rows[offset:offset + 100]), offset + 1):
                    if kept == 2000:
                        break
                    beatmap_id, _, playcount, ranked = row
                    if error:
                        rejected.append({"beatmap_id": beatmap_id, "mode": mode, "rank": rank, "reason": error})
                        continue
                    if data is None:
                        raise AssertionError("Successful fetch must have bytes")
                    destination = files / f"{beatmap_id}.osu"
                    if not destination.exists():
                        destination.write_bytes(data)
                        manifest[beatmap_id] = {"mode": mode, "source": "non-standard-top-2000"}
                    manifest[beatmap_id].update(playcount=playcount, ranked=ranked, candidate_rank=rank)
                    kept += 1
                print(f"mode={mode} selected={kept} checked_through_rank={offset + 100}", flush=True)
                if kept == 2000:
                    break
            if not kept:
                raise RuntimeError(f"No available maps for mode {mode}")

    digest = hashlib.sha256()
    total_bytes = 0
    with (args.output / "manifest.csv").open("w", newline="") as destination:
        writer = csv.DictWriter(destination, fieldnames=["file", "mode", "bytes", "sha256", "source", "playcount", "ranked", "candidate_rank"])
        writer.writeheader()
        for path in sorted(files.glob("*.osu")):
            data = path.read_bytes()
            checksum = hashlib.sha256(data)
            digest.update(path.name.encode() + b"\0" + checksum.digest())
            total_bytes += len(data)
            writer.writerow(dict(file=path.name, bytes=len(data), sha256=checksum.hexdigest(), **manifest[int(path.stem)]))
    summary = {
        "files": len(manifest), "bytes": total_bytes, "sha256": digest.hexdigest(),
        "modes": dict(sorted(Counter(row["mode"] for row in manifest.values()).items())),
        "original_files_preserved": 10000, "added_files": len(manifest) - 10000,
        "rejected": rejected,
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({key: value for key, value in summary.items() if key != "rejected"}, indent=2))


if __name__ == "__main__":
    main()
