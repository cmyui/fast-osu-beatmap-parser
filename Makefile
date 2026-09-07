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

clean:
	rm -rf build

.PHONY: all test bench bench-native bench-pgo coldstart clean
