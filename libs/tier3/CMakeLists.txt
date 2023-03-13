cmake_minimum_required(VERSION 3.22)
project(tier3)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(tier3
        mdlutils.cpp
        tier3.cpp
        studiohdrstub.cpp
        )
target_include_directories(tier3 PUBLIC ../../include)
target_include_directories(tier3 PRIVATE ../../include/tier3)
target_compile_definitions(tier3 PRIVATE -DSTATIC_TIER0)
target_link_libraries(tier3 PUBLIC tier0 tier1 tier2 materialsystem)