#!/usr/bin/env python3
"""Differential test: compiles fixture QCs with studiomdl_v2 and with Source Filmmaker's studiomdl.exe (under Wine) and
compares the structure of the results (mdl_struct.py), not the bytes.

    sfm_diff.py --studiomdl /tmp/v2build/studiomdl_v2 --sfm-root <SourceFilmmaker dir> [--mod usermod]
                [--fixtures DIR ...] [--sfm-studiomdl <sfm-studiomdl.sh>] [qc names ...]

SFM's studiomdl crashes unless the QC lives below content/<mod>/, so the fixtures are copied to
content/<mod>/_sfm_diff/ for the SFM compile (and removed afterwards). Commands that only studiomdl_v2 knows are removed
from that copy (V2_ONLY_COMMANDS). A QC containing the line `// sfm_diff: skip` is not compared.

Accepted differences (KNOWN_DIFFERENCES) are applied before comparing.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mdl_struct  # noqa: E402
from run_golden import GAMEINFO  # noqa: E402

V2_ONLY_COMMANDS = ("$flexoptimize",)

KNOWN_DIFFERENCES = {
    # Blender Source Tools adds a "blender_implicit" bone that only zero-weight links refer to. SFM's studiomdl drops it,
    # studiomdl_v2 keeps it (AGENT_NOTES.md: zero-weight bone links).
    "ignore_bones": ("blender_implicit",),
    # Valve's SortAndBalanceBones (mrmsupport.cpp) drops bone weights below 0.05 and renormalizes; studiomdl_v2 keeps
    # every influence on purpose (the cull was removed). Applied to both sides before comparing.
    "weight_cull": 0.05,
}

DEFAULT_SFM_STUDIOMDL = os.path.expanduser(
    "/mnt/new_stuff/Stuff_copy/projects/python/SourceIOPlugin/SourceIO/tools/sfm/sfm-studiomdl.sh")


def model_name(qc_text):
    m = re.search(r'^\s*\$modelname\s+"?([^"\s]+)"?', qc_text, re.M | re.I)
    return m.group(1).replace("\\", "/") if m else None


def strip_v2_only(qc_text):
    return "\n".join(line for line in qc_text.splitlines()
                     if not line.strip().lower().startswith(V2_ONLY_COMMANDS)) + "\n"


def diff(a, b, path="", out=None, limit=20):
    """Lists differences between two JSON-like values as 'path: a != b' strings."""
    out = [] if out is None else out
    if len(out) >= limit:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            diff(a.get(k), b.get(k), "%s.%s" % (path, k), out, limit)
    elif isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        if len(a) != len(b):
            out.append("%s: %d entries != %d entries" % (path, len(a), len(b)))
        for i, (x, y) in enumerate(zip(a, b)):
            diff(x, y, "%s[%d]" % (path, i), out, limit)
    elif isinstance(a, float) and isinstance(b, float):
        # summaries round to 3-4 decimals; two compilers may land on either side of a rounding boundary
        if abs(a - b) > 0.002:
            out.append("%s: %r != %r" % (path, a, b))
    elif a != b:
        out.append("%s: %r != %r" % (path, a, b))
    return out


def _close(x, y, tol):
    if isinstance(x, (list, tuple)):
        return len(x) == len(y) and all(_close(a, b, tol) for a, b in zip(x, y))
    if isinstance(x, float) or isinstance(y, float):
        return abs(x - y) <= tol
    return x == y


def unmatched_vertices(a, b, tol=0.002):
    """Vertices (position, normal, uv, weights) of a that have no counterpart in b within tol; compilers may round the
    last digit differently, so exact comparison of the rounded values is too strict."""
    buckets = {}
    for v in b:
        buckets.setdefault(tuple(round(c, 1) for c in v[0]), []).append(v)
    missing = []
    for v in a:
        key = tuple(round(c, 1) for c in v[0])
        found = False
        for dx in (-0.1, 0, 0.1):
            for dy in (-0.1, 0, 0.1):
                for dz in (-0.1, 0, 0.1):
                    cand = buckets.get((round(key[0] + dx, 1), round(key[1] + dy, 1), round(key[2] + dz, 1)), [])
                    for i, w in enumerate(cand):
                        if _close(v, w, tol):
                            cand.pop(i)
                            found = True
                            break
                    if found:
                        break
                if found:
                    break
            if found:
                break
        if not found:
            missing.append(v)
    return missing


def normalize(summary):
    """Applies the accepted naming differences and moves the vertex data out of the structure."""
    vertex_data = {}
    for bp in summary["bodyparts"]:
        m = re.match(r"^.+_(clamped\d+)$", bp["name"])
        if m:  # studiomdl_v2 names split pieces <origin>_clamped<N>, Valve's studiomdl clamped<N>
            bp["name"] = m.group(1)
            for model in bp["models"]:
                model["name"] = re.sub(r"^.+_(clamped\d+)$", r"\1", model["name"])
        for j, model in enumerate(bp["models"]):
            for k, mesh in enumerate(model["meshes"]):
                vertex_data["%s/%d/%d" % (bp["name"], j, k)] = mesh.pop("vertex_data")
    return vertex_data


def compile_v2(studiomdl, fixtures, qc):
    game = tempfile.mkdtemp(prefix="sfmdiff_v2_")
    with open(os.path.join(game, "gameinfo.txt"), "w") as f:
        f.write(GAMEINFO)
    rc = subprocess.run([studiomdl, "-game", game, qc], cwd=fixtures, stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL).returncode
    return rc, game


def compile_sfm(sfm_studiomdl, game_dir, work, qc):
    proc = subprocess.run([sfm_studiomdl, "--game", game_dir, qc], cwd=work, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    log = proc.stdout.decode("utf-8", "replace")
    return (0 if 'Completed "' in log else (proc.returncode or 1)), log


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--studiomdl", required=True)
    p.add_argument("--sfm-root", default=os.environ.get("SFM_ROOT"), help="SourceFilmmaker directory (or $SFM_ROOT)")
    p.add_argument("--mod", default="usermod", help="mod directory under game/ and content/")
    p.add_argument("--sfm-studiomdl", default=os.environ.get("SFM_STUDIOMDL", DEFAULT_SFM_STUDIOMDL),
                   help="wrapper that runs SFM's studiomdl.exe (SourceIO tools/sfm/sfm-studiomdl.sh)")
    p.add_argument("--fixtures", action="append", default=[])
    p.add_argument("qcs", nargs="*", help="only these QC files")
    args = p.parse_args()
    if not args.sfm_root:
        p.error("--sfm-root or SFM_ROOT is required")

    game_dir = os.path.join(args.sfm_root, "game", args.mod)
    work = os.path.join(args.sfm_root, "content", args.mod, "_sfm_diff")
    status = 0
    for fixtures in args.fixtures or [os.path.join(HERE, "fixtures")]:
        fixtures = os.path.abspath(fixtures)
        shutil.rmtree(work, ignore_errors=True)
        shutil.copytree(fixtures, work)
        try:
            for qc in sorted(n for n in os.listdir(fixtures) if n.lower().endswith(".qc")):
                if args.qcs and qc not in args.qcs:
                    continue
                text = open(os.path.join(fixtures, qc), encoding="latin-1").read()
                if "// sfm_diff: skip" in text:
                    print("%-32s skipped" % qc)
                    continue
                with open(os.path.join(work, qc), "w", encoding="latin-1") as f:
                    f.write(strip_v2_only(text))
                name = model_name(text)

                rc_v2, v2_game = compile_v2(os.path.abspath(args.studiomdl), fixtures, qc)
                rc_sfm, log = compile_sfm(args.sfm_studiomdl, game_dir, work, qc)
                try:
                    if rc_v2 != 0 or rc_sfm != 0:
                        same = (rc_v2 != 0) == (rc_sfm != 0)
                        print("%-32s %s (v2 rc=%d, SFM %s)" % (qc, "ok" if same else "DIFFERS", rc_v2,
                                                               "failed" if rc_sfm else "ok"))
                        status |= 0 if same else 1
                        continue
                    ignore, cull = KNOWN_DIFFERENCES["ignore_bones"], KNOWN_DIFFERENCES["weight_cull"]
                    a = mdl_struct.summarize(os.path.join(game_dir, "models", name), ignore, cull)
                    b = mdl_struct.summarize(os.path.join(v2_game, "models", name), ignore, cull)
                    a, b = json.loads(json.dumps(a)), json.loads(json.dumps(b))
                    va, vb = normalize(a), normalize(b)
                    found = diff(a, b, "model")
                    for key in sorted(set(va) & set(vb)):
                        only_sfm = unmatched_vertices(va[key], vb[key])
                        only_v2 = unmatched_vertices(vb[key], va[key])
                        if only_sfm or only_v2:
                            found.append("mesh %s: %d vertices only in SFM output (e.g. %s), %d only in v2 output (e.g. %s)"
                                         % (key, len(only_sfm), only_sfm[:1], len(only_v2), only_v2[:1]))
                    print("%-32s %s" % (qc, "ok" if not found else "DIFFERS (SFM != v2)"))
                    for line in found:
                        print("    " + line)
                    status |= 1 if found else 0
                finally:
                    shutil.rmtree(v2_game, ignore_errors=True)
                    if name:
                        base = os.path.splitext(os.path.join(game_dir, "models", name))[0]
                        for ext in (".mdl", ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".phy", ".ani"):
                            if os.path.exists(base + ext):
                                os.remove(base + ext)
        finally:
            shutil.rmtree(work, ignore_errors=True)
    sys.exit(status)


if __name__ == "__main__":
    main()
