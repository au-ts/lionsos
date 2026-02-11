import ctypes

_DIRECT = {
    ctypes.c_uint8: "uint8_t",
    ctypes.c_uint16: "uint16_t",
    ctypes.c_uint32: "uint32_t",
    ctypes.c_uint64: "uint64_t",
    ctypes.c_int8: "int8_t",
    ctypes.c_int16: "int16_t",
    ctypes.c_int32: "int32_t",
    ctypes.c_int64: "int64_t",
    ctypes.c_bool: "bool",
}

# ctypes may alias types (e.g. c_int32 -> c_long), so we map both the
# declared type and whatever _type_ resolves to when used in an array.
TYPE_MAP: dict[type, str] = dict(_DIRECT)
for _ct, _cname in _DIRECT.items():
    _resolved = (_ct * 1)._type_
    if _resolved not in TYPE_MAP:
        TYPE_MAP[_resolved] = _cname


class CStruct(ctypes.LittleEndianStructure):
    """Base class for C-compatible struct definitions."""
    extern = False
    @classmethod
    def validate(cls) -> None:
        """Check all field types are serialization-safe."""
        for name, ftype in cls._fields_:
            elem, _dims = unwrap_array(ftype)
            if issubclass(elem, CStruct):
                elem.validate()
            elif elem not in TYPE_MAP:
                raise ValueError(
                    f"{cls.__name__}.{name}: unsupported type {elem}. "
                    f"Only exact-width integers, bool, CStruct subclasses, and arrays are allowed."
                )

    @classmethod
    def to_c(cls) -> str:
        """Return the C typedef string for this struct."""
        cls.validate()

        lines = [f"typedef struct {cls.c_name} {{"]
        for name, ftype in cls._fields_:
            lines.append(f"\t{c_field_decl(name, ftype)};")
        lines.append(f"}} {cls.c_name}_t;")

        return "\n".join(lines)

    def to_bytes(self) -> bytes:
        """Return the raw C ABI binary representation."""
        return bytes(self)

    serialise = to_bytes


def CArray(base_type, const_name: str, constants: dict[str, int]):
    """Create a ctypes array that remembers its constant name for C codegen."""
    # Create a unique subclass to avoid ctypes caching collisions when
    # different constants resolve to the same integer length.
    arr = type(f"_{const_name}_array", (base_type * constants[const_name],), {
        "_const_name": const_name,
    })
    return arr


def unwrap_array(ftype) -> tuple:
    """Unwrap ctypes.Array types -> (c_uint8, [10,10])"""
    dims = []
    while issubclass(ftype, ctypes.Array):
        const = getattr(ftype, "_const_name", None)
        dims.append(const if const else ftype._length_)
        ftype = ftype._type_
    return ftype, dims


def c_field_decl(name: str, ftype) -> str:
    """Return the C field declaration for a single field -> 'uint8_t foo[10][10]"""
    elem, dims = unwrap_array(ftype)

    if issubclass(elem, CStruct):
        base = elem.c_name + "_t"
    else:
        base = TYPE_MAP.get(elem)
        if base is None:
            raise ValueError(
                f"Unsupported field type: {elem}. "
                f"Only exact-width integers, bool, CStruct subclasses, and arrays are allowed."
            )

    if dims:
        dim_str = "".join(f"[{d}]" for d in dims)
        return f"{base} {name}{dim_str}"
    else:
        return f"{base} {name}"


def collect_deps(c_struct: type) -> set[type]:
    """Return the set of CStruct subclasses that `c_struct` directly references."""
    deps: set[type] = set()
    for _name, ftype in c_struct._fields_:
        elem, _dims = unwrap_array(ftype)
        if issubclass(elem, CStruct) and elem is not c_struct:
            deps.add(elem)
    return deps


def topo_sort(structs: list[type]) -> list[type]:
    all_structs = set(structs)

    # Pull in transitive deps
    to_visit = list(all_structs)
    while to_visit:
        cls = to_visit.pop()
        for dep in collect_deps(cls):
            if dep not in all_structs:
                all_structs.add(dep)
                to_visit.append(dep)

    # Kahn's algorithm
    in_degree: dict[type, int] = {s: 0 for s in all_structs}
    dependents: dict[type, list[type]] = {s: [] for s in all_structs}

    for s in all_structs:
        for dep in collect_deps(s):
            if dep in all_structs:
                in_degree[s] += 1
                dependents[dep].append(s)

    queue = [s for s in all_structs if in_degree[s] == 0]
    result: list[type] = []

    while queue:
        queue.sort(key=lambda c: c.__name__)
        node = queue.pop(0)
        result.append(node)
        for dep in dependents[node]:
            in_degree[dep] -= 1
            if in_degree[dep] == 0:
                queue.append(dep)

    if len(result) != len(all_structs):
        raise ValueError("Circular dependency detected among structs")

    return result


def generate_header(*structs: type, constants: dict[str, int] = {}, includes: list[str] = []) -> str:
    """Generate a complete C header file from CStruct subclasses."""
    sorted_structs = topo_sort(list(structs))

    parts = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "#include <stdbool.h>",
    ]

    for include in includes:
        if include not in parts:
            parts.append(include)

    parts.append("")
    for name, value in constants.items():
        parts.append(f"#define {name} {value}")

    for cls in sorted_structs:
        if cls.extern:
            continue
        parts.append("")
        parts.append(cls.to_c())

    parts.append("")
    return "\n".join(parts)
