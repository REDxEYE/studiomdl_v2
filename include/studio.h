//===== Copyright (c) 1996-2008, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#pragma once

#include <utility>
#include <array>

#include "bspflags.h"
#include "enum_utils.hpp"
#include "tier0/basetypes.h"
#include "mathlib/vector2d.h"
#include "mathlib/vector.h"
#include "mathlib/vector4d.h"
#include "mathlib/compressed_vector.h"


#include "mathlib/mathlib.h"
#include "tier1/utlvector.h"
#include "tier1/utlhash.h"
#include "tier1/utldict.h"
#include "tier1/utlsymbol.h"
#include "tier1/generichash.h"
#include "localflexcontroller.h"

// flex rule opcodes (mstudioflexop_t::op)
enum class StudioOpcode : uint32_t {
    Const = 1, // get float
    Fetch1 = 2, // get Flexcontroller value
    Fetch2 = 3, // get flex weight
    Add = 4,
    Sub = 5,
    Mul = 6,
    Div = 7,
    Neg = 8, // not implemented
    Exp = 9, // not implemented
    Open = 10, // only used in token parsing
    Close = 11,
    Comma = 12, // only used in token parsing
    Max = 13,
    Min = 14,
    TwoWay0 = 15, // Fetch a value from a 2 Way slider for the 1st value RemapVal( 0.0, 0.5, 0.0, 1.0 )
    TwoWay1 = 16, // Fetch a value from a 2 Way slider for the 2nd value RemapVal( 0.5, 1.0, 0.0, 1.0 )
    NWay = 17, // Fetch a value from a 2 Way slider for the 2nd value RemapVal( 0.5, 1.0, 0.0, 1.0 )
    Combo = 18, // Perform a combo operation (essentially multiply the last N values on the stack)
    Dominate = 19, // Performs a combination domination operation
    DmeLowerEyelid = 20, //
    DmeUpperEyelid = 21, //
};

// motion flags (bone controller types, movement and motion extraction axes)
enum class StudioMotionFlags : uint32_t {
    None = 0x00000000,
    X = 0x00000001,
    Y = 0x00000002,
    Z = 0x00000004,
    XR = 0x00000008,
    YR = 0x00000010,
    ZR = 0x00000020,

    LX = 0x00000040,
    LY = 0x00000080,
    LZ = 0x00000100,
    LXR = 0x00000200,
    LYR = 0x00000400,
    LZR = 0x00000800,

    Linear = 0x00001000,
    QuadraticMotion = 0x00002000,

    Types = 0x0003FFFF,
    RLoop = 0x00040000, // controller that wraps shortest distance
    Invalid = 0xFFFFFFFF, // lookup failure, never stored
};

ENUM_OPERATORS(StudioMotionFlags)

// sequence and animation flags
enum class StudioSequenceFlags : uint32_t {
    None = 0x0000,
    Looping = 0x0001, // ending frame should be the same as the starting frame
    Snap = 0x0002, // do not interpolate between previous animation and this one
    Delta = 0x0004, // this sequence "adds" to the base sequences, not slerp blends
    Autoplay = 0x0008, // temporary flag that forces the sequence to always play
    Post = 0x0010, //
    AllZeros = 0x0020, // this animation/sequence has no real animation data
    FrameAnim = 0x0040, // animation is encoded as by frame x bone instead of RLE bone x frame
    CyclePose = 0x0080, // cycle index is taken from a pose parameter index
    Realtime = 0x0100, // cycle index is taken from a real-time clock, not the animations cycle index
    Local = 0x0200, // sequence has a local context sequence
    Hidden = 0x0400, // don't show in default selection views
    Override = 0x0800, // a forward declared sequence (empty)
    Activity = 0x1000, // Has been updated at runtime to activity index
    Event = 0x2000, // Has been updated at runtime to event index on server
    World = 0x4000, // sequence blends in worldspace
    NoForceLoop = 0x8000, // do not force the animation loop
    EventClient = 0x10000, // Has been updated at runtime to event index on client
    WorldAndRelative = 0x20000, // do worldspace blend, then do normal blend on top
    RootXform = 0x40000, // sequence wants to derive a root re-xform from a given bone
};

ENUM_OPERATORS(StudioSequenceFlags)

// autolayer flags
enum class AutoLayerFlags : uint32_t {
    None = 0x0000,
    Post = 0x0010, //
    Spline = 0x0040, // convert layer ramp in/out curve is a spline instead of linear
    XFade = 0x0080,
    // pre-bias the ramp curve to compense for a non-1 weight, assuming a second layer is also going to accumulate
    NoBlend = 0x0200, // animation always blends at 1.0 (ignores weight)
    Local = 0x1000, // layer is a local context sequence
    Pose = 0x4000, // layer blends using a pose parameter instead of parent cycle
};

ENUM_OPERATORS(AutoLayerFlags)

#define STUDIO_ENABLE_PERF_COUNTERS

#define STUDIO_SEQUENCE_ACTIVITY_LOOKUPS_ARE_SLOW 0
//-----------------------------------------------------------------------------
// forward declarations
//-----------------------------------------------------------------------------

class IMaterial;
class IMesh;
class IMorph;
struct virtualmodel_t;
struct vertexFileHeader_t;
struct thinModelVertices_t;

namespace OptimizedModel {
    struct StripHeader_t;
}


/*
==============================================================================

STUDIO MODELS

Studio models are position independent, so the cache manager can move them.
==============================================================================
*/

#define STUDIO_VERSION		49

struct studiohdr_t;

#define MAXSTUDIOTRIANGLES	65536	// TODO: tune this
#define MAXSTUDIOVERTS		65536	// TODO: tune this
#define	MAXSTUDIOFLEXVERTS	(MAXSTUDIOVERTS - 1)	// max number of verts that can be flexed per model (the engine flex cache addresses 65535 verts)



#define MAXSTUDIOSKINS		128		// total textures (compiler side limit; SFM renders models with more than 32)
#define MAXSTUDIOBONES		256		// total bones actually used
#define MAXSTUDIOFLEXDESC	1024	// maximum number of low level flexes (actual morph targets)
#define MAXSTUDIOFLEXCTRL	128		// maximum number of flexcontrollers (input sliders)
#define MAXSTUDIOPOSEPARAM	24
#define MAXSTUDIOBONECTRLS	4
#define MAXSTUDIOANIMBLOCKS 256

#define MAXSTUDIOBONEBITS	8		// NOTE: MUST MATCH MAXSTUDIOBONES

// NOTE!!! : Changing this number also changes the vtx file format!!!!!
#define MAX_NUM_BONES_PER_VERT 3

// mstudioevent_t::type
enum class StudioEventType : int {
    OldStyle = 0,
    NewStyle = 1 << 10, //Adrian - Remove this when we completely phase out the old event system.
};

struct mstudiodata_t {
    int count;
    int offset;
};

enum class ProcBoneType:uint32_t {
    AxisInterp = 1,
    QuatInterp = 2,
    AimAtBone = 3,
    AimAtAttach = 4,
    Jiggle = 5,
    TwistMaster = 6,
    TwistSLAVE = 7,
    // Multiple twist bones are computed at once for the same parent/child combo so TWIST_NULL do nothing
    PointConstraint = 8,
    OrientConstraint = 9,
    AimConstraint = 10,
    IKConstraint = 11,
    ParentConstraint = 12,
    // 13 is reserved for legacy Dota cloth
    SoftBody = 16
};

// If you want to embed a pointer into one of the structures that is serialized, use this class! It will ensure that the pointers consume the
// right amount of space and work correctly across 32 and 64 bit. It also makes sure that there is no surprise about how large the structure
// is when placed in the middle of another structure, and supports Intel's desired behavior on 64-bit that pointers are always 8-byte aligned.
#pragma pack( push, 4 )
template<class T>
struct ALIGN4 serializedstudioptr_t {
    T *m_pData;
#ifndef PLATFORM_64BITS
    int32_t padding;
#endif

    serializedstudioptr_t() {
        m_pData = nullptr;
#if _DEBUG && !defined( PLATFORM_64BITS )
        padding = 0;
#endif
    }

    operator T *() { return m_pData; }
    operator const T *() const { return m_pData; }

    T *operator->() { return m_pData; }
    const T *operator->() const { return m_pData; }

    T *operator=(T *ptr) { return m_pData = ptr; }
}
        ALIGN4_POST;

#pragma pack( pop )

struct mstudioaxisinterpbone_t {
    int control; // local transformation of this bone used to calc 3 point blend
    int axis; // axis to check
    std::array<Vector, 6> pos; // X+, X-, Y+, Y-, Z+, Z-
    std::array<Quaternion, 6> quat; // X+, X-, Y+, Y-, Z+, Z-

    // No copy constructors allowed
    mstudioaxisinterpbone_t(const mstudioaxisinterpbone_t &vOther) = delete;
};


struct mstudioquatinterpinfo_t {
    float inv_tolerance; // 1 / radian angle of trigger influence
    Quaternion trigger; // angle to match
    Vector pos; // new position
    Quaternion quat; // new angle

    // No copy constructors allowed
    mstudioquatinterpinfo_t(const mstudioquatinterpinfo_t &vOther) = delete;
};

struct mstudioquatinterpbone_t {
    int control; // local transformation to check
    int numtriggers;
    int triggerindex;

    mstudioquatinterpinfo_t *pTrigger(int i) const {
        return (mstudioquatinterpinfo_t *) (((uint8_t *) this) + triggerindex) + i;
    };

    // No copy constructors allowed
    mstudioquatinterpbone_t(const mstudioquatinterpbone_t &vOther) = delete;
};

enum class JiggleBoneFlags:uint32_t {
    None = 0x0,
    IsFlexible = 0x01,
    IsRigid = 0x02,
    HasYawConstraint = 0x04,
    HasPitchConstraint = 0x08,
    HasAngleConstraint = 0x10,
    HasLengthConstraint = 0x20,
    HasBaseSpring = 0x40,
};

ENUM_OPERATORS(JiggleBoneFlags)

struct mstudiojigglebone_t {
    JiggleBoneFlags flags;

    // general params
    float length; // how from from bone base, along bone, is tip
    float tipMass;

    // flexible params
    float yawStiffness;
    float yawDamping;
    float pitchStiffness;
    float pitchDamping;
    float alongStiffness;
    float alongDamping;

    // angle constraint
    float angleLimit; // maximum deflection of tip in radians

    // yaw constraint
    float minYaw; // in radians
    float maxYaw; // in radians
    float yawFriction;
    float yawBounce;

    // pitch constraint
    float minPitch; // in radians
    float maxPitch; // in radians
    float pitchFriction;
    float pitchBounce;

    // base spring
    float baseMass;
    float baseStiffness;
    float baseDamping;
    float baseMinLeft;
    float baseMaxLeft;
    float baseLeftFriction;
    float baseMinUp;
    float baseMaxUp;
    float baseUpFriction;
    float baseMinForward;
    float baseMaxForward;
    float baseForwardFriction;

private:
    // No copy constructors allowed
    //mstudiojigglebone_t(const mstudiojigglebone_t& vOther);
};

struct mstudioaimatbone_t {
    int parent;
    int aim; // Might be bone or attach
    Vector aimvector;
    Vector upvector;
    Vector basepos;

