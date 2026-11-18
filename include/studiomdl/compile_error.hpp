#pragma once

#include <stdexcept>
#include <string>

// Thrown by MdlError, MdlErrorAt, the QC tokenizer's errors and every other studiomdl error: a compile that fails is
// abandoned by unwinding to the entry point (CLI RunStudioMdl, the C API compile driver), which reports it. what() is
// the message as it was printed, without "ERROR: " and the trailing line end. file/line: the QC position for script
// errors, else empty / 0.
// (Fatal errors of the libraries - tier0 Error(), an LR_ABORT log response - are Plat_ExitException, allocation
// failure is std::bad_alloc; the entry points handle those as well.)
class MdlCompileError : public std::runtime_error {
public:
    explicit MdlCompileError(const std::string &message, std::string file = {}, int line = 0)
        : std::runtime_error(message), m_File(std::move(file)), m_Line(line) {}

    const std::string &file() const { return m_File; }
    int line() const { return m_Line; }

private:
    std::string m_File;
    int m_Line;
};
