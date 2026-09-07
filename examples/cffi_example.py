"""PYTHONPATH=build/cffi python3 examples/cffi_example.py map.osu"""
import sys
import numpy as np
from _fosu import ffi, lib

if lib.fosu_abi_version() != lib.FOSU_ABI_VERSION:
    raise RuntimeError('libfosu ABI version mismatch; rebuild the CFFI module')

handle = lib.fosu_new()
if handle == ffi.NULL:
    raise MemoryError('fosu_new')
try:
    status = lib.fosu_parse_file(handle, sys.argv[1].encode(), lib.FOSU_ALL)
    if status != lib.FOSU_OK:
        raise RuntimeError(f'fosu_parse_file status {status}')
    view = lib.fosu_get_view(handle)
    text = ffi.buffer(view.text, view.text_size)
    title = view.metadata.title
    print(bytes(text[title.offset:title.offset + title.length]).decode('utf-8'))

    # NumPy views the C array directly. Use compiler-checked CFFI offsets;
    # no per-object FFI calls and no intermediate Python object list.
    fields = ['x', 'y', 'time', 'type', 'hitsound']
    dtype = np.dtype({
        'names': fields, 'formats': ['i4', 'i4', 'i4', 'u4', 'u4'],
        'offsets': [ffi.offsetof('fosu_hit_object', name) for name in fields],
        'itemsize': ffi.sizeof('fosu_hit_object'),
    })
    if view.hit_object_count:
        objects = np.frombuffer(
            ffi.buffer(view.hit_objects, view.hit_object_count * dtype.itemsize), dtype=dtype)
        objects.setflags(write=False)
        print(f'{len(objects)} objects; first five: {objects[:5]}')
    else:
        print('0 objects')
    # The same handle can parse another map and reuse capacity. All views,
    # including NumPy arrays, expire on that call or on fosu_free. Use objects.copy() to retain data.
finally:
    lib.fosu_free(handle)
