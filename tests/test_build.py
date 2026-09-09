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
add_executable(headers main.cc)
target_link_libraries(headers PRIVATE fosu::headers)
target_compile_options(headers PRIVATE -fno-exceptions)
add_executable(runtime runtime.cc)
target_link_libraries(runtime PRIVATE fosu::fosu)
""")
    (root / "main.cc").write_text("""#include <fosu/parser.h>
int main() { fosu::Parser parser; auto result = parser.parse(nullptr, 0);
return !result || !result.value()->hit_objects.empty(); }
""")
    (root / "runtime.cc").write_text("""#include <fosu/parser.h>
#include <fosu/runtime.h>
#include <cstdio>
int main() {
    const auto* engine = fosu::runtime_engine(); if (!engine) return 1;
    fosu::Parser parser(*engine);
    const char data[] = "[HitObjects]\\n1,2,3,1,0\\n";
    auto result = parser.parse(data, sizeof(data)-1);
    if (!result || result.value()->hit_objects.size() != 1) return 2;
    puts(engine->kind == fosu::EngineKind::Scalar ? "scalar" :
         engine->kind == fosu::EngineKind::Avx2 ? "avx2" : "neon");
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
    consumer = str(root / "build" / "runtime")
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
print("Installed header-only and runtime-dispatched C++ CMake consumers passed")
