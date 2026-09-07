"""Generate CFFI wrappers; CMake owns all compilation and linking."""

import re
import sys
from pathlib import Path

from cffi import FFI

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "include/fosu/c_api.h").read_text()
header = re.sub(r"#ifdef __cplusplus\n.*?#endif", "", header, flags=re.DOTALL)
header = "\n".join(
    line
    for line in header.splitlines()
    if not line.startswith("#")
    or line.startswith(("#define FOSU_ABI_VERSION ", "#define FOSU_NO_SLIDER "))
)
header = header.replace("FOSU_API ", "")


def builder(variant: str) -> FFI:
    ffi = FFI()
    ffi.cdef(header + "\nint fosu_python_has_avx2(void);\n")
    ffi.set_source(
        f"fosu._native_{variant}",
        """#include <fosu/c_api.h>
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
""",
    )
    return ffi


if __name__ == "__main__":
    variant, output = sys.argv[1:]
    if variant not in ("scalar", "avx2"):
        raise ValueError("unknown native variant")
    builder(variant).emit_c_code(output)
