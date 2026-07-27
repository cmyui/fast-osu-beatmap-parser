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

test: build/test_native build/test_x86
	./build/test_native
	$(X86_RUN) ./build/test_x86

bench: build/bench_native build/bench_x86
	$(X86_RUN) ./build/bench_x86 $(BENCH_ARGS)

bench-native: build/bench_native
	./build/bench_native $(BENCH_ARGS)

# Profile-guided build (gcc/Linux): train on the benchmark corpus, then
# rebuild with measured branch probabilities. Worth +2-3% on real maps.
bench-pgo: | build
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) -fprofile-generate bench/bench.cpp -o build/bench_pgo
	$(X86_RUN) ./build/bench_pgo $(BENCH_ARGS) > /dev/null
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) -fprofile-use -fprofile-correction bench/bench.cpp -o build/bench_pgo
	$(X86_RUN) ./build/bench_pgo $(BENCH_ARGS)

# --- RTL simulation (Verilator) ---
# Phase 0 of the FPGA port: the byte classifier, verified against the C++
# parser as golden model on real corpus bytes. Needs `brew install verilator`
# (or apt); deliberately not part of `all`, since the C++ build must not
# depend on an RTL toolchain.
VERILATOR ?= verilator
RTL_CORPUS ?= bench/corpus-large
RTL_SRCS = rtl/fosu_classify.sv rtl/fosu_classify_stage.sv
RTL_CFLAGS = -std=c++20 -O2 -I$(CURDIR)/include
# On a native x86 host, build the testbench with AVX2 so FOSU_SIMD_X86 is 1
# and the third leg of the equivalence (RTL vs the shipping AVX2 intrinsics)
# actually runs. On arm64 the scalar golden model stands alone -- the C++
# suite already pins scalar == AVX2 there.
ifeq ($(UNAME_M),x86_64)
RTL_CFLAGS += -march=x86-64-v3
endif

rtl-test: | build
	$(VERILATOR) --cc --exe --build -j 0 -Wall \
	  --top-module fosu_classify_stage -Mdir build/vsim -o Vtb_classify \
	  -CFLAGS "$(RTL_CFLAGS)" \
	  $(RTL_SRCS) $(CURDIR)/sim/tb_classify.cpp
	./build/vsim/Vtb_classify $(RTL_CORPUS)

# Lint only: no C++ build, no simulation. Fast structural check.
rtl-lint:
	$(VERILATOR) --lint-only -Wall --top-module fosu_classify_stage $(RTL_SRCS)
	$(VERILATOR) --lint-only -Wall --top-module and32_demo rtl/examples/and32.sv

# Teaching example (not part of the parser): AND two 32-bit numbers at three
# levels of interface -- combinational, registered, valid/ready handshake.
rtl-example: | build
	$(VERILATOR) --cc --exe --build -j 0 -Wall \
	  --top-module and32_demo -Mdir build/vexample -o Vtb_and32 \
	  -CFLAGS "$(RTL_CFLAGS)" \
	  rtl/examples/and32.sv $(CURDIR)/sim/tb_and32.cpp
	./build/vexample/Vtb_and32

clean:
	rm -rf build

.PHONY: all test bench bench-native bench-pgo rtl-test rtl-lint rtl-example clean
