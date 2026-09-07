"""Compile the same C ABI into self-contained scalar and AVX2 extensions."""
from pathlib import Path
import os
import re
import sys
from cffi import FFI

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "include/fosu/c_api.h").read_text()
header = re.sub(r"#ifdef __cplusplus\n.*?#endif", "", header, flags=re.S)
header = "\n".join(line for line in header.splitlines()
                   if not line.startswith("#") or line.startswith((
                       "#define FOSU_ABI_VERSION ", "#define FOSU_NO_SLIDER ")))
header = header.replace("FOSU_API ", "")


def builder(variant):
    ffi = FFI()
    ffi.cdef(header + "\nint fosu_python_has_avx2(void);\n")
    flags = ["-std=c++20", "-O3", "-g0", "-fvisibility=hidden"]
    link = []
    if variant == "avx2":
        # The runtime guard checks exactly these extra instruction sets.
        flags += ["-mavx2", "-mbmi", "-mbmi2"]
        if sys.platform == "linux":
            flags += ["-mtune=znver4"]
    if sys.platform == "linux":
        flags += ["-fno-plt"]
        # Many distributions install static C++ archives separately. Ordinary
        # source builds use the system runtime; release wheels bundle it.
        if os.environ.get("FOSU_BUNDLE_RUNTIME", os.environ.get("CIBUILDWHEEL", "0")) == "1":
            link += ["-static-libstdc++", "-static-libgcc"]
        link += ["-Wl,--exclude-libs,ALL",
                 f"-Wl,--version-script=python/{variant}.map"]
    elif sys.platform == "darwin":
        link += [f"-Wl,-exported_symbol,_PyInit__native_{variant}"]
    else:
        raise RuntimeError("fosu currently supports Linux and macOS")
    ffi.set_source(
        f"fosu._native_{variant}",
        '''#include <fosu/c_api.h>
extern "C" int fosu_python_has_avx2(void) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") &&
           __builtin_cpu_supports("bmi") && __builtin_cpu_supports("bmi2") &&
           __builtin_cpu_supports("popcnt");
#else
    return 0;
#endif
}
''',
        sources=[f"python/native_{variant}.cpp"],
        include_dirs=["include"],
        source_extension=".cpp",
        extra_compile_args=flags,
        extra_link_args=link,
        py_limited_api=True,
        define_macros=[("Py_LIMITED_API", "0x030A0000")],
    )
    return ffi


scalar = builder("scalar")
avx2 = builder("avx2")
