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

HEADERS = $(wildcard include/fosu/*.hpp include/fosu/detail/*.hpp include/fosu/detail/*.h include/fosu/*.h)

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

# In-process profiling driver (perf-friendly) and fresh-handle C API loop.
build/profile_parse: bench/profile_parse.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -g $(X86_FLAGS) $< -o $@

build/c_api_loop: bench/c_api_loop.c include/fosu/c_api.h | build
	$(CC) -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Iinclude $< -ldl -o $@

build/test_hardening: tests/test_hardening.cpp oneshot/dump.hpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(LIB_ARCH_FLAGS) $< -o $@

test: build/test_native build/test_x86 build/test_hardening
	./build/test_native
	$(X86_RUN) ./build/test_x86
	./build/test_hardening

SANITIZERS = -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
build/test_sanitize: tests/test_hardening.cpp oneshot/dump.hpp $(HEADERS) | build
	$(CXX) -std=c++20 -O1 -g -Iinclude $(LIB_ARCH_FLAGS) $(SANITIZERS) $< -o $@

test-sanitize: build/test_sanitize
	./build/test_sanitize

build/fuzz_parser: tests/fuzz_parser.cpp oneshot/dump.hpp $(HEADERS) | build
	$(CXX) -std=c++20 -O1 -g -Iinclude $(LIB_ARCH_FLAGS) $(SANITIZERS) -fsanitize=fuzzer $< -o $@

fuzz-smoke: build/fuzz_parser
	mkdir -p build/fuzz-corpus
	cp tests/fuzz-seeds/*.osu build/fuzz-corpus/
	./build/fuzz_parser build/fuzz-corpus -dict=tests/parser.dict -max_total_time=60 -max_len=65536 -artifact_prefix=build/

bench: build/bench_native build/bench_x86
	$(X86_RUN) ./build/bench_x86 $(BENCH_ARGS)

bench-native: build/bench_native
	./build/bench_native $(BENCH_ARGS)

# Cold-start single-beatmap benchmark: one fresh process per file, first
# parse timed. COLD_CPU pins each process on Linux; COLD_REPS = best-of.
COLD_REPS ?= 5
coldstart: build/coldstart_x86
	sh bench/coldstart.sh "$(X86_RUN) ./build/coldstart_x86" $(BENCH_ARGS) $(COLD_REPS) $(COLD_CPU)

# GCC/Linux: train on 20% of sorted files, compare on the other 80%.
# Run the whole command under taskset to keep every variant on one CPU.
bench-pgo: | build
	CXX=$(CXX) FOSU_BENCH_FLAGS="$(CXXFLAGS) $(LIB_ARCH_FLAGS)" sh bench/library_pgo.sh $(BENCH_ARGS)

# The freestanding executable targets Linux x86-64/Zen 4. Its build flags
# are confined to this target; library callers choose their own flags.
ONESHOT_CXX ?= g++

build/fosu_oneshot: oneshot/main.cpp oneshot/runtime.hpp include/fosu/detail/fast_float.h $(HEADERS) | build
	CXX=$(ONESHOT_CXX) sh oneshot/build.sh $@

build/oneshot_reference: bench/oneshot_reference.cpp oneshot/dump.hpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(X86_FLAGS) $< -o $@

build/oneshot_process: bench/oneshot_process.c | build
	$(CC) -O2 -Wall -Wextra $< -o $@

oneshot: build/fosu_oneshot build/oneshot_reference build/oneshot_process

# Hosted C ABI: process-global runtime and huge-page policy stay with the caller.
LIB_EXT = so
LIB_ARCH_FLAGS =
ifeq ($(UNAME_M),x86_64)
LIB_ARCH_FLAGS = $(X86_FLAGS)
endif
LIB_LINK_FLAGS = -shared
LIB_RUNTIME ?= bundled
ifeq ($(filter $(LIB_RUNTIME),bundled shared),)
$(error LIB_RUNTIME must be bundled or shared)
endif
LIB_RUNTIME_FLAGS =
LIB_EXPORT_FLAGS =
ifeq ($(UNAME_S),Linux)
ifeq ($(LIB_RUNTIME),bundled)
LIB_RUNTIME_FLAGS = -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL
LIB_EXPORT_FLAGS = --bundled
endif
endif
ifeq ($(UNAME_S),Darwin)
LIB_EXT = dylib
LIB_ARCH_FLAGS =
LIB_LINK_FLAGS = -dynamiclib -Wl,-install_name,@rpath/libfosu.dylib
endif

# Switching the runtime option must rebuild even if source timestamps match.
LIB_RUNTIME_STAMP = build/.libfosu-runtime-$(LIB_RUNTIME)-$(LIB_EXT)
$(LIB_RUNTIME_STAMP): | build
	rm -f build/.libfosu-runtime-*
	touch $@

build/libfosu.$(LIB_EXT): src/c_api.cpp $(HEADERS) Makefile $(LIB_RUNTIME_STAMP) | build
	$(CXX) $(CXXFLAGS) $(LIB_ARCH_FLAGS) -fPIC -fvisibility=hidden $(LIB_LINK_FLAGS) $(LIB_RUNTIME_FLAGS) $< -o $@

lib: build/libfosu.$(LIB_EXT)

build/test_c_api: tests/test_c_api.cpp bench/c_api_view.hpp oneshot/dump.hpp build/libfosu.$(LIB_EXT) | build
	$(CXX) $(CXXFLAGS) $(LIB_ARCH_FLAGS) $< -Lbuild -lfosu -Wl,-rpath,$(abspath build) -pthread -o $@

build/c_api_reference: bench/c_api_reference.cpp bench/c_api_view.hpp oneshot/dump.hpp build/libfosu.$(LIB_EXT) | build
	$(CXX) $(CXXFLAGS) $(LIB_ARCH_FLAGS) $< -Lbuild -lfosu -Wl,-rpath,$(abspath build) -o $@

test-c-api: build/test_c_api
	./build/test_c_api
ifeq ($(UNAME_S),Linux)
	python3 tests/test_library_exports.py build/libfosu.$(LIB_EXT) $(LIB_EXPORT_FLAGS)
	./build/test_c_api_io
endif

ifeq ($(UNAME_S),Linux)
build/test_c_api_io: tests/test_c_api_io.cpp build/libfosu.$(LIB_EXT) | build
	$(CXX) $(CXXFLAGS) $< -Lbuild -lfosu -Wl,-rpath,$(abspath build) -o $@

test-c-api: build/test_c_api_io
endif

cffi: lib
	python3 examples/cffi_build.py

clean:
	rm -rf build

.PHONY: all test test-sanitize fuzz-smoke bench bench-native bench-pgo coldstart oneshot lib test-c-api cffi clean
