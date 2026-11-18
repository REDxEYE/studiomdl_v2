// Builder front end, Core part: the model-wide settings (smdl_globals.h) and the final step of the compile.
// Everything is handed to the apply functions the QC parsers use (studiomdl/apply_globals.hpp, ProcessStaticProp,
// SetDefaultSurfaceProp ...), so the builder and a QC with the same commands set the same state.
#include "apply.hpp"

#include <algorithm>

#include "studiomdl/apply_globals.hpp"
#include "studiomdl/compile_error.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_commands.h"

namespace {
    FlexOptimizeMode_t FlexOptimizeMode(smdl_flex_optimize_mode mode) {
        switch (mode) {
            case SMDL_FLEX_OPTIMIZE_ON:
                return FLEXOPT_ON;
            case SMDL_FLEX_OPTIMIZE_OFF:
                return FLEXOPT_OFF;
            case SMDL_FLEX_OPTIMIZE_AUTO:
                break;
        }
        return FLEXOPT_AUTO;
    }

    uint32_t ContentsKeywords(uint32_t flags) {
        uint32_t keywords = 0;
        if (flags & SMDL_CONTENTS_SOLID)
            keywords |= CONTENTS_KEYWORD_SOLID;
        if (flags & SMDL_CONTENTS_GRATE)
            keywords |= CONTENTS_KEYWORD_GRATE;
        if (flags & SMDL_CONTENTS_LADDER)
            keywords |= CONTENTS_KEYWORD_LADDER;
        if (flags & SMDL_CONTENTS_MONSTER)
            keywords |= CONTENTS_KEYWORD_MONSTER;
        if (flags & SMDL_CONTENTS_NOTSOLID)
            keywords |= CONTENTS_KEYWORD_NOTSOLID;
        return keywords;
    }

    // Compile options: they change how the sources are loaded and processed, so they come first
    void ApplyCompileOptions(StudioMdlState &ctx, const GlobalsDesc &g) {
        if (g.noWarnings)
            ctx.options.noWarnings = true;
        if (g.maxWarnings)
            ctx.options.maxWarnings = *g.maxWarnings;
        if (g.fitWeights) {
            ctx.options.fitWeights = true;
            if (g.fitWeightsFitPoses)
                ctx.options.fitWeightsFitPoses = g.fitWeightsFitPoses;
            if (g.fitWeightsTestPoses)
                ctx.options.fitWeightsTestPoses = g.fitWeightsTestPoses;
        }
        if (g.stripLods)
            ctx.options.stripLods = true;
        if (g.buildPreview)
            ctx.options.buildPreview = true;
        if (g.normalBlendAngle)
            ApplyNormalBlendAngle(ctx, *g.normalBlendAngle);
        for (const auto &[replacement, replaces]: g.textureReplacements)
            ApplyTextureReplacement(ctx, replacement, replaces);
        if (g.zbrush)
            ApplyZBrush(ctx);
        if (g.preserveTriangleOrder)
            ApplyPreserveTriangleOrder(ctx);
        if (g.clipToTextures)
            ApplyClipToTextures(ctx);
        if (g.maxVertices) {
            ApplyMaxVerts(ctx, g.maxVertices->first,
                          g.maxVertices->second > 0 ? std::optional<int>(g.maxVertices->second) : std::nullopt);
        }
        if (g.flexOptimize)
            ApplyFlexOptimizeMode(ctx, FlexOptimizeMode(*g.flexOptimize));
        if (g.flexOptimizeMinStatic)
            ApplyFlexOptimizeMinStatic(ctx, *g.flexOptimizeMinStatic);
        if (g.gamma)
            ApplyGamma(ctx, *g.gamma);
        if (g.minLod)
            ApplyMinLod(ctx, *g.minLod);
        if (g.allowRootLods)
            ApplyAllowRootLods(ctx, *g.allowRootLods);
    }

