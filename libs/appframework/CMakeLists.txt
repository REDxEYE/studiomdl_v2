cmake_minimum_required(VERSION 3.22)
project(appframework)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(appframework
        AppSystemGroup.cpp
        WinApp.cpp
        )
target_include_directories(appframework PUBLIC  ../../include)
target_include_directories(appframework PRIVATE ../../include/appframework)
target_compile_definitions(appframework PRIVATE -DSTATIC_TIER0)
target_link_libraries(appframework PRIVATE tier0)