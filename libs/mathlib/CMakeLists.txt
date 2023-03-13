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
        feagglomerator.cpp
        disjoint_set_forest.cpp
        camera.cpp
        bumpvects.cpp
        imagequant.cpp
        capsule.cpp
        lightdesc.cpp
        almostequal.cpp
        ssenoise.cpp
        femodel.cpp
        polygon.cpp
        femodeldesc.cpp
        kdop.cpp
        sseconst.cpp
        planefit.cpp
        color_conversion.cpp
        simdvectormatrix.cpp
        polyhedron.cpp
        eigen.cpp
        powsse.cpp
        expressioncalculator.cpp
        box_buoyancy.cpp
        softbody.cpp
        sparse_convolution_noise.cpp
        svd.cpp
        anorms.cpp
        mathlib_base.cpp
        halton.cpp
        spherical.cpp
        randsse.cpp
        femodelbuilder.cpp
        IceKey.cpp
        cholesky.cpp
        transform.cpp
        dynamictree.cpp
        softbodyenvironment.cpp
        quantize.cpp
        simplex.cpp
        sphere.cpp
        vmatrix.cpp
        volumeculler.cpp
        )
target_include_directories(mathlib PUBLIC ../../include)
target_include_directories(mathlib PRIVATE ../../include/mathlib)
target_compile_definitions(mathlib PRIVATE -DSTATIC_TIER0)
target_link_libraries(mathlib PRIVATE tier0)
