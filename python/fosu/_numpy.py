"""Optional NumPy support; importing fosu does not import NumPy."""
from functools import lru_cache
import numpy as np
from ._native import ffi


@lru_cache(maxsize=None)
def dtype_for(ctype):
    if ctype.kind != "struct":
        return np.dtype("u4")  # combo colours
    names, formats, offsets = [], [], []
    for name, field in ctype.fields:
        if name == "reserved":
            continue
        size = ffi.sizeof(field.type)
        if field.type.kind == "struct":
            fmt = dtype_for(field.type)
        elif field.type.cname == "char":
            fmt = "S1"
        elif field.type.cname in ("double", "float"):
            fmt = f"f{size}"
        else:
            signed = int(ffi.cast(field.type, -1)) < 0
            fmt = f"{'i' if signed else 'u'}{size}"
        names.append("slider_index" if ctype == ffi.typeof("fosu_hit_object") and name == "slider" else name)
        formats.append(fmt)
        offsets.append(field.offset)
    return np.dtype({"names": names, "formats": formats, "offsets": offsets,
                     "itemsize": ffi.sizeof(ctype)})
