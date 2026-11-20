// smdl::Model::compile and smdl::Result (cpp/model.hpp, cpp/result.hpp): one builder compile (process lock, filesystem,
// StudioMdlState, the part apply steps, the shared back end) and the result object. smdl_compile.h wraps them.
#include "studiomdl/c/smdl_compile.h"
#include "studiomdl/studiomdl.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

#include "api_util.hpp"
#include "apply.hpp"
#include "compile_driver.hpp"

#include "datamodel/idatamodel.h"
#include "filesystem.h"
#include "filesystem_init.h"
#include "interfaces/interfaces.h"
#include "mathlib/mathlib.h"
#include "studiomdl/compile.hpp"
#include "studiomdl/compile_error.hpp"
#include "studiomdl/processing.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_errors.h"
#include "tier0/logging.h"
#include "tier1_v2/function_timing.hpp"
#include "tier1_v2/path_utils.hpp"

using namespace smdl_api;



std::string ApplyState::objectPath() const {
    std::string path;
    for (const std::string &object: objectStack) {
        if (!path.empty())
            path += " / ";
        path += object;
    }
    return path;
}

namespace {
    // (the lock, process init, GameDirString and the scopes are in compile_driver.hpp)

    void RunCompile(const smdl_model &model, const char *gameDirArg, smdl_log_fn log, void *user, smdl_result &result) {
        InitProcessOnce();

        LoggingScope logging;
        ProcessingScope processing;
        auto pCtx = std::make_unique<StudioMdlState>();
        StudioMdlState &ctx = *pCtx;
        ScopedMdlDiagnostics diagnostics(ctx);
        ctx.diagnostics.sink = [&](MdlSeverity severity, std::string_view text) {
            const auto cSeverity = static_cast<smdl_severity>(severity);
            result.messages.push_back({cSeverity, std::string(text)});
            if (log)
                log(user, cSeverity, result.messages.back().text.c_str());
        };

        ApplyState state(model);
        // BeginProcessing resets the timing rows. Start the API scope only after that reset.
        std::optional<vstd::FunctionTimingRecorder> timing;
        try {
            if (!BeginProcessing())
                throw MdlCompileError("could not initialize the datamodel");
            processing.active = true;
            timing.emplace("API", false, std::source_location::current());
            g_pFileSystem = g_pFullFileSystem;

            SetCompileDefaults(ctx);

            std::error_code ec;
            if (gameDirArg) {
                const std::filesystem::path dir = std::filesystem::absolute(vstd::paths::to_fs(gameDirArg), ec);
                if (ec || !std::filesystem::is_directory(dir))
                    throw MdlCompileError(std::format("game directory '{}' does not exist", gameDirArg));
                const std::string gameDir = GameDirString(dir);
                if (FileSystem_LoadSearchPaths(g_pFullFileSystem, gameDir) != FS_OK)
                    throw MdlCompileError(std::format("game directory '{}' has no valid gameinfo.txt", gameDirArg));
                FileSystem_AddSearchPath_Platform(g_pFullFileSystem, gameDir);
                ctx.paths.gameDir = gameDir;
            }
            else {
                // Outputs are only kept in memory: the directory is a key prefix and is never created
                ctx.paths.gameDir = GameDirString(std::filesystem::temp_directory_path(ec) / "smdl_virtual_game");
            }
            ctx.paths.qdir = ctx.paths.gameDir;
            g_pFullFileSystem->AddSearchPath("", IBaseFileSystem::PathID::Local, PATH_ADD_TO_HEAD);

            // default to having one entry in the LOD list that doesn't do anything (as CompileModel does)
            ctx.body.ScriptLODs.clear();
            ctx.body.ScriptLODs.emplace_back();
            ctx.body.ScriptLODs[0].switchValue = 0.0f;
            ClearModel();

            ProcessModelName(ctx, model.name);

            ApplyGlobals(ctx, state);
            ApplySkeleton(ctx, state);
            ApplyBody(ctx, state);
            ApplyFlex(ctx, state);
            ApplyLods(ctx, state);
            ApplyRig(ctx, state);
            ApplyAnimations(ctx, state);
            ApplySequences(ctx, state);
            ApplyBoneDynamics(ctx, state);
            ApplyCollision(ctx, state);
            ApplyFinish(ctx, state);

            BuildModelFiles(ctx);

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
            result.errorObject = state.failedObject;
            result.files.clear();
        }
        catch (const Plat_ExitException &e) {
            // tier0 Error() / LR_ABORT (already reported through the log): a failed compile like MdlError
            std::string_view message = e.what();
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.remove_suffix(1);
            result.status = SMDL_ERROR_COMPILE;
            result.errorMessage = std::string(message);
            result.errorObject = state.failedObject;
            result.files.clear();
        }
        catch (const std::exception &e) {
            result.status = SMDL_ERROR_INTERNAL;
            result.errorMessage = std::format("internal error: {}", e.what());
            result.errorObject = state.failedObject;
            result.files.clear();
        }
        // Finish the outer API scope before formatting: its own/total durations are finalized in the destructor.
        timing.reset();
        result.warningCount = ctx.diagnostics.warningCount;
        // Whatever a failed compile left in the datamodel (files, elements that belong to none) goes now, so
        // EndProcessing finds nothing leaked
        if (processing.active)
            MdlDiscardDataModelContents();
        if (ctx.options.perf) {
            result.messages.emplace_back(SMDL_SEVERITY_MESSAGE, vstd::format_function_timings());
        }
    }

