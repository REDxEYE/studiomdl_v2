cmake_minimum_required(VERSION 3.22)
project(fbxutils)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(fbxutils
        dmfbxserializer.cpp
        )
target_include_directories(fbxutils PUBLIC ../../include)
target_include_directories(fbxutils PRIVATE ../../include/fbxutils ../../include/fbx_sdk)
target_compile_definitions(fbxutils PUBLIC)