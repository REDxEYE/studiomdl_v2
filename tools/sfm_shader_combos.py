#!/usr/bin/env python3
"""Extract SFM stdshader_dx9 static combo layouts (slot names, weights, ranges) from the live IDB.

SFM's stdshader keeps a debug printf after every combo setter:
    Src[14] = bIsDecal; if (byte_100DB961) printf("\\n   VS stat var %s = %d (%s)", "DECAL", ..., "bIsDecal");
and passes the combo struct to a generated GetIndex() before naming the shader:
    v106 = sub_100540E0(&Src[7]); SetVertexShader(api, "vertexlit_and_unlit_generic_vs30", v106);
The decompiled draw function gives each setter's stack address, the struct base and the shader name;
GetIndex's affine expression gives each slot's weight. Ranges follow from consecutive weights and the
VCS header's static count.
"""
import argparse
import ast
import json
import re
import struct
import tempfile
from pathlib import Path

from ida_mcp import upstream
from sfm_vertex_combos import linear

DRAW_FUNCTIONS = {'vertexlit_and_unlit_generic': 0x10055320, 'phong': 0x100380F0}  # offsets from the SFM DLL base

# Combos set by the generated static-index constructors (no debug printf), e.g. 0x100541C0:
#   this[22] = !( $flags & MATERIAL_VAR_VERTEXFOG ); 0x10054310: this[6] = dxlevel >= 92 && !VERTEXFOG, this[7] = 0.
# Names follow Source's fxc convention for these init expressions; they are inferred, not printed by the DLL.
AUTO_INIT = {
    ('vertexlit_and_unlit_generic_vs30', 11): ('DOPIXELFOG', 'dxlevel >= 92 && !IS_FLAG_SET( MATERIAL_VAR_VERTEXFOG )'),
    ('vertexlit_and_unlit_generic_vs30', 12): ('HARDWAREFOGBLEND', '0'),
    ('vertexlit_and_unlit_generic_ps30', 22): ('DOPIXELFOG', '!IS_FLAG_SET( MATERIAL_VAR_VERTEXFOG )'),
    ('vertexlit_and_unlit_generic_bump_ps30', 14): ('DOPIXELFOG', '!IS_FLAG_SET( MATERIAL_VAR_VERTEXFOG )'),
}


def decompile_to_text(port, ea):
    with tempfile.NamedTemporaryFile(suffix='.c', delete=False) as out:
        path = out.name
    code = f'import ida_hexrays\nopen({path!r}, "w").write(str(ida_hexrays.decompile({ea})))'
    result = upstream(port, 'tools/call', {'name': 'py_eval', 'arguments': {'code': code}})
    if result.get('structuredContent', {}).get('stderr'):
        raise RuntimeError(result['structuredContent']['stderr'])
    text = Path(path).read_text()
    Path(path).unlink()
    return text


def stack_offsets(text):
    offsets = {}
    for name, size, esp in re.findall(r'\b([A-Za-z_]\w*)(\[\d+\])?; // \[esp\+([0-9A-F]+)h\]', text):
        offsets[name] = int(esp, 16)
    return offsets


def lvalue_address(lhs, offsets):
    lhs = lhs.strip()
    m = re.fullmatch(r'(?:\*\([^)]*\)\s*&)?(\w+)(?:\[(\d+)\])?', lhs)
    if not m or m.group(1) not in offsets:
        return None
    return offsets[m.group(1)] + 4 * int(m.group(2) or 0)


def statements(text):
    """Yield top-level-ish statements: join lines until ';' while tracking the debug if blocks."""
    buffer = ''
    for line in text.splitlines():
        stripped = line.strip()
        if stripped in ('{', '}') or not stripped:
            continue
        buffer += ' ' + stripped
        if buffer.rstrip().endswith((';', ')')) and buffer.count('(') == buffer.count(')'):
            yield buffer.strip()
            buffer = ''


def extract_segments(text):
    offsets = stack_offsets(text)
    candidate = None
    setters = []
    segments = []
    pending_index = None
    for stmt in statements(text):
        if stmt.startswith('if ( byte_100DB961 )'):
            stmt = stmt[len('if ( byte_100DB961 )'):].strip()
            if not stmt:
                continue
        printed = re.search(r'printf\(\s*"\\n   (VS|PS) (stat|dyn ) var %s = %d \(%s\)",\s*"(\w+)",.*,\s*"((?:[^"\\]|\\.)*)"\s*\)', stmt)
        if printed:
            if printed.group(2) == 'stat':
                setters.append((candidate, printed.group(1), printed.group(3), printed.group(4)))
            candidate = None
            continue
        call = re.search(r'=\s*(sub_[0-9A-F]+)\(&(\w+(?:\[\d+\])?)\);', stmt)
        if call:
            pending_index = (int(call.group(1)[4:], 16), lvalue_address(call.group(2), offsets), setters)
            setters = []
            continue
        shader = re.search(r'"(\w+_(?:vs|ps)(?:20b?|30))"', stmt)
        if shader and pending_index:
            index_fn, base, stage_setters = pending_index
            segments.append({'shader': shader.group(1), 'get_index': index_fn, 'base': base, 'setters': stage_setters})
            pending_index = None
            continue
        assign = re.fullmatch(r'(.+?)\s*=\s*(.+);', stmt)
        if assign and '==' not in assign.group(1):
            address = lvalue_address(assign.group(1), offsets)
            if address is not None:
                candidate = address
    return segments


