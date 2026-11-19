#!/usr/bin/env python3
"""Syntax-only pass over every compile_commands.json entry with -Wformat, which the build doesn't enable (no -Wall).
Catches std::string_view / std::string passed to printf-style varargs (CUtlBuffer::Printf, Log_*, V_snprintf, sscanf):
those compile silently and crash at runtime. Functions without FMTFUNCTION (ReportError, CUtlBuffer::Scanf,
FileSystemWarning, CmdLib_FPrintf) are not covered.

usage: tests/tools/check_format.py [build dir (default /tmp/v2build)] [--all]
Prints each distinct warning once. Without --all, the known LP64 `%lld`/`%llu` on int64_t/uint64_t noise is hidden.
Exit code 1 when anything is printed.
"""
import concurrent.futures as cf
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FLAGS = ['-fsyntax-only', '-Wformat', '-Wno-format-zero-length', '-Wno-format-security', '-Wno-format-truncation',
         '-Wno-format-overflow']
# long vs long long: same size on LP64, harmless
LP64_NOISE = re.compile(r"‘%ll[duxX]’ expects argument of type ‘long long (unsigned )?int\*?’, but argument \d+ has "
                        r"type ‘(u?int64_t|long (unsigned )?int)\*?’")


def syntax_check(entry):
    args = shlex.split(entry['command']) if 'command' in entry else list(entry['arguments'])
    cmd, skip = [], False
    for a in args:
        if skip:
            skip = False
        elif a == '-o':
            skip = True
        elif a != '-c':
            cmd.append(a)
    r = subprocess.run(cmd + FLAGS, capture_output=True, text=True, cwd=entry['directory'])
    return r.stderr


def main():
    args = [a for a in sys.argv[1:] if a != '--all']
    show_all = '--all' in sys.argv[1:]
    build = Path(args[0] if args else os.environ.get('BUILD_DIR', '/tmp/v2build'))
    entries = json.loads((build / 'compile_commands.json').read_text())
    with cf.ThreadPoolExecutor(os.cpu_count()) as pool:
        outputs = list(pool.map(syntax_check, entries))

    warnings = set()
    for out in outputs:
        for line in out.splitlines():
            if 'warning:' not in line or 'format' not in line:
                continue
            if not show_all and LP64_NOISE.search(line):
                continue
            # one entry per source location, independent of the include path it was reached through
            line = re.sub(r'^.*?((?:include|libs|studiomdl|dmxconvert|tests)/(?!.*\.\./))', r'\1',
                          line.replace(str(REPO) + '/', ''))
            warnings.add(re.sub(r'(\w+/)+\.\./\.\./', '', line))
    for w in sorted(warnings):
        print(w)
    print(f'{len(warnings)} format warning(s) in {len(entries)} translation units', file=sys.stderr)
    sys.exit(1 if warnings else 0)


if __name__ == '__main__':
    main()
