#!/bin/sh
# Paired GCC PGO experiment. Train on sorted file indices 0,5,10,...;
# evaluate on every other file, retaining identical corpus order in both DSOs.
set -eu
corpus=${1:?usage: library_pgo.sh corpus}
compiler=${CXX:-g++}
flags=${FOSU_BENCH_FLAGS:--std=c++20 -O3 -march=x86-64-v3 -mtune=znver4 -fno-plt -fstack-protector-strong -fstack-clash-protection -D_FORTIFY_SOURCE=3}
ldflags=-Wl,-z,relro,-z,now,-z,noexecstack
output=${BUILD_DIR:-build/pgo}
mkdir -p "$output/data"
profile=$(cd "$output/data" && pwd)
find "$profile" -type f -name "*.gcda" -delete
# Intentional word splitting: flags is a list of compiler options.
$compiler $flags $ldflags -Isrc -fPIC -fvisibility=hidden -shared bench/library_module.cc -o "$output/baseline.so"
$compiler -std=c++20 -O2 -fstack-protector-strong -fstack-clash-protection -D_FORTIFY_SOURCE=3 $ldflags -fPIE -pie -Isrc bench/library_compare.cc -ldl -o "$output/compare"
$compiler $flags $ldflags -Isrc -fPIC -fvisibility=hidden -shared -fprofile-generate="$profile" bench/library_module.cc -o "$output/profiled.so"
FOSU_BENCH_SPLIT=train "$output/compare" "$corpus" 3 "$output/profiled.so" > "$output/training.csv"
$compiler $flags $ldflags -Isrc -fPIC -fvisibility=hidden -shared -fprofile-use="$profile" -fprofile-correction -Werror=missing-profile bench/library_module.cc -o "$output/profiled.so"
FOSU_BENCH_SPLIT=eval "$output/compare" "$corpus" 5 "$output/baseline.so" "$output/profiled.so" > "$output/heldout.csv"
python3 bench/summarize.py "$output/heldout.csv"
