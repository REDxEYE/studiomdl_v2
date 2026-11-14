cmake_minimum_required(VERSION 3.22)
project(movieobjects)

set(CMAKE_CXX_STANDARD 23)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(movieobjects
        dmmeshcomp.cpp
        dmetransform.cpp
        dmefaceset.cpp
        dmechannel.cpp
        dmelog.cpp
        dmemodel.cpp
        dmerigconstraintoperators.cpp
        dmerig.cpp
        dmerighandle.cpp
        dmemdlmakefile.cpp
        dmemakefile.cpp
        dmeaxissystem.cpp
        dmetimeframe.cpp
        dmeoperator.cpp
        dmeconnectionoperator.cpp
        dmecycle.cpp
        dmeexpressionoperator.cpp
        dmobjserializer.cpp
        dmetransformoperator.cpp
        dmeexporttags.cpp
        dmemesh.cpp
        dmeselection.cpp
        dmeshape.cpp
        dmecombinationoperator.cpp
        dmeattributereference.cpp
        dmedag.cpp
        dmevertexdata.cpp
        dmejoint.cpp
        dmematerial.cpp
        dmemorphoperator.cpp
        dmeclip.cpp
        dmeflexrules.cpp
        dmeattachment.cpp
        dmetransformlist.cpp
        dmeanimationlist.cpp
        )
target_include_directories(movieobjects PUBLIC ../../include)
target_include_directories(movieobjects PRIVATE ../../include/movieobjects)
target_compile_definitions(movieobjects PRIVATE -DMOVIEOBJECTS_LIB -DSTATIC_TIER0)
target_link_libraries(movieobjects PRIVATE datamodel)
