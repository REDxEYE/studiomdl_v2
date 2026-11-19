# A host project that consumes studiomdl as a subproject (what pylib does): add_subdirectory() the repository and link
# studiomdl::static into a C++20 executable that uses the header-only C++ wrapper. Built by the ctest api_consumer
# (tests/consumer/run_consumer.cmake), or by hand:
#   cmake -S tests/consumer -B /tmp/consumer-build -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build /tmp/consumer-build && /tmp/consumer-build/consumer_host /tmp/consumer-out
cmake_minimum_required(VERSION 3.24)
project(studiomdl_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Where studiomdl comes from. With FetchContent this would be
#   FetchContent_Declare(studiomdl GIT_REPOSITORY <url> GIT_TAG <tag>)
#   FetchContent_MakeAvailable(studiomdl)
set(STUDIOMDL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "studiomdl_v2 source directory")

# Not top level: tests, studiomdl_v2 and dmxconvert are off by default; spelled out here for the record
set(STUDIOMDL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STUDIOMDL_BUILD_CLI OFF CACHE BOOL "" FORCE)
# EXCLUDE_FROM_ALL: build only what the targets of this project need
add_subdirectory(${STUDIOMDL_SOURCE_DIR} studiomdl EXCLUDE_FROM_ALL)

add_executable(consumer_host consumer_host.cpp)
target_link_libraries(consumer_host PRIVATE studiomdl::static)
