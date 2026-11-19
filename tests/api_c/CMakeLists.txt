# The shared library as a C host and as a ctypes host, and the export check (docs/LIBRARY.md).

# api_c: pure C11 against the C API
add_executable(api_c_host c_host.c)
target_link_libraries(api_c_host PRIVATE studiomdl_shared)
set_target_properties(api_c_host PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if (NOT MSVC)
    target_compile_options(api_c_host PRIVATE -Wall -Wextra -pedantic)
endif ()
if (WIN32)
    # no rpath on Windows: the loader finds studiomdl.dll next to the executable
    add_custom_command(TARGET api_c_host POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:studiomdl_shared> $<TARGET_FILE_DIR:api_c_host>)
endif ()
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/out)
add_test(NAME api_c COMMAND api_c_host ${CMAKE_CURRENT_BINARY_DIR}/out/c)

# api_ctypes: the same through ctypes (how a Python host loads the library)
if (Python3_Interpreter_FOUND)
    add_test(NAME api_ctypes
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ctypes_smoke.py $<TARGET_FILE:studiomdl_shared>
                    ${CMAKE_CURRENT_BINARY_DIR}/out/ctypes)
else ()
    message(STATUS "Python 3 not found: the api_ctypes test is disabled")
endif ()

# api_exports: nothing but smdl_* in the dynamic symbol table (Linux: nm -D)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_NM)
    add_test(NAME api_exports
            COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DLIBRARY=$<TARGET_FILE:studiomdl_shared>
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
endif ()

# api_consumer: tests/consumer is a separate CMake project that add_subdirectory()s this repository (as a subproject, tests
# and CLIs off) and links studiomdl::static into a C++20 executable using the C++ wrapper; its output must be
# byte-identical to the shared-library C host's (the test runs the C host itself). The build directory is kept between
# runs: a full second build of the library the first time, incremental afterwards.
if (CMAKE_NM)
    set(SMDL_CONSUMER_NM -DNM=${CMAKE_NM})
endif ()
add_test(NAME api_consumer
        COMMAND ${CMAKE_COMMAND}
                -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/tests/consumer
                -DBINARY_DIR=${PROJECT_BINARY_DIR}/consumer_build
                -DC_HOST=$<TARGET_FILE:api_c_host>
                -DGENERATOR=${CMAKE_GENERATOR}
                -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                -DC_COMPILER=${CMAKE_C_COMPILER}
                ${SMDL_CONSUMER_NM}
                -DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
                -DOSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
                -DOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
                -P ${PROJECT_SOURCE_DIR}/tests/consumer/run_consumer.cmake)
set_tests_properties(api_consumer PROPERTIES TIMEOUT 3600)

if (STUDIOMDL_SANITIZE)
    set_tests_properties(api_c api_consumer PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    if (Python3_Interpreter_FOUND)
        # python itself isn't built with ASan: the runtime must be loaded first
        set_tests_properties(api_ctypes PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:verify_asan_link_order=0")
    endif ()
endif ()
