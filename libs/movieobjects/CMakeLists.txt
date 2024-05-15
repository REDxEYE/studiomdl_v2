cmake_minimum_required(VERSION 3.22)
project(movieobjects)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(movieobjects
        dmmeshcomp.cpp
        dmetransform.cpp
        dmetrack.cpp
        dmepackoperators.cpp
        dmefaceset.cpp
        dmemakefileutils.cpp
        dmeusersettings.cpp
        dmetrackgroup.cpp
        dmegamemodel.cpp
        dmechannel.cpp
        movieobjects_interfaces.cpp
        dmelog.cpp
        dmemodel.cpp
        dmerigconstraintoperators.cpp
        dmeaxissystem.cpp
        dmecamera.cpp
        dmetimeframe.cpp
        dmecontrolgroup.cpp
        dmeoperator.cpp
        dmeunpackoperators.cpp
        dmeconnectionoperator.cpp
        dmecycle.cpp
        dmeexpressionoperator.cpp
        dmeoverlay.cpp
        dmobjserializer.cpp
        dmetransformoperator.cpp
        dmeexporttags.cpp
        dmemesh.cpp
        dmerig.cpp
        dmerighandle.cpp
        dmephonememapping.cpp
        exportfacialanimation.cpp
        dmetransformcontrol.cpp
        dmeselection.cpp
        dmeshape.cpp
        dmecombinationoperator.cpp
        dmeattributereference.cpp
        dmedag.cpp
        dmevertexdata.cpp
        dmejoint.cpp
        dmetimeselection.cpp
        dmeinput.cpp
        dmesound.cpp
        dmemdlmakefile.cpp
        dmematerial.cpp
        dmemorphoperator.cpp
        dmedccmakefile.cpp
        dmeanimationset.cpp
        dmemakefile.cpp
        dmeclip.cpp
        dmeflexrules.cpp
        dmeeditortypedictionary.cpp
        dmetransforminput.cpp
        dmebookmark.cpp
        dmedrawsettings.cpp
        dmeattachment.cpp
        dmetransformlist.cpp
        dmeanimationlist.cpp
        )
target_include_directories(movieobjects PUBLIC ../../include)
target_include_directories(movieobjects PRIVATE ../../include/movieobjects)
target_compile_definitions(movieobjects PRIVATE -DMOVIEOBJECTS_LIB -DSTATIC_TIER0)
target_link_libraries(movieobjects PRIVATE datamodel)
