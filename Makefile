# Products and checks share an explicit build configuration. See docs/build.md.
.DEFAULT_GOAL := all
ifeq ($(origin CXX),default)
CXX = clang++
endif
ifeq ($(origin CC),default)
CC = clang
endif
PYTHON ?= python3
PROFILE ?= release
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ISA ?= $(if $(filter x86_64,$(UNAME_M)),avx2,scalar)
LIB_RUNTIME ?= $(if $(filter Linux,$(UNAME_S)),$(if $(filter sanitize,$(PROFILE)),shared,bundled),shared)

ifeq ($(filter $(PROFILE),release debug portable sanitize),)
$(error PROFILE must be release, debug, portable, or sanitize)
endif
ifeq ($(filter $(ISA),scalar avx2),)
$(error ISA must be scalar or avx2)
endif
ifeq ($(filter $(LIB_RUNTIME),bundled shared),)
$(error LIB_RUNTIME must be bundled or shared)
endif

BUILD_DIR ?= build/$(PROFILE)-$(ISA)-$(LIB_RUNTIME)
CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra
CFLAGS ?= -O2 -Wall -Wextra
ARCH_FLAGS =
RUN =
ifeq ($(ISA),avx2)
ARCH_FLAGS = -march=x86-64-v3
ifeq ($(UNAME_S),Linux)
ARCH_FLAGS += -mtune=znver4 -fno-plt -fno-stack-protector
endif
ifeq ($(UNAME_S)-$(UNAME_M),Darwin-arm64)
ARCH_FLAGS += -target x86_64-apple-macos12
RUN = arch -x86_64
endif
else ifeq ($(UNAME_M),x86_64)
ARCH_FLAGS = -march=x86-64
endif

