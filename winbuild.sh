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
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -B/usr/$TARGET/bin -L$GCC_LIBDIR -static" \
  -DCMAKE_CXX_STANDARD_LIBRARIES="-lstdc++ -lwinpthread" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld -B/usr/$TARGET/bin -L$GCC_LIBDIR" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fuse-ld=lld -B/usr/$TARGET/bin -L$GCC_LIBDIR" \
  -DCMAKE_RC_COMPILER=$TARGET-windres

# -static links libstdc++, libgcc and winpthread into the executables (no DLLs to deploy).
# Old copies of the runtime DLLs next to the exe would hide a missing static link when testing, so remove them.
rm -f build-win/libstdc++-6.dll build-win/libgcc_s_seh-1.dll build-win/libwinpthread-1.dll

cmake --build build-win --target studiomdl_v2 --target dmxconvert --parallel 16

