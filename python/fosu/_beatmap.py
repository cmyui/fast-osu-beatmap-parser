"""Owned native results with lazy, read-only Python views."""

from collections.abc import Sequence
from enum import IntFlag
from errno import EIO
from operator import attrgetter, index
from os import fsencode, fspath, strerror

from ._native import ffi, lib

# Quoted types and this sentinel keep annotation dependencies off the import path.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Iterator
    from os import PathLike
    from typing import Any, overload

    from numpy.typing import DTypeLike, NDArray
    from typing_extensions import Buffer

    # Generated CFFI structs expose fields dynamically; the C header is their schema.
    CData = Any

if lib.fosu_abi_version() != lib.FOSU_ABI_VERSION:
    raise ImportError("fosu native ABI version mismatch; reinstall the package")

NO_SLIDER: int = lib.FOSU_NO_SLIDER


class Sections(IntFlag):
    GENERAL = lib.FOSU_GENERAL
    EDITOR = lib.FOSU_EDITOR
    METADATA = lib.FOSU_METADATA
    DIFFICULTY = lib.FOSU_DIFFICULTY
    EVENTS = lib.FOSU_EVENTS
    TIMING_POINTS = lib.FOSU_TIMING_POINTS
    COLOURS = lib.FOSU_COLOURS
    HIT_OBJECTS = lib.FOSU_HIT_OBJECTS
    ALL = lib.FOSU_ALL


class _Owner:
    __slots__ = ("handle", "view", "__weakref__")

    def __init__(self, handle: "CData") -> None:
        self.handle = handle
        self.view = lib.fosu_get_view(handle)

    def string(self, ref: "CData") -> str:
        if not ref.length:
            return ""
        return ffi.unpack(self.view.text + ref.offset, ref.length).decode(
            "utf-8", "surrogateescape"
        )

    def buffer(self, ptr: "CData", size: int) -> memoryview:
        # ffi.buffer retains this exact gc cdata, whose callback retains us.
        # This ownership survives NumPy slicing, np.asarray and memoryviews.
        retained = ffi.gc(ptr, lambda unused, owner=self: None)
        return memoryview(ffi.buffer(retained, size)).toreadonly()


class _Record:
    __slots__ = ("_owner", "_data")
    _fields: "tuple[str, ...]"

    def __init__(self, owner: "_Owner", data: "CData") -> None:
        self._owner = owner
        self._data = data

    def __repr__(self) -> str:
        fields = (f"{name}={getattr(self, name)!r}" for name in self._fields)
        return f"{type(self).__name__}({', '.join(fields)})"

    def __eq__(self, other: object) -> bool:
        if type(self) is not type(other):
            return NotImplemented
        return self._owner is other._owner and ffi.addressof(
            self._data
        ) == ffi.addressof(other._data)

    def __hash__(self) -> int:
        return hash((type(self), self._owner, ffi.addressof(self._data)))


class HitObject(_Record):
    __slots__ = ()

    @property
    def slider_index(self) -> int:
        return self._data.slider

    @property
    def slider(self) -> "Slider | None":
        value = self.slider_index
        return (
            None
            if value == lib.FOSU_NO_SLIDER
            else Slider(self._owner, self._owner.view.sliders[value])
        )

    @property
    def is_circle(self) -> bool:
        return bool(self._data.type & 1)

    @property
    def is_slider(self) -> bool:
        return bool(self._data.type & 2)

    @property
    def is_spinner(self) -> bool:
        return bool(self._data.type & 8)

    @property
    def is_hold(self) -> bool:
        return bool(self._data.type & 128)

    @property
    def is_new_combo(self) -> bool:
        return bool(self._data.type & 4)


class Slider(_Record):
    __slots__ = ()

    @property
    def points(self) -> "RecordSequence":
        view = self._owner.view
        start = self._data.point_begin
        return RecordSequence(
            self._owner,
            view.points,
            view.point_count,
            Point,
            range(start, start + self._data.point_count),
        )


class Point(_Record):
    __slots__ = ()


class TimingPoint(_Record):
    __slots__ = ()


class Break(_Record):
    __slots__ = ()


class ParseStats(_Record):
    __slots__ = ()


