// Error handling of the compile (STUDIOMDL_API_PLAN.md phase 0): a failed compile reports through the result, the
// model's last error and the log callback only (never stdout), and leaves the process reusable.
#include "harness.hpp"

#include <cstdio>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
    bool Contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

    // Counts what the library writes to fd 1 meanwhile. The runner captures fd 1 per test as well; this nests inside
    // it so a single compile can be asserted on. Not available on Windows (always 0).
    class StdoutProbe {
    public:
        StdoutProbe() {
#ifndef _WIN32
            std::fflush(stdout);
            m_File = std::tmpfile();
            if (!m_File)
                return;
            m_Saved = dup(1);
            if (m_Saved < 0 || dup2(fileno(m_File), 1) < 0) {
                Close();
                return;
            }
#endif
        }
        StdoutProbe(const StdoutProbe &) = delete;
        StdoutProbe &operator=(const StdoutProbe &) = delete;
        ~StdoutProbe() { Finish(); }

        // Restores stdout; the number of bytes written since construction
        size_t Finish() {
#ifndef _WIN32
            if (m_File) {
                std::fflush(stdout);
                struct stat info{};
                if (fstat(fileno(m_File), &info) == 0)
                    m_Written = static_cast<size_t>(info.st_size);
                dup2(m_Saved, 1);
                Close();
            }
#endif
            return m_Written;
        }

    private:
#ifndef _WIN32
        void Close() {
            if (m_Saved >= 0)
                close(m_Saved);
            m_Saved = -1;
            if (m_File)
                std::fclose(m_File);
            m_File = nullptr;
        }
        std::FILE *m_File = nullptr;
        int m_Saved = -1;
#endif
        size_t m_Written = 0;
    };

    struct LoggedMessage {
        smdl::Severity severity;
        std::string text;
    };

    // One compile through the C API: the status, the result (also present for a failed compile), what the log
    // callback received, and what was written to stdout meanwhile
    struct Outcome {
        smdl_status status = SMDL_OK;
        smdl::Result result{nullptr};
        std::vector<LoggedMessage> log;
        size_t stdoutBytes = 0;
        std::string lastError; // smdl_model_last_error after the call
    };

    void CollectLog(void *user, smdl_severity severity, const char *text) {
        static_cast<std::vector<LoggedMessage> *>(user)->push_back({static_cast<smdl::Severity>(severity), text});
    }

    Outcome CompileRaw(const smdl::Model &model, const char *gameDir = nullptr) {
        Outcome outcome;
        smdl_result *raw = nullptr;
        StdoutProbe probe;
        outcome.status = smdl_model_compile(model.handle(), gameDir, &CollectLog, &outcome.log, &raw);
        outcome.stdoutBytes = probe.Finish();
        outcome.result = smdl::Result(raw);
        outcome.lastError = smdl_model_last_error(model.handle());
        return outcome;
    }

    bool HasSeverity(const Outcome &outcome, smdl::Severity severity) {
        for (const LoggedMessage &message: outcome.log)
            if (message.severity == severity)
                return true;
        return false;
    }

    // A model that compiles: a static crate prop
    smdl::Model BuildGoodModel() {
        smdl::Model model("api/errors_good");
        model.addMaterialPath("models/api");
        model.setSurfaceProp("wood");
        model.setStaticProp(true);
        const SmdFile smd = ReadSmd(FixturePath("src/crate.smd"));
        AddSkeleton(model, smd);
        smdl::BodyGroup group = model.addBodyGroup("crate");
        smdl::Mesh mesh = group.createMesh("src/crate");
        FillGeometry(model, mesh.geometry(), smd, VertexMode::Indexed);
        return model;
    }

    void RequireFailedCompile(const Outcome &outcome, std::string_view what) {
        Require(outcome.status == SMDL_ERROR_COMPILE, std::string(what) + ": status is SMDL_ERROR_COMPILE");
        Require(outcome.result.handle() != nullptr, std::string(what) + ": a failed compile still returns a result");
        Require(!outcome.result.ok(), std::string(what) + ": result status is an error");
        Require(!outcome.result.errorMessage().empty(), std::string(what) + ": result has an error message");
        Require(outcome.result.files().empty(), std::string(what) + ": no output files");
        Require(outcome.lastError == outcome.result.errorMessage(),
                std::string(what) + ": model last error is the result's message ('" + outcome.lastError + "')");
    }
}

#ifndef _WIN32
// The probe the other tests rely on sees what goes to stdout (else "0 bytes" would prove nothing)
SMDL_TEST(errors_stdout_probe_counts_bytes) {
    StdoutProbe probe;
    std::fputs("probe", stdout);
    Require(probe.Finish() == 5, "the probe counts the bytes written to stdout");
}
#endif

