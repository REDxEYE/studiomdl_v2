#!/usr/bin/env bash
# Compiles the same inputs with two studiomdl_v2 binaries and compares every output byte for byte, plus the
# compile logs (with temp paths stripped) and exit codes. Meant for refactors that must not change the output:
# keep a binary built before the change as the reference.
#
# Inputs: the golden fixtures (plain and -minlod 1), tests/golden/unicode, the regression models in
# tests/regression/work/<name>/src (plain and -minlod 2, each compiled twice so the second run loads the existing
# .mdl for sequence order), pigeon with reordered/renamed sequences and an addlayer over an existing model, and any
# --qc given.
#
# usage: tests/compare_outputs.sh --ref BIN [options]
#   --ref BIN        reference studiomdl_v2 (required)
#   --new BIN        binary under test           (default: /tmp/v2build/studiomdl_v2)
#   --build DIR      run ninja studiomdl_v2 in DIR first (stops on build errors)
#   --work DIR       scratch directory           (default: /tmp/studiomdl_compare)
#   --models "A B"   regression models           (default: "gman pigeon", skipped when their src is missing)
#   --qc PATH        extra QC to compile from its own directory; may be repeated (e.g. a big flexed model)
#
# Exit code: 0 when everything matches, 1 on any difference or build failure.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
REF=""
NEW=/tmp/v2build/studiomdl_v2
BUILD=""
WORK=/tmp/studiomdl_compare
MODELS="gman pigeon"
EXTRA_QCS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ref) REF="$2"; shift 2 ;;
        --new) NEW="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        --work) WORK="$2"; shift 2 ;;
        --models) MODELS="$2"; shift 2 ;;
        --qc) EXTRA_QCS+=("$(realpath "$2")"); shift 2 ;;
        -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
done
[[ -n "$REF" ]] || { echo "--ref is required" >&2; exit 1; }
REF="$(realpath "$REF")"
NEW="$(realpath "$NEW")"

if [[ -n "$BUILD" ]]; then
    if ! (cd "$BUILD" && ninja -k 0 studiomdl_v2 > "$WORK.build.log" 2>&1); then
        grep -E "error:" "$WORK.build.log" | sort -u | head -40
        echo "build failed, see $WORK.build.log"
        exit 1
    fi
fi

GAMEINFO='"GameInfo"\n{\n\tgame "compare"\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tGame |gameinfo_path|.\n\t\t}\n\t}\n}\n'

# compile <bin> <out dir> <qc dir> <qc file> [args...]: one compile into its own game dir, log and exit code saved
compile() {
    local bin="$1" out="$2" dir="$3" qc="$4"
    shift 4
    mkdir -p "$out"
    printf "$GAMEINFO" > "$out/gameinfo.txt"
    (cd "$dir" && "$bin" -game "$out" "$@" "$qc" > "$out/log.txt" 2>&1)
    echo "$?" > "$out/rc.txt"
}

