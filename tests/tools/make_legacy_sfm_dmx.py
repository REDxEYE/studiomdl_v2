#!/usr/bin/env python3
"""Writes a legacy `sfm_v1` binary DMX (header `<!-- DMXVersion sfm_v1 -->`, binary encoding 0) that runs every
legacy updater, importsfmv1.cpp .. importsfmv9.cpp: float-second DmeTimeFrame times, int/float logs with curve info and
edge attributes, a camera (toneMapScale), spot/point lights (direction, vector4 colors) and an animation set with presets
and controls. No real files of this age exist, so this is the only way to exercise those importers.

usage: tests/tools/make_legacy_sfm_dmx.py <out.dmx>
Compare with SFM: tests/tools/compare_sfm_dmxconvert.py builds and checks it automatically.
"""
import struct
import sys
import uuid

# DmAttributeType_t values (include/datamodel/dmattributetypes.h)
AT = dict(ELEMENT=1, INT=2, FLOAT=3, BOOL=4, STRING=5, VECTOR3=10, VECTOR4=11,
          ELEMENT_ARRAY=15, INT_ARRAY=16, FLOAT_ARRAY=17)


def cstr(s: str) -> bytes:
    return s.encode() + b'\0'


def pack_value(kind: str, v) -> bytes:
    if kind in ('ELEMENT', 'INT'):
        return struct.pack('<i', v)
    if kind == 'FLOAT':
        return struct.pack('<f', v)
    if kind == 'BOOL':
        return struct.pack('<B', v)
    if kind == 'STRING':
        return cstr(v)
    if kind == 'VECTOR3':
        return struct.pack('<3f', *v)
    if kind == 'VECTOR4':
        return struct.pack('<4f', *v)
    if kind in ('ELEMENT_ARRAY', 'INT_ARRAY'):
        return struct.pack('<i', len(v)) + b''.join(struct.pack('<i', x) for x in v)
    if kind == 'FLOAT_ARRAY':
        return struct.pack('<i', len(v)) + b''.join(struct.pack('<f', x) for x in v)
    raise ValueError(kind)


def build() -> bytes:
    elements = []  # [type, name, [(attr name, kind, value)]]; ELEMENT values are element indices

    def element(type_, name, attrs):
        elements.append([type_, name, attrs])
        return len(elements) - 1

    root = element('DmElement', 'root', [])
    timeframe = element('DmeTimeFrame', 'timeframe', [('start', 'FLOAT', 1.25), ('duration', 'FLOAT', 3.5),
                                                      ('offset', 'FLOAT', -0.5), ('scale', 'FLOAT', 1.0)])
    # importsfmv3 moves every edge attribute into the curve info and needs all of them when usecurvetypes is set
    edge = [('defaultcurvetype', 'INT', 1), ('defaultedgezerovalue', 'FLOAT', 0.25), ('useedgeinfo', 'BOOL', 1),
            ('rightedgetime', 'INT', 500), ('left_edge_active', 'BOOL', 1), ('right_edge_active', 'BOOL', 0),
            ('left_edge_curvetype', 'INT', 2), ('right_edge_curvetype', 'INT', 3),
            ('left_edge_value', 'FLOAT', 0.5), ('right_edge_value', 'FLOAT', 0.75)]
    float_log = element('DmeFloatLog', 'floatlog',
                        [('times', 'INT_ARRAY', [0, 100, 250]), ('values', 'FLOAT_ARRAY', [0.0, 0.5, 1.0]),
                         ('curvetypes', 'INT_ARRAY', [1, 2, 3]), ('usecurvetypes', 'BOOL', 1)] + edge)
    int_log = element('DmeIntLog', 'intlog', [('times', 'INT_ARRAY', [0, 40]), ('values', 'INT_ARRAY', [3, 7]),
                                              ('usecurvetypes', 'BOOL', 0)])
    camera = element('DmeCamera', 'camera', [('toneMapScale', 'FLOAT', 2.5), ('fieldOfView', 'FLOAT', 45.0)])
    spot = element('DmeSpotLight', 'spot', [('direction', 'VECTOR3', (0.0, 0.0, -1.0)),
                                            ('color', 'VECTOR4', (255.0, 128.0, 300.0, 1.0))])
    point = element('DmePointLight', 'point', [('color', 'VECTOR4', (10.0, 20.0, 30.0, 255.0))])
    preset = element('DmElement', 'preset1', [('controlValues', 'ELEMENT_ARRAY', [])])
    control1 = element('DmElement', 'ctrl1', [('transform', 'BOOL', 0), ('midpoint', 'FLOAT', 0.3),
                                              ('value', 'FLOAT', 0.1)])
    control2 = element('DmElement', 'ctrl2', [('transform', 'BOOL', 1), ('midpoint', 'FLOAT', 0.6)])
    animset = element('DmeAnimationSet', 'animset', [('presets', 'ELEMENT_ARRAY', [preset]),
                                                     ('controls', 'ELEMENT_ARRAY', [control1, control2])])
    elements[root][2] = [('timeframe', 'ELEMENT', timeframe), ('logs', 'ELEMENT_ARRAY', [float_log, int_log]),
                         ('camera', 'ELEMENT', camera), ('lights', 'ELEMENT_ARRAY', [spot, point]),
                         ('animset', 'ELEMENT', animset), ('label', 'STRING', 'hello')]

    # Binary encoding 0: no string table; elements (type, name, 16-byte id), then each element's attributes
    out = bytearray(b'<!-- DMXVersion sfm_v1 -->\n\0')
    out += struct.pack('<i', len(elements))
    for i, (type_, name, _) in enumerate(elements):
        out += cstr(type_) + cstr(name) + uuid.UUID(int=i + 1).bytes_le
    for _, _, attrs in elements:
        out += struct.pack('<i', len(attrs))
        for name, kind, value in attrs:
            out += cstr(name) + struct.pack('<B', AT[kind]) + pack_value(kind, value)
    return bytes(out)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], 'wb') as f:
        f.write(build())
