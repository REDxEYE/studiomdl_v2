cmake_minimum_required(VERSION 3.22)
project(bonesetup)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(bonesetup
        bone_constraints.cpp
        bone_decode.cpp
        bone_ik.cpp
        bone_setup.cpp
        bone_utils.cpp
        )
target_include_directories(bonesetup PUBLIC ../../include)
target_include_directories(bonesetup PRIVATE ../../include/studiomdl)
target_compile_definitions(bonesetup PRIVATE -DSTATIC_TIER0)
target_link_libraries(bonesetup PRIVATE tier0)