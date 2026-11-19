// Compiling an existing QC through the API (smdl_qc.h): byte-identical to the console tool, host-provided files,
// typed switches, error positions, repeated runs, smdl_run_command_line.
#include "harness.hpp"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;

    // The golden fixtures (tests/golden/fixtures): QCs with their sources below src/
    fs::path GoldenDir() { return fs::path(SMDL_TEST_FIXTURES_DIR).parent_path().parent_path() / "golden" / "fixtures"; }

    constexpr const char *kGameInfo =
            "\"GameInfo\"\n{\n\tgame \"golden\"\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tGame |gameinfo_path|.\n\t\t}\n\t}\n}\n";

    void MakeGameDir(const fs::path &dir) {
        fs::create_directories(dir);
        std::ofstream(dir / "gameinfo.txt", std::ios::binary) << kGameInfo;
    }

    std::vector<std::byte> ReadBytes(const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes(chars.size());
        std::memcpy(bytes.data(), chars.data(), chars.size());
        return bytes;
    }

    std::vector<fs::path> GoldenQcs() {
        std::vector<fs::path> qcs;
        for (const auto &entry: fs::directory_iterator(GoldenDir())) {
            if (entry.path().extension() == ".qc" && entry.path().stem() != "error_missing_source")
                qcs.push_back(entry.path());
        }
        std::ranges::sort(qcs);
        return qcs;
    }

    Outputs Compile(smdl::QcCompile &compile) { return ResultFiles(compile.run()); }

    // The QC and everything below the fixtures directory, copied to <dir>/model/
    std::map<fs::path, std::vector<std::byte>> CopyFixtureTree(const fs::path &qc, const fs::path &dir) {
        std::map<fs::path, std::vector<std::byte>> files;
        for (const auto &entry: fs::recursive_directory_iterator(GoldenDir())) {
            if (!entry.is_regular_file())
                continue;
            const fs::path relative = fs::relative(entry.path(), GoldenDir());
            if (relative.extension() == ".qc" && entry.path() != qc)
                continue;
            if (relative.filename() == "expected.json" || relative.extension() == ".py")
                continue;
            const fs::path target = dir / "model" / relative;
            fs::create_directories(target.parent_path());
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
            files[target] = ReadBytes(target);
        }
        return files;
    }
}

SMDL_TEST(qc_golden_fixtures_match_cli) {
    const auto qcs = GoldenQcs();
    Require(!qcs.empty(), "golden fixtures found");
    for (const fs::path &qc: qcs) {
        TempDir game("qcgame");
        MakeGameDir(game.path());
        smdl::QcCompile compile;
        compile.setGameDir(game.path().string()).setQcPath(qc.string());
        RequireSameOutputs(CompileQcReference(qc), Compile(compile));
    }
}

SMDL_TEST(qc_error_has_file_and_line) {
    TempDir dir("qcerr");
    const std::string qc = (dir.path() / "bad.qc").string();
    smdl::QcCompile compile;
    compile.setQcPath(qc).addFile(qc, "$modelname \"bad\"\n$cdmaterials \"\"\n$bogus_command 1\n");
    try {
        compile.run();
        Require(false, "the compile fails");
    }
    catch (const smdl::QcError &e) {
        Require(e.status() == SMDL_ERROR_COMPILE, "status is SMDL_ERROR_COMPILE");
        Require(!e.file().empty() && e.file().find("bad.qc") != std::string::npos, "error file is the QC");
        Require(e.line() == 3, "error line is 3");
    }
}

SMDL_TEST(qc_sources_only_from_add_file) {
    for (const char *name: {"smd_vta.qc", "prop_static.qc", "suzanne_binary_dmx.qc"}) {
        const fs::path qc = GoldenDir() / name;
        TempDir game("qcgame");
        MakeGameDir(game.path());
        TempDir work("qcwork");
        const auto files = CopyFixtureTree(qc, work.path());
        const fs::path workQc = work.path() / "model" / name;

        smdl::QcCompile fromDisk;
        fromDisk.setGameDir(game.path().string()).setQcPath(workQc.string());
        const Outputs expected = Compile(fromDisk);

        fs::remove_all(work.path() / "model");
        smdl::QcCompile fromMemory;
        fromMemory.setGameDir(game.path().string()).setQcPath(workQc.string());
        for (const auto &[path, data]: files)
            fromMemory.addFile(path.string(), data);
        RequireSameOutputs(expected, Compile(fromMemory));
    }
}

