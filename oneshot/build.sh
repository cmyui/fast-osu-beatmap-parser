#!/bin/sh
# Builds the freestanding one-shot parser for the target machine (Zen 4).
#   sh oneshot/build.sh [out=build/fosu_oneshot] [extra flags...]
set -e
out=${1:-build/fosu_oneshot}; [ $# -gt 0 ] && shift
mkdir -p "$(dirname "$out")"
${CXX:-g++} -std=c++20 -Iinclude -O2 -march=znver4 -mtune=znver4 \
  -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables \
  -fno-stack-protector -fno-pie -no-pie -fno-plt -fno-threadsafe-statics \
  -nostdlib -nostartfiles -static -D_FORTIFY_SOURCE=0 -DNDEBUG -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
  -Wl,--gc-sections -Wl,-z,norelro -Wl,--build-id=none -Wl,-N -s \
  "$@" oneshot/main.cpp -o "$out"
ls -l "$out" | awk '{print $5, $9}'
