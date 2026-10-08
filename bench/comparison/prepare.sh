#!/usr/bin/env bash
# Linux/x86-64 benchmark dependencies only; nothing is added to FOSU's runtime.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:?Usage: bash bench/comparison/prepare.sh /absolute/build/directory}
mkdir -p "$build"
build=$(cd "$build" && pwd)
bench="$repo/bench/comparison"

python3 -m venv "$build/venv"
"$build/venv/bin/pip" install -r "$bench/requirements.txt"
FOSU_BUNDLE_RUNTIME=1 "$build/venv/bin/pip" install --no-deps "$repo"
cp "$bench/package.json" "$bench/package-lock.json" "$build/"
npm ci --prefix "$build"
CARGO_TARGET_DIR="$build/rust-target" RUSTFLAGS="-C target-cpu=x86-64-v3" \
    cargo build --release --locked --manifest-path "$bench/rust/Cargo.toml"
dotnet restore "$bench/dotnet/Comparison.csproj" --locked-mode
dotnet build "$bench/dotnet/Comparison.csproj" -c Release --no-restore -o "$build/dotnet"

mkdir -p "$build/include/nlohmann"
curl --fail --location --silent --show-error \
    https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp \
    -o "$build/include/nlohmann/json.hpp"
echo "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63  $build/include/nlohmann/json.hpp" | sha256sum --check
# Build the native worker like any CMake user, with the product's flags.
for isa in avx2 scalar; do
    cmake -S "$repo" -B "$build/cmake-$isa" -DCMAKE_BUILD_TYPE=Release \
        -DFOSU_ISA="$isa" -DFOSU_BUILD_PYTHON=OFF -DFOSU_JSON_INCLUDE_DIR="$build/include"
    cmake --build "$build/cmake-$isa" --target comparison_native_worker
    cp "$build/cmake-$isa/comparison_native_worker" "$build/native-$isa"
done
python3 "$bench/configure.py" "$build"
