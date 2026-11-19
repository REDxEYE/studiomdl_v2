"""Audit std::format logging calls (std::format migration checks).

Checks every call whose callee takes a std::format_string (Msg/Warning/Error/DevMsg/ConMsg/MdlError/...): leftover
printf specs, doubled braces, more arguments than replacement fields, bool printed as true/false, x/o on signed ints,
{} on float/double (printf %f printed 6 decimals). Also flags printf-style (C variadic) calls given {} syntax.
Only active code is seen; grep comments/#if 0 separately.

Usage: tests/tools/run_py.sh audit_format.py <build dir with compile_commands.json>
"""
import json, multiprocessing, os, re, shlex, subprocess, sys
import clang.cindex as ci

ci.Config.set_library_file(os.environ.get('LIBCLANG', '/usr/lib64/libclang.so'))
R = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
B = sys.argv[1] if len(sys.argv) > 1 else os.path.join(R, 'build')
SKIP = ('meshoptimizer', 'legacy_qhull', '/tests/')
RESOURCE_DIR = subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True).stdout.strip()

PRINTF = re.compile(r'%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|L|z|j|t|I64)?[diouxXeEfFgGaAcspn]')


def args_for(e):
    a = shlex.split(e['command'])[1:]
    out, skip = [], False
    for x in a:
        if skip:
            skip = False
            continue
        if x in ('-o', '-c', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if not x.startswith('-') and os.path.basename(x) == os.path.basename(e['file']):
            continue
        if x == '-Wconditionally-supported':
            continue
        out.append(x)
    return ['-resource-dir', RESOURCE_DIR] + out


def literal_text(cursor):
    """Concatenated value of the string literal tokens in the cursor's extent, or None if not a pure literal."""
    toks = [t for t in cursor.get_tokens()]
    if not toks:
        return None
    parts = []
    for t in toks:
        s = t.spelling
        if t.kind == ci.TokenKind.LITERAL and s.endswith('"'):
            body = s[s.index('"') + 1:-1]
            parts.append(body)
        elif t.kind == ci.TokenKind.PUNCTUATION and s in '()':
            continue
        else:
            return None
    return ''.join(parts)


def parse_placeholders(fmt):
    """Returns list of spec strings for each replacement field, or raises ValueError on bad braces."""
    specs, i = [], 0
    while i < len(fmt):
        c = fmt[i]
        if c == '{':
            if fmt.startswith('{{', i):
                i += 2
                continue
            j = fmt.find('}', i)
            if j < 0:
                raise ValueError('unclosed {')
            specs.append(fmt[i + 1:j])
            i = j + 1
        elif c == '}':
            if fmt.startswith('}}', i):
                i += 2
                continue
            raise ValueError('stray }')
        else:
            i += 1
    return specs


def is_format_callee(ref):
    try:
        params = list(ref.get_arguments())
    except Exception:
        return None
    for idx, p in enumerate(params):
        if 'basic_format_string' in p.type.spelling or 'format_string' in p.type.get_canonical().spelling:
            return idx
    return None


def check_tu(e):
    findings = []
    try:
        tu = ci.Index.create().parse(e['file'], args=args_for(e))
    except Exception as ex:
        return [('PARSEFAIL', e['file'], 0, str(ex))]

    def loc(c):
        return os.path.relpath(os.path.normpath(c.location.file.name), R), c.location.line

    def walk(c):
        for ch in c.get_children():
            f = ch.location.file
            if f is None or any(s in f.name for s in SKIP) or not os.path.normpath(f.name).startswith(R):
                walk(ch)
                continue
            if ch.kind == ci.CursorKind.CALL_EXPR and ch.referenced is not None:
                ref = ch.referenced
                args = list(ch.get_arguments())
                fi = is_format_callee(ref)
                if fi is not None and fi < len(args):
                    text = literal_text(args[fi])
                    if text is not None:
                        rest = args[fi + 1:]
                        p, l = loc(ch)
                        if PRINTF.search(text.replace('%%', '')):
                            findings.append(('PRINTF_SPEC', p, l, f'{ref.spelling}: "{text}"'))
                        if '{{}}' in text or '{{:' in text:
                            findings.append(('DOUBLE_ESCAPE', p, l, f'{ref.spelling}: "{text}"'))
                        try:
                            specs = parse_placeholders(text)
                        except ValueError as ex:
                            specs = None
                            findings.append(('BAD_BRACES', p, l, f'{ref.spelling}: {ex}: "{text}"'))
                        if specs is not None:
                            if len(rest) > len(specs):
                                findings.append(('EXTRA_ARGS', p, l, f'{ref.spelling}: {len(specs)} fields, {len(rest)} args: "{text}"'))
                            for spec, a in zip(specs, rest):
                                t = a.type.get_canonical().spelling
                                s = spec.split(':', 1)[1] if ':' in spec else ''
                                ty = s[-1:] if s[-1:].isalpha() else ''
                                if t == 'bool' and ty == '':
                                    findings.append(('BOOL_AS_TEXT', p, l, f'{ref.spelling}: "{{{spec}}}" bool prints true/false: "{text}"'))
                                if t in ('float', 'double') and ':' not in spec:
                                    findings.append(('FLOAT_SHORTEST', p, l, f'{ref.spelling}: "{{{spec}}}" on {t} (printf %f printed 6 decimals): "{text}"'))
                                if ty in ('x', 'X', 'o') and t in ('int', 'long', 'long long', 'short', 'signed char'):
                                    findings.append(('SIGNED_HEX', p, l, f'{ref.spelling}: "{{{spec}}}" on {t}: "{text}"'))
                                if ty == 'c' and t not in ('char', 'int', 'unsigned char', 'signed char', 'wchar_t', 'unsigned int'):
                                    findings.append(('CHAR_SPEC', p, l, f'{ref.spelling}: {{:c}} on {t}'))
                elif ref.type.kind == ci.TypeKind.FUNCTIONPROTO and ref.type.is_function_variadic():
                    for a in args:
                        text = literal_text(a)
                        if text and re.search(r'\{(:[^}]*)?\}', text) and not PRINTF.search(text.replace('%%', '')):
                            p, l = loc(ch)
                            findings.append(('FORMAT_IN_PRINTF', p, l, f'{ref.spelling}: "{text}"'))
                            break
            walk(ch)

    walk(tu.cursor)
    return findings


if __name__ == '__main__':
    cc = json.load(open(os.path.join(B, 'compile_commands.json')))
    todo = [e for e in cc if not any(s in e['file'] for s in SKIP)]
    seen = set()
    with multiprocessing.Pool(16) as pool:
        for fs in pool.imap_unordered(check_tu, todo):
            for f in fs:
                if f not in seen:
                    seen.add(f)
    for kind, p, l, msg in sorted(seen, key=lambda x: (x[0], x[1], x[2])):
        print(f'{kind} {p}:{l}: {msg}')
