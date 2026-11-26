#include <charconv>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string_view>
#include <vector>

#include "tier0/dbg.h"
#include "togl/vcs.h"

// tier0's command-line parser uses the compiler diagnostic hook in standalone tools.
[[noreturn]] void MdlErrorV(std::string_view fmt, std::format_args args) { ErrorV(fmt, args); }

int main(int argc, char **argv)
{
    std::filesystem::path output;
    uint32_t samples = 0;
    std::optional<uint32_t> staticID, dynamicID;
    std::vector<std::filesystem::path> files;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view option = argv[i];
        if (option == "-o" && i + 1 < argc)
            output = argv[++i];
        else if (option == "-n" || option == "-s" || option == "-d")
        {
            uint32_t value;
            const std::string_view arg = ++i < argc ? argv[i] : "";
            const auto end = arg.data() + arg.size();
            const auto result = std::from_chars(arg.data(), end, value);
            if (result.ec != std::errc{} || result.ptr != end)
            {
                std::println(stderr, "{} requires a nonnegative integer", option);
                return 2;
            }
            if (option == "-n") samples = value;
            else if (option == "-s") staticID = value;
            else dynamicID = value;
        }
        else
            files.emplace_back(argv[i]);
    }
    if (files.empty() || (samples && staticID) || (dynamicID && (!staticID || !output.empty())))
    {
        std::println(stderr, "usage: vcs2glsl [-o <dir>] [-n <count> | -s <static ID>] <file.vcs>...\n"
                             "       vcs2glsl -s <static ID> -d <dynamic ID> <file.vcs>... > shader.glsl");
        return 2;
    }
    bool ok = true;
    for (const auto &file : files)
    {
        if (!dynamicID)
            ok &= togl::ExportVcsShaders(file, output, samples, staticID);
        else
            try
            {
                std::print("{}", togl::LoadVcsShader(file, *staticID, *dynamicID));
            }
            catch (const std::exception &error)
            {
                std::println(stderr, "{}: {}", file.string(), error.what());
                ok = false;
            }
    }
    return ok ? 0 : 1;
}
