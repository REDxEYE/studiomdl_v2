// Test harness: registry, assertions, temp dirs, reference compiles, output comparison (see harness.hpp)
#include "harness.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <set>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

// ---------------------------------------------------------------------------------------------------------------
// Registry and assertions
// ---------------------------------------------------------------------------------------------------------------

std::vector<TestCase> &TestRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}

TestRegistrar::TestRegistrar(const char *name, void (*function)(), const char *file, int line) {
    TestRegistry().push_back({name, function, file, line});
}

static std::string Where(const std::source_location &where) {
    return std::format("{}:{}", std::filesystem::path(where.file_name()).filename().string(), where.line());
}

void Require(bool condition, std::string_view what, std::source_location where) {
    if (!condition)
        throw TestFailure(std::format("{}: requirement failed: {}", Where(where), what));
}

void Fail(std::string_view what, std::source_location where) {
    throw TestFailure(std::format("{}: {}", Where(where), what));
}

// ---------------------------------------------------------------------------------------------------------------
// Files and temp dirs
// ---------------------------------------------------------------------------------------------------------------

std::filesystem::path FixturePath(std::string_view rel) {
    std::filesystem::path path = std::filesystem::path(SMDL_TEST_FIXTURES_DIR) / std::filesystem::path(std::u8string(rel.begin(), rel.end()));
    if (!std::filesystem::exists(path))
        throw TestFailure(std::format("fixture not found: {}", path.string()));
    return path;
}

static unsigned long ProcessId() {
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

TempDir::TempDir(std::string_view tag) {
    static std::atomic<unsigned> counter{0};
    std::string name = std::format("smdl_api_{}_{}_{}", ProcessId(), counter.fetch_add(1), tag);
    for (char &c: name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            c = '_';
    }
    m_Path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(m_Path);
    std::filesystem::create_directories(m_Path);
}

TempDir::~TempDir() {
    if (std::getenv("SMDL_TEST_KEEP"))
        return;
    std::error_code ec;
    std::filesystem::remove_all(m_Path, ec);
}

static std::vector<std::byte> ReadFileBytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw TestFailure(std::format("cannot read {}", path.string()));
    file.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<std::byte> bytes(size);
    if (size)
        file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
    return bytes;
}

Outputs ReadModelsDir(const std::filesystem::path &dir) {
    Outputs outputs;
    const std::filesystem::path models = dir / "models";
    if (!std::filesystem::exists(models))
        return outputs;
    for (const auto &entry: std::filesystem::recursive_directory_iterator(models)) {
        if (!entry.is_regular_file())
            continue;
        const std::string key = "models/" + std::filesystem::relative(entry.path(), models).generic_string();
        outputs[key] = ReadFileBytes(entry.path());
    }
    return outputs;
}

Outputs ResultFiles(const smdl::Result &result) {
    Outputs outputs;
    for (const smdl::OutputFile &file: result.files()) {
        outputs[std::string(file.relativePath)].assign(file.data.begin(), file.data.end());
    }
    return outputs;
}

// ---------------------------------------------------------------------------------------------------------------
// Reference compiles
// ---------------------------------------------------------------------------------------------------------------

// Same gameinfo.txt as tests/golden/run_golden.py
static constexpr const char *kGameInfo =
        "\"GameInfo\"\n{\n\tgame \"golden\"\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tGame |gameinfo_path|.\n\t\t}\n\t}\n}\n";

