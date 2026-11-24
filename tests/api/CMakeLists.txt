# API tests: self-registering equivalence tests for the model builder (tests/api/harness.hpp).
# Sources are globbed: every part adds tests/api/test_<part>.cpp without editing this file (re-run CMake).
file(GLOB_RECURSE SMDL_API_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE SMDL_API_TEST_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_executable(smdl_api_tests ${SMDL_API_TEST_SOURCES} ${SMDL_API_TEST_HEADERS})
# The C++ API is implemented in the library and not exported by the shared one: the tests link the static library
# (the C API is covered against the shared library by tests/api_c)
target_link_libraries(smdl_api_tests PRIVATE studiomdl_static)
target_include_directories(smdl_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(smdl_api_tests PRIVATE STATIC_TIER0)
set_target_properties(smdl_api_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
# The reference compiler (CompileQcReference) and the fixtures are found through paths baked in at build time
target_compile_definitions(smdl_api_tests PRIVATE
        SMDL_TEST_STUDIOMDL_V2="$<TARGET_FILE:studiomdl_v2>"
        SMDL_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_dependencies(smdl_api_tests studiomdl_v2)

# One ctest entry runs the whole suite; filter names with `smdl_api_tests <glob>...` (e.g. `core_*`, `-flex_*`)
add_test(NAME api COMMAND smdl_api_tests)
if (STUDIOMDL_SANITIZE)
    set_tests_properties(api PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif ()
