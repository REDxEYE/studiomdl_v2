#!/usr/bin/env python3
"""Compare our dmxconvert against Source Filmmaker's dmxconvert.exe (run under Wine).

Every case converts one input with both tools to keyvalues2 text (or exports to a non-DMX encoding) and compares the
outputs. Element ids that both tools create fresh are random, so every UUID is renumbered in order of first appearance
(ids read from the input keep their position, references stay linked). Outputs that differ only in how a float was
rounded to text (glibc rounds exact ties to even, the MSVC CRT away from zero) are reported as OK_FLOAT; outputs whose
numbers differ by at most MAX_ULPS float32 ulps (MSVC vs glibc trig, e.g. importsfmv6 DirectionToOrientation) as OK_ULP.
Generated inputs: a legacy sfm_v1 file (make_legacy_sfm_dmx.py) and SFM sessions relabelled "format dmx 18".
Binary output is checked by cross-reading: SFM converts our binary file and its own binary file to keyvalues2, and the
two results must match.

usage: tests/tools/compare_sfm_dmxconvert.py [--new BIN] [--sfm SFM_ROOT] [--work DIR] [--jobs N] [--keep] [--filter S]
"""
import argparse
import concurrent.futures as cf
import difflib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_legacy_sfm_dmx  # noqa: E402

UUID_RE = re.compile(r'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}')


def wine_path(p: Path) -> str:
    return 'Z:' + str(p.resolve()).replace('/', '\\')


def normalize(text: str) -> str:
    ids = {}
    text = UUID_RE.sub(lambda m: ids.setdefault(m.group(0).lower(), f'<id{len(ids)}>'), text)
    return text.replace('\r\n', '\n')


NUMBER_RE = re.compile(r'-?\d+\.\d+(?:[eE][-+]?\d+)?')


def float32_text(text: str) -> str:
    """Numbers rounded to the float32 they denote: equal after this means only the text rounding differed"""
    return NUMBER_RE.sub(lambda m: repr(struct.unpack('f', struct.pack('f', float(m.group(0))))[0]), text)


MAX_ULPS = 4


def float32_ordinal(x: float) -> int:
    """Monotonic integer for a float32, so ulp distance is a subtraction"""
    i, = struct.unpack('<i', struct.pack('<f', x))
    return i if i >= 0 else -(i & 0x7fffffff)


def within_ulps(a: str, b: str) -> bool:
    if NUMBER_RE.split(a) != NUMBER_RE.split(b):
        return False
    na, nb = NUMBER_RE.findall(a), NUMBER_RE.findall(b)
    return all(abs(float32_ordinal(float(x)) - float32_ordinal(float(y))) <= MAX_ULPS for x, y in zip(na, nb))


def compare(a: str, b: str, name_a: str, name_b: str):
    if a == b:
        return 'OK', ''
    if float32_text(a) == float32_text(b):
        return 'OK_FLOAT', ''
    if within_ulps(a, b):
        return 'OK_ULP', ''
    diff = list(difflib.unified_diff(a.splitlines(), b.splitlines(), name_a, name_b, lineterm='', n=1))
    return None, f'{sum(1 for l in diff if l[:1] in "+-") - 2} changed lines\n' + '\n'.join(diff[:60])


class Runner:
    def __init__(self, new_bin: Path, sfm_root: Path, work: Path):
        self.new_bin = new_bin
        self.sfm_bin = sfm_root / 'game' / 'bin'
        self.work = work
        self.env = dict(os.environ)
        self.env.setdefault('WINEPREFIX', str(Path.home() / '.cache/sourceio-sfm/prefix'))
        self.env['WINEDEBUG'] = '-all'

    def ours(self, args):
        r = subprocess.run([str(self.new_bin)] + args, capture_output=True, text=True, timeout=300)
        return r.returncode, r.stdout + r.stderr

    def sfm(self, args):
        r = subprocess.run(['wine', 'dmxconvert.exe'] + args, capture_output=True, text=True, timeout=600,
                           cwd=self.sfm_bin, env=self.env)
        log = '\n'.join(l for l in (r.stdout + r.stderr).splitlines()
                        if not l.startswith(('MESA', 'pci id')))
        return r.returncode, log