// studiomdl's own message, through the result, the model's last error, the log callback and the C++ exception
SMDL_TEST(errors_empty_model) {
    smdl::Model model("api/errors_empty");
    const Outcome outcome = CompileRaw(model);

    RequireFailedCompile(outcome, "empty model");
    Require(Contains(outcome.result.errorMessage(), "no sequences"),
            "message is studiomdl's: " + std::string(outcome.result.errorMessage()));
    Require(HasSeverity(outcome, smdl::Severity::Error), "the log callback received an error message");
    bool logged = false;
    for (const LoggedMessage &message: outcome.log)
        logged = logged || (message.severity == smdl::Severity::Error && Contains(message.text, "no sequences"));
    Require(logged, "the error message of the log names the problem");

    // the result keeps the messages as well
    bool kept = false;
    for (const smdl::Message &message: outcome.result.messages())
        kept = kept || (message.severity == smdl::Severity::Error && Contains(message.text, "no sequences"));
    Require(kept, "the result's messages hold the error");

    Require(outcome.stdoutBytes == 0,
            "nothing is written to stdout during the compile (" + std::to_string(outcome.stdoutBytes) + " bytes)");

    // the wrapper throws the same
    try {
        model.compile();
        Fail("expected an smdl::Error");
    } catch (const smdl::Error &error) {
        Require(error.status() == SMDL_ERROR_COMPILE, "exception status");
        Require(Contains(error.what(), "no sequences"), std::string("exception message: ") + error.what());
    }
}

// The process is reusable: the same failing compile gives the same result again
SMDL_TEST(errors_second_compile_same) {
    smdl::Model model("api/errors_empty");
    const Outcome first = CompileRaw(model);
    const Outcome second = CompileRaw(model);

    RequireFailedCompile(first, "first compile");
    RequireFailedCompile(second, "second compile");
    Require(first.result.errorMessage() == second.result.errorMessage(), "same error message");
    Require(first.result.errorObject() == second.result.errorObject(), "same error object");
    Require(first.log.size() == second.log.size(), "same number of log messages");
    for (size_t i = 0; i < first.log.size() && i < second.log.size(); ++i)
        Require(first.log[i].severity == second.log[i].severity && first.log[i].text == second.log[i].text,
                "same log message " + std::to_string(i));
    Require(second.stdoutBytes == 0, "nothing written to stdout by the second compile");
}

SMDL_TEST(errors_missing_game_dir) {
    const TempDir temp("errors");
    const std::string missing = (temp.path() / "no_such_game").string();
    const smdl::Model model = BuildGoodModel();
    const Outcome outcome = CompileRaw(model, missing.c_str());

    RequireFailedCompile(outcome, "missing game dir");
    Require(Contains(outcome.result.errorMessage(), "does not exist"),
            "message says the directory is missing: " + std::string(outcome.result.errorMessage()));
    Require(Contains(outcome.result.errorMessage(), "no_such_game"),
            "message names the directory: " + std::string(outcome.result.errorMessage()));
    Require(outcome.stdoutBytes == 0, "nothing written to stdout");
    Require(!std::filesystem::exists(temp.path() / "no_such_game"), "the directory is not created");
}

SMDL_TEST(errors_game_dir_without_gameinfo) {
    const TempDir temp("errors");
    const std::string dir = temp.path().string();
    const smdl::Model model = BuildGoodModel();
    const Outcome outcome = CompileRaw(model, dir.c_str());

    RequireFailedCompile(outcome, "game dir without gameinfo.txt");
    Require(Contains(outcome.result.errorMessage(), "gameinfo.txt"),
            "message names gameinfo.txt: " + std::string(outcome.result.errorMessage()));
    Require(outcome.stdoutBytes == 0, "nothing written to stdout");
    Require(std::filesystem::is_empty(temp.path()), "nothing is written to the game directory");
}

// Failing and succeeding compiles alternate: a failure leaves no state behind that changes the next compile
SMDL_TEST(errors_failures_do_not_poison_later_compiles) {
    const smdl::Model empty("api/errors_empty");
    const smdl::Model good = BuildGoodModel();
    const TempDir temp("errors");
    const std::string missing = (temp.path() / "no_such_game").string();

    const Outcome reference = CompileRaw(good);
    Require(reference.status == SMDL_OK && reference.result.ok(), "the good model compiles");
    const auto referenceFiles = ResultFiles(reference.result);
    Require(!referenceFiles.empty(), "the good model writes files");

    for (int round = 0; round < 3; ++round) {
        RequireFailedCompile(CompileRaw(empty), "empty model in round " + std::to_string(round));
        RequireFailedCompile(CompileRaw(good, missing.c_str()), "missing game dir in round " + std::to_string(round));

        const Outcome again = CompileRaw(good);
        Require(again.status == SMDL_OK && again.result.ok(), "the good model compiles after failures");
        Require(again.stdoutBytes == 0, "nothing written to stdout by a successful compile");
        Require(!HasSeverity(again, smdl::Severity::Error), "no error message in a successful compile");
        RequireSameOutputs(referenceFiles, ResultFiles(again.result));
    }
}
