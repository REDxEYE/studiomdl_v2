#!/usr/bin/env bash
# Runs a Python tool from tests/tools inside tests/tools/.venv (libclang bindings), creating the venv on first use.
# usage: tests/tools/run_py.sh <script.py> [args...]
set -euo pipefail
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$TOOLS/.venv"
if [[ ! -x "$VENV/bin/python" ]]; then
    python3 -m venv "$VENV"
    # Python bindings must not be newer than the system libclang; PyPI 'clang' tops out below current Fedora's.
    major="$(clang --version | head -1 | grep -oE '[0-9]+' | head -1)"
    "$VENV/bin/pip" install -q "clang==${major}.*" 2>/dev/null || "$VENV/bin/pip" install -q "clang<${major}.99"
fi
script="$1"
shift
[[ -f "$script" ]] || script="$TOOLS/$script"
exec "$VENV/bin/python" "$script" "$@"