    void ApplyTransform(StudioMdlState &ctx, const GlobalsDesc &g) {
        if (g.scale)
            ApplyScale(ctx, *g.scale);
        if (g.upAxis) {
            static constexpr UpAxis axes[] = {UpAxis::X, UpAxis::NegX, UpAxis::Y, UpAxis::NegY, UpAxis::Z, UpAxis::NegZ};
            ApplyUpAxis(ctx, axes[*g.upAxis]);
        }
        if (g.origin)
            ApplyOrigin(ctx, g.origin->x, g.origin->y, g.origin->z);
        if (g.originAngle)
            ApplyOriginAngle(ctx, *g.originAngle);
        if (g.autocenter)
            ApplyAutocenter(ctx);
        if (g.centerBonesOnVerts)
            ApplyCenterBonesOnVerts(ctx);
    }

    void ApplyFlags(StudioMdlState &ctx, const GlobalsDesc &g) {
        if (g.staticProp)
            ProcessStaticProp(ctx);
        if (g.mostlyOpaque)
            ApplyMostlyOpaque(ctx);
        if (g.opaque)
            ApplyOpaque(ctx);
        if (g.ambientBoost)
            ApplyAmbientBoost(ctx);
        if (g.noForcedFade)
            ApplyNoForcedFade(ctx);
        if (g.doNotCastShadows)
            ApplyDoNotCastShadows(ctx);
        if (g.castTextureShadows)
            ApplyCastTextureShadows(ctx);
        if (g.subd)
            ApplySubdivisionSurface(ctx);
        if (g.obsolete)
            ApplyObsolete(ctx);
        if (g.forcePhonemeCrossfade)
            ApplyForcePhonemeCrossfade(ctx);
        if (g.constantDirectionalLight)
            ApplyConstantDirectionalLight(ctx, *g.constantDirectionalLight);
    }

    void ApplyHeaderValues(StudioMdlState &ctx, const ApplyState &state, const GlobalsDesc &g) {
        const SkeletonDesc &skeleton = state.model.skeleton;
        if (g.eyePosition)
            ApplyEyePosition(ctx, g.eyePosition->x, g.eyePosition->y, g.eyePosition->z);
        if (g.illumPosition) {
            const auto &illum = *g.illumPosition;
            ApplyIllumPosition(ctx, illum.position.x, illum.position.y, illum.position.z,
                               illum.attachmentBone == SMDL_NO_BONE ? std::string_view()
                                                                    : std::string_view(skeleton.bones[illum.attachmentBone].name));
        }
        if (g.maxEyeDeflection)
            ApplyMaxEyeDeflection(ctx, *g.maxEyeDeflection);
        if (g.bbox)
            ApplyBBox(ctx, g.bbox->first, g.bbox->second);
        if (g.cbox)
            ApplyCBox(ctx, g.cbox->first, g.cbox->second);
        if (g.bboxOnlyVerts)
            ApplyBBoxOnlyVerts(ctx);
        if (g.skipBoneInBBox)
            ApplySkipBoneInBBox(ctx);
        if (g.collisionPrecision)
            ApplyCollisionPrecision(ctx, *g.collisionPrecision);
        if (!g.keyValues.empty())
            ApplyKeyValues(ctx, g.keyValues);
        // a builder has no QC: without an explicit text the .mdl gets no qc_path entry
        ctx.paths.qcPathEntry = g.qcPath.value_or(std::string());
        for (const std::string &path: g.includeModels)
            ApplyIncludeModel(ctx, path);
    }

    void ApplySurfaceAndContents(StudioMdlState &ctx, const ApplyState &state, const GlobalsDesc &g) {
        const SkeletonDesc &skeleton = state.model.skeleton;
        if (g.surfaceProp)
            SetDefaultSurfaceProp(ctx, g.surfaceProp->c_str());
        for (const auto &[bone, name]: g.boneSurfaceProp)
            AddSurfaceProp(ctx, skeleton.bones[bone].name.c_str(), name.c_str());
        if (g.contents)
            ApplyContents(ctx, ContentsKeywords(*g.contents));
        for (const auto &[bone, flags]: g.boneContents)
            ApplyJointContents(ctx, skeleton.bones[bone].name, ContentsKeywords(flags));
    }

