// Description of LODs (smdl_lod.h); defines struct smdl_lod. Owned by the LodPhys part.
//
// The model's LOD list is the QC's: LOD 0 is the model itself (nothing to describe), the LODs added here are LOD 1,
// 2, ... in creation order, a shadow LOD (at most one) last. Each LOD keeps what the QC block of `$lod` / `$shadowlod`
// lists; the compile applies them in the fixed order replacemodel, removemodel, replacebone, bonetreecollapse,
// replacematerial, removemesh, facial, use_shadowlod_materials (ApplyLods, apply_lod.cpp).
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "desc_shared.hpp"

struct smdl_lod {
    smdl_model *model = nullptr;
    bool isShadow = false;
    float switchDistance = 0.0f;        // unused by a shadow LOD
    bool facialAnimation = true;        // `nofacial` / `facial`; a shadow LOD starts without
    bool useShadowLodMaterials = false; // shadow LOD only

    // replacemodel (geometry set) / removemodel (geometry null): one entry per mesh
    struct ModelReplacement {
        smdl_mesh *mesh = nullptr;
        smdl_geometry *geometry = nullptr;
    };
    std::vector<ModelReplacement> modelReplacements;
    std::vector<std::pair<smdl_bone_id, smdl_bone_id>> boneReplacements; // replacebone from, to
    std::vector<smdl_bone_id> boneTreeCollapses;                          // bonetreecollapse
    std::vector<std::pair<std::string, std::string>> materialReplacements; // replacematerial from, to
    std::vector<std::string> removedMaterials;                            // removemesh <material>

    // 1-based position in the model's LOD list, for messages ("lod 1", "shadow lod")
    std::string label;
};

struct LodsDesc {
    std::vector<std::unique_ptr<smdl_lod>> lods; // creation order; the shadow LOD, when present, is the last
    bool hasShadow() const { return !lods.empty() && lods.back()->isShadow; }
};
