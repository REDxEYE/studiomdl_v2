cmake_minimum_required(VERSION 3.22)
project(bitmap)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(bitmap
        bitmap.cpp
        floatbitmap.cpp
        floatbitmap2.cpp
        floatbitmap3.cpp
        floatbitmap4.cpp
        floatbitmap_bilateralfilter.cpp
        ImageByteSwap.cpp
        imageformat.cpp
        psd.cpp
        resample.cpp
        texturepacker.cpp
        tgaloader.cpp
        tgawriter.cpp
        )
target_include_directories(bitmap PUBLIC ../../include)
target_include_directories(bitmap PUBLIC ../../include/bitmap)
target_compile_definitions(bitmap PRIVATE -DSTATIC_TIER0)
target_link_libraries(bitmap PUBLIC)
