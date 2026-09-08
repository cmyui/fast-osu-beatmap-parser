# C API

Python applications should start with the [Python package](python.md), which
provides `fosu.parse_file(path)` and manages ownership automatically. This page
documents the lower-level C interface.

```sh
cmake -S . -B build/native -DCMAKE_CXX_COMPILER=g++
cmake --build build/native --target check -j4
```

This builds `build/native/libfosu.so` on Linux x86-64 or
`build/native/libfosu.dylib` on Apple Silicon. See
[build configurations](build.md) for all targets.
`src/fosu/bindings/c_api.h` is a C-compatible header. The default library selects
AVX2 on x86-64-v3 CPUs with OS support for XMM/YMM state, or NEON on
supported AArch64 systems, otherwise scalar. Apple Silicon uses NEON. The
Linux AVX2 backend retains Zen 4 scheduling. All backends use the same ABI and
result ownership contract.

`fosu_backend_name()` returns `"scalar"`, `"avx2"` or `"neon"`.
`fosu_backend_available("avx2")` checks CPU/OS support and whether the adjacent
engine library is present; it does not load it. Set `FOSU_BACKEND=auto|scalar|avx2|neon` before the first
call to select a backend; `FOSU_FORCE_SCALAR=1` also works when `FOSU_BACKEND` is
unset. An unknown, unsupported or unloadable forced request makes `fosu_backend_name()` and
`fosu_new()` return NULL. Selection is thread-safe and fixed for the lifetime
of that loaded library, even if the environment subsequently changes.

The core contains Parser ownership, C adaptation and the scalar engine. Keep
`libfosu_engine_avx2.so` or `libfosu_engine_neon.dylib` (as appropriate for the
platform) beside the core library. Automatic selection falls back to scalar
if the optimized engine cannot be loaded. Only the selected engine is loaded,
and it is unloaded with the core after parser storage cleanup.

One cached function pointer invokes the complete parsing engine per document;
parsing loops have no runtime ISA dispatch. Engine types are a private,
versioned interface: distribute matching core and engine builds together.
Header-only C++ remains
compile-time selected; C++ applications can use this C ABI for runtime selection.

On Linux the default build bundles private copies of the C++ runtime and
unwinder. Only `fosu_*` functions are exported; no C++ exceptions cross the ABI.
Arena storage uses private virtual-memory reservations and on-demand page
commits. Small control objects still use the library's `operator new`/`delete`,
so a host's C++ replacement operators and `std::set_new_handler` state do not
apply. The header-only C++ interface retains its caller's runtime and operators.

To link the system C++ runtime instead, configure with
`-DFOSU_BUNDLE_RUNTIME=OFF` and rebuild. macOS uses the system runtime.
Bundled runtime fixes require rebuilding and distributing the library; updating
the host's shared C++ runtime does not update the private copy.
The [compiled-target hardening policy](build.md#hardening) applies to all backends.
The `check` target also checks Linux binary protections, exports, dependencies
and allocation-failure translation in a resource-limited child process.

## Minimal C usage

```c
#include <fosu/bindings/c_api.h>
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

Compile on Linux with `cc -Isrc examples/c_example.c -Lbuild/native -lfosu
-Wl,-rpath,"$PWD/build/native" -o build/example`. Check the ABI version before accessing
records. Version 2 defines the current structs, including double timestamps.
Incompatible struct changes require a version bump and rebuilding bindings.

## Storage

A handle owns a parser with one working arena for its padded input, domain
records and converted C records. It reserves virtual address space up front,
makes it writable in fixed-size chunks and reuses touched pages across parses
on the same handle. Each parse creates a fresh logical result. Independent
handles keep separate live storage and may be used concurrently.

Freeing a handle returns its arena to a bounded lock-free single-slot pool. A
later handle can reuse its mapping and touched pages; checked-out arenas are
exclusively owned. Library unload releases the spare mapping.

On Linux, `MADV_HUGEPAGE` requests larger pages where the host enables them.
The advice is optional and never changes host settings. Define
`FOSU_ARENA_NO_HUGEPAGE` when building to omit it. Page provisioning affects first use;
benchmark on the deployment host.

## Contract

- `fosu_new`/`fosu_free` own a reusable handle. Freeing `NULL` is allowed.
- `fosu_parse` copies an input byte span into padded, owned storage;
  `fosu_parse_file` reads directly into that storage. Both reuse the parser
  arena.
- `fosu_get_view` returns borrowed metadata and bulk arrays after a successful
  parse, or `NULL` before success/after failure. **Every new parse call,
  including a failed or argument-rejected call, invalidates the previous view.**
- The parser produces its domain records first. The C boundary copies them once
  into versioned ABI records in the parser arena; there is no allocation or FFI
  call per object. Hitobjects occupy 48 bytes and sliders 40 bytes on the
  supported 64-bit ABIs; point pairs occupy 8 and timing points 40.
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
