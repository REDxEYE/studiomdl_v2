#!/usr/bin/env python3
"""ctypes smoke test of the studiomdl shared library: the C ABI as a Python host (e.g. pylib) uses it.

Builds a static prop (a cube), compiles it in memory, checks SMDL_OK and the output files, saves them to a directory.

usage: ctypes_smoke.py <path to libstudiomdl.so / studiomdl.dll> [output directory]
"""
import ctypes
import os
import sys
import tempfile
from ctypes import POINTER, c_char_p, c_float, c_int, c_size_t, c_uint32, c_void_p

SMDL_OK = 0
SMDL_ERROR_COMPILE = 7
SMDL_NO_BONE = 0xFFFFFFFF

LogFn = ctypes.CFUNCTYPE(None, c_void_p, c_int, c_char_p)

POSITIONS = [(-8, -8, 0), (8, -8, 0), (8, 8, 0), (-8, 8, 0), (-8, -8, 16), (8, -8, 16), (8, 8, 16), (-8, 8, 16)]
UVS = [(0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1)]
# Normals point away from the box center (smooth shading; the compiler requires normals)
NORMALS = [tuple(c / 13.856406 for c in (x, y, z - 8)) for x, y, z in POSITIONS]
TRIANGLES = [(0, 3, 2), (0, 2, 1), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4),
             (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]


def load(path):
    lib = ctypes.CDLL(os.path.abspath(path))
    p = c_void_p
    sigs = {
        "smdl_api_version": (c_uint32, []),
        "smdl_status_string": (c_char_p, [c_int]),
        "smdl_model_create": (p, [c_char_p]),
        "smdl_model_destroy": (None, [p]),
        "smdl_model_last_error": (c_char_p, [p]),
        "smdl_model_add_material_path": (c_int, [p, c_char_p]),
        "smdl_model_set_surfaceprop": (c_int, [p, c_char_p]),
        "smdl_model_set_static_prop": (c_int, [p, c_int]),
        "smdl_model_add_bone": (c_int, [p, c_char_p, c_uint32, POINTER(c_float), POINTER(c_float), POINTER(c_uint32)]),
        "smdl_model_create_mesh": (c_int, [p, c_char_p, POINTER(p)]),
        "smdl_mesh_get_geometry": (c_int, [p, POINTER(p)]),
        "smdl_geometry_set_positions": (c_int, [p, POINTER(c_float), c_size_t, c_size_t]),
        "smdl_geometry_set_uvs": (c_int, [p, c_uint32, POINTER(c_float), c_size_t, c_size_t]),
        "smdl_geometry_set_normals": (c_int, [p, POINTER(c_float), c_size_t, c_size_t]),
        "smdl_geometry_add_material": (c_int, [p, c_char_p, POINTER(c_uint32)]),
        "smdl_geometry_set_triangles": (c_int, [p, POINTER(c_uint32), c_size_t, c_size_t, POINTER(c_uint32), c_size_t]),
        "smdl_model_compile": (c_int, [p, c_char_p, LogFn, c_void_p, POINTER(p)]),
        "smdl_result_destroy": (None, [p]),
        "smdl_result_status": (c_int, [p]),
        "smdl_result_file_count": (c_size_t, [p]),
        "smdl_result_file": (c_int, [p, c_size_t, POINTER(c_char_p), POINTER(c_void_p), POINTER(c_size_t)]),
        "smdl_result_save_to": (c_int, [p, c_char_p]),
        "smdl_result_error_message": (c_char_p, [p]),
    }
    for name, (restype, argtypes) in sigs.items():
        function = getattr(lib, name)
        function.restype = restype
        function.argtypes = argtypes
    return lib


def check(lib, status, what):
    if status != SMDL_OK:
        raise AssertionError(f"{what}: {lib.smdl_status_string(status).decode()} ({status})")


def floats(rows):
    flat = [float(v) for row in rows for v in row]
    return (c_float * len(flat))(*flat)


