#!/usr/bin/env bash
# Syntax-only compile of one translation unit with its flags from compile_commands.json; prints errors only.
# usage: tests/tools/compile_one.sh <repo-relative .cpp> [max error lines] [build dir]
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
file="$1"
max="${2:-40}"
build="${3:-${BUILD_DIR:-/tmp/v2build}}"
python3 - "$REPO" "$file" "$build" "$max" <<'EOF'
import json, os, shlex, subprocess, sys
repo, rel, build, maxl = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
path = os.path.join(repo, rel)
db = {os.path.realpath(e['file']): e for e in json.load(open(os.path.join(build, 'compile_commands.json')))}
e = db.get(os.path.realpath(path))
if e is None:
    sys.exit(f'{rel}: not in {build}/compile_commands.json')
args, out, skip = shlex.split(e['command']), [], False
for a in args:
    if skip:
        skip = False
        continue
    if a in ('-o', '-MF', '-MT', '-MQ'):
        skip = True
        continue
    out.append(a)
r = subprocess.run(out + ['-fsyntax-only'], cwd=e['directory'], capture_output=True, text=True)
lines = [l.replace(repo + '/', '') for l in r.stderr.splitlines() if 'error' in l or 'required from' in l]
print('\n'.join(lines[:maxl]))
sys.exit(1 if r.returncode else 0)
EOF
