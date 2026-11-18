//===========================================================================//
//
// Purpose: Files that a host provides instead of the disk. While a FileOverlay is installed, the lowest level of the
//          filesystem (CStdFile::Open for reading, FS_Stat) asks it first, so every reader above it (QC and $include,
//          SMD/VTA/OBJ, DMX/.mpp, gameinfo.txt, KeyValues) sees the host's bytes without knowing about it. Writing
//          always goes to the disk.
//
//===========================================================================//

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using FileOverlayData = std::shared_ptr<const std::vector<std::byte>>;

class IFileOverlay {
public:
    virtual ~IFileOverlay() = default;

    // The contents of the file at `path` (FS_NormalizeOverlayPath form), or null when this overlay doesn't provide it
    // and the disk decides. Called for every open and stat of a file while the overlay is installed.
    virtual FileOverlayData Find(const std::string &path) = 0;
};

// The form in which paths reach IFileOverlay::Find: absolute (against the current directory when relative), lexically
// normalized (no "." / ".."), '/' separators; on Windows lower case (ASCII), as the file system compares there.
std::string FS_NormalizeOverlayPath(std::string_view path);

// Installs the overlay for the process (the compile lock makes this the only compile) until destroyed; the previous
// one comes back. The overlay must outlive the scope.
class ScopedFileOverlay {
public:
    explicit ScopedFileOverlay(IFileOverlay &overlay);
    ~ScopedFileOverlay();

    ScopedFileOverlay(const ScopedFileOverlay &) = delete;
    ScopedFileOverlay &operator=(const ScopedFileOverlay &) = delete;

private:
    IFileOverlay *m_pPrevious;
};
