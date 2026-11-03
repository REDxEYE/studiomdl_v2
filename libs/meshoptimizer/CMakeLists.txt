# meshoptimizer (https://github.com/zeux/meshoptimizer, MIT, commit f0a684f): vertex cache optimization only.
# Vendored subset: meshoptimizer.h, allocator.cpp, vcacheoptimizer.cpp (+ LICENSE.md); add more files from upstream as needed.
add_library(meshoptimizer STATIC allocator.cpp vcacheoptimizer.cpp)
target_include_directories(meshoptimizer PUBLIC .)
