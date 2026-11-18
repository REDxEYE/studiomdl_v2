#pragma once

#include <string_view>

struct StudioMdlState;

// The values a compile starts from before the options and the QC change them (default scale and rotation, the
// default weight list, normal blend angle, gamma...)
void SetCompileDefaults(StudioMdlState &ctx);

// Sets qdir from the QC (or .mpp) path and gameDir from the directory of gameinfo.txt, both absolute
void SetupCompileDirectories(StudioMdlState &ctx, std::string_view qcPath, std::string_view gameInfoPath);

enum class CompileOutput {
    WriteFiles,   // the console tool: the files are written to disk (CommitOutputFiles)
    KeepInMemory, // library hosts: the files stay in ctx.write.outputFiles for the caller
};

// Compiles ctx.paths.qcPath: parses the QC or .mpp, processes the model and writes the .mdl/.vvd/.vtx/.phy files
// (or, with -makefile, appends the make rule). Needs the game search paths and SetupCompileDirectories. Returns the
// exit code; errors go through MdlError.
int CompileModel(StudioMdlState &ctx, CompileOutput output = CompileOutput::WriteFiles);

// The shared back end of every front end (QC, .mpp, the model builder): processes the parsed model (flex optimize,
// vertex clamp, skins, simplify, checks) and builds the .mdl/.ani/.vvd/.vtx/.phy files into ctx.write.outputFiles.
// Writes nothing to disk (CommitOutputFiles does). Returns false when the compile ends before the files are built
// because -definebones only lists the bones (nothing is stored); true otherwise.
bool BuildModelFiles(StudioMdlState &ctx);
