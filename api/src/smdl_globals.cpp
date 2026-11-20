// smdl_globals.h: model-wide settings. The setters validate and store into GlobalsDesc; ApplyGlobals (apply_globals.cpp)
// hands them to studiomdl's shared apply functions at compile time.
#include "studiomdl/c/smdl_globals.h"
#include "studiomdl/studiomdl.hpp"

#include <cmath>

#include "core_util.hpp"
#include "studiomdl/apply_globals.hpp"
#include "studiomdl/studiomdl_constants.hpp"
#include "studiomdl/weight_fit.hpp"

using namespace smdl_api;

namespace {
    // Limits of the storage the apply functions write into (studiomdl_global_state.hpp / studiomdl.h)
    constexpr size_t kMaxMaterialPaths = 16;       // StudioMdlMaterials::cdtextures
    constexpr size_t kMaxIncludeModels = 128;      // StudioMdlBody::includemodels
    constexpr size_t kMaxTextureReplacements = 16; // StudioMdlMaterials::defaulttextures
    constexpr size_t kMaxSkinMaterials = 128;      // MAXSTUDIOSKINS
    constexpr size_t kMaxSkipTransitionNodes = 10; // Cmd_Skiptransition's list
    constexpr size_t kMaxSurfacePropLength = 127;  // vstd::FixedString<128>
    constexpr size_t kMaxPathLength = 259;         // StudioPath (MAX_PATH)
    constexpr size_t kMaxMaterialPathLength = 500; // the buffer of ApplyCdMaterial leaves room for the '/'

    // Flag setters: the flag is a bool member of GlobalsDesc
    smdl_status SetFlag(smdl_model *model, bool GlobalsDesc::*flag, bool enabled) {
        model->globals.*flag = enabled;
        return Ok(model);
    }

    // Empty = fine. A name that is empty, null or longer than `maxLength` bytes is an invalid argument.
    std::string BadText(const char *what, const char *text, size_t maxLength) {
        if (!ValidName(text))
            return std::format("{} is null or empty", what);
        if (std::string_view(text).size() > maxLength)
            return std::format("{} '{}' is too long (max {} bytes)", what, text, maxLength);
        return {};
    }

    smdl_status OptionalFloat(smdl_model *model, const char *what, float value, std::optional<float> GlobalsDesc::*member,
                              bool nonNegative) {
        if (!std::isfinite(value))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{} must be a finite number", what);
        if (nonNegative && value < 0.0f)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{} must be >= 0, got {}", what, value);
        model->globals.*member = value;
        return Ok(model);
    }

    smdl_status OptionalInt(smdl_model *model, const char *what, int value, std::optional<int> GlobalsDesc::*member,
                            int minimum) {
        if (value < minimum)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{} must be >= {}, got {}", what, minimum, value);
        model->globals.*member = value;
        return Ok(model);
    }

