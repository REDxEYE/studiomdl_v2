// Turning a smdl_model into StudioMdlState (the builder front end). smdl_model_compile (compile.cpp) calls the part
// entry points in this order, inside one compile (BeginProcessing, ctx with defaults, diagnostics throwing
// MdlCompileError):
//
//   ApplyGlobals       Core     model path, material paths, flags, transforms (before any source is built)
//   ApplySkeleton      Core     bone options that must exist before sources ($definebone-like rest poses, ...)
//   ApplyBody          Core     body groups and meshes -> s_model_t + s_source_t (BuildSourceFromGeometry),
//                               ApplyMeshCombination (what loading a DMX does to its source), AddBodyFlexData,
//                               ApplyMeshFlex per mesh, attachments, AddBodyFlexRules; presets, skins
//   ApplyFlex          Flex     model-level flex: controllers, rules, local vars, mouths, ...
//   ApplyLods          LodPhys  LODs
//   ApplyRig           Anim     attachments, hitboxes, pose params, IK chains, weight lists
//   ApplyAnimations    Anim     animations
//   ApplySequences     Anim     sequences
//   ApplyBoneDynamics  LodPhys  jiggle and procedural bones
//   ApplyCollision     LodPhys  collision model
//   ApplyFinish        Core     whatever needs everything above (e.g. the default sequence of a static prop)
//
// Then BuildModelFiles(ctx) (the shared back end) builds the files. Errors: call MdlError (it throws during an API
// compile); put the object being applied on the stack with ScopedApplyObject so the result can name it.
#pragma once

#include <exception>
#include <string>
#include <unordered_map>
#include <vector>

#include "model.hpp"

struct StudioMdlState;
struct s_source_t;
struct s_model_t;

struct ApplyState {
    explicit ApplyState(const smdl_model &m) : model(m) {}

    const smdl_model &model;

    // filled by BuildSourceFromGeometry / ApplyBody, read by later parts
    std::unordered_map<const smdl_geometry *, s_source_t *> sources;
    std::unordered_map<const smdl_mesh *, s_model_t *> meshModels;
    std::unordered_map<const smdl_mesh *, int> meshModelIndex; // index in ctx.body.model (imodel)

    // error context: "bodygroup 'lid' / mesh 'a'"
    std::vector<std::string> objectStack;
    std::string failedObject; // objectPath() when the first exception unwound through a ScopedApplyObject
    std::string objectPath() const;
};

class ScopedApplyObject {
public:
    ScopedApplyObject(ApplyState &state, std::string object)
        : m_State(state), m_Exceptions(std::uncaught_exceptions()) {
        m_State.objectStack.push_back(std::move(object));
    }
    ~ScopedApplyObject() {
        if (std::uncaught_exceptions() > m_Exceptions && m_State.failedObject.empty())
            m_State.failedObject = m_State.objectPath();
        m_State.objectStack.pop_back();
    }
    ScopedApplyObject(const ScopedApplyObject &) = delete;
    ScopedApplyObject &operator=(const ScopedApplyObject &) = delete;

private:
    ApplyState &m_State;
    int m_Exceptions;
};

// Part entry points (see the order above)
void ApplyGlobals(StudioMdlState &ctx, ApplyState &state);
void ApplySkeleton(StudioMdlState &ctx, ApplyState &state);
void ApplyBody(StudioMdlState &ctx, ApplyState &state);
void ApplyFlex(StudioMdlState &ctx, ApplyState &state);
void ApplyLods(StudioMdlState &ctx, ApplyState &state);
void ApplyRig(StudioMdlState &ctx, ApplyState &state);
void ApplyAnimations(StudioMdlState &ctx, ApplyState &state);
void ApplySequences(StudioMdlState &ctx, ApplyState &state);
void ApplyBoneDynamics(StudioMdlState &ctx, ApplyState &state);
void ApplyCollision(StudioMdlState &ctx, ApplyState &state);
void ApplyFinish(StudioMdlState &ctx, ApplyState &state);

// Core (apply_geometry.cpp): builds the s_source_t of a geometry the way a SMD/DMX loader would (skeleton from the
// model skeleton plus the geometry's rest overrides, BindPose frame, unified vertices through BuildIndividualMeshes,
// materials through LookupTexture/UseTextureAsMaterial), appends it to ctx.anim.sources and records it in
// state.sources. Calls BuildShapeAnimations for the geometry's shapes. Returns the cached source when called again
// for the same geometry.
s_source_t *BuildSourceFromGeometry(StudioMdlState &ctx, ApplyState &state, const smdl_geometry &geometry,
                                    bool isActiveModel);

// Flex (apply_flex.cpp): adds the geometry's shapes to the source as vertex animation frames (what Load_VTA / the
// DMX delta states produce). Called by BuildSourceFromGeometry once the base vertices exist.
void BuildShapeAnimations(StudioMdlState &ctx, ApplyState &state, const smdl_geometry &geometry, s_source_t *source);

// Flex (apply_flex.cpp): the DMX part of a mesh: the combination operator built from the geometry's shapes (delta
// states, flex keys, controls, domination rules, flex rules), what LoadModelAndSkeleton does to a DMX source after
// loading its meshes. Called by ApplyBody once the mesh's s_model_t and source exist, before AddBodyFlexData.
void ApplyMeshCombination(StudioMdlState &ctx, ApplyState &state, const smdl_mesh &mesh, s_model_t *pmodel);

// Flex (apply_flex.cpp): the per-mesh part of the $model block (flex keys, eyeballs, eyelids, mesh flex rules...),
// called by ApplyBody after AddBodyFlexData (the flexes of the loaded source) and before the source's flex rules.
void ApplyMeshFlex(StudioMdlState &ctx, ApplyState &state, const smdl_mesh &mesh, s_model_t *pmodel, int imodel);
