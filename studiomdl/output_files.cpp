#include "studiomdl/output_files.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "tier1_v2/function_timing.hpp"
#include "common/cmdlib.h"
#include "studiomdl/studiomdl_global_state.hpp"

namespace {
    using OutputFile = StudioMdlWriteState::OutputFile;

    OutputFile* FindOutputFile(StudioMdlState &ctx, const std::string_view fileName) {
        for (OutputFile &file: ctx.write.outputFiles) {
            if (file.name == fileName)
                return &file;
        }
        return nullptr;
    }
}

void SaveOutputFile(StudioMdlState &ctx, const std::string_view fileName, vstd::Buffer &&data) {
    if (OutputFile* file = FindOutputFile(ctx, fileName))
        file->data = std::move(data);
    else
        ctx.write.outputFiles.emplace_back(std::string(fileName), std::move(data));
}

void SaveOutputFile(StudioMdlState &ctx, const std::string_view fileName, const void* data, const size_t size) {
    SaveOutputFile(ctx, fileName,
                   vstd::Buffer::copy_of(vstd::ConstByteBufferView(static_cast<const uint8_t*>(data), size)));
}


std::optional<vstd::Buffer> LoadOutputFile(StudioMdlState &ctx, const std::string_view fileName) {
    const OutputFile* file = FindOutputFile(ctx, fileName);
    if (!file) {
        return LoadFile(ctx.paths.content, fileName);
    }
    return vstd::Buffer::copy_of(file->data.view());
}

void CommitOutputFiles(StudioMdlState &ctx) {
    vstd::FunctionTimingRecorder _("Write");
    for (auto &[name, data]: ctx.write.outputFiles) {
        SaveFile(name, data);
    }
    ctx.write.outputFiles.clear();
}
