cmake_minimum_required(VERSION 3.22)
project(tier0)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(tier0
        platform.cpp
        pch_tier0.cpp
        mem.cpp
        logging.cpp
        dbg.cpp
        assert_dialog.cpp
        memstd.cpp
        mem_helpers.cpp
        tier0_strtools.cpp
        commandline.cpp
        memprocessheap.cpp
        memvirt.cpp
        cpu.cpp
        platwindow.cpp
        cputopology.cpp
        )
target_include_directories(tier0 PUBLIC ../../include)
target_include_directories(tier0 PRIVATE ../../include/tier0)
target_compile_definitions(tier0 PRIVATE -DTIER0_DLL_EXPORT)
target_compile_definitions(tier0 PUBLIC -DSTATIC_LINK -DSTATIC_TIER0)