# run_all <bin> <out dir>
run_all() {
    local bin="$1" out="$2"
    rm -rf "$out"
    mkdir -p "$out"
    python3 "$REPO/tests/golden/run_golden.py" --studiomdl "$bin" --keep "$out/golden" > "$out/golden.txt" 2>&1
    python3 "$REPO/tests/golden/run_golden.py" --studiomdl "$bin" --keep "$out/golden_minlod" -- -minlod 1 \
        > "$out/golden_minlod.txt" 2>&1
    python3 "$REPO/tests/golden/run_golden.py" --studiomdl "$bin" --fixtures "$REPO/tests/golden/unicode/Модели" \
        --keep "$out/unicode" > "$out/unicode.txt" 2>&1

    local m variant
    for m in $MODELS; do
        local src="$REPO/tests/regression/work/$m/src"
        [[ -f "$src/$m.qc" ]] || continue
        for variant in plain minlod; do
            local args=()
            [[ $variant == minlod ]] && args=(-minlod 2)
            compile "$bin" "$out/$m-$variant" "$src" "$m.qc" "${args[@]}"
            # the second run reads the first run's .mdl (LoadPreexistingSequenceOrder)
            mkdir -p "$out/$m-$variant/first"
            mv "$out/$m-$variant/log.txt" "$out/$m-$variant/first/log.txt"
            cp -r "$out/$m-$variant/models" "$out/$m-$variant/first/"
            compile "$bin" "$out/$m-$variant" "$src" "$m.qc" "${args[@]}"
        done
    done

    # sequences reordered against an existing model: remap table, new sequence slotting, autolayer remap
    local pigeon="$REPO/tests/regression/work/pigeon/src"
    if [[ -f "$pigeon/pigeon.qc" && -d "$out/pigeon-plain/first/models" ]]; then
        mkdir -p "$out/reorder"
        cp -r "$out/pigeon-plain/first/models" "$out/reorder/"
        cp "$WORK/pigeon_reorder.qc" "$pigeon/zz_compare_reorder.qc"
        compile "$bin" "$out/reorder" "$pigeon" zz_compare_reorder.qc
        rm -f "$pigeon/zz_compare_reorder.qc"
    fi

    local qc i=0
    for qc in "${EXTRA_QCS[@]}"; do
        compile "$bin" "$out/extra$i-$(basename "$qc" .qc)" "$(dirname "$qc")" "$(basename "$qc")"
        i=$((i + 1))
    done
}

mkdir -p "$WORK"
if [[ -f "$REPO/tests/regression/work/pigeon/src/pigeon.qc" ]]; then
    python3 - "$REPO/tests/regression/work/pigeon/src/pigeon.qc" "$WORK/pigeon_reorder.qc" <<'EOF'
import re, sys
text = open(sys.argv[1]).read()
blocks = list(re.finditer(r'^\$sequence "[^"]*" \{.*?^\}\n?', text, re.S | re.M))
# the first sequence moved to the end: every index shifts and the remap table has to restore the order
text = text[:blocks[0].start()] + text[blocks[0].end():] + "\n" + blocks[0].group(0)
names = re.findall(r'\$sequence "([^"]*)"', text)
# a renamed sequence is new to the existing model and takes the free slot
text = text.replace('$sequence "%s" {' % names[-2], '$sequence "%s_renamed" {' % names[-2], 1)
# an autolayer index has to follow the remap
text = text.replace('$sequence "%s" {' % names[1], '$sequence "%s" {\n\taddlayer "%s"' % (names[1], names[0]), 1)
open(sys.argv[2], "w").write(text)
EOF
fi

echo "reference: $REF"
run_all "$REF" "$WORK/ref"
echo "new:       $NEW"
run_all "$NEW" "$WORK/new"

cd "$WORK"
bad=0
count=0
while IFS= read -r f; do
    count=$((count + 1))
    if [[ ! -f "new/$f" ]]; then
        echo "MISSING $f"; bad=$((bad + 1))
    elif ! cmp -s "ref/$f" "new/$f"; then
        echo "DIFF    $f"; bad=$((bad + 1))
    fi
done < <(cd ref && find . -type f \( -name "*.mdl" -o -name "*.vvd" -o -name "*.vtx" -o -name "*.ani" \) | sort)
while IFS= read -r f; do
    [[ -f "ref/$f" ]] || { echo "EXTRA   $f"; bad=$((bad + 1)); }
done < <(cd new && find . -type f \( -name "*.mdl" -o -name "*.vvd" -o -name "*.vtx" -o -name "*.ani" \) | sort)
while IFS= read -r f; do
    if ! diff -q <(sed -E 's#/tmp/[^ ":]*##g' "ref/$f") <(sed -E 's#/tmp/[^ ":]*##g' "new/$f") > /dev/null 2>&1; then
        echo "LOGDIFF $f"; bad=$((bad + 1))
    fi
done < <(cd ref && find . -type f \( -name "*.txt" -o -name "*.log" \) ! -name gameinfo.txt | sort)

echo "compared $count output files: $bad difference(s); outputs in $WORK/ref and $WORK/new"
[[ $bad -eq 0 ]]
