// studiomdl C++ API: compiling an existing QC / .mpp file (implemented in api/src/smdl_qc.cpp; smdl_qc.h is the C form)
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "result.hpp"
#include "model.hpp" // LogFunction
#include "../c/smdl_qc.h"

namespace smdl {

enum class FlexOptimize : int { Auto = SMDL_QC_FLEXOPT_AUTO, On = SMDL_QC_FLEXOPT_ON, Off = SMDL_QC_FLEXOPT_OFF };

// A failed QC compile: the message plus the QC file and line it was reported at (empty / 0 when unknown)
class QcError : public Error {
public:
    QcError(const std::string &message, std::string file, int line)
        : Error(SMDL_ERROR_COMPILE, message), m_File(std::move(file)), m_Line(line) {}

    const std::string &file() const { return m_File; }
    int line() const { return m_Line; }

private:
    std::string m_File;
    int m_Line;
};

// Returns the bytes of a file, or nothing for "not mine" (the disk decides). The path is absolute, '/' separated.
using FileReader = std::function<std::optional<std::vector<std::byte>>(std::string_view path)>;

class QcCompile;

namespace detail {
    // A non-owning QcCompile over a handle, and the handle taken out of an owning one (the C API wraps the C++ API with
    // them). Not for applications.
    QcCompile BorrowQcCompile(smdl_qc_compile *compile);
    smdl_qc_compile *ReleaseQcCompile(QcCompile &&compile);
}

class QcCompile {
public:
    QcCompile();
    QcCompile(QcCompile &&) noexcept = default;
    QcCompile &operator=(QcCompile &&) noexcept = default;

    smdl_qc_compile *handle() const { return m_Handle.get(); }

    // Empty = no game directory (outputs in memory only)
    QcCompile &setGameDir(std::string_view gameDir);
    QcCompile &setQcPath(std::string_view path);

    QcCompile &setNoWarnings(bool on);
    QcCompile &setMaxWarnings(std::optional<int> max);
    // poses 0 = the default (24)
    QcCompile &setFitWeights(bool on, int fittingPoses = 0, int testPoses = 0);
    QcCompile &setFlexOptimize(FlexOptimize mode);
    QcCompile &setFlexOptimizeMinStatic(std::optional<int> vertices);
    QcCompile &setStripLods(bool on);
    QcCompile &setMinLod(std::optional<int> lod);
    QcCompile &setDefineBones(bool on);
    QcCompile &setOverrideDefineBones(bool on);
    QcCompile &setCheckLengths(bool on);
    QcCompile &setPrintBones(bool on);
    QcCompile &setDumpMaterials(bool on);
    QcCompile &setCollapseReport(bool on);

    // One command line token ("-maxwarnings", then "10"); validated by studiomdl's own parser
    QcCompile &addArgument(std::string_view argument);

    // A file the compile reads instead of (or without) the disk; copied
    QcCompile &addFile(std::string_view path, const void *data, size_t size);
    QcCompile &addFile(std::string_view path, std::span<const std::byte> data) {
        return addFile(path, data.data(), data.size());
    }
    QcCompile &addFile(std::string_view path, std::string_view text) { return addFile(path, text.data(), text.size()); }

    QcCompile &setReader(FileReader reader);
    QcCompile &setReader(smdl_qc_read_fn reader, void *user);

    QcCompile &setLog(LogFunction log);
    QcCompile &setLog(smdl_log_fn log, void *user);

    QcCompile &setWriteToGameDir(bool on);

    // Throws QcError (message, QC file and line) when the compile fails
    Result run();

private:
    friend QcCompile detail::BorrowQcCompile(smdl_qc_compile *compile);
    friend smdl_qc_compile *detail::ReleaseQcCompile(QcCompile &&compile);
    struct Borrowed {};

    static void forwardLog(void *user, smdl_severity severity, const char *text);
    static int forwardRead(void *user, const char *path, const void **data, size_t *size);

    struct Deleter {
        bool owned = true;
        void operator()(smdl_qc_compile *compile) const; // api/src/smdl_qc.cpp
    };
    QcCompile(smdl_qc_compile *compile, Borrowed) : m_Handle(compile, Deleter{false}) {}

    std::unique_ptr<smdl_qc_compile, Deleter> m_Handle;
    std::unique_ptr<FileReader> m_Reader;
    std::unique_ptr<LogFunction> m_Log;
};

// The whole console tool: returns studiomdl_v2's exit code. argv[0] is the program name, the QC file is last.
int runCommandLine(std::span<const std::string> args, const LogFunction &log = {});

} // namespace smdl
