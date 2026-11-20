// Builder front end, LodPhys part: the collision model. What `$collisionmodel` / `$collisionjoints` and the commands of
// their block do, through the shared apply functions of studiomdl/collisionmodel.cpp (apply_collision.hpp; the QC parser
// calls the same ones). The geometries are built into sources with BuildSourceFromGeometry where the QC loads the SMD.
#include <format>
#include <string>

#include "apply.hpp"
#include "studiomdl/apply_collision.hpp"
#include "studiomdl/collisionmodel.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_errors.h"
#include "tier1_v2/function_timing.hpp"

namespace {
    using Kind = CollisionOp::Kind;

    // A collision source is a source of its own, even when the geometry is a mesh's (collision processing moves and
    // welds its vertices, and the QC loads the file again): build it with a state that has no cached sources
    s_source_t *BuildCollisionSource(StudioMdlState &ctx, const ApplyState &state, const smdl_geometry &geometry) {
        ApplyState fresh(state.model);
        return BuildSourceFromGeometry(ctx, fresh, geometry, false);
    }

    void ApplyConvexSource(StudioMdlState &ctx, const ApplyState &state, const CollisionConvexSource &convex) {
        const int slot = ApplyCollisionNextConvexSlot(ctx, convex.geometry->sourceName);
        if (slot < 0)
            return; // (the call checks keep the count within the limit)

        const CollisionMaterialCounts materialCounts = GetCollisionMaterialCounts(ctx);
        s_source_t *source = BuildCollisionSource(ctx, state, *convex.geometry);
        ApplyCollisionDropNewMaterials(ctx, source, materialCounts);
        ApplyCollisionConvexSource(ctx, slot, source);
        if (convex.hasOffset)
            ApplyCollisionConvexOffset(ctx, slot, convex.position, convex.angles, convex.scale);
        if (convex.concave)
            ApplyCollisionConvexConcave(ctx, slot);
    }

    void ApplyConstrain(StudioMdlState &ctx, const std::string &joint, const CollisionOp &op) {
        const int axis = op.integer[0];
        const char *jointName = ApplyCollisionJointConstrainCheck(ctx, joint, axis, op.value[0], op.value[1]);
        if (!jointName)
            return;
        ApplyCollisionJointConstrain(ctx, jointName, axis, static_cast<CollisionJointType>(op.integer[1]), op.value[0],
                                     op.value[1], op.value[2]);
    }