    // No copy constructors allowed
    mstudioaimatbone_t(const mstudioaimatbone_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct mstudiotwistbonetarget_t {
    int m_nBone;
    float m_flWeight;
    Vector m_vBaseTranslate;
    Quaternion m_qBaseRotation;

    // No copy constructors allowed
    mstudiotwistbonetarget_t(const mstudiotwistbonetarget_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct mstudiotwistbone_t {
    bool m_bInverse; // False: Apply child rotation to twist targets True: Apply parent rotation to twist targets
    Vector m_vUpVector; // In parent space, projected into plane defined by vector between parent & child
    int m_nParentBone;
    Quaternion m_qBaseInv; // The base rotation of the parent, used if m_bInverse is true
    int m_nChildBone;

    int m_nTargetCount;
    int m_nTargetIndex;

    mstudiotwistbonetarget_t *pTarget(int i) const {
        return (mstudiotwistbonetarget_t *) (((uint8_t *) this) + m_nTargetIndex) + i;
    }

    // No copy constructors allowed
    mstudiotwistbone_t(const mstudiotwistbone_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct mstudioconstraintslave_t {
    int m_nBone;
    Vector m_vBasePosition;
    Quaternion m_qBaseOrientation;

    // No copy constructors allowed
    mstudioconstraintslave_t(const mstudioconstraintslave_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct mstudioconstrainttarget_t {
    int m_nBone;
    float m_flWeight;
    Vector m_vOffset;
    Quaternion m_qOffset;

    // No copy constructors allowed
    mstudioconstrainttarget_t(const mstudioconstrainttarget_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
// Point constraint, slave position matches target position
//-----------------------------------------------------------------------------
struct mstudiopointconstraint_t {
    mstudioconstraintslave_t m_slave; // DEFINE_EMBEDDED

    int m_nTargetCount;
    int m_nTargetIndex;

    mstudioconstrainttarget_t *pTarget(int i) const {
        return (mstudioconstrainttarget_t *) (((uint8_t *) this) + m_nTargetIndex) + i;
    }

    mstudiopointconstraint_t(const mstudiopointconstraint_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
// Orient constraint, slave orientation matches target orientation
//-----------------------------------------------------------------------------
struct mstudioorientconstraint_t {
    mstudioconstraintslave_t m_slave; // DEFINE_EMBEDDED

    int m_nTargetCount;
    int m_nTargetIndex;

    mstudioconstrainttarget_t *pTarget(int i) const {
        return (mstudioconstrainttarget_t *) (((uint8_t *) this) + m_nTargetIndex) + i;
    }

    mstudioorientconstraint_t(const mstudioorientconstraint_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct mstudioaimconstraint_t {
    mstudioconstraintslave_t m_slave; // DEFINE_EMBEDDED

    int m_nTargetCount;
    int m_nTargetIndex;

    mstudioconstrainttarget_t *pTarget(int i) const {
        return (mstudioconstrainttarget_t *) (((uint8_t *) this) + m_nTargetIndex) + i;
    }

    Quaternion m_qAimOffset;
    Vector m_vUp;
    short m_nUpSpaceTarget;
    uint8_t m_nUpType;
    uint8_t m_unused;

    // No copy constructors allowed
    mstudioaimconstraint_t(const mstudioaimconstraint_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct mstudioikconstraint_t {
    mstudioikconstraint_t() {
    }

private:
    // No copy constructors allowed
    mstudioikconstraint_t(const mstudioaimconstraint_t &vOther);
};


//-----------------------------------------------------------------------------
// Parent constraint, slave position and orientation are updated to behave as children of the target
//-----------------------------------------------------------------------------
struct mstudioparentconstraint_t {
    mstudioconstraintslave_t m_slave; // DEFINE_EMBEDDED

    int m_nTargetCount;
    int m_nTargetIndex;

    mstudioconstrainttarget_t *pTarget(int i) const {
        return (mstudioconstrainttarget_t *) (((uint8_t *) this) + m_nTargetIndex) + i;
    }

    mstudioparentconstraint_t(const mstudioparentconstraint_t &vOther) = delete;
};


// bone flags (mstudiobone_t::flags, mstudiolinearbone_t flags, bone masks)
enum class BoneFlags : uint32_t {
    None = 0,

    CalculateMask = 0x1F,
    PhysicallySimulated = 0x01, // bone is physically simulated when physics are active
    PhysicsProcedural = 0x02, // procedural when physics is active
    AlwaysProcedural = 0x04, // bone is always procedurally animated
    ScreenAlignSphere = 0x08, // bone aligns to the screen, not constrained in motion.
    ScreenAlignCylinder = 0x10, // bone aligns to the screen, constrained by it's own axis.
    WorldAlign = 0x20, // bone is rigidly aligned to the world (but can still translate)

    UsedMask = 0x000FFF00,
    UsedByAnything = 0x000FFF00,
    UsedByHitbox = 0x00000100, // bone (or child) is used by a hit box
    UsedByAttachment = 0x00000200, // bone (or child) is used by an attachment point
    UsedByVertexMask = 0x0003FC00,
    UsedByVertexLod0 = 0x00000400, // bone (or child) is used by the toplevel model via skinned vertex
    UsedByVertexLod1 = 0x00000800,
    UsedByVertexLod2 = 0x00001000,
    UsedByVertexLod3 = 0x00002000,
    UsedByVertexLod4 = 0x00004000,
    UsedByVertexLod5 = 0x00008000,
    UsedByVertexLod6 = 0x00010000,
    UsedByVertexLod7 = 0x00020000,
    UsedByBoneMerge = 0x00040000, // bone is available for bone merge to occur against it
    AlwaysSetup = 0x00080000,

    TypeMask = 0x00F00000,
    FixedAlignment = 0x00100000,
    // bone can't spin 360 degrees, all interpolation is normalized around a fixed orientation

    HasSaveframePos = 0x00200000, // Vector48
    HasSaveframeRot64 = 0x00400000, // Quaternion64
    HasSaveframeRot32 = 0x00800000, // Quaternion32
};

ENUM_OPERATORS(BoneFlags)

// UsedByVertexLod<lod>
constexpr BoneFlags BoneUsedByVertexAtLod(const uint32_t lod) {
    return static_cast<BoneFlags>(std::to_underlying(BoneFlags::UsedByVertexLod0) << lod);
}

// bones
struct mstudiobone_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    int parent; // parent bone
    int bonecontroller[6]; // bone controller index, -1 == none

    // default values
    Vector pos;
    Quaternion quat;
    RadianEuler rot;
    // compression scale
    Vector posscale;
    Vector rotscale;

    matrix3x4_t poseToBone;
    Quaternion qAlignment;
    BoneFlags flags;
    ProcBoneType proctype;
    int procindex; // procedural rule
    mutable int physicsbone; // index into physically simulated bone
    void *pProcedure() const {
        if (procindex == 0) return nullptr;
        else return (void *) (((uint8_t *) this) + procindex);
    };
    int surfacepropidx; // index into string tablefor property name
    char *const pszSurfaceProp() const { return ((char *) this) + surfacepropidx; }
    int GetSurfaceProp() const { return surfacepropLookup; }

    ContentType contents; // See BSPFlags.h for the contents flags
    int surfacepropLookup; // this index must be cached by the loader, not saved in the file
    int unused[7]; // remove as appropriate

    mstudiobone_t(const mstudiobone_t &vOther) = delete;
};

struct mstudiolinearbone_t {
    int numbones;

    int flagsindex;

    BoneFlags flags(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((BoneFlags *) (((uint8_t *) this) + flagsindex) + i);
    };

    BoneFlags *pflags(int i) {
        Assert(i >= 0 && i < numbones);
        return ((BoneFlags *) (((uint8_t *) this) + flagsindex) + i);
    };

    int parentindex;

    int parent(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((int *) (((uint8_t *) this) + parentindex) + i);
    };

    int posindex;

    const Vector &pos(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((Vector *) (((uint8_t *) this) + posindex) + i);
    };

    int quatindex;

    const Quaternion &quat(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((Quaternion *) (((uint8_t *) this) + quatindex) + i);
    };

    int rotindex;

    const RadianEuler &rot(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((RadianEuler *) (((uint8_t *) this) + rotindex) + i);
    };

    int posetoboneindex;

    const matrix3x4_t &poseToBone(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((matrix3x4_t *) (((uint8_t *) this) + posetoboneindex) + i);
    };

    int posscaleindex;

    const Vector &posscale(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((Vector *) (((uint8_t *) this) + posscaleindex) + i);
    };

    int rotscaleindex;

    const Vector &rotscale(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((Vector *) (((uint8_t *) this) + rotscaleindex) + i);
    };

    int qalignmentindex;

    const Quaternion &qalignment(int i) const {
        Assert(i >= 0 && i < numbones);
        return *((Quaternion *) (((uint8_t *) this) + qalignmentindex) + i);
    };

    int unused[6];

    mstudiolinearbone_t(const mstudiolinearbone_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
// The component of the bone used by mstudioboneflexdriver_t
//-----------------------------------------------------------------------------
enum StudioBoneFlexComponent_t {
    STUDIO_BONE_FLEX_INVALID = -1, // Invalid
    STUDIO_BONE_FLEX_TX = 0, // Translate X
    STUDIO_BONE_FLEX_TY = 1, // Translate Y
    STUDIO_BONE_FLEX_TZ = 2 // Translate Z
};


//-----------------------------------------------------------------------------
// Component is one of Translate X, Y or Z [0,2] (StudioBoneFlexComponent_t)
//-----------------------------------------------------------------------------
struct mstudioboneflexdrivercontrol_t {
    int m_nBoneComponent; // Bone component that drives flex, StudioBoneFlexComponent_t
    int m_nFlexControllerIndex; // Flex controller to drive
    float m_flMin; // Min value of bone component mapped to 0 on flex controller
    float m_flMax; // Max value of bone component mapped to 1 on flex controller

    mstudioboneflexdrivercontrol_t(const mstudioboneflexdrivercontrol_t &vOther) = delete;
};


//-----------------------------------------------------------------------------
// Drive flex controllers from bone components
//-----------------------------------------------------------------------------
struct mstudioboneflexdriver_t {
    int m_nBoneIndex; // Bone to drive flex controller
    int m_nControlCount; // Number of flex controllers being driven
    int m_nControlIndex; // Index into data where controllers are (relative to this)

    mstudioboneflexdrivercontrol_t *pBoneFlexDriverControl(int i) const {
        Assert(i >= 0 && i < m_nControlCount);
        return (mstudioboneflexdrivercontrol_t *) (((uint8_t *) this) + m_nControlIndex) + i;
    }

    int unused[3];

    mstudioboneflexdriver_t(const mstudioboneflexdriver_t &vOther) = delete;
};


#define MAX_NUM_LODS 8

// bone controllers
struct mstudiobonecontroller_t {
    int bone; // -1 == 0
    StudioMotionFlags type; // X, Y, Z, XR, YR, ZR, M
    float start;
    float end;
    int rest; // byte index value at rest
    int inputfield; // 0-3 user set controller, 4 mouth
    int unused[8];
};

// intersection boxes
struct mstudiobbox_t {
    int bone;
    int group; // intersection group
    Vector bbmin; // bounding box, or the ends of the capsule if flCapsuleRadius > 0
    Vector bbmax;
    int szhitboxnameindex; // offset to the name of the hitbox.
    QAngle angOffsetOrientation;
    float flCapsuleRadius;
    int32_t unused[4];

    const char *pszHitboxName() const {
        if (szhitboxnameindex == 0)
            return "";

        return ((const char *) this) + szhitboxnameindex;
    }

    mstudiobbox_t(const mstudiobbox_t &vOther) = delete;
};

// demand loaded sequence groups
struct mstudiomodelgroup_t {
    int szlabelindex; // textual name
    char *const pszLabel() const { return ((char *) this) + szlabelindex; }
    int sznameindex; // file name
    char *const pszName() const { return ((char *) this) + sznameindex; }
};

struct mstudiomodelgrouplookup_t {
    int modelgroup;
    int indexwithingroup;
};

// animtags
struct mstudioanimtag_t {
    int tag;
    float cycle;

    int sztagindex;
    char *const pszTagName() const { return ((char *) this) + sztagindex; }
};

// events
// NOTE: If you modify this struct you MUST also modify mstudioevent_for_client_server_t in npcevent.h!!!
struct mstudioevent_t {
    float cycle;
    int event;
    StudioEventType type;
    const char *pszOptions() const { return options; }
    char options[64];

    int szeventindex;
    char *const pszEventName() const { return ((char *) this) + szeventindex; }
};

// mstudioattachment_t::flags
enum class AttachmentFlags : uint32_t {
    None = 0,
    WorldAlign = 0x10000,
};

ENUM_OPERATORS(AttachmentFlags)

// attachment
struct mstudioattachment_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    AttachmentFlags flags;
    int localbone;
    matrix3x4_t local; // attachment point
    int unused[8];
};

// mstudioikrule_t::type
enum class IkRuleType : int {
    None = 0,
    Self = 1,
    World = 2,
    Ground = 3,
    Release = 4,
    Attachment = 5,
    Unlatch = 6,
};

struct mstudioikerror_t {
    Vector pos;
    Quaternion q;

    mstudioikerror_t(const mstudioikerror_t &vOther) = delete;
};

union mstudioanimvalue_t;

struct mstudiocompressedikerror_t {
    float scale[6];
    short offset[6];

    mstudioanimvalue_t *pAnimvalue(int i) const {
        if (offset[i] > 0) return (mstudioanimvalue_t *) (((uint8_t *) this) + offset[i]);
        else return nullptr;
    };

    mstudiocompressedikerror_t(const mstudiocompressedikerror_t &vOther) = delete;
};

struct mstudioikrule_t {
    int index;

    IkRuleType type;
    int chain;

    int bone;

    int slot; // iktarget slot.  Usually same as chain.
    float height;
    float radius;
    float floor;
    Vector pos;
    Quaternion q;

    int compressedikerrorindex;

    mstudiocompressedikerror_t *pCompressedError() const {
        return (mstudiocompressedikerror_t *) (((uint8_t *) this) + compressedikerrorindex);
    };
    int unused2;

    int iStart;
    int ikerrorindex;

    mstudioikerror_t *pError(int i) const {
        return (ikerrorindex) ? (mstudioikerror_t *) (((uint8_t *) this) + ikerrorindex) + (i - iStart) : nullptr;
    };

    float start; // beginning of influence
    float peak; // start of full influence
    float tail; // end of full influence
    float end; // end of all influence

    float unused3; //
    float contact; // frame footstep makes ground concact
    float drop; // how far down the foot should drop when reaching for IK
    float top; // top of the foot box

    int unused6;
    int unused7;
    int unused8;

    int szattachmentindex; // name of world attachment
    char *const pszAttachment() const { return ((char *) this) + szattachmentindex; }

    int unused[7];

    mstudioikrule_t(const mstudioikrule_t &vOther) = delete;
};


struct mstudioikrulezeroframe_t {
    short chain;
    short slot;
    float16 start; // beginning of influence
    float16 peak; // start of full influence
    float16 tail; // end of full influence
    float16 end; // end of all influence
};


struct mstudioiklock_t {
    int chain;
    float flPosWeight;
    float flLocalQWeight;
    int flags;

    int unused[4];
};


struct mstudiolocalhierarchy_t {
    int iBone; // bone being adjusted
    int iNewParent; // the bones new parent

    float start; // beginning of influence
    float peak; // start of full influence
    float tail; // end of full influence
    float end; // end of all influence

    int iStart; // first frame

    int localanimindex;

    mstudiocompressedikerror_t *pLocalAnim() const {
        return (mstudiocompressedikerror_t *) (((uint8_t *) this) + localanimindex);
    };

    int unused[4];
};


// animation frames
union mstudioanimvalue_t {
    struct {
        uint8_t valid;
        uint8_t total;
    } num;

    short value;
};

struct mstudioanim_valueptr_t {
    short offset[3];

    mstudioanimvalue_t *pAnimvalue(int i) const {
        if (offset[i] > 0) return (mstudioanimvalue_t *) (((uint8_t *) this) + offset[i]);
        else return nullptr;
    };
};

// mstudio_rle_anim_t::flags
enum class StudioAnimFlags : uint8_t {
    None = 0x00,
    RawPos = 0x01, // Vector48
    RawRot = 0x02, // Quaternion48
    AnimPos = 0x04, // mstudioanim_valueptr_t
    AnimRot = 0x08, // mstudioanim_valueptr_t
    Delta = 0x10,
    RawRot2 = 0x20, // Quaternion64
};

ENUM_OPERATORS(StudioAnimFlags)

// per bone per animation DOF and weight pointers, RLE encoded
struct mstudio_rle_anim_t {
    uint8_t bone;
    StudioAnimFlags flags; // weighing options

    // valid for animating data only
    uint8_t *pData() const { return (((uint8_t *) this) + sizeof(struct mstudio_rle_anim_t)); };
    mstudioanim_valueptr_t *pRotV() const { return (mstudioanim_valueptr_t *) (pData()); };

    mstudioanim_valueptr_t *pPosV() const {
        return (mstudioanim_valueptr_t *) (pData()) + HasFlag(flags, StudioAnimFlags::AnimRot);
    };

    // valid if animation unvaring over timeline
    Quaternion48 *pQuat48() const { return (Quaternion48 *) (pData()); };
    Quaternion64 *pQuat64() const { return (Quaternion64 *) (pData()); };

    Vector48 *pPos() const {
        return (Vector48 *) (pData() + HasFlag(flags, StudioAnimFlags::RawRot) * sizeof(*pQuat48()) +
                             HasFlag(flags, StudioAnimFlags::RawRot2) * sizeof(*pQuat64()));
    };

    // points to next bone in the list
    short nextoffset;

    mstudio_rle_anim_t *pNext() const {
        if (nextoffset != 0) return (mstudio_rle_anim_t *) (((uint8_t *) this) + nextoffset);
        else return nullptr;
    };
};


// per bone flags of mstudio_frame_anim_t (pBoneFlags)
enum class StudioFrameFlags : uint8_t {
    None = 0x00,
    ConstPos = 0x01, // Vector48 in constants
    ConstRot = 0x02, // Quaternion48 in constants
    AnimPos = 0x04, // Vector48 in framedata
    AnimRot = 0x08, // Quaternion48 in framedata
    AnimPos2 = 0x10, // Vector in framedata
    ConstPos2 = 0x20, // Vector in constants
    ConstRot2 = 0x40, // Quaternion48S in constants
    AnimRot2 = 0x80, // Quaternion48S in framedata
};

ENUM_OPERATORS(StudioFrameFlags)

struct mstudio_frame_anim_t {
    StudioFrameFlags *pBoneFlags() const {
        return (StudioFrameFlags *) (((uint8_t *) this) + sizeof(struct mstudio_frame_anim_t));
    };

    int constantsoffset;
    uint8_t *pConstantData() const { return (((uint8_t *) this) + constantsoffset); };

    int frameoffset;
    int framelength;
    uint8_t *pFrameData(int iFrame) const { return (((uint8_t *) this) + frameoffset + iFrame * framelength); };

    int unused[3];
};


struct mstudiomovement_t {
    int endframe;
    StudioMotionFlags motionflags;
    float v0; // velocity at start of block
    float v1; // velocity at end of block
    float angle; // YAW rotation at end of this blocks movement
    Vector vector; // movement vector relative to this blocks initial angle
    Vector position; // relative to start of animation???

    mstudiomovement_t(const mstudiomovement_t &vOther) = delete;
};

// used for piecewise loading of animation data
struct mstudioanimblock_t {
    int datastart;
    int dataend;
};

struct mstudioanimsections_t {
    int animblock;
    int animindex;
};

struct mstudioanimdesc_t {
    int baseptr;
    studiohdr_t *pStudiohdr() const { return (studiohdr_t *) (((uint8_t *) this) + baseptr); }

    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }

    float fps; // frames per second
    StudioSequenceFlags flags; // looping/non-looping flags

    int numframes;

    // piecewise movement
    int nummovements;
    int movementindex;

    mstudiomovement_t *const pMovement(int i) const {
        return (mstudiomovement_t *) (((uint8_t *) this) + movementindex) + i;
    };

    int ikrulezeroframeindex;

    mstudioikrulezeroframe_t *pIKRuleZeroFrame(int i) const {
        if (ikrulezeroframeindex) return (mstudioikrulezeroframe_t *) (((uint8_t *) this) + ikrulezeroframeindex) + i;
        else return nullptr;
    };

    int unused1[5]; // remove as appropriate (and zero if loading older versions)

    int animblock;
    int animindex; // non-zero when anim data isn't in sections
    uint8_t *pAnimBlock(int block, int index, bool preloadIfMissing = true) const;

    // returns pointer to a specific anim block (local or external)
    bool hasAnimBlockBeenPreloaded(int block) const;

    uint8_t *pAnim(int *piFrame, float &flStall) const; // returns pointer to data and new frame index
    uint8_t *pAnim(int *piFrame) const; // returns pointer to data and new frame index

    int numikrules;
    int ikruleindex; // non-zero when IK rule is stored in the mdl
    int animblockikruleindex; // non-zero when IK data is stored in animblock file
    mstudioikrule_t *pIKRule(int i) const;

    int numlocalhierarchy;
    int localhierarchyindex;

    mstudiolocalhierarchy_t *pHierarchy(int i) const;

    int sectionindex;
    int sectionframes; // number of frames used in each fast lookup section, zero if not used
    mstudioanimsections_t *const pSection(int i) const {
        return (mstudioanimsections_t *) (((uint8_t *) this) + sectionindex) + i;
    }

    short zeroframespan; // frames per span
    short zeroframecount; // number of spans
    int zeroframeindex;

    uint8_t *pZeroFrameData() const {
        if (zeroframeindex) return (((uint8_t *) this) + zeroframeindex);
        else return nullptr;
    };
    mutable float zeroframestalltime; // saved during read stalls

    mstudioanimdesc_t(const mstudioanimdesc_t &vOther) = delete;
};

struct mstudioikrule_t;

struct mstudioautolayer_t {
    //private:
    short iSequence;
    short iPose;
    //public:
    AutoLayerFlags flags;
    float start; // beginning of influence
    float peak; // start of full influence
    float tail; // end of full influence
    float end; // end of all influence
};

struct mstudioactivitymodifier_t {
    int sznameindex;
    char *pszName() { return (sznameindex) ? (char *) (((uint8_t *) this) + sznameindex) : nullptr; }
};

// sequence descriptions
struct mstudioseqdesc_t {
    int baseptr;
    studiohdr_t *pStudiohdr() const { return (studiohdr_t *) (((uint8_t *) this) + baseptr); }

    int szlabelindex;
    char *const pszLabel() const { return ((char *) this) + szlabelindex; }

    int szactivitynameindex;
    char *const pszActivityName() const { return ((char *) this) + szactivitynameindex; }

    StudioSequenceFlags flags; // looping/non-looping flags

    int activity; // initialized at loadtime to game DLL values
    int actweight;

    int numevents;
    int eventindex;

    mstudioevent_t *pEvent(int i) const {
        Assert(i >= 0 && i < numevents);
        return (mstudioevent_t *) (((uint8_t *) this) + eventindex) + i;
    };

    Vector bbmin; // per sequence bounding box
    Vector bbmax;

    int numblends;

    // Index into array of shorts which is groupsize[0] x groupsize[1] in length
    int animindexindex;

    int anim(int x, int y) const {
        if (x >= groupsize[0]) {
            x = groupsize[0] - 1;
        }

        if (y >= groupsize[1]) {
            y = groupsize[1] - 1;
        }

        int offset = y * groupsize[0] + x;
        short *blends = (short *) (((uint8_t *) this) + animindexindex);
        int value = (int) blends[offset];
        return value;
    }

    int movementindex; // [blend] float array for blended movement
    int groupsize[2];
    int paramindex[2]; // X, Y, Z, XR, YR, ZR
    float paramstart[2]; // local (0..1) starting value
    float paramend[2]; // local (0..1) ending value
    int paramparent;

    float fadeintime; // ideal cross fate in time (0.2 default)
    float fadeouttime; // ideal cross fade out time (0.2 default)

    int localentrynode; // transition node at entry
    int localexitnode; // transition node at exit
    int nodeflags; // transition rules

    float entryphase; // used to match entry gait
    float exitphase; // used to match exit gait

    float lastframe; // frame that should generation EndOfSequence

    int nextseq; // auto advancing sequences
    int pose; // index of delta animation between end and nextseq

    int numikrules;

    int numautolayers; //
    int autolayerindex;

    mstudioautolayer_t *pAutolayer(int i) const {
        Assert(i >= 0 && i < numautolayers);
        return (mstudioautolayer_t *) (((uint8_t *) this) + autolayerindex) + i;
    };

    int weightlistindex;
    float *pBoneweight(int i) const { return ((float *) (((uint8_t *) this) + weightlistindex) + i); };
    float weight(int i) const { return *(pBoneweight(i)); };

    // FIXME: make this 2D instead of 2x1D arrays
    int posekeyindex;

    float *pPoseKey(int iParam, int iAnim) const {
        return (float *) (((uint8_t *) this) + posekeyindex) + iParam * groupsize[0] + iAnim;
    }

    float poseKey(int iParam, int iAnim) const { return *(pPoseKey(iParam, iAnim)); }

    int numiklocks;
    int iklockindex;

    mstudioiklock_t *pIKLock(int i) const {
        Assert(i >= 0 && i < numiklocks);
        return (mstudioiklock_t *) (((uint8_t *) this) + iklockindex) + i;
    };

    // Key values
    int keyvalueindex;
    int keyvaluesize;

    const char *KeyValueText() const {
        return keyvaluesize != 0 ? ((char *) this) + keyvalueindex : nullptr;
    }

    int cycleposeindex; // index of pose parameter to use as cycle index

    int activitymodifierindex;
    int numactivitymodifiers;

    mstudioactivitymodifier_t *pActivityModifier(int i) const {
        Assert(i >= 0 && i < numactivitymodifiers);
        return activitymodifierindex != 0
                   ? (mstudioactivitymodifier_t *) (((uint8_t *) this) + activitymodifierindex) + i
                   : nullptr;
    };

    int animtagindex;
    int numanimtags;

    mstudioanimtag_t *pAnimTag(int i) const {
        Assert(i >= 0 && i < numanimtags);
        return (mstudioanimtag_t *) (((uint8_t *) this) + animtagindex) + i;
    };

    int rootDriverIndex;

    int unused[2]; // remove/add as appropriate (grow back to 8 ints on version change!)

    mstudioseqdesc_t(const mstudioseqdesc_t &vOther) = delete;
};


struct mstudioposeparamdesc_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    StudioSequenceFlags flags; // Looping or None
    float start; // starting value
    float end; // ending value
    float loop; // looping range, 0 for no looping, 360 for rotations, etc.
};

struct mstudioflexdesc_t {
    int szFACSindex;
    char *const pszFACS() const { return ((char *) this) + szFACSindex; }
};


struct mstudioflexcontroller_t {
    int sztypeindex;
    char *const pszType() const { return ((char *) this) + sztypeindex; }
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    mutable int localToGlobal; // remapped at load time to master list
    float min;
    float max;
};


enum FlexControllerRemapType_t {
    Passthru = 0,
    _2way, // Control 0 -> ramps from 1-0 from 0->0.5. Control 1 -> ramps from 0-1 from 0.5->1
    Nway,
    // StepSize = 1 / (control count-1) Control n -> ramps from 0-1-0 from (n-1)*StepSize to n*StepSize to (n+1)*StepSize. A second control is needed to specify amount to use
    Eyelid
};


class CStudioHdr;

struct mstudioflexcontrollerui_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }

    // These are used like a union to save space
    // Here are the possible configurations for a UI controller
    //
    // SIMPLE NON-STEREO:	0: control	1: unused	2: unused
    // STEREO:				0: left		1: right	2: unused
    // NWAY NON-STEREO:		0: control	1: unused	2: value
    // NWAY STEREO:			0: left		1: right	2: value

    int szindex0;
    int szindex1;
    int szindex2;

    const mstudioflexcontroller_t *pController() const {
        return !stereo ? (mstudioflexcontroller_t *) ((char *) this + szindex0) : nullptr;
    }

    char *const pszControllerName() const { return !stereo ? pController()->pszName() : nullptr; }

    inline int controllerIndex(const CStudioHdr &cStudioHdr) const;

    const mstudioflexcontroller_t *pLeftController() const {
        return stereo ? (mstudioflexcontroller_t *) ((char *) this + szindex0) : nullptr;
    }

    char *const pszLeftName() const { return stereo ? pLeftController()->pszName() : nullptr; }

    inline int leftIndex(const CStudioHdr &cStudioHdr) const;

    const mstudioflexcontroller_t *pRightController() const {
        return stereo ? (mstudioflexcontroller_t *) ((char *) this + szindex1) : nullptr;
    }

    char *const pszRightName() const { return stereo ? pRightController()->pszName() : nullptr; }

    inline int rightIndex(const CStudioHdr &cStudioHdr) const;

    const mstudioflexcontroller_t *pNWayValueController() const {
        return remaptype == Nway
                   ? (mstudioflexcontroller_t *) ((char *) this + szindex2)
                   : nullptr;
    }

    char *const pszNWayValueName() const {
        return remaptype == Nway ? pNWayValueController()->pszName() : nullptr;
    }

    inline int nWayValueIndex(const CStudioHdr &cStudioHdr) const;

    // Number of controllers this ui description contains, 1, 2 or 3
    int Count() const { return (stereo ? 2 : 1) + (remaptype == Nway ? 1 : 0); }

    inline const mstudioflexcontroller_t *pController(int index) const;

    uint8_t remaptype; // See the FlexControllerRemapType_t enum
    bool stereo; // Is this a stereo control?
    uint8_t unused[2];
};


// this is the memory image of vertex anims (16-bit fixed point)
struct mstudiovertanim_t {
    unsigned short index;
    uint8_t speed; // 255/max_length_in_flex
    uint8_t side; // 255/left_right

protected:
    union {
        short delta[3];
        float16 flDelta[3];
    };

    union {
        short ndelta[3];
        float16 flNDelta[3];
    };

public:
    void ConvertToFixed(float flVertAnimFixedPointScale) {
        delta[0] = (short) (flDelta[0].GetFloat() / flVertAnimFixedPointScale);
        delta[1] = (short) (flDelta[1].GetFloat() / flVertAnimFixedPointScale);
        delta[2] = (short) (flDelta[2].GetFloat() / flVertAnimFixedPointScale);
        ndelta[0] = (short) (flNDelta[0].GetFloat() / flVertAnimFixedPointScale);
        ndelta[1] = (short) (flNDelta[1].GetFloat() / flVertAnimFixedPointScale);
        ndelta[2] = (short) (flNDelta[2].GetFloat() / flVertAnimFixedPointScale);
    }

    Vector GetDeltaFixed(float flVertAnimFixedPointScale) {
        return Vector(delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale,
                      delta[2] * flVertAnimFixedPointScale);
    }

    Vector GetNDeltaFixed(float flVertAnimFixedPointScale) {
        return Vector(ndelta[0] * flVertAnimFixedPointScale, ndelta[1] * flVertAnimFixedPointScale,
                      ndelta[2] * flVertAnimFixedPointScale);
    }

    void GetDeltaFixed4DAligned(Vector4DAligned *vFillIn, float flVertAnimFixedPointScale) {
        vFillIn->Set(delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale,
                     delta[2] * flVertAnimFixedPointScale, 0.0f);
    }

    void GetNDeltaFixed4DAligned(Vector4DAligned *vFillIn, float flVertAnimFixedPointScale) {
        vFillIn->Set(ndelta[0] * flVertAnimFixedPointScale, ndelta[1] * flVertAnimFixedPointScale,
                     ndelta[2] * flVertAnimFixedPointScale, 0.0f);
    }

    Vector GetDeltaFloat() {
        return Vector(flDelta[0].GetFloat(), flDelta[1].GetFloat(), flDelta[2].GetFloat());
    }

    Vector GetNDeltaFloat() {
        return Vector(flNDelta[0].GetFloat(), flNDelta[1].GetFloat(), flNDelta[2].GetFloat());
    }

    void SetDeltaFixed(const Vector &vInput, float flVertAnimFixedPointScale) {
        delta[0] = (short) (vInput.x / flVertAnimFixedPointScale);
        delta[1] = (short) (vInput.y / flVertAnimFixedPointScale);
        delta[2] = (short) (vInput.z / flVertAnimFixedPointScale);
    }

    void SetNDeltaFixed(const Vector &vInputNormal, float flVertAnimFixedPointScale) {
        ndelta[0] = (short) (vInputNormal.x / flVertAnimFixedPointScale);
        ndelta[1] = (short) (vInputNormal.y / flVertAnimFixedPointScale);
        ndelta[2] = (short) (vInputNormal.z / flVertAnimFixedPointScale);
    }

    // Ick...can also force fp16 data into this structure for writing to file in legacy format...
    void SetDeltaFloat(const Vector &vInput) {
        flDelta[0].SetFloat(vInput.x);
        flDelta[1].SetFloat(vInput.y);
        flDelta[2].SetFloat(vInput.z);
    }

    void SetNDeltaFloat(const Vector &vInputNormal) {
        flNDelta[0].SetFloat(vInputNormal.x);
        flNDelta[1].SetFloat(vInputNormal.y);
        flNDelta[2].SetFloat(vInputNormal.z);
    }

    class CSortByIndex {
    public:
        bool operator()(const mstudiovertanim_t &left, const mstudiovertanim_t &right) const {
            return left.index < right.index;
        }
    };

    friend class CSortByIndex;

    //private:
    // No copy constructors allowed, but it's needed for std::sort()
    //	mstudiovertanim_t(const mstudiovertanim_t& vOther);
};


// this is the memory image of vertex anims (16-bit fixed point)
struct mstudiovertanim_wrinkle_t : public mstudiovertanim_t {
    short wrinkledelta;

    void SetWrinkleFixed(float flWrinkle, float flVertAnimFixedPointScale) {
        int nWrinkleDeltaInt = (int) (flWrinkle / flVertAnimFixedPointScale);
        wrinkledelta = clamp(nWrinkleDeltaInt, -32767, 32767);
    }

    Vector4D GetDeltaFixed(float flVertAnimFixedPointScale) {
        return Vector4D(delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale,
                        delta[2] * flVertAnimFixedPointScale, wrinkledelta * flVertAnimFixedPointScale);
    }

    void GetDeltaFixed4DAligned(Vector4DAligned *vFillIn, float flVertAnimFixedPointScale) {
        vFillIn->Set(delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale,
                     delta[2] * flVertAnimFixedPointScale, wrinkledelta * flVertAnimFixedPointScale);
    }

    float GetWrinkleDeltaFixed(float flVertAnimFixedPointScale) {
        return wrinkledelta * flVertAnimFixedPointScale;
    }
};


enum StudioVertAnimType_t {
    Normal = 0,
    Wrinkle,
};


struct mstudioflex_t {
    int flexdesc; // input value

    float target0; // zero
    float target1; // one
    float target2; // one
    float target3; // zero

    int numverts;
    int vertindex;

    mstudiovertanim_t *pVertanim(int i) const {
        Assert(vertanimtype == STUDIO_VERT_ANIM_NORMAL);
        return (mstudiovertanim_t *) (((uint8_t *) this) + vertindex) + i;
    };

    mstudiovertanim_wrinkle_t *pVertanimWrinkle(int i) const {
        Assert(vertanimtype == STUDIO_VERT_ANIM_WRINKLE);
        return (mstudiovertanim_wrinkle_t *) (((uint8_t *) this) + vertindex) + i;
    };

    uint8_t *pBaseVertanim() const { return ((uint8_t *) this) + vertindex; };

    int VertAnimSizeBytes() const {
        return (vertanimtype == Normal)
                   ? sizeof(mstudiovertanim_t)
                   : sizeof(mstudiovertanim_wrinkle_t);
    }

    int flexpair; // second flex desc
    uint8_t vertanimtype; // See StudioVertAnimType_t
    uint8_t unusedchar[3];
    int unused[6];
};


struct mstudioflexop_t {
    StudioOpcode op;

    union {
        int index;
        float value;
    } d;
};

struct mstudioflexrule_t {
    int flex;
    int numops;
    int opindex;
    mstudioflexop_t *iFlexOp(int i) const { return (mstudioflexop_t *) (((uint8_t *) this) + opindex) + i; };
};

// 16 bytes
struct mstudioboneweight_t {
    float weight[MAX_NUM_BONES_PER_VERT];
    uint8_t bone[MAX_NUM_BONES_PER_VERT];
    uint8_t numbones;

    //	byte	material;
    //	short	firstref;
    //	short	lastref;
};

// NOTE: This is exactly 48 bytes
struct mstudiovertex_t {
    mstudioboneweight_t m_BoneWeights;
    Vector m_vecPosition;
    Vector m_vecNormal;
    Vector2D m_vecTexCoord;

    mstudiovertex_t() {
    }

private:
    // No copy constructors allowed
    mstudiovertex_t(const mstudiovertex_t &vOther);
};

// skin info
struct mstudiotexture_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    int flags;
    int used;
    int unused1;

#ifdef PLATFORM_64BITS
    mutable IMaterial *material;
    //Having pointers in here really messes up 64 bit. these are only used
    //on by studiomdl though, will need to figure this out if we want
    //to port studiomdl to 64 bit.
    int unused[10];
#else
    mutable IMaterial *material; // fixme: this needs to go away . .isn't used by the engine, but is used by studiomdl
    mutable void *clientmaterial; // gary, replace with client material pointer if used
    int unused[10];
#endif
};

// eyeball
struct mstudioeyeball_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    int bone;
    Vector org;
    float zoffset;
    float radius;
    Vector up;
    Vector forward;
    int texture;

    int unused1;
    float iris_scale;
    int unused2;

    int upperflexdesc[3]; // index of raiser, neutral, and lowerer flexdesc that is set by flex controllers
    int lowerflexdesc[3];
    float uppertarget[3]; // angle (radians) of raised, neutral, and lowered lid positions
    float lowertarget[3];

    int upperlidflexdesc; // index of flex desc that actual lid flexes look to
    int lowerlidflexdesc;
    int unused[4]; // These were used before, so not guaranteed to be 0
    bool m_bNonFACS; // Never used before version 44
    char unused3[3];
    int unused4[7];

    mstudioeyeball_t() {
    }

private:
    // No copy constructors allowed
    mstudioeyeball_t(const mstudioeyeball_t &vOther);
};


// ikinfo
struct mstudioiklink_t {
    int bone;
    Vector kneeDir; // ideal bending direction (per link, if applicable)
    Vector unused0; // unused

    mstudioiklink_t() {
    }

private:
    // No copy constructors allowed
    mstudioiklink_t(const mstudioiklink_t &vOther);
};

struct mstudioikchain_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    int linktype;
    int numlinks;
    int linkindex;
    mstudioiklink_t *pLink(int i) const { return (mstudioiklink_t *) (((uint8_t *) this) + linkindex) + i; };
    // FIXME: add unused entries
};

struct mstudioiface_t {
    mstudioiface_t() {
        a = b = c = d = 0xFFFF;
    }

    unsigned short a, b, c, d; // Indices to vertices (If d is 0xFFFF, this is a triangle, else it's a quad)
};

enum ExtraVertexAttributeType_t {
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD0 = 0,
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD1,
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD2,
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD3,
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD4,
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD5,
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD6,
    STUDIO_EXTRA_ATTRIBUTE_TEXCOORD7
};

struct ExtraVertexAttributesHeader_t {
    int m_count; // Number of individual extra attribute chunks
    int m_totalbytes; // Total size of extra attribute data (all chunks plus header and index)
};

struct ExtraVertexAttributeIndex_t {
    ExtraVertexAttributeType_t m_type;
    int m_offset;
    int m_bytes; //bytes per vertex
};

struct mstudiomodel_t;

struct mstudio_modelvertexdata_t {
    Vector *Position(int i) const;

    Vector *Normal(int i) const;

    Vector4D *TangentS(int i) const;

    void *ExtraData(ExtraVertexAttributeType_t type) const;

    Vector2D *Texcoord(int i) const;

    mstudioboneweight_t *BoneWeights(int i) const;

    mstudiovertex_t *Vertex(int i) const;

    bool HasTangentData() const;

    bool HasExtraData() const;

    int GetGlobalVertexIndex(int i) const;

    int GetGlobalTangentIndex(int i) const;

    // base of external vertex data stores
    const void *pVertexData;
    const void *pTangentData;
    const void *pExtraData;
};

#ifdef PLATFORM_64BITS
// 64b - match 32-bit packing
#pragma pack( push, 4 )
#endif

struct mstudio_meshvertexdata_t {
    Vector *Position(int i) const;

    Vector *Normal(int i) const;

    Vector4D *TangentS(int i) const;

    Vector2D *Texcoord(int i) const;

    mstudioboneweight_t *BoneWeights(int i) const;

    mstudiovertex_t *Vertex(int i) const;

    bool HasTangentData() const;

    int GetModelVertexIndex(int i) const;

    int GetGlobalVertexIndex(int i) const;

    // indirection to this mesh's model's vertex data
    int unused_modelvertexdata; // 64b - Moved to follow num_LOD_Vertexes.

    // used for fixup calcs when culling top level lods
    // expected number of mesh verts at desired lod
    int numLODVertexes[MAX_NUM_LODS];

    serializedstudioptr_t<const mstudio_modelvertexdata_t> modelvertexdata;
};

struct mstudiomesh_t {
    int material;

    int modelindex;

    mstudiomodel_t *pModel() const;

    int numvertices; // number of unique vertices/normals/texcoords
    int vertexoffset; // vertex mstudiovertex_t

    // Access thin/fat mesh vertex data (only one will return a non-NULL result)
    const mstudio_meshvertexdata_t *GetVertexData(void *pModelData = nullptr);

    const thinModelVertices_t *GetThinVertexData(void *pModelData = nullptr);

    int numflexes; // vertex animation
    int flexindex;
    mstudioflex_t *pFlex(int i) const { return (mstudioflex_t *) (((uint8_t *) this) + flexindex) + i; };

    // special codes for material operations
    int materialtype;
    int materialparam;

    // a unique ordinal for this mesh
    int meshid;

    Vector center;

    mstudio_meshvertexdata_t vertexdata;

    int unused[6]; // remove as appropriate

    mstudiomesh_t() {
    }

private:
    // No copy constructors allowed
    mstudiomesh_t(const mstudiomesh_t &vOther);
};

// studio models
struct mstudiomodel_t {
    const char *pszName() const { return name; }
    char name[64];

    int type;

    float boundingradius;

    int nummeshes;
    int meshindex;
    mstudiomesh_t *pMesh(int i) const { return (mstudiomesh_t *) (((uint8_t *) this) + meshindex) + i; };

    // cache purposes
    int numvertices; // number of unique vertices/normals/texcoords
    int vertexindex; // vertex Vector
    int tangentsindex; // tangents Vector

    // These functions are defined in application-specific code:
    const vertexFileHeader_t *CacheVertexData(void *pModelData);

    // Access thin/fat mesh vertex data (only one will return a non-NULL result)
    const mstudio_modelvertexdata_t *GetVertexData(void *pModelData = nullptr);

    const thinModelVertices_t *GetThinVertexData(void *pModelData = nullptr);

    int numattachments;
    int attachmentindex;

    int numeyeballs;
    int eyeballindex;
    mstudioeyeball_t *pEyeball(int i) { return (mstudioeyeball_t *) (((uint8_t *) this) + eyeballindex) + i; };

    mstudio_modelvertexdata_t vertexdata;

#ifdef PLATFORM_64BITS
    int unused[4]; // 64b - mstudio_modelvertexdata_t has 3 naked pointers.
#else
    int unused[7]; // remove as appropriate
#endif
};

#ifdef PLATFORM_64BITS
#pragma pack( pop )
#endif

inline bool mstudio_modelvertexdata_t::HasTangentData() const {
    return pTangentData != nullptr;
}

inline bool mstudio_modelvertexdata_t::HasExtraData() const {
    return pExtraData != nullptr;
}

inline int mstudio_modelvertexdata_t::GetGlobalVertexIndex(int i) const {
    mstudiomodel_t *modelptr = (mstudiomodel_t *) ((uint8_t *) this - offsetof(mstudiomodel_t, vertexdata));
    Assert(( modelptr->vertexindex % sizeof( mstudiovertex_t ) ) == 0);
    return (i + (modelptr->vertexindex / sizeof(mstudiovertex_t)));
}

inline int mstudio_modelvertexdata_t::GetGlobalTangentIndex(int i) const {
    mstudiomodel_t *modelptr = (mstudiomodel_t *) ((uint8_t *) this - offsetof(mstudiomodel_t, vertexdata));
    Assert(( modelptr->tangentsindex % sizeof( Vector4D ) ) == 0);
    return (i + (modelptr->tangentsindex / sizeof(Vector4D)));
}

inline mstudiovertex_t *mstudio_modelvertexdata_t::Vertex(int i) const {
    return (mstudiovertex_t *) pVertexData + GetGlobalVertexIndex(i);
}

inline Vector *mstudio_modelvertexdata_t::Position(int i) const {
    return &Vertex(i)->m_vecPosition;
}

inline Vector *mstudio_modelvertexdata_t::Normal(int i) const {
    return &Vertex(i)->m_vecNormal;
}

inline Vector4D *mstudio_modelvertexdata_t::TangentS(int i) const {
    // NOTE: The tangents vector is 16-bytes in a separate array
    // because it only exists on the high end, and if I leave it out
    // of the mstudiovertex_t, the vertex is 64-bytes (good for low end)
    return (Vector4D *) pTangentData + GetGlobalTangentIndex(i);
}

inline void *mstudio_modelvertexdata_t::ExtraData(ExtraVertexAttributeType_t type) const {
    ExtraVertexAttributesHeader_t *pHeader = (ExtraVertexAttributesHeader_t *) pExtraData;
    ExtraVertexAttributeIndex_t *pIndex = (ExtraVertexAttributeIndex_t *) (
        (uint8_t *) pExtraData + sizeof(ExtraVertexAttributesHeader_t));
    for (int i = 0; i < pHeader->m_count; ++i) {
        if (pIndex[i].m_type == type) {
            return (uint8_t *) pExtraData + pIndex[i].m_offset;
        }
    }
    return nullptr;
}

inline Vector2D *mstudio_modelvertexdata_t::Texcoord(int i) const {
    return &Vertex(i)->m_vecTexCoord;
}

inline mstudioboneweight_t *mstudio_modelvertexdata_t::BoneWeights(int i) const {
    return &Vertex(i)->m_BoneWeights;
}

inline mstudiomodel_t *mstudiomesh_t::pModel() const {
    return (mstudiomodel_t *) (((uint8_t *) this) + modelindex);
}

inline bool mstudio_meshvertexdata_t::HasTangentData() const {
    return modelvertexdata->HasTangentData();
}

inline const mstudio_meshvertexdata_t *mstudiomesh_t::GetVertexData(void *pModelData) {
    // get this mesh's model's vertex data (allow for mstudiomodel_t::GetVertexData
    // returning NULL if the data has been converted to 'thin' vertices)
    this->pModel()->GetVertexData(pModelData);
    vertexdata.modelvertexdata = &(this->pModel()->vertexdata);

    if (!vertexdata.modelvertexdata->pVertexData)
        return nullptr;

    return &vertexdata;
}

inline const thinModelVertices_t *mstudiomesh_t::GetThinVertexData(void *pModelData) {
    // get this mesh's model's thin vertex data
    return this->pModel()->GetThinVertexData(pModelData);
}

inline int mstudio_meshvertexdata_t::GetModelVertexIndex(int i) const {
    mstudiomesh_t *meshptr = (mstudiomesh_t *) ((uint8_t *) this - offsetof(mstudiomesh_t, vertexdata));
    return meshptr->vertexoffset + i;
}

inline int mstudio_meshvertexdata_t::GetGlobalVertexIndex(int i) const {
    return modelvertexdata->GetGlobalVertexIndex(GetModelVertexIndex(i));
}

inline Vector *mstudio_meshvertexdata_t::Position(int i) const {
    return modelvertexdata->Position(GetModelVertexIndex(i));
};

inline Vector *mstudio_meshvertexdata_t::Normal(int i) const {
    return modelvertexdata->Normal(GetModelVertexIndex(i));
};

inline Vector4D *mstudio_meshvertexdata_t::TangentS(int i) const {
    return modelvertexdata->TangentS(GetModelVertexIndex(i));
}

inline Vector2D *mstudio_meshvertexdata_t::Texcoord(int i) const {
    return modelvertexdata->Texcoord(GetModelVertexIndex(i));
};

inline mstudioboneweight_t *mstudio_meshvertexdata_t::BoneWeights(int i) const {
    return modelvertexdata->BoneWeights(GetModelVertexIndex(i));
};

inline mstudiovertex_t *mstudio_meshvertexdata_t::Vertex(int i) const {
    return modelvertexdata->Vertex(GetModelVertexIndex(i));
}

// a group of studio model data
enum studiomeshgroupflags_t {
    MESHGROUP_IS_HWSKINNED = 0x2,
    MESHGROUP_IS_DELTA_FLEXED = 0x4
};


// ----------------------------------------------------------
// Runtime stuff
// ----------------------------------------------------------

struct studiomeshgroup_t {
    IMesh *m_pMesh;
    int m_NumStrips;
    int m_Flags; // see studiomeshgroupflags_t
    OptimizedModel::StripHeader_t *m_pStripData;
    unsigned short *m_pGroupIndexToMeshIndex;
    int m_NumVertices;
    int *m_pUniqueFaces; // for performance measurements
    unsigned short *m_pIndices;
    unsigned short *m_pTopologyIndices;
    bool m_MeshNeedsRestore;
    short m_ColorMeshID;
    IMorph *m_pMorph;

    unsigned short MeshIndex(int i) const { return m_pGroupIndexToMeshIndex[m_pIndices[i]]; }
};


// studio model data
struct studiomeshdata_t {
    int m_NumGroup;
    studiomeshgroup_t *m_pMeshGroup;
};

struct studioloddata_t {
    // not needed - this is really the same as studiohwdata_t.m_NumStudioMeshes
    //int					m_NumMeshes;
    studiomeshdata_t *m_pMeshData; // there are studiohwdata_t.m_NumStudioMeshes of these.
    float m_SwitchPoint;
    // one of these for each lod since we can switch to simpler materials on lower lods.
    int numMaterials;
    IMaterial **ppMaterials; /* will have studiohdr_t.numtextures elements allocated */
    // hack - this needs to go away.
    int *pMaterialFlags; /* will have studiohdr_t.numtextures elements allocated */
#ifndef _CERT
    int m_NumFaces; /* Total face count for this LOD */
#endif // !_CERT

    // For decals on hardware morphing, we must actually do hardware skinning
    // For this to work, we have to hope that the total # of bones used by
    // hw flexed verts is < than the max possible for the dx level we're running under
    int *m_pHWMorphDecalBoneRemap;
    int m_nDecalBoneCount;
};

struct studiohwdata_t {
    int m_RootLOD; // calced and clamped, nonzero for lod culling
    int m_NumLODs;
    studioloddata_t *m_pLODs;
    int m_NumStudioMeshes;

    float LODMetric(float unitSphereSize) const {
        return (unitSphereSize != 0.0f) ? (100.0f / unitSphereSize) : 0.0f;
    }

    int GetLODForMetric(float lodMetric) const {
        if (!m_NumLODs)
            return 0;

        // On low GPU levels, we pull in the LOD transitions with a scale factor
        // Always slam the LOD transition scale factor to 1.0f in CS:GO. (Not that it should matter, we've disabled model LOD's, but just in case.)
        float flSwitchPointModifier = 1.0f;

        // shadow lod is specified on the last lod with a negative switch
        // never consider shadow lod as viable candidate
        int numLODs = (m_pLODs[m_NumLODs - 1].m_SwitchPoint < 0.0f) ? m_NumLODs - 1 : m_NumLODs;

        for (int i = m_RootLOD; i < numLODs - 1; i++) {
            if ((m_pLODs[i + 1].m_SwitchPoint * flSwitchPointModifier) > lodMetric)
                return i;
        }

        return numLODs - 1;
    }

#ifndef _CERT
    // Each model counts how many rendered faces it accounts for each frame:
    void UpdateFacesRenderedCount(studiohdr_t *pStudioHdr, CUtlHash<studiohwdata_t *> &hwDataHash, int nLOD,
                                  int nInstances, int nFacesOverride = -1) {
        if (hwDataHash.Find(this) == hwDataHash.InvalidHandle()) {
            m_NumFacesRenderedThisFrame = 0;
            m_NumTimesRenderedThisFrame = 0;
            m_pStudioHdr = pStudioHdr;
            hwDataHash.Insert(this);
        }
        Assert(m_pStudioHdr && ( m_pStudioHdr == pStudioHdr ));
        if (nFacesOverride == -1) {
            nFacesOverride = (nLOD < m_NumLODs) ? m_pLODs[nLOD].m_NumFaces : 0;
        }
        m_NumFacesRenderedThisFrame += nInstances * nFacesOverride;
        m_NumTimesRenderedThisFrame++;
    }

    int m_NumFacesRenderedThisFrame;
    int m_NumTimesRenderedThisFrame;
    studiohdr_t *m_pStudioHdr; // There is no way to map between these inside CStudioRender, so we have to store it.
#endif // !_CERT
};

// ----------------------------------------------------------
// ----------------------------------------------------------

// body part index
struct mstudiobodyparts_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    int nummodels;
    int base;
    int modelindex; // index into models array
    mstudiomodel_t *pModel(int i) const { return (mstudiomodel_t *) (((uint8_t *) this) + modelindex) + i; };
};

// body group preset
struct mstudiobodygrouppreset_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    int iValue;
    int iMask;
};

struct mstudiomouth_t {
    int bone;
    Vector forward;
    int flexdesc;

    mstudiomouth_t() {
    }

private:
    // No copy constructors allowed
    mstudiomouth_t(const mstudiomouth_t &vOther);
};

struct mstudiohitboxset_t {
    int sznameindex;
    char *const pszName() const { return ((char *) this) + sznameindex; }
    int numhitboxes;
    int hitboxindex;
    mstudiobbox_t *pHitbox(int i) const { return (mstudiobbox_t *) (((uint8_t *) this) + hitboxindex) + i; };
};


//-----------------------------------------------------------------------------
// Src bone transforms are transformations that will convert .dmx or .smd-based animations into .mdl-based animations
// NOTE: The operation you should apply is: pretransform * bone transform * posttransform
//-----------------------------------------------------------------------------
struct mstudiosrcbonetransform_t {
    int sznameindex;
    const char *pszName() const { return ((char *) this) + sznameindex; }
    matrix3x4_t pretransform;
    matrix3x4_t posttransform;
};


// ----------------------------------------------------------
// Purpose: Load time results on model compositing
// ----------------------------------------------------------

class virtualgroup_t {
public:
    virtualgroup_t() { cache = nullptr; };
    // tool dependant.  In engine this is a model_t, in tool it's a direct pointer
    void *cache;

    // converts cache entry into a usable studiohdr_t *
    const studiohdr_t *GetStudioHdr() const;

    CUtlVector<int> boneMap; // maps global bone to local bone
    CUtlVector<int> masterBone; // maps local bone to global bone
    CUtlVector<int> masterSeq; // maps local sequence to master sequence
    CUtlVector<int> masterAnim; // maps local animation to master animation
    CUtlVector<int> masterAttachment; // maps local attachment to global
    CUtlVector<int> masterPose; // maps local pose parameter to global
    CUtlVector<int> masterNode; // maps local transition nodes to global
};

struct virtualsequence_t {
    int flags;
    int activity;
    int group;
    int index;
};

struct virtualgeneric_t {
    int group;
    int index;
};


struct virtualmodel_t {
    void AppendSequences(int group, const studiohdr_t *pStudioHdr);

    void AppendAnimations(int group, const studiohdr_t *pStudioHdr);

    void AppendAttachments(int ground, const studiohdr_t *pStudioHdr);

    void AppendPoseParameters(int group, const studiohdr_t *pStudioHdr);

    void AppendBonemap(int group, const studiohdr_t *pStudioHdr);

    void AppendNodes(int group, const studiohdr_t *pStudioHdr);

    void AppendTransitions(int group, const studiohdr_t *pStudioHdr);

    void AppendIKLocks(int group, const studiohdr_t *pStudioHdr);

    void AppendModels(int group, const studiohdr_t *pStudioHdr);

    void UpdateAutoplaySequences(const studiohdr_t *pStudioHdr);

    virtualgroup_t *pAnimGroup(int animation) { return &m_group[m_anim[animation].group]; };
    // Note: user must manage mutex for this
    virtualgroup_t *pSeqGroup(int sequence) { return &m_group[m_seq[sequence].group]; };
    // Note: user must manage mutex for this

    CUtlVector<virtualsequence_t> m_seq;
    CUtlVector<virtualgeneric_t> m_anim;
    CUtlVector<virtualgeneric_t> m_attachment;
    CUtlVector<virtualgeneric_t> m_pose;
    CUtlVector<virtualgroup_t> m_group;
    CUtlVector<virtualgeneric_t> m_node;
    CUtlVector<virtualgeneric_t> m_iklock;
    CUtlVector<unsigned short> m_autoplaySequences;
};

// 'thin' vertex data, used to do model decals (see Studio_CreateThinVertexes())
struct thinModelVertices_t {
    void Init(int numBoneInfluences, Vector *positions, unsigned short *normals, float *boneWeights,
              uint8_t *boneIndices) {
        Assert(positions != NULL);
        Assert(normals != NULL);
        Assert(( numBoneInfluences >= 0 ) && ( numBoneInfluences <= 3 ));
        Assert(numBoneInfluences > 0 ? !!boneIndices : !boneIndices);
        Assert(numBoneInfluences > 1 ? !!boneWeights : !boneWeights);
        m_numBoneInfluences = numBoneInfluences;
        m_vecPositions = positions;
        m_vecNormals = normals;
        m_boneWeights = boneWeights;
        m_boneIndices = boneIndices;
    }

    void SetPosition(int vertIndex, const Vector &position) {
        Assert(m_vecPositions);
        m_vecPositions[vertIndex] = position;
    }

    void SetNormal(int vertIndex, const Vector &normal) {
        Assert(m_vecNormals);
        unsigned int packedNormal;
        PackNormal_UBYTE4(normal.x, normal.y, normal.z, &packedNormal);
        m_vecNormals[vertIndex] = (unsigned short) (0x0000FFFF & packedNormal);
    }

    void SetBoneWeights(int vertIndex, const mstudioboneweight_t &boneWeights) {
        Assert(( m_numBoneInfluences >= 1 ) && ( m_numBoneInfluences <= 3 ));
        Assert(( boneWeights.numbones >= 1 ) && ( boneWeights.numbones <= m_numBoneInfluences ));
        int numStoredWeights = MAX(0, ( m_numBoneInfluences - 1 ));
        float *pBaseWeight = m_boneWeights + vertIndex * numStoredWeights;
        uint8_t *pBaseIndex = m_boneIndices + vertIndex * m_numBoneInfluences;
        for (int i = 0; i < m_numBoneInfluences; i++) {
            pBaseIndex[i] = boneWeights.bone[i];
        }
        for (int i = 0; i < numStoredWeights; i++) {
            pBaseWeight[i] = boneWeights.weight[i];
        }
    }

    void GetMeshPosition(mstudiomesh_t *pMesh, int meshIndex, Vector *pPosition) const {
        Assert(pMesh);
        GetPosition(pMesh->vertexdata.GetGlobalVertexIndex(meshIndex), pPosition);
    }

    void GetMeshNormal(mstudiomesh_t *pMesh, int meshIndex, Vector *pNormal) const {
        Assert(pMesh);
        GetNormal(pMesh->vertexdata.GetGlobalVertexIndex(meshIndex), pNormal);
    }

    void GetMeshBoneWeights(mstudiomesh_t *pMesh, int meshIndex, mstudioboneweight_t *pBoneWeights) const {
        Assert(pMesh);
        GetBoneWeights(pMesh->vertexdata.GetGlobalVertexIndex(meshIndex), pBoneWeights);
    }

    void GetModelPosition(mstudiomodel_t *pModel, int modelIndex, Vector *pPosition) const {
        Assert(pModel);
        GetPosition(pModel->vertexdata.GetGlobalVertexIndex(modelIndex), pPosition);
    }

    void GetModelNormal(mstudiomodel_t *pModel, int modelIndex, Vector *pNormal) const {
        Assert(pModel);
        GetNormal(pModel->vertexdata.GetGlobalVertexIndex(modelIndex), pNormal);
    }

    void GetModelBoneWeights(mstudiomodel_t *pModel, int modelIndex, mstudioboneweight_t *pBoneWeights) const {
        Assert(pModel);
        GetBoneWeights(pModel->vertexdata.GetGlobalVertexIndex(modelIndex), pBoneWeights);
    }

private:
    void GetPosition(int vertIndex, Vector *pPosition) const {
        Assert(pPosition);
        Assert(m_vecPositions);
        *pPosition = m_vecPositions[vertIndex];
    }

    void GetNormal(int vertIndex, Vector *pNormal) const {
        Assert(pNormal);
        Assert(m_vecNormals);
        unsigned int packedNormal = 0x0000FFFF & m_vecNormals[vertIndex];
        UnpackNormal_UBYTE4(&packedNormal, pNormal->Base());
    }

    void GetBoneWeights(int vertIndex, mstudioboneweight_t * RESTRICT pBoneWeights) const {
        Assert(pBoneWeights);
        Assert(( m_numBoneInfluences <= 1 ) || ( m_boneWeights != NULL ));
        Assert(( m_numBoneInfluences <= 0 ) || ( m_boneIndices != NULL ));
        int numStoredWeights = MAX(0, ( m_numBoneInfluences - 1 ));
        float * RESTRICT pBaseWeight = m_boneWeights + vertIndex * numStoredWeights;
        uint8_t * RESTRICT pBaseIndex = m_boneIndices + vertIndex * m_numBoneInfluences;
        float sum = 0.0f;
        // TODO: unroll this loop? It's only three. We could use a switch
        // and code it explicitly for the various possible m_numBoneInfluences
        // which would improve scheduling but bloat code.
        for (int i = 0; i < MAX_NUM_BONES_PER_VERT; i++) {
            float weight;
            if (i < (m_numBoneInfluences - 1)) {
                weight = pBaseWeight[i];
                sum += weight;
            }
            else {
                weight = 1.0f - sum;
                sum = 1.0f;
            }

            pBoneWeights->weight[i] = weight;
            pBoneWeights->bone[i] = (i < m_numBoneInfluences) ? pBaseIndex[i] : 0;

            /*
            if ( i < ( m_numBoneInfluences - 1 ) )
                pBoneWeights->weight[i] = pBaseWeight[i];
            else
                pBoneWeights->weight[i] = 1.0f - sum;
            sum += pBoneWeights->weight[i];

            pBoneWeights->bone[i] = ( i < m_numBoneInfluences ) ? pBaseIndex[i] : 0;
            */
        }

        // Treat 'zero weights' as '100% binding to bone zero':
        pBoneWeights->numbones = m_numBoneInfluences ? m_numBoneInfluences : 1;
    }

    int m_numBoneInfluences; // Number of bone influences per vertex, N
    float *m_boneWeights; // This array stores (N-1) weights per vertex (unless N is zero)
    uint8_t *m_boneIndices; // This array stores N indices per vertex
    Vector *m_vecPositions;
    unsigned short *m_vecNormals; // Normals are compressed into 16 bits apiece (see PackNormal_UBYTE4() )
};


// ----------------------------------------------------------
// Studio Model Stream Data File
// ----------------------------------------------------------

// little-endian "IDSS"
#define MODEL_STREAM_FILE_ID		(('S'<<24)+('S'<<16)+('D'<<8)+'I')
#define MODEL_STREAM_FILE_VERSION	1

struct vertexStreamFileHeader_t {
    int id; // MODEL_STREAM_FILE_ID
    int version; // MODEL_STREAM_FILE_VERSION
    int checksum; // same as studiohdr_t, ensures sync
    int flags; // flags
    int numVerts; // number of vertices
    int uv2StreamStart; // offset from base to uv2 stream
    int uv2ElementSize; // size of each uv2 element
    int pad; // pad

public:
    // Accessor to uv2 stream
    const void *GetStreamUv2() const {
        if ((id == MODEL_STREAM_FILE_ID) && (uv2StreamStart != 0))
            return (void *) (uv2StreamStart + (uint8_t *) this);
        else
            return nullptr;
    }
};

struct PhysFeModelDesc_t;

// ----------------------------------------------------------
// Studio Model Vertex Data File
// Position independent flat data for cache manager
// ----------------------------------------------------------

// little-endian "IDSV"
#define MODEL_VERTEX_FILE_ID		(('V'<<24)+('S'<<16)+('D'<<8)+'I')
#define MODEL_VERTEX_FILE_VERSION	4
// this id (IDCV) is used once the vertex data has been compressed (see CMDLCache::CreateThinVertexes)
#define MODEL_VERTEX_FILE_THIN_ID	(('V'<<24)+('C'<<16)+('D'<<8)+'I')
// this id (IDDV) is used once the vertex data has been discarded (see CMDLCache::CreateNullVertexes)
#define MODEL_VERTEX_FILE_NULL_ID	(('V'<<24)+('D'<<16)+('D'<<8)+'I')

struct vertexFileHeader_t {
    int id; // MODEL_VERTEX_FILE_ID
    int version; // MODEL_VERTEX_FILE_VERSION
    int checksum; // same as studiohdr_t, ensures sync
    int numLODs; // num of valid lods
    int numLODVertexes[MAX_NUM_LODS]; // num verts for desired root lod
    int numFixups; // num of vertexFileFixup_t
    int fixupTableStart; // offset from base to fixup table
    int vertexDataStart; // offset from base to vertex block
    int tangentDataStart; // offset from base to tangent block

public:
    // Accessor to fat vertex data
    const mstudiovertex_t *GetVertexData() const {
        if ((id == MODEL_VERTEX_FILE_ID) && (vertexDataStart != 0))
            return (mstudiovertex_t *) (vertexDataStart + (uint8_t *) this);
        else
            return nullptr;
    }

    // Accessor to (fat) tangent vertex data (tangents aren't stored in compressed data)
    const Vector4D *GetTangentData() const {
        if ((id == MODEL_VERTEX_FILE_ID) && (tangentDataStart != 0))
            return (Vector4D *) (tangentDataStart + (uint8_t *) this);
        else
            return nullptr;
    }

    // Accessor to extra vertex data
    const void *GetExtraData() const {
        if ((id == MODEL_VERTEX_FILE_ID) && (tangentDataStart != 0))
            return (void *) (tangentDataStart + (uint8_t *) this + numLODVertexes[0] * sizeof(Vector4D));
        else
            return nullptr;
    }

    // Accessor to thin vertex data
    const thinModelVertices_t *GetThinVertexData() const {
        if ((id == MODEL_VERTEX_FILE_THIN_ID) && (vertexDataStart != 0))
            return (thinModelVertices_t *) (vertexDataStart + (uint8_t *) this);
        else
            return nullptr;
    }
};

// apply sequentially to lod sorted vertex and tangent pools to re-establish mesh order
struct vertexFileFixup_t {
    int lod; // used to skip culled root lod
    int sourceVertexID; // absolute index from start of vertex/tangent blocks
    int numVertexes;
};

enum StudioHDRFlags:uint32_t {
    // This flag is set if no hitbox information was specified
    STUDIOHDR_FLAGS_AUTOGENERATED_HITBOX = (1 << 0),

    // NOTE:  This flag is set at loadtime, not mdl build time so that we don't have to rebuild
    // models when we change materials.
    STUDIOHDR_FLAGS_USES_ENV_CUBEMAP = (1 << 1),

    // Use this when there are translucent parts to the model but we're not going to sort it
    STUDIOHDR_FLAGS_FORCE_OPAQUE = (1 << 2),

    // Use this when we want to render the opaque parts during the opaque pass
    // and the translucent parts during the translucent pass
    STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS = (1 << 3),

    // This is set any time the .qc files has $staticprop in it
    // Means there's no bones and no transforms
    STUDIOHDR_FLAGS_STATIC_PROP = (1 << 4),

    // NOTE:  This flag is set at loadtime, not mdl build time so that we don't have to rebuild
    // models when we change materials.
    STUDIOHDR_FLAGS_USES_FB_TEXTURE = (1 << 5),

    // This flag is set by studiomdl.exe if a separate "$shadowlod" entry was present
    //  for the .mdl (the shadow lod is the last entry in the lod list if present)
    STUDIOHDR_FLAGS_HASSHADOWLOD = (1 << 6),

    // NOTE:  This flag is set at loadtime, not mdl build time so that we don't have to rebuild
    // models when we change materials.
    STUDIOHDR_FLAGS_USES_BUMPMAPPING = (1 << 7),

    // NOTE:  This flag is set when we should use the actual materials on the shadow LOD
    // instead of overriding them with the default one (necessary for translucent shadows)
    STUDIOHDR_FLAGS_USE_SHADOWLOD_MATERIALS = (1 << 8),

    // NOTE:  This flag is set when we should use the actual materials on the shadow LOD
    // instead of overriding them with the default one (necessary for translucent shadows)
    STUDIOHDR_FLAGS_OBSOLETE = (1 << 9),

    // NOTE:  This flag is set when we need to draw in the additive stage of the deferred rendering
    STUDIOHDR_FLAGS_NEEDS_DEFERRED_ADDITIVE = (1 << 10),

    // NOTE:  This flag is set at mdl build time
    STUDIOHDR_FLAGS_NO_FORCED_FADE = (1 << 11),

    // NOTE:  The npc will lengthen the viseme check to always include two phonemes
    STUDIOHDR_FLAGS_FORCE_PHONEME_CROSSFADE = (1 << 12),

    // This flag is set when the .qc has $constantdirectionallight in it
    // If set, we use constantdirectionallightdot to calculate light intensity
    // rather than the normal directional dot product
    // only valid if STUDIOHDR_FLAGS_STATIC_PROP is also set
    STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT = (1 << 13),

    // Flag to mark delta flexes as already converted from disk format to memory format
    STUDIOHDR_FLAGS_FLEXES_CONVERTED = (1 << 14),

    // Indicates the studiomdl was built in preview mode
    STUDIOHDR_FLAGS_BUILT_IN_PREVIEW_MODE = (1 << 15),

    // Ambient boost (runtime flag)
    STUDIOHDR_FLAGS_AMBIENT_BOOST = (1 << 16),

    // Don't cast shadows from this model (useful on first-person models)
    STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS = (1 << 17),

    // alpha textures should cast shadows in vrad on this model (ONLY prop_static!)
    STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS = (1 << 18),

    // Model has a quad-only Catmull-Clark SubD cage
    STUDIOHDR_FLAGS_SUBDIVISION_SURFACE = (1 << 19),

    // flagged on load to indicate no animation events on this model
    STUDIOHDR_FLAGS_NO_ANIM_EVENTS = (1 << 20),

    // If flag is set then studiohdr_t.flVertAnimFixedPointScale contains the
    // scale value for fixed point vert anim data, if not set then the
    // scale value is the default of 1.0 / 4096.0.  Regardless use
    // studiohdr_t::VertAnimFixedPointScale() to always retrieve the scale value
    STUDIOHDR_FLAGS_VERT_ANIM_FIXED_POINT_SCALE = (1 << 21),

    // If flag is set then model data is processed for EDGE
    // the flag is set at tool time when producing PS3-format assets
    STUDIOHDR_FLAGS_PS3_EDGE_FORMAT = (1 << 22),

    // this is a specific case to indicate a model is over budget
    STUDIOHDR_FLAGS_OVER_BUDGET = (1 << 23),

    // this is a specific case to indicate a model is over budget
    STUDIOHDR_FLAGS_IGNORE_BUDGETS = (1 << 24),

    // internally generated combined model
    STUDIOHDR_FLAGS_COMBINED = (1 << 25),

    // Model has an additional set of UVs
    STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA = (1 << 26),

    // NOTE:  This flag is set at loadtime, not mdl build time so that we don't have to rebuild
    // models when we change materials.
    STUDIOHDR_BAKED_VERTEX_LIGHTING_IS_INDIRECT_ONLY = (1 << 27),
};

// NOTE! Next time we up the .mdl file format, remove studiohdr2_t
// and insert all fields in this structure into studiohdr_t.
struct studiohdr2_t {
    // NOTE: For forward compat, make sure any methods in this struct
    // are also available in studiohdr_t so no leaf code ever directly references
    // a studiohdr2_t structure
    int numsrcbonetransform;
    int srcbonetransformindex;

    int illumpositionattachmentindex;
    int IllumPositionAttachmentIndex() const { return illumpositionattachmentindex; }

    float flMaxEyeDeflection;
    float MaxEyeDeflection() const { return flMaxEyeDeflection != 0.0f ? flMaxEyeDeflection : 0.866f; }
    // default to cos(30) if not set

    int linearboneindex;

    mstudiolinearbone_t *pLinearBones() const {
        return (linearboneindex) ? (mstudiolinearbone_t *) (((uint8_t *) this) + linearboneindex) : nullptr;
    }

    int sznameindex;
    char *pszName() { return (sznameindex) ? (char *) (((uint8_t *) this) + sznameindex) : nullptr; }

    int m_nBoneFlexDriverCount;
    int m_nBoneFlexDriverIndex;

    mstudioboneflexdriver_t *pBoneFlexDriver(int i) const {
        Assert(i >= 0 && i < m_nBoneFlexDriverCount);
        return (mstudioboneflexdriver_t *) (((uint8_t *) this) + m_nBoneFlexDriverIndex) + i;
    }

    int m_nBodyGroupPresetCount;
    int m_nBodyGroupPresetIndex;

    mstudiobodygrouppreset_t *pBodyGroupPreset(int i) const {
        Assert(i >= 0 && i < m_nBodyGroupPresetCount);
        return (mstudiobodygrouppreset_t *) (((uint8_t *) this) + m_nBodyGroupPresetIndex) + i;
    };

    int padding_unused; // This hasn't been used before, use it if you need it.

    mutable serializedstudioptr_t<void> virtualModel;
    mutable serializedstudioptr_t<void> animblockModel;

    serializedstudioptr_t<void> pVertexBase;
    serializedstudioptr_t<void> pIndexBase;

    int reserved[44];
};

struct studiohdr_t {
    int id;
    int version;

    int checksum; // this has to be the same in the phy and vtx files to load!

    const char *pszName() const {
        if (studiohdr2index && pStudioHdr2()->pszName()) return pStudioHdr2()->pszName();
        else return name;
    }

    char name[64];

    int length;

    Vector eyeposition; // ideal eye position

    Vector illumposition; // illumination center

    Vector hull_min; // ideal movement hull size
    Vector hull_max;

    Vector view_bbmin; // clipping bounding box
    Vector view_bbmax;

    uint32_t flags;

    int numbones; // bones
    int boneindex;

    const mstudiobone_t *pBone(int i) const {
        Assert(i >= 0 && i < numbones);
        return (mstudiobone_t *) (((uint8_t *) this) + boneindex) + i;
    };

    int RemapSeqBone(int iSequence, int iLocalBone) const; // maps local sequence bone to global bone
    int RemapAnimBone(int iAnim, int iLocalBone) const; // maps local animations bone to global bone

    int numbonecontrollers; // bone controllers
    int bonecontrollerindex;

    mstudiobonecontroller_t *pBonecontroller(int i) const {
        Assert(i >= 0 && i < numbonecontrollers);
        return (mstudiobonecontroller_t *) (((uint8_t *) this) + bonecontrollerindex) + i;
    };

    int numhitboxsets;
    int hitboxsetindex;

    // Look up hitbox set by index
    mstudiohitboxset_t *pHitboxSet(int i) const {
        Assert(i >= 0 && i < numhitboxsets);
        return (mstudiohitboxset_t *) (((uint8_t *) this) + hitboxsetindex) + i;
    };

    // Calls through to hitbox to determine size of specified set
    mstudiobbox_t *pHitbox(int i, int set) const {
        mstudiohitboxset_t const *s = pHitboxSet(set);
        if (!s)
            return nullptr;

        return s->pHitbox(i);
    };

    // Calls through to set to get hitbox count for set
    int iHitboxCount(int set) const {
        mstudiohitboxset_t const *s = pHitboxSet(set);
        if (!s)
            return 0;

        return s->numhitboxes;
    };

    // file local animations? and sequences
    //private:
    int numlocalanim; // animations/poses
    int localanimindex; // animation descriptions
    mstudioanimdesc_t *pLocalAnimdesc(int i) const {
        if (i < 0 || i >= numlocalanim) i = 0;
        return (mstudioanimdesc_t *) (((uint8_t *) this) + localanimindex) + i;
    };

    int numlocalseq; // sequences
    int localseqindex;

    mstudioseqdesc_t *pLocalSeqdesc(int i) const {
        if (i < 0 || i >= numlocalseq) i = 0;
        return (mstudioseqdesc_t *) (((uint8_t *) this) + localseqindex) + i;
    };

    //public:
    bool SequencesAvailable() const;

    int GetNumSeq_Internal() const;

    int GetNumSeq() const {
        if (numincludemodels == 0) {
            return numlocalseq;
        }
        return GetNumSeq_Internal();
    }

    mstudioanimdesc_t &pAnimdesc_Internal(int i) const;

    mstudioanimdesc_t &pAnimdesc(int i) const {
        if (numincludemodels == 0) {
            return *pLocalAnimdesc(i);
        }

        return pAnimdesc_Internal(i);
    }

    mstudioseqdesc_t &pSeqdesc_Internal(int i) const;

    mstudioseqdesc_t &pSeqdesc(int i) const {
        if (numincludemodels == 0) {
            return *pLocalSeqdesc(i);
        }
        return pSeqdesc_Internal(i);
    }

    int iRelativeAnim_Internal(int baseseq, int relanim) const; // maps seq local anim reference to global anim index
    int iRelativeAnim(int baseseq, int relanim) const {
        if (numincludemodels == 0)
            return relanim;
        return iRelativeAnim_Internal(baseseq, relanim);
    }

    int iRelativeSeq_Internal(int baseseq, int relseq) const; // maps seq local seq reference to global seq index
    int iRelativeSeq(int baseseq, int relseq) const {
        if (numincludemodels == 0) {
            return relseq;
        }
        return iRelativeSeq_Internal(baseseq, relseq);
    }

    //private:
    mutable int activitylistversion; // initialization flag - have the sequences been indexed?
    mutable int eventsindexed;

    //public:
    int GetSequenceActivity(int iSequence);

    void SetSequenceActivity(int iSequence, int iActivity);

    int GetActivityListVersion();

    void SetActivityListVersion(int version) const;

    int GetEventListVersion();

    void SetEventListVersion(int version);

    // raw textures
    int numtextures;
    int textureindex;

    mstudiotexture_t *pTexture(int i) const {
        Assert(i >= 0 && i < numtextures);
        return (mstudiotexture_t *) (((uint8_t *) this) + textureindex) + i;
    };


    // raw textures search paths
    int numcdtextures;
    int cdtextureindex;

    char *pCdtexture(int i) const {
        return (((char *) this) + *((int *) (((uint8_t *) this) + cdtextureindex) + i));
    };

    // replaceable textures tables
    int numskinref;
    int numskinfamilies;
    int skinindex;
    short *pSkinref(int i) const { return (short *) (((uint8_t *) this) + skinindex) + i; };

    int numbodyparts;
    int bodypartindex;

    mstudiobodyparts_t *pBodypart(const int i) const {
        return (mstudiobodyparts_t *) (((uint8_t *) this) + bodypartindex) + i;
    };

    // queryable attachable points
    //private:
    int numlocalattachments;
    int localattachmentindex;

    mstudioattachment_t *pLocalAttachment(int i) const {
        Assert(i >= 0 && i < numlocalattachments);
        return (mstudioattachment_t *) (((uint8_t *) this) + localattachmentindex) + i;
    };

    //public:
    int GetNumAttachments() const;

    const mstudioattachment_t &pAttachment(int i) const;

    int GetAttachmentBone(int i);

    // used on my tools in hlmv, not persistant
    void SetAttachmentBone(int iAttachment, int iBone);

    // animation node to animation node transition graph
    //private:
    int numlocalnodes;
    int localnodeindex;
    int localnodenameindex;

    char *pszLocalNodeName(int iNode) const {
        Assert(iNode >= 0 && iNode < numlocalnodes);
        return (((char *) this) + *((int *) (((uint8_t *) this) + localnodenameindex) + iNode));
    }

    uint8_t *pLocalTransition(int i) const {
        Assert(i >= 0 && i < (numlocalnodes * numlocalnodes));
        return (uint8_t *) this + localnodeindex + i;
    };

    //public:
    int EntryNode(int iSequence);

    int ExitNode(int iSequence);

    char *pszNodeName(int iNode);

    int GetTransition(int iFrom, int iTo) const;

    int numflexdesc;
    int flexdescindex;

    mstudioflexdesc_t *pFlexdesc(int i) const {
        Assert(i >= 0 && i < numflexdesc);
        return (mstudioflexdesc_t *) (((uint8_t *) this) + flexdescindex) + i;
    };

    int numflexcontrollers;
    int flexcontrollerindex;

    mstudioflexcontroller_t *pFlexcontroller(LocalFlexController_t i) const {
        Assert(i >= 0 && i < numflexcontrollers);
        return (mstudioflexcontroller_t *) (((uint8_t *) this) + flexcontrollerindex) + i;
    };

    int numflexrules;
    int flexruleindex;

    mstudioflexrule_t *pFlexRule(int i) const {
        Assert(i >= 0 && i < numflexrules);
        return (mstudioflexrule_t *) (((uint8_t *) this) + flexruleindex) + i;
    };

    int numikchains;
    int ikchainindex;

    mstudioikchain_t *pIKChain(int i) const {
        Assert(i >= 0 && i < numikchains);
        return (mstudioikchain_t *) (((uint8_t *) this) + ikchainindex) + i;
    };

    int nummouths;
    int mouthindex;

    mstudiomouth_t *pMouth(int i) const {
        Assert(i >= 0 && i < nummouths);
        return (mstudiomouth_t *) (((uint8_t *) this) + mouthindex) + i;
    };

    //private:
    int numlocalposeparameters;
    int localposeparamindex;

    mstudioposeparamdesc_t *pLocalPoseParameter(int i) const {
        Assert(i >= 0 && i < numlocalposeparameters);
        return (mstudioposeparamdesc_t *) (((uint8_t *) this) + localposeparamindex) + i;
    };

    //public:
    int GetNumPoseParameters() const;

    const mstudioposeparamdesc_t &pPoseParameter(int i);

    int GetSharedPoseParameter(int iSequence, int iLocalPose) const;

    int surfacepropindex;
    char *const pszSurfaceProp() const { return ((char *) this) + surfacepropindex; }
    int GetSurfaceProp() const { return surfacepropLookup; }

    // Key values
    int keyvalueindex;
    int keyvaluesize;

    const char *KeyValueText() const {
        return keyvaluesize != 0 ? ((char *) this) + keyvalueindex : nullptr;
    }

    int numlocalikautoplaylocks;
    int localikautoplaylockindex;

    mstudioiklock_t *pLocalIKAutoplayLock(int i) const {
        Assert(i >= 0 && i < numlocalikautoplaylocks);
        return (mstudioiklock_t *) (((uint8_t *) this) + localikautoplaylockindex) + i;
    };

    int GetNumIKAutoplayLocks() const;

    const mstudioiklock_t &pIKAutoplayLock(int i);

    int CountAutoplaySequences() const;

    int CopyAutoplaySequences(unsigned short *pOut, int outCount) const;

    int GetAutoplayList(unsigned short **pOut) const;

    // The collision model mass that jay wanted
    float mass;
    ContentType contents;

    // external animations, models, etc.
    int numincludemodels;
    int includemodelindex;

    mstudiomodelgroup_t *pModelGroup(int i) const {
        Assert(i >= 0 && i < numincludemodels);
        return (mstudiomodelgroup_t *) (((uint8_t *) this) + includemodelindex) + i;
    };

    // implementation specific call to get a named model
    const studiohdr_t *FindModel(void **cache, char const *modelname) const;

    // implementation specific back pointer to virtual data. Relocated to studiohdr2_t
    int unused_virtualModel;

    virtualmodel_t *GetVirtualModel() const;

    // for demand loaded animation blocks
    int szanimblocknameindex;
    char *const pszAnimBlockName() const { return ((char *) this) + szanimblocknameindex; }
    int numanimblocks;
    int animblockindex;

    mstudioanimblock_t *pAnimBlock(int i) const {
        Assert(i > 0 && i < numanimblocks);
        return (mstudioanimblock_t *) (((uint8_t *) this) + animblockindex) + i;
    };

    // Relocated to studiohdr2_t
    int unused_animblockModel;

    uint8_t *GetAnimBlock(int i, bool preloadIfMissing = true) const;

    bool hasAnimBlockBeenPreloaded(int i) const;

    int bonetablebynameindex;
    const uint8_t *GetBoneTableSortedByName() const { return (uint8_t *) this + bonetablebynameindex; }

    // used by tools only that don't cache, but persist mdl's peer data
    // engine uses virtualModel to back link to cache pointers
    // Relocated to studiohdr2_t
    int unused_pVertexBase;
    int unused_pIndexBase;

    // if STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT is set,
    // this value is used to calculate directional components of lighting
    // on static props
    uint8_t constdirectionallightdot;

    // set during load of mdl data to track *desired* lod configuration (not actual)
    // the *actual* clamped root lod is found in studiohwdata
    // this is stored here as a global store to ensure the staged loading matches the rendering
    uint8_t rootLOD;

    // set in the mdl data to specify that lod configuration should only allow first numAllowRootLODs
    // to be set as root LOD:
    //	numAllowedRootLODs = 0	means no restriction, any lod can be set as root lod.
    //	numAllowedRootLODs = N	means that lod0 - lod(N-1) can be set as root lod, but not lodN or lower.
    uint8_t numAllowedRootLODs;

    uint8_t unused[1];

    int unused4; // zero out if version < 47

    int numflexcontrollerui;
    int flexcontrolleruiindex;

    mstudioflexcontrollerui_t *pFlexControllerUI(int i) const {
        Assert(i >= 0 && i < numflexcontrollerui);
        return (mstudioflexcontrollerui_t *) (((uint8_t *) this) + flexcontrolleruiindex) + i;
    }

    float flVertAnimFixedPointScale;

    float VertAnimFixedPointScale() const {
        return (flags & STUDIOHDR_FLAGS_VERT_ANIM_FIXED_POINT_SCALE) ? flVertAnimFixedPointScale : 1.0f / 4096.0f;
    }

    mutable int surfacepropLookup; // this index must be cached by the loader, not saved in the file

    // FIXME: Remove when we up the model version. Move all fields of studiohdr2_t into studiohdr_t.
    int studiohdr2index;
    studiohdr2_t *pStudioHdr2() const { return (studiohdr2_t *) (((uint8_t *) this) + studiohdr2index); }

    // Src bone transforms are transformations that will convert .dmx or .smd-based animations into .mdl-based animations
    int NumSrcBoneTransforms() const { return studiohdr2index ? pStudioHdr2()->numsrcbonetransform : 0; }

    const mstudiosrcbonetransform_t *SrcBoneTransform(int i) const {
        Assert(i >= 0 && i < NumSrcBoneTransforms());
        return (mstudiosrcbonetransform_t *) (((uint8_t *) this) + pStudioHdr2()->srcbonetransformindex) + i;
    }

    int IllumPositionAttachmentIndex() const {
        return studiohdr2index ? pStudioHdr2()->IllumPositionAttachmentIndex() : 0;
    }

    float MaxEyeDeflection() const { return studiohdr2index ? pStudioHdr2()->MaxEyeDeflection() : 0.866f; }
    // default to cos(30) if not set

    mstudiolinearbone_t *pLinearBones() const {
        return studiohdr2index ? pStudioHdr2()->pLinearBones() : nullptr;
    }

    int BoneFlexDriverCount() const { return studiohdr2index ? pStudioHdr2()->m_nBoneFlexDriverCount : 0; }

    const mstudioboneflexdriver_t *BoneFlexDriver(int i) const {
        Assert(i >= 0 && i < BoneFlexDriverCount());
        return studiohdr2index ? pStudioHdr2()->pBoneFlexDriver(i) : nullptr;
    }

    int BodyGroupPresetCount() const { return studiohdr2index ? pStudioHdr2()->m_nBodyGroupPresetCount : 0; }

    const mstudiobodygrouppreset_t *BodyGroupPreset(int i) const {
        Assert(i >= 0 && i < BodyGroupPresetCount());
        return studiohdr2index ? pStudioHdr2()->pBodyGroupPreset(i) : nullptr;
    }

    void *VirtualModel() const { return studiohdr2index ? (void *) (pStudioHdr2()->virtualModel) : nullptr; }

    void SetVirtualModel(void *ptr) {
        Assert(studiohdr2index);
        if (studiohdr2index) { pStudioHdr2()->virtualModel = ptr; }
    }

    void *VertexBase() const { return studiohdr2index ? (void *) (pStudioHdr2()->pVertexBase) : nullptr; }

    void SetVertexBase(void *pVertexBase) const {
        Assert(studiohdr2index);
        if (studiohdr2index) { pStudioHdr2()->pVertexBase = pVertexBase; }
    }

    void *IndexBase() const { return studiohdr2index ? (void *) (pStudioHdr2()->pIndexBase) : nullptr; }

    void SetIndexBase(void *pIndexBase) const {
        Assert(studiohdr2index);
        if (studiohdr2index) { pStudioHdr2()->pIndexBase = pIndexBase; }
    }


    // NOTE: No room to add stuff? Up the .mdl file format version
    // [and move all fields in studiohdr2_t into studiohdr_t and kill studiohdr2_t],
    // or add your stuff to studiohdr2_t. See NumSrcBoneTransforms/SrcBoneTransform for the pattern to use.
    int unused2[1];

    studiohdr_t() {
    }

private:
    // No copy constructors allowed
    studiohdr_t(const studiohdr_t &vOther);

    friend struct virtualmodel_t;
};

// model vertex data accessor (defined here so vertexFileHeader_t and studiohdr_t can be used)
inline const mstudio_modelvertexdata_t *mstudiomodel_t::GetVertexData(void *pModelData) {
    const vertexFileHeader_t *pVertexHdr = CacheVertexData(pModelData);
    if (!pVertexHdr)
        return nullptr;

    vertexdata.pVertexData = pVertexHdr->GetVertexData();
    vertexdata.pTangentData = pVertexHdr->GetTangentData();
    vertexdata.pExtraData = nullptr;

    studiohdr_t *pModelHdr = (studiohdr_t *) pModelData;
    if (pModelHdr && pModelHdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) {
        vertexdata.pExtraData = pVertexHdr->GetExtraData();
    }
    else {
        vertexdata.pExtraData = nullptr;
    }

    if (!vertexdata.pVertexData)
        return nullptr;

    return &vertexdata;
}

// model thin vertex data accessor (defined here so vertexFileHeader_t can be used)
inline const thinModelVertices_t *mstudiomodel_t::GetThinVertexData(void *pModelData) {
    const vertexFileHeader_t *pVertexHdr = CacheVertexData(pModelData);
    if (!pVertexHdr)
        return nullptr;

    return pVertexHdr->GetThinVertexData();
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

class CFeModel;
class CSoftbody;
class CSoftbodyEnvironment;

class CStudioHdr {
public:
    CStudioHdr();

    CStudioHdr(const studiohdr_t *pStudioHdr);

    ~CStudioHdr() { Term(); }

    void Init(const studiohdr_t *pStudioHdr);

    void Term();

public:
    bool IsVirtual() { return (m_pVModel != nullptr); };
    bool IsValid() { return (m_pStudioHdr != nullptr); };
    bool IsReadyForAccess() const { return (m_pStudioHdr != nullptr); };
    virtualmodel_t *GetVirtualModel() const { return m_pVModel; };
    const studiohdr_t *GetRenderHdr() const { return m_pStudioHdr; };

    const studiohdr_t *pSeqStudioHdr(int sequence);

    const studiohdr_t *pAnimStudioHdr(int animation);

    //CFeModel *GetFeModel() { return m_pSoftbody? m_pSoftbody->GetFeModel(); }
    CSoftbody *GetSoftbody() const { return m_pSoftbody; }
    void SetSoftbody(CSoftbody *pSoftbody) { m_pSoftbody = pSoftbody; }

    CSoftbody *InitSoftbody(CSoftbodyEnvironment *pSoftbodyEnvironment);

    void FreeSoftbody();

private:
    mutable const studiohdr_t *m_pStudioHdr;
    mutable virtualmodel_t *m_pVModel;
    mutable CSoftbody *m_pSoftbody;

    const virtualmodel_t *ResetVModel(const virtualmodel_t *pVModel) const;

    const studiohdr_t *GroupStudioHdr(int group);

    mutable CUtlVector<const studiohdr_t *> m_pStudioHdrCache;

public:
    int numbones() const { return m_pStudioHdr->numbones; };
    const mstudiobone_t *pBone(int i) const { return m_pStudioHdr->pBone(i); };

    int RemapAnimBone(int iAnim, int iLocalBone) const; // maps local animations bone to global bone
    int RemapSeqBone(int iSequence, int iLocalBone) const; // maps local sequence bone to global bone

    bool SequencesAvailable() const;

    int GetNumSeq_Internal() const;

    int GetNumSeq() const {
        if (!m_pVModel)
            return m_pStudioHdr->numlocalseq;
        return GetNumSeq_Internal();
    }

    mstudioanimdesc_t &pAnimdesc_Internal(int i);

    mstudioanimdesc_t &pAnimdesc(int i) {
        if (!m_pVModel)
            return *m_pStudioHdr->pLocalAnimdesc(i);
        return pAnimdesc_Internal(i);
    }

    mstudioseqdesc_t &pSeqdesc_Internal(int iSequence);

    mstudioseqdesc_t &pSeqdesc(int iSequence) {
        if (!m_pVModel)
            return *m_pStudioHdr->pLocalSeqdesc(iSequence);

        return pSeqdesc_Internal(iSequence);
    }

    int iRelativeAnim_Internal(int baseseq, int relanim) const; // maps seq local anim reference to global anim index
    int iRelativeAnim(int baseseq, int relanim) const {
        if (!m_pVModel)
            return relanim;
        return iRelativeAnim_Internal(baseseq, relanim);
    }

    int iRelativeSeq(int baseseq, int relseq) const; // maps seq local seq reference to global seq index

    int GetSequenceActivity(int iSequence);

    void SetSequenceActivity(int iSequence, int iActivity);

    int GetActivityListVersion();

    void SetActivityListVersion(int version);

    int GetEventListVersion();

    void SetEventListVersion(int version);

    int GetNumAttachments() const;

    const mstudioattachment_t &pAttachment(int i);

    int GetAttachmentBone(int i);

    // used on my tools in hlmv, not persistant
    void SetAttachmentBone(int iAttachment, int iBone);

    int EntryNode(int iSequence);

    int ExitNode(int iSequence);

    char *pszNodeName(int iNode);

    // FIXME: where should this one be?
    int GetTransition(int iFrom, int iTo) const;

    int GetNumPoseParameters() const;

    const mstudioposeparamdesc_t &pPoseParameter(int i);

    int GetSharedPoseParameter(int iSequence, int iLocalPose) const;

    int GetNumIKAutoplayLocks() const;

    const mstudioiklock_t &pIKAutoplayLock(int i);

    int CountAutoplaySequences() const { return m_pStudioHdr->CountAutoplaySequences(); };

    int CopyAutoplaySequences(unsigned short *pOut, int outCount) const {
        return m_pStudioHdr->CopyAutoplaySequences(pOut, outCount);
    };
    int GetAutoplayList(unsigned short **pOut) const { return m_pStudioHdr->GetAutoplayList(pOut); };

    int GetNumBoneControllers() const { return m_pStudioHdr->numbonecontrollers; };
    mstudiobonecontroller_t *pBonecontroller(int i) const { return m_pStudioHdr->pBonecontroller(i); };

    int numikchains() const { return m_pStudioHdr->numikchains; };
    int GetNumIKChains() const { return m_pStudioHdr->numikchains; };
    mstudioikchain_t *pIKChain(int i) const { return m_pStudioHdr->pIKChain(i); };

    int numflexrules() const { return m_pStudioHdr->numflexrules; };
    mstudioflexrule_t *pFlexRule(int i) const { return m_pStudioHdr->pFlexRule(i); };

    int numflexdesc() const { return m_pStudioHdr->numflexdesc; };
    mstudioflexdesc_t *pFlexdesc(int i) const { return m_pStudioHdr->pFlexdesc(i); };

    LocalFlexController_t numflexcontrollers() const {
        return (LocalFlexController_t) m_pStudioHdr->numflexcontrollers;
    };

    mstudioflexcontroller_t *pFlexcontroller(LocalFlexController_t i) const {
        return m_pStudioHdr->pFlexcontroller(i);
    };

    int numflexcontrollerui() const { return m_pStudioHdr->numflexcontrollerui; };
    mstudioflexcontrollerui_t *pFlexcontrollerUI(int i) const { return m_pStudioHdr->pFlexControllerUI(i); };

    const char *name() const { return m_pStudioHdr->pszName(); }; // deprecated -- remove after full xbox merge
    const char *pszName() const { return m_pStudioHdr->pszName(); };

    int numbonecontrollers() const { return m_pStudioHdr->numbonecontrollers; };

    int numhitboxsets() const { return m_pStudioHdr->numhitboxsets; };
    mstudiohitboxset_t *pHitboxSet(int i) const { return m_pStudioHdr->pHitboxSet(i); };

    mstudiobbox_t *pHitbox(int i, int set) const { return m_pStudioHdr->pHitbox(i, set); };
    int iHitboxCount(int set) const { return m_pStudioHdr->iHitboxCount(set); };

    int numbodyparts() const { return m_pStudioHdr->numbodyparts; };
    mstudiobodyparts_t *pBodypart(int i) const { return m_pStudioHdr->pBodypart(i); };

    int numskinfamilies() const { return m_pStudioHdr->numskinfamilies; }

    Vector eyeposition() const { return m_pStudioHdr->eyeposition; };

    int flags() const { return m_pStudioHdr->flags; };

    char *const pszSurfaceProp() const { return m_pStudioHdr->pszSurfaceProp(); };
    int GetSurfaceProp() const { return m_pStudioHdr->surfacepropLookup; }

    float mass() const { return m_pStudioHdr->mass; };
    ContentType contents() const { return m_pStudioHdr->contents; }

    const uint8_t *GetBoneTableSortedByName() const { return m_pStudioHdr->GetBoneTableSortedByName(); };

    Vector illumposition() const { return m_pStudioHdr->illumposition; };

    Vector hull_min() const { return m_pStudioHdr->hull_min; }; // ideal movement hull size
    Vector hull_max() const { return m_pStudioHdr->hull_max; };

    Vector view_bbmin() const { return m_pStudioHdr->view_bbmin; }; // clipping bounding box
    Vector view_bbmax() const { return m_pStudioHdr->view_bbmax; };

    int numtextures() const { return m_pStudioHdr->numtextures; };

    int IllumPositionAttachmentIndex() const { return m_pStudioHdr->IllumPositionAttachmentIndex(); }

    float MaxEyeDeflection() const { return m_pStudioHdr->MaxEyeDeflection(); }

    mstudiolinearbone_t *pLinearBones() const { return m_pStudioHdr->pLinearBones(); }

    int BoneFlexDriverCount() const { return m_pStudioHdr->BoneFlexDriverCount(); }
    const mstudioboneflexdriver_t *BoneFlexDriver(int i) const { return m_pStudioHdr->BoneFlexDriver(i); }

    int GetNumBodyGroupPresets() const { return m_pStudioHdr->BodyGroupPresetCount(); };
    const mstudiobodygrouppreset_t *GetBodyGroupPreset(int i) const { return m_pStudioHdr->BodyGroupPreset(i); }

public:
    int IsSequenceLooping(int iSequence);

    float GetSequenceCycleRate(int iSequence);

    void RunFlexRules(const float *src, float *dest);

    void RunFlexRulesOld(const float *src, float *dest);

    void RunFlexRulesNew(const float *src, float *dest);

public:
    BoneFlags boneFlags(int iBone) const { return m_boneFlags[iBone]; }

    void setBoneFlags(int iBone, BoneFlags flags);

    void clearBoneFlags(int iBone, BoneFlags flags);

    int boneParent(int iBone) const { return m_boneParent[iBone]; }

private:
    CUtlVector<BoneFlags> m_boneFlags;
    CUtlVector<int> m_boneParent;

public:
    // This class maps an activity to sequences allowed for that activity, accelerating the resolution
    // of SelectWeightedSequence(), especially on PowerPC. Iterating through every sequence
    // attached to a model turned out to be a very destructive cache access pattern on 360.
    //
    // I've encapsulated this behavior inside a nested class for organizational reasons; there is
    // no particular programmatic or efficiency benefit to it. It just makes clearer what particular
    // code in the otherwise very complicated StudioHdr class has to do with this particular
    // optimization, and it lets you collapse the whole definition down to a single line in Visual
    // Studio.
    class CActivityToSequenceMapping /* final */
    {
    public:
        // A tuple of a sequence and its corresponding weight. Lists of these correspond to activities.
        struct SequenceTuple {
            short seqnum;
            short weight; // the absolute value of the weight from the sequence header
            CUtlSymbol *pActivityModifiers; // list of activity modifier symbols
            int iNumActivityModifiers;
        };

        // The type of the hash's stored data, a composite of both key and value
        // (because that's how CUtlHash works):
        // key: an int, the activity #
        // values: an index into the m_pSequenceTuples array, a count of the
        // total sequences present for an activity, and the sum of their
        // weights.
        // Note this struct is 128-bits wide, exactly coincident to a PowerPC
        // cache line and VMX register. Please consider very carefully the
        // performance implications before adding any additional fields to this.
        // You could probably do away with totalWeight if you really had to.
        struct HashValueType {
            // KEY (hashed)
            int activityIdx;

            // VALUE (not hashed)
            int startingIdx;
            int count;
            int totalWeight;

            HashValueType(int _actIdx, int _stIdx, int _ct, int _tW) : activityIdx(_actIdx), startingIdx(_stIdx),
                                                                       count(_ct), totalWeight(_tW) {
            }

            // default constructor (ought not to be actually used)
            HashValueType() : activityIdx(-1), startingIdx(-1), count(-1), totalWeight(-1) {
                AssertMsg(false, "Don't use default HashValueType()!");
            }


            class HashFuncs {
            public:
                // dummy constructor (gndn)
                HashFuncs(int) {
                }

                // COMPARE
                // compare two entries for uniqueness. We should never have two different
                // entries for the same activity, so we only compare the activity index;
                // this allows us to use the utlhash as a dict by constructing dummy entries
                // as hash lookup keys.
                bool operator()(const HashValueType &lhs, const HashValueType &rhs) const {
                    return lhs.activityIdx == rhs.activityIdx;
                }

                // HASH
                // We only hash on the activity index; everything else is data.
                unsigned int operator()(const HashValueType &item) const {
                    return HashInt(item.activityIdx);
                }
            };
        };

        typedef CUtlHash<HashValueType, HashValueType::HashFuncs, HashValueType::HashFuncs> ActivityToValueIdxHash;

        // These must be here because IFM does not compile/link studio.cpp (?!?)

        // ctor
        CActivityToSequenceMapping()
            : m_pSequenceTuples(nullptr), m_iSequenceTuplesCount(0), m_ActToSeqHash(8, 0, 0), m_expectedVModel(nullptr),
              m_pStudioHdr(nullptr) {
        };

        // dtor -- not virtual because this class has no inheritors
        ~CActivityToSequenceMapping() {
            if (m_pSequenceTuples != nullptr) {
                if (m_pSequenceTuples->pActivityModifiers != nullptr) {
                    delete[] m_pSequenceTuples->pActivityModifiers;
                }
                delete[] m_pSequenceTuples;
            }
        }

        /// Get the list of sequences for an activity. Returns the pointer to the
        /// first sequence tuple. Output parameters are a count of sequences present,
        /// and the total weight of all the sequences. (it would be more LHS-friendly
        /// to return these on registers, if only C++ offered more than one return
        /// value....)
        const SequenceTuple *GetSequences(int forActivity, int *outSequenceCount, int *outTotalWeight);

        /// The number of sequences available for an activity.
        int NumSequencesForActivity(int forActivity);

        static CActivityToSequenceMapping *FindMapping(const CStudioHdr *pstudiohdr);

        static void ReleaseMapping(CActivityToSequenceMapping *pMap);

        static void ResetMappings();

    private:
        /// Allocate my internal array. (It is freed in the destructor.) Also,
        /// build the hash of activities to sequences and populate m_pSequenceTuples.
        void Initialize(const CStudioHdr *pstudiohdr);

        /// Force Initialize() to occur again, even if it has already occured.
        void Reinitialize(CStudioHdr *pstudiohdr);

        /// A more efficient version of the old SelectWeightedSequence() function in animation.cpp.
        int SelectWeightedSequence(CStudioHdr *pstudiohdr, int activity, int curSequence);

        // selects the sequence with the most matching modifiers
        int SelectWeightedSequenceFromModifiers(CStudioHdr *pstudiohdr, int activity, CUtlSymbol *pActivityModifiers,
                                                int iModifierCount);

        // Actually a big array, into which the hash values index.
        SequenceTuple *m_pSequenceTuples;
        unsigned int m_iSequenceTuplesCount; // (size of the whole array)

        // we don't store an outer pointer because we can't initialize it at construction time
        // (warning c4355) -- there are ways around this but it's easier to just pass in a
        // pointer to the CStudioHdr when we need it, since this class isn't supposed to
        // export its interface outside the studio header anyway.
        // CStudioHdr * const m_pOuter;

        ActivityToValueIdxHash m_ActToSeqHash;

        const studiohdr_t *m_pStudioHdr;

        // we store these so we can know if the contents of the studiohdr have changed
        // from underneath our feet (this is an emergency data integrity check)
        const void *m_expectedVModel;


        // double-check that the data I point to hasn't changed
        bool ValidateAgainst(const CStudioHdr * RESTRICT pstudiohdr) RESTRICT;

        void SetValidation(const CStudioHdr *RESTRICT pstudiohdr) RESTRICT;

        friend class CStudioHdr;
    };

    CActivityToSequenceMapping *m_pActivityToSequence;

    void InitActivityToSequence() {
        if (!m_pActivityToSequence) {
            m_pActivityToSequence = CActivityToSequenceMapping::FindMapping(this);
        }
    }

    /// A more efficient version of the old SelectWeightedSequence() function in animation.cpp.
    /// Returns -1 on failure to find a sequence
    int SelectWeightedSequence(int activity, int curSequence) {
        InitActivityToSequence();
        return m_pActivityToSequence->SelectWeightedSequence(this, activity, curSequence);
    }

    int SelectWeightedSequenceFromModifiers(int activity, CUtlSymbol *pActivityModifiers, int iModifierCount) {
        InitActivityToSequence();
        return m_pActivityToSequence->SelectWeightedSequenceFromModifiers(
            this, activity, pActivityModifiers, iModifierCount);
    }

    /// True iff there is at least one sequence for the given activity.
    bool HaveSequenceForActivity(int activity) {
        InitActivityToSequence();
        return (m_pActivityToSequence->NumSequencesForActivity(activity) > 0);
    }

    // Force this CStudioHdr's activity-to-sequence mapping to be reinitialized
    void ReinitializeSequenceMapping() {
        if (m_pActivityToSequence) {
            CActivityToSequenceMapping::ReleaseMapping(m_pActivityToSequence);
            m_pActivityToSequence = nullptr;
        }
        m_pActivityToSequence = CActivityToSequenceMapping::FindMapping(this);
    }

public:
    int LookupSequence(const char *pszName);

private:
    CUtlDict<int, int> m_namedSequence;

#ifdef STUDIO_ENABLE_PERF_COUNTERS

public:
    void ClearPerfCounters() {
        m_nPerfAnimatedBones = 0;
        m_nPerfUsedBones = 0;
        m_nPerfAnimationLayers = 0;
    };

    // timing info
    mutable int m_nPerfAnimatedBones;
    mutable int m_nPerfUsedBones;
    mutable int m_nPerfAnimationLayers;
#endif
};

/*
class CModelAccess
{
public:
	CModelAccess(CStudioHdr *pSemaphore)
	 : m_pStudioHdr(pSemaphore)
	{
		m_pStudioHdr->IncrementAccess();
	}

	~CModelAccess()
	{
		m_pStudioHdr->DecrementAccess();
	}

private:
	CStudioHdr *m_pStudioHdr;
};

#define ENABLE_MODEL_ACCESS( a ) \
	CModelAccess ModelAccess##__LINE__( a->m_pStudioHdr )
*/

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

struct flexweight_t {
    int key;
    float weight;
    float influence;
};

struct flexsetting_t {
    int nameindex;

    char *pszName() const {
        return (char *) (((uint8_t *) this) + nameindex);
    }

    // Leaving this for legacy support
    int obsolete1;

    // Number of flex settings
    int numsettings;
    int index;

    // OBSOLETE:
    int obsolete2;

    // Index of start of contiguous array of flexweight_t structures
    int settingindex;

    //-----------------------------------------------------------------------------
    // Purpose: Retrieves a pointer to the flexweight_t, including resolving
    //  any markov chain hierarchy.  Because of this possibility, we return
    //  the number of settings in the weights array returned.  We'll generally
    //  call this function with i == 0
    // Input  : *base -
    //			i -
    //			**weights -
    // Output : int
    //-----------------------------------------------------------------------------
    inline int psetting(uint8_t *base, int i, flexweight_t **weights) const;
};


struct flexsettinghdr_t {
    int id;
    int version;

    const char *pszName() const { return name; }
    char name[64];
    int length;

    int numflexsettings;
    int flexsettingindex;

    flexsetting_t *pSetting(int i) const {
        return (flexsetting_t *) (((uint8_t *) this) + flexsettingindex) + i;
    };
    int nameindex;

    // look up flex settings by "index"
    int numindexes;
    int indexindex;

    flexsetting_t *pIndexedSetting(int index) const {
        if (index < 0 || index >= numindexes) {
            return nullptr;
        }

        int i = *((int *) (((uint8_t *) this) + indexindex) + index);

        if (i == -1) {
            return nullptr;
        }

        return pSetting(i);
    }

    // index names of "flexcontrollers"
    int numkeys;
    int keynameindex;

    char *pLocalName(int i) const {
        return (char *) (((uint8_t *) this) + *((int *) (((uint8_t *) this) + keynameindex) + i));
    };

    int keymappingindex;
    int *pLocalToGlobal(int i) const { return (int *) (((uint8_t *) this) + keymappingindex) + i; };
    int LocalToGlobal(int i) const { return *pLocalToGlobal(i); };
};

//-----------------------------------------------------------------------------
// Purpose: Retrieves a pointer to the flexweight_t.
// Input  : *base - flexsettinghdr_t * pointer
//			i - index of flex setting to retrieve
//			**weights - destination for weights array starting at index i.
// Output : int
//-----------------------------------------------------------------------------
inline int flexsetting_t::psetting(uint8_t *base, int i, flexweight_t **weights) const {
    // Grab array pointer
    *weights = (flexweight_t *) (((uint8_t *) this) + settingindex) + i;
    // Return true number of settings
    return numsettings;
};


//-----------------------------------------------------------------------------
// For a given flex controller ui struct, these return the index of the
// studiohdr_t flex controller that correspond to the the left and right
// flex controllers if the ui controller is a stereo control.
// nWayValueIndex returns the index of the flex controller that is the value
// flex controller for an NWAY combination
// If these functions are called and the ui controller isn't of the type
// specified then -1 is returned
//-----------------------------------------------------------------------------
inline int mstudioflexcontrollerui_t::controllerIndex(const CStudioHdr &cStudioHdr) const {
    return !stereo ? pController() - cStudioHdr.pFlexcontroller((LocalFlexController_t) 0) : -1;
}


inline int mstudioflexcontrollerui_t::rightIndex(const CStudioHdr &cStudioHdr) const {
    return stereo ? pRightController() - cStudioHdr.pFlexcontroller((LocalFlexController_t) 0) : -1;
}


inline int mstudioflexcontrollerui_t::leftIndex(const CStudioHdr &cStudioHdr) const {
    return stereo ? pLeftController() - cStudioHdr.pFlexcontroller((LocalFlexController_t) 0) : -1;
}


inline int mstudioflexcontrollerui_t::nWayValueIndex(const CStudioHdr &cStudioHdr) const {
    return remaptype == Nway
               ? pNWayValueController() - cStudioHdr.pFlexcontroller((LocalFlexController_t) 0)
               : -1;
}


inline const mstudioflexcontroller_t *mstudioflexcontrollerui_t::pController(int index) const {
    if (index < 0 || index > Count())
        return nullptr;

    if (remaptype == Nway) {
        if (stereo)
            return (mstudioflexcontroller_t *) ((char *) this) + *(&szindex0 + index);

        if (index == 0)
            return pController();

        if (index == 1)
            return pNWayValueController();

        return nullptr;
    }

    if (index > 1)
        return nullptr;

    if (stereo)
        return (mstudioflexcontroller_t *) ((char *) this) + *(&szindex0 + index);

    if (index > 0)
        return nullptr;

    return pController();
}

// Insert this code anywhere that you need to allow for conversion from an old STUDIO_VERSION to a new one.
// If we only support the current version, this function should be empty.
inline bool Studio_ConvertStudioHdrToNewVersion(studiohdr_t *pStudioHdr) {
    COMPILE_TIME_ASSERT(STUDIO_VERSION == 49); //  put this to make sure this code is updated upon changing version.

    int version = pStudioHdr->version;
    if (version == STUDIO_VERSION)
        return true;

    bool bResult = true;
    if (version < 46) {
        // some of the anim index data is incompatible
        for (int i = 0; i < pStudioHdr->numlocalanim; i++) {
            mstudioanimdesc_t *pAnim = (mstudioanimdesc_t *) pStudioHdr->pLocalAnimdesc(i);

            // old ANI files that used sections (v45 only) are not compatible
            if (pAnim->sectionframes != 0) {
                // zero most everything out
                memset(&(pAnim->numframes), 0, (uint8_t *) (pAnim + 1) - (uint8_t *) &(pAnim->numframes));

                pAnim->numframes = 1;
                pAnim->animblock = -1; // disable animation fetching
                bResult = false;
            }
        }
    }

    if (version < 47) {
        // now used to contain zeroframe cache data, make sure it's empty
        if (pStudioHdr->unused4 != 0) {
            pStudioHdr->unused4 = 0;
            bResult = false;
        }
        for (int i = 0; i < pStudioHdr->numlocalanim; i++) {
            mstudioanimdesc_t *pAnim = (mstudioanimdesc_t *) pStudioHdr->pLocalAnimdesc(i);
            pAnim->zeroframeindex = 0;
            pAnim->zeroframespan = 0;
        }
    }
    else if (version == 47) {
        // clear out stale version of zeroframe cache data
        for (int i = 0; i < pStudioHdr->numlocalanim; i++) {
            mstudioanimdesc_t *pAnim = (mstudioanimdesc_t *) pStudioHdr->pLocalAnimdesc(i);
            if (pAnim->zeroframeindex != 0) {
                pAnim->zeroframeindex = 0;
                pAnim->zeroframespan = 0;
                bResult = false;
            }
        }
    }

    if (version < 49) {
        // remove any frameanim flag settings that might be stale
        for (int i = 0; i < pStudioHdr->numlocalanim; i++) {
            mstudioanimdesc_t *pAnim = (mstudioanimdesc_t *) pStudioHdr->pLocalAnimdesc(i);
            if (HasFlag(pAnim->flags, StudioSequenceFlags::FrameAnim)) {
                pAnim->flags &= ~StudioSequenceFlags::FrameAnim;
                bResult = false;
            }
        }
    }
    // for now, just slam the version number since they're compatible
    pStudioHdr->version = STUDIO_VERSION;

    return bResult;
}

// must be run to fixup with specified rootLOD
inline void Studio_SetRootLOD(studiohdr_t *pStudioHdr, int rootLOD) {
    // honor studiohdr restriction of root lod in case requested root lod exceeds restriction.
    if (pStudioHdr->numAllowedRootLODs > 0 &&
        rootLOD >= pStudioHdr->numAllowedRootLODs) {
        rootLOD = pStudioHdr->numAllowedRootLODs - 1;
    }

    // run the lod fixups that culls higher detail lods
    // vertexes are external, fixups ensure relative offsets and counts are cognizant of shrinking data
    // indexes are built in lodN..lod0 order so higher detail lod data can be truncated at load
    // the fixup lookup arrays are filled (or replicated) to ensure all slots valid
    int vertexindex = 0;
    int tangentsindex = 0;
    int bodyPartID;
    for (bodyPartID = 0; bodyPartID < pStudioHdr->numbodyparts; bodyPartID++) {
        mstudiobodyparts_t *pBodyPart = pStudioHdr->pBodypart(bodyPartID);
        int modelID;
        for (modelID = 0; modelID < pBodyPart->nummodels; modelID++) {
            mstudiomodel_t *pModel = pBodyPart->pModel(modelID);
            int totalMeshVertexes = 0;
            int meshID;
            for (meshID = 0; meshID < pModel->nummeshes; meshID++) {
                mstudiomesh_t *pMesh = pModel->pMesh(meshID);

                // get the fixup, vertexes are reduced
                pMesh->numvertices = pMesh->vertexdata.numLODVertexes[rootLOD];
                pMesh->vertexoffset = totalMeshVertexes;
                totalMeshVertexes += pMesh->numvertices;
            }

            // stay in sync
            pModel->numvertices = totalMeshVertexes;
            pModel->vertexindex = vertexindex;
            pModel->tangentsindex = tangentsindex;

            vertexindex += totalMeshVertexes * sizeof(mstudiovertex_t);
            tangentsindex += totalMeshVertexes * sizeof(Vector4D);
        }
    }

    // track the set desired configuration
    pStudioHdr->rootLOD = rootLOD;
}

// Determines allocation requirements for vertexes
inline int Studio_VertexDataSize(const vertexFileHeader_t *pVvdHdr, int rootLOD, bool bNeedsTangentS,
                                 bool bHasExtraData) {
    // the quantity of vertexes necessary for root lod and all lower detail lods
    // add one extra vertex to each section
    // the extra vertex allows prefetch hints to read ahead 1 vertex without faulting
    int numVertexes = pVvdHdr->numLODVertexes[rootLOD] + 1;
    int dataLength = pVvdHdr->vertexDataStart + numVertexes * sizeof(mstudiovertex_t);
    if (bNeedsTangentS) {
        dataLength += numVertexes * sizeof(Vector4D);
    }

    if (bHasExtraData) {
        ExtraVertexAttributesHeader_t *pExtraDataHdr = (ExtraVertexAttributesHeader_t *) (
            (uint8_t *) pVvdHdr + pVvdHdr->tangentDataStart + (numVertexes - 1) * sizeof(Vector4D));
        dataLength += pExtraDataHdr->m_totalbytes;
    }

    // allocate this much
    return dataLength;
}

// Load the minimum quantity of verts and run fixups
inline int Studio_LoadVertexes(const vertexFileHeader_t *pTempVvdHdr, vertexFileHeader_t *pNewVvdHdr, int rootLOD,
                               bool bNeedsTangentS, bool bHasExtraData) {
    int i;
    int target;
    int numVertexes;
    vertexFileFixup_t *pFixupTable;
    ExtraVertexAttributeIndex_t *pExtraIndex = nullptr;
    ExtraVertexAttributeIndex_t *pNewExtraIndex = nullptr;
    ExtraVertexAttributesHeader_t *pExtraHeader = nullptr;
    ExtraVertexAttributesHeader_t *pNewExtraHeader = nullptr;

    numVertexes = pTempVvdHdr->numLODVertexes[rootLOD];

    // copy all data up to start of vertexes
    memcpy((void *) pNewVvdHdr, (void *) pTempVvdHdr, pTempVvdHdr->vertexDataStart);

    for (i = 0; i < rootLOD; i++) {
        pNewVvdHdr->numLODVertexes[i] = pNewVvdHdr->numLODVertexes[rootLOD];
    }

    // fixup data starts
    if (bNeedsTangentS) {
        // tangent data follows possibly reduced vertex data
        pNewVvdHdr->tangentDataStart = pNewVvdHdr->vertexDataStart + numVertexes * sizeof(mstudiovertex_t);
    }
    else {
        // no tangent data will be available, mark for identification
        pNewVvdHdr->tangentDataStart = 0;
    }

    if (bHasExtraData) {
        pExtraHeader = (ExtraVertexAttributesHeader_t *) (
            (uint8_t *) pTempVvdHdr + pTempVvdHdr->tangentDataStart + numVertexes * sizeof(Vector4D));
        pExtraIndex = (ExtraVertexAttributeIndex_t *) (pExtraHeader + 1);

        pNewExtraHeader = (ExtraVertexAttributesHeader_t *) (
            (uint8_t *) pNewVvdHdr + pNewVvdHdr->tangentDataStart + numVertexes * sizeof(Vector4D));
        pNewExtraIndex = (ExtraVertexAttributeIndex_t *) (pNewExtraHeader + 1);
    }

    if (!pNewVvdHdr->numFixups) {
        // fixups not required
        // transfer vertex data
        memcpy(
            (uint8_t *) pNewVvdHdr + pNewVvdHdr->vertexDataStart,
            (uint8_t *) pTempVvdHdr + pTempVvdHdr->vertexDataStart,
            numVertexes * sizeof(mstudiovertex_t));

        if (bNeedsTangentS) {
            // transfer tangent data to cache memory
            memcpy(
                (uint8_t *) pNewVvdHdr + pNewVvdHdr->tangentDataStart,
                (uint8_t *) pTempVvdHdr + pTempVvdHdr->tangentDataStart,
                numVertexes * sizeof(Vector4D));
        }

        if (bHasExtraData) {
            // Memcpy whole blob of extra bytes
            memcpy(pNewExtraHeader, pExtraHeader, pExtraHeader->m_totalbytes);
        }

        return numVertexes;
    }

    // fixups required
    // re-establish mesh ordered vertexes into cache memory, according to table
    target = 0;
    pFixupTable = (vertexFileFixup_t *) ((uint8_t *) pTempVvdHdr + pTempVvdHdr->fixupTableStart);
    for (i = 0; i < pTempVvdHdr->numFixups; i++) {
        if (pFixupTable[i].lod < rootLOD) {
            // working bottom up, skip over copying higher detail lods
            continue;
        }

        // copy vertexes
        memcpy(
            (mstudiovertex_t *) ((uint8_t *) pNewVvdHdr + pNewVvdHdr->vertexDataStart) + target,
            (mstudiovertex_t *) ((uint8_t *) pTempVvdHdr + pTempVvdHdr->vertexDataStart) + pFixupTable[i].
            sourceVertexID,
            pFixupTable[i].numVertexes * sizeof(mstudiovertex_t));

        if (bNeedsTangentS) {
            // copy tangents
            memcpy(
                (Vector4D *) ((uint8_t *) pNewVvdHdr + pNewVvdHdr->tangentDataStart) + target,
                (Vector4D *) ((uint8_t *) pTempVvdHdr + pTempVvdHdr->tangentDataStart) + pFixupTable[i].sourceVertexID,
                pFixupTable[i].numVertexes * sizeof(Vector4D));
        }

        if (bHasExtraData) {
            // Memcpy header and index records
            memcpy(pNewExtraHeader, pExtraHeader,
                   sizeof(ExtraVertexAttributesHeader_t) + sizeof(ExtraVertexAttributeIndex_t) * pExtraHeader->m_count);
            // copy extra data
            for (int e = 0; e < pExtraHeader->m_count; ++e) {
                int offset = pExtraIndex[e].m_offset;
                int bytesPerVertex = pExtraIndex[e].m_bytes;
                memcpy((uint8_t *) pNewExtraHeader + offset + target * bytesPerVertex,
                       (uint8_t *) pExtraHeader + offset + pFixupTable[i].sourceVertexID * bytesPerVertex,
                       pFixupTable[i].numVertexes * bytesPerVertex);
            }
        }

        // data is placed consecutively
        target += pFixupTable[i].numVertexes;
    }

    pNewVvdHdr->numFixups = 0;
    pNewVvdHdr->fixupTableStart = 0; // otherwise PS3 will be confusing it for EDGE DMA INPUT

    return target;
}
