// studiomdl C++ wrapper: smdl_globals.h
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "common.hpp"
#include "../c/smdl_globals.h"

namespace smdl {

using UpAxis = smdl_up_axis;
using FlexOptimizeMode = smdl_flex_optimize_mode;
using AnimBlockResolution = smdl_anim_block_resolution;

// $contents / $jointcontents words
inline constexpr uint32_t ContentsSolid = SMDL_CONTENTS_SOLID;
inline constexpr uint32_t ContentsGrate = SMDL_CONTENTS_GRATE;
inline constexpr uint32_t ContentsLadder = SMDL_CONTENTS_LADDER;
inline constexpr uint32_t ContentsMonster = SMDL_CONTENTS_MONSTER;
inline constexpr uint32_t ContentsNotSolid = SMDL_CONTENTS_NOTSOLID;

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle. The C function smdl_model_set_static_prop
    // wraps setStaticProp (defined in api/src/smdl_globals.cpp); see smdl_globals.h for the semantics.
        class ModelGlobalsApi {
    public:
        // Identity and paths
        void setInternalName(std::string_view name);
        void addMaterialPath(std::string_view path);
        void addIncludeModel(std::string_view path);

        // Skins and material replacement. A skin family is a list of material names; the first one added is the
        // default skin. Returns the family index.
        uint32_t addSkinFamily(std::span<const std::string> materials);
        uint32_t addSkinFamily(std::initializer_list<std::string_view> materials) {
            std::vector<std::string> names(materials.begin(), materials.end());
            return addSkinFamily(std::span<const std::string>(names));
        }
        void overrideMaterial(std::string_view name);
        // replaces empty = every material
        void addTextureReplacement(std::string_view replacement, std::string_view replaces = {});

        // Transform
        void setScale(float scale);
        void setOrigin(const Vec3 &origin);
        void setOriginAngle(float degrees);
        void setUpAxis(UpAxis axis);
        void setAutocenter(bool enabled);
        void setCenterBonesOnVerts(bool enabled);

        // Surface properties and contents
        void setSurfaceProp(std::string_view name);
        void setBoneSurfaceProp(BoneId bone, std::string_view name);
        // flags: ContentsSolid | ContentsGrate ...
        void setContents(uint32_t flags);
        void setBoneContents(BoneId bone, uint32_t flags);

        // Flags
        void setStaticProp(bool enabled);
        void setOpaque(bool enabled);
        void setMostlyOpaque(bool enabled);
        void setAmbientBoost(bool enabled);
        void setNoForcedFade(bool enabled);
        void setDoNotCastShadows(bool enabled);
        void setCastTextureShadows(bool enabled);
        void setSubd(bool enabled);
        void setObsolete(bool enabled);
        void setForcePhonemeCrossfade(bool enabled);
        void setConstantDirectionalLight(bool enabled, float dot);

        // Header values
        void setEyePosition(const Vec3 &position);
        // attachmentBone = NoBone: a plain position
        void setIllumPosition(const Vec3 &position, BoneId attachmentBone = NoBone);
        void setMaxEyeDeflection(float degrees);
        void setGamma(float gamma);
        void setBBox(const Vec3 &mins, const Vec3 &maxs);
        void setCBox(const Vec3 &mins, const Vec3 &maxs);
        void setBBoxOnlyVerts(bool enabled);
        void setSkipBoneInBBox(bool enabled);
        void setCollisionPrecision(float precision);
        // KeyValues text without the outer braces ("prop_data { base Wooden.Large }"); throws on a syntax error
        void addKeyValues(std::string_view text);
        // Text of the `qc_path` entry of the .mdl's key values ("" = none, the default of a model built in code)
        void setQcPath(std::string_view path);

        // Animation and LOD defaults
        void setMinLod(int minLod);
        void setAllowRootLods(int count);
        void setDefaultFadeInTime(float seconds);
        void setDefaultFadeOutTime(float seconds);
        void setMotionRollback(float rollback);
        void setClampWorldspace(const Vec3 &mins, const Vec3 &maxs);
        void addAllowedActivityName(std::string_view name);
        void setErrorOnSequenceRemapFailure(bool enabled);
        void setModelHasNoSequences(bool enabled);
        void setCalcTransitions(bool enabled);
        void addSkipTransition(std::span<const std::string> nodes);
        void addSkipTransition(std::initializer_list<std::string_view> nodes) {
            std::vector<std::string> names(nodes.begin(), nodes.end());
            addSkipTransition(std::span<const std::string>(names));
        }
        void setSectionFrames(int frames, int minFrames);
        void setAnimBlockSize(int size);
        void setAnimBlockNoStall(bool enabled);
        void setAnimBlockResolution(AnimBlockResolution resolution);
        void setAnimBlockNumFrames(int frames);
        void setAnimBlockCacheHighRes(bool enabled);
        void setAnimBlockPosDelta(float delta);

        // Compile options
        void setZBrush(bool enabled);
        void setPreserveTriangleOrder(bool enabled);
        void setClipToTextures(bool enabled);
        void setMaxVertices(int limit, int clampTarget = 0);
        void setFlexOptimize(FlexOptimizeMode mode);
        void setFlexOptimizeMinStatic(int minStaticVertices);
        void setNormalBlendAngle(float degrees);
        void setNoWarnings(bool enabled);
        void setMaxWarnings(int count);
        // -fitweights [<fitting poses>,<test poses>] (experimental); 0 keeps studiomdl's default count (24)
        void setFitWeights(bool enabled, int fittingPoses = 0, int testPoses = 0);
        void setStripLods(bool enabled);
        void setBuildPreview(bool enabled);

        void setDumpPerf(bool enabled);

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl
