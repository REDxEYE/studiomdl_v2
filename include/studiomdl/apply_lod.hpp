// Apply functions for the LOD QC commands ($lod, $shadowlod and the options of their blocks), shared by the QC parsers
// (studiomdl_commands.cpp: Cmd_LOD, Cmd_ShadowLOD) and the builder API front end (api/src/apply_lod.cpp): the parser
// turns tokens into typed parameters, these apply them to StudioMdlState (ctx.body.ScriptLODs, ctx.header.gflags).
//
// A LOD is a LodScriptData_t. It is begun (ApplyLodBegin), its options are applied with the functions below while the
// entry is the last one of ctx.body.ScriptLODs, and it is ended (ApplyLodEnd). Bones are referred to by name; they are
// resolved when the model is simplified (UnifyLODs), exactly as for the QC.
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "studiomdl/studiomdl_global_state.hpp"

enum class LodBeginResult {
    Ok,
    ShadowLodNotLast, // a $shadowlod exists and must be the last LOD
    TooManyLods,      // more than MAX_NUM_LODS
};

// $lod / $shadowlod: appends a LOD (switch value -1 until the caller sets it; the shadow LOD keeps -1) and returns it in
// `lod`. A shadow LOD has facial animation off by default. With -striplods every LOD after the first is marked
// stripped: its sources are not loaded and ApplyLodEnd forgets it. Printing "Processing $shadowlod" is part of this.
// On an error the LOD list is unchanged except for TooManyLods (the compile is abandoned then).
LodBeginResult ApplyLodBegin(StudioMdlState &ctx, bool isShadow, LodScriptData_t *&lod);

// End of the block: a stripped LOD is dropped again; a $shadowlod marks the model as having one.
void ApplyLodEnd(StudioMdlState &ctx, bool isShadow);

// `replacemodel <from> <to> [reverse]`: from = the source name of the replaced model (extension removed), to = the
// replacement. `load` returns the replacement's source (Load_Source for the QC, BuildSourceFromGeometry for the
// builder); it is not called for a stripped LOD (the message "Stripped lod" is printed instead) and not for a
// `from` of "blank" (the entry is dropped).
void ApplyLodReplaceModel(StudioMdlState &ctx, LodScriptData_t &lod, std::string_view from, std::string_view to,
                          const std::function<s_source_t *()> &load);

// `removemodel <from>`: the LOD stops using the model (no source)
void ApplyLodRemoveModel(LodScriptData_t &lod, std::string_view from);

// `replacebone <from> <to>`, `bonetreecollapse <bone>`
void ApplyLodReplaceBone(LodScriptData_t &lod, std::string_view from, std::string_view to);
void ApplyLodBoneTreeCollapse(LodScriptData_t &lod, std::string_view bone);

// `replacematerial <from> <to>`: `to` joins the master material list unless the LOD is stripped
void ApplyLodReplaceMaterial(StudioMdlState &ctx, LodScriptData_t &lod, std::string_view from, std::string_view to);

// `removemesh <material>`: the LOD drops every mesh that uses the material (matched by its base name)
void ApplyLodRemoveMesh(LodScriptData_t &lod, std::string_view material);

// `facial` / `nofacial`
void ApplyLodFacialAnimation(LodScriptData_t &lod, bool enabled);

// `use_shadowlod_materials` (only meaningful in a $shadowlod)
void ApplyLodUseShadowLodMaterials(StudioMdlState &ctx);

// The name a model is replaced by: the source path without its extension (what the QC parser strips before the
// lookup and what GetModelLODSource strips from the model's file name)
std::string LodSourceName(std::string_view path);
