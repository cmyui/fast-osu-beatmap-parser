"""Write a machine-local worker configuration after prepare.sh."""

import json
from pathlib import Path
import subprocess
import sys

bench = Path(__file__).resolve().parent
build = Path(sys.argv[1]).resolve()
source = subprocess.check_output(
    ["git", "-C", str(bench), "rev-parse", "HEAD"], text=True
).strip()
variants = []


def add(name, version, command, workloads=("bytes",), env=None):
    variants.append(
        {
            "name": name,
            "version": version,
            "command": [str(x) for x in command],
            "workloads": list(workloads),
            "env": env or {},
        }
    )


python = [build / "venv/bin/python", bench / "python_worker.py"]
for backend in ("avx2", "scalar"):
    add(
        f"fosu-python-{backend}",
        source,
        [*python, "fosu"],
        ("bytes", "file", "visit"),
        {"FOSU_BACKEND": backend},
    )
add("slider", "0.8.4", [*python, "slider"], ("bytes", "file", "visit"))
variants[-1]["modes"] = [0, 1, 2]  # Measured mania corpus support is incomplete.
add("osupyparser", "1.0.7", [*python, "osupyparser"], ("file",))
add(
    "rosu-map",
    "0.2.1",
    [build / "rust-target/release/fosu-parser-comparison", "rosu-map"],
)
for name, version in (
    ("osu-lazer", "2026.730.0"),
    ("osuparsers", "1.7.2"),
    ("coosu", "2.5.1"),
):
    add(
        name,
        version,
        ["dotnet", build / "dotnet/Comparison.dll", name],
        env={"DOTNET_NOLOGO": "1"},
    )
for name, version in (("osu-parser", "0.3.3"), ("osu-parsers", "4.1.7")):
    add(
        name,
        version,
        ["node", bench / "node_worker.cjs", name],
        env={"NODE_PATH": str(build / "node_modules")},
    )
for backend in ("avx2", "scalar"):
    add(f"fosu-cpp-{backend}", source, [build / f"native-{backend}"])
(build / "variants.json").write_text(
    json.dumps(
        {
            "reference": "fosu-python-avx2",
            "variants": variants,
        },
        indent=2,
    )
    + "\n"
)
