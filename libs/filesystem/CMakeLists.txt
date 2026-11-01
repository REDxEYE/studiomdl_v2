cmake_minimum_required(VERSION 3.22)
project(filesystem_stdio_v2)

set(CMAKE_CXX_STANDARD 20)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC _AMD64_)
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
    endif ()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_DEFINES LINUX COMPILER_GCC POSIX _AMD64_)
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(PLATFORM_DEFINES LINUX COMPILER_GCC POSIX _X86_)
    endif ()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Do something on macOS
else()
    # Do something on other operating systems
endif()

# Lowest level file access (std::filebuf / std::filesystem only); also used by utils
add_library(filesystem_stdfile STATIC stdfile.cpp)
target_include_directories(filesystem_stdfile PUBLIC ../../include)

add_library(filesystem_stdio_v2_static
        STATIC
        basefilesystem.cpp
        filesystem_stdio.cpp
        )

target_compile_definitions(filesystem_stdio_v2_static PRIVATE -DDISABLE_ASYNC -DSTATIC_TIER0)
target_include_directories(filesystem_stdio_v2_static PUBLIC ../../include)
target_include_directories(filesystem_stdio_v2_static PRIVATE ../../include/filesystem)
target_link_libraries(filesystem_stdio_v2_static PUBLIC filesystem_stdfile PRIVATE tier0 tier1 tier2 utils)