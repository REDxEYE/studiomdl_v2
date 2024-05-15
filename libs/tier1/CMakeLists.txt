cmake_minimum_required(VERSION 3.22)
project(tier1)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(tier1
        exprevaluator.cpp
        characterset.cpp
        checksum_crc.cpp
        checksum_md5.cpp
        checksum_sha1.cpp
        circularbuffer.cpp
        commandbuffer.cpp
        convar.cpp
        datamanager.cpp
        fileio.cpp
        generichash.cpp
        interface.cpp
        keyvalues.cpp
        keyvaluesjson.cpp
        kvpacker.cpp
        lzmaDecoder.cpp
        lzss.cpp
        sparsematrix.cpp
        splitstring.cpp
        stringpool.cpp
        strtools.cpp
        strtools_unicode.cpp
        tier1.cpp
        tier1_logging.cpp
        timeutils.cpp
        undiff.cpp
        memstack.cpp
        mempool.cpp
        uniqueid.cpp
        utlbuffer.cpp
        utlbufferutil.cpp
        utlsoacontainer.cpp
        utlstring.cpp
        utlstringtoken.cpp
        utlsymbol.cpp
        )
target_include_directories(tier1 PUBLIC ../../include)
target_include_directories(tier1 PRIVATE ../../include/tier1)
target_compile_definitions(tier1 PRIVATE -DSTATIC_TIER0)
target_link_libraries(tier1 PRIVATE tier0 interfaces vstdlib_static mathlib rpcrt4)
