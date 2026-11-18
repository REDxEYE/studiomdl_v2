// smdl::QcCompile (cpp/qc.hpp): compiling an existing QC / .mpp. The same front end as the console tool (the shared
// switch parser, SetupCompileDirectories, CompileModel) inside the builder's process lock, with host-provided files
// served by the filesystem layer (filesystem/file_overlay.h) instead of the disk. smdl_qc.h wraps it.
#include "studiomdl/c/smdl_qc.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "compile_driver.hpp"

#include "datamodel/idatamodel.h"
#include "filesystem.h"
#include "filesystem/file_overlay.h"
#include "filesystem_init.h"
#include "interfaces/interfaces.h"
#include "studiomdl/command_line.hpp"
#include "studiomdl/compile.hpp"
#include "studiomdl/compile_error.hpp"
#include "studiomdl/processing.hpp"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_app.h"
#include "studiomdl_errors.h"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

using smdl_api::CompileMutex;
using smdl_api::GameDirString;
using smdl_api::InitProcessOnce;
using smdl_api::LoggingScope;
using smdl_api::ProcessingScope;

namespace {
    // The host's files: in-memory ones and the reader, behind the filesystem layer's overlay hook
    class QcOverlay final : public IFileOverlay {
    public:
        void Add(const std::string &path, const void *data, size_t size) {
            const auto *bytes = static_cast<const std::byte *>(data);
            auto file = std::make_shared<const std::vector<std::byte>>(bytes, bytes + size);
            m_Files[path] = file;
            m_Lowered[Lower(path)] = file;
        }

        void SetReader(smdl_qc_read_fn reader, void *user) {
            m_Reader = reader;
            m_ReaderUser = user;
        }

        FileOverlayData Find(const std::string &path) override {
            if (const auto it = m_Files.find(path); it != m_Files.end())
                return it->second;
            // Content paths are often written with the wrong case; the disk lookup is lenient too
            if (const auto it = m_Lowered.find(Lower(path)); it != m_Lowered.end())
                return it->second;
            if (!m_Reader)
                return nullptr;
            if (const auto it = m_ReaderCache.find(path); it != m_ReaderCache.end())
                return it->second;
            FileOverlayData file;
            const void *data = nullptr;
            size_t size = 0;
            if (m_Reader(m_ReaderUser, path.c_str(), &data, &size)) {
                const auto *bytes = static_cast<const std::byte *>(data);
                file = std::make_shared<const std::vector<std::byte>>(bytes, bytes + (data ? size : 0));
            }
            m_ReaderCache.emplace(path, file);
            return file;
        }

        // Per compile: what the reader said is not remembered between runs
        void ResetCache() { m_ReaderCache.clear(); }

    private:
        static std::string Lower(std::string text) {
            for (char &c: text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        std::unordered_map<std::string, FileOverlayData> m_Files;
        std::unordered_map<std::string, FileOverlayData> m_Lowered;
        std::unordered_map<std::string, FileOverlayData> m_ReaderCache;
        smdl_qc_read_fn m_Reader = nullptr;
        void *m_ReaderUser = nullptr;
    };

    struct TypedOptions {
        bool noWarnings = false;
        std::optional<int> maxWarnings;
        bool fitWeights = false;
        int fitPoses = 0, testPoses = 0;
        std::optional<smdl_qc_flex_optimize> flexOptimize;
        std::optional<int> flexMinStatic;
        bool stripLods = false;
        std::optional<int> minLod;
        bool defineBones = false;
        bool overrideDefineBones = false;
        bool checkLengths = false;
        bool printBones = false;
        bool dumpMaterials = false;
        bool collapseReport = false;

        std::vector<std::string> Arguments() const {
            std::vector<std::string> args;
            if (noWarnings)
                args.push_back("-nowarnings");
            if (maxWarnings) {
                args.push_back("-maxwarnings");
                args.push_back(std::to_string(*maxWarnings));
            }
            if (fitWeights) {
                args.push_back("-fitweights");
                if (fitPoses || testPoses)
                    args.push_back(std::format("{},{}", fitPoses, testPoses));
            }
            if (flexOptimize) {
                args.push_back("-flexopt");
                args.push_back(*flexOptimize == SMDL_QC_FLEXOPT_ON ? "on" : *flexOptimize == SMDL_QC_FLEXOPT_OFF ? "off" : "auto");
            }
            if (flexMinStatic) {
                args.push_back("-flexopt_min_static");
                args.push_back(std::to_string(*flexMinStatic));
            }
            if (stripLods)
                args.push_back("-striplods");
            if (minLod) {
                args.push_back("-minlod");
                args.push_back(std::to_string(*minLod));
            }
            if (defineBones)
                args.push_back("-definebones");
            if (overrideDefineBones)
                args.push_back("-overridedefinebones");
            if (checkLengths)
                args.push_back("-checklengths");
            if (printBones)
                args.push_back("-printbones");
            if (dumpMaterials)
                args.push_back("-dumpmaterials");
            if (collapseReport)
                args.push_back("-collapsereport");
            return args;
        }
    };

    // Runs the console tool's parser over the arguments on a scratch state; false with its message when it refuses them
    bool ValidateArguments(const std::vector<std::string> &arguments, std::string &error) {
        auto pCtx = std::make_unique<StudioMdlState>();
        StudioMdlState &ctx = *pCtx;
        ScopedMdlDiagnostics diagnostics(ctx);
        ctx.diagnostics.sink = [](MdlSeverity, std::string_view) {};
        std::vector<std::string_view> views(arguments.begin(), arguments.end());
        try {
            ParseStudioMdlArguments(ctx, views, ArgumentMode::Options);
        }
        catch (const MdlCompileError &e) {
            error = e.what();
            return false;
        }
        return true;
    }
}

struct smdl_qc_compile {
    std::optional<std::string> gameDir;
    std::string qcPath;
    TypedOptions typed;
    std::vector<std::string> extra;
    QcOverlay overlay;
    smdl_log_fn log = nullptr;
    void *logUser = nullptr;
    bool writeToGameDir = false;
    mutable std::string lastError;

