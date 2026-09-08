"""Generate CFFI wrappers; CMake owns all compilation and linking."""

import re
import sys
from pathlib import Path

from cffi import FFI

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src/fosu/bindings/c_api.h").read_text()
header = re.sub(r"#ifdef __cplusplus\n.*?#endif", "", header, flags=re.DOTALL)
header = "\n".join(
    line
    for line in header.splitlines()
    if not line.startswith("#")
    or line.startswith(("#define FOSU_ABI_VERSION ", "#define FOSU_NO_SLIDER "))
)
header = header.replace("FOSU_API ", "")


def builder() -> FFI:
    ffi = FFI()
    ffi.cdef(header)
    ffi.set_source("fosu._core", "#include <fosu/bindings/c_api.h>\n")
    return ffi


if __name__ == "__main__":
    builder().emit_c_code(sys.argv[1])
