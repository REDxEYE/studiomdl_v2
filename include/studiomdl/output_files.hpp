//
// Model output files (.mdl, .ani, .vvd, .vtx) are built in memory and written to disk once, by CommitOutputFiles().
// The writer reads several of them back for fixups (vertex sort, root LOD clamp, perf stats), and rewriting a file
// on disk is slow: btrfs and ext4 start writeback in close() when a non-empty file was truncated and rewritten.
//

#pragma once

#include <cstddef>
#include <string>

// Stores (or replaces) the contents of an output file. fileName is the full UTF-8 path it will be written to.
void SaveOutputFile(const std::string& fileName, const void *data, size_t size);

// LoadFile contract: *buffer is malloc'ed with a terminating 0 byte (the caller frees it), returns the size.
// Reads the stored contents, or the file on disk when nothing was stored under that name.
int LoadOutputFile(const std::string& fileName, void **buffer);

// Writes every stored file, in the order they were first stored, and forgets them.
void CommitOutputFiles();
