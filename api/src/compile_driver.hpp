// The pieces of one compile that the model builder (compile.cpp) and the QC compile (smdl_qc.cpp) share: the result
// object, the process-wide compile lock and the scopes that set up and tear down the process state around a compile.
#pragma once

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "studiomdl/c/smdl_compile.h"

#include "mathlib/mathlib.h"
#include "studiomdl/processing.hpp"
#include "studiomdl_errors.h"
#include "tier0/logging.h"
#include "tier1_v2/path_utils.hpp"

struct smdl_result {
    struct File {
        std::string relativePath;
        std::vector<std::byte> data;
    };
    struct Message {
        smdl_severity severity;
        std::string text;
    };

    smdl_status status = SMDL_OK;
    std::vector<File> files;
    std::vector<Message> messages;
    int warningCount = 0;
    std::string errorMessage;
    std::string errorObject;
    std::string errorFile; // QC compiles: the script file and line of the error
    int errorLine = 0;
};

namespace smdl_api {

// One compile at a time per process
inline std::mutex &CompileMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void InitProcessOnce() {
    static std::once_flag once;
    std::call_once(once, [] { MathLib_Init(2.2f, 2.2f, 0.0f, 2.0f); });
}

// Absolute directory with '/' separators and a trailing '/', as ctx.paths.gameDir is everywhere
inline std::string GameDirString(const std::filesystem::path &dir) {
    std::string out = vstd::paths::to_utf8(dir.lexically_normal());
    std::ranges::replace(out, '\\', '/');
    if (out.empty() || out.back() != '/')
        out += '/';
    return out;
}

struct ProcessingScope {
    bool active = false;
    ~ProcessingScope() {
        if (active)
            EndProcessing();
    }
};

struct LoggingScope {
    LoggingScope() {
        LoggingSystem_PushLoggingState();
        LoggingSystem_RegisterLoggingListener(&Listener());
    }
    ~LoggingScope() { LoggingSystem_PopLoggingState(); }

    static CMdlLoggingListener &Listener() {
        static CMdlLoggingListener listener;
        return listener;
    }
};

} // namespace smdl_api