    smdl_status SetBox(smdl_model *model, const char *what, const float mins[3], const float maxs[3],
                       std::optional<std::pair<Vector, Vector>> GlobalsDesc::*member) {
        if (!mins || !maxs || !AllFinite(mins, 3) || !AllFinite(maxs, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: mins and maxs must be 3 finite numbers each", what);
        model->globals.*member = std::make_pair(ToVector(mins), ToVector(maxs));
        return Ok(model);
    }

    // A bone id of the model's skeleton, or the failure to return
    std::optional<smdl_status> CheckBone(const smdl_model &model, const char *what, smdl_bone_id bone) {
        if (model.skeleton.valid(bone))
            return std::nullopt;
        return Fail(&model, SMDL_ERROR_OUT_OF_RANGE, "{}: bone {} is out of range ({} bones)", what, bone,
                    model.skeleton.bones.size());
    }

    constexpr uint32_t kAllContentsFlags = SMDL_CONTENTS_SOLID | SMDL_CONTENTS_GRATE | SMDL_CONTENTS_LADDER |
                                           SMDL_CONTENTS_MONSTER | SMDL_CONTENTS_NOTSOLID;
}

namespace smdl {
namespace detail {

void ModelGlobalsApi::setInternalName(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (std::string bad = BadText("internal name", name, kMaxPathLength); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        model->globals.internalName = name;
        return Ok(model);
    });
}

void ModelGlobalsApi::addMaterialPath(std::string_view pathView) {
    const std::string pathStorage(pathView);
    const char *path = pathStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (std::string bad = BadText("material path", path, kMaxMaterialPathLength); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (model->globals.materialPaths.size() >= kMaxMaterialPaths)
            return Fail(model, SMDL_ERROR_LIMIT, "material path '{}': a model has at most {} material paths", path,
                        kMaxMaterialPaths);
        model->globals.materialPaths.emplace_back(path);
        return Ok(model);
    });
}

void ModelGlobalsApi::addIncludeModel(std::string_view pathView) {
    const std::string pathStorage(pathView);
    const char *path = pathStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        // stored as "models/<path>" in a StudioName
        if (std::string bad = BadText("include model path", path, MAXSTUDIONAME - 1 - 7); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (model->globals.includeModels.size() >= kMaxIncludeModels)
            return Fail(model, SMDL_ERROR_LIMIT, "include model '{}': a model has at most {} include models", path,
                        kMaxIncludeModels);
        model->globals.includeModels.emplace_back(path);
        return Ok(model);
    });
}

uint32_t ModelGlobalsApi::addSkinFamily(std::span<const std::string> materialSpan) {
    std::vector<const char *> names;
    names.reserve(materialSpan.size());
    for (const std::string &name: materialSpan)
        names.push_back(name.c_str());
    const char *const *materials = names.data();
    size_t count = names.size();
    smdl_model *model = this->model();
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_index = &result;
    RunBody(model, [&] {
        auto &families = model->globals.skinFamilies;
        if (count == 0 || !materials)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "skin family {}: needs at least one material", families.size());
        if (!families.empty() && count != families[0].size())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "skin family {}: has {} materials, the default skin family has {}", families.size(), count,
                        families[0].size());
        size_t total = count;
        for (const auto &family: families)
            total += family.size();
        if (total > kMaxSkinMaterials)
            return Fail(model, SMDL_ERROR_LIMIT, "skin family {}: skins hold at most {} materials in all families together",
                        families.size(), kMaxSkinMaterials);
        std::vector<std::string> family;
        family.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            if (std::string bad = BadText("skin material name", materials[i], MAXSTUDIONAME - 1); !bad.empty())
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "skin family {}, entry {}: {}", families.size(), i, bad);
            family.emplace_back(materials[i]);
        }
        families.push_back(std::move(family));
        if (out_index)
            *out_index = static_cast<uint32_t>(families.size() - 1);
        return Ok(model);
    });
    return result;
}

void ModelGlobalsApi::overrideMaterial(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (std::string bad = BadText("override material name", name, MAXSTUDIONAME - 1); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        model->globals.overrideMaterial = name;
        return Ok(model);
    });
}

void ModelGlobalsApi::addTextureReplacement(std::string_view replacementView, std::string_view replacesView) {
    const std::string replacementStorage(replacementView), replacesStorage(replacesView);
    const char *replacement = replacementStorage.c_str();
    const char *replaces = replacesStorage.empty() ? nullptr : replacesStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (std::string bad = BadText("replacement material name", replacement, kMaxPathLength); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (replaces && std::string_view(replaces).size() > kMaxPathLength)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "replaced material name is too long (max {} bytes)",
                        kMaxPathLength);
        if (model->globals.textureReplacements.size() >= kMaxTextureReplacements)
            return Fail(model, SMDL_ERROR_LIMIT, "a model has at most {} texture replacements", kMaxTextureReplacements);
        model->globals.textureReplacements.emplace_back(replacement, replaces ? replaces : "");
        return Ok(model);
    });
}

void ModelGlobalsApi::setScale(float scale) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!std::isfinite(scale) || scale == 0.0f)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "scale must be finite and not 0");
        model->globals.scale = scale;
        return Ok(model);
    });
}

void ModelGlobalsApi::setOrigin(const Vec3 &originVec) {
    const float *origin = originVec.data();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!origin || !AllFinite(origin, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "origin must be 3 finite numbers");
        model->globals.origin = ToVector(origin);
        return Ok(model);
    });
}

void ModelGlobalsApi::setUpAxis(UpAxis axis) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (axis < SMDL_UP_AXIS_X || axis > SMDL_UP_AXIS_NEG_Z)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "up axis {} is not a smdl_up_axis value", static_cast<int>(axis));
        model->globals.upAxis = axis;
        return Ok(model);
    });
}

void ModelGlobalsApi::setSurfaceProp(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (std::string bad = BadText("surface property", name, kMaxSurfacePropLength); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        model->globals.surfaceProp = name;
        return Ok(model);
    });
}

