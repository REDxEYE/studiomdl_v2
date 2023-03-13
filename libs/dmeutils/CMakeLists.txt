cmake_minimum_required(VERSION 3.22)
project(dmeutils)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(dmeutils
        savetodmx.cpp
        dmanimutils.cpp
        dmmeshutils.cpp
        loadfromdmx.cpp
        )
target_include_directories(dmeutils PUBLIC ../../include)
target_include_directories(dmeutils PRIVATE ../../include/dmeutils)
target_compile_definitions(dmeutils PRIVATE -DSTATIC_TIER0)
target_link_libraries(dmeutils PRIVATE tier0)