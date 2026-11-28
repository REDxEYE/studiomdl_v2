#pragma once

// $jigglebone rules of an MDL v49: reading them, changing a loaded model's copy in place, and rebuilding the file
// with its .vvd/.vtx/.phy checksums moved along.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

// mstudiojigglebone_t field for field (angles in radians, as stored); defaults are studiomdl's for a new
// is_flexible rule.
struct JiggleParams
{
    enum Flag : uint32_t
    {
        Flexible = 0x01, Rigid = 0x02, YawConstraint = 0x04, PitchConstraint = 0x08, AngleConstraint = 0x10,
        LengthConstraint = 0x20, BaseSpring = 0x40
    };
    uint32_t flags = Flexible | LengthConstraint;
    float length = 10, tipMass = 0;
    float yawStiffness = 100, yawDamping = 0, pitchStiffness = 100, pitchDamping = 0, alongStiffness = 100, alongDamping = 0;
    float angleLimit = 0;
    float minYaw = 0, maxYaw = 0, yawFriction = 0, yawBounce = 0;
    float minPitch = 0, maxPitch = 0, pitchFriction = 0, pitchBounce = 0;
    float baseMass = 0, baseStiffness = 100, baseDamping = 0;
    float baseMinLeft = -100, baseMaxLeft = 100, baseLeftFriction = 0;
    float baseMinUp = -100, baseMaxUp = 100, baseUpFriction = 0;
    float baseMinForward = -100, baseMaxForward = 100, baseForwardFriction = 0;
    bool operator==(const JiggleParams &) const = default;
};

// Bone index -> new rule; nullopt removes the bone's rule.
using JiggleEdits = std::map<int, std::optional<JiggleParams>>;

// What a bone's procedural slot holds.
enum class JiggleState { None, Jiggle, OtherProcedural };

// Per bone of the MDL `mdl` (an intact IDST v49 file): its state and jiggle rule. Throws std::runtime_error otherwise.
std::vector<JiggleState> ReadJiggleStates(const QByteArray &mdl);
std::vector<std::optional<JiggleParams>> ReadJiggleBones(const QByteArray &mdl);

// Where new rules go in an MDL buffer: freed slots first, then from `end` up to `capacity` (byte offsets).
struct JiggleStorage
{
    size_t end = 0, capacity = 0;
    std::vector<size_t> freed;
};
// Sets `bone`'s rule in the studiohdr_t at `mdl`: rewrites an existing rule in place, takes a slot from `storage` for a
// new one (procedural type, procedure offset and BONE_ALWAYS_PROCEDURAL, in the bone and the linear bone table) and
// returns a removed rule's slot to `storage`. False, with nothing changed, for a bone with another procedural rule.
// Throws std::runtime_error for a bone out of range or full storage.
bool ApplyJiggle(std::byte *mdl, JiggleStorage &storage, int bone, const std::optional<JiggleParams> &params);

// studiomdl's MDL checksum (write.cpp): a rotating word sum, with the checksum field read as the running sum. It is
// taken over the finished file, so it differs from compilers' values (they sum before filling in mesh ids and the
// length); the engine only requires the .mdl, .vvd, .vtx and .phy values to match.
int32_t MdlChecksum(const QByteArray &mdl);

// `mdl` with `edits` applied: removed rules' slots are zeroed and reused for new rules, the rest are appended, and
// the length and checksum fields are recomputed. Throws std::runtime_error like ApplyJiggle.
QByteArray RebuildMdlJiggles(QByteArray mdl, const JiggleEdits &edits);

// Rebuilds `mdlPath` and writes the new checksum into its .vvd and .dx90.vtx (both must match the old checksum) and
// into the other .vtx files and the .phy that match it; nothing is written when a step fails. Returns notes on
// files written and stale ones skipped. Throws std::runtime_error.
QStringList SaveMdlJiggles(const QString &mdlPath, const JiggleEdits &edits);
