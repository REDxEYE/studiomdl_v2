cmake_minimum_required(VERSION 3.24)
project(studiomdl_v2)

# This repository is also a library: add_subdirectory() / FetchContent it and link studiomdl::static (or
# studiomdl::studiomdl, the shared library), see docs/LIBRARY.md. Tests, the command line tools and dmxconvert are only
# built by default when this is the top level project.
option(STUDIOMDL_BUILD_TESTS "Build the tests (ctest)" ${PROJECT_IS_TOP_LEVEL})
option(STUDIOMDL_BUILD_CLI "Build the studiomdl_v2 and dmxconvert executables" ${PROJECT_IS_TOP_LEVEL})

# Variables set here (language standard, visibility, ...) are scoped to this directory: they don't leak into a parent
# project that add_subdirectory()s us.
set(CMAKE_CXX_STANDARD 23)
if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy / clangd
endif ()
# Every static library is also linked into the studiomdl shared library (studiomdl/CMakeLists.txt): position
# independent code, and symbols hidden unless exported explicitly
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
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
# meshoptimizer: only optimizeVertexCache is used (libs/meshoptimizer is a vendored subset of 1.3). A host project that
# already has the target (pylib fetches 1.2: same function, same signature, same vcacheoptimizer.cpp) keeps its own, as
# long as it is declared before this directory.
if (NOT TARGET meshoptimizer)
    add_subdirectory(libs/meshoptimizer)
endif ()
add_subdirectory(libs/tier0)
add_subdirectory(libs/tier1)
add_subdirectory(libs/tier2)
add_subdirectory(libs/vstdlib)
add_subdirectory(libs/mathlib)
add_subdirectory(libs/datamodel)
add_subdirectory(libs/movieobjects)
add_subdirectory(libs/mdlobjects)
add_subdirectory(libs/dmserializers)
add_subdirectory(libs/utils)
add_subdirectory(libs/bonesetup)
add_subdirectory(libs/filesystem)
add_subdirectory(libs/vphys_lite)
add_subdirectory(studiomdl)
add_subdirectory(api)

# dmxconvert and the studiomdl_v2 executable (studiomdl/CMakeLists.txt) are the command line tools
if (STUDIOMDL_BUILD_CLI)
    add_executable(dmxconvert
            dmxconvert/dmxconvert.cpp
            studiomdl/filesystem_init.cpp
            studiomdl/studiomdl_errors.cpp
            studiomdl/studiomdl_helpers.cpp
            include/studiomdl/studiomdl_helpers.hpp
            studiomdl/processing.cpp
            include/studiomdl/processing.hpp
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
endif ()

if (STUDIOMDL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