    std::vector<std::string> Arguments() const {
        std::vector<std::string> args = typed.Arguments();
        args.insert(args.end(), extra.begin(), extra.end());
        return args;
    }
};

namespace {
    smdl_status Fail(smdl_qc_compile *c, smdl_status status, std::string message) {
        c->lastError = std::move(message);
        return status;
    }

    // The body of a QcCompile method in the status style; throws smdl::Error for a non-OK status
    template<class F>
    void RunBody(smdl_qc_compile *c, F &&body) {
        c->lastError.clear();
        const smdl_status status = body();
        if (status != SMDL_OK)
            throw smdl::Error(status, c->lastError.empty() ? smdl_status_string(status) : c->lastError);
    }

    // The C boundary: runs a C++ call, turns exceptions into statuses
    template<class F>
    smdl_status CallCpp(smdl_qc_compile *c, F &&call) noexcept {
        if (!c)
            return SMDL_ERROR_INVALID_ARGUMENT;
        try {
            c->lastError.clear();
            call();
            return SMDL_OK;
        }
        catch (const smdl::Error &e) {
            c->lastError = e.what();
            return e.status();
        }
        catch (const std::exception &e) {
            return Fail(c, SMDL_ERROR_INTERNAL, std::format("internal error: {}", e.what()));
        }
        catch (...) {
            return Fail(c, SMDL_ERROR_INTERNAL, "internal error");
        }
    }

    // Changes the typed options and checks the whole argument list again; the change is undone when it is refused
    template<class F>
    void SetTyped(smdl_qc_compile *c, F &&change) {
        RunBody(c, [&] {
            const TypedOptions saved = c->typed;
            change(c->typed);
            std::string error;
            if (!ValidateArguments(c->Arguments(), error) && !error.ends_with("needs a value")) {
                c->typed = saved;
                return Fail(c, SMDL_ERROR_INVALID_ARGUMENT, std::move(error));
            }
            return SMDL_OK;
        });
    }

