// Runs several studiomdl command lines in one process through RunStudioMdl, as a DLL host will:
//     studiomdl_repeat_test <args of compile 1> --then <args of compile 2> [--then ...]
// Before each run it prints a "=== studiomdl_repeat run <n>" line and after it "=== studiomdl_repeat exit <rc>", so
// tests/repeat_process/run_repeat.py can split the output into per-run logs. Failed compiles are part of the
// sequence: the process stays usable after them. Each run starts in the working directory the process started in
// (the console tool changes into the QC's directory, which a separate process would not carry over).
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <vector>

#include "studiomdl_app.h"

int main(int argc, char **argv) {
    std::vector<std::vector<char *>> runs(1);
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--then") {
            runs.emplace_back();
            continue;
        }
        runs.back().push_back(argv[i]);
    }

    std::error_code ec;
    const std::filesystem::path startDirectory = std::filesystem::current_path(ec);

    int result = 0;
    for (size_t n = 0; n < runs.size(); ++n) {
        std::filesystem::current_path(startDirectory, ec);
        std::vector<char *> args{argv[0]};
        args.insert(args.end(), runs[n].begin(), runs[n].end());
        args.push_back(nullptr);

        std::printf("=== studiomdl_repeat run %zu\n", n);
        std::fflush(stdout);
        const int rc = RunStudioMdl(static_cast<int>(args.size()) - 1, args.data());
        std::fflush(stdout);
        std::printf("=== studiomdl_repeat exit %d\n", rc);
        std::fflush(stdout);
        if (rc != 0)
            result = rc;
    }
    return result;
}
