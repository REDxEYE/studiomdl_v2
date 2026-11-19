"""Which studiomdl functions need a StudioMdlState& parameter?

Seeds: functions that read/write g_studiomdl or a mutable namespace-scope variable defined in studiomdl/ (or
libs/utils/cmdlib.cpp). Closure: every function that calls a seeded function (transitively). Also reports
functions whose address is taken (callback tables, qsort comparators): those can't simply get a parameter.

Usage: tests/tools/run_py.sh ctx_closure.py <build dir> [out.json]
"""
import collections, json, multiprocessing, os, sys
import clang.cindex as ci

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
build = sys.argv[1] if len(sys.argv) > 1 else '/tmp/v2build'
out_json = sys.argv[2] if len(sys.argv) > 2 else None
sys.argv = [sys.argv[0], build]
import audit_format as A  # noqa: E402  (args_for, repo root)

R = A.R
FUNC_KINDS = {ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD, ci.CursorKind.CONSTRUCTOR,
              ci.CursorKind.DESTRUCTOR, ci.CursorKind.FUNCTION_TEMPLATE, ci.CursorKind.CONVERSION_FUNCTION}
# process-wide by design (application object, logging, filesystem setup, element-class linker anchors)
NOT_PER_COMPILE = ('PullInModule', 's_ApplicationObject', 's_MdlLoggingListener', 's_BufferedLoggingListener',
                   'g_FileSystemError', 'g_FileSystemErrorMode', 's_pfnSuggestGameInfoDir',
                   'g_CmdLibOutputLoggingListener', 's_bUseVProjectBinDir')


def in_scope(path):
    p = os.path.relpath(os.path.normpath(path), R)
    return p.startswith('studiomdl/') or p.startswith('include/studiomdl/') or p == 'libs/utils/cmdlib.cpp'


def callee_cursor(call):
    """The DECL_REF_EXPR / MEMBER_REF_EXPR naming the called function, unwrapping implicit casts."""
    children = list(call.get_children())
    c = children[0] if children else None
    while c is not None and c.kind == ci.CursorKind.UNEXPOSED_EXPR:
        inner = list(c.get_children())
        c = inner[0] if inner else None
    return c


def is_seed_var(ref):
    if ref.kind != ci.CursorKind.VAR_DECL or ref.location.file is None or not in_scope(ref.location.file.name):
        return False
    if any(s in ref.spelling for s in NOT_PER_COMPILE):
        return False
    if ref.type.is_const_qualified():
        return False
    parent = ref.semantic_parent
    # namespace-scope (incl. anonymous namespace) or class static; function-local statics are reported separately
    return parent is not None and parent.kind in (ci.CursorKind.TRANSLATION_UNIT, ci.CursorKind.NAMESPACE,
                                                   ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL)


def scan(e):
    defs, calls, uses, taken, local_statics = {}, set(), collections.defaultdict(set), set(), set()
    callee_refs = set()  # hashes of the cursors that name the function in a call expression
    tu = ci.Index.create().parse(e['file'], args=A.args_for(e))

    def walk(c, fn):
        for ch in c.get_children():
            f = ch.location.file
            if f is None or not in_scope(f.name):
                # still descend into in-scope code expanded from elsewhere is rare; skip foreign headers
                if ch.kind not in (ci.CursorKind.NAMESPACE, ci.CursorKind.UNEXPOSED_DECL, ci.CursorKind.LINKAGE_SPEC):
                    continue
            nfn = fn
            if ch.kind in FUNC_KINDS and ch.is_definition():
                usr = ch.get_usr()
                nfn = usr
                defs[usr] = dict(name=(ch.semantic_parent.spelling + '::' if ch.semantic_parent and
                                       ch.semantic_parent.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL,
                                                                   ci.CursorKind.NAMESPACE) else '') + ch.spelling,
                                 display=ch.displayname, file=os.path.relpath(f.name, R), line=ch.location.line,
                                 kind=str(ch.kind).split('.')[-1])
            elif ch.kind == ci.CursorKind.VAR_DECL and fn and ch.storage_class == ci.StorageClass.STATIC \
                    and not ch.type.is_const_qualified():
                local_statics.add((fn, ch.spelling, os.path.relpath(f.name, R), ch.location.line))
            if ch.kind == ci.CursorKind.CALL_EXPR:
                callee = callee_cursor(ch)
                if callee is not None:
                    callee_refs.add(callee.hash)
            if fn and ch.kind == ci.CursorKind.DECL_REF_EXPR and ch.referenced is not None:
                ref = ch.referenced
                if is_seed_var(ref):
                    uses[fn].add(ref.spelling)
                elif ref.kind in FUNC_KINDS and ch.hash not in callee_refs:
                    # named but not called: address taken (callback table, comparator, std::function...)
                    taken.add((ref.get_usr(), os.path.relpath(f.name, R), ch.location.line))
            if fn and ch.kind in (ci.CursorKind.CALL_EXPR, ci.CursorKind.MEMBER_REF_EXPR) and ch.referenced is not None \
                    and ch.referenced.kind in FUNC_KINDS:
                calls.add((fn, ch.referenced.get_usr()))
            walk(ch, nfn)

    walk(tu.cursor, None)
    return defs, calls, {k: sorted(v) for k, v in uses.items()}, taken, local_statics


if __name__ == '__main__':
    cc = json.load(open(os.path.join(build, 'compile_commands.json')))
    todo = [e for e in cc if in_scope(e['file'])]
    defs, calls, uses, taken, statics = {}, set(), collections.defaultdict(set), set(), set()
    with multiprocessing.Pool(16) as pool:
        for d, c, u, t, s in pool.imap_unordered(scan, todo):
            defs.update(d)
            calls |= c
            for k, v in u.items():
                uses[k].update(v)
            taken |= t
            statics |= s
    callers = collections.defaultdict(set)
    for a, b in calls:
        if a != b:
            callers[b].add(a)
    need = set(k for k in uses if k in defs)
    queue = list(need)
    while queue:
        f = queue.pop()
        for c in callers[f]:
            if c in defs and c not in need:
                need.add(c)
                queue.append(c)
    taken_need = sorted({(defs[u]['name'], fl, ln) for u, fl, ln in taken if u in need})
    result = dict(
        functions=sorted((dict(defs[u], usr=u, direct=sorted(uses.get(u, ()))) for u in need),
                         key=lambda d: (d['file'], d['line'])),
        address_taken=[dict(name=n, file=f, line=l) for n, f, l in taken_need],
        local_statics=[dict(function=defs.get(fn, {}).get('name', fn), name=n, file=f, line=l)
                       for fn, n, f, l in sorted(statics, key=lambda s: (s[2], s[3]))],
        seed_globals=sorted({g for v in uses.values() for g in v}),
    )
    by_file = collections.Counter(d['file'] for d in result['functions'])
    print(f"{len(result['functions'])} functions need the context ({sum(1 for d in result['functions'] if d['direct'])} use state directly)")
    for f, n in by_file.most_common():
        print(f'  {n:4d} {f}')
    print(f"{len(result['address_taken'])} of them have their address taken; {len(result['local_statics'])} mutable function-local statics")
    print('seed globals:', ', '.join(result['seed_globals']))
    if out_json:
        json.dump(result, open(out_json, 'w'), indent=1)
