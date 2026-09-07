"""Installed CMake targets work without access to the source checkout."""

import os
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
target_compile_options(headers PRIVATE -fno-exceptions)
add_executable(c_api main.c)
target_link_libraries(c_api PRIVATE fosu::fosu)
""")
    (root / "main.cpp").write_text("""#include <fosu/parser.hpp>
int main() { fosu::Parser parser; auto result = parser.parse(nullptr, 0);
return !result || !result.value()->hit_objects.empty(); }
""")
    (root / "main.c").write_text("""#include <fosu/c_api.h>
#include <stdio.h>
int main(void) {
    fosu_handle* h = fosu_new(); if (!h) return 1;
    const char data[] = "[HitObjects]\\n1,2,3,1,0\\n";
    if (fosu_parse(h, data, sizeof(data)-1, FOSU_ALL) != FOSU_OK) return 2;
    if (fosu_get_view(h)->hit_object_count != 1) return 3;
    puts(fosu_backend_name());
    fosu_free(h); return fosu_abi_version() != FOSU_ABI_VERSION;
}
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
    subprocess.run([str(root / "build" / "headers")], check=True)
    consumer = str(root / "build" / "c_api")
    env = dict(os.environ, FOSU_BACKEND="auto")
    selected = subprocess.check_output([consumer], env=env, text=True).strip()
    # Change only this temporary installation, never the source build.
    engines = list(prefix.rglob("libfosu_engine_*"))
    for engine in engines:
        engine.rename(engine.with_name(engine.name + ".unavailable"))
    assert subprocess.check_output([consumer], env=env, text=True).strip() == "scalar"
    if selected != "scalar":
        env["FOSU_BACKEND"] = selected
        assert subprocess.run([consumer], env=env).returncode == 1
print("Installed header-only and C ABI CMake consumers passed")
