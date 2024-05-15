cmake_minimum_required(VERSION 3.22)
project(tier2)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(tier2
        defaultfilesystem.cpp
        fileutils.cpp
        interval.cpp
        meshutils.cpp
        resourceprecacher.cpp
        riff.cpp
        socketcreator.cpp
        soundutils.cpp
        tier2.cpp
        tier2_logging.cpp
        tokenreader.cpp
        util_init.cpp
        utlstreambuffer.cpp
        vconfig.cpp
        )
target_include_directories(tier2 PUBLIC ../../include)
target_include_directories(tier2 PRIVATE ../../include/tier2)
target_compile_definitions(tier2 PRIVATE -DSTATIC_TIER0)
target_link_libraries(tier2 PUBLIC tier0 tier1)