#include "studiomdl/apply_lod.hpp"

#include <cstring>

#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_log.hpp"
#include "tier1/strtools.h"

LodBeginResult ApplyLodBegin(StudioMdlState &ctx, const bool isShadow, LodScriptData_t *&lod) {
    if (isShadow && !ctx.options.quiet) {
        MdlPrint("Processing $shadowlod\n");
    }

    if (ctx.header.gflags & STUDIOHDR_FLAGS_HASSHADOWLOD)
        return LodBeginResult::ShadowLodNotLast;

    lod = &ctx.body.ScriptLODs.emplace_back();

    if (std::ssize(ctx.body.ScriptLODs) > MAX_NUM_LODS)
        return LodBeginResult::TooManyLods;

    // Shadow lod reserves -1 as switch value which uniquely identifies a shadow lod
    lod->switchValue = -1.0f;

    if (isShadow) {
        // Disable facial animation by default
        lod->EnableFacialAnimation(false);
    }

    // In case we are stripping all lods and it's not Lod0, strip it
    if (ctx.body.ScriptLODs.size() > 1 && ctx.options.stripLods)
        lod->StripFromModel(true);

    return LodBeginResult::Ok;
}

void ApplyLodEnd(StudioMdlState &ctx, const bool isShadow) {
    // If the LOD is stripped, then forget we saw it
    if (ctx.body.ScriptLODs.back().IsStrippedFromModel()) {
        ctx.body.ScriptLODs.pop_back();
    }

    // Mark .mdl as having shadow lod (we also check at the start that we have only one of these and that it's the
    // last entered lod)
    if (isShadow)
        ctx.header.gflags |= STUDIOHDR_FLAGS_HASSHADOWLOD;
}

void ApplyLodReplaceModel(StudioMdlState &ctx, LodScriptData_t &lod, const std::string_view from,
                          const std::string_view to, const std::function<s_source_t *()> &load) {
    // If the LOD system tells us to replace "blank", let's forget we ever read this
    if (vstd::iequals(from, "blank"))
        return;

    CLodScriptReplacement_t &replacement = lod.modelReplacements.emplace_back();
    replacement.SetSrcName(std::string(from));
    replacement.SetDstName(std::string(to));

    // Load the source right here baby! That way its bones will get converted
    if (!lod.IsStrippedFromModel()) {
        replacement.m_pSource = load();
    }
    else if (!ctx.options.quiet) {
        MdlPrint("Stripped lod \"{}\" @ {:.1f}\n", to, lod.switchValue);
    }
}

void ApplyLodRemoveModel(LodScriptData_t &lod, const std::string_view from) {
    // If the LOD system tells us to replace "blank", let's forget we ever read this
    if (vstd::iequals(from, "blank"))
        return;

    CLodScriptReplacement_t &replacement = lod.modelReplacements.emplace_back();
    replacement.SetSrcName(std::string(from));
    replacement.SetDstName("");
}

void ApplyLodReplaceBone(LodScriptData_t &lod, const std::string_view from, const std::string_view to) {
    CLodScriptReplacement_t &replacement = lod.boneReplacements.emplace_back();
    replacement.SetSrcName(std::string(from));
    replacement.SetDstName(std::string(to));
}

void ApplyLodBoneTreeCollapse(LodScriptData_t &lod, const std::string_view bone) {
    lod.boneTreeCollapses.emplace_back().SetSrcName(std::string(bone));
}

void ApplyLodReplaceMaterial(StudioMdlState &ctx, LodScriptData_t &lod, const std::string_view from,
                             const std::string_view to) {
    CLodScriptReplacement_t &replacement = lod.materialReplacements.emplace_back();
    replacement.SetSrcName(std::string(from));
    replacement.SetDstName(std::string(to));

    if (!lod.IsStrippedFromModel()) {
        // make sure it goes into the master list
        UseTextureAsMaterial(ctx, LookupTexture(ctx, to));
    }
}

void ApplyLodRemoveMesh(LodScriptData_t &lod, const std::string_view material) {
    std::string name(material);
    Q_FixSlashes(name.data());
    lod.meshRemovals.emplace_back().SetSrcName(name);
}

void ApplyLodFacialAnimation(LodScriptData_t &lod, const bool enabled) {
    lod.EnableFacialAnimation(enabled);
}

void ApplyLodUseShadowLodMaterials(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_USE_SHADOWLOD_MATERIALS;
}

std::string LodSourceName(const std::string_view path) {
    std::string name(path);
    if (const size_t dot = name.rfind('.'); dot != std::string::npos)
        name.resize(dot);
    return name;
}