    void RunQc(smdl_qc_compile &c, smdl_result &result) {
        InitProcessOnce();

        LoggingScope logging;
        ProcessingScope processing;
        auto pCtx = std::make_unique<StudioMdlState>();
        StudioMdlState &ctx = *pCtx;
        ScopedMdlDiagnostics diagnostics(ctx);
        ctx.diagnostics.sink = [&](MdlSeverity severity, std::string_view text) {
            const auto cSeverity = static_cast<smdl_severity>(severity);
            result.messages.push_back({cSeverity, std::string(text)});
            if (c.log)
                c.log(c.logUser, cSeverity, result.messages.back().text.c_str());
        };

        try {
            if (c.qcPath.empty())
                throw MdlCompileError("no QC file set (smdl_qc_compile_set_qc_path)");

            const std::string ext(vstd::paths::extension(c.qcPath));
            if (vstd::iequals(ext, "mdl"))
                throw MdlCompileError("a .qc or .mpp file should be specified to build");

            if (!BeginProcessing())
                throw MdlCompileError("could not initialize the datamodel");
            processing.active = true;
            g_pFileSystem = g_pFullFileSystem;

            // the options, by the console tool's own parser
            const std::vector<std::string> arguments = c.Arguments();
            const std::vector<std::string_view> views(arguments.begin(), arguments.end());
            ParseStudioMdlArguments(ctx, views, ArgumentMode::Options);

            std::error_code ec;
            const std::filesystem::path qc =
                    std::filesystem::absolute(vstd::paths::to_fs(c.qcPath), ec).lexically_normal();
            SetCompileInput(ctx, vstd::paths::to_utf8(qc));

            std::filesystem::path gameDir;
            if (c.gameDir) {
                gameDir = std::filesystem::absolute(vstd::paths::to_fs(*c.gameDir), ec);
                const std::string gameDirText = GameDirString(gameDir);
                const bool hasGameInfo = c.overlay.Find(FS_NormalizeOverlayPath(gameDirText + "gameinfo.txt")) != nullptr;
                if (!hasGameInfo && !std::filesystem::is_directory(gameDir))
                    throw MdlCompileError(std::format("game directory '{}' does not exist", *c.gameDir));
                if (FileSystem_LoadSearchPaths(g_pFullFileSystem, gameDirText) != FS_OK)
                    throw MdlCompileError(std::format("game directory '{}' has no valid gameinfo.txt", *c.gameDir));
                FileSystem_AddSearchPath_Platform(g_pFullFileSystem, gameDirText);
            }
            else {
                // Outputs are only kept in memory: the directory is a key prefix and is never created
                gameDir = std::filesystem::temp_directory_path(ec) / "smdl_virtual_game";
            }
            SetupCompileDirectories(ctx, vstd::paths::to_utf8(qc), GameDirString(gameDir));
            ctx.paths.gameDir = GameDirString(gameDir);
            g_pFullFileSystem->AddSearchPath("", IBaseFileSystem::PathID::Local, PATH_ADD_TO_HEAD);

            const int exitCode = CompileModel(ctx, CompileOutput::KeepInMemory);
            if (exitCode != 0)
                throw MdlCompileError(std::format("the compile failed (exit code {})", exitCode));

            for (auto &[name, data]: ctx.write.outputFiles) {
                std::string relative = name;
                std::ranges::replace(relative, '\\', '/');
                if (relative.starts_with(ctx.paths.gameDir))
                    relative.erase(0, ctx.paths.gameDir.size());
                const auto *bytes = reinterpret_cast<const std::byte *>(data.data());
                result.files.push_back({std::move(relative), std::vector<std::byte>(bytes, bytes + data.size())});
            }
            result.status = SMDL_OK;
        }
        catch (const MdlCompileError &e) {
            result.status = SMDL_ERROR_COMPILE;
            result.errorMessage = e.what();
            result.errorFile = e.file();
            result.errorLine = e.line();
            result.files.clear();
        }
        catch (const Plat_ExitException &e) {
            std::string_view message = e.what();
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.remove_suffix(1);
            result.status = SMDL_ERROR_COMPILE;
            result.errorMessage = std::string(message);
            result.files.clear();
        }
        catch (const std::exception &e) {
            result.status = SMDL_ERROR_INTERNAL;
            result.errorMessage = std::format("internal error: {}", e.what());
            result.files.clear();
        }
        result.warningCount = ctx.diagnostics.warningCount;
        if (processing.active)
            MdlDiscardDataModelContents();
    }

