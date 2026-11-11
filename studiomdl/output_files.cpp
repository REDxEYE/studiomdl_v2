#include "studiomdl/output_files.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "tier1_v2/function_timing.hpp"
#include "common/cmdlib.h"

namespace {
    struct OutputFile {
        std::string name;
        std::vector<uint8_t> data;
    };

    // a handful of files per model: a vector keeps the write order deterministic
    std::vector<OutputFile> s_outputFiles;

    OutputFile* FindOutputFile(const std::string &fileName) {
        for (OutputFile &file: s_outputFiles) {
            if (file.name == fileName)
                return &file;
        }
        return nullptr;
    }
}

void SaveOutputFile(const std::string &fileName, const void* data, const size_t size) {
    OutputFile* file = FindOutputFile(fileName);
    if (!file)
        file = &s_outputFiles.emplace_back(OutputFile{fileName, {}});

    const auto* bytes = static_cast<const uint8_t*>(data);
    file->data.assign(bytes, bytes + size);
}

int LoadOutputFile(const std::string &fileName, void** buffer) {
    const OutputFile* file = FindOutputFile(fileName);
    if (!file)
        return LoadFile(fileName.c_str(), buffer);

    const size_t size = file->data.size();
    auto* copy = static_cast<uint8_t*>(malloc(size + 1));
    memcpy(copy, file->data.data(), size);
    copy[size] = 0;
    *buffer = copy;
    return static_cast<int>(size);
}

void CommitOutputFiles() {
    vstd::FunctionTimingRecorder _("Write");
    for (OutputFile &file: s_outputFiles) {
        SaveFile(file.name.c_str(), file.data.data(), static_cast<int>(file.data.size()));
    }
    s_outputFiles.clear();
}
