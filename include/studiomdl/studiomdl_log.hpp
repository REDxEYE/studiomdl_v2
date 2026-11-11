//
// studiomdl console messages, warnings and errors with std::format syntax ("{}", not "%s").
// Any type with a std::formatter works as an argument: std::string, std::string_view, const char *,
// vstd::FixedString (formatter in tier1_v2/fixed_string.hpp), numbers, ...
// The format string is checked at compile time.
//

#pragma once

#include <format>
#include <print>
#include <string_view>

#include "tier1_v2/fixed_string.hpp"

// Type-erased implementations (studiomdl/studiomdl_errors.cpp); call the templates below instead.
[[noreturn]] void MdlErrorV(std::string_view fmt, std::format_args args);
void MdlWarningV(std::string_view fmt, std::format_args args);
void MdlMessageV(std::string_view fmt, std::format_args args);

class ScriptTokenizer;
// Routes the tokenizer's errors ("<file>(<line>): - <message>", see ScriptTokenizer::Error) through MdlError.
void InstallMdlErrorHandler(ScriptTokenizer &tokenizer);

// Prints "ERROR: <message>", removes the partially written output files and exits.
// The first message of a -quiet run is preceded by the QC path, so batch logs show which model failed.
template<typename... Args>
[[noreturn]] void MdlError(std::format_string<Args...> fmt, Args &&...args) {
    MdlErrorV(fmt.get(), std::make_format_args(args...));
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
