cmake_minimum_required(VERSION 3.22)
project(tier0)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()


set(VTSDLIB_SOURCE
        cvar.cpp
        keyvaluessystem.cpp
        random.cpp
        vstrtools.cpp
        vstdlib_strtools.cpp
        )

add_library(vstdlib_static ${VTSDLIB_SOURCE})

target_include_directories(vstdlib_static PUBLIC ../../include)
target_include_directories(vstdlib_static PRIVATE ../../include/vstdlib)
target_compile_definitions(vstdlib_static PRIVATE -DVSTDLIB_DLL_EXPORT -DSTATIC_LINK -DSTATIC_TIER0)
target_link_libraries(vstdlib_static PUBLIC tier0 tier1)
