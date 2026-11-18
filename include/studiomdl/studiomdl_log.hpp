//
// studiomdl console messages, warnings and errors with std::format syntax ("{}", not "%s").
// Any type with a std::formatter works as an argument: std::string, std::string_view, const char *,
// vstd::FixedString (formatter in tier1_v2/fixed_string.hpp), numbers, ...
// The format string is checked at compile time.
//
// Where the text goes: without a diagnostics sink (the console tool) to stdout; with ctx.diagnostics.sink (a library
// compile) to the sink, as complete messages (MdlPrint/MdlMessage text is split into lines), never to stdout.
// Use these instead of std::print/printf for everything a compile writes to the console.
//

#pragma once

#include <format>
#include <print>
#include <string_view>

// Type-erased implementations (studiomdl/studiomdl_errors.cpp); call the templates below instead.
[[noreturn]] void MdlErrorV(std::string_view fmt, std::format_args args);
void MdlWarningV(std::string_view fmt, std::format_args args);
void MdlMessageV(std::string_view fmt, std::format_args args);
void MdlPrintV(std::string_view fmt, std::format_args args);
// MdlError for a script error: file and line (empty and 0 when unknown) become the position of the MdlCompileError
[[noreturn]] void MdlErrorAtV(std::string_view file, int line, std::string_view fmt, std::format_args args);

class ScriptTokenizer;
// Routes the tokenizer's errors ("<file>(<line>): - <message>", see ScriptTokenizer::Error) through MdlErrorAt, with the
// script file and line as the position of the MdlCompileError.
void InstallMdlErrorHandler(ScriptTokenizer &tokenizer);

// Reports "ERROR: <message>" and ends the compile by throwing MdlCompileError (compile_error.hpp). The entry point
// catches it: the console tool removes the partially written output files (MdlAbortCompile), the C API returns the
// message in its result. Never returns, never exits the process: no code may call MdlError from a destructor or from
// a C callback (qsort comparator, qhull): destroying frames or unwinding through C frames is not safe there.
// The first message of a -quiet run is preceded by the QC path, so batch logs show which model failed.
template<typename... Args>
[[noreturn]] void MdlError(std::format_string<Args...> fmt, Args &&...args) {
    MdlErrorV(fmt.get(), std::make_format_args(args...));
}

template<typename... Args>
[[noreturn]] void MdlErrorAt(std::string_view file, int line, std::format_string<Args...> fmt, Args &&...args) {
    MdlErrorAtV(file, line, fmt.get(), std::make_format_args(args...));
}

// Prints "WARNING: <message>" in yellow, unless warnings are disabled or the -maxwarnings budget is used up.
template<typename... Args>
void MdlWarning(std::format_string<Args...> fmt, Args &&...args) {
    MdlWarningV(fmt.get(), std::make_format_args(args...));
}

// Prints the message unless the compile runs with -quiet.
template<typename... Args>
void MdlMessage(std::format_string<Args...> fmt, Args &&...args) {
    MdlMessageV(fmt.get(), std::make_format_args(args...));
}

// Prints the text as it is, even when the compile runs with -quiet (the caller decides, as with std::print).
template<typename... Args>
void MdlPrint(std::format_string<Args...> fmt, Args &&...args) {
    MdlPrintV(fmt.get(), std::make_format_args(args...));
}
