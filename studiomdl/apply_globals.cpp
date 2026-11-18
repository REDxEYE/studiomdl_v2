#include "studiomdl/apply_globals.hpp"

#include <array>
#include <numbers>

#include "common/scriplib.h"
#include "studiomdl_errors.h"
#include "tier1_v2/math_utils.hpp"
#include "tier1_v2/string_utils.hpp"

// ---------------------------------------------------------------------------------------------------------------
// Paths, transforms
// ---------------------------------------------------------------------------------------------------------------

void ApplyInternalName(StudioMdlState &ctx, const std::string_view name) {
    const std::string text(name);
    Q_strncpy(ctx.paths.internalName, text.c_str(), sizeof(ctx.paths.internalName));
}

void ApplyCdMaterial(StudioMdlState &ctx, const std::string_view path) {
    char szPath[512];
    const std::string text(path);
    Q_strncpy(szPath, text.c_str(), sizeof(szPath));

    int len = strlen(szPath);
    if (len > 0 && szPath[len - 1] != '/' && szPath[len - 1] != '\\') {
        Q_strncat(szPath, "/", sizeof(szPath), COPY_ALL_CHARACTERS);
    }

    Q_FixSlashes(szPath);
    ctx.materials.cdtextures.emplace_back(ctx.arena.CopyString(szPath));
}

void ApplyScale(StudioMdlState &ctx, const float scale) {
    ctx.script.defaultscale = scale;
    ctx.script.currentscale = ctx.script.defaultscale;
}

void ApplyOrigin(StudioMdlState &ctx, const float x, const float y, const float z) {
    ctx.script.defaultadjust.x = x;
    ctx.script.defaultadjust.y = y;
    ctx.script.defaultadjust.z = z;
}

void ApplyOriginAngle(StudioMdlState &ctx, const float degrees) {
    ctx.script.defaultrotation.z = DEG2RAD(degrees + 90);
}

bool ParseUpAxis(const std::string_view text, UpAxis &axis) {
    static constexpr std::pair<std::string_view, UpAxis> axes[] = {
        {"x", UpAxis::X}, {"-x", UpAxis::NegX}, {"y", UpAxis::Y},
        {"-y", UpAxis::NegY}, {"z", UpAxis::Z}, {"-z", UpAxis::NegZ},
    };
    for (const auto &[name, value]: axes) {
        if (vstd::iequals(text, name)) {
            axis = value;
            return true;
        }
    }
    return false;
}

// We want to create a rotation that rotates from the art space (specified by the up direction) to a z up space
// Note: x, -x, -y are untested
RadianEuler UpAxisAngles(const UpAxis axis) {
    RadianEuler angles(0.0f, 0.0f, M_PI / 2.0f);
    switch (axis) {
        case UpAxis::X:
            // rotate 90 degrees around y to move x into z
            angles.x = 0.0f;
            angles.y = M_PI / 2.0f;
            break;
        case UpAxis::NegX:
            angles.x = 0.0f;
            angles.y = -M_PI / 2.0f;
            break;
        case UpAxis::Y:
            // rotate 90 degrees around x to move y into z
            angles.x = M_PI / 2.0f;
            angles.y = 0.0f;
            break;
        case UpAxis::NegY:
            angles.x = -M_PI / 2.0f;
            angles.y = 0.0f;
            break;
        case UpAxis::Z:
        case UpAxis::NegZ:
            // there's still a built in 90 degree Z rotation :(
            angles.x = 0.0f;
            angles.y = 0.0f;
            break;
    }
    return angles;
}

void ApplyUpAxis(StudioMdlState &ctx, const UpAxis axis) {
    ProcessUpAxis(ctx, UpAxisAngles(axis));
}

void ApplyAutocenter(StudioMdlState &ctx) {
    ctx.header.centerstaticprop = true;
}

