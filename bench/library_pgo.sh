#!/bin/sh
# Paired GCC PGO experiment. Train on sorted file indices 0,5,10,...;
# evaluate on every other file, retaining identical corpus order in both DSOs.
set -eu
corpus=${1:?usage: library_pgo.sh corpus}
compiler=${CXX:-g++}
flags=${FOSU_BENCH_FLAGS:--std=c++20 -O3 -march=x86-64-v3 -mtune=znver4 -fno-plt -fno-stack-protector}
mkdir -p build/library-pgo-data
profile=$(pwd)/build/library-pgo-data
find "$profile" -type f -name "*.gcda" -delete
# Intentional word splitting: flags is a list of compiler options.
$compiler $flags -Iinclude -fPIC -fvisibility=hidden -shared bench/library_module.cpp -o build/library_baseline.so
$compiler -std=c++20 -O2 -Iinclude bench/library_compare.cpp -ldl -o build/library_compare
$compiler $flags -Iinclude -fPIC -fvisibility=hidden -shared -fprofile-generate="$profile" bench/library_module.cpp -o build/library_profiled.so
FOSU_BENCH_SPLIT=train build/library_compare "$corpus" 3 build/library_profiled.so > build/library-training.csv
$compiler $flags -Iinclude -fPIC -fvisibility=hidden -shared -fprofile-use="$profile" -fprofile-correction -Werror=missing-profile bench/library_module.cpp -o build/library_profiled.so
FOSU_BENCH_SPLIT=eval build/library_compare "$corpus" 5 build/library_baseline.so build/library_profiled.so > build/library-heldout.csv
python3 bench/library_summary.py build/library-heldout.csv
