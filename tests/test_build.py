"""Installed CMake targets work without access to the source checkout."""

import subprocess
import sys
import tempfile
from pathlib import Path

build = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="fosu-consumer-") as temp:
    root = Path(temp)
    prefix = root / "install"
    subprocess.run(
        ["cmake", "--install", str(build), "--prefix", str(prefix)], check=True
    )
    (root / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.26)
project(consumer LANGUAGES C CXX)
find_package(fosu CONFIG REQUIRED)
add_executable(headers main.cpp)
target_link_libraries(headers PRIVATE fosu::headers)
add_executable(c_api main.c)
target_link_libraries(c_api PRIVATE fosu::fosu)
""")
    (root / "main.cpp").write_text("""#include <fosu/parser.hpp>
int main() { fosu::Parser parser; return parser.parse(nullptr, 0).hit_objects.size(); }
""")
    (root / "main.c").write_text("""#include <fosu/c_api.h>
int main(void) { fosu_handle* h = fosu_new(); if (!h) return 1;
fosu_free(h); return fosu_abi_version() != FOSU_ABI_VERSION; }
""")
    subprocess.run(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(root / "build"),
            f"-DCMAKE_PREFIX_PATH={prefix}",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(root / "build")], check=True)
    for name in ("headers", "c_api"):
        subprocess.run([str(root / "build" / name)], check=True)
print("Installed header-only and C ABI CMake consumers passed")