// Runs a program with its output in logFile; returns the exit code (-1: could not start / abnormal end)
static int RunProgram(const std::filesystem::path &program, const std::vector<std::string> &args,
                      const std::filesystem::path &cwd, const std::filesystem::path &logFile) {
#ifdef _WIN32
    auto quote = [](const std::wstring &arg) {
        std::wstring out = L"\"";
        size_t backslashes = 0;
        for (wchar_t c: arg) {
            if (c == L'\\') {
                ++backslashes;
                continue;
            }
            if (c == L'"') {
                out.append(backslashes * 2 + 1, L'\\');
                out += c;
            } else {
                out.append(backslashes, L'\\');
                out += c;
            }
            backslashes = 0;
        }
        out.append(backslashes * 2, L'\\');
        return out + L"\"";
    };
    std::wstring commandLine = quote(program.wstring());
    for (const std::string &arg: args)
        commandLine += L" " + quote(std::filesystem::path(std::u8string(arg.begin(), arg.end())).wstring());

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW(logFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE)
        return -1;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION info{};
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, cwd.c_str(),
                                        &startup, &info);
    CloseHandle(log);
    if (!started)
        return -1;
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return static_cast<int>(code);
#else
    // Everything the child needs is prepared before fork (only async-signal-safe calls in the child)
    const std::string programString = program.string();
    const std::string cwdString = cwd.string();
    const std::string logString = logFile.string();
    std::vector<std::string> storage;
    storage.push_back(programString);
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char *> argv;
    for (std::string &s: storage)
        argv.push_back(s.data());
    argv.push_back(nullptr);

    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        const int fd = open(logString.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || chdir(cwdString.c_str()) != 0)
            _exit(126);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        execv(programString.c_str(), argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
#endif
}