void ModelGlobalsApi::setBoneSurfaceProp(BoneId bone, std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (auto failure = CheckBone(*model, "bone surface property", bone))
            return *failure;
        if (std::string bad = BadText("surface property", name, kMaxSurfacePropLength); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone {}: {}", bone, bad);
        model->globals.boneSurfaceProp[bone] = name;
        return Ok(model);
    });
}

void ModelGlobalsApi::setContents(uint32_t flags) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (flags & ~kAllContentsFlags)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "contents: unknown flag bits 0x{:x}", flags & ~kAllContentsFlags);
        model->globals.contents = flags;
        return Ok(model);
    });
}

void ModelGlobalsApi::setBoneContents(BoneId bone, uint32_t flags) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (auto failure = CheckBone(*model, "bone contents", bone))
            return *failure;
        if (flags & ~kAllContentsFlags)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone {}: contents: unknown flag bits 0x{:x}", bone,
                        flags & ~kAllContentsFlags);
        model->globals.boneContents[bone] = flags;
        return Ok(model);
    });
}

void ModelGlobalsApi::setConstantDirectionalLight(bool enabled, float dot) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!enabled) {
            model->globals.constantDirectionalLight.reset();
            return Ok(model);
        }
        if (!std::isfinite(dot) || dot < 0.0f || dot > 1.0f)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "constant directional light dot must be 0..1, got {}", dot);
        model->globals.constantDirectionalLight = dot;
        return Ok(model);
    });
}

void ModelGlobalsApi::setEyePosition(const Vec3 &positionVec) {
    const float *position = positionVec.data();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!position || !AllFinite(position, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "eye position must be 3 finite numbers");
        model->globals.eyePosition = ToVector(position);
        return Ok(model);
    });
}

void ModelGlobalsApi::setIllumPosition(const Vec3 &positionVec, BoneId attachment_bone) {
    const float *position = positionVec.data();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!position || !AllFinite(position, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "illum position must be 3 finite numbers");
        if (attachment_bone != SMDL_NO_BONE)
            if (auto failure = CheckBone(*model, "illum position attachment", attachment_bone))
                return *failure;
        model->globals.illumPosition = GlobalsDesc::IllumPosition{ToVector(position), attachment_bone};
        return Ok(model);
    });
}

void ModelGlobalsApi::setBBox(const Vec3 &minsVec, const Vec3 &maxsVec) {
    const float *mins = minsVec.data(), *maxs = maxsVec.data();
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetBox(model, "bbox", mins, maxs, &GlobalsDesc::bbox); });
}

void ModelGlobalsApi::setCBox(const Vec3 &minsVec, const Vec3 &maxsVec) {
    const float *mins = minsVec.data(), *maxs = maxsVec.data();
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetBox(model, "cbox", mins, maxs, &GlobalsDesc::cbox); });
}

void ModelGlobalsApi::setClampWorldspace(const Vec3 &minsVec, const Vec3 &maxsVec) {
    const float *mins = minsVec.data(), *maxs = maxsVec.data();
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetBox(model, "clamp worldspace", mins, maxs, &GlobalsDesc::clampWorldspace); });
}

void ModelGlobalsApi::addKeyValues(std::string_view textView) {
    const std::string textStorage(textView);
    const char *text = textStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!text)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "keyvalues text is null");
        std::string formatted, error;
        if (!FormatKeyValuesText(text, formatted, error))
            return Fail(model, SMDL_ERROR_SYNTAX, "keyvalues: {}", error);
        model->globals.keyValues += formatted;
        return Ok(model);
    });
}

void ModelGlobalsApi::setQcPath(std::string_view pathView) {
    const std::string pathStorage(pathView);
    const char *path = pathStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (path && *path)
            model->globals.qcPath = path;
        else
            model->globals.qcPath.reset();
        return Ok(model);
    });
}

void ModelGlobalsApi::addAllowedActivityName(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "activity name is null or empty");
        model->globals.allowedActivityNames.emplace_back(name);
        return Ok(model);
    });
}