void ApplyCenterBonesOnVerts(StudioMdlState &ctx) {
    // force centering on bones
    ctx.options.centerBonesOnVerts = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Contents
// ---------------------------------------------------------------------------------------------------------------

uint32_t ParseContentsKeyword(const std::string_view word) {
    if (vstd::iequals(word, "grate"))
        return CONTENTS_KEYWORD_GRATE;
    if (vstd::iequals(word, "ladder"))
        return CONTENTS_KEYWORD_LADDER;
    if (vstd::iequals(word, "solid"))
        return CONTENTS_KEYWORD_SOLID;
    if (vstd::iequals(word, "monster"))
        return CONTENTS_KEYWORD_MONSTER;
    if (vstd::iequals(word, "notsolid"))
        return CONTENTS_KEYWORD_NOTSOLID;
    return 0;
}

void ContentsKeywordsToFlags(const uint32_t keywords, ContentType &addFlags, ContentType &removeFlags) {
    addFlags = ContentType::EMPTY;
    removeFlags = ContentType::EMPTY;
    if (keywords & CONTENTS_KEYWORD_GRATE) {
        addFlags |= ContentType::GRATE;
        removeFlags |= ContentType::SOLID;
    }
    if (keywords & CONTENTS_KEYWORD_LADDER) {
        addFlags |= ContentType::LADDER;
    }
    if (keywords & CONTENTS_KEYWORD_SOLID) {
        addFlags |= ContentType::SOLID;
    }
    if (keywords & CONTENTS_KEYWORD_MONSTER) {
        addFlags |= ContentType::MONSTER;
    }
    if (keywords & CONTENTS_KEYWORD_NOTSOLID) {
        removeFlags |= ContentType::SOLID;
    }
}

void ApplyContents(StudioMdlState &ctx, const uint32_t keywords) {
    ContentType nAddFlags, nRemoveFlags;
    ContentsKeywordsToFlags(keywords, nAddFlags, nRemoveFlags);
    ctx.core.defaultContents |= nAddFlags;
    ctx.core.defaultContents &= ~nRemoveFlags;
}

void ApplyJointContents(StudioMdlState &ctx, const std::string_view boneName, const uint32_t keywords) {
    // Search for the name in our list
    int i;
    for (i = std::ssize(ctx.core.jointContents); --i >= 0;) {
        if (vstd::iequals(ctx.core.jointContents[i].m_pJointName.view(), boneName)) {
            break;
        }
    }

    // Add new entry if we haven't seen this name before
    if (i < 0) {
        i = std::ssize(ctx.core.jointContents);
        ctx.core.jointContents.emplace_back();
        ctx.core.jointContents[i].m_pJointName = boneName;
    }

    ContentType nAddFlags, nRemoveFlags;
    ContentsKeywordsToFlags(keywords, nAddFlags, nRemoveFlags);
    ctx.core.jointContents[i].m_nContents = ContentType::SOLID;
    ctx.core.jointContents[i].m_nContents |= nAddFlags;
    ctx.core.jointContents[i].m_nContents &= ~nRemoveFlags;
}

// ---------------------------------------------------------------------------------------------------------------
// Header values and flags
// ---------------------------------------------------------------------------------------------------------------

void ApplyIncludeModel(StudioMdlState &ctx, const std::string_view path) {
    ctx.body.includemodels.emplace_back(StudioName::from_format("models/{}", path));
}

void ApplyKeyValues(StudioMdlState &ctx, const std::string_view formattedText) {
    ctx.header.KeyValueText.append(formattedText);
}

namespace {
    // Thrown by the tokenizer's error handler while parsing a $keyvalues text outside a script
    struct KeyValuesSyntaxError {
        std::string message;
    };
}

bool FormatKeyValuesText(const std::string_view text, std::string &formatted, std::string &error) {
    // the QC form: the block parser reads "{ ... }"; the text after the closing brace must be empty
    std::string buffer = "{\n";
    buffer += text;
    buffer += "\n}\n";

    ScriptTokenizer qc;
    qc.SetErrorHandler([](const std::string &message, const std::string &, int) {
        throw KeyValuesSyntaxError{message};
    });
    try {
        qc.LoadMemory(buffer);
        std::string result;
        Option_KeyValues(qc, result);
        if (qc.GetToken(true)) {
            error = "keyvalues: unexpected \"" + std::string(qc.token.view()) + "\" after the closing '}'";
            return false;
        }
        formatted = std::move(result);
        return true;
    }
    catch (const KeyValuesSyntaxError &e) {
        error = e.message;
        while (error.ends_with('\n'))
            error.pop_back();
        return false;
    }
}

void ApplyEyePosition(StudioMdlState &ctx, const float a, const float b, const float c) {
    // rotate points into frame of reference so ctx.body.model points down the positive x axis
    //	FIXME: these coords are bogus
    ctx.header.eyeposition[1] = a;
    ctx.header.eyeposition[0] = -b;
    ctx.header.eyeposition[2] = c;
}

void ApplyIllumPosition(StudioMdlState &ctx, const float x, const float y, const float z,
                        const std::string_view attachmentBone) {
    ctx.header.illumposition[0] = x;
    ctx.header.illumposition[1] = y;
    ctx.header.illumposition[2] = z;

    if (!attachmentBone.empty()) {
        auto &attachment = ctx.bones.attachments.emplace_back(s_attachment_t{
            .name = "__illumPosition",
            .bonename = attachmentBone,
            .type = AttachmentType::Rigid,
        });

        AngleMatrix(QAngle(0, 0, 0), ctx.header.illumposition, attachment.local);

        ctx.header.illumpositionattachment = vstd::as_signed_checked<int>(ctx.bones.attachments.size());
        // No -1 because 0 is used as no illumposiotion attachment
    }
    else {
        ctx.header.illumpositionattachment = 0;

        // rotate points into frame of reference so
        // ctx.body.model points down the positive x axis
        // FIXME: these coords are bogus
        const float flTemp = ctx.header.illumposition[0];
        ctx.header.illumposition[0] = -ctx.header.illumposition[1];
        ctx.header.illumposition[1] = flTemp;
    }

    ctx.header.illumpositionset = true;
}

void ApplyMaxEyeDeflection(StudioMdlState &ctx, const float degrees) {
    ctx.header.flMaxEyeDeflection = cosf(degrees * M_PI / 180.0f);
}

void ApplyGamma(StudioMdlState &ctx, const float gamma) {
    ctx.options.gamma = gamma;
}

void ApplyBBox(StudioMdlState &ctx, const Vector &mins, const Vector &maxs) {
    ctx.header.bbox[0] = mins;
    ctx.header.bbox[1] = maxs;
    ctx.header.wrotebbox = true;
}

void ApplyCBox(StudioMdlState &ctx, const Vector &mins, const Vector &maxs) {
    ctx.header.cbox[0] = mins;
    ctx.header.cbox[1] = maxs;
    ctx.header.wrotecbox = true;
}

void ApplyBBoxOnlyVerts(StudioMdlState &ctx) {
    ctx.header.bboxonlyverts = true;
}

void ApplySkipBoneInBBox(StudioMdlState &ctx) {
    ctx.options.useBoneInBBox = false;
}

void ApplyOpaque(StudioMdlState &ctx) {
    // Force Opaque has precedence
    ctx.header.gflags |= STUDIOHDR_FLAGS_FORCE_OPAQUE;
    ctx.header.gflags &= ~STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
}

void ApplyMostlyOpaque(StudioMdlState &ctx) {
    // Force Opaque has precedence
    if ((ctx.header.gflags & STUDIOHDR_FLAGS_FORCE_OPAQUE) == 0) {
        ctx.header.gflags |= STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
    }
}

void ApplyAmbientBoost(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_AMBIENT_BOOST;
}

void ApplyNoForcedFade(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_NO_FORCED_FADE;
}

void ApplyDoNotCastShadows(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS;
}

void ApplyCastTextureShadows(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS;
}

void ApplySubdivisionSurface(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_SUBDIVISION_SURFACE;
}

void ApplyObsolete(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_OBSOLETE;
}

void ApplyForcePhonemeCrossfade(StudioMdlState &ctx) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_FORCE_PHONEME_CROSSFADE;
}

