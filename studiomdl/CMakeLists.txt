# The compiler as an object library, linked into the studiomdl_v2 executable, the studiomdl shared library (api/) and
# tests/repeat_process. An object library (not a static one) so every object is linked: element factories and other
# self-registering objects would be dropped from a static archive when nothing references them.
add_library(studiomdl_core OBJECT
        dmxsupport.cpp
        mrmsupport.cpp
        v1support.cpp
        perfstats.cpp
        hardwarematrixstate.cpp
        hardwarevertexcache.cpp
        objsupport.cpp
        optimize.cpp
        optimize_subd.cpp
        simplify.cpp
        UnifyLODs.cpp
        write.cpp
        flexoptimize.cpp
        studiomdl.cpp
        compile.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/compile.hpp
        studiomdl_app.cpp
        studiomdl_app.h
        filesystem_init.cpp
        collisionmodel.cpp
        collisionmodelsource.cpp
        studiomdl_commands.cpp
        studiomdl_errors.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/studiomdl_global_state.hpp
        mppsupport.cpp
        output_files.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/output_files.hpp
        weight_fit.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/weight_fit.hpp
        # pulls every mdlobjects element factory out of the static library (.mpp elements are created by type name)
        ${PROJECT_SOURCE_DIR}/include/mdlobjects/mdlobjects.cpp
        studiomdl_helpers.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/studiomdl_helpers.hpp
        processing.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/processing.hpp
        # builder API apply layer (shared with the QC parsers)
        apply_globals.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/apply_globals.hpp
        apply_bones.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/apply_bones.hpp
        apply_body.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/apply_body.hpp
        apply_anim.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/apply_anim.hpp
        apply_lod.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/apply_lod.hpp
        apply_flex.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/apply_flex.hpp
        apply_bone_dynamics.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/apply_bone_dynamics.hpp
        # command line switches shared by the console tool and the library (QC through the API)
        command_line.cpp
        ${PROJECT_SOURCE_DIR}/include/studiomdl/command_line.hpp
)
# Users of studiomdl_core compile against the same configuration and get its objects and libraries
target_compile_definitions(studiomdl_core PUBLIC STATIC_TIER0 MDLCOMPILE)
#target_compile_definitions(studiomdl_core PUBLIC FBX_SUPPORT)
target_include_directories(studiomdl_core PUBLIC ${PROJECT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studiomdl_core PUBLIC
        tier0
        tier1
        tier2
        mathlib
        utils
        datamodel
        dmserializers
        movieobjects
        bonesetup
        mdlobjects
        appframework
        meshoptimizer
        filesystem_stdio_v2_static
        vphys_lite
)
# The API serializes compiles with std::mutex and std::call_once (api/src/compile_driver.hpp). With a statically linked
# libstdc++ on glibc older than 2.34 nothing else pulls in libpthread: libstdc++ then treats threads as inactive and
# std::call_once throws "Unknown error -1"
find_package(Threads REQUIRED)
target_link_libraries(studiomdl_core PUBLIC Threads::Threads)
if (WIN32)
    # shell32: CommandLineToArgvW for the UTF-8 command line
    target_link_libraries(studiomdl_core PUBLIC shlwapi shell32)
endif ()
if (MINGW)
    # libstdc++ keeps the Windows console part of std::print (__open_terminal, __write_to_terminal) in libstdc++exp
    target_link_libraries(studiomdl_core PUBLIC stdc++exp)
endif ()

# The command line compiler; the tests need it too (reference compiles, golden, repeat_process)
if (STUDIOMDL_BUILD_CLI OR STUDIOMDL_BUILD_TESTS)
    add_executable(studiomdl_v2 main.cpp)
    target_link_libraries(studiomdl_v2 PRIVATE studiomdl_core)
    set_target_properties(studiomdl_v2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif ()
