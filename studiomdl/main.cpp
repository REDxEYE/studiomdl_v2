// studiomdl_v2 executable: the console entry point around RunStudioMdl (studiomdl_app.cpp)
#include <string>
#include <vector>

#include "studiomdl/platform_compat.h"
#include "studiomdl_app.h"

int main(int argc, char** argv) {
    StudioMdlCompat::EnableUtf8Console();

    // Paths are UTF-8 everywhere inside, starting with the command line
    std::vector<std::string> args = StudioMdlCompat::Utf8Arguments(argc, argv);
    std::vector<char*> argPointers;
    argPointers.reserve(args.size() + 1);
    for (std::string &arg: args) {
        argPointers.push_back(arg.data());
    }
    argPointers.push_back(nullptr);

    return RunStudioMdl(static_cast<int>(args.size()), argPointers.data());
}
