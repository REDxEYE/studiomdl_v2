// studiomdl C++ API: compile results (implemented in api/src/compile.cpp; smdl_compile.h is the C form)
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "../c/smdl_compile.h"

namespace smdl {

struct OutputFile {
    std::string_view relativePath; // "models/props/crate.mdl"
    std::span<const std::byte> data;
};

struct Message {
    Severity severity;
    std::string_view text;
};

// Owns an smdl_result. Views returned by the accessors live as long as the Result.
class Result {
public:
    explicit Result(smdl_result *result) : m_Result(result) {}

    smdl_result *handle() const { return m_Result.get(); }

    smdl_status status() const;
    bool ok() const { return status() == SMDL_OK; }

    std::vector<OutputFile> files() const;
    std::vector<Message> messages() const;

    int warningCount() const;
    std::string_view errorMessage() const;
    std::string_view errorObject() const;
    // QC compiles: the QC file and line of the error ("" / 0 when unknown)
    std::string_view errorFile() const;
    int errorLine() const;

    // <gameDir>/models/<name>.*; throws Error(SMDL_ERROR_IO) when a file can't be written
    void save(std::string_view gameDir) const;
    // <dir>/models/<name>.*
    void saveTo(std::string_view dir) const;

private:
    struct Deleter {
        void operator()(smdl_result *result) const; // api/src/compile.cpp
    };
    std::unique_ptr<smdl_result, Deleter> m_Result;
};

} // namespace smdl
