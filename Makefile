CXX ?= clang++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -Iinclude

UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# x86 build: native on x86_64 hosts, cross-compiled + run under Rosetta 2
# on Apple Silicon (requires macOS 15+ for AVX2 translation).
X86_FLAGS = -march=x86-64-v3
ifeq ($(UNAME_S),Linux)
# Zen 4 scheduling plus no PLT indirection / stack-protector hardening:
# +4-6% combined on the benchmark box. The ISA stays x86-64-v3 — mtune
# only reorders instructions, the binary runs on any v3 machine.
X86_FLAGS += -mtune=znver4 -fno-plt -fno-stack-protector
endif
ifeq ($(UNAME_S),Darwin)
ifeq ($(UNAME_M),arm64)
X86_FLAGS += -target x86_64-apple-macos12
X86_RUN = arch -x86_64
endif
endif

HEADERS = $(wildcard include/fosu/*.hpp)

all: test bench

build:
	mkdir -p build

# --- native (on arm64 this exercises the scalar path only) ---
build/test_native: tests/test_parser.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/bench_native: bench/bench.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $< -o $@

# --- x86-64-v3 (AVX2 + BMI fast path) ---
build/test_x86: tests/test_parser.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) $< -o $@

build/bench_x86: bench/bench.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) $< -o $@

build/coldstart_x86: bench/coldstart.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) $< -o $@

test: build/test_native build/test_x86
	./build/test_native
	$(X86_RUN) ./build/test_x86

bench: build/bench_native build/bench_x86
	$(X86_RUN) ./build/bench_x86 $(BENCH_ARGS)

bench-native: build/bench_native
	./build/bench_native $(BENCH_ARGS)

# Cold-start single-beatmap benchmark: one fresh process per file, first
# parse timed. COLD_CPU pins each process on Linux; COLD_REPS = best-of.
COLD_REPS ?= 5
coldstart: build/coldstart_x86
	sh bench/coldstart.sh "$(X86_RUN) ./build/coldstart_x86" $(BENCH_ARGS) $(COLD_REPS) $(COLD_CPU)

# Profile-guided build (gcc/Linux): train on the benchmark corpus, then
# rebuild with measured branch probabilities. Worth +2-3% on real maps.
bench-pgo: | build
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) -fprofile-generate bench/bench.cpp -o build/bench_pgo
	$(X86_RUN) ./build/bench_pgo $(BENCH_ARGS) > /dev/null
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) -fprofile-use -fprofile-correction bench/bench.cpp -o build/bench_pgo
	$(X86_RUN) ./build/bench_pgo $(BENCH_ARGS)

# Linux x86-64 / Zen 4 standalone process. These flags and the compact
# representation belong to this executable, not to library consumers.
ONESHOT_CXX ?= g++
ONESHOT_FLAGS = -std=c++20 -O3 -march=znver4 -DFOSU_ONESHOT_COMPACT \
	-fno-exceptions -fno-rtti -fno-stack-protector -fno-pie \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -flto -Iinclude
ONESHOT_LINK = -O3 -flto -nostdlib -static -no-pie \
	-Wl,--gc-sections,-z,noseparate-code,--build-id=none
ONESHOT_PROFILE = $(abspath build/oneshot/profile)

build/oneshot:
	mkdir -p $@

build/oneshot/runtime.o: oneshot/runtime.cpp oneshot/third_party/fast_float.h | build/oneshot
	$(ONESHOT_CXX) $(ONESHOT_FLAGS) -fno-builtin -c $< -o $@

build/oneshot/main.o: oneshot/main.cpp oneshot/serialize.hpp $(HEADERS) | build/oneshot
	$(ONESHOT_CXX) $(ONESHOT_FLAGS) -c $< -o $@

build/oneshot/start.o: oneshot/start.S | build/oneshot
	$(ONESHOT_CXX) -c $< -o $@

build/fosu_oneshot: build/oneshot/main.o build/oneshot/runtime.o build/oneshot/start.o
	$(ONESHOT_CXX) $(ONESHOT_LINK) $^ -o $@

build/oneshot_reference: bench/oneshot_reference.cpp oneshot/serialize.hpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) $< -o $@

build/oneshot_process: bench/oneshot_process.c | build
	$(CC) -O2 -Wall -Wextra $< -o $@

oneshot: build/fosu_oneshot build/oneshot_reference build/oneshot_process

# Train the parser in fresh hosted processes; then use those branch
# profiles in the executable with its minimal runtime. No input bytes or
# parsed results are embedded in the profile or retained between runs.
oneshot-pgo: build/oneshot/runtime.o build/oneshot/start.o
	test -d "$(CORPUS)"
	mkdir -p build/oneshot/pgo
	rm -rf "$(ONESHOT_PROFILE)"
	$(ONESHOT_CXX) $(ONESHOT_FLAGS) -fprofile-generate="$(ONESHOT_PROFILE)" -c oneshot/main.cpp -o build/oneshot/pgo/main.o
	$(ONESHOT_CXX) -O3 -flto -static -no-pie -fprofile-generate="$(ONESHOT_PROFILE)" build/oneshot/pgo/main.o -o build/fosu_oneshot_train
	python3 bench/oneshot_train.py build/fosu_oneshot_train "$(CORPUS)"
	$(ONESHOT_CXX) $(ONESHOT_FLAGS) -fprofile-use="$(ONESHOT_PROFILE)" -fprofile-correction -c oneshot/main.cpp -o build/oneshot/pgo/main.o
	$(ONESHOT_CXX) $(ONESHOT_LINK) build/oneshot/pgo/main.o build/oneshot/runtime.o build/oneshot/start.o -o build/fosu_oneshot_pgo

clean:
	rm -rf build

.PHONY: all test bench bench-native bench-pgo coldstart oneshot oneshot-pgo clean
