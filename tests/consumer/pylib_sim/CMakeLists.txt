# Simulates pylib (cpp/pylib): its cmake/dependencies.cmake (FetchContent: meshoptimizer v1.2, zstd, lz4, miniz, tinyexr,
# VTFLib, ...) first, then studiomdl_v2 as a subproject, then a host executable linking studiomdl::static and the
# host's own meshoptimizer target. The FetchContent sources come from pylib's existing build directory (no network).
#   cmake -S tests/consumer/pylib_sim -B /tmp/pylib_sim -G Ninja -DPYLIB_DIR=<pylib> -DSTUDIOMDL_DIR=<studiomdl_v2>
cmake_minimum_required(VERSION 3.26)
project(pylib_sim C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PYLIB_DIR "/mnt/new_stuff/Stuff_copy/projects/cpp/pylib" CACHE PATH "")
set(STUDIOMDL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "")
foreach (dep compressonator devil lz4 meshoptimizer miniz tinyexr vtfedit zstd)
    string(TOUPPER ${dep} DEP)
    set(FETCHCONTENT_SOURCE_DIR_${DEP} ${PYLIB_DIR}/cmake-build-release/_deps/${dep}-src CACHE PATH "")
endforeach ()

include(${PYLIB_DIR}/cmake/dependencies.cmake)

add_subdirectory(${STUDIOMDL_DIR} studiomdl EXCLUDE_FROM_ALL)

add_executable(sim_host ${STUDIOMDL_DIR}/tests/consumer/consumer_host.cpp)
target_link_libraries(sim_host PRIVATE studiomdl::static meshoptimizer)
