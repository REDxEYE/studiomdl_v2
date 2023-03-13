cmake_minimum_required(VERSION 3.22)
project(datamodel)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(datamodel
        clipboardmanager.cpp
        datamodel.cpp
        dependencygraph.cpp
        dmattribute.cpp
        dmconnect.cpp
        dmelement.cpp
        dmelementdictionary.cpp
        dmelementfactoryhelper.cpp
        DmElementFramework.cpp
        dmserializerbinary.cpp
        dmserializerkeyvalues.cpp
        dmserializerkeyvalues2.cpp
        undomanager.cpp
        )
target_include_directories(datamodel PUBLIC ../../include)
target_include_directories(datamodel PRIVATE ../../include/datamodel)
target_compile_definitions(datamodel PRIVATE -DDATAMODEL_LIB -DSTATIC_TIER0)
target_link_libraries(datamodel PRIVATE)