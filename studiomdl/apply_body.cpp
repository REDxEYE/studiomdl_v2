#include "studiomdl/apply_body.hpp"

#include <format>

#include "studiomdl/studiomdl.h"
#include "tier1_v2/function_timing.hpp"

s_bodypart_t &ApplyBodyPart(StudioMdlState &ctx, const std::string_view name) {
    s_bodypart_t &bodypart = ctx.body.AddBodyPart();
    bodypart.name = name;
    return bodypart;
}

s_model_t *ApplyBodyModel(StudioMdlState &ctx, s_bodypart_t &bodypart, int &imodel) {
    auto *pmodel = ctx.arena.Create<s_model_t>();
    imodel = ctx.body.AddModel(pmodel);
    bodypart.pmodel.push_back(pmodel);
    return pmodel;
}

void BeginStudioOption(StudioMdlState &ctx, s_model_t *pmodel, const std::string_view name, const float scale) {
    pmodel->filename = name;

    if (scale != 0.0f) {
        pmodel->scale = ctx.script.currentscale = scale;
    }
    else {
        pmodel->scale = ctx.script.currentscale = ctx.script.defaultscale;
    }

    ctx.importers.pCurrentModel = pmodel;
}

void EndStudioOption(StudioMdlState &ctx) {
    ctx.importers.pCurrentModel = nullptr;

    // Reset currentscale to whatever global we currently have set
    // ctx.script.defaultscale gets set in Cmd_ScaleUp everytime the $scale command is used.
    ctx.script.currentscale = ctx.script.defaultscale;
}

void ApplyBlankModel(StudioMdlState &ctx, s_bodypart_t &bodypart) {
    auto *pmodel = ctx.arena.Create<s_model_t>();
    ctx.body.AddModel(pmodel);

    auto *source = ctx.arena.Create<s_source_t>();
    ctx.anim.sources.emplace_back(source);

    pmodel->source = source;
    bodypart.pmodel.push_back(pmodel);

    pmodel->name = "blank";
}

bool HasBodyGroupPreset(const StudioMdlState &ctx, const std::string_view name) {
    for (const s_bodygrouppreset_t &preset: ctx.body.bodygrouppresets) {
        if (name == preset.name.view())
            return true;
    }
    return false;
}

std::string ApplyBodyGroupPresetValue(BodyGroupPresetBuilder &preset, const s_bodypart_t &bodypart, const int value) {
    const int numModels = static_cast<int>(bodypart.pmodel.size());
    if (value < 0 || value >= numModels) {
        return std::format("Error: can't assign value \"{}\" to bodygroup preset \"{}\" (out of available range).\n",
                           value, preset.name.view());
    }

    const int currentValue = (preset.value / bodypart.base) % numModels;
    preset.value = (preset.value - (currentValue * bodypart.base) + (value * bodypart.base));

    const int currentMask = (preset.mask / bodypart.base) % numModels;
    preset.mask = (preset.mask - (currentMask * bodypart.base) + (1 * bodypart.base));
    return {};
}

void ApplyBodyGroupPreset(StudioMdlState &ctx, const BodyGroupPresetBuilder &preset) {
    s_bodygrouppreset_t &created = ctx.body.bodygrouppresets.emplace_back();
    created.name = preset.name;
    created.iValue = preset.value;
    created.iMask = preset.mask;
}

void ApplyAppendSource(StudioMdlState &ctx, s_source_t *original, s_source_t *append,
                       const std::optional<AppendSourceOffset> &offset) {
    vstd::FunctionTimingRecorder _("API");
    matrix3x4_t matTemp;
    matTemp.SetToIdentity();
    matTemp.ScaleUpper3x3Matrix(ctx.script.currentscale);

    if (offset) {
        AngleMatrix(offset->angles, offset->position, matTemp);
        matTemp.ScaleUpper3x3Matrix(offset->scale * (1.0f / ctx.script.currentscale));
    }

    AddSrcToSrc(ctx, original, append, matTemp);
}
