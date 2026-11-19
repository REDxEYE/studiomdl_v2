# Tests: run with `ctest` from the build directory.

# ClampMaxVerticesPerModel / CClampedSource, compiled from the real sources against the shims in clamp_split/shim.h
set(CLAMP_SPLIT_GEN ${CMAKE_CURRENT_BINARY_DIR}/clamp_split)
add_custom_command(
        OUTPUT ${CLAMP_SPLIT_GEN}/class_decl.inc ${CLAMP_SPLIT_GEN}/clamped_impl.inc ${CLAMP_SPLIT_GEN}/clamp_fn.inc
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CLAMP_SPLIT_GEN}
        COMMAND ${CMAKE_COMMAND}
                -DSRC_DIR=${PROJECT_SOURCE_DIR}/studiomdl
                -DHDR=${PROJECT_SOURCE_DIR}/include/studiomdl/studiomdl.h
                -DOUT_DIR=${CLAMP_SPLIT_GEN}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/clamp_split/extract.cmake
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/clamp_split/extract.cmake
                ${PROJECT_SOURCE_DIR}/studiomdl/studiomdl.cpp
                ${PROJECT_SOURCE_DIR}/include/studiomdl/studiomdl.h
        COMMENT "Extracting clamp code for the clamp_split test")

add_executable(clamp_split_test
        clamp_split/test.cpp
        ${CLAMP_SPLIT_GEN}/class_decl.inc ${CLAMP_SPLIT_GEN}/clamped_impl.inc ${CLAMP_SPLIT_GEN}/clamp_fn.inc)
target_include_directories(clamp_split_test PRIVATE clamp_split ${CLAMP_SPLIT_GEN})
set_target_properties(clamp_split_test PROPERTIES CXX_STANDARD 17)
if (NOT MSVC)
    # the extracted code is written against the engine headers; the shims are deliberately minimal
    target_compile_options(clamp_split_test PRIVATE -fpermissive -w)
endif ()
add_test(NAME clamp_split COMMAND clamp_split_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/clamp_split)

# vstd::paths / vstd string helpers (header only)
add_executable(path_utils_test path_utils/test.cpp)
target_include_directories(path_utils_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(path_utils_test PROPERTIES CXX_STANDARD 23)
add_test(NAME path_utils COMMAND path_utils_test)

# Golden output: compiles tests/golden/fixtures/*.qc and compares against fixtures/expected.json
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME golden
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/golden/run_golden.py --studiomdl $<TARGET_FILE:studiomdl_v2>)
    # Cyrillic QC, $include, SMD/VTA/DMX and $pushd paths, with the QC given by name and by absolute path
    set(GOLDEN_UNICODE_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/golden/unicode/Модели")
    add_test(NAME golden_unicode
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/golden/run_golden.py --studiomdl $<TARGET_FILE:studiomdl_v2>
                    --fixtures ${GOLDEN_UNICODE_FIXTURES})
    add_test(NAME golden_unicode_absolute
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/golden/run_golden.py --studiomdl $<TARGET_FILE:studiomdl_v2>
                    --fixtures ${GOLDEN_UNICODE_FIXTURES} --absolute-qc)

    # Several compiles in one process (BeginProcessing/EndProcessing) == the same compiles in separate processes
    add_executable(studiomdl_repeat_test repeat_process/repeat_main.cpp)
    target_link_libraries(studiomdl_repeat_test PRIVATE studiomdl_core)
    add_test(NAME repeat_process
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/repeat_process/run_repeat.py
                    --studiomdl $<TARGET_FILE:studiomdl_v2> --repeat $<TARGET_FILE:studiomdl_repeat_test>)
else ()
    message(STATUS "Python 3 not found: the golden test is disabled")
endif ()

# Model builder API tests (tests/api/harness.hpp) and the C host / ctypes / export checks (docs/LIBRARY.md). tests/ is
# also configured against older commits by tests/tools/build_ref.sh, which have no library targets.
if (TARGET studiomdl_shared)
    add_subdirectory(api)
    add_subdirectory(api_c)
endif ()

if (STUDIOMDL_SANITIZE)
    # The vendored libraries leak by design (process-lifetime singletons); report memory errors only
    set_tests_properties(clamp_split PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    if (Python3_Interpreter_FOUND)
        set_tests_properties(golden golden_unicode golden_unicode_absolute repeat_process PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    endif ()
endif ()