void ApplyConstantDirectionalLight(StudioMdlState &ctx, const float dot) {
    ctx.header.gflags |= STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT;
    ctx.header.constdirectionalightdot = (uint8_t) (dot * 255.0f);
}

void ApplyCollisionPrecision(StudioMdlState &ctx, const float precision) {
    ctx.header.collisionPrecision = precision;
}

// ---------------------------------------------------------------------------------------------------------------
// Compile options
// ---------------------------------------------------------------------------------------------------------------

void ApplyZBrush(StudioMdlState &ctx) {
    ctx.options.ZBrush = true;
}

void ApplyPreserveTriangleOrder(StudioMdlState &ctx) {
    ctx.options.preserveTriangleOrder = true;
}

void ApplyClipToTextures(StudioMdlState &ctx) {
    ctx.options.clip_texcoords = 1;
}

void ApplyLCaseAllSequences(StudioMdlState &ctx) {
    ctx.options.lCaseAllSequences = true;
}

void ApplyErrorOnSeqRemapFail(StudioMdlState &ctx) {
    ctx.options.errorOnSeqRemapFail = true;
}

void ApplyModelIntentionallyHasZeroSequences(StudioMdlState &ctx) {
    ctx.options.modelIntentionallyHasZeroSequences = true;
}

void ApplyCalcTransitions(StudioMdlState &ctx) {
    ctx.options.multistageGraph = true;
}