void ModelGlobalsApi::addSkipTransition(std::span<const std::string> nodeSpan) {
    std::vector<const char *> pointers;
    for (const std::string &name: nodeSpan)
        pointers.push_back(name.c_str());
    const char *const *nodes = pointers.data();
    size_t count = pointers.size();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (count < 2 || !nodes)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "skip transition needs at least 2 node names");
        if (count > kMaxSkipTransitionNodes)
            return Fail(model, SMDL_ERROR_LIMIT, "skip transition lists at most {} nodes, got {}", kMaxSkipTransitionNodes,
                        count);
        std::vector<std::string> list;
        for (size_t i = 0; i < count; ++i) {
            if (!ValidName(nodes[i]))
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "skip transition: node name {} is null or empty", i);
            list.emplace_back(nodes[i]);
        }
        model->globals.skipTransitions.push_back(std::move(list));
        return Ok(model);
    });
}

void ModelGlobalsApi::setSectionFrames(int frames, int min_frames) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (frames < 0 || min_frames < 0)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "section frames and minimum frames must be >= 0");
        model->globals.sectionFrames = std::make_pair(frames, min_frames);
        return Ok(model);
    });
}

void ModelGlobalsApi::setAnimBlockSize(int size) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (size < 0)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "anim block size must be >= 0, got {}", size);
        model->globals.animBlockSize = size;
        return Ok(model);
    });
}

void ModelGlobalsApi::setAnimBlockResolution(AnimBlockResolution resolution) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (resolution < SMDL_ANIM_BLOCK_DEFAULT || resolution > SMDL_ANIM_BLOCK_LOW_RES)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "anim block resolution {} is not a value of the enum",
                        static_cast<int>(resolution));
        model->globals.animBlockResolution = resolution;
        return Ok(model);
    });
}

void ModelGlobalsApi::setMaxVertices(int limit, int clamp_target) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (limit <= 0 || clamp_target < 0)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "max vertices: limit must be > 0 and the clamp target >= 0");
        model->globals.maxVertices = std::make_pair(limit, clamp_target);
        return Ok(model);
    });
}

void ModelGlobalsApi::setFlexOptimize(FlexOptimizeMode mode) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (mode < SMDL_FLEX_OPTIMIZE_AUTO || mode > SMDL_FLEX_OPTIMIZE_OFF)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex optimize mode {} is not a value of the enum",
                        static_cast<int>(mode));
        model->globals.flexOptimize = mode;
        return Ok(model);
    });
}

void ModelGlobalsApi::setAutocenter(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::autocenter, enabled); });
}

void ModelGlobalsApi::setCenterBonesOnVerts(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::centerBonesOnVerts, enabled); });
}

void ModelGlobalsApi::setStaticProp(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::staticProp, enabled); });
}

void ModelGlobalsApi::setOpaque(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::opaque, enabled); });
}

void ModelGlobalsApi::setMostlyOpaque(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::mostlyOpaque, enabled); });
}

void ModelGlobalsApi::setAmbientBoost(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::ambientBoost, enabled); });
}

void ModelGlobalsApi::setNoForcedFade(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::noForcedFade, enabled); });
}

void ModelGlobalsApi::setDoNotCastShadows(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::doNotCastShadows, enabled); });
}

void ModelGlobalsApi::setCastTextureShadows(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::castTextureShadows, enabled); });
}

void ModelGlobalsApi::setSubd(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::subd, enabled); });
}

void ModelGlobalsApi::setObsolete(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::obsolete, enabled); });
}

void ModelGlobalsApi::setForcePhonemeCrossfade(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::forcePhonemeCrossfade, enabled); });
}

void ModelGlobalsApi::setBBoxOnlyVerts(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::bboxOnlyVerts, enabled); });
}

void ModelGlobalsApi::setSkipBoneInBBox(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::skipBoneInBBox, enabled); });
}

void ModelGlobalsApi::setErrorOnSequenceRemapFailure(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::errorOnSequenceRemapFailure, enabled); });
}

void ModelGlobalsApi::setModelHasNoSequences(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::modelHasNoSequences, enabled); });
}

void ModelGlobalsApi::setCalcTransitions(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::calcTransitions, enabled); });
}

void ModelGlobalsApi::setAnimBlockNoStall(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::animBlockNoStall, enabled); });
}

void ModelGlobalsApi::setAnimBlockCacheHighRes(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::animBlockCacheHighRes, enabled); });
}

void ModelGlobalsApi::setZBrush(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::zbrush, enabled); });
}

void ModelGlobalsApi::setPreserveTriangleOrder(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::preserveTriangleOrder, enabled); });
}