ReferenceRun RunReference(const std::filesystem::path &qc, const std::vector<std::string> &extraArgs) {
    const std::filesystem::path absoluteQc = std::filesystem::absolute(qc);
    if (!std::filesystem::exists(absoluteQc))
        throw TestFailure(std::format("QC not found: {}", absoluteQc.string()));

    TempDir game("game");
    {
        std::ofstream info(game.path() / "gameinfo.txt", std::ios::binary);
        info << kGameInfo;
    }
    const std::filesystem::path log = game.path() / "compile.log";

    std::vector<std::string> args{"-game", game.path().string()};
    args.insert(args.end(), extraArgs.begin(), extraArgs.end());
    args.push_back(absoluteQc.filename().string());

    ReferenceRun run;
    run.exitCode = RunProgram(SMDL_TEST_STUDIOMDL_V2, args, absoluteQc.parent_path(), log);
    if (std::filesystem::exists(log)) {
        std::ifstream in(log, std::ios::binary);
        run.log.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    run.outputs = ReadModelsDir(game.path());
    return run;
}

Outputs CompileQcReference(const std::filesystem::path &qc, std::vector<std::string> extraArgs) {
    ReferenceRun run = RunReference(qc, extraArgs);
    if (run.exitCode != 0) {
        constexpr size_t kTail = 4000;
        const std::string tail = run.log.size() > kTail ? "..." + run.log.substr(run.log.size() - kTail) : run.log;
        throw TestFailure(std::format("studiomdl_v2 failed on {} (exit code {}):\n{}", qc.filename().string(),
                                      run.exitCode, tail));
    }
    return std::move(run.outputs);
}

std::string QcPathEntry(const std::filesystem::path &qc) {
    const std::filesystem::path full = std::filesystem::absolute(qc).lexically_normal();
#ifdef _WIN32
    // The compiler records the QC's directory lowercased (ASCII) with backslashes and keeps the file name as it is
    const auto utf8 = [](const std::filesystem::path &part) {
        const std::u8string text = part.generic_u8string();
        return std::string(text.begin(), text.end());
    };
    std::string directory = utf8(full.parent_path());
    for (char &c: directory)
        c = c == '/' ? '\\' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    return directory + '\\' + utf8(full.filename());
#else
    return full.generic_string();
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Output comparison
// ---------------------------------------------------------------------------------------------------------------

namespace {
    // studiohdr_t (include/studio.h) in declaration order; the offsets are the running sum of the sizes
    struct HeaderField {
        const char *name;
        uint32_t size;
    };

    constexpr HeaderField kHeaderFields[] = {
            {"id", 4}, {"version", 4}, {"checksum", 4}, {"name", 64}, {"length", 4},
            {"eyeposition", 12}, {"illumposition", 12}, {"hull_min", 12}, {"hull_max", 12},
            {"view_bbmin", 12}, {"view_bbmax", 12}, {"flags", 4},
            {"numbones", 4}, {"boneindex", 4},
            {"numbonecontrollers", 4}, {"bonecontrollerindex", 4},
            {"numhitboxsets", 4}, {"hitboxsetindex", 4},
            {"numlocalanim", 4}, {"localanimindex", 4},
            {"numlocalseq", 4}, {"localseqindex", 4},
            {"activitylistversion", 4}, {"eventsindexed", 4},
            {"numtextures", 4}, {"textureindex", 4},
            {"numcdtextures", 4}, {"cdtextureindex", 4},
            {"numskinref", 4}, {"numskinfamilies", 4}, {"skinindex", 4},
            {"numbodyparts", 4}, {"bodypartindex", 4},
            {"numlocalattachments", 4}, {"localattachmentindex", 4},
            {"numlocalnodes", 4}, {"localnodeindex", 4}, {"localnodenameindex", 4},
            {"numflexdesc", 4}, {"flexdescindex", 4},
            {"numflexcontrollers", 4}, {"flexcontrollerindex", 4},
            {"numflexrules", 4}, {"flexruleindex", 4},
            {"numikchains", 4}, {"ikchainindex", 4},
            {"nummouths", 4}, {"mouthindex", 4},
            {"numlocalposeparameters", 4}, {"localposeparamindex", 4},
            {"surfacepropindex", 4},
            {"keyvalueindex", 4}, {"keyvaluesize", 4},
            {"numlocalikautoplaylocks", 4}, {"localikautoplaylockindex", 4},
            {"mass", 4}, {"contents", 4},
            {"numincludemodels", 4}, {"includemodelindex", 4},
            {"unused_virtualModel", 4},
            {"szanimblocknameindex", 4}, {"numanimblocks", 4}, {"animblockindex", 4},
            {"unused_animblockModel", 4},
            {"bonetablebynameindex", 4},
            {"unused_pVertexBase", 4}, {"unused_pIndexBase", 4},
            {"constdirectionallightdot", 1}, {"rootLOD", 1}, {"numAllowedRootLODs", 1}, {"unused", 1},
            {"unused4", 4},
            {"numflexcontrollerui", 4}, {"flexcontrolleruiindex", 4},
            {"flVertAnimFixedPointScale", 4}, {"surfacepropLookup", 4},
            {"studiohdr2index", 4}, {"unused2", 4},
    };

    constexpr uint32_t HeaderSize() {
        uint32_t size = 0;
        for (const HeaderField &field: kHeaderFields)
            size += field.size;
        return size;
    }

    static_assert(HeaderSize() == 408, "studiohdr_t is 408 bytes: the field table above is out of date");

    // Header fields holding the file offset of a section
    constexpr const char *kSectionIndexFields[] = {
            "boneindex", "bonecontrollerindex", "hitboxsetindex", "localanimindex", "localseqindex", "textureindex",
            "cdtextureindex", "skinindex", "bodypartindex", "localattachmentindex", "localnodeindex",
            "localnodenameindex", "flexdescindex", "flexcontrollerindex", "flexruleindex", "ikchainindex",
            "mouthindex", "localposeparamindex", "surfacepropindex", "keyvalueindex", "localikautoplaylockindex",
            "includemodelindex", "szanimblocknameindex", "animblockindex", "bonetablebynameindex",
            "flexcontrolleruiindex", "studiohdr2index",
    };

    uint32_t HeaderFieldOffset(std::string_view name) {
        uint32_t offset = 0;
        for (const HeaderField &field: kHeaderFields) {
            if (name == field.name)
                return offset;
            offset += field.size;
        }
        return UINT32_MAX;
    }

    int32_t ReadInt32(std::span<const std::byte> bytes, size_t offset) {
        int32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    bool EndsWith(const std::string &text, std::string_view suffix) {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
} // namespace

std::string DescribeMdlOffset(std::span<const std::byte> mdl, size_t offset) {
    if (mdl.size() < 408)
        return "file shorter than studiohdr_t";
    if (offset < 408) {
        uint32_t start = 0;
        for (const HeaderField &field: kHeaderFields) {
            if (offset < start + field.size)
                return std::format("studiohdr_t.{} (header +0x{:x})", field.name, start);
            start += field.size;
        }
    }
    const char *bestName = nullptr;
    int32_t bestStart = -1;
    for (const char *fieldName: kSectionIndexFields) {
        const int32_t start = ReadInt32(mdl, HeaderFieldOffset(fieldName));
        if (start > 0 && static_cast<size_t>(start) <= offset && start > bestStart) {
            bestStart = start;
            bestName = fieldName;
        }
    }
    if (!bestName)
        return std::format("file offset 0x{:x} after the header, before any section", offset);
    return std::format("after the start of the section of studiohdr_t.{} (0x{:x}), +0x{:x} into it", bestName,
                       bestStart, offset - static_cast<size_t>(bestStart));
}

std::string DescribeOutputDifference(const Outputs &expected, const Outputs &actual) {
    std::string report;
    std::vector<std::string> missing, extra, different;
    for (const auto &[name, bytes]: expected) {
        const auto found = actual.find(name);
        if (found == actual.end())
            missing.push_back(name);
        else if (found->second != bytes)
            different.push_back(name);
    }
    for (const auto &[name, bytes]: actual) {
        if (!expected.contains(name))
            extra.push_back(name);
    }
    if (missing.empty() && extra.empty() && different.empty())
        return {};

    auto join = [](const std::vector<std::string> &names) {
        std::string out;
        for (const std::string &name: names)
            out += (out.empty() ? "" : ", ") + name;
        return out;
    };
    if (!missing.empty())
        report += std::format("missing in actual: {}\n", join(missing));
    if (!extra.empty())
        report += std::format("unexpected in actual: {}\n", join(extra));
    if (!different.empty()) {
        // the .mdl first: its header names the cause of most differences
        std::stable_sort(different.begin(), different.end(), [](const std::string &a, const std::string &b) {
            return EndsWith(a, ".mdl") && !EndsWith(b, ".mdl");
        });
        const std::string &name = different.front();
        const std::vector<std::byte> &a = expected.at(name);
        const std::vector<std::byte> &b = actual.at(name);
        const size_t common = std::min(a.size(), b.size());
        size_t offset = 0;
        while (offset < common && a[offset] == b[offset])
            ++offset;
        report += std::format("{}: first difference at offset {} (0x{:x}), sizes expected {} / actual {}", name,
                              offset, offset, a.size(), b.size());
        if (offset < common) {
            report += std::format(", expected 0x{:02x} / actual 0x{:02x}", static_cast<unsigned>(a[offset]),
                                  static_cast<unsigned>(b[offset]));
        } else {
            report += ", one is a prefix of the other";
        }
        report += "\n";
        if (EndsWith(name, ".mdl"))
            report += std::format("  location in the expected .mdl: {}\n", DescribeMdlOffset(a, offset));
        if (different.size() > 1)
            report += std::format("other files that differ: {}\n", join({different.begin() + 1, different.end()}));
    }
    return report;
}

void RequireSameOutputs(const Outputs &expected, const Outputs &actual, std::source_location where) {
    const std::string report = DescribeOutputDifference(expected, actual);
    if (!report.empty()) {
        // SMDL_TEST_DUMP=<dir>: keep both sides of a mismatch as <dir>/expected/... and <dir>/actual/... for diffing
        if (const char *dump = std::getenv("SMDL_TEST_DUMP")) {
            for (const auto &[side, files]: {std::pair{"expected", &expected}, std::pair{"actual", &actual}}) {
                for (const auto &[name, bytes]: *files) {
                    const std::filesystem::path path = std::filesystem::path(dump) / side / name;
                    std::filesystem::create_directories(path.parent_path());
                    std::ofstream out(path, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                }
            }
        }
        throw TestFailure(std::format("{}: outputs differ\n{}", Where(where), report));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------------------------------------------

namespace {
    bool GlobMatch(std::string_view pattern, std::string_view text) {
        size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
        while (t < text.size()) {
            if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
                ++p;
                ++t;
            } else if (p < pattern.size() && pattern[p] == '*') {
                star = p++;
                mark = t;
            } else if (star != std::string_view::npos) {
                p = star + 1;
                t = ++mark;
            } else {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == '*')
            ++p;
        return p == pattern.size();
    }

    bool Selected(const std::vector<std::string> &patterns, const std::string &name) {
        bool anyInclude = false, included = false;
        for (const std::string &pattern: patterns) {
            if (!pattern.empty() && pattern[0] == '-') {
                if (GlobMatch(std::string_view(pattern).substr(1), name))
                    return false;
            } else {
                anyInclude = true;
                included = included || GlobMatch(pattern, name);
            }
        }
        return !anyInclude || included;
    }

    std::string DescribeError(const smdl::Error &error) {
        std::string text = std::format("smdl::Error (status {} {}): {}", static_cast<int>(error.status()),
                                       smdl_status_string(error.status()), error.what());
        if (!error.object().empty())
            text += std::format(" [object: {}]", error.object());
        return text;
    }

    // Library code must not write to stdout (its text goes through the compile's log callback). While a test runs,
    // fd 1 points to a temp file; what ends up there is reported after the test (a failure with
    // SMDL_TEST_STRICT_STDOUT set). POSIX only: on Windows nothing is captured.
    class StdoutCapture {
    public:
        StdoutCapture() {
#ifndef _WIN32
            std::fflush(stdout);
            m_File = std::tmpfile();
            if (!m_File)
                return;
            m_Saved = dup(1);
            if (m_Saved < 0 || dup2(fileno(m_File), 1) < 0) {
                if (m_Saved >= 0)
                    close(m_Saved);
                m_Saved = -1;
                std::fclose(m_File);
                m_File = nullptr;
            }
#endif
        }
        StdoutCapture(const StdoutCapture &) = delete;
        StdoutCapture &operator=(const StdoutCapture &) = delete;
        ~StdoutCapture() { Finish(); }

        // Restores stdout and returns what was written meanwhile (empty on later calls)
        std::string Finish() {
            std::string text;
#ifndef _WIN32
            if (m_File) {
                std::fflush(stdout);
                dup2(m_Saved, 1);
                close(m_Saved);
                m_Saved = -1;
                std::fflush(m_File);
                std::rewind(m_File);
                char buffer[4096];
                size_t got;
                while ((got = std::fread(buffer, 1, sizeof buffer, m_File)) > 0)
                    text.append(buffer, got);
                std::fclose(m_File);
                m_File = nullptr;
            }
#endif
            return text;
        }

    private:
        std::FILE *m_File = nullptr;
        int m_Saved = -1;
    };
} // namespace

int RunTests(const std::vector<std::string> &patterns) {
    std::vector<TestCase> tests = TestRegistry();
    std::sort(tests.begin(), tests.end(), [](const TestCase &a, const TestCase &b) { return a.name < b.name; });
    for (size_t i = 1; i < tests.size(); ++i) {
        if (tests[i].name == tests[i - 1].name) {
            std::fprintf(stderr, "duplicate test name '%s' (%s:%d and %s:%d)\n", tests[i].name.c_str(),
                         tests[i - 1].file, tests[i - 1].line, tests[i].file, tests[i].line);
            return 2;
        }
    }

    int run = 0;
    std::vector<std::string> failed;
    for (const TestCase &test: tests) {
        if (!Selected(patterns, test.name))
            continue;
        ++run;
        std::string failure, captured;
        const auto begin = std::chrono::steady_clock::now();
        {
            StdoutCapture capture;
            try {
                test.function();
            } catch (const TestFailure &error) {
                failure = error.what();
            } catch (const smdl::Error &error) {
                failure = DescribeError(error);
            } catch (const std::exception &error) {
                failure = std::format("unexpected exception: {}", error.what());
            } catch (...) {
                failure = "unexpected non-standard exception";
            }
            captured = capture.Finish();
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
        if (!captured.empty() && failure.empty() && std::getenv("SMDL_TEST_STRICT_STDOUT"))
            failure = std::format("wrote {} byte(s) to stdout (library output must go through the log callback)", captured.size());
        if (failure.empty()) {
            std::printf("[ PASS ] %s (%lld ms)\n", test.name.c_str(), static_cast<long long>(ms));
        } else {
            std::printf("[ FAIL ] %s (%lld ms)\n         %s\n", test.name.c_str(), static_cast<long long>(ms),
                        failure.c_str());
            failed.push_back(test.name);
        }
        if (!captured.empty()) {
            const size_t end = captured.find('\n');
            std::printf("[ NOTE ] %s wrote %zu byte(s) to stdout; first line: %s\n", test.name.c_str(), captured.size(),
                        captured.substr(0, end).c_str());
        }
        std::fflush(stdout);
    }

    if (run == 0) {
        std::printf("no test matches the given patterns (%zu registered)\n", tests.size());
        return 2;
    }
    std::printf("\n%d test(s) run, %zu failed\n", run, failed.size());
    for (const std::string &name: failed)
        std::printf("  failed: %s\n", name.c_str());
    return failed.empty() ? 0 : 1;
}
