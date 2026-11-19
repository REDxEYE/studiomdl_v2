#!/usr/bin/env bash
# Regression check on real (decompiled) game models: compiles each model with studiomdl_v2 and compares it with a
# baseline, chosen with --baseline:
#   v2  (default) bytes against a saved studiomdl_v2 run (--save-baseline once, then every run compares to it)
#   sfm           decoded animations (tests/regression/animcmp.py) and structure/vertices (vertcmp.py) against
#                 SFM's studiomdl.exe output, within --pos-tol/--rot-tol/--max-unmatched
# In both modes the SFM comparison is printed (SFM runs under Wine; its output is cached in the work dir), and the
# animations are also compared with the original game model (<source-root>/<name>.mdl), if present.
#
# usage: tests/regression.sh [options]
#   --studiomdl PATH    studiomdl_v2 binary            (default: <repo>/cmake-build-debug/studiomdl_v2)
#   --sfm-root DIR      SourceFilmmaker directory      (default: $SFM_ROOT or the Steam library path below)
#   --wrapper PATH      runs SFM's studiomdl.exe       (default: $SFM_STUDIOMDL or SourceIO's sfm-studiomdl.sh)
#   --source-root DIR   holds <name>_decompiled/<name>.qc and the original <name>.mdl
#                       (default: <sfm-root>/game/hl2/models)
#   --models "A B"      model names                    (default: "pigeon gman")
#   --work DIR          baselines, SFM cache, scratch  (default: tests/regression/work, git-ignored)
#   --baseline v2|sfm   what the run is checked against (default: v2)
#   --sfm               recompile the SFM references even if cached
#   --no-lod            strip $lod/$shadowlod from the QCs (separate cache: <work>/nolod)
#   --save-baseline     v2: store this run's outputs as the baseline; sfm: recompile the SFM references (= --sfm)
#   --pos-tol X         sfm: max animation position difference   (default 0.002)
#   --rot-tol X         sfm: max animation rotation difference, degrees (default 0.01)
#   --max-unmatched N   sfm: max vertices without an SFM counterpart (default 0); structure must match exactly
#
# Exit code: 1 if a v2 compile fails or the run differs from the baseline (v2: any byte; sfm: beyond the tolerances).
# In v2 mode the SFM/original comparisons are reports only (they always show small rounding differences).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STUDIOMDL=/mnt/new_stuff/Stuff_copy/projects/cpp/studiomdl_v2/cmake-build-debug/studiomdl_v2
SFM_ROOT="${SFM_ROOT:-/mnt/games/SteamLibrary/steamapps/common/SourceFilmmaker}"
WRAPPER="${SFM_STUDIOMDL:-/mnt/new_stuff/Stuff_copy/projects/python/SourceIOPlugin/SourceIO/tools/sfm/sfm-studiomdl.sh}"
SOURCE_ROOT=""
MODELS="pigeon gman"
WORK="$HERE/regression/work"
REFRESH_SFM=0
NO_LOD=0
SAVE_BASELINE=0
BASELINE=v2
POS_TOL=0.002
ROT_TOL=0.01
MAX_UNMATCHED=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --studiomdl) STUDIOMDL="$2"; shift 2 ;;
        --sfm-root) SFM_ROOT="$2"; shift 2 ;;
        --wrapper) WRAPPER="$2"; shift 2 ;;
        --source-root) SOURCE_ROOT="$2"; shift 2 ;;
        --models) MODELS="$2"; shift 2 ;;
        --work) WORK="$2"; shift 2 ;;
        --sfm) REFRESH_SFM=1; shift ;;
        --no-lod) NO_LOD=1; shift ;;
        --save-baseline) SAVE_BASELINE=1; shift ;;
        --baseline) BASELINE="$2"; shift 2 ;;
        --pos-tol) POS_TOL="$2"; shift 2 ;;
        --rot-tol) ROT_TOL="$2"; shift 2 ;;
        --max-unmatched) MAX_UNMATCHED="$2"; shift 2 ;;
        -h|--help) sed -n '2,27p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
if [[ $BASELINE != v2 && $BASELINE != sfm ]]; then
    echo "--baseline must be v2 or sfm, got: $BASELINE" >&2
    exit 2
fi
if [[ $BASELINE == sfm && $SAVE_BASELINE == 1 ]]; then
    REFRESH_SFM=1
    SAVE_BASELINE=0
fi
SOURCE_ROOT="${SOURCE_ROOT:-$SFM_ROOT/game/hl2/models}"
STUDIOMDL="$(realpath "$STUDIOMDL")"
LOD_ARGS=()
if [[ $NO_LOD == 1 ]]; then
    WORK="$WORK/nolod"
    LOD_ARGS=(--no-lod)
fi

# SFM's studiomdl only works on QCs below content/<mod>/; its output lands in game/hl2/models/regression/
SFM_CONTENT="$SFM_ROOT/content/hl2/_studiomdl_regression"
SFM_OUT="$SFM_ROOT/game/hl2/models/regression"
cleanup() { rm -rf "$SFM_CONTENT" "$SFM_OUT"; }
trap cleanup EXIT

