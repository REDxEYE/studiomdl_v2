cmake_minimum_required(VERSION 3.22)
project(filesystem_stdio_v2)

set(additional_sources "")

set(CMAKE_CXX_STANDARD 14)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC _AMD64_)
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
    endif ()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(additional_sources "linux_support.cpp")
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

add_library(filesystem_stdio_v2
        SHARED
        ${additional_sources}
        basefilesystem.cpp
        filesystem_stdio.cpp
        )

add_library(filesystem_stdio_v2_static
        STATIC
        ${additional_sources}
        basefilesystem.cpp
        filesystem_stdio.cpp
        )

target_compile_definitions(filesystem_stdio_v2 PRIVATE -DDISABLE_ASYNC -DSTATIC_TIER0)
target_compile_definitions(filesystem_stdio_v2_static PRIVATE -DDISABLE_ASYNC -DSTATIC_TIER0)
target_include_directories(filesystem_stdio_v2 PUBLIC ../../include)
target_include_directories(filesystem_stdio_v2_static PUBLIC ../../include)
target_include_directories(filesystem_stdio_v2 PRIVATE ../../include/filesystem)
target_include_directories(filesystem_stdio_v2_static PRIVATE ../../include/filesystem)
target_link_libraries(filesystem_stdio_v2 PRIVATE tier0 tier1 tier2 utils)
target_link_libraries(filesystem_stdio_v2_static PRIVATE tier0 tier1 tier2 utils)
add_custom_command(TARGET filesystem_stdio_v2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:filesystem_stdio_v2> ${CMAKE_BINARY_DIR}/
        )

add_custom_target(copy_filesystem_stdio_v2 ALL DEPENDS filesystem_stdio_v2)