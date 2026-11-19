#!/usr/bin/env python3
"""Lists the DMX format updaters compiled into SFM's dmxconvert.exe (32-bit PE): for each CDmFormatUpdater_<name>
class (found through MSVC RTTI) the current format version and the address of its update function table, then the
tables themselves, marking functions that are not in the `dmx` (MovieObjects) table. Use it to see which formats/versions
SFM knows that we don't (e.g. sfm_session 20 = MovieObjects list + Update16 + Update20), then disassemble an
unknown function with --dis.

usage: tests/tools/sfm_format_updaters.py [--exe PATH] [--dis 0xADDR [BYTES]]
Needs objdump (binutils) for --dis and for reading GetCurrentVersion/Update.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

DEFAULT_EXE = Path(os.environ.get('SFM_DIR', '/mnt/games/SteamLibrary/steamapps/common/SourceFilmmaker')) \
              / 'game/bin/dmxconvert.exe'


class PE:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        nsec, = struct.unpack_from('<H', self.data, pe + 6)
        optsz, = struct.unpack_from('<H', self.data, pe + 20)
        self.base, = struct.unpack_from('<I', self.data, pe + 24 + 28)
        self.sections = []
        off = pe + 24 + optsz
        for _ in range(nsec):
            name = self.data[off:off + 8].rstrip(b'\0').decode()
            vsize, va, rsize, raw = struct.unpack_from('<IIII', self.data, off + 8)
            self.sections.append((name, va, vsize, raw, rsize))
            off += 40

    def off_to_va(self, o):
        for _, va, _, raw, rsize in self.sections:
            if raw <= o < raw + rsize:
                return self.base + va + (o - raw)
        return None

    def va_to_off(self, v):
        r = v - self.base
        for _, va, vsize, raw, rsize in self.sections:
            if va <= r < va + max(vsize, rsize):
                return raw + (r - va)
        return None

    def u32(self, va):
        o = self.va_to_off(va)
        # zero-initialized data (virtual size beyond the raw bytes) isn't stored in the file
        return struct.unpack_from('<I', self.data, o)[0] if o is not None and o + 4 <= len(self.data) else 0

    def find_all(self, needle: bytes):
        i, out = -1, []
        while (i := self.data.find(needle, i + 1)) >= 0:
            out.append(i)
        return out

    def string_at(self, va):
        o = self.va_to_off(va)
        if o is None:
            return None
        s = self.data[o:o + 64].split(b'\0')[0]
        return s.decode() if s and all(32 <= c < 127 for c in s) else None

    def disassemble(self, va, size):
        r = subprocess.run(['objdump', '-d', '-M', 'intel', '--start-address', hex(va), '--stop-address',
                            hex(va + size), str(self.path)], capture_output=True, text=True, check=True)
        return [l for l in r.stdout.splitlines() if re.match(r'\s+[0-9a-f]+:', l)]

    def vtable_of(self, class_name):
        """MSVC RTTI: TypeDescriptor name -> CompleteObjectLocator -> vtable (the slot after the COL pointer)"""
        o = self.data.find(f'.?AV{class_name}@@'.encode())
        if o < 0:
            return None
        type_descriptor = self.off_to_va(o - 8)
        cols = [x - 12 for x in self.find_all(struct.pack('<I', type_descriptor))
                if struct.unpack_from('<I', self.data, x - 12)[0] == 0]
        if not cols:
            return None
        col = self.off_to_va(cols[0])
        refs = self.find_all(struct.pack('<I', col))
        return self.off_to_va(refs[0]) + 4 if refs else None


def updater_info(pe: PE, vtable):
    # IDmFormatUpdater: GetName, GetDescription, GetExtension, GetCurrentVersion, GetDefaultEncoding, Update
    get_version, update = pe.u32(vtable + 12), pe.u32(vtable + 20)
    first = pe.disassemble(get_version, 8)[0]
    m = re.search(r'mov\s+eax,0x([0-9a-f]+)', first)
    version = int(m.group(1), 16) if m else None
    table = None
    for line in pe.disassemble(update, 0x60):
        m = re.search(r'\[esi\*4\+0x([0-9a-f]+)\]', line)
        if m:
            table = int(m.group(1), 16)
            break
    return version, table


def read_table(pe: PE, table, version):
    return [pe.u32(table + 4 * i) for i in range(version or 0)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    ap.add_argument('--dis', nargs='+', metavar=('ADDR', 'BYTES'), help='disassemble ADDR (hex) and list strings')
    args = ap.parse_args()
    pe = PE(args.exe)

    if args.dis:
        va = int(args.dis[0], 16)
        size = int(args.dis[1], 0) if len(args.dis) > 1 else 0x100
        lines = pe.disassemble(va, size)
        print('\n'.join(lines))
        strings = {m: s for line in lines for m in re.findall(r'0x4[0-9a-f]{5}', line)
                   if (s := pe.string_at(int(m, 16)))}
        for addr, s in sorted(strings.items()):
            print(f'{addr}: "{s}"')
        return

    names = sorted({m.decode() for m in re.findall(rb'\.\?AVCDmFormatUpdater_(\w+)@@', pe.data)})
    infos = {}
    for name in names:
        vt = pe.vtable_of(f'CDmFormatUpdater_{name}')
        if vt is None:
            continue
        version, table = updater_info(pe, vt)
        infos[name] = (version, table)
        print(f'{name:20} version {version:>3}  table {hex(table) if table else "-"}')

    if 'dmx' not in infos:
        sys.exit('no dmx updater found')
    reference = set(read_table(pe, infos['dmx'][1], infos['dmx'][0]))
    for name, (version, table) in sorted(infos.items(), key=lambda kv: kv[1][1] or 0):
        if not table:
            continue
        funcs = read_table(pe, table, version)
        extra = [f'{i}:{hex(f)}' for i, f in enumerate(funcs) if f and f not in reference]
        print(f'{name:20} {len([f for f in funcs if f])} functions'
              + (f', not in dmx: {", ".join(extra)}' if extra and name != 'dmx' else ''))


if __name__ == '__main__':
    main()
