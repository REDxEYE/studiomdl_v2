cmake_minimum_required(VERSION 3.22)
project(dmserializers)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(dmserializers
        importcommentary.cpp
        importsfmv4.cpp
        importmks.cpp
        importsfmv5.cpp
        dmserializers.cpp
        importsfmv2.cpp
        importsfmv6.cpp
        importtex.cpp
        importsfmv7.cpp
        importsfmv8.cpp
        importactbusy.cpp
        dmebaseimporter.cpp
        importkeyvaluebase.cpp
        importvmf.cpp
        importsfmv9.cpp
        importsfmv3.cpp
        importmks.cpp
        importvmt.cpp
        importsfmv1.cpp
        )
target_include_directories(dmserializers PUBLIC ../../include)
target_include_directories(dmserializers PRIVATE ../../include/dmserializers)
target_compile_definitions(dmserializers PRIVATE -DSTATIC_TIER0)
target_link_libraries(dmserializers PRIVATE)
