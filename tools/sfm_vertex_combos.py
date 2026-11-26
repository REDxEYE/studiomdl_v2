#!/usr/bin/env python3
"""Extract slot weights from SFM's generic VS30 selectors in the live stdshader IDB."""
import argparse
import ast
import json
import re
import struct
from pathlib import Path

from ida_mcp import upstream


def linear(node):
    """Return constant and slot coefficients, rejecting non-affine decompiler output."""
    if isinstance(node, ast.Constant) and type(node.value) is int:
        return node.value, {}
    if isinstance(node, ast.Name) and re.fullmatch(r'slot\d+', node.id):
        return 0, {int(node.id[4:]): 1}
    if isinstance(node, ast.BinOp):
        a, left = linear(node.left)
        b, right = linear(node.right)
        if isinstance(node.op, ast.Add):
            return a + b, {i: left.get(i, 0) + right.get(i, 0) for i in left.keys() | right.keys()}
        if isinstance(node.op, ast.Mult) and not (left and right):
            return a * b, {i: x * (b if left else a) for i, x in (left or right).items()}
    raise ValueError('IDA selector is not a straight-line affine expression')


def extract(port, addr, stride, names):
    result = upstream(port, 'tools/call', {'name': 'decompile', 'arguments': {'addr': hex(addr), 'include_addresses': False}})
    code = result.get('structuredContent', {}).get('code', '')
    returns = re.findall(r'\breturn\s+([^;]+);', code)
    if len(returns) != 1:
        raise ValueError(f'{addr:#x}: expected exactly one return expression')
    expr = re.sub(r'\*\(\s*this\s*\+\s*(\d+)\s*\)', r'slot\1', returns[0])
    expr = re.sub(r'\*this\b', 'slot0', expr)
    bias, weights = linear(ast.parse(' '.join(expr.split()), mode='eval').body)
    if bias or not weights or tuple(sorted(weights)) != tuple(range(len(names))) or any(w % stride for w in weights.values()):
        raise ValueError(f'{addr:#x}: selector does not match the known SFM slot count and VCS stride')
    return {'address': hex(addr), 'stride': stride, 'slots': [
        {'slot': i, 'flag': flag, 'weight': weights[i] // stride} for i, flag in enumerate(names)
    ]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('vcs', type=Path, help='SFM vertexlit_and_unlit_generic_vs30.vcs')
    p.add_argument('--port', type=int, default=13338)
    args = p.parse_args()
    with args.vcs.open('rb') as f:
        version, total, dynamic = struct.unpack('<III', f.read(12))
    if args.vcs.name != 'vertexlit_and_unlit_generic_vs30.vcs' or (version, total, dynamic) != (6, 6144 * 96, 96):
        p.error('expected the SFM generic VS30 archive matching these selectors')
    health = upstream(args.port, 'tools/call', {'name': 'server_health', 'arguments': {}})['structuredContent']
    if health['module'].lower() != 'stdshader_dx9.dll':
        p.error('IDA port must serve SFM stdshader_dx9.dll')
    base = int(health['imagebase'], 16)
    # Slot labels verified from the draw helper's assignments and debug strings; null means unverified.
    static_names = ('VERTEXCOLOR', 'CUBEMAP', 'HALFLAMBERT', 'FLASHLIGHT', 'SEAMLESS_BASE', 'SEAMLESS_DETAIL',
                    'SEPARATE_DETAIL_UVS', 'DECAL', 'STATICLIGHT3', 'LIGHTING_PREVIEW', 'TREESWAY', None, None)
    dynamic_names = ('COMPRESSED_VERTS', 'DYNAMIC_LIGHT', 'SKINNING', None, 'TESSELLATION', None)
    print(json.dumps({'shader': args.vcs.stem,
                      'static': extract(args.port, base + 0x540E0, dynamic, static_names),
                      'dynamic': extract(args.port, base + 0x54190, 1, dynamic_names)}, indent=2))


if __name__ == '__main__':
    main()
