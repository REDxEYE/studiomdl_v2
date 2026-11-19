#!/usr/bin/env bash
# Builds studiomdl_v2 and dmxconvert from a commit in a temporary git worktree (the working tree is left alone).
# tests/ is untracked, so it is symlinked into the worktree for the CMake configure step.
# usage: tests/tools/build_ref.sh [--windows] <commit> <build dir>     e.g. tests/tools/build_ref.sh HEAD /tmp/refb
#   --windows: mingw-w64 cross build with clang + lld (same settings as build-win/); run the .exe under Wine with
#              Z:\... paths
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
windows=0
if [[ "${1:-}" == "--windows" ]]; then
    windows=1
    shift
fi
commit="$1"
build="$2"

cmake_args=(-G Ninja)
if [[ $windows -eq 1 ]]; then
    gcc_dir="$(ls -d /usr/lib/gcc/x86_64-w64-mingw32/* | sort -V | tail -1)"
    linker="-fuse-ld=lld -B/usr/x86_64-w64-mingw32/bin -L$gcc_dir"
    cmake_args+=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows
                 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
                 -DCMAKE_C_COMPILER_TARGET=x86_64-w64-mingw32 -DCMAKE_CXX_COMPILER_TARGET=x86_64-w64-mingw32
                 -DCMAKE_SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
                 "-DCMAKE_CXX_FLAGS=-fsized-deallocation -w"
                 "-DCMAKE_EXE_LINKER_FLAGS=$linker -static" "-DCMAKE_SHARED_LINKER_FLAGS=$linker"
                 "-DCMAKE_MODULE_LINKER_FLAGS=$linker" "-DCMAKE_CXX_STANDARD_LIBRARIES=-lstdc++ -lwinpthread"
                 -DSTUDIOMDL_BUILD_TESTS=OFF)
else
    cmake_args+=(-DCMAKE_BUILD_TYPE=RelWithDebInfo)
fi

wt="$(mktemp -d /tmp/studiomdl_refwt.XXXXXX)"
cleanup() { rm -f "$wt/tests"; git -C "$REPO" worktree remove --force "$wt" 2>/dev/null || rm -rf "$wt"; git -C "$REPO" worktree prune; }
trap cleanup EXIT
git -C "$REPO" worktree add -f --detach "$wt" "$commit" >/dev/null
ln -sfn "$REPO/tests" "$wt/tests"
cmake -S "$wt" -B "$build" "${cmake_args[@]}" >/dev/null
ninja -C "$build" studiomdl_v2 dmxconvert
echo "built $(git -C "$REPO" rev-parse --short "$commit") into $build"