void ModelGlobalsApi::setClipToTextures(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::clipToTextures, enabled); });
}

void ModelGlobalsApi::setNoWarnings(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::noWarnings, enabled); });
}

void ModelGlobalsApi::setStripLods(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::stripLods, enabled); });
}

void ModelGlobalsApi::setBuildPreview(bool enabled) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetFlag(model, &GlobalsDesc::buildPreview, enabled); });
}

void ModelGlobalsApi::setDumpPerf(bool enabled) {
    this->model()->globals.dumpPerf = enabled;
}

void ModelGlobalsApi::setOriginAngle(float degrees) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "origin angle", degrees, &GlobalsDesc::originAngle, false); });
}

void ModelGlobalsApi::setMaxEyeDeflection(float degrees) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "max eye deflection", degrees, &GlobalsDesc::maxEyeDeflection, false); });
}

void ModelGlobalsApi::setGamma(float gamma) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "gamma", gamma, &GlobalsDesc::gamma, false); });
}

void ModelGlobalsApi::setCollisionPrecision(float precision) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "collision precision", precision, &GlobalsDesc::collisionPrecision, false); });
}

void ModelGlobalsApi::setDefaultFadeInTime(float seconds) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "default fade in time", seconds, &GlobalsDesc::defaultFadeInTime, true); });
}

void ModelGlobalsApi::setDefaultFadeOutTime(float seconds) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "default fade out time", seconds, &GlobalsDesc::defaultFadeOutTime, true); });
}

void ModelGlobalsApi::setMotionRollback(float rollback) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "motion rollback", rollback, &GlobalsDesc::motionRollback, false); });
}

void ModelGlobalsApi::setAnimBlockPosDelta(float delta) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "anim block position delta", delta, &GlobalsDesc::animBlockPosDelta, false); });
}

void ModelGlobalsApi::setNormalBlendAngle(float degrees) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalFloat(model, "normal blend angle", degrees, &GlobalsDesc::normalBlendAngle, true); });
}

void ModelGlobalsApi::setMinLod(int min_lod) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalInt(model, "min lod", min_lod, &GlobalsDesc::minLod, 0); });
}

void ModelGlobalsApi::setAllowRootLods(int count) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalInt(model, "allowed root lods", count, &GlobalsDesc::allowRootLods, 0); });
}

void ModelGlobalsApi::setAnimBlockNumFrames(int frames) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalInt(model, "anim block frame count", frames, &GlobalsDesc::animBlockNumFrames, 0); });
}

void ModelGlobalsApi::setFlexOptimizeMinStatic(int min_static_vertices) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalInt(model, "flex optimize minimum static vertices", min_static_vertices, &GlobalsDesc::flexOptimizeMinStatic, 0); });
}

void ModelGlobalsApi::setMaxWarnings(int count) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return OptionalInt(model, "max warnings", count, &GlobalsDesc::maxWarnings, -1); });
}

void ModelGlobalsApi::setFitWeights(bool enabled, int fittingPoses, int testPoses) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        for (const int count: {fittingPoses, testPoses}) {
            if (count < 0 || count > kMaxFitWeightsPoses)
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "fit weights pose count must be 0..{}, got {}",
                            kMaxFitWeightsPoses, count);
        }
        model->globals.fitWeights = enabled;
        model->globals.fitWeightsFitPoses = fittingPoses;
        model->globals.fitWeightsTestPoses = testPoses;
        return Ok(model);
    });
}

} // namespace detail
} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
extern "C" {

SMDL_API smdl_status smdl_model_set_internal_name(smdl_model *model, const char *name) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setInternalName(name ? name : ""); });
}

SMDL_API smdl_status smdl_model_add_material_path(smdl_model *model, const char *path) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addMaterialPath(path ? path : ""); });
}

SMDL_API smdl_status smdl_model_add_include_model(smdl_model *model, const char *path) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addIncludeModel(path ? path : ""); });
}

SMDL_API smdl_status smdl_model_override_material(smdl_model *model, const char *name) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).overrideMaterial(name ? name : ""); });
}

SMDL_API smdl_status smdl_model_set_surfaceprop(smdl_model *model, const char *name) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setSurfaceProp(name ? name : ""); });
}

SMDL_API smdl_status smdl_model_add_allowed_activity_name(smdl_model *model, const char *name) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addAllowedActivityName(name ? name : ""); });
}

