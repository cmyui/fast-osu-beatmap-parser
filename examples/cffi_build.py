"""Build the CFFI API-mode module against the same public C header."""
from pathlib import Path
import re
import sys
from cffi import FFI

root = Path(__file__).resolve().parents[1]
header = (root / 'include/fosu/c_api.h').read_text()
# C++ constructors/convenience members do not change the C record layout.
header = re.sub(r'#ifdef __cplusplus\n.*?#endif', '', header, flags=re.S)
header = '\n'.join(line for line in header.splitlines() if not line.startswith('#') or line.startswith(('#define FOSU_ABI_VERSION ', '#define FOSU_NO_SLIDER ')))
header = header.replace('FOSU_API ', '')
ffi = FFI()
ffi.cdef(header)
ffi.set_source(
    '_fosu', '#include <fosu/c_api.h>',
    include_dirs=[str(root / 'include')],
    libraries=['fosu'], library_dirs=[str(root / 'build')],
    runtime_library_dirs=[str(root / 'build')] if sys.platform != 'win32' else [],
)
if __name__ == '__main__':
    ffi.compile(tmpdir=str(root / 'build/cffi'), verbose=True)
