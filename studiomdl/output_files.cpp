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

size_t LoadOutputFile(const std::string_view fileName, void** buffer) {
    const OutputFile* file = FindOutputFile(fileName);
    if (!file) {
        const std::string tmp(fileName);
        return LoadFile(tmp.c_str(), buffer);
    }

    const size_t size = file->data.size();
    auto* copy = static_cast<uint8_t*>(malloc(size + 1));
    memcpy(copy, file->data.data(), size);
    copy[size] = 0;
    *buffer = copy;
    return size;
}

std::optional<vstd::Buffer> LoadOutputFile(std::string_view fileName) {
    const OutputFile* file = FindOutputFile(fileName);
    if (!file) {
        const std::string tmp(fileName);
        void* raw_data;
        const size_t size= LoadFile(tmp.c_str(), &raw_data);
        if (size == 0)
            return std::nullopt;
        return vstd::Buffer::copy_of(vstd::ConstByteBufferView(static_cast<uint8_t*>(raw_data), size));
    }
    return vstd::Buffer::copy_of(file->data.view());
}

void CommitOutputFiles() {
    vstd::FunctionTimingRecorder _("Write");
    for (auto &[name, data]: s_outputFiles) {
        SaveFile(name.c_str(), data.data(), static_cast<int>(data.size()));
    }
    s_outputFiles.clear();
}