SMDL_API smdl_status smdl_model_set_qc_path(smdl_model *model, const char *path) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setQcPath(path ? path : ""); });
}

SMDL_API smdl_status smdl_model_add_keyvalues(smdl_model *model, const char *text) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!text)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "keyvalues text is null");
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addKeyValues(text); });
}

SMDL_API smdl_status smdl_model_add_skin_family(smdl_model *model, const char *const *materials, size_t count,
                                                uint32_t *out_index) {
    if (out_index)
        *out_index = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        // a null array or entry reads as an empty name, which the C++ method rejects with the same messages
        std::vector<std::string> names;
        if (materials) {
            names.reserve(count);
            for (size_t i = 0; i < count; ++i)
                names.emplace_back(materials[i] ? materials[i] : "");
        }
        const uint32_t index = smdl::detail::BorrowModel(model).addSkinFamily(std::span<const std::string>(names));
        if (out_index)
            *out_index = index;
    });
}

SMDL_API smdl_status smdl_model_add_texture_replacement(smdl_model *model, const char *replacement, const char *replaces) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addTextureReplacement(replacement ? replacement : "", replaces ? replaces : ""); });
}

SMDL_API smdl_status smdl_model_set_scale(smdl_model *model, float scale) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setScale(scale); });
}

SMDL_API smdl_status smdl_model_set_origin(smdl_model *model, const float origin[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!origin)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "origin must be 3 finite numbers");
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setOrigin({origin[0], origin[1], origin[2]}); });
}

SMDL_API smdl_status smdl_model_set_up_axis(smdl_model *model, smdl_up_axis axis) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setUpAxis(axis); });
}

SMDL_API smdl_status smdl_model_set_bone_surfaceprop(smdl_model *model, smdl_bone_id bone, const char *name) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBoneSurfaceProp(bone, name ? name : ""); });
}

SMDL_API smdl_status smdl_model_set_contents(smdl_model *model, uint32_t flags) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setContents(flags); });
}

SMDL_API smdl_status smdl_model_set_bone_contents(smdl_model *model, smdl_bone_id bone, uint32_t flags) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBoneContents(bone, flags); });
}

SMDL_API smdl_status smdl_model_set_constant_directional_light(smdl_model *model, int enabled, float dot) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setConstantDirectionalLight(enabled != 0, dot); });
}

SMDL_API smdl_status smdl_model_set_eye_position(smdl_model *model, const float position[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "eye position must be 3 finite numbers");
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setEyePosition({position[0], position[1], position[2]}); });
}

SMDL_API smdl_status smdl_model_set_illum_position(smdl_model *model, const float position[3],
                                                   smdl_bone_id attachment_bone) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "illum position must be 3 finite numbers");
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setIllumPosition({position[0], position[1], position[2]}, attachment_bone); });
}

SMDL_API smdl_status smdl_model_set_bbox(smdl_model *model, const float mins[3], const float maxs[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!mins || !maxs)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bbox: mins and maxs must be 3 finite numbers each");
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBBox({mins[0], mins[1], mins[2]}, {maxs[0], maxs[1], maxs[2]}); });
}

SMDL_API smdl_status smdl_model_set_cbox(smdl_model *model, const float mins[3], const float maxs[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!mins || !maxs)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "cbox: mins and maxs must be 3 finite numbers each");
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setCBox({mins[0], mins[1], mins[2]}, {maxs[0], maxs[1], maxs[2]}); });
}

SMDL_API smdl_status smdl_model_set_clamp_worldspace(smdl_model *model, const float mins[3], const float maxs[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!mins || !maxs)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "clamp worldspace: mins and maxs must be 3 finite numbers each");
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setClampWorldspace({mins[0], mins[1], mins[2]}, {maxs[0], maxs[1], maxs[2]}); });
}

SMDL_API smdl_status smdl_model_add_skip_transition(smdl_model *model, const char *const *nodes, size_t count) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        // a null array or entry reads as an empty name, which the C++ method rejects with the same messages
        std::vector<std::string> names;
        if (nodes) {
            names.reserve(count);
            for (size_t i = 0; i < count; ++i)
                names.emplace_back(nodes[i] ? nodes[i] : "");
        }
        smdl::detail::BorrowModel(model).addSkipTransition(std::span<const std::string>(names));
    });
}

SMDL_API smdl_status smdl_model_set_section_frames(smdl_model *model, int frames, int min_frames) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setSectionFrames(frames, min_frames); });
}

