"""Check emitted Linux library/wheel protections, not just compiler flag spelling."""

import argparse
import re
import subprocess
import sys


def check(path):
    elf = subprocess.check_output(
        ["readelf", "-W", "-h", "-l", "-d", str(path)], text=True
    )
    assert re.search(r"Type:\s+DYN\b", elf), "library must be position independent"
    segments = [line.split() for line in elf.splitlines() if line.strip()]
    loads = [row for row in segments if row[0] == "LOAD"]
    assert loads, "missing loadable segments"
    for row in loads:
        flags = "".join(row[6:-1])
        assert not ("W" in flags and "E" in flags), "writable executable segment"
    stacks = [row for row in segments if row[0] == "GNU_STACK"]
    assert len(stacks) == 1 and "E" not in "".join(stacks[0][6:-1]), (
        "executable or unspecified stack"
    )
    assert any(row[0] == "GNU_RELRO" for row in segments), "missing RELRO"
    assert re.search(r"\(BIND_NOW\)|\(FLAGS\).*BIND_NOW|\(FLAGS_1\).*\bNOW\b", elf), (
        "missing eager binding"
    )
    assert not re.search(r"\(TEXTREL\)|\(FLAGS\).*TEXTREL", elf), "text relocations"
    symbols = subprocess.check_output(
        ["readelf", "-W", "--dyn-syms", str(path)], text=True
    )
    # Evidence of emitted canary checks; this does not prove coverage of every
    # function (especially when a private C++ runtime is bundled).
    assert "__stack_chk_fail" in symbols, "no stack-canary failure handler"
    print(f"ELF hardening passed: {path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("libraries", nargs="*")
    args = parser.parse_args()
    if sys.platform == "linux":
        if not args.libraries:
            from fosu import _core

            args.libraries = [_core.__file__]
        for library in args.libraries:
            check(library)
    else:
        print("ELF artifact checks apply to Linux only")
