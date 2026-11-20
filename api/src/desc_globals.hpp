// Description of model-wide settings (smdl_globals.h). Owned by the Core part. A value that was never set is left out
// (std::optional / false) so the compile keeps studiomdl's default, as a QC without the command does.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "desc_shared.hpp"
#include "studiomdl/c/smdl_globals.h"

struct GlobalsDesc {
    // identity and paths
    std::optional<std::string> internalName;       // $internalname
    std::vector<std::string> materialPaths;        // $cdmaterials, in call order
    std::vector<std::string> includeModels;        // $includemodel

    // skins and material replacement
    std::vector<std::vector<std::string>> skinFamilies; // $texturegroup rows, row 0 = default skin
    std::optional<std::string> overrideMaterial;   // $overridematerial
    std::vector<std::pair<std::string, std::string>> textureReplacements; // -t <replacement> [<replaced>]

    // transform
    std::optional<float> scale;                    // $scale
    std::optional<Vector> origin;                  // $origin
    std::optional<float> originAngle;              // $origin ... <angle>, degrees
    std::optional<smdl_up_axis> upAxis;            // $upaxis
    bool autocenter = false;
    bool centerBonesOnVerts = false;

    // surface properties and contents
    std::optional<std::string> surfaceProp;
    std::map<smdl_bone_id, std::string> boneSurfaceProp;   // $jointsurfaceprop
    std::optional<uint32_t> contents;                       // smdl_contents_flags
    std::map<smdl_bone_id, uint32_t> boneContents;          // $jointcontents

    // flags
    bool staticProp = false;
    bool opaque = false;
    bool mostlyOpaque = false;
    bool ambientBoost = false;
    bool noForcedFade = false;
    bool doNotCastShadows = false;
    bool castTextureShadows = false;
    bool subd = false;
    bool obsolete = false;
    bool forcePhonemeCrossfade = false;
    bool dumpPerf = false;
    std::optional<float> constantDirectionalLight;          // dot product

    // header values
    std::optional<Vector> eyePosition;
    struct IllumPosition {
        Vector position;
        smdl_bone_id attachmentBone = SMDL_NO_BONE;
    };
    std::optional<IllumPosition> illumPosition;
    std::optional<float> maxEyeDeflection;
    std::optional<float> gamma;
    std::optional<std::pair<Vector, Vector>> bbox;
    std::optional<std::pair<Vector, Vector>> cbox;
    bool bboxOnlyVerts = false;
    bool skipBoneInBBox = false;
    std::optional<float> collisionPrecision;
    std::string keyValues;                                   // formatted text of the $keyvalues blocks, appended
    std::optional<std::string> qcPath;                       // value of the `qc_path` key value; unset = no entry

    // animation and LOD defaults
    std::optional<int> minLod;
    std::optional<int> allowRootLods;
    std::optional<float> defaultFadeInTime;
    std::optional<float> defaultFadeOutTime;
    std::optional<float> motionRollback;
    std::optional<std::pair<Vector, Vector>> clampWorldspace;
    std::vector<std::string> allowedActivityNames;
    bool errorOnSequenceRemapFailure = false;
    bool modelHasNoSequences = false;
    bool calcTransitions = false;
    std::vector<std::vector<std::string>> skipTransitions;
    std::optional<std::pair<int, int>> sectionFrames;       // frames, min frames
    int animBlockSize = 0;                                   // 0 = not set
    bool animBlockNoStall = false;
    smdl_anim_block_resolution animBlockResolution = SMDL_ANIM_BLOCK_DEFAULT;
    std::optional<int> animBlockNumFrames;
    bool animBlockCacheHighRes = false;
    std::optional<float> animBlockPosDelta;

    // compile options
    bool zbrush = false;
    bool preserveTriangleOrder = false;
    bool clipToTextures = false;
    std::optional<std::pair<int, int>> maxVertices;          // limit, clamp target (0 = none)
    std::optional<smdl_flex_optimize_mode> flexOptimize;
    std::optional<int> flexOptimizeMinStatic;
    std::optional<float> normalBlendAngle;
    bool noWarnings = false;
    std::optional<int> maxWarnings;
    bool fitWeights = false;
    int fitWeightsFitPoses = 0, fitWeightsTestPoses = 0; // 0 = studiomdl's default
    bool stripLods = false;
    bool buildPreview = false;
};
