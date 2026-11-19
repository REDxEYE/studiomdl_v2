#!/usr/bin/env python3
"""Counts compiler warnings per module from a full build log and compares them with tests/warnings_baseline.json.

    cmake -S . -B /tmp/v2warn -G Ninja -DSTUDIOMDL_WARNINGS=ON && ninja -C /tmp/v2warn > /tmp/warn.log 2>&1
    tests/warnings_report.py /tmp/warn.log            # fails if an owned module got more warnings
    tests/warnings_report.py /tmp/warn.log --update   # record the current counts

Use the log of a *clean* build: ninja does not repeat the warnings of up-to-date objects. Each warning is counted
once (headers are compiled by many units). Owned modules (OWNED) gate; the vendored SDK libraries are only reported.
"""
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASELINE = os.path.join(HERE, "warnings_baseline.json")
OWNED = ("studiomdl", "include/studiomdl", "libs/utils", "libs/filesystem", "tests")
WARNING = re.compile(r"^(\S+?):(\d+):(\d+): warning: .*?\[(-W[\w=+-]+)\]", re.M)


def module_of(path):
    path = os.path.normpath(path).replace(os.sep, "/")
    m = re.search(r"(?:^|/)(studiomdl|libs|include|tests|dmxconvert)/([^/]+)", path)
    if not m:
        return "other"
    top, sub = m.group(1), m.group(2)
    if top in ("libs", "include") and not sub.endswith((".h", ".cpp")):
        return "%s/%s" % (top, sub)
    return top


def count(log):
    seen = set()
    counts = collections.Counter()
    for m in WARNING.finditer(log):
        key = (os.path.normpath(m.group(1)), m.group(2), m.group(3), m.group(4))
        if key in seen:
            continue
        seen.add(key)
        counts[module_of(m.group(1))] += 1
    return dict(sorted(counts.items()))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    counts = count(open(sys.argv[1], errors="replace").read())
    if "--update" in sys.argv:
        with open(BASELINE, "w") as f:
            json.dump(counts, f, indent=2, sort_keys=True)
            f.write("\n")
        print("wrote %s (%d warnings)" % (BASELINE, sum(counts.values())))
        return
    baseline = json.load(open(BASELINE))
    status = 0
    for module in sorted(set(counts) | set(baseline)):
        now, before = counts.get(module, 0), baseline.get(module, 0)
        owned = module in OWNED
        mark = ""
        if now > before:
            mark = "  MORE" + (" (fails: owned module)" if owned else "")
            status |= owned
        elif now < before:
            mark = "  fewer: run with --update to lower the baseline"
        print("%-24s %5d (baseline %5d)%s" % (module, now, before, mark))
    sys.exit(status)


if __name__ == "__main__":
    main()
