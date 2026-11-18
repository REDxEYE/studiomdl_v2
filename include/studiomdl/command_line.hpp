#pragma once

#include <span>
#include <string_view>

struct StudioMdlState;

// The studiomdl command line switches, parsed into ctx.options and friends. Shared by the console tool and by library
// hosts (a QC compile through the C API), so a switch means the same in both.
enum class ArgumentMode {
    // argv of the console tool: args[0] is the program, the QC file is the last argument. Switches that aren't
    // studiomdl's own (-game and the other options of the Valve tool framework, read elsewhere) are ignored.
    CommandLine,
    // Library: only switches and their values, no program and no QC file. A positional argument, an unknown switch and
    // a switch that needs the console or the current directory (-makefile, -perf, -d, -mdlreport) is an error.
    Options,
};

enum class ArgumentsResult {
    Compile,   // compile ctx.paths.qcPath
    MdlReport, // -mdlreport: the console tool reports instead of compiling (CommandLine mode only)
};

// Starts from SetCompileDefaults(ctx), then applies the arguments in order. CommandLine mode also sets
// ctx.paths.qcPath and outname from the last argument (SetCompileInput). Errors: MdlError (prints, throws
// MdlCompileError), as the console tool always reported them; the usage text for a malformed command line.
ArgumentsResult ParseStudioMdlArguments(StudioMdlState &ctx, std::span<const std::string_view> args, ArgumentMode mode);

// ctx.paths.qcPath = the QC (or .mpp) as the user gave it; outname = the same without extension
void SetCompileInput(StudioMdlState &ctx, std::string_view qcPath);

// MdlError "Bad or missing options" with the usage text
[[noreturn]] void StudioMdlUsageError();