void ApplyFlexOptimizeMode(StudioMdlState &ctx, const FlexOptimizeMode_t mode) {
    ctx.options.flexOptimizeMode = mode;
}

void ApplyFlexOptimizeMinStatic(StudioMdlState &ctx, const int minStatic) {
    ctx.options.flexOptimizeMinStatic = minStatic;
}

void ApplyMaxVerts(StudioMdlState &ctx, const int limit, const std::optional<int> clampTarget) {
    // first limit
    ctx.options.maxVertexLimit = clamp(limit, 1024, MAXSTUDIOVERTS);
    ctx.options.maxVertexClamp = MIN(ctx.options.maxVertexLimit, MAXSTUDIOVERTS / 2);

    if (clampTarget) {
        // actual target limit
        ctx.options.maxVertexClamp = clamp(*clampTarget, 1024, MAXSTUDIOVERTS);
    }
}

void ApplyMinLod(StudioMdlState &ctx, const int minLod) {
    ctx.options.minLod = minLod;

    // "minlod" rules over "allowrootlods"
    if (ctx.options.numAllowedRootLODs > 0 && ctx.options.numAllowedRootLODs < ctx.options.minLod) {
        MdlWarning("$minlod {} overrides $allowrootlods {}, proceeding with $allowrootlods {}.\n",
                   ctx.options.minLod,
                   ctx.options.numAllowedRootLODs, ctx.options.minLod);
        ctx.options.numAllowedRootLODs = ctx.options.minLod;
    }
}

void ApplyAllowRootLods(StudioMdlState &ctx, const int numAllowedRootLods) {
    ctx.options.numAllowedRootLODs = numAllowedRootLods;

    // Root LOD restriction has to obey "minlod" request
    if (ctx.options.numAllowedRootLODs > 0 && ctx.options.numAllowedRootLODs < ctx.options.minLod) {
        MdlWarning("$allowrootlods {} is conflicting with $minlod {}, proceeding with $allowrootlods {}.\n",
                   ctx.options.numAllowedRootLODs, ctx.options.minLod, ctx.options.minLod);
        ctx.options.numAllowedRootLODs = ctx.options.minLod;
    }
}

void ApplyMotionRollback(StudioMdlState &ctx, const float rollback) {
    ctx.options.defaultMotionRollback = rollback;
}

void ApplyDefaultFadeInTime(StudioMdlState &ctx, const float seconds) {
    ctx.options.defaultFadeInTime = seconds;
}

