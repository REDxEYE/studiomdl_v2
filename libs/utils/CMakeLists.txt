cmake_minimum_required(VERSION 3.22)
project(utils)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(utils
        cmdlib.cpp
        datalinker.cpp
        filesystem_tools.cpp
        filesystem_helpers.cpp
        scriplib.cpp
        studio.cpp
        collisionutils.cpp
        phonemeconverter.cpp
        interpolatortypes.cpp
        physdll.cpp
        zip_utils.cpp
        )
target_include_directories(utils PUBLIC ../../include)
target_include_directories(utils PRIVATE ../../include/common)
target_compile_definitions(utils PRIVATE -DSTATIC_TIER0)
target_link_libraries(utils PRIVATE tier0)