def build_cube(lib, name):
    model = lib.smdl_model_create(name.encode())
    assert model, "smdl_model_create returned NULL"
    check(lib, lib.smdl_model_add_material_path(model, b"models/ctypes"), "add_material_path")
    check(lib, lib.smdl_model_set_surfaceprop(model, b"wood"), "set_surfaceprop")
    check(lib, lib.smdl_model_set_static_prop(model, 1), "set_static_prop")
    root = c_uint32()
    check(lib, lib.smdl_model_add_bone(model, b"root", SMDL_NO_BONE, floats([(0, 0, 0)]), floats([(0, 0, 0, 1)]),
                                       ctypes.byref(root)), "add_bone")
    mesh = c_void_p()
    check(lib, lib.smdl_model_create_mesh(model, b"crate", ctypes.byref(mesh)), "create_mesh")
    geometry = c_void_p()
    check(lib, lib.smdl_mesh_get_geometry(mesh, ctypes.byref(geometry)), "get_geometry")
    check(lib, lib.smdl_geometry_set_positions(geometry, floats(POSITIONS), len(POSITIONS), 0), "set_positions")
    check(lib, lib.smdl_geometry_set_uvs(geometry, 0, floats(UVS), len(UVS), 0), "set_uvs")
    check(lib, lib.smdl_geometry_set_normals(geometry, floats(NORMALS), len(NORMALS), 0), "set_normals")
    material = c_uint32()
    check(lib, lib.smdl_geometry_add_material(geometry, b"crate_wood", ctypes.byref(material)), "add_material")
    indices = (c_uint32 * (3 * len(TRIANGLES)))(*[i for tri in TRIANGLES for i in tri])
    materials = (c_uint32 * len(TRIANGLES))(*([material.value] * len(TRIANGLES)))
    check(lib, lib.smdl_geometry_set_triangles(geometry, indices, len(TRIANGLES), 0, materials, 0), "set_triangles")
    return model


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    lib = load(sys.argv[1])
    assert lib.smdl_api_version() >= 1

    messages = []

    @LogFn
    def log(_user, severity, text):
        messages.append((severity, text.decode(errors="replace")))

    model = build_cube(lib, "ctypes/crate")
    result = c_void_p()
    status = lib.smdl_model_compile(model, None, log, None, ctypes.byref(result))
    assert result, "no result"
    try:
        assert status == SMDL_OK, f"compile: {status} {lib.smdl_result_error_message(result)}"
        assert lib.smdl_result_status(result) == SMDL_OK
        assert messages, "the log callback received nothing"
        count = lib.smdl_result_file_count(result)
        assert count >= 3, f"expected at least 3 files, got {count}"
        names = {}
        for i in range(count):
            path, data, size = c_char_p(), c_void_p(), c_size_t()
            check(lib, lib.smdl_result_file(result, i, ctypes.byref(path), ctypes.byref(data), ctypes.byref(size)), "file")
            assert size.value > 0
            names[path.value.decode()] = ctypes.string_at(data, size.value)
        assert "models/ctypes/crate.mdl" in names, sorted(names)
        assert names["models/ctypes/crate.mdl"][:4] == b"IDST"
        assert names["models/ctypes/crate.vvd"][:4] == b"IDSV"
        assert "models/ctypes/crate.dx90.vtx" in names

        out = sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix="smdl_ctypes_")
        check(lib, lib.smdl_result_save_to(result, out.encode()), "save_to")
        for path, data in names.items():
            with open(os.path.join(out, path), "rb") as handle:
                assert handle.read() == data, path
    finally:
        lib.smdl_result_destroy(result)
        lib.smdl_model_destroy(model)

    # A failing compile reports through the result and leaves the library usable
    empty = lib.smdl_model_create(b"ctypes/empty")
    result = c_void_p()
    assert lib.smdl_model_compile(empty, None, LogFn(0), None, ctypes.byref(result)) == SMDL_ERROR_COMPILE
    assert result and lib.smdl_result_file_count(result) == 0
    assert lib.smdl_result_error_message(result)
    lib.smdl_result_destroy(result)
    lib.smdl_model_destroy(empty)

    print(f"ctypes_smoke: ok ({count} files, {len(messages)} log messages)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
