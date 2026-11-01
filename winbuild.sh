#!/usr/bin/env bash
set -euo pipefail

TARGET=x86_64-w64-mingw32
SYSROOT=/usr/$TARGET/sys-root/mingw
GCC_LIBDIR="$($TARGET-g++ -print-libgcc-file-name)"
GCC_LIBDIR="$(dirname "$GCC_LIBDIR")"

cmake -S . -B build-win \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER_TARGET=$TARGET \
  -DCMAKE_CXX_COMPILER_TARGET=$TARGET \
  -DCMAKE_SYSROOT=$SYSROOT \
  -DCMAKE_FIND_ROOT_PATH=$SYSROOT \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCMAKE_CXX_FLAGS="-fsized-deallocation -w" \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -B/usr/$TARGET/bin -L$GCC_LIBDIR" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld -B/usr/$TARGET/bin -L$GCC_LIBDIR" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fuse-ld=lld -B/usr/$TARGET/bin -L$GCC_LIBDIR" \
  -DCMAKE_RC_COMPILER=$TARGET-windres

cmake --build build-win --target studiomdl_v2 --target dmxconvert --parallel 16

# C++ objects cross module boundaries: deploy the shared toolchain runtimes.
for runtime in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
  runtime_path="$($TARGET-g++ -print-file-name="$runtime")"
  if [[ ! -f "$runtime_path" ]]; then
    runtime_path="$SYSROOT/bin/$runtime"
  fi
  cp "$runtime_path" build-win/
done
