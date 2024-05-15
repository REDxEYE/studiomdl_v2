cmake_minimum_required(VERSION 3.22)
project(mdlobjects)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(mdlobjects
        dmehitboxset.cpp
        dmemdllist.cpp
        dmematerialgrouplist.cpp
        dmeelementgroup.cpp
        dmebodypart.cpp
        dmehitbox.cpp
        dmeskinner.cpp
        dmemouth.cpp
        dmeanimblocksize.cpp
        dmemotioncontrol.cpp
        dmejigglebone.cpp
        dmeproceduralbone.cpp
        dmehitboxsetlist.cpp
        dmematsysroot.cpp
        dmeincludemodellist.cpp
        dmesequencelist.cpp
        dmeboneweight.cpp
        dmeasset.cpp
        mpp_utils.cpp
        dmebodygroup.cpp
        dmeblankbodypart.cpp
        dmeassemblycommand.cpp
        dmeposeparameter.cpp
        dmeeyeballglobals.cpp
        dmebonemask.cpp
        dmeik.cpp
        dmelod.cpp
        dmematerialgroup.cpp
        dmeanimcmd.cpp
        dmeboneflexdriver.cpp
        dmecollisionjoints.cpp
        dmebonemasklist.cpp
        dmeanimationassemblycommand.cpp
        dmeposeparameterlist.cpp
        dmelodlist.cpp
        dmecollisionmodel.cpp
        dmebbox.cpp
        physmodelsource.cpp
        dmesequence.cpp
        dmedefinebonelist.cpp
        dmebodygrouplist.cpp
        dmeeyelid.cpp
        dmeeyeball.cpp
        dmedefinebone.cpp
        )
target_include_directories(mdlobjects PUBLIC ../../include)
target_include_directories(mdlobjects PRIVATE ../../include/mdlobjects)
target_compile_definitions(mdlobjects PRIVATE -DSTATIC_TIER0)
target_link_libraries(mdlobjects PRIVATE)
