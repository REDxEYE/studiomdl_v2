//
// Model output files (.mdl, .ani, .vvd, .vtx) are built in memory and written to disk once, by CommitOutputFiles().
// The writer reads several of them back for fixups (vertex sort, root LOD clamp, perf stats), and rewriting a file
// on disk is slow: btrfs and ext4 start writeback in close() when a non-empty file was truncated and rewritten.
// The stored files live in ctx.write.outputFiles.
//

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "tier1_v2/buffer.hpp"

struct StudioMdlState;

// Same, taking over a finished buffer without copying it.
void SaveOutputFile(StudioMdlState &ctx, std::string_view fileName, vstd::Buffer &&data);

// Stores (or replaces) the contents of an output file. fileName is the full UTF-8 path it will be written to.
void SaveOutputFile(StudioMdlState &ctx, std::string_view fileName, const void *data, size_t size);

// The stored file, or the file on disk when it isn't stored
std::optional<vstd::Buffer> LoadOutputFile(StudioMdlState &ctx, std::string_view fileName);

// Writes every stored file, in the order they were first stored, and forgets them.
void CommitOutputFiles(StudioMdlState &ctx);
