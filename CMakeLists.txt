cmake_minimum_required(VERSION 3.22)
project(studiomdl_v2)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy / clangd
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC _AMD64_)
        if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            add_compile_options(/Ox /Oi /GL /GF /Gy)
        endif ()
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
    endif ()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_DEFINES LINUX COMPILER_GCC POSIX _AMD64_)
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(PLATFORM_DEFINES LINUX COMPILER_GCC POSIX _X86_)
    endif ()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Do something on macOS
else ()
    # Do something on other operating systems
endif ()

foreach (flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    STRING(REGEX REPLACE "/RTC[^ ]*" "" ${flag_var} "${${flag_var}}")
endforeach (flag_var)


add_compile_definitions(${PLATFORM_DEFINES} RAD_TELEMETRY_DISABLED _CRT_SECURE_NO_WARNINGS USE_MEM_DEBUG=0 _DLL_EXT=.dll)

# -DSTUDIOMDL_SANITIZE=ON: AddressSanitizer + UndefinedBehaviorSanitizer for every target (GCC/clang), run ctest on that build
option(STUDIOMDL_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if (STUDIOMDL_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif ()

# -DSTUDIOMDL_WARNINGS=ON: -Wall -Wextra for every target; tests/warnings_report.py compares a clean build log
# against tests/warnings_baseline.json
option(STUDIOMDL_WARNINGS "Build with -Wall -Wextra" OFF)
if (STUDIOMDL_WARNINGS AND NOT MSVC)
    add_compile_options(-Wall -Wextra)
endif ()

# Always on: passing a non-trivially-copyable object (e.g. vstd::FixedString without .c_str()) through "..." is an
# error with clang (the user IDE and Windows builds) and a warning with GCC, whose flag also covers unrelated
# function-pointer casts. memset/memcpy on non-trivial types is reported by -DSTUDIOMDL_WARNINGS=ON (-Wclass-memaccess).
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wconditionally-supported>)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Werror=non-pod-varargs>)
endif ()

add_subdirectory(libs/appframework)
add_subdirectory(libs/meshoptimizer)
add_subdirectory(libs/tier0)
add_subdirectory(libs/tier1)
add_subdirectory(libs/tier2)
add_subdirectory(libs/vstdlib)
add_subdirectory(libs/mathlib)
add_subdirectory(libs/datamodel)
add_subdirectory(libs/movieobjects)
add_subdirectory(libs/mdlobjects)
add_subdirectory(libs/dmserializers)
add_subdirectory(libs/dmeutils)
add_subdirectory(libs/utils)
add_subdirectory(libs/bonesetup)
add_subdirectory(libs/filesystem)
add_subdirectory(libs/vphys_lite)


add_executable(studiomdl_v2
        studiomdl/dmxsupport.cpp
        studiomdl/mrmsupport.cpp
        studiomdl/v1support.cpp
        studiomdl/perfstats.cpp
        studiomdl/hardwarematrixstate.cpp
        studiomdl/hardwarevertexcache.cpp
        studiomdl/objsupport.cpp
        studiomdl/optimize.cpp
        studiomdl/optimize_subd.cpp
        studiomdl/simplify.cpp
        studiomdl/tristrip.cpp
        studiomdl/UnifyLODs.cpp
        studiomdl/write.cpp
        studiomdl/flexoptimize.cpp
        studiomdl/studiomdl.cpp
        studiomdl/filesystem_init.cpp
        studiomdl/collisionmodel.cpp
        studiomdl/collisionmodelsource.cpp
        studiomdl/studiomdl_commands.cpp
        studiomdl/studiomdl_errors.cpp
        studiomdl/studiomdl_app.cpp
        studiomdl/studiomdl_app.h
        include/studiomdl/studiomdl_global_state.hpp
        studiomdl/mppsupport.cpp
        studiomdl/output_files.cpp
        include/studiomdl/output_files.hpp
        studiomdl/weight_fit.cpp
        include/studiomdl/weight_fit.hpp
        # pulls every mdlobjects element factory out of the static library (.mpp elements are created by type name)
        include/mdlobjects/mdlobjects.cpp
        studiomdl/studiomdl_helpers.cpp
        include/studiomdl/studiomdl_helpers.hpp
)
set_property(TARGET studiomdl_v2 APPEND_STRING PROPERTY COMPILE_FLAGS)

target_compile_definitions(studiomdl_v2 PRIVATE -DSTATIC_TIER0)
target_compile_definitions(studiomdl_v2 PRIVATE -DMDLCOMPILE)
#target_compile_definitions(studiomdl_v2 PRIVATE -DFBX_SUPPORT)
target_include_directories(studiomdl_v2 PUBLIC include)
target_link_libraries(studiomdl_v2 PRIVATE
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
        dmeutils
        appframework
        meshoptimizer
        filesystem_stdio_v2_static
        vphys_lite
)
if (WIN32)
    # shell32: CommandLineToArgvW for the UTF-8 command line
    target_link_libraries(studiomdl_v2 PRIVATE shlwapi shell32)
endif ()
if (MINGW)
    # libstdc++ keeps the Windows console part of std::print (__open_terminal, __write_to_terminal) in libstdc++exp
    target_link_libraries(studiomdl_v2 PRIVATE stdc++exp)
endif ()

add_executable(dmxconvert
        dmxconvert/dmxconvert.cpp
        studiomdl/filesystem_init.cpp
        studiomdl/studiomdl_errors.cpp
        studiomdl/studiomdl_helpers.cpp
        include/studiomdl/studiomdl_helpers.hpp
)

set_property(TARGET dmxconvert APPEND_STRING PROPERTY COMPILE_FLAGS)
target_compile_definitions(dmxconvert PRIVATE -DSTATIC_TIER0)
target_include_directories(dmxconvert PUBLIC include)
target_link_libraries(dmxconvert PRIVATE
        tier0
        tier1
        tier2
        appframework
        datamodel
        dmserializers
        mathlib
        filesystem_stdio_v2_static
)
if (MINGW)
    # studiomdl_errors.cpp uses std::print, see studiomdl_v2 above
    target_link_libraries(dmxconvert PRIVATE stdc++exp)
endif ()

option(STUDIOMDL_BUILD_TESTS "Build the tests (ctest)" ON)
if (STUDIOMDL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
