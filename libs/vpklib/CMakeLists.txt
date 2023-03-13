cmake_minimum_required(VERSION 3.22)
project(vpklib)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()


add_library(vpklib packedstore.cpp)

target_compile_definitions(vpklib PRIVATE -DSTATIC_TIER0)
target_include_directories(vpklib PRIVATE ../../include/vpklib)
target_link_libraries(vpklib PRIVATE tier0 tier1)