SMDL_API smdl_status smdl_model_set_anim_block_size(smdl_model *model, int size) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAnimBlockSize(size); });
}

SMDL_API smdl_status smdl_model_set_anim_block_resolution(smdl_model *model, smdl_anim_block_resolution resolution) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAnimBlockResolution(resolution); });
}

SMDL_API smdl_status smdl_model_set_max_vertices(smdl_model *model, int limit, int clamp_target) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setMaxVertices(limit, clamp_target); });
}

SMDL_API smdl_status smdl_model_set_flex_optimize(smdl_model *model, smdl_flex_optimize_mode mode) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setFlexOptimize(mode); });
}

SMDL_API smdl_status smdl_model_set_autocenter(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAutocenter(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_center_bones_on_verts(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setCenterBonesOnVerts(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_static_prop(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setStaticProp(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_opaque(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setOpaque(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_mostly_opaque(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setMostlyOpaque(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_ambient_boost(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAmbientBoost(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_no_forced_fade(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setNoForcedFade(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_do_not_cast_shadows(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setDoNotCastShadows(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_cast_texture_shadows(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setCastTextureShadows(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_subd(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setSubd(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_obsolete(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setObsolete(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_force_phoneme_crossfade(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setForcePhonemeCrossfade(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_bbox_only_verts(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBBoxOnlyVerts(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_skip_bone_in_bbox(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setSkipBoneInBBox(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_error_on_sequence_remap_failure(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setErrorOnSequenceRemapFailure(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_model_has_no_sequences(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setModelHasNoSequences(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_calc_transitions(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setCalcTransitions(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_anim_block_no_stall(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAnimBlockNoStall(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_anim_block_cache_high_res(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAnimBlockCacheHighRes(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_zbrush(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setZBrush(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_preserve_triangle_order(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setPreserveTriangleOrder(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_clip_to_textures(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setClipToTextures(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_no_warnings(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setNoWarnings(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_strip_lods(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setStripLods(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_build_preview(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBuildPreview(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_origin_angle(smdl_model *model, float degrees) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setOriginAngle(degrees); });
}

SMDL_API smdl_status smdl_model_set_max_eye_deflection(smdl_model *model, float degrees) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setMaxEyeDeflection(degrees); });
}

SMDL_API smdl_status smdl_model_set_gamma(smdl_model *model, float gamma) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setGamma(gamma); });
}

SMDL_API smdl_status smdl_model_set_collision_precision(smdl_model *model, float precision) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setCollisionPrecision(precision); });
}

SMDL_API smdl_status smdl_model_set_default_fade_in_time(smdl_model *model, float seconds) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setDefaultFadeInTime(seconds); });
}

SMDL_API smdl_status smdl_model_set_default_fade_out_time(smdl_model *model, float seconds) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setDefaultFadeOutTime(seconds); });
}

SMDL_API smdl_status smdl_model_set_motion_rollback(smdl_model *model, float rollback) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setMotionRollback(rollback); });
}

SMDL_API smdl_status smdl_model_set_anim_block_pos_delta(smdl_model *model, float delta) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAnimBlockPosDelta(delta); });
}

SMDL_API smdl_status smdl_model_set_normal_blend_angle(smdl_model *model, float degrees) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setNormalBlendAngle(degrees); });
}

SMDL_API smdl_status smdl_model_set_min_lod(smdl_model *model, int min_lod) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setMinLod(min_lod); });
}

SMDL_API smdl_status smdl_model_set_allow_root_lods(smdl_model *model, int count) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAllowRootLods(count); });
}

SMDL_API smdl_status smdl_model_set_anim_block_num_frames(smdl_model *model, int frames) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAnimBlockNumFrames(frames); });
}

SMDL_API smdl_status smdl_model_set_flex_optimize_min_static(smdl_model *model, int min_static_vertices) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setFlexOptimizeMinStatic(min_static_vertices); });
}

SMDL_API smdl_status smdl_model_set_max_warnings(smdl_model *model, int count) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setMaxWarnings(count); });
}

SMDL_API smdl_status smdl_model_set_fit_weights(smdl_model *model, int enabled, int fitting_poses, int test_poses) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setFitWeights(enabled != 0, fitting_poses, test_poses);
    });
}

} // extern "C"
SMDL_API smdl_status smdl_model_set_dump_perf(smdl_model* model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setDumpPerf(enabled != 0); });
}