PROFILE_FLAGS =
ifeq ($(PROFILE),debug)
PROFILE_FLAGS = -O0 -g -D_GLIBCXX_DEBUG
endif
ifeq ($(PROFILE),portable)
PROFILE_FLAGS = -DFOSU_PORTABLE_VECTORS
endif
SANITIZERS = -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
ifeq ($(PROFILE),sanitize)
PROFILE_FLAGS = -O1 -g $(SANITIZERS) -D_GLIBCXX_SANITIZE_VECTOR=1
endif
COMPILE = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ARCH_FLAGS) $(PROFILE_FLAGS)
LIB_EXT = so
LIB_LINK_FLAGS = -shared
DL_FLAGS = -ldl
MODULE_LINK_FLAGS = -Wl,-Bsymbolic
RUNTIME_FLAGS =
EXPORT_CHECK_FLAGS =
ifeq ($(UNAME_S),Linux)
ifeq ($(LIB_RUNTIME),bundled)
RUNTIME_FLAGS = -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL
EXPORT_CHECK_FLAGS = --bundled
endif
else ifeq ($(UNAME_S),Darwin)
LIB_EXT = dylib
LIB_LINK_FLAGS = -dynamiclib -Wl,-install_name,@rpath/libfosu.dylib
DL_FLAGS =
MODULE_LINK_FLAGS =
endif
LIB = $(BUILD_DIR)/libfosu.$(LIB_EXT)
LINK_FOSU = -L$(BUILD_DIR) -lfosu -Wl,-rpath,$(abspath $(BUILD_DIR))
HEADERS := $(wildcard include/fosu/*.hpp include/fosu/*.h include/fosu/detail/*.hpp include/fosu/detail/*.h)
TEST_HEADERS := $(wildcard tests/support/*.hpp)
ONESHOT_CXX ?= g++
ONESHOT_FLAGS ?=

# Store commands per output, after a successful build. A shared timestamp
# alone misses flag changes in partial builds or within one timestamp tick.
quote = '$(subst ','"'"',$(1))'
CONFIG_ARGS = $(BUILD_DIR)/config.json --cxx=$(call quote,$(CXX)) --cc=$(call quote,$(CC)) --compile=$(call quote,$(COMPILE)) --cflags=$(call quote,$(CFLAGS)) --link=$(call quote,$(LDFLAGS) $(LIB_LINK_FLAGS) $(RUNTIME_FLAGS) $(MODULE_LINK_FLAGS) $(DL_FLAGS)) --oneshot=$(call quote,$(ONESHOT_CXX) $(ONESHOT_FLAGS))
$(BUILD_DIR)/config.json: FORCE
	@mkdir -p $(BUILD_DIR)
	@$(PYTHON) tools/build_config.py $(CONFIG_ARGS)

define compile
	@if test -n "$(filter-out FORCE $(BUILD_DIR)/config.json,$?)" || ! cmp -s $(BUILD_DIR)/config.json $@.build.json; then \
	  printf '%s\n' $(call quote,$(1)); \
	  $(1) && cp $(BUILD_DIR)/config.json $@.build.json; \
	fi
endef

all: lib
lib: $(LIB)
$(LIB): src/c_api.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -fPIC -fvisibility=hidden $(LIB_LINK_FLAGS) $(RUNTIME_FLAGS) $< $(LDFLAGS) -o $@)

PARSER_TESTS = $(addprefix $(BUILD_DIR)/test_,numeric sections storage hardening)
$(PARSER_TESTS): $(BUILD_DIR)/test_%: tests/test_%.cpp $(HEADERS) $(TEST_HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -UNDEBUG $< $(LDFLAGS) -o $@)
test-parser: $(PARSER_TESTS)
	@set -e; for test in $(PARSER_TESTS); do $(RUN) $$test; done

test: test-parser test-c-api
$(BUILD_DIR)/test_c_api: tests/test_c_api.cpp $(HEADERS) $(TEST_HEADERS) $(LIB) FORCE $(BUILD_DIR)/config.json Makefile
	$(call compile,$(COMPILE) -UNDEBUG $< $(LINK_FOSU) -pthread $(LDFLAGS) -o $@)
$(BUILD_DIR)/test_c_api_unload: tests/test_c_api_unload.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -UNDEBUG $< $(DL_FLAGS) $(LDFLAGS) -o $@)
$(BUILD_DIR)/test_c_api_io: tests/test_c_api_io.cpp $(HEADERS) $(LIB) FORCE $(BUILD_DIR)/config.json Makefile
	$(call compile,$(COMPILE) -UNDEBUG $< $(LINK_FOSU) $(LDFLAGS) -o $@)
test-c-api: $(BUILD_DIR)/test_c_api $(BUILD_DIR)/test_c_api_unload
	$(RUN) $(BUILD_DIR)/test_c_api
	$(RUN) $(BUILD_DIR)/test_c_api_unload $(abspath $(LIB))
ifeq ($(UNAME_S),Linux)
ifneq ($(PROFILE),sanitize)
	$(PYTHON) tests/test_library_exports.py $(LIB) $(EXPORT_CHECK_FLAGS)
	$(BUILD_DIR)/test_c_api_io
test-c-api: $(BUILD_DIR)/test_c_api_io
endif
endif

# Rosetta is opt-in; ordinary tests exercise the host's ISA.
test-rosetta:
	$(MAKE) ISA=avx2 test

$(BUILD_DIR)/fuzz_parser: tests/fuzz_parser.cpp $(HEADERS) $(TEST_HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g $(ARCH_FLAGS) $(SANITIZERS) -UNDEBUG -fsanitize=fuzzer $< -o $@)
FUZZ_SECONDS ?= 60
fuzz-smoke: $(BUILD_DIR)/fuzz_parser
	mkdir -p $(BUILD_DIR)/fuzz-corpus
	cp tests/fuzz-seeds/*.osu $(BUILD_DIR)/fuzz-corpus/
	$(RUN) $(BUILD_DIR)/fuzz_parser $(BUILD_DIR)/fuzz-corpus -dict=tests/parser.dict -max_total_time=$(FUZZ_SECONDS) -max_len=65536 -artifact_prefix=$(BUILD_DIR)/

$(BUILD_DIR)/reference_native: tests/reference/native.cpp $(HEADERS) $(TEST_HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -UNDEBUG $< $(LDFLAGS) -o $@)
$(BUILD_DIR)/reference_c_api: tests/reference/c_api.cpp $(HEADERS) $(TEST_HEADERS) $(LIB) FORCE $(BUILD_DIR)/config.json Makefile
	$(call compile,$(COMPILE) -UNDEBUG $< $(LINK_FOSU) $(LDFLAGS) -o $@)
$(BUILD_DIR)/numeric_oracle.$(LIB_EXT): tests/reference/numeric.cpp $(HEADERS) $(TEST_HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PROFILE_FLAGS) -UNDEBUG -fPIC -fvisibility=hidden $(LIB_LINK_FLAGS) $< $(LDFLAGS) -o $@)
$(BUILD_DIR)/validate_corpus: tests/validate_corpus.cpp $(HEADERS) $(TEST_HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -UNDEBUG $< $(DL_FLAGS) $(LDFLAGS) -o $@)
references: $(BUILD_DIR)/reference_native $(BUILD_DIR)/reference_c_api $(BUILD_DIR)/numeric_oracle.$(LIB_EXT) $(BUILD_DIR)/validate_corpus

# A separate, deliberately freestanding GCC/Linux product; no runtime overrides
# leak into C++, C ABI, or Python builds. CI uses ONESHOT_FLAGS=-march=x86-64-v3.
$(BUILD_DIR)/fosu_oneshot: oneshot/main.cpp oneshot/runtime.hpp oneshot/build.sh $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,CXX=$(call quote,$(ONESHOT_CXX)) sh oneshot/build.sh $@ $(ONESHOT_FLAGS))
oneshot: $(BUILD_DIR)/fosu_oneshot
test-oneshot: oneshot $(BUILD_DIR)/reference_native
	$(PYTHON) tests/test_oneshot.py $(BUILD_DIR)/reference_native $(BUILD_DIR)/fosu_oneshot
	$(PYTHON) tests/test_oneshot_limits.py $(BUILD_DIR)/fosu_oneshot

$(BUILD_DIR)/library_native.$(LIB_EXT): bench/library_module.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -fPIC -fvisibility=hidden $(LIB_LINK_FLAGS) $< $(LDFLAGS) -o $@)
$(BUILD_DIR)/library_c_api.$(LIB_EXT): bench/library_module.cpp src/c_api.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -DFOSU_BENCH_CAPI -fPIC -fvisibility=hidden $(LIB_LINK_FLAGS) $(RUNTIME_FLAGS) $(MODULE_LINK_FLAGS) bench/library_module.cpp src/c_api.cpp $(LDFLAGS) -o $@)
$(BUILD_DIR)/library_compare: bench/library_compare.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) $< $(DL_FLAGS) $(LDFLAGS) -o $@)
$(BUILD_DIR)/library_first: bench/library_first.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) $< $(LDFLAGS) -o $@)
$(BUILD_DIR)/c_api_first: bench/c_api_first.c include/fosu/c_api.h Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -std=c11 -D_POSIX_C_SOURCE=200809L -DFOSU_DEFAULT_LIBRARY='"$(abspath $(LIB))"' $< $(DL_FLAGS) $(LDFLAGS) -o $@)
$(BUILD_DIR)/profile_parse: bench/profile_parse.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) -g $< $(LDFLAGS) -o $@)
$(BUILD_DIR)/prefix: bench/prefix.cpp $(HEADERS) Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) $< $(LDFLAGS) -o $@)
$(BUILD_DIR)/generate_corpus: bench/generate_corpus.cpp Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(COMPILE) $< $(LDFLAGS) -o $@)
$(BUILD_DIR)/oneshot_process: bench/oneshot_process.c Makefile $(BUILD_DIR)/config.json FORCE
	$(call compile,$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@)
bench-build: $(BUILD_DIR)/library_native.$(LIB_EXT) $(BUILD_DIR)/library_c_api.$(LIB_EXT) $(BUILD_DIR)/library_compare $(BUILD_DIR)/library_first $(BUILD_DIR)/c_api_first $(BUILD_DIR)/profile_parse $(BUILD_DIR)/prefix $(BUILD_DIR)/generate_corpus lib

# Runs are explicit, with corpus and repetition counts visible in the command.
REPS ?= 9
bench: bench-build
	$(RUN) $(BUILD_DIR)/library_compare $(CORPUS) $(REPS) $(BUILD_DIR)/library_native.$(LIB_EXT) $(BUILD_DIR)/library_c_api.$(LIB_EXT) > $(BUILD_DIR)/library.csv
	$(PYTHON) bench/summarize.py $(BUILD_DIR)/library.csv
bench-pgo:
	CXX=$(CXX) FOSU_BENCH_FLAGS="$(CPPFLAGS) $(CXXFLAGS) $(ARCH_FLAGS)" BUILD_DIR=$(BUILD_DIR)/pgo sh bench/library_pgo.sh $(CORPUS)

clean:
	rm -rf $(BUILD_DIR)
.PHONY: all lib test test-parser test-c-api test-rosetta fuzz-smoke references oneshot test-oneshot bench-build bench bench-pgo clean
FORCE:
.PHONY: FORCE
