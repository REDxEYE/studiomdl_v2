cmake_minimum_required(VERSION 3.22)
project(materialsystem)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(materialsystem
        MaterialSystemUtil.cpp
)
target_include_directories(materialsystem PUBLIC ../../include)
target_include_directories(materialsystem PRIVATE ../../include/materialsystem)
target_compile_definitions(materialsystem PRIVATE -DSTATIC_TIER0)
target_link_libraries(materialsystem PUBLIC tier0 tier1 tier2 interfaces bitmap)