    // The whole run under the process lock; a failed compile is reported in the result, not thrown. Returns the
    // status of the run (the result's, or the failure to write the outputs) and records the message in lastError.
    smdl_status RunToResult(smdl_qc_compile *compile, std::unique_ptr<smdl_result> &out) {
        if (compile->writeToGameDir && !compile->gameDir)
            return Fail(compile, SMDL_ERROR_INVALID_ARGUMENT, "writing the outputs needs a game directory");
        auto result = std::make_unique<smdl_result>();
        {
            std::scoped_lock lock(CompileMutex());
            compile->overlay.ResetCache();
            ScopedFileOverlay overlay(compile->overlay);
            RunQc(*compile, *result);
        }
        smdl_status status = result->status;
        if (status != SMDL_OK) {
            Fail(compile, status, result->errorMessage);
        }
        else if (compile->writeToGameDir) {
            status = smdl_result_save(result.get(), compile->gameDir->c_str());
            if (status != SMDL_OK)
                Fail(compile, status, "could not write the outputs to the game directory");
        }
        out = std::move(result);
        return status;
    }
}

namespace smdl {

QcCompile::QcCompile() : m_Handle(new smdl_qc_compile, Deleter{true}) {}

void QcCompile::Deleter::operator()(smdl_qc_compile *compile) const {
    if (owned)
        delete compile;
}

namespace detail {
    QcCompile BorrowQcCompile(smdl_qc_compile *compile) { return QcCompile(compile, QcCompile::Borrowed{}); }
    smdl_qc_compile *ReleaseQcCompile(QcCompile &&compile) { return compile.m_Handle.release(); }
}

QcCompile &QcCompile::setGameDir(const std::string_view gameDir) {
    smdl_qc_compile *compile = handle();
    if (!gameDir.empty())
        compile->gameDir = std::string(gameDir);
    else
        compile->gameDir.reset();
    return *this;
}

QcCompile &QcCompile::setQcPath(const std::string_view qc_path) {
    smdl_qc_compile *compile = handle();
    RunBody(compile, [&] {
        if (qc_path.empty())
            return Fail(compile, SMDL_ERROR_INVALID_ARGUMENT, "the QC path is empty");
        compile->qcPath = std::string(qc_path);
        return SMDL_OK;
    });
    return *this;
}

QcCompile &QcCompile::setNoWarnings(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.noWarnings = on; });
    return *this;
}

QcCompile &QcCompile::setMaxWarnings(std::optional<int> max) {
    SetTyped(handle(), [&](TypedOptions &t) { t.maxWarnings = max; });
    return *this;
}

QcCompile &QcCompile::setFitWeights(bool on, int fittingPoses, int testPoses) {
    SetTyped(handle(), [&](TypedOptions &t) {
        t.fitWeights = on;
        t.fitPoses = fittingPoses;
        t.testPoses = testPoses;
    });
    return *this;
}

QcCompile &QcCompile::setFlexOptimize(FlexOptimize mode) {
    const auto value = static_cast<smdl_qc_flex_optimize>(mode);
    smdl_qc_compile *c = handle();
    RunBody(c, [&] {
        if (value != SMDL_QC_FLEXOPT_AUTO && value != SMDL_QC_FLEXOPT_ON && value != SMDL_QC_FLEXOPT_OFF)
            return Fail(c, SMDL_ERROR_INVALID_ARGUMENT, "invalid flex optimize mode");
        return SMDL_OK;
    });
    SetTyped(c, [&](TypedOptions &t) { t.flexOptimize = value; });
    return *this;
}

QcCompile &QcCompile::setFlexOptimizeMinStatic(std::optional<int> vertices) {
    SetTyped(handle(), [&](TypedOptions &t) { t.flexMinStatic = vertices; });
    return *this;
}

QcCompile &QcCompile::setStripLods(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.stripLods = on; });
    return *this;
}

QcCompile &QcCompile::setMinLod(std::optional<int> lod) {
    SetTyped(handle(), [&](TypedOptions &t) { t.minLod = lod; });
    return *this;
}

QcCompile &QcCompile::setDefineBones(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.defineBones = on; });
    return *this;
}

QcCompile &QcCompile::setOverrideDefineBones(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.overrideDefineBones = on; });
    return *this;
}

QcCompile &QcCompile::setCheckLengths(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.checkLengths = on; });
    return *this;
}

QcCompile &QcCompile::setPrintBones(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.printBones = on; });
    return *this;
}

QcCompile &QcCompile::setDumpMaterials(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.dumpMaterials = on; });
    return *this;
}

QcCompile &QcCompile::setCollapseReport(bool on) {
    SetTyped(handle(), [&](TypedOptions &t) { t.collapseReport = on; });
    return *this;
}

QcCompile &QcCompile::addArgument(const std::string_view argument) {
    smdl_qc_compile *compile = handle();
    RunBody(compile, [&] {
        compile->extra.emplace_back(argument);
        std::string error;
        // a switch whose value comes with the next call is complete at the next check (and at the run)
        if (!ValidateArguments(compile->Arguments(), error) && !error.ends_with("needs a value")) {
            compile->extra.pop_back();
            return Fail(compile, SMDL_ERROR_INVALID_ARGUMENT, std::move(error));
        }
        return SMDL_OK;
    });
    return *this;
}

