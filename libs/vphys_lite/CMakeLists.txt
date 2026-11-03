cmake_minimum_required(VERSION 3.22)
project(vphys_lite)

set(CMAKE_CXX_STANDARD 20)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(vphys_lite
        studiocollision.cpp
        studiosurfaceprops.cpp
        legacy_qhull/legacy_qhull_bridge.cpp
        legacy_qhull/qhull.cxx
        legacy_qhull/qhull_qset.cxx
        legacy_qhull/qhull_mem.cxx
        legacy_qhull/qhull_user.cxx
        legacy_qhull/qhull_global.cxx
        legacy_qhull/qhull_stat.cxx
        legacy_qhull/qhull_merge.cxx
        legacy_qhull/qhull_geom.cxx
        legacy_qhull/qhull_geom2.cxx
        legacy_qhull/qhull_poly.cxx
        legacy_qhull/qhull_poly2.cxx
        legacy_qhull/qhull_io.cxx
)
target_include_directories(vphys_lite PUBLIC ./)
target_include_directories(vphys_lite PUBLIC ../../include)
target_include_directories(vphys_lite PRIVATE ../../include/common)
target_compile_definitions(vphys_lite PRIVATE -DSTATIC_TIER0)
target_link_libraries(vphys_lite PRIVATE )
