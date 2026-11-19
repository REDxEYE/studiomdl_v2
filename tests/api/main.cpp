// smdl_api_tests [--list] [pattern...]: runs the self-registered API tests (harness.hpp)
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "harness.hpp"

int main(int argc, char **argv) {
    std::vector<std::string> patterns;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            list = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("usage: smdl_api_tests [--list] [pattern...]\n"
                        "  pattern: test name with * and ? (e.g. core_*); a leading '-' excludes (-flex_*)\n");
            return 0;
        } else {
            patterns.emplace_back(argv[i]);
        }
    }
    if (list) {
        std::vector<std::string> names;
        for (const TestCase &test: TestRegistry())
            names.push_back(test.name);
        std::sort(names.begin(), names.end());
        for (const std::string &name: names)
            std::printf("%s\n", name.c_str());
        return 0;
    }
    return RunTests(patterns);
}