    void ApplyOp(StudioMdlState &ctx, ApplyState &state, const smdl_collision &collision, const CollisionOp &op) {
        const auto &bones = state.model.skeleton.bones;
        const auto name = [&](smdl_bone_id bone) -> const std::string & { return bones[bone].name; };

        switch (op.kind) {
            case Kind::Mass:
                ApplyCollisionMass(ctx, op.value[0]);
                break;
            case Kind::AutoMass:
                ApplyCollisionAutoMass(ctx);
                break;
            case Kind::Inertia:
                ApplyCollisionInertia(ctx, op.value[0]);
                break;
            case Kind::Damping:
                ApplyCollisionDamping(ctx, op.value[0]);
                break;
            case Kind::RotDamping:
                ApplyCollisionRotDamping(ctx, op.value[0]);
                break;
            case Kind::Drag:
                ApplyCollisionDrag(ctx, op.value[0]);
                break;
            case Kind::RollingDrag:
                break; // `$rollingDrag` reads its argument and does nothing in the QC
            case Kind::MaxConvexPieces:
                ApplyCollisionMaxConvexPieces(ctx, op.integer[0]);
                break;
            case Kind::Remove2D:
                ApplyCollisionRemove2D(ctx);
                break;
            case Kind::ConcavePerJoint:
                ApplyCollisionConcavePerJoint(ctx);
                break;
            case Kind::WeldPosition:
                ApplyCollisionWeldPosition(ctx, op.value[0]);
                break;
            case Kind::WeldNormal:
                ApplyCollisionWeldNormal(ctx, op.value[0]);
                break;
            case Kind::Concave:
                ApplyCollisionConcave(ctx);
                break;
            case Kind::ConvexHullCountOverride:
                ApplyCollisionConvexHullCountOverride(ctx);
                break;
            case Kind::MassCenter:
                ApplyCollisionMassCenter(ctx, op.vector);
                break;
            case Kind::NoSelfCollisions:
                ApplyCollisionNoSelfCollisions(ctx);
                break;
            case Kind::AssumeWorldSpace:
                ApplyCollisionAssumeWorldSpace(ctx);
                break;
            case Kind::RootBone:
                ApplyCollisionRootBone(ctx, name(op.bone));
                break;
            case Kind::AnimatedFriction:
                ApplyCollisionAnimatedFriction(ctx, op.integer[0], op.integer[1], op.value[0], op.value[1], op.value[2]);
                break;
            case Kind::JointSkip:
                ApplyCollisionJointSkip(ctx, name(op.bone));
                break;
            case Kind::JointMerge:
                ApplyCollisionJointMerge(ctx, name(op.bone), name(op.bone2));
                break;
            case Kind::JointConstrain:
                ApplyConstrain(ctx, name(op.bone), op);
                break;
            case Kind::JointInertia:
                ApplyCollisionJointInertia(ctx, name(op.bone), op.value[0]);
                break;
            case Kind::JointDamping:
                ApplyCollisionJointDamping(ctx, name(op.bone), op.value[0]);
                break;
            case Kind::JointRotDamping:
                ApplyCollisionJointRotDamping(ctx, name(op.bone), op.value[0]);
                break;
            case Kind::JointMassBias:
                ApplyCollisionJointMassBias(ctx, name(op.bone), op.value[0]);
                break;
            case Kind::JointCollide:
                ApplyCollisionJointCollide(ctx, name(op.bone), name(op.bone2));
                break;
            case Kind::JointCollideGroup: {
                std::vector<std::string> names;
                names.reserve(op.bones.size());
                for (const smdl_bone_id bone: op.bones)
                    names.push_back(name(bone));
                ApplyCollisionJointCollideAllToAll(ctx, names);
                break;
            }
            case Kind::JointNoCollide:
                ApplyCollisionJointNoCollide(ctx, name(op.bone), name(op.bone2));
                break;
            case Kind::ConvexSource:
                ApplyConvexSource(ctx, state, collision.convexSources[op.index]);
                break;
        }
    }
}

void ApplyCollision(StudioMdlState &ctx, ApplyState &state) {
    vstd::FunctionTimingRecorder _("API");
    const smdl_collision *collision = state.model.collision.collision.get();
    if (!collision || !collision->present())
        return;
    ScopedApplyObject object(state, "collision model");

    const bool hasShape = collision->geometry || !collision->convexSources.empty();
    if (!hasShape && !collision->ops.empty())
        MdlError("collision model has no geometry and no convex source: the commands have nothing to act on");

    if (hasShape) {
        const CollisionMaterialCounts materialCounts = GetCollisionMaterialCounts(ctx);
        if (collision->geometry) {
            s_source_t *source = BuildCollisionSource(ctx, state, *collision->geometry);
            // a collision model's materials do not belong to the model
            ApplyCollisionDropNewMaterials(ctx, source, materialCounts);
            ApplyCollisionSource(ctx, source);
        } else {
            // `$collisionmodel blank`: the convex sources follow
            ApplyCollisionBlankRoot(ctx);
        }

        ApplyCollisionBeginBlock(ctx);
        for (const CollisionOp &op: collision->ops)
            ApplyOp(ctx, state, *collision, op);

        ApplyCollisionJointed(ctx, collision->jointed);
    }

    if (collision->hasText)
        LoadCollisionText(ctx, collision->text.c_str());
}
