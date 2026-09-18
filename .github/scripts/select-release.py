#!/usr/bin/env python3

import json
import os
import re
import subprocess
import tomllib
from pathlib import Path


ROOT = Path(__file__).parents[2]


def command(*args: str) -> str:
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def main() -> None:
    pyproject = tomllib.loads((ROOT / "pyproject.toml").read_text())
    version = pyproject["project"]["version"]
    cmake = re.search(
        r"project\(fosu VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES",
        (ROOT / "CMakeLists.txt").read_text(),
    )
    if not cmake or cmake.group(1) != version:
        raise SystemExit("pyproject.toml and CMakeLists.txt versions differ")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise SystemExit(f"unsupported release version: {version}")

    current = tuple(map(int, version.split(".")))
    tag = f"v{version}"
    tags = command("git", "tag", "--list", "v[0-9]*.[0-9]*.[0-9]*").splitlines()
    releases = [
        tuple(map(int, value[1:].split(".")))
        for value in tags
        if re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", value)
    ]
    latest = max(releases, default=(0, 0, 0))
    tag_exists = tag in tags
    if current < latest or (current == latest and not tag_exists):
        raise SystemExit(f"version {version} is not newer than the latest tag")

    release = subprocess.run(
        ["gh", "release", "view", tag, "--json", "isDraft"],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    release_state = json.loads(release.stdout) if release.returncode == 0 else None
    publish = (
        os.environ["GITHUB_EVENT_NAME"] == "push"
        and os.environ["GITHUB_REF"] == "refs/heads/master"
        and (not tag_exists or release_state is None or release_state["isDraft"])
    )
    if tag_exists and publish:
        tagged_commit = command("git", "rev-list", "-n", "1", tag)
        if tagged_commit != os.environ["GITHUB_SHA"]:
            raise SystemExit(f"unfinished {tag} release belongs to another commit")

    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
        print(f"publish={str(publish).lower()}", file=output)
        print(f"tag={tag}", file=output)


if __name__ == "__main__":
    main()
