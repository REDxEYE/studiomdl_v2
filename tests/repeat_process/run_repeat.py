#!/usr/bin/env python3
"""Several compiles in one process must give the same results as the same compiles in separate processes.

    run_repeat.py --studiomdl <studiomdl_v2> --repeat <studiomdl_repeat_test> [--fixtures DIR] [--keep DIR]

Takes the golden fixtures that compile successfully (rc 0 in expected.json), each preceded by a compile that fails
(the golden error fixture and tests/repeat_process/fixtures/fail_*.qc: QC syntax error, error late in the compile,
a tier0 Error() that is not an MdlError), runs them all, then all again in reverse order (so each QC also runs after
every other one and after itself), every compile into its own game directory. Once as separate studiomdl_v2
processes, once as one studiomdl_repeat_test process, then compares exit codes, logs (game directory paths
normalized) and every output file. A failed compile must leave the process exactly as reusable as a fresh one.
"""
import argparse
import filecmp
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GAMEINFO = '"GameInfo"\n{\n\tgame "repeat"\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tGame |gameinfo_path|.\n\t\t}\n\t}\n}\n'


def make_games(root, count):
    games = []
    for i in range(count):
        game = os.path.join(root, "game%02d" % i)
        os.makedirs(game)
        with open(os.path.join(game, "gameinfo.txt"), "w") as f:
            f.write(GAMEINFO)
        games.append(game)
    return games


def normalize(log, root):
    # Windows consoles write \r\n
    log = log.replace("\r\n", "\n")
    return log.replace(root, "<root>").replace(root.replace("/", "\\"), "<root>")


def model_files(game):
    models = os.path.join(game, "models")
    found = []
    for base, _, names in os.walk(models):
        for name in names:
            found.append(os.path.relpath(os.path.join(base, name), models))
    return sorted(found)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--studiomdl", required=True)
    parser.add_argument("--repeat", required=True)
    parser.add_argument("--fixtures", default=os.path.join(HERE, "..", "golden", "fixtures"))
    parser.add_argument("--keep")
    args = parser.parse_args()

    fixtures = os.path.abspath(args.fixtures)
    with open(os.path.join(fixtures, "expected.json")) as f:
        expected = json.load(f)
    qcs = sorted(qc for qc, result in expected.items() if result["rc"] == 0)
    failing = sorted(qc for qc, result in expected.items() if result["rc"] != 0)
    failing += sorted(os.path.join(HERE, "fixtures", name) for name in os.listdir(os.path.join(HERE, "fixtures"))
                      if name.endswith(".qc"))
    sequence = []
    for i, qc in enumerate(qcs):
        sequence += [failing[i % len(failing)], qc]
    sequence += sequence[::-1]

    work = args.keep or tempfile.mkdtemp(prefix="repeat_")
    if args.keep:
        shutil.rmtree(work, ignore_errors=True)
    separate_root = os.path.join(work, "separate")
    single_root = os.path.join(work, "single")
    separate_games = make_games(separate_root, len(sequence))
    single_games = make_games(single_root, len(sequence))

    separate = []
    for qc, game in zip(sequence, separate_games):
        proc = subprocess.run([args.studiomdl, "-game", game, qc], cwd=fixtures, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
        separate.append((proc.returncode, normalize(proc.stdout.decode("utf-8", "replace"), separate_root)))

    cmd = [args.repeat]
    for i, (qc, game) in enumerate(zip(sequence, single_games)):
        if i:
            cmd.append("--then")
        cmd += ["-game", game, qc]
    proc = subprocess.run(cmd, cwd=fixtures, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = normalize(proc.stdout.decode("utf-8", "replace"), single_root)
    single = [(int(rc), log) for log, rc in
              re.findall(r"=== studiomdl_repeat run \d+\n(.*?)=== studiomdl_repeat exit (-?\d+)\n", output, re.S)]

    failures = 0
    if len(single) != len(sequence):
        print("the single process ran %d of %d compiles (rc %d); output tail:\n%s"
              % (len(single), len(sequence), proc.returncode, output[-2000:]))
        failures += 1
    for i, qc in enumerate(sequence[:len(single)]):
        (rc_a, log_a), (rc_b, log_b) = separate[i], single[i]
        label = "run %2d %s" % (i, qc)
        if rc_a != rc_b:
            print("%s: exit code %d (separate) vs %d (single process)" % (label, rc_a, rc_b))
            failures += 1
        if log_a != log_b:
            print("%s: log differs" % label)
            failures += 1
        files_a, files_b = model_files(separate_games[i]), model_files(single_games[i])
        if files_a != files_b:
            print("%s: output files differ: %s vs %s" % (label, files_a, files_b))
            failures += 1
            continue
        for name in files_a:
            if not filecmp.cmp(os.path.join(separate_games[i], "models", name),
                               os.path.join(single_games[i], "models", name), shallow=False):
                print("%s: %s differs" % (label, name))
                failures += 1

    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    print("%d compiles in one process vs separate processes: %s"
          % (len(sequence), "identical" if not failures else "%d difference(s)" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
