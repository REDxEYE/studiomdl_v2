#include "studiomdl/output_files.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "tier1_v2/function_timing.hpp"
#include "common/cmdlib.h"

namespace {
    struct OutputFile {
        std::string name;
        vstd::Buffer data;
    };

    // a handful of files per model: a vector keeps the write order deterministic
    std::vector<OutputFile> s_outputFiles;

    OutputFile* FindOutputFile(const std::string_view fileName) {
        for (OutputFile &file: s_outputFiles) {
            if (file.name == fileName)
                return &file;
        }
        return nullptr;
    }
}

void SaveOutputFile(const std::string_view fileName, vstd::Buffer &&data) {
    if (OutputFile* file = FindOutputFile(fileName))
        file->data = std::move(data);
    else
        s_outputFiles.emplace_back(std::string(fileName), std::move(data));
}

void SaveOutputFile(const std::string_view fileName, const void* data, const size_t size) {
    SaveOutputFile(fileName, vstd::Buffer::copy_of(vstd::ConstByteBufferView(static_cast<const uint8_t*>(data), size)));
}


std::optional<vstd::Buffer> LoadOutputFile(const std::string_view fileName) {
    const OutputFile* file = FindOutputFile(fileName);
    if (!file) {
        return LoadFile(fileName);
    }
    return vstd::Buffer::copy_of(file->data.view());
}

void CommitOutputFiles() {
    vstd::FunctionTimingRecorder _("Write");
    for (auto &[name, data]: s_outputFiles) {
        SaveFile(name, data);
    }
    s_outputFiles.clear();
}