void ApplyDefaultFadeOutTime(StudioMdlState &ctx, const float seconds) {
    ctx.options.defaultFadeOutTime = seconds;
}

void ApplyClampWorldspace(StudioMdlState &ctx, const Vector &mins, const Vector &maxs) {
    ctx.options.vecMinWorldspace = mins;
    ctx.options.vecMaxWorldspace = maxs;
}

void ApplySectionFrames(StudioMdlState &ctx, const int frames, const int minFrameLimit) {
    ctx.options.sectionFrames = frames;
    ctx.options.minSectionFrameLimit = minFrameLimit;
}

void ApplyAllowActivityName(StudioMdlState &ctx, const std::string_view name) {
    ctx.anim.AllowedActivityNames.emplace_back(name);
}

void ApplyAnimBlockSize(StudioMdlState &ctx, const AnimBlockSettings &settings) {
    ctx.anim.animblocksize = settings.size;
    if (ctx.anim.animblocksize < 1024) {
        ctx.anim.animblocksize *= 1024;
    }
    if (settings.noStall) {
        ctx.options.noAnimblockStall = true;
    }
    if (settings.resolution == AnimBlockSettings::Resolution::High) {
        ctx.options.animblockHighRes = true;
        ctx.options.animblockLowRes = false;
    }
    else if (settings.resolution == AnimBlockSettings::Resolution::Low) {
        ctx.options.animblockLowRes = true;
        ctx.options.animblockHighRes = false;
    }
    if (settings.numFrames) {
        ctx.options.maxZeroFrames = clamp(*settings.numFrames, 1, 4);
    }
    if (settings.cacheHighRes) {
        ctx.options.zeroFramesHighres = true;
    }
    if (settings.posDelta) {
        ctx.options.minZeroFramePosDelta = *settings.posDelta;
    }
}

void ApplySkipTransition(StudioMdlState &ctx, const std::vector<std::string> &nodeNames) {
    std::vector<int> list;
    list.reserve(nodeNames.size());
    for (const std::string &name: nodeNames) {
        list.push_back(LookupXNode(ctx, name.c_str()));
    }

    for (size_t i = 0; i < list.size(); i++) {
        for (size_t j = 0; j < list.size(); j++) {
            if (list[i] != list[j]) {
                ctx.anim.xnodeskips.emplace_back(std::array{list[i], list[j]});
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------------------------------------------

void ApplyTextureGroup(StudioMdlState &ctx, const std::vector<std::vector<std::string>> &rows,
                       const bool relativePaths) {
    // [skin family][index] = material
    auto &families = ctx.materials.texturegroups.emplace_back();

    for (size_t group = 0; group < rows.size(); group++) {
        for (size_t index = 0; index < rows[group].size(); index++) {
            const int i = static_cast<int>(UseTextureAsMaterial(ctx, LookupTexture(ctx, rows[group][index], relativePaths)));
            if (families.size() <= group)
                families.resize(group + 1);
            families[group].push_back(i);
            if (group != 0)
                ctx.materials.textures[i].parent = index < families[0].size() ? families[0][index] : 0;
        }
    }
}

void ApplyOverrideMaterial(StudioMdlState &ctx, const std::string_view name) {
    Msg("$overridematerial is replacing ALL material references with {}.\n", name);

    for (size_t i = 0; i < ctx.materials.textures.size(); i++) {
        ctx.materials.textures[i].name = name;
    }
}

void ApplyTextureReplacement(StudioMdlState &ctx, const std::string_view replacement, const std::string_view replaces) {
    auto &default_texture = ctx.materials.defaulttextures.emplace_back();
    auto &source_texture = ctx.materials.sourcetextures.emplace_back();
    default_texture = replacement;
    source_texture = replaces;
}

void ApplyNormalBlendAngle(StudioMdlState &ctx, const float degrees) {
    ctx.options.normal_blend = std::cos(vstd::deg2rad(degrees));
}