class RecordSequence(Sequence):
    """A read-only sequence; indexing creates one record and slicing stays lazy."""

    __slots__ = ("_owner", "_ptr", "_count", "_record", "_indices")

    def __init__(
        self,
        owner: "_Owner",
        ptr: "CData",
        count: int,
        record: "type[_Record] | None",
        indices: "range | None" = None,
    ) -> None:
        self._owner, self._ptr, self._count, self._record = owner, ptr, count, record
        self._indices = range(count) if indices is None else indices

    def __len__(self) -> int:
        return len(self._indices)

    if TYPE_CHECKING:

        @overload
        def __getitem__(self, key: int) -> "_Record | int": ...
        @overload
        def __getitem__(self, key: "slice") -> "RecordSequence": ...

    def __getitem__(self, key: "int | slice") -> "_Record | int | RecordSequence":
        if isinstance(key, slice):
            return RecordSequence(
                self._owner, self._ptr, self._count, self._record, self._indices[key]
            )
        data = self._ptr[self._indices[index(key)]]
        return self._record(self._owner, data) if self._record is not None else data

    def __iter__(self) -> "Iterator[_Record | int]":
        for i in self._indices:
            data = self._ptr[i]
            yield self._record(self._owner, data) if self._record is not None else data

    def __repr__(self) -> str:
        name = self._record.__name__ if self._record is not None else "int"
        return f"RecordSequence[{name}](length={len(self)})"

    def to_numpy(self) -> "NDArray[Any]":
        """Return a read-only, zero-copy record array (requires NumPy).

        String fields contain offset/length pairs into Beatmap.text;
        use record attributes for decoded Python strings.
        The array and all its views keep the native allocation alive.
        """
        import numpy as np

        from ._numpy import dtype_for

        dtype = dtype_for(ffi.typeof(self._ptr).item)
        if not self:
            return np.frombuffer(b"", dtype=dtype)
        array = np.frombuffer(
            self._owner.buffer(self._ptr, self._count * dtype.itemsize), dtype=dtype
        )
        return array[self._indices.start :: self._indices.step][: len(self)]

    def __array__(
        self, dtype: "DTypeLike | None" = None, copy: "bool | None" = None
    ) -> "NDArray[Any]":
        array = self.to_numpy()
        if dtype is not None and array.dtype != dtype:
            if copy is False:
                raise ValueError("changing dtype requires a copy")
            return array.astype(dtype)
        return array.copy() if copy else array


class Beatmap(_Record):
    """An immutable parsed beatmap owning its input and native record arrays."""

    __slots__ = ("_strings", "__weakref__")

    if TYPE_CHECKING:
        # These descriptors are installed from the C metadata schema below.
        title: str
        artist: str
        version: str

    def __init__(self, owner: "_Owner", data: "CData") -> None:
        super().__init__(owner, data)
        self._strings: "dict[str, str] | None" = None

    def _string(self, name: str) -> str:
        if self._strings is None:
            self._strings = {}
        if name not in self._strings:
            self._strings[name] = self._owner.string(getattr(self._data, name))
        return self._strings[name]

    @property
    def source_size(self) -> int:
        return self._owner.view.source_size

    @property
    def text(self) -> memoryview:
        """A read-only memoryview of the original .osu bytes."""
        return self._owner.buffer(self._owner.view.text, self.source_size)

    @property
    def stats(self) -> "ParseStats":
        return ParseStats(self._owner, self._owner.view.stats)

    def __repr__(self) -> str:
        return (
            f"Beatmap(title={self.title!r}, artist={self.artist!r}, "
            f"version={self.version!r}, hit_objects={self._owner.view.hit_object_count})"
        )


def _string_property(name: str) -> "property":
    return property(lambda self: self._owner.string(getattr(self._data, name)))


def _bool_property(name: str) -> "property":
    return property(lambda self: bool(getattr(self._data, name)))


def _char_property(name: str) -> "property":
    return property(
        lambda self: getattr(self._data, name).decode("ascii", "surrogateescape")
    )


