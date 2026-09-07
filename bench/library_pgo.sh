#!/bin/sh
# Paired GCC PGO experiment. Train on sorted file indices 0,5,10,...;
# evaluate on every other file, retaining identical corpus order in both DSOs.
set -eu
corpus=${1:?usage: library_pgo.sh corpus}
compiler=${CXX:-g++}
flags=${FOSU_BENCH_FLAGS:--std=c++20 -O3 -march=x86-64-v3 -mtune=znver4 -fno-plt -fno-stack-protector}
output=${BUILD_DIR:-build/pgo}
mkdir -p "$output/data"
profile=$(cd "$output/data" && pwd)
find "$profile" -type f -name "*.gcda" -delete
# Intentional word splitting: flags is a list of compiler options.
$compiler $flags -Iinclude -fPIC -fvisibility=hidden -shared bench/library_module.cpp -o "$output/baseline.so"
$compiler -std=c++20 -O2 -Iinclude bench/library_compare.cpp -ldl -o "$output/compare"
$compiler $flags -Iinclude -fPIC -fvisibility=hidden -shared -fprofile-generate="$profile" bench/library_module.cpp -o "$output/profiled.so"
FOSU_BENCH_SPLIT=train "$output/compare" "$corpus" 3 "$output/profiled.so" > "$output/training.csv"
$compiler $flags -Iinclude -fPIC -fvisibility=hidden -shared -fprofile-use="$profile" -fprofile-correction -Werror=missing-profile bench/library_module.cpp -o "$output/profiled.so"
FOSU_BENCH_SPLIT=eval "$output/compare" "$corpus" 5 "$output/baseline.so" "$output/profiled.so" > "$output/heldout.csv"
python3 bench/summarize.py "$output/heldout.csv"