GAMEINFO='"GameInfo"\n{\n\tgame "regression"\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tGame |gameinfo_path|.\n\t\t}\n\t}\n}\n'
OUTPUTS=(mdl ani vvd dx90.vtx)
failed=0

for name in $MODELS; do
    echo "== $name"
    src="$SOURCE_ROOT/${name}_decompiled"
    dir="$WORK/$name"
    mkdir -p "$dir"
    python3 "$HERE/regression/prepare_qc.py" "$src" "$dir/src" "$name.qc" "regression/$name.mdl" "${LOD_ARGS[@]}"

    # studiomdl_v2
    rm -rf "$dir/v2"
    mkdir -p "$dir/v2"
    printf "$GAMEINFO" > "$dir/v2/gameinfo.txt"
    if (cd "$dir/src" && "$STUDIOMDL" -game "$dir/v2" "$name.qc" > "$dir/v2.log" 2>&1); then
        echo "  v2 compile ok ($dir/v2.log)"
    else
        echo "  v2 compile FAILED, see $dir/v2.log"
        tail -5 "$dir/v2.log" | sed 's/^/    /'
        failed=1
        continue
    fi
    v2="$dir/v2/models/regression/$name.mdl"

    # v2 baseline (a previous v2 run)
    if [[ $BASELINE == v2 ]]; then
        if [[ $SAVE_BASELINE == 1 ]]; then
            rm -rf "$dir/baseline"
            cp -r "$dir/v2/models/regression" "$dir/baseline"
            echo "  baseline saved to $dir/baseline"
        elif [[ -d "$dir/baseline" ]]; then
            for ext in "${OUTPUTS[@]}"; do
                if cmp -s "$dir/baseline/$name.$ext" "$dir/v2/models/regression/$name.$ext"; then
                    echo "  $name.$ext identical to baseline"
                else
                    echo "  $name.$ext CHANGED vs baseline"
                    failed=1
                fi
            done
        else
            echo "  no baseline yet (run with --save-baseline)"
        fi
    fi

    # SFM reference (cached)
    if [[ $REFRESH_SFM == 1 || ! -f "$dir/sfm/$name.mdl" ]]; then
        rm -rf "$SFM_CONTENT/$name" "$dir/sfm"
        mkdir -p "$SFM_CONTENT"
        cp -r "$dir/src" "$SFM_CONTENT/$name"
        (cd "$SFM_CONTENT/$name" && timeout 900 "$WRAPPER" --game "$SFM_ROOT/game/hl2" "$name.qc" > "$dir/sfm.log" 2>&1) || true
        if grep -q 'Completed "' "$dir/sfm.log"; then
            mkdir -p "$dir/sfm"
            cp "$SFM_OUT/$name".* "$dir/sfm/"
            echo "  SFM reference compiled ($dir/sfm.log)"
        else
            echo "  SFM compile failed, see $dir/sfm.log"
        fi
    fi
    if [[ -f "$dir/sfm/$name.mdl" ]]; then
        if [[ $BASELINE == sfm ]]; then
            echo "  animations vs SFM baseline (pos <= $POS_TOL, rot <= $ROT_TOL deg):"
            if python3 "$HERE/regression/animcmp.py" "$dir/sfm/$name.mdl" "$v2" --pos-tol "$POS_TOL" --rot-tol "$ROT_TOL" > "$dir/animcmp.log"; then
                tail -1 "$dir/animcmp.log"
            else
                tail -1 "$dir/animcmp.log"
                echo "  animations DIFFER from the SFM baseline, see $dir/animcmp.log"
                failed=1
            fi
            echo "  structure/vertices vs SFM baseline (unmatched <= $MAX_UNMATCHED):"
            if ! python3 "$HERE/regression/vertcmp.py" "$dir/sfm/$name.mdl" "$v2" --max-unmatched "$MAX_UNMATCHED"; then
                echo "  structure/vertices DIFFER from the SFM baseline"
                failed=1
            fi
        else
            echo "  animations vs SFM:"
            python3 "$HERE/regression/animcmp.py" "$dir/sfm/$name.mdl" "$v2" | tail -1
            echo "  structure/vertices vs SFM:"
            python3 "$HERE/regression/vertcmp.py" "$dir/sfm/$name.mdl" "$v2"
        fi
    elif [[ $BASELINE == sfm ]]; then
        echo "  no SFM baseline (SFM compile failed)"
        failed=1
    fi

    # original game model
    if [[ -f "$SOURCE_ROOT/$name.mdl" ]]; then
        echo "  animations vs original $SOURCE_ROOT/$name.mdl (includes decompile rounding):"
        python3 "$HERE/regression/animcmp.py" "$SOURCE_ROOT/$name.mdl" "$v2" | tail -1
    fi
done

exit $failed
