#!/usr/bin/env bash
# Byte-compares dmxconvert output (keyvalues2 and binary encodings, plus console output and exit code) of a reference
# binary against the current build, over the golden DMX fixtures and a few SFM DMX files when SFM is installed.
# usage: tests/tools/compare_dmxconvert.sh <reference dmxconvert> [new dmxconvert]
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ref="$1"
new="${2:-${BUILD_DIR:-/tmp/v2build}/dmxconvert}"
SFM="${SFM_DIR:-/mnt/games/SteamLibrary/steamapps/common/SourceFilmmaker}"
work="$(mktemp -d /tmp/dmxconvert_compare.XXXXXX)"
trap 'rm -rf "$work"' EXIT

inputs=("$REPO"/tests/golden/fixtures/src/*.dmx "$REPO"/tests/golden/fixtures/src/anims/*.dmx)
if [[ -d "$SFM/content/testing/models/red_eye" ]]; then
    while IFS= read -r -d '' f; do inputs+=("$f"); done < <(find "$SFM/content/testing/models/red_eye" -name '*.dmx' -size -8M -print0 | head -z -n 6)
fi

n=0 same=0 diff=0
for f in "${inputs[@]}"; do
    for enc in keyvalues2 binary; do
        for side in ref new; do
            bin="$ref"; [[ $side == new ]] && bin="$new"
            timeout 120 "$bin" -i "$f" -o "$work/$side.$n.$enc.dmx" -oe "$enc" > "$work/$side.$n.$enc.log" 2>&1
            echo "rc=$?" >> "$work/$side.$n.$enc.log"
        done
        if cmp -s "$work/ref.$n.$enc.log" "$work/new.$n.$enc.log" &&
           { [[ ! -f "$work/ref.$n.$enc.dmx" ]] || cmp -s "$work/ref.$n.$enc.dmx" "$work/new.$n.$enc.dmx"; }; then
            same=$((same + 1))
        else
            diff=$((diff + 1))
            echo "DIFF $enc: $f"
        fi
    done
    n=$((n + 1))
done
echo "dmxconvert: $n inputs, $same identical, $diff different"
[[ $diff -eq 0 ]]
