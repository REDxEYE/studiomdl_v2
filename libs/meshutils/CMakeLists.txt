cmake_minimum_required(VERSION 3.22)
project(meshutils)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(meshutils
        simplify.cpp
        atlas.cpp
        clipmesh.cpp
        convexhull.cpp
        mesh.cpp
        uvparam.cpp
        vertexops.cpp
        )
target_include_directories(meshutils PUBLIC ../../include)
target_include_directories(meshutils PRIVATE ../../include/meshutils)
target_compile_definitions(meshutils PRIVATE -DSTATIC_TIER0)
target_link_libraries(meshutils PRIVATE tier0)
