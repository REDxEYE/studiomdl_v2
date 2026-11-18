// Apply functions for the bone dynamics commands ($jigglebone and the procedural bones of $proceduralbones: quaternion
// interpolation helpers with their triggers, aim constraints, axis interpolation), shared by the QC parsers
// (studiomdl_commands.cpp: Cmd_JiggleBone, Grab_QuatInterpBones, Grab_AimAtBones, Grab_AxisInterpBones) and the builder
// API front end (api/src/apply_bone_dynamics.cpp): the parser turns tokens (or .vrd / .jiggle file lines) into typed
// parameters, these apply them to StudioMdlState (ctx.bones.jigglebones, quatinterpbones, aimatbones, axisinterpbones).
//
// Bones are referred to by name; they are resolved when the model is simplified, exactly as for the QC. Angles that the
// QC writes in degrees are taken in degrees here too (the conversions are the QC's).
#pragma once

#include <string_view>

#include "studiomdl/studiomdl_global_state.hpp"

// -- $jigglebone ----------------------------------------------------------------------------------------------------

// Appends a jiggle bone with the QC defaults (length 10, stiffness 100 for yaw/pitch/along/base, base constraints
// +-100, everything else 0) and returns it for the setters below. The returned reference stays valid until the next
// jiggle bone is added.
s_jigglebone_t &ApplyJiggleBegin(StudioMdlState &ctx, std::string_view bone);
// End of the block: "Marking bone x as a jiggle bone"
void ApplyJiggleEnd(const StudioMdlState &ctx, const s_jigglebone_t &jiggle);

// Section flags: is_flexible, is_rigid, has_base_spring, and `allow_length_flex` (flexible only)
void ApplyJiggleFlexible(s_jigglebone_t &jiggle);
void ApplyJiggleRigid(s_jigglebone_t &jiggle);
void ApplyJiggleBaseSpring(s_jigglebone_t &jiggle);
void ApplyJiggleAllowLengthFlex(s_jigglebone_t &jiggle);

// Parameters of every section
void ApplyJiggleTipMass(s_jigglebone_t &jiggle, float mass);
void ApplyJiggleLength(s_jigglebone_t &jiggle, float length);
void ApplyJiggleAngleConstraint(s_jigglebone_t &jiggle, float degrees);                   // sets the constraint flag
void ApplyJiggleYawConstraint(s_jigglebone_t &jiggle, float minDegrees, float maxDegrees);   // sets the constraint flag
void ApplyJiggleYawFriction(s_jigglebone_t &jiggle, float friction);
void ApplyJiggleYawBounce(s_jigglebone_t &jiggle, float bounce);
void ApplyJigglePitchConstraint(s_jigglebone_t &jiggle, float minDegrees, float maxDegrees); // sets the constraint flag
void ApplyJigglePitchFriction(s_jigglebone_t &jiggle, float friction);
void ApplyJigglePitchBounce(s_jigglebone_t &jiggle, float bounce);

// is_flexible parameters; the stiffness and damping values are clamped to 0..1000 like the QC does
void ApplyJiggleYawStiffness(s_jigglebone_t &jiggle, float stiffness);
void ApplyJiggleYawDamping(s_jigglebone_t &jiggle, float damping);
void ApplyJigglePitchStiffness(s_jigglebone_t &jiggle, float stiffness);
void ApplyJigglePitchDamping(s_jigglebone_t &jiggle, float damping);
void ApplyJiggleAlongStiffness(s_jigglebone_t &jiggle, float stiffness);
void ApplyJiggleAlongDamping(s_jigglebone_t &jiggle, float damping);

// has_base_spring parameters (stiffness and damping clamped to 0..1000)
void ApplyJiggleBaseStiffness(s_jigglebone_t &jiggle, float stiffness);
void ApplyJiggleBaseDamping(s_jigglebone_t &jiggle, float damping);
void ApplyJiggleBaseLeftConstraint(s_jigglebone_t &jiggle, float min, float max);
void ApplyJiggleBaseLeftFriction(s_jigglebone_t &jiggle, float friction);
void ApplyJiggleBaseUpConstraint(s_jigglebone_t &jiggle, float min, float max);
void ApplyJiggleBaseUpFriction(s_jigglebone_t &jiggle, float friction);
void ApplyJiggleBaseForwardConstraint(s_jigglebone_t &jiggle, float min, float max);
void ApplyJiggleBaseForwardFriction(s_jigglebone_t &jiggle, float friction);
void ApplyJiggleBaseMass(s_jigglebone_t &jiggle, float mass);

// -- quaternion interpolation bones (.vrd `<helper>`) -----------------------------------------------------------------

// `<helper> <bone> <parent> <control parent> <control>`: appends a helper (no triggers yet, zero size and percentage)
s_quatinterpbone_t &ApplyQuatInterpBegin(StudioMdlState &ctx, std::string_view bone, std::string_view parent,
                                         std::string_view controlParent, std::string_view control);
// `<display> <size x y z> <distance>`: size and percentage (distance / 100)
void ApplyQuatInterpDisplay(s_quatinterpbone_t &helper, const Vector &size, float distance);
// `<trigger> <tolerance> <trigger x y z> <angles x y z> <position x y z>`, all angles in degrees. `basePosition` is the
// last `<basepos>` of the file, `rotateAxisDegrees` and `jointOrientDegrees` the last `<rotateaxis>` / `<jointorient>`;
// they apply to the triggers that follow them. At most 32 triggers: false when there is no room (the caller reports
// it).
bool ApplyQuatInterpTrigger(s_quatinterpbone_t &helper, const Vector &basePosition, const Vector &rotateAxisDegrees,
                            const Vector &jointOrientDegrees, float toleranceDegrees, const Vector &triggerDegrees,
                            const Vector &anglesDegrees, const Vector &position);

// -- aim constraints (.vrd `<aimconstraint>`) -------------------------------------------------------------------------

// `<aimconstraint> <bone> <parent> <aim>`: `aim` is an attachment or a bone, looked up when the model is simplified (the
// attachment first). The vectors follow with the three calls below.
s_aimatbone_t &ApplyAimAtBegin(StudioMdlState &ctx, std::string_view bone, std::string_view parent,
                               std::string_view aim);
void ApplyAimAtAimVector(s_aimatbone_t &aimAt, const Vector &vector);   // normalized
void ApplyAimAtUpVector(s_aimatbone_t &aimAt, const Vector &vector);    // normalized
void ApplyAimAtBasePosition(s_aimatbone_t &aimAt, const Vector &position);

// -- axis interpolation bones ($proceduralbones .jiggle-style file: `bone`, `axis`) ---------------------------------

// `bone "<bone>" "..." "<control>" "..." <axis>`: axis as written in the file, 1..3 (the engine's 0..2 is axis - 1)
s_axisinterpbone_t &ApplyAxisInterpBegin(StudioMdlState &ctx, std::string_view bone, std::string_view control, int axis);
// `axis <index 0..5> <position> <angles>`: position = basePosition + position; the file lists the angles as
// roll pitch yaw (angles.z, x, y), degrees: pass them as the QAngle (pitch, yaw, roll). False for an index outside 0..5.
bool ApplyAxisInterpAxis(s_axisinterpbone_t &interp, int index, const Vector &basePosition, const Vector &position,
                         const QAngle &angles);
