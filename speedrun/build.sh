#!/bin/sh
# Builds the freestanding one-shot parser for the target machine (Zen 4).
#   sh speedrun/build.sh [out=build/speedrun] [extra flags...]
set -e
out=${1:-build/speedrun}; [ $# -gt 0 ] && shift
mkdir -p "$(dirname "$out")"
${CXX:-g++} -std=c++20 -O2 -march=znver4 -mtune=znver4 \
  -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables \
  -fno-stack-protector -fno-pie -no-pie -fno-plt -fno-threadsafe-statics -ffixed-r15 \
  -nostdlib -nostartfiles -static -D_FORTIFY_SOURCE=0 -DNDEBUG -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
  -Wl,--gc-sections -Wl,-z,norelro -Wl,--build-id=none -Wl,-N -s \
  "$@" speedrun/speedrun.cpp -o "$out"
ls -l "$out" | awk '{print $5, $9}'