def run_case(runner: Runner, idx: int, src: Path, in_enc, out_enc: str):
    """Returns (status, detail). status in OK / OK_FLOAT / OK_ULP / DIFF / FAIL_OURS / FAIL_SFM / BOTH_FAIL / BINARY_DIFF"""
    d = runner.work / f'{idx:03d}'
    d.mkdir(parents=True, exist_ok=True)
    ext = {'vmt': '.vmt', 'vmf': '.vmf', 'keyvalues': '.txt', 'actbusy': '.txt'}.get(out_enc, '.dmx')
    ours_out, sfm_out = d / f'ours{ext}', d / f'sfm{ext}'
    hint = ['-ie', in_enc] if in_enc else []
    rc_o, log_o = runner.ours(['-i', str(src)] + hint + ['-o', str(ours_out), '-oe', out_enc])
    rc_s, log_s = runner.sfm(['-i', wine_path(src)] + hint + ['-o', wine_path(sfm_out), '-oe', out_enc])
    ok_o, ok_s = ours_out.exists() and rc_o == 0, sfm_out.exists() and rc_s == 0
    if not ok_o and not ok_s:
        return 'BOTH_FAIL', f'ours rc={rc_o} sfm rc={rc_s}'
    if not ok_o:
        return 'FAIL_OURS', f'rc={rc_o}\n{log_o[-800:]}'
    if not ok_s:
        return 'FAIL_SFM', f'rc={rc_s}\n{log_s[-800:]}'
    a = normalize(sfm_out.read_text(errors='replace'))
    b = normalize(ours_out.read_text(errors='replace'))
    status, detail = compare(a, b, 'sfm', 'ours')
    if status is None:
        return 'DIFF', detail

    # Binary: SFM reads our binary file and its own; both must give the same tree
    if out_enc == 'keyvalues2':
        ours_bin, sfm_bin = d / 'ours_bin.dmx', d / 'sfm_bin.dmx'
        ours_cross, sfm_cross = d / 'sfm_from_ours_bin.dmx', d / 'sfm_from_sfm_bin.dmx'
        rc, log = runner.ours(['-i', str(src)] + hint + ['-o', str(ours_bin), '-oe', 'binary'])
        if rc != 0 or not ours_bin.exists():
            return 'FAIL_OURS', f'binary write rc={rc}\n{log[-800:]}'
        rc, log = runner.sfm(['-i', wine_path(src)] + hint + ['-o', wine_path(sfm_bin), '-oe', 'binary'])
        if rc != 0 or not sfm_bin.exists():
            return 'FAIL_SFM', f'binary write rc={rc}\n{log[-800:]}'
        for binary, cross in ((ours_bin, ours_cross), (sfm_bin, sfm_cross)):
            rc, log = runner.sfm(['-i', wine_path(binary), '-o', wine_path(cross), '-oe', 'keyvalues2'])
            if rc != 0 or not cross.exists():
                return 'BINARY_DIFF', f'SFM could not read {binary.name} rc={rc}\n{log[-800:]}'
        bin_status, bin_detail = compare(normalize(sfm_cross.read_text(errors='replace')),
                                         normalize(ours_cross.read_text(errors='replace')),
                                         'sfm(sfm binary)', 'sfm(our binary)')
        if bin_status is None:
            return 'BINARY_DIFF', bin_detail
    return status, detail


