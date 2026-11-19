"""Copies a decompiled model folder and makes its QC compile with studiomdl_v2 on any platform.

usage: prepare_qc.py <src dir> <dst dir> <qc name> <modelname> [--no-lod]

- backslashes inside quoted strings become '/' (v2 can't open "anims\\x.smd" on Linux)
- $collisionjoints / $collisionmodel blocks are removed (unsupported by v2)
- $modelname is replaced, so outputs never overwrite game models
- --no-lod also removes $lod / $shadowlod blocks
"""
import re
import shutil
import sys


def strip_blocks(text, command):
    """Removes every `$command ...` line and the { } block that follows it."""
    out = []
    i = 0
    pattern = re.compile(r'^\s*\$' + command + r'\b[^\n]*\n', re.M | re.I)
    while True:
        m = pattern.search(text, i)
        if not m:
            out.append(text[i:])
            return ''.join(out)
        out.append(text[i:m.start()])
        j = m.end()
        k = text.find('{', j)
        if k != -1 and text[j:k].strip() == '':
            depth = 0
            for t in range(k, len(text)):
                if text[t] == '{':
                    depth += 1
                elif text[t] == '}':
                    depth -= 1
                    if depth == 0:
                        j = t + 1
                        break
        i = j


def main():
    src, dst, qc, modelname = sys.argv[1:5]
    no_lod = '--no-lod' in sys.argv[5:]
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    path = f'{dst}/{qc}'
    text = open(path, encoding='latin-1').read()
    text = re.sub(r'"[^"\n]*"', lambda m: m.group(0).replace('\\', '/'), text)
    for command in ('collisionjoints', 'collisionmodel') + (('lod', 'shadowlod') if no_lod else ()):
        text = strip_blocks(text, command)
    text, n = re.subn(r'^\s*\$modelname\s+"[^"]*"', f'$modelname "{modelname}"', text, flags=re.M | re.I)
    if n != 1:
        sys.exit(f'{path}: expected one $modelname, found {n}')
    open(path, 'w', encoding='latin-1').write(text)


main()
