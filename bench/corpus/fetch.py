#!/usr/bin/env python3
"""Download and verify an immutable FOSU benchmark corpus."""

from __future__ import annotations

import argparse
import csv
import hashlib
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path


VERSION = "performance-v1"
ARCHIVE_NAME = "fosu-performance-v1.tar.gz"
ARCHIVE_SHA256 = "6ee072ee92e1dcc6c0ef92b49cc0ab3734755bc7216d50b75a7a2b1e3730d260"
ARCHIVE_URL = (
    "https://github.com/cmyui/fast-osu-beatmap-parser/releases/download/"
    f"benchmark-corpus-v1/{ARCHIVE_NAME}"
)
MANIFEST = Path(__file__).with_name("performance-v1.csv")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_manifest() -> dict[str, tuple[int, str]]:
    with MANIFEST.open(newline="", encoding="utf-8") as stream:
        rows = csv.DictReader(stream)
        return {row["file"]: (int(row["bytes"]), row["sha256"]) for row in rows}


def verify(directory: Path, expected: dict[str, tuple[int, str]]) -> bool:
    files = (
        {path.name for path in directory.glob("*.osu")} if directory.is_dir() else set()
    )
    if files != expected.keys():
        return False
    for name, (size, digest) in expected.items():
        path = directory / name
        if path.stat().st_size != size or sha256(path) != digest:
            return False
    return True


def materialize(archive: Path, destination: Path) -> None:
    expected = read_manifest()
    if verify(destination, expected):
        print(f"verified {len(expected)} entries in {destination}")
        return
    if destination.exists():
        raise SystemExit(
            f"destination exists but does not match {VERSION}: {destination}"
        )
    if sha256(archive) != ARCHIVE_SHA256:
        raise SystemExit(f"archive checksum does not match {VERSION}: {archive}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(
        tempfile.mkdtemp(prefix=f".{destination.name}-", dir=destination.parent)
    )
    try:
        with tarfile.open(archive, "r:gz") as bundle:
            members = {
                Path(member.name).name: member
                for member in bundle.getmembers()
                if member.isfile()
                and Path(member.name).parent == Path(f"fosu-{VERSION}")
                and Path(member.name).suffix == ".osu"
            }
            if members.keys() != expected.keys():
                raise SystemExit("archive contents do not match the tracked manifest")
            for name, (size, digest) in expected.items():
                source = bundle.extractfile(members[name])
                if source is None:
                    raise SystemExit(f"archive member is unreadable: {name}")
                output = temporary / name
                with output.open("wb") as stream:
                    shutil.copyfileobj(source, stream)
                if output.stat().st_size != size or sha256(output) != digest:
                    raise SystemExit(f"archive member failed verification: {name}")
        temporary.rename(destination)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    print(f"materialized and verified {len(expected)} entries in {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "--archive",
        type=Path,
        help="use a local archive instead of downloading the release asset",
    )
    args = parser.parse_args()

    if args.archive:
        materialize(args.archive, args.destination)
        return

    with tempfile.TemporaryDirectory() as temporary:
        archive = Path(temporary) / ARCHIVE_NAME
        print(f"downloading {ARCHIVE_URL}")
        urllib.request.urlretrieve(ARCHIVE_URL, archive)
        materialize(archive, args.destination)


if __name__ == "__main__":
    main()
