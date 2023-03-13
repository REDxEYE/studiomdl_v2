cmake_minimum_required(VERSION 3.22)
project(vphysics)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_library(vphysics
        SHARED
        convert.cpp
        ledgewriter.cpp
        linear_solver.cpp
        physics_airboat.cpp
        physics_collide.cpp
        physics_constraint.cpp
        physics_controller_raycast_vehicle.cpp
        physics_environment.cpp
        physics_fluid.cpp
        physics_friction.cpp
        physics_material.cpp
        physics_motioncontroller.cpp
        physics_object.cpp
        physics_shadow.cpp
        physics_spring.cpp
        physics_vehicle.cpp
        physics_virtualmesh.cpp
        vphysics_saverestore.cpp
        vcollide_parse.cpp
        main.cpp
        )
target_include_directories(vphysics PUBLIC ../../include)
target_include_directories(vphysics PRIVATE ../../include/vphysics)
target_compile_definitions(vphysics PRIVATE -DSTATIC_TIER0)
target_link_libraries(vphysics PUBLIC)