cmake_minimum_required(VERSION 3.22)
project(appframework)

set(CMAKE_CXX_STANDARD 23)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(appframework app.cpp)
target_include_directories(appframework PUBLIC  ../../include)
target_compile_definitions(appframework PRIVATE -DSTATIC_TIER0)
target_link_libraries(appframework PRIVATE tier0)