def _fields(
    cls: "type[_Record]",
    ctype: str,
    *,
    omit: "tuple[str, ...]" = (),
    bools: "tuple[str, ...]" = (),
) -> None:
    """Derive descriptors from the compiled C header, avoiding a second schema."""
    names = []
    for name, field in ffi.typeof(ctype).fields:
        if name in omit or name == "reserved":
            continue
        if field.type == ffi.typeof("fosu_string_ref"):
            if cls is Beatmap:

                def get_string(self: "Beatmap", key: str = name) -> str:
                    return self._string(key)

                prop = property(get_string)
            else:
                prop = _string_property(name)
        elif name in bools:
            prop = _bool_property(name)
        elif field.type.cname == "char":
            prop = _char_property(name)
        else:
            prop = property(attrgetter("_data." + name))
        setattr(cls, name, prop)
        names.append(name)
    cls._fields = tuple(names)


_fields(
    Beatmap,
    "fosu_metadata",
    bools=(
        "letterbox_in_breaks",
        "widescreen_storyboard",
        "epilepsy_warning",
        "special_style",
        "use_skin_sprites",
        "samples_match_playback_rate",
    ),
)
_fields(HitObject, "fosu_hit_object", omit=("slider",))
HitObject._fields += ("slider_index",)
_fields(Slider, "fosu_slider")
_fields(Point, "fosu_point")
_fields(TimingPoint, "fosu_timing_point", bools=("uninherited",))
_fields(Break, "fosu_break")
_fields(ParseStats, "fosu_stats")


def _array_property(
    field: str, count: str, record: "type[_Record] | None"
) -> "property":
    def get(self: "Beatmap") -> "RecordSequence":
        view = self._owner.view
        return RecordSequence(
            self._owner, getattr(view, field), getattr(view, count), record
        )

    return property(get)


for _name, _field, _count, _record in (
    ("hit_objects", "hit_objects", "hit_object_count", HitObject),
    ("sliders", "sliders", "slider_count", Slider),
    ("slider_points", "points", "point_count", Point),
    ("timing_points", "timing_points", "timing_point_count", TimingPoint),
    ("breaks", "breaks", "break_count", Break),
    ("combo_colours", "colours", "colour_count", None),
):
    setattr(Beatmap, _name, _array_property(_field, _count, _record))


def _sections(value: "Sections | int") -> int:
    value = index(value)
    if value < 0 or value & ~lib.FOSU_ALL:
        raise ValueError("sections must contain only fosu.Sections flags")
    return value


def _new_handle() -> "CData":
    handle = lib.fosu_new()
    if handle == ffi.NULL:
        raise MemoryError("could not allocate a beatmap")
    try:
        return ffi.gc(handle, lib.fosu_free)
    except BaseException:
        lib.fosu_free(handle)
        raise


def _result(
    handle: "CData", status: int, path: "str | bytes | None" = None
) -> "Beatmap":
    if status != lib.FOSU_OK:
        error = ffi.errno or EIO
        ffi.release(handle)
        if status == lib.FOSU_OUT_OF_MEMORY:
            raise MemoryError("could not allocate beatmap data")
        if status == lib.FOSU_IO_ERROR:
            raise OSError(error, strerror(error), path)
        raise ValueError("beatmap input exceeds the supported size")
    owner = _Owner(handle)
    return Beatmap(owner, owner.view.metadata)


def parse(data: "Buffer", *, sections: "Sections | int" = Sections.ALL) -> "Beatmap":
    """Parse a contiguous byte buffer into an owned Beatmap, supplying padding."""
    flags = _sections(sections)
    if isinstance(data, bytes):
        handle = _new_handle()
        return _result(handle, lib.fosu_parse(handle, data, len(data), flags))
    try:
        buffer = ffi.from_buffer("const char[]", data)
    except (ValueError, BufferError) as error:
        raise BufferError("parse requires a C-contiguous buffer") from error
    with buffer:
        handle = _new_handle()
        return _result(handle, lib.fosu_parse(handle, buffer, len(buffer), flags))


def parse_file(
    path: "str | bytes | PathLike[str] | PathLike[bytes]",
    *,
    sections: "Sections | int" = Sections.ALL,
) -> "Beatmap":
    """Read and parse a path into an owned Beatmap, raising OSError on I/O failure."""
    flags = _sections(sections)
    path = fspath(path)
    encoded = fsencode(path)
    if b"\0" in encoded:
        raise ValueError("file path contains a null byte")
    handle = _new_handle()
    return _result(handle, lib.fosu_parse_file(handle, encoded, flags), path)