QcCompile &QcCompile::addFile(const std::string_view path, const void *data, size_t size) {
    smdl_qc_compile *compile = handle();
    RunBody(compile, [&] {
        if (path.empty() || (size && !data))
            return Fail(compile, SMDL_ERROR_INVALID_ARGUMENT, "invalid file path or data");
        compile->overlay.Add(FS_NormalizeOverlayPath(std::string(path)), data, size);
        return SMDL_OK;
    });
    return *this;
}

QcCompile &QcCompile::setReader(smdl_qc_read_fn reader, void *user) {
    handle()->overlay.SetReader(reader, user);
    return *this;
}

QcCompile &QcCompile::setReader(FileReader reader) {
    m_Reader = std::make_unique<FileReader>(std::move(reader));
    return setReader(*m_Reader ? &forwardRead : nullptr, m_Reader.get());
}

QcCompile &QcCompile::setLog(smdl_log_fn log, void *user) {
    handle()->log = log;
    handle()->logUser = user;
    return *this;
}

QcCompile &QcCompile::setLog(LogFunction log) {
    m_Log = std::make_unique<LogFunction>(std::move(log));
    return setLog(*m_Log ? &forwardLog : nullptr, m_Log.get());
}

QcCompile &QcCompile::setWriteToGameDir(bool on) {
    handle()->writeToGameDir = on;
    return *this;
}

Result QcCompile::run() {
    smdl_qc_compile *compile = handle();
    compile->lastError.clear();
    std::unique_ptr<smdl_result> raw;
    const smdl_status status = RunToResult(compile, raw);
    if (!raw)
        throw Error(status, compile->lastError);
    Result result(raw.release());
    if (status == SMDL_ERROR_COMPILE)
        throw QcError(std::string(result.errorMessage()), std::string(result.errorFile()), result.errorLine());
    if (status != SMDL_OK)
        throw Error(status, compile->lastError);
    return result;
}

void QcCompile::forwardLog(void *user, smdl_severity severity, const char *text) {
    (*static_cast<const LogFunction *>(user))(static_cast<Severity>(severity), text);
}

int QcCompile::forwardRead(void *user, const char *path, const void **data, size_t *size) {
    auto &self = *static_cast<FileReader *>(user);
    thread_local std::vector<std::byte> keep;
    auto bytes = self(path);
    if (!bytes)
        return 0;
    keep = std::move(*bytes);
    *data = keep.data();
    *size = keep.size();
    return 1;
}

int runCommandLine(std::span<const std::string> args, const LogFunction &log) {
    if (args.empty())
        return 255;
    std::vector<std::string> storage(args.begin(), args.end());
    std::vector<char *> pointers;
    for (std::string &arg: storage)
        pointers.push_back(arg.data());
    std::function<void(MdlSeverity, std::string_view)> sink;
    if (log)
        sink = [&log](MdlSeverity severity, std::string_view text) { log(static_cast<Severity>(severity), text); };
    std::scoped_lock lock(CompileMutex());
    InitProcessOnce();
    return RunStudioMdlLibrary(static_cast<int>(pointers.size()), pointers.data(), std::move(sink));
}

} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
namespace {
    smdl::QcCompile View(smdl_qc_compile *compile) { return smdl::detail::BorrowQcCompile(compile); }

    template<class F>
    smdl_status SetTypedFlag(smdl_qc_compile *c, F &&call) {
        return CallCpp(c, [&] { call(View(c)); });
    }
}

extern "C" {

SMDL_API smdl_qc_compile *smdl_qc_compile_create(void) {
    try {
        return smdl::detail::ReleaseQcCompile(smdl::QcCompile());
    }
    catch (...) {
        return nullptr;
    }
}

SMDL_API void smdl_qc_compile_destroy(smdl_qc_compile *compile) { delete compile; }

SMDL_API const char *smdl_qc_compile_last_error(const smdl_qc_compile *compile) {
    return compile ? compile->lastError.c_str() : "";
}

SMDL_API smdl_status smdl_qc_compile_set_game_dir(smdl_qc_compile *compile, const char *game_dir) {
    return CallCpp(compile, [&] { View(compile).setGameDir(game_dir ? game_dir : ""); });
}

SMDL_API smdl_status smdl_qc_compile_set_qc_path(smdl_qc_compile *compile, const char *qc_path) {
    return CallCpp(compile, [&] { View(compile).setQcPath(qc_path ? qc_path : ""); });
}

SMDL_API smdl_status smdl_qc_compile_set_no_warnings(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setNoWarnings(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_set_max_warnings(smdl_qc_compile *c, int enabled, int max) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) {
        qc.setMaxWarnings(enabled ? std::optional<int>(max) : std::nullopt);
    });
}