def weights(port, ea):
    code = upstream(port, 'tools/call', {'name': 'decompile', 'arguments': {'addr': hex(ea), 'include_addresses': False}})
    code = code.get('structuredContent', {}).get('code', '')
    returns = re.findall(r'\breturn\s+([^;]+);', code)
    if len(returns) != 1:
        raise ValueError(f'{ea:#x}: expected one return expression')
    expr = re.sub(r'\*\(\s*this\s*\+\s*(\d+)\s*\)', r'slot\1', returns[0])
    expr = re.sub(r'\*this\b', 'slot0', expr)
    bias, slot_weights = linear(ast.parse(' '.join(expr.split()), mode='eval').body)
    if bias:
        raise ValueError(f'{ea:#x}: unexpected bias')
    return slot_weights


def vcs_static_count(path):
    with path.open('rb') as f:
        version, total, dynamic = struct.unpack('<iii', f.read(12))
    return total // dynamic, dynamic


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('shaders', type=Path, help='SFM game/platform/shaders/fxc directory')
    p.add_argument('--port', type=int, default=13338)
    p.add_argument('--cpp', action='store_true', help='emit the C++ table for sfm_gl_probe/sfm_shader_combos.inc')
    args = p.parse_args()
    health = upstream(args.port, 'tools/call', {'name': 'server_health', 'arguments': {}})['structuredContent']
    if health['module'].lower() != 'stdshader_dx9.dll':
        p.error('IDA port must serve SFM stdshader_dx9.dll')
    base = int(health['imagebase'], 16)
    layouts = {}
    for draw, rva in DRAW_FUNCTIONS.items():
        text = decompile_to_text(args.port, base - 0x10000000 + rva)
        for seg in extract_segments(text):
            if not seg['shader'].endswith('30') or seg['shader'] in layouts or seg['base'] is None:
                continue
            slot_weights = weights(args.port, seg['get_index'])
            static_count, dynamic_count = vcs_static_count(args.shaders / f"{seg['shader']}.vcs")
            stride = min(slot_weights.values())  # GetIndex scales statics by the dynamic count
            if stride != dynamic_count:
                raise ValueError(f"{seg['shader']}: GetIndex stride {stride} != VCS dynamic count {dynamic_count}")
            names = {}
            for address, stage, name, expr in seg['setters']:
                if address is not None and address >= seg['base']:
                    names[(address - seg['base']) // 4] = (name, expr)
            order = sorted(slot_weights, key=lambda s: (slot_weights[s], s))
            slots = []
            for i, slot in enumerate(order):
                weight = slot_weights[slot] // stride
                following = slot_weights[order[i + 1]] // stride if i + 1 < len(order) else static_count
                name, expr = names.get(slot, AUTO_INIT.get((seg['shader'], slot), (None, None)))
                slots.append({'slot': slot, 'name': name, 'expr': expr, 'weight': weight, 'count': following // weight})
            layouts[seg['shader']] = {'static_count': static_count, 'dynamic_count': dynamic_count,
                                      'get_index': hex(seg['get_index']), 'slots': slots}
    if args.cpp:
        print('// Generated by tools/sfm_shader_combos.py --cpp from SFM stdshader_dx9.dll; do not edit.')
        print('// Rows: combo name (nullptr when the DLL does not print it), weight, value count.')
        for shader, layout in sorted(layouts.items()):
            print(f'constexpr ComboSlot k_{shader}_slots[] = {{')
            for slot in layout['slots']:
                name = f'"{slot["name"]}"' if slot['name'] else 'nullptr'
                print(f'    {{{name}, {slot["weight"]}, {slot["count"]}}},')
            print('};')
        print('constexpr ComboLayout k_extractedLayouts[] = {')
        for shader, layout in sorted(layouts.items()):
            print(f'    {{"{shader}", {layout["static_count"]}, {layout["dynamic_count"]}, k_{shader}_slots}},')
        print('};')
        return
    print(json.dumps(layouts, indent=1))


if __name__ == '__main__':
    main()
