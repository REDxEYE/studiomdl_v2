#!/usr/bin/env bash
# Byte-compares studiomdl outputs and logs of a reference binary against the current build
# (golden fixtures, gman/pigeon regression models, plus SFM glamchica DMX model when SFM is installed).
# usage: tests/tools/compare.sh <reference studiomdl_v2> [new studiomdl_v2] [tail lines]
#   build a reference first: tests/tools/build_ref.sh <commit> /tmp/refb
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ref="$1"
new="${2:-${BUILD_DIR:-/tmp/v2build}/studiomdl_v2}"
tail_lines="${3:-6}"
SFM="${SFM_DIR:-/mnt/games/SteamLibrary/steamapps/common/SourceFilmmaker}"
extra=()
glam="$SFM/content/testing/models/red_eye/glamchica_nsfw.qc"
[[ -f "$glam" ]] && extra+=(--qc "$glam")
"$REPO/tests/compare_outputs.sh" --ref "$ref" --new "$new" --work /tmp/studiomdl_compare "${extra[@]}" 2>&1 | tail -n "$tail_lines"