SMDL_API smdl_status smdl_qc_compile_set_fit_weights(smdl_qc_compile *c, int enabled, int fitting_poses, int test_poses) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setFitWeights(enabled != 0, fitting_poses, test_poses); });
}

SMDL_API smdl_status smdl_qc_compile_set_flex_optimize(smdl_qc_compile *c, smdl_qc_flex_optimize mode) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setFlexOptimize(static_cast<smdl::FlexOptimize>(mode)); });
}

SMDL_API smdl_status smdl_qc_compile_set_flex_optimize_min_static(smdl_qc_compile *c, int enabled, int vertices) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) {
        qc.setFlexOptimizeMinStatic(enabled ? std::optional<int>(vertices) : std::nullopt);
    });
}

SMDL_API smdl_status smdl_qc_compile_set_strip_lods(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setStripLods(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_set_min_lod(smdl_qc_compile *c, int enabled, int lod) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setMinLod(enabled ? std::optional<int>(lod) : std::nullopt); });
}

SMDL_API smdl_status smdl_qc_compile_set_define_bones(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setDefineBones(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_set_override_define_bones(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setOverrideDefineBones(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_set_check_lengths(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setCheckLengths(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_set_print_bones(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setPrintBones(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_set_dump_materials(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setDumpMaterials(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_set_collapse_report(smdl_qc_compile *c, int enabled) {
    return SetTypedFlag(c, [&](smdl::QcCompile qc) { qc.setCollapseReport(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_add_argument(smdl_qc_compile *compile, const char *argument) {
    if (!compile)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!argument)
        return Fail(compile, SMDL_ERROR_INVALID_ARGUMENT, "argument is null");
    return CallCpp(compile, [&] { View(compile).addArgument(argument); });
}

SMDL_API smdl_status smdl_qc_compile_add_file(smdl_qc_compile *compile, const char *path, const void *data, size_t size) {
    return CallCpp(compile, [&] { View(compile).addFile(path ? path : "", data, size); });
}

SMDL_API smdl_status smdl_qc_compile_set_reader(smdl_qc_compile *compile, smdl_qc_read_fn reader, void *user) {
    return CallCpp(compile, [&] { View(compile).setReader(reader, user); });
}

SMDL_API smdl_status smdl_qc_compile_set_log(smdl_qc_compile *compile, smdl_log_fn log, void *user) {
    return CallCpp(compile, [&] { View(compile).setLog(log, user); });
}

SMDL_API smdl_status smdl_qc_compile_set_write_to_game_dir(smdl_qc_compile *compile, int enabled) {
    return CallCpp(compile, [&] { View(compile).setWriteToGameDir(enabled != 0); });
}

SMDL_API smdl_status smdl_qc_compile_run(smdl_qc_compile *compile, smdl_result **out_result) {
    if (out_result)
        *out_result = nullptr;
    if (!compile)
        return SMDL_ERROR_INVALID_ARGUMENT;
    try {
        compile->lastError.clear();
        std::unique_ptr<smdl_result> result;
        const smdl_status status = RunToResult(compile, result);
        if (out_result)
            *out_result = result.release();
        return status;
    }
    catch (const std::exception &e) {
        return Fail(compile, SMDL_ERROR_INTERNAL, std::format("internal error: {}", e.what()));
    }
    catch (...) {
        return Fail(compile, SMDL_ERROR_INTERNAL, "internal error");
    }
}

SMDL_API int smdl_run_command_line(int argc, const char *const *argv, smdl_log_fn log, void *user) {
    if (argc < 1 || !argv)
        return 255;
    try {
        const std::vector<std::string> args(argv, argv + argc);
        smdl::LogFunction forward;
        if (log) {
            forward = [log, user](smdl::Severity severity, std::string_view text) {
                const std::string copy(text);
                log(user, static_cast<smdl_severity>(severity), copy.c_str());
            };
        }
        return smdl::runCommandLine(args, forward);
    }
    catch (...) {
        return 255;
    }
}

} // extern "C"