    smdl_status SaveFiles(const smdl_result *result, const char *root) {
        if (!result || !root || !*root)
            return SMDL_ERROR_INVALID_ARGUMENT;
        try {
            const std::filesystem::path base = vstd::paths::to_fs(root);
            for (const auto &file: result->files) {
                const std::filesystem::path path = base / vstd::paths::to_fs(file.relativePath);
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char *>(file.data.data()), static_cast<std::streamsize>(file.data.size()));
                if (!out)
                    return SMDL_ERROR_IO;
            }
            return SMDL_OK;
        }
        catch (...) {
            return SMDL_ERROR_IO;
        }
    }

    // The whole compile under the process lock; never throws for a failed compile (the result says so)
    std::unique_ptr<smdl_result> CompileToResult(const smdl_model &model, const char *gameDir, smdl_log_fn log,
                                                 void *user) {
        auto result = std::make_unique<smdl_result>();
        std::scoped_lock lock(CompileMutex());
        RunCompile(model, gameDir, log, user, *result);
        return result;
    }
}

namespace smdl {

Result Model::compile(const std::string_view gameDir, const LogFunction &log) const {
    const std::string dir(gameDir);
    Result result(CompileToResult(*handle(), dir.empty() ? nullptr : dir.c_str(), log ? &forwardLog : nullptr,
                                  log ? const_cast<LogFunction *>(&log) : nullptr)
                      .release());
    if (!result.ok())
        throw Error(result.status(), std::string(result.errorMessage()), std::string(result.errorObject()));
    return result;
}

void Result::Deleter::operator()(smdl_result *result) const { delete result; }

smdl_status Result::status() const { return handle()->status; }

std::vector<OutputFile> Result::files() const {
    std::vector<OutputFile> out;
    out.reserve(handle()->files.size());
    for (const auto &file: handle()->files)
        out.push_back({file.relativePath, {file.data.data(), file.data.size()}});
    return out;
}

std::vector<Message> Result::messages() const {
    std::vector<Message> out;
    out.reserve(handle()->messages.size());
    for (const auto &message: handle()->messages)
        out.push_back({static_cast<Severity>(message.severity), message.text});
    return out;
}

int Result::warningCount() const { return handle()->warningCount; }
std::string_view Result::errorMessage() const { return handle()->errorMessage; }
std::string_view Result::errorObject() const { return handle()->errorObject; }
std::string_view Result::errorFile() const { return handle()->errorFile; }
int Result::errorLine() const { return handle()->errorLine; }

void Result::save(const std::string_view gameDir) const { saveTo(gameDir); }

void Result::saveTo(const std::string_view dir) const {
    const smdl_status status = SaveFiles(handle(), std::string(dir).c_str());
    if (status != SMDL_OK)
        throw Error(status, smdl_status_string(status));
}

} // namespace smdl

extern "C" {

SMDL_API smdl_status smdl_model_compile(const smdl_model *model, const char *game_dir, smdl_log_fn log, void *user,
                                        smdl_result **out_result) {
    if (out_result)
        *out_result = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return Guard(model, [&] {
        std::unique_ptr<smdl_result> result = CompileToResult(*model, game_dir, log, user);
        const smdl_status status = result->status;
        if (status != SMDL_OK)
            RecordError(model, "{}", result->errorMessage);
        else
            Ok(model);
        if (out_result)
            *out_result = result.release();
        return status;
    });
}

SMDL_API void smdl_result_destroy(smdl_result *result) { delete result; }

SMDL_API smdl_status smdl_result_status(const smdl_result *result) {
    return result ? result->status : SMDL_ERROR_INVALID_ARGUMENT;
}

SMDL_API size_t smdl_result_file_count(const smdl_result *result) { return result ? result->files.size() : 0; }

SMDL_API smdl_status smdl_result_file(const smdl_result *result, size_t index, const char **relative_path,
                                      const void **data, size_t *size) {
    if (!result)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (index >= result->files.size())
        return SMDL_ERROR_OUT_OF_RANGE;
    const auto &file = result->files[index];
    if (relative_path)
        *relative_path = file.relativePath.c_str();
    if (data)
        *data = file.data.data();
    if (size)
        *size = file.data.size();
    return SMDL_OK;
}

SMDL_API smdl_status smdl_result_save(const smdl_result *result, const char *game_dir) {
    return SaveFiles(result, game_dir);
}

SMDL_API smdl_status smdl_result_save_to(const smdl_result *result, const char *dir) { return SaveFiles(result, dir); }

SMDL_API int smdl_result_warning_count(const smdl_result *result) { return result ? result->warningCount : 0; }

SMDL_API size_t smdl_result_message_count(const smdl_result *result) { return result ? result->messages.size() : 0; }

SMDL_API smdl_status smdl_result_message(const smdl_result *result, size_t index, smdl_severity *severity,
                                         const char **text) {
    if (!result)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (index >= result->messages.size())
        return SMDL_ERROR_OUT_OF_RANGE;
    if (severity)
        *severity = result->messages[index].severity;
    if (text)
        *text = result->messages[index].text.c_str();
    return SMDL_OK;
}

SMDL_API const char *smdl_result_error_message(const smdl_result *result) {
    return result ? result->errorMessage.c_str() : "";
}

SMDL_API const char *smdl_result_error_object(const smdl_result *result) {
    return result ? result->errorObject.c_str() : "";
}

SMDL_API const char *smdl_result_error_file(const smdl_result *result) {
    return result ? result->errorFile.c_str() : "";
}

SMDL_API int smdl_result_error_line(const smdl_result *result) { return result ? result->errorLine : 0; }

} // extern "C"
