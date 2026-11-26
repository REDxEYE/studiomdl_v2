# The model builder API (STUDIOMDL_API_PLAN.md). Sources and headers are globbed so every part can add files
# without editing this file; re-run CMake (or rely on CONFIGURE_DEPENDS) after adding one.
file(GLOB_RECURSE SMDL_API_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE SMDL_API_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)

add_library(studiomdl_api OBJECT ${SMDL_API_SOURCES} ${SMDL_API_HEADERS})
target_include_directories(studiomdl_api
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# dllexport on Windows (smdl_common.h); the exported symbols are the smdl_* functions only
target_compile_definitions(studiomdl_api PRIVATE SMDL_BUILDING)
target_link_libraries(studiomdl_api PUBLIC studiomdl_core)

# The same sources for the static library, compiled with SMDL_STATIC (SMDL_API empty: no dllexport on Windows, so a host
# executable doesn't export smdl_*; with the default hidden visibility the symbols stay hidden on Linux too)
add_library(studiomdl_api_static OBJECT ${SMDL_API_SOURCES})
target_include_directories(studiomdl_api_static
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(studiomdl_api_static PUBLIC SMDL_STATIC)
target_link_libraries(studiomdl_api_static PUBLIC studiomdl_core)

# studiomdl as a library (studiomdl.dll / libstudiomdl.so): the compiler plus the API, only smdl_* exported
# (hidden visibility by default, --exclude-libs for the static libraries, a version script on Linux).
add_library(studiomdl_shared SHARED)
target_link_libraries(studiomdl_shared PRIVATE studiomdl_api studiomdl_core)
target_include_directories(studiomdl_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(studiomdl_shared PROPERTIES
        OUTPUT_NAME studiomdl
        LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
if (WIN32)
    set_target_properties(studiomdl_shared PROPERTIES PREFIX "")  # studiomdl.dll, also with MinGW
endif ()
if (NOT MSVC AND NOT APPLE)
    # Every symbol must resolve inside the library (shared libraries allow undefined ones by default on Linux; the
    # Apple linker reports them by default and MSVC always does)
    target_link_options(studiomdl_shared PRIVATE LINKER:--no-undefined)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(studiomdl_shared PRIVATE LINKER:--exclude-libs,ALL
            LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/smdl_exports.map)
    set_property(TARGET studiomdl_shared APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/smdl_exports.map)
endif ()

# The same library as a static archive (libstudiomdl.a / studiomdl.lib) for another CMake project. Built from the same
# compiler objects as the shared library (the API objects are recompiled with SMDL_STATIC). Linking it gives the host:
# - SMDL_STATIC (SMDL_API expands to nothing) and the include directory of the C/C++ API headers, nothing else of ours
#   (the Valve include directories stay private);
# - every object of studiomdl_core and studiomdl_api_static, not only the ones the host's calls reference: element factories
#   and other self-registering objects (include/mdlobjects/mdlobjects.cpp) are referenced by nothing and a static
#   archive would drop them, which changes the output silently;
# - the bundled static libraries (tier0 ... vphys_lite, meshoptimizer) through the link interface.
add_library(studiomdl_static STATIC)
target_link_libraries(studiomdl_static
        PRIVATE studiomdl_api_static studiomdl_core
        INTERFACE $<TARGET_OBJECTS:studiomdl_api_static> $<TARGET_OBJECTS:studiomdl_core>)
target_compile_definitions(studiomdl_static PUBLIC SMDL_STATIC)
target_include_directories(studiomdl_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(studiomdl_static PROPERTIES
        # studiomdl.lib is the import library of studiomdl.dll with MSVC
        OUTPUT_NAME $<IF:$<BOOL:${MSVC}>,studiomdl_static,studiomdl>
        ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_library(studiomdl::studiomdl ALIAS studiomdl_shared)
add_library(studiomdl::static ALIAS studiomdl_static)

if (NOT PROJECT_IS_TOP_LEVEL)
    # A host that links studiomdl::static doesn't need the shared library built with `all`
    set_target_properties(studiomdl_shared PROPERTIES EXCLUDE_FROM_ALL ON)
endif ()

# Usage examples of the C++ API (built at top level only, so they keep compiling and IDEs know their flags)
if (PROJECT_IS_TOP_LEVEL)
    add_executable(smdl_example_cube examples/cube.cpp)
    target_link_libraries(smdl_example_cube PRIVATE studiomdl_static)
    set_target_properties(smdl_example_cube PROPERTIES CXX_STANDARD 20 RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    add_executable(smdl_example_many_flexes examples/many_flexes.cpp)
    target_link_libraries(smdl_example_many_flexes PRIVATE studiomdl_static)
    set_target_properties(smdl_example_many_flexes PROPERTIES CXX_STANDARD 20 RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif ()
