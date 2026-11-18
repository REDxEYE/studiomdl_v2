// Apply functions for the collision QC commands ($collisionmodel / $collisionjoints and the commands of their blocks),
// shared by the QC parser (collisionmodel.cpp: DoCollisionModel, ParseCollisionCommands) and the builder API front end
// (api/src/apply_collision.cpp): the parser turns tokens into typed parameters, these apply them to the compile's
// collision model (ctx.collision.jointedModel, created by the first of them). They are defined in collisionmodel.cpp,
// next to the class they drive.
//
// Joint names are the bones of the collision source (its "local bones"). The commands act immediately, in call order,
// like the QC lines: pairs and constraints are lists, merges and skips change the bone map.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "studiomdl/studiomdl_global_state.hpp"

// -- the source -----------------------------------------------------------------------------------------------------

// Materials and textures the collision source's load added: they are removed again (a collision model's materials do
// not belong to the model) when the model had materials before. Take the counts before the source is loaded or built.
struct CollisionMaterialCounts {
    size_t materials = 0;
    size_t textures = 0;
};
CollisionMaterialCounts GetCollisionMaterialCounts(const StudioMdlState &ctx);
void ApplyCollisionDropNewMaterials(StudioMdlState &ctx, s_source_t *source, const CollisionMaterialCounts &before);

// $collisionmodel <file> / $collisionjoints <file>: the root collision source (bone map: every bone maps to itself)
void ApplyCollisionSource(StudioMdlState &ctx, s_source_t *source);
// `blank` instead of a file: the root collision is empty, convex sources (ApplyCollisionConvexSource) follow
void ApplyCollisionBlankRoot(StudioMdlState &ctx);
// End of the command: with a constraint system ($collisionjoints) or one body
void ApplyCollisionJointed(StudioMdlState &ctx, bool jointed);
// Start of the command's block: $convexhullcountoverride is off again
void ApplyCollisionBeginBlock(StudioMdlState &ctx);

// -- whole model ----------------------------------------------------------------------------------------------------

void ApplyCollisionMass(StudioMdlState &ctx, float mass);                 // $mass
void ApplyCollisionAutoMass(StudioMdlState &ctx);                         // $automass
void ApplyCollisionInertia(StudioMdlState &ctx, float inertia);           // $inertia (default of every joint)
void ApplyCollisionDamping(StudioMdlState &ctx, float damping);           // $damping
void ApplyCollisionRotDamping(StudioMdlState &ctx, float rotDamping);     // $rotdamping
void ApplyCollisionDrag(StudioMdlState &ctx, float drag);                 // $drag
void ApplyCollisionMaxConvexPieces(StudioMdlState &ctx, int count);       // $maxconvexpieces
void ApplyCollisionRemove2D(StudioMdlState &ctx);                         // $remove2d
void ApplyCollisionConcavePerJoint(StudioMdlState &ctx);                  // $concaveperjoint
void ApplyCollisionWeldPosition(StudioMdlState &ctx, float epsilon);      // $weldposition
void ApplyCollisionWeldNormal(StudioMdlState &ctx, float epsilon);        // $weldnormal
void ApplyCollisionConcave(StudioMdlState &ctx);                          // $concave
void ApplyCollisionConvexHullCountOverride(StudioMdlState &ctx);          // $convexhullcountoverride
void ApplyCollisionMassCenter(StudioMdlState &ctx, const Vector &center); // $masscenter
void ApplyCollisionNoSelfCollisions(StudioMdlState &ctx);                 // $noselfcollisions
void ApplyCollisionAssumeWorldSpace(StudioMdlState &ctx);                 // $assumeworldspace
void ApplyCollisionRootBone(StudioMdlState &ctx, std::string_view bone);  // $rootbone
// $animatedfriction <min> <max> <time in> <time hold> <time out>
void ApplyCollisionAnimatedFriction(StudioMdlState &ctx, int minFriction, int maxFriction, float timeIn, float timeHold,
                                    float timeOut);

// -- joints ---------------------------------------------------------------------------------------------------------

void ApplyCollisionJointSkip(StudioMdlState &ctx, std::string_view joint);                    // $jointskip
void ApplyCollisionJointMerge(StudioMdlState &ctx, std::string_view parent, std::string_view child); // $jointmerge
void ApplyCollisionJointInertia(StudioMdlState &ctx, std::string_view joint, float inertia);  // $jointinertia
void ApplyCollisionJointDamping(StudioMdlState &ctx, std::string_view joint, float damping);  // $jointdamping
void ApplyCollisionJointRotDamping(StudioMdlState &ctx, std::string_view joint, float rotDamping); // $jointrotdamping
void ApplyCollisionJointMassBias(StudioMdlState &ctx, std::string_view joint, float massBias); // $jointmassbias

enum class CollisionJointType { Free = 0, Fixed = 1, Limit = 2 };
// $jointconstrain <joint> <axis> <type> <min> <max> [<friction>] in two steps (the QC checks the type between them):
// the check finds the joint (a warning and nullptr when the collision source has no such bone) and fails with MdlError
// for an axis outside 0..2 (x, y, z) or min > max; it returns the joint's name as the source spells it. The add
// appends the constraint (friction is the QC value: the compile divides it by 5; the QC default is 1).
const char *ApplyCollisionJointConstrainCheck(StudioMdlState &ctx, std::string_view joint, int axis, float limitMin,
                                              float limitMax);
void ApplyCollisionJointConstrain(StudioMdlState &ctx, const char *jointName, int axis, CollisionJointType type,
                                  float limitMin, float limitMax, float friction);

// $jointcollide <a> <b>, $jointcollidealltoall { ... } (every ordered pair of different entries, at most 32 names),
// $jointnocollide <a> <b> (removes both directions)
void ApplyCollisionJointCollide(StudioMdlState &ctx, std::string_view a, std::string_view b);
void ApplyCollisionJointCollideAllToAll(StudioMdlState &ctx, std::span<const std::string> joints);
void ApplyCollisionJointNoCollide(StudioMdlState &ctx, std::string_view a, std::string_view b);

// -- convex sources and text ----------------------------------------------------------------------------------------

// $addconvexsrc: takes the next free slot (at most 24; -1 and a warning when they are all used, `name` is for the
// message), then the slot gets its source, optionally an offset and the concave flag. The caller loads the source
// (and drops its new materials) between the first two steps.
int ApplyCollisionNextConvexSlot(StudioMdlState &ctx, std::string_view name);
void ApplyCollisionConvexSource(StudioMdlState &ctx, int slot, s_source_t *source);
// offset pos[...] angle[...] scale[...]: position, QAngle degrees, scale relative to the model's $scale
void ApplyCollisionConvexOffset(StudioMdlState &ctx, int slot, const Vector &position, const QAngle &angles, float scale);
void ApplyCollisionConvexConcave(StudioMdlState &ctx, int slot);
// Collision text (the .phy KeyValues section) already has its entry point, LoadCollisionText (collisionmodel.h).
