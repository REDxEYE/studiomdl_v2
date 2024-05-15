cmake_minimum_required(VERSION 3.22)
project(mathlib)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(mathlib
        sse.cpp
        disjoint_set_forest.cpp
        bumpvects.cpp
        capsule.cpp
        almostequal.cpp
        ssenoise.cpp
        polygon.cpp
        sseconst.cpp
        color_conversion.cpp
        simdvectormatrix.cpp
        polyhedron.cpp
        eigen.cpp
        powsse.cpp
        expressioncalculator.cpp
        sparse_convolution_noise.cpp
        anorms.cpp
        mathlib_base.cpp
        randsse.cpp
        transform.cpp
        simplex.cpp
        sphere.cpp
        vmatrix.cpp
)
target_include_directories(mathlib PUBLIC ../../include)
target_include_directories(mathlib PRIVATE ../../include/mathlib)
target_compile_definitions(mathlib PRIVATE -DSTATIC_TIER0)
target_link_libraries(mathlib PRIVATE tier0)
