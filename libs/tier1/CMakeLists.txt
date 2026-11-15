cmake_minimum_required(VERSION 3.22)
project(tier1)

set(CMAKE_CXX_STANDARD 23)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(tier1
        exprevaluator.cpp
        characterset.cpp
        checksum_crc.cpp
        datamanager.cpp
        generichash.cpp
        keyvalues.cpp
        strtools.cpp
        tier1_logging.cpp
        timeutils.cpp
        memstack.cpp
        mempool.cpp
        uniqueid.cpp
        utlbuffer.cpp
        utlbufferutil.cpp
        utlstring.cpp
        utlsymbol.cpp
        function_timing.cpp
        ../../include/tier1_v2/fixed_vector.hpp
        ../../include/tier1_v2/function_timing.hpp
)
target_include_directories(tier1 PUBLIC ../../include)
target_include_directories(tier1 PRIVATE ../../include/tier1)
target_compile_definitions(tier1 PRIVATE -DSTATIC_TIER0)
target_link_libraries(tier1 PRIVATE tier0 vstdlib_static mathlib)
if (WIN32)
    target_link_libraries(tier1 PRIVATE rpcrt4)
endif ()