SMDL_TEST(qc_sources_from_reader) {
    const fs::path qc = GoldenDir() / "smd_vta.qc";
    TempDir game("qcgame");
    MakeGameDir(game.path());
    TempDir work("qcwork");
    const auto files = CopyFixtureTree(qc, work.path());
    const fs::path workQc = work.path() / "model" / "smd_vta.qc";

    smdl::QcCompile fromDisk;
    fromDisk.setGameDir(game.path().string()).setQcPath(workQc.string());
    const Outputs expected = Compile(fromDisk);

    fs::remove_all(work.path() / "model");
    std::vector<std::string> asked;
    smdl::QcCompile fromReader;
    fromReader.setGameDir(game.path().string()).setQcPath(workQc.string());
    fromReader.setReader([&](std::string_view path) -> std::optional<std::vector<std::byte>> {
        asked.emplace_back(path);
        for (const auto &[file, data]: files) {
            std::string name = file.generic_string();
#ifdef _WIN32
            // the compiler hands out paths with '/' and lowercased (ASCII) on Windows
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
#endif
            if (name == path)
                return data;
        }
        return std::nullopt;
    });
    RequireSameOutputs(expected, Compile(fromReader));
    Require(!asked.empty(), "the reader was asked");
}

SMDL_TEST(qc_typed_option_equals_argument) {
    const fs::path qc = GoldenDir() / "split_flexopt_off.qc";
    TempDir game("qcgame");
    MakeGameDir(game.path());

    smdl::QcCompile typed;
    typed.setGameDir(game.path().string()).setQcPath(qc.string()).setFlexOptimize(smdl::FlexOptimize::On);
    const Outputs a = Compile(typed);

    smdl::QcCompile args;
    args.setGameDir(game.path().string()).setQcPath(qc.string()).addArgument("-flexopt").addArgument("on");
    const Outputs b = Compile(args);

    RequireSameOutputs(a, b);
    RequireSameOutputs(CompileQcReference(qc, {"-flexopt", "on"}), b);
}

SMDL_TEST(qc_arguments_are_validated) {
    smdl::QcCompile compile;
    auto refused = [&](std::string_view argument) {
        try {
            compile.addArgument(argument);
        }
        catch (const smdl::Error &e) {
            return e.status() == SMDL_ERROR_INVALID_ARGUMENT && std::string_view(e.what()).size() > 0;
        }
        return false;
    };
    Require(refused("-makefile"), "-makefile is refused");
    Require(refused("model.qc"), "a positional argument is refused");
    Require(refused("-no_such_switch"), "an unknown switch is refused");
    // a switch waits for its value (the next call); the run reports it when it never comes
    compile.addArgument("-flexopt");
    compile.addArgument("auto");
    compile.addArgument("-nowarnings").addArgument("-maxwarnings").addArgument("5");
    compile.setFitWeights(true, 8, 8).setMinLod(3).setStripLods(true);
}

SMDL_TEST(qc_repeated_runs_and_failures_in_between) {
    const fs::path qc = GoldenDir() / "prop_static.qc";
    TempDir game("qcgame");
    MakeGameDir(game.path());
    smdl::QcCompile good;
    good.setGameDir(game.path().string()).setQcPath(qc.string());
    const Outputs first = Compile(good);

    TempDir dir("qcfail");
    const std::string badQc = (dir.path() / "bad.qc").string();
    smdl::QcCompile bad;
    bad.setQcPath(badQc).addFile(badQc, "$modelname \"bad\"\n$sequence\n");
    for (int i = 0; i < 2; ++i) {
        bool failed = false;
        try {
            bad.run();
        }
        catch (const smdl::QcError &) {
            failed = true;
        }
        Require(failed, "the failing compile fails");
        RequireSameOutputs(first, Compile(good));
    }
}

SMDL_TEST(qc_run_command_line) {
    const fs::path qc = GoldenDir() / "prop_static.qc";
    TempDir game("qcgame");
    MakeGameDir(game.path());
    std::vector<std::string> messages;
    const std::vector<std::string> args{"studiomdl", "-game", game.path().string(), qc.string()};
    const int code = smdl::runCommandLine(args, [&](smdl::Severity, std::string_view text) { messages.emplace_back(text); });
    Require(code == 0, "exit code 0");
    Require(!messages.empty(), "the log function received the output");

    // the files were written to the game directory, as the tool does
    Outputs written;
    for (const auto &entry: fs::recursive_directory_iterator(game.path() / "models")) {
        if (entry.is_regular_file())
            written[fs::relative(entry.path(), game.path()).generic_string()] = ReadBytes(entry.path());
    }
    RequireSameOutputs(CompileQcReference(qc), written);

    const std::vector<std::string> bad{"studiomdl", "-game", game.path().string(), (GoldenDir() / "error_missing_source.qc").string()};
    messages.clear();
    Require(smdl::runCommandLine(bad, [&](smdl::Severity, std::string_view text) { messages.emplace_back(text); }) != 0,
            "a failing compile has a non-zero exit code");
    Require(!messages.empty(), "the error went to the log function");
}
