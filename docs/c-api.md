# C API and Python CFFI

Python applications should start with the [Python package](python.md), which
provides `fosu.parse_file(path)` and manages ownership automatically. This page
documents the lower-level C interface and manual CFFI example.

```sh
make lib test-c-api CXX=g++
```

This builds `build/libfosu.so` on Linux or `build/libfosu.dylib` on macOS.
`include/fosu/c_api.h` is a C-compatible header. The default Linux x86-64 build
requires **x86-64-v3 (including AVX2 and BMI)** and is tuned for Zen 4; it has no
CPU dispatch. Override `LIB_ARCH_FLAGS=` for a scalar build. Native Apple
Silicon builds use the scalar parser.

On Linux the default build bundles private copies of the C++ runtime and
unwinder. This reduced measured first-library-use time from about 756 to 182 µs,
with steady-state parsing unchanged. Loaded sections grow from about 65 to
250 KB. Only `fosu_*` functions are exported; no C++ exceptions cross the ABI.
Allocation still uses the process's `malloc`/`free` (including interposed
allocators), but the library uses its own `operator new`/`delete`, so a host's
C++ replacement operators and `std::set_new_handler` state do not apply. The header-only C++ interface retains
its caller's runtime and operators.

To link the system C++ runtime instead, rebuild with
`make lib test-c-api LIB_RUNTIME=shared CXX=g++`. macOS uses the system runtime.
`make test-c-api` also checks Linux exports, dependencies and allocation-failure
translation in a resource-limited child process.

## Minimal C usage

```c
#include <fosu/c_api.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2 || fosu_abi_version() != FOSU_ABI_VERSION) return 2;
    fosu_handle *handle = fosu_new();
    if (!handle) return 1;
    int status = fosu_parse_file(handle, argv[1], FOSU_ALL);
    if (status == FOSU_OK) {
        const fosu_view *view = fosu_get_view(handle);
        fwrite(view->text + view->metadata.title.offset,
               1, view->metadata.title.length, stdout);
        printf("\n%zu objects\n", view->hit_object_count);
    }
    fosu_free(handle);
    return status;
}
```

Compile on Linux with `cc -Iinclude examples/c_example.c -Lbuild -lfosu
-Wl,-rpath,"$PWD/build" -o build/example`. Check the ABI version before accessing
records. Version 2 defines the current structs, including double timestamps.
Incompatible struct changes require a version bump and rebuilding bindings.

## Storage

A handle owns an arena containing the padded input copy and contiguous record
arrays. It retains that mapping while the next input fits; arrays that outgrow
the arena use heap allocations. Input ownership and result lifetimes are the
same in either case.

Freeing a handle can park its arena, up to 8 MiB, in one spare slot per loaded
library image. A later handle can take those warm pages. Each parse creates a
fresh logical result; memory reuse never substitutes a previous parse. The
spare is released when the library unloads. Independent handles keep separate
live storage and may be used concurrently.

On Linux, `MADV_HUGEPAGE` requests larger pages where the host enables them.
The advice is optional and never changes host settings. Define
`FOSU_ARENA_NO_HUGEPAGE` when building to omit it, or `FOSU_ARENA_MALLOC` to
back the arena with `malloc` instead of `mmap`. These choices can affect first
use and memory provisioning; benchmark on the deployment host.

## Contract

- `fosu_new`/`fosu_free` own a reusable handle. Freeing `NULL` is allowed.
- `fosu_parse` copies an input byte span into padded, owned storage;
  `fosu_parse_file` reads directly into that storage. Both reuse the arena
  while it is large enough.
- `fosu_get_view` returns borrowed metadata and bulk arrays after a successful
  parse, or `NULL` before success/after failure. **Every new parse call,
  including a failed or argument-rejected call, invalidates the previous view.**
- Array records are produced directly by the parser. There is no second array
  conversion or one FFI call per object. Hitobjects occupy 48 bytes and sliders
  40 bytes on the supported 64-bit ABIs; point pairs occupy 8 and timing points 40.
- Every `fosu_string_ref` addresses `view->text` using `offset` and `length`.
  Empty strings have length zero. `source_size` is the original input size;
  `text_size` also includes 128 zero-padding bytes and six bytes for the
  default `"Normal"` sample set. Bounds checks use `text_size`.
- `FOSU_NO_SLIDER` is the sentinel for an object without a slider. Other values
  index the slider array; sliders index the shared point pool.
- Section bits such as `FOSU_DIFFICULTY` or `FOSU_HIT_OBJECTS` can be ORed
  together; use `FOSU_ALL` for a complete parse.
- Status is `FOSU_OK`, `FOSU_INVALID_ARGUMENT`, `FOSU_IO_ERROR` or
  `FOSU_OUT_OF_MEMORY`. Input is limited to `FOSU_MAX_INPUT_SIZE` (64 MiB);
  unknown section bits are rejected. Successful parsing can contain skipped malformed records;
  inspect the counters and [input contract](compatibility.md).
- On `FOSU_IO_ERROR`, `fosu_parse_file` preserves the failing operation's
  `errno`; an unexpected early EOF sets `EIO`.
- Separate handles may be used concurrently. Serialize mutations to one
  handle and finish consuming its view before reparsing or freeing it.

The C record layout is an in-process ABI, not a portable raw-memory file
format. Use the [one-shot stream](../oneshot/README.md) for serialized results.
The library does not change process allocator settings, CPU affinity or host
huge-page configuration.

## Manual CFFI example

The example uses CFFI's compiled API mode, reading declarations from the actual
C header. This lets the C compiler check sizes and field offsets. It exposes
NumPy views of the C arrays without creating a Python object for every note.

```sh
python3 -m venv build/python
build/python/bin/pip install cffi numpy setuptools
build/python/bin/python examples/cffi_build.py
PYTHONPATH=build/cffi build/python/bin/python examples/cffi_example.py map.osu
```

`setuptools` is needed for CFFI compilation on Python 3.12+. The example embeds
a library search path to this checkout's `build` directory; packaging an
application requires configuring its own installed library location.

**Keep the handle alive while reading a NumPy array.** A read-only NumPy flag
prevents accidental writes; it does not extend ownership. Use `objects.copy()`
before the next parse or `fosu_free` if the array must survive. The same rules
apply to `ffi.buffer`, strings and pointers obtained from the view. CFFI can
release the GIL during C calls, so independent handles are required for parallel
parses.
