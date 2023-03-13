cmake_minimum_required(VERSION 3.22)
project(resourcefile)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(resourcefile
        resourcedictionary.cpp
        resourcefile.cpp
        resourceintrospection.cpp
        resourcestream.cpp
        )
target_include_directories(resourcefile PUBLIC ../../include)
target_include_directories(resourcefile PRIVATE ../../include/resourcefile)
target_compile_definitions(resourcefile PRIVATE -DSTATIC_TIER0)
target_link_libraries(resourcefile PRIVATE tier0)