def collect_cases(sfm_root: Path, work: Path):
    game = sfm_root / 'game'
    cases = []
    for f in sorted((REPO / 'tests/golden/fixtures/src').rglob('*.dmx')):
        cases.append((f, None, 'keyvalues2'))
    sfm_dmx = sorted(p for p in sfm_root.rglob('*.dmx') if p.stat().st_size < 3_000_000)
    for f in sfm_dmx[:12]:
        cases.append((f, None, 'keyvalues2'))
    for f in sorted((game / 'hl2/particles').glob('*.pcf'))[:4]:
        cases.append((f, None, 'keyvalues2'))
    # VMTs: a spread that includes proxies, fallbacks, patch files and vectors
    vmts = sorted((game / 'hl2/materials').rglob('*.vmt'))
    picked = []
    for key in ('proxies', '_dx', 'patch', '[', '{', 'basetexturetransform'):
        picked += [v for v in vmts if key in v.read_text(errors='replace').lower()][:3]
    picked += vmts[::max(1, len(vmts) // 8)][:8]
    for f in dict.fromkeys(picked):
        cases.append((f, 'vmt', 'keyvalues2'))
        cases.append((f, 'vmt', 'vmt'))
    if (game / 'hl2/scripts/actbusy.txt').exists():
        cases.append((game / 'hl2/scripts/actbusy.txt', 'actbusy', 'keyvalues2'))
        cases.append((game / 'hl2/scripts/actbusy.txt', 'actbusy', 'actbusy'))
    for f in sorted((game / 'tf/maps').glob('*_commentary.txt'))[:2]:
        cases.append((f, 'commentary', 'keyvalues2'))

    # Legacy sfm_v1 binary: runs every importsfmv*.cpp updater (no real files that old exist)
    legacy = work / 'inputs/legacy_sfm_v1.dmx'
    legacy.parent.mkdir(parents=True, exist_ok=True)
    legacy.write_bytes(make_legacy_sfm_dmx.build())
    cases.append((legacy, None, 'keyvalues2'))

    # SFM sessions are format sfm_session 20, which we don't install (SFM-only Update20). Their content is already at
    # the latest version, so a copy relabelled "dmx 18" (no updater runs) compares the serializers on real data.
    for session in sorted((game / 'usermod/elements/sessions').glob('*.dmx'))[:2]:
        data = session.read_bytes()
        header = b'format sfm_session 20'
        if header in data[:128]:
            copy = work / f'inputs/session_as_dmx_{session.name}'
            copy.write_bytes(data.replace(header, b'format dmx 18', 1))
            cases.append((copy, None, 'keyvalues2'))
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--new', type=Path, default=Path(os.environ.get('BUILD_DIR', '/tmp/v2build')) / 'dmxconvert')
    ap.add_argument('--sfm', type=Path,
                    default=Path(os.environ.get('SFM_DIR', '/mnt/games/SteamLibrary/steamapps/common/SourceFilmmaker')))
    ap.add_argument('--work', type=Path)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--keep', action='store_true', help='keep the work directory')
    ap.add_argument('--filter', default='', help='only inputs whose path contains this string')
    ap.add_argument('--extra', nargs=3, action='append', default=[], metavar=('FILE', 'IN_ENC|-', 'OUT_ENC'),
                    help='additional case, e.g. --extra my.vmf vmf keyvalues2')
    args = ap.parse_args()

    if not (args.sfm / 'game/bin/dmxconvert.exe').exists():
        sys.exit(f'no dmxconvert.exe under {args.sfm}/game/bin')
    work = args.work or Path(tempfile.mkdtemp(prefix='sfm_dmxconvert.'))
    runner = Runner(args.new, args.sfm, work)
    cases = collect_cases(args.sfm, work) + [(Path(f), None if e == '-' else e, o) for f, e, o in args.extra]
    cases = [c for c in cases if args.filter in str(c[0])]

    results = {}
    with cf.ThreadPoolExecutor(args.jobs) as pool:
        futs = {pool.submit(run_case, runner, i, *c): (i, c) for i, c in enumerate(cases)}
        for fut in cf.as_completed(futs):
            i, c = futs[fut]
            try:
                results[i] = fut.result()
            except Exception as e:  # timeouts and the like
                results[i] = ('ERROR', repr(e))

    counts = {}
    for i, (src, in_enc, out_enc) in enumerate(cases):
        status, detail = results[i]
        counts[status] = counts.get(status, 0) + 1
        label = f'[{i:03d}] {status:11} {in_enc or "dmx":10} -> {out_enc:10} {src}'
        print(label)
        if status not in ('OK', 'BOTH_FAIL') and detail:
            print('    ' + detail.replace('\n', '\n    '))
    print('summary:', ', '.join(f'{k} {v}' for k, v in sorted(counts.items())), f'(work dir {work})')
    if not args.keep and not args.work:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(0 if set(counts) <= {'OK', 'OK_FLOAT', 'OK_ULP', 'BOTH_FAIL'} else 1)


if __name__ == '__main__':
    main()
