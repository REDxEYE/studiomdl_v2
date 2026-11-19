#!/usr/bin/env python3
"""Golden-output test: compiles every QC of a fixture directory and compares the outputs byte for byte.

    run_golden.py --studiomdl <path to studiomdl_v2> [--fixtures DIR ...] [--update] [--keep DIR]

Each fixture directory holds *.qc files (sources referenced relative to the QC) and `expected.json` with, per QC,
the exit code and the SHA-256 of every file written under <game>/models/. `--update` rewrites expected.json from the
current compiler: do that only after checking that an output change is intended (`--keep` saves the outputs so they can
be inspected, e.g. with testfiles/mdl_tools/).

Extra fixture directories (models that can't be committed) can be added with --fixtures or the
STUDIOMDL_GOLDEN_FIXTURES environment variable (paths separated by os.pathsep).
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GAMEINFO = '"GameInfo"\n{\n\tgame "golden"\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tGame |gameinfo_path|.\n\t\t}\n\t}\n}\n'


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compile_qc(studiomdl, fixtures, qc, extra_args, keep_dir, absolute_qc):
    with tempfile.TemporaryDirectory(prefix="golden_") as game:
        with open(os.path.join(game, "gameinfo.txt"), "w") as f:
            f.write(GAMEINFO)
        # relative: the QC name, run in the fixture directory; absolute: the full QC path, run elsewhere
        qc_arg, cwd = (os.path.join(fixtures, qc), game) if absolute_qc else (qc, fixtures)
        cmd = [studiomdl, "-game", game] + extra_args + [qc_arg]
        proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log = proc.stdout.decode("utf-8", "replace")
        files = {}
        models = os.path.join(game, "models")
        for root, _, names in os.walk(models):
            for name in names:
                path = os.path.join(root, name)
                files[os.path.relpath(path, models).replace(os.sep, "/")] = sha256(path)
        if keep_dir:
            dst = os.path.join(keep_dir, os.path.splitext(qc)[0])
            shutil.rmtree(dst, ignore_errors=True)
            if os.path.isdir(models):
                shutil.copytree(models, dst)
            os.makedirs(dst, exist_ok=True)
            with open(os.path.join(dst, "compile.log"), "w") as f:
                f.write(log)
        return {"rc": proc.returncode, "files": dict(sorted(files.items()))}, log


def run_dir(studiomdl, fixtures, update, extra_args, keep_dir, absolute_qc):
    expected_path = os.path.join(fixtures, "expected.json")
    expected = {}
    if os.path.exists(expected_path):
        with open(expected_path) as f:
            expected = json.load(f)

    qcs = sorted(name for name in os.listdir(fixtures) if name.lower().endswith(".qc"))
    if not qcs:
        print("no .qc files in %s" % fixtures)
        return 1

    results = {}
    failures = 0
    for qc in qcs:
        result, log = compile_qc(studiomdl, fixtures, qc, extra_args, keep_dir, absolute_qc)
        results[qc] = result
        if update:
            print("%-32s rc=%d, %d files" % (qc, result["rc"], len(result["files"])))
            continue

        want = expected.get(qc)
        if want == result:
            print("%-32s ok" % qc)
            continue

        failures += 1
        print("%-32s FAILED" % qc)
        if want is None:
            print("    no expected result recorded (run with --update)")
        else:
            if want["rc"] != result["rc"]:
                print("    exit code %d, expected %d" % (result["rc"], want["rc"]))
            for name in sorted(set(want["files"]) | set(result["files"])):
                a, b = want["files"].get(name), result["files"].get(name)
                if a != b:
                    state = "missing" if b is None else ("unexpected" if a is None else "differs")
                    print("    %s %s" % (name, state))
        if result["rc"] != 0 or (want and want["rc"] != result["rc"]):
            tail = log.strip().splitlines()[-15:]
            print("    log tail:\n      " + "\n      ".join(tail))

    if update:
        with open(expected_path, "w") as f:
            json.dump(results, f, indent=2, sort_keys=True)
            f.write("\n")
        print("wrote %s" % expected_path)
        return 0

    stale = sorted(set(expected) - set(results))
    for qc in stale:
        print("%-32s expected result without a .qc (run with --update)" % qc)
    return 1 if failures or stale else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--studiomdl", required=True)
    parser.add_argument("--fixtures", action="append", default=[],
                        help="fixture directory (default: tests/golden/fixtures); may be repeated")
    parser.add_argument("--update", action="store_true", help="rewrite expected.json from the current output")
    parser.add_argument("--keep", help="copy every output and compile log into this directory")
    parser.add_argument("--absolute-qc", action="store_true",
                        help="pass the absolute QC path and run outside the fixture directory")
    parser.add_argument("extra", nargs="*", help="extra studiomdl arguments (after --)")
    args = parser.parse_args()

    dirs = args.fixtures or [os.path.join(HERE, "fixtures")]
    env_dirs = os.environ.get("STUDIOMDL_GOLDEN_FIXTURES", "")
    dirs += [d for d in env_dirs.split(os.pathsep) if d]

    studiomdl = os.path.abspath(args.studiomdl)
    status = 0
    for d in dirs:
        print("== %s" % d)
        status |= run_dir(studiomdl, os.path.abspath(d), args.update, args.extra, args.keep, args.absolute_qc)
    sys.exit(status)


if __name__ == "__main__":
    main()