    void ApplyAnimationDefaults(StudioMdlState &ctx, const GlobalsDesc &g) {
        if (g.defaultFadeInTime)
            ApplyDefaultFadeInTime(ctx, *g.defaultFadeInTime);
        if (g.defaultFadeOutTime)
            ApplyDefaultFadeOutTime(ctx, *g.defaultFadeOutTime);
        if (g.motionRollback)
            ApplyMotionRollback(ctx, *g.motionRollback);
        if (g.clampWorldspace)
            ApplyClampWorldspace(ctx, g.clampWorldspace->first, g.clampWorldspace->second);
        for (const std::string &name: g.allowedActivityNames)
            ApplyAllowActivityName(ctx, name);
        if (g.errorOnSequenceRemapFailure)
            ApplyErrorOnSeqRemapFail(ctx);
        if (g.modelHasNoSequences)
            ApplyModelIntentionallyHasZeroSequences(ctx);
        if (g.calcTransitions)
            ApplyCalcTransitions(ctx);
        if (g.sectionFrames)
            ApplySectionFrames(ctx, g.sectionFrames->first, g.sectionFrames->second);
        if (g.animBlockSize > 0) {
            AnimBlockSettings settings;
            settings.size = g.animBlockSize;
            settings.noStall = g.animBlockNoStall;
            if (g.animBlockResolution == SMDL_ANIM_BLOCK_HIGH_RES)
                settings.resolution = AnimBlockSettings::Resolution::High;
            else if (g.animBlockResolution == SMDL_ANIM_BLOCK_LOW_RES)
                settings.resolution = AnimBlockSettings::Resolution::Low;
            settings.numFrames = g.animBlockNumFrames;
            settings.cacheHighRes = g.animBlockCacheHighRes;
            settings.posDelta = g.animBlockPosDelta;
            ApplyAnimBlockSize(ctx, settings);
        }
    }

    // The default sequence of a static prop that has none: what SetupStaticProp (mppsupport.cpp) creates for a static
    // prop of an .mpp file, a sequence "BindPose" playing the bind pose of the first model
    void AddStaticPropDefaultSequence(StudioMdlState &ctx) {
        if (!ctx.header.staticprop || !ctx.anim.sequence.empty() || ctx.body.model.empty())
            return;
        s_source_t *source = ctx.body.model[0]->source;
        if (!source)
            return;
        s_sequence_t *pSeq = ProcessCmdSequence(ctx, "BindPose");
        s_animation_t *pAnim = ProcessImpliedAnimation(ctx, pSeq, source->filename.c_str());
        pSeq->panim[0][0] = pAnim;
        ProcessSequence(pSeq, 1, &pAnim, false);
    }
}

void ApplyGlobals(StudioMdlState &ctx, ApplyState &state) {
    const GlobalsDesc &g = state.model.globals;
    ScopedApplyObject object(state, "model settings");

    ApplyCompileOptions(ctx, g);

    if (g.internalName)
        ApplyInternalName(ctx, *g.internalName);
    for (const std::string &path: g.materialPaths)
        ApplyCdMaterial(ctx, path);

    ApplyTransform(ctx, g);
    ApplyFlags(ctx, g);
    ApplyHeaderValues(ctx, state, g);
    ApplySurfaceAndContents(ctx, state, g);
    ApplyAnimationDefaults(ctx, g);
}

void ApplyFinish(StudioMdlState &ctx, ApplyState &state) {
    const GlobalsDesc &g = state.model.globals;
    ScopedApplyObject object(state, "model settings");

    // $skiptransition names nodes, which exist once the sequences created theirs
    for (const std::vector<std::string> &nodes: g.skipTransitions)
        ApplySkipTransition(ctx, nodes);

    AddStaticPropDefaultSequence(ctx);
}
