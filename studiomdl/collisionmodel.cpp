//========= Copyright ? 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Builds physics collision models from studio model source
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

// NOTE: The term joint here is used to mean a bone, collision model, and a joint.
// Each "joint" is the collision geometry at a named bone (or set of bones that have been merged)
// and the joint (with constraints) between that set and its parent.  The root "joint" has
// no constraints.
// I chose to refer to them as joints to avoid confusion.  Yes they encompass bones and joints,
// but they use the same names, and the data is actually linked.

#include <stdio.h>
#include <stdlib.h>

#include "studioconstraints.h"
#include "collisionmodelsource.h"
#include "studiomdl/collisionmodel.h"
//#include "physics2collision.h"
#include "common/cmdlib.h"
#include "common/scriplib.h"
#include "mathlib/mathlib.h"
#include "studio.h"
#include "studiomdl/studiomdl.h"
#include "studiocollision.h"
#include "studiosurfaceprops.h"
#include "phyfile.h"
#include <vector>
#include "vcollide_parse.h"
#include "tier1/strtools.h"
#include "tier1/keyvalues.h"
#include "datamodel/dmattributevar.h"
#include "datamodel/dmelement.h"
#include "interfaces/interfaces.h"
#include "mdlobjects/dmecollisionjoints.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"
#include "studiomdl/output_files.hpp"
#include "studiomdl/apply_collision.hpp"
#include <cstring>
#include <span>
#include <filesystem>

#include <format>
#include <iterator>
#include <string>

//#include "vphysics2_interface.h"

#define SURFPROP_MANIFEST "scripts/surfaceproperties_manifest.txt";
#define SUPPORT_EMBEDDED_SURFPROP 0

// Finds the bone index for a particular source
extern int FindLocalBoneNamed(StudioMdlState &ctx, const s_source_t *pSource, const char *pName);

// these functions just wrap atoi/atof and check for NULL
static float Safe_atof(const char *pString);

static int Safe_atoi(const char *pString);

//-----------------------------------------------------------------------------
// Purpose: Contains a single convex element of a physical collision system
//-----------------------------------------------------------------------------
class CPhysCollisionModel {
public:
    CPhysCollisionModel(void) {
        memset(this, 0, sizeof(*this));
    }

    const char *m_parent;
    const char *m_name;

    // physical properties stored on disk
    float m_mass;
    float m_volume;
    float m_surfaceArea;
    float m_damping;
    float m_rotdamping;
    float m_inertia;
    float m_dragCoefficient;

    // these tune the model building process, they don't go in the file
    float m_massBias;

    StudioCollide_t *m_pCollisionData;
    CPhysCollisionModel *m_pNext;
};

enum jointlimit_t {
    JOINT_FREE = 0,
    JOINT_FIXED = 1,
    JOINT_LIMIT = 2,
};


//-----------------------------------------------------------------------------
// Purpose: element of a list of constraints for a jointed model
//-----------------------------------------------------------------------------
class CJointConstraint {
public:
    CJointConstraint(void) {
        m_pJointName = NULL;
    }

    CJointConstraint(const char *pName, int axis, jointlimit_t type, float min, float max, float friction)
        : m_axis(axis), m_jointType(type), m_limitMin(min), m_limitMax(max), m_friction(friction) {
        m_pJointName = pName;
    }

    const char *m_pJointName;
    int m_axis;
    jointlimit_t m_jointType;
    float m_limitMin;
    float m_limitMax;
    float m_friction;

    CJointConstraint *m_pNext;
};

struct mergelist_t {
    std::string pParent;
    std::string pChild;
};

struct collisionpair_t {
    int obj0;
    int obj1;
    const char *pName0;
    const char *pName1;
    collisionpair_t *pNext;
};


// Returns the index to pName in ctx.bones.bonetable
static int FindBoneInTable(StudioMdlState &ctx, const char *pName) {
    return findGlobalBone(ctx, pName);
}


//-----------------------------------------------------------------------------
// Purpose: Contains a complete physical joint system with constraint relationships
//-----------------------------------------------------------------------------
// This class is really just a namespace for a set of globals...
class CJointedModel : public CCollisionModelSource {
public:
    int m_collisionCount;
    CPhysCollisionModel *m_pCollisionList;
    collisionpair_t *m_pCollisionPairs;
    float m_totalMass;
    CJointConstraint *m_pConstraintList;
    int m_constraintCount;
    int m_totalVerts;
    bool m_isMassCenterForced;
    bool m_noSelfCollisions;
    bool m_remove2d;
    Vector m_massCenterForced;

    float m_defaultDamping;
    float m_defaultRotdamping;
    float m_defaultInertia;
    float m_defaultDrag;
    std::vector<char> m_textCommands;
    std::vector<mergelist_t> m_mergeList;

    // What the lists below point to (and the names they refer to): owned here, freed with the model. Unlinking an
    // element from its list never frees it.
    std::vector<std::unique_ptr<CPhysCollisionModel>> m_OwnedCollisionModels;
    std::deque<collisionpair_t> m_CollisionPairStorage;
    std::deque<CJointConstraint> m_ConstraintStorage;
    std::deque<std::string> m_NameStorage;

    explicit CJointedModel(StudioMdlState &ctx);

    void SetSource(s_source_t *pmodel);

    void SetOverrideName(const char *pName) {
        m_OverrideName = pName ? pName : "";
    }

    void AddMergeCommand(char const *pParent, char const *pChild);

    int BoneIndex(const char *pName);

    void AppendCollisionModel(CPhysCollisionModel *pCollide);

    void UnlinkCollisionModel(CPhysCollisionModel *pCollide);

    CPhysCollisionModel *GetCollisionModel(const char *pName);

    void AppendCollisionPair(const char *pName0, const char *pName1);

    void RemoveCollisionPair(const char *pName0, const char *pName1);

    void AddConstraint(const char *pJointName, int axis, jointlimit_t jointType, float limitMin, float limitMax,
                       float friction);

    int CollisionIndex(const char *pName);

    void SortCollisionList(void);

    void ForceMassCenter(const Vector &centerOfMass);

    void AllowConcave(void) { m_allowConcave = true; }
    void AllowConcaveJoints() { m_allowConcaveJoints = true; }
    void Remove2DConvex() { m_remove2d = true; }
    void SetMaxConvex(int newMax) { m_maxConvex = newMax; }

    void DefaultDamping(float damping);

    void DefaultRotdamping(float rotdamping);

    void DefaultInertia(float inertia);

    void DefaultDrag(float drag);

    void SetTotalMass(float mass);

    void SetAutoMass(void);

    void SetNoSelfCollisions();

    void SetCollisionModelDefaults(CPhysCollisionModel *pModel);

    CPhysCollisionModel *InitCollisionModel(const char *pJointName);

    void JointDamping(const char *pJointName, float damping);

    void JointRotdamping(const char *pJointName, float rotdamping);

    void JointInertia(const char *pJointName, float inertia);

    void JointMassBias(const char *pJointName, float massBias);

    void FixBoneList();

    const char *FixParent(const char *pParentName);

    void FixCollisionHierarchy();

    int ProcessSingleBody();

    int ProcessJointedModel();

    void AddConvexSrc(ScriptTokenizer &qc, const char *szFileName);

    void AddText(const std::string_view text) {
        m_textCommands.insert(m_textCommands.end(), text.begin(), text.end());
    }

    void ComputeMass(void);

    float m_flFrictionTimeIn;
    float m_flFrictionTimeOut;
    float m_flFrictionTimeHold = 0.0f;
    int m_iMinAnimatedFriction;
    int m_iMaxAnimatedFriction;
    bool m_bHasAnimatedFriction;

    // $weldposition / $weldnormal: verts closer than this with normals more parallel than this are welded
    float m_weldVertEpsilon = 0.0f;
    float m_weldNormalEpsilon = 0.999f;
    // $convexhullcountoverride: allow more than m_maxConvex pieces
    bool m_convexHullCountOverride = false;
};


StudioMdlCollisionState::StudioMdlCollisionState() = default;
StudioMdlCollisionState::~StudioMdlCollisionState() = default;

// The compile's collision model: created by the first command that configures it, read by the build and write steps
static CJointedModel &JointedModel(StudioMdlState &ctx) {
    if (!ctx.collision.jointedModel)
        ctx.collision.jointedModel = std::make_unique<CJointedModel>(ctx);
    return *ctx.collision.jointedModel;
}

CJointedModel::CJointedModel(StudioMdlState &ctx) : CCollisionModelSource(ctx) {
    m_pModel = NULL;

    for (int i = 0; i <= MAX_EXTRA_COLLISION_MODELS; i++) {
        m_ExtraModels[i].m_pSrc = NULL;
        m_ExtraModels[i].m_bConcave = false;
    }

    m_bRootCollisionIsEmpty = false;

    m_collisionCount = 0;
    m_pCollisionList = NULL;
    m_pCollisionPairs = NULL;
    m_totalMass = 1.0;

    m_bonemap.clear();
    m_pConstraintList = NULL;
    m_constraintCount = 0;

    m_totalVerts = 0;

    // UNDONE: Move these defaults elsewhere?  They are all overrideable by the QC/script
    // These defaults are also in the CDmeCollisionModel/CDmeCollisionJoints
    m_defaultDamping = 0;
    m_defaultRotdamping = 0;
    m_defaultInertia = 1.0;
    m_defaultDrag = -1;
    m_allowConcave = false;
    m_allowConcaveJoints = false;
    m_remove2d = false;
    m_maxConvex = 40;
    m_isMassCenterForced = false;
    m_noSelfCollisions = false;
    m_massCenterForced.Init();

    m_flFrictionTimeIn = 0.0f;
    m_flFrictionTimeOut = 0.0f;
    m_iMinAnimatedFriction = 1.0f;
    m_iMaxAnimatedFriction = 1.0f;
    m_bHasAnimatedFriction = false;
}


void CJointedModel::SetSource(s_source_t *pmodel) {
    m_pModel = pmodel;
    InitBoneMap();
    m_totalVerts = pmodel->numvertices;
}


void CJointedModel::AddMergeCommand(char const *pParent, char const *pChild) {
    mergelist_t &merge = m_mergeList.emplace_back();
    merge.pParent = pParent;
    merge.pChild = pChild;
}


int CJointedModel::BoneIndex(const char *pName) {
    pName = RenameBone(m_ctx, pName);
    for (int boneIndex = 0; boneIndex < m_pModel->numbones; boneIndex++) {
        if (vstd::iequals(m_pModel->localBone[boneIndex].name, pName))
            return boneIndex;
    }

    return -1;
}


void CJointedModel::AppendCollisionModel(CPhysCollisionModel *pCollide) {
    if (m_isMassCenterForced) {
        g_StudioCollision.CollideSetMassCenter(pCollide->m_pCollisionData, m_massCenterForced);
    }

    pCollide->m_pNext = m_pCollisionList;
    m_pCollisionList = pCollide;
    m_collisionCount++;
}


void CJointedModel::UnlinkCollisionModel(CPhysCollisionModel *pCollide) {
    CPhysCollisionModel **pList = &m_pCollisionList;

    if (!pCollide)
        return;

    while (*pList) {
        CPhysCollisionModel *pNode = *pList;
        if (pNode == pCollide) {
            *pList = pCollide->m_pNext;
            m_collisionCount--;
            pCollide->m_pNext = NULL;
            return;
        }
        pList = &pNode->m_pNext;
    }
}

int CJointedModel::CollisionIndex(const char *pName) {
    if (!pName)
        return -1;

    CPhysCollisionModel *pList = m_pCollisionList;
    int index = 0;
    while (pList) {
        if (!stricmp(pName, pList->m_name))
            return index;

        pList = pList->m_pNext;
        index++;
    }

    return -1;
}


//-----------------------------------------------------------------------------
// Purpose: Sort the list so that parents come before their children
//-----------------------------------------------------------------------------
void CJointedModel::SortCollisionList(void) {
    if (!m_collisionCount)
        return;

    std::vector<CPhysCollisionModel *> pArray(m_collisionCount);
    CPhysCollisionModel *pList = m_pCollisionList;

    // make an array to make sorting easier
    int i = 0;

    while (pList) {
        pArray[i++] = pList;
        pList = pList->m_pNext;
    }

    // really stupid bubble sort!
    // this is really inefficient but it was easy to code and there are never
    // more than maxConvex elements.
    bool swapped = true;

    while (swapped) {
        swapped = false;
        // loop over all solids and swap any parent/child pairs that are out of order
        for (i = 0; i < m_collisionCount; i++) {
            CPhysCollisionModel *pPhys = pArray[i];
            if (!pPhys->m_parent)
                continue;

            // Don't try to move ones where the pPhys and its parent have the same name
            // otherwise an infinite loop results
            if (!Q_stricmp(pPhys->m_name, pPhys->m_parent))
                continue;

            // find the parent
            int j;
            for (j = 0; j < m_collisionCount; j++) {
                if (j == i)
                    continue;

                if (!stricmp(pPhys->m_parent, pArray[j]->m_name))
                    break;
            }

            // if the child came before the parent, then swap the parent and child positions
            if (j > i && j < m_collisionCount) {
                swapped = true;
                pArray[i] = pArray[j];
                pArray[j] = pPhys;
            }
        }
    }

    // link up the sorted list
    for (i = 0; i < m_collisionCount - 1; i++) {
        pArray[i]->m_pNext = pArray[i + 1];
    }
    // terminate
    pArray[i]->m_pNext = NULL;
    // point the list to first joint
    m_pCollisionList = pArray[0];
}

void CJointedModel::AddConvexSrc(ScriptTokenizer &qc, const char *szFileName) {
    const int slot = ApplyCollisionNextConvexSlot(m_ctx, szFileName);
    if (slot < 0)
        return;

    const CollisionMaterialCounts materialCounts = GetCollisionMaterialCounts(m_ctx);

    char szTempFileName[MAX_PATH];
    V_strcpy_safe(szTempFileName, szFileName);

    // The CONTENT search path is only configured for a game directory next to a content directory; a source that
    // Load_Source finds relative to the current $cd directory exists as well
    const auto sourceExists = [&](const char *path) {
        return g_pFullFileSystem->FileExists(path, IBaseFileSystem::PathID::Content) ||
               std::filesystem::exists(vstd::paths::to_fs(m_ctx.paths.cddir.back() + path));
    };

    bool bFoundSource = false;

    if (!bFoundSource) {
        V_SetExtension(szTempFileName, ".smd", sizeof(szTempFileName));
        bFoundSource = sourceExists(szTempFileName);
    }

    if (!bFoundSource) {
        V_SetExtension(szTempFileName, ".dmx", sizeof(szTempFileName));
        bFoundSource = sourceExists(szTempFileName);
    }

    if (!bFoundSource) {
        V_SetExtension(szTempFileName, ".fbx", sizeof(szTempFileName));
        bFoundSource = sourceExists(szTempFileName);
    }

    s_source_t *pmodel = Load_Source(m_ctx, szTempFileName, "SMD", false, false, false);
    if (!pmodel || !bFoundSource)
        return;

    // auto-remove any new materials/textures
    ApplyCollisionDropNewMaterials(m_ctx, pmodel, materialCounts);

    ApplyCollisionConvexSource(m_ctx, slot, pmodel);

    if (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (!V_strncmp(qc.token, "offset", 6)) {
            Vector vecOffsetPosition;
            vecOffsetPosition.Init();
            QAngle angOffsetAngle;
            angOffsetAngle.Init();
            float flScale = 1;

            int nCount = sscanf(qc.token, "offset pos[ %f %f %f ] angle[ %f %f %f ] scale[ %f ]",
                                &vecOffsetPosition.x, &vecOffsetPosition.y, &vecOffsetPosition.z,
                                &angOffsetAngle.x, &angOffsetAngle.y, &angOffsetAngle.z,
                                &flScale);

            if (nCount == 7) {
                ApplyCollisionConvexOffset(m_ctx, slot, vecOffsetPosition, angOffsetAngle, flScale);
            }
            else {
                qc.Error("Malformed offset parameters to $addconvexsrc.");
                return;
            }
        }
        else {
            qc.UnGetToken();
        }
    }

    if (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (!V_strncmp(qc.token, "concave", 7)) {
            ApplyCollisionConvexConcave(m_ctx, slot);
        }
        else {
            qc.UnGetToken();
        }
    }
}

void CJointedModel::AppendCollisionPair(const char *pName0, const char *pName1) {
    collisionpair_t *pPair = &m_CollisionPairStorage.emplace_back();
    pPair->obj0 = -1;
    pPair->obj1 = -1;
    int jointIndex0 = FindLocalBoneNamed(pName0);
    pPair->pName0 = (jointIndex0 >= 0) ? m_pModel->localBone[jointIndex0].name.c_str() : NULL;
    int jointIndex1 = FindLocalBoneNamed(pName1);
    pPair->pName1 = (jointIndex1 >= 0) ? m_pModel->localBone[jointIndex1].name.c_str() : NULL;

    //printf("Appending collision pair: %s to %s\n", pPair->pName0, pPair->pName1 );

    pPair->pNext = m_pCollisionPairs;
    m_pCollisionPairs = pPair;
}

void CJointedModel::RemoveCollisionPair(const char *pName0, const char *pName1) {
    int jointIndex0 = FindLocalBoneNamed(pName0);
    const char *szName0 = m_pModel->localBone[jointIndex0].name.c_str();
    int jointIndex1 = FindLocalBoneNamed(pName1);
    const char *szName1 = m_pModel->localBone[jointIndex1].name.c_str();

    collisionpair_t *pPairToRemove = NULL;

    // find the pair to remove
    collisionpair_t *pPair = m_pCollisionPairs;
    while (pPair) {
        if (!strcmp(pPair->pName0, szName0) && !strcmp(pPair->pName1, szName1)) {
            pPairToRemove = pPair;
            break;
        }
        pPair = pPair->pNext;
    }

    if (pPairToRemove) {
        // find the prev
        collisionpair_t *pPairPrev = NULL;

        pPair = m_pCollisionPairs;
        while (pPair) {
            if (pPair->pNext == pPairToRemove) {
                pPairPrev = pPair;
                break;
            }
            pPair = pPair->pNext;
        }

        if (pPairPrev) {
            pPairPrev->pNext = pPairToRemove->pNext;
        }
        else {
            // the pair we're removing is at the front
            m_pCollisionPairs = pPairToRemove->pNext;
        }

        //printf("Removing collision pair: %s to %s\n", szName0, szName1 );
    }
    else {
        //MdlWarning( "No such collision pair exists: [%s] to [%s]\n", pName0, pName1 );
    }
}

void CJointedModel::ForceMassCenter(const Vector &centerOfMass) {
    m_isMassCenterForced = true;
    m_massCenterForced = centerOfMass;
}


CPhysCollisionModel *CJointedModel::GetCollisionModel(const char *pName) {
    if (!pName)
        return NULL;

    CPhysCollisionModel *pList = m_pCollisionList;
    while (pList) {
        if (!stricmp(pName, pList->m_name))
            return pList;

        pList = pList->m_pNext;
    }

    return NULL;
}

void CJointedModel::AddConstraint(const char *pJointName, int axis, jointlimit_t jointType, float limitMin,
                                  float limitMax, float friction) {
    // In the editor/qc friction values are shown as 5X so 1.0 can be the default.
    CJointConstraint *pConstraint = &m_ConstraintStorage.emplace_back(pJointName, axis, jointType, limitMin, limitMax,
                                                                      friction * (1.0f / 5.0f));

    // link it in
    pConstraint->m_pNext = m_pConstraintList;
    m_pConstraintList = pConstraint;
    m_constraintCount++;
}

void CJointedModel::DefaultDamping(float damping) {
    m_defaultDamping = damping;
}

void CJointedModel::DefaultRotdamping(float rotdamping) {
    m_defaultRotdamping = rotdamping;
}

void CJointedModel::DefaultInertia(float inertia) {
    m_defaultInertia = inertia;
}

void CJointedModel::SetTotalMass(float mass) {
    m_totalMass = mass;
}

void CJointedModel::SetAutoMass(void) {
    m_totalMass = -1;
}

void CJointedModel::SetNoSelfCollisions() {
    m_noSelfCollisions = true;
}

void CJointedModel::SetCollisionModelDefaults(CPhysCollisionModel *pModel) {
    pModel->m_damping = m_defaultDamping;
    pModel->m_inertia = m_defaultInertia;
    pModel->m_rotdamping = m_defaultRotdamping;
    pModel->m_massBias = 1.0;

    // not written unless modified
    pModel->m_dragCoefficient = m_defaultDrag;
}


void CJointedModel::ComputeMass(void) {
    // already set
    if (m_totalMass >= 0)
        return;

    CPhysCollisionModel *pList = m_pCollisionList;
    m_totalMass = 0;

    while (pList) {
        char *pSurfaceProps = GetSurfaceProp(m_ctx, pList->m_name);
        int index = g_StudioSurfaceProps.GetSurfaceIndex(pSurfaceProps);
        float density, thickness;
        g_StudioSurfaceProps.GetPhysicsProperties(index, &density, &thickness, NULL, NULL);

        if (thickness > 0) {
            m_totalMass += pList->m_surfaceArea * thickness * CUBIC_METERS_PER_CUBIC_INCH * density;
        }
        else {
            // density is in kg/m^3, volume is in in^3
            m_totalMass += pList->m_volume * CUBIC_METERS_PER_CUBIC_INCH * density;
        }
        pList = pList->m_pNext;
    }

    MdlMessage("Computed Mass: {:.2f} kg\n", m_totalMass);
}

//-----------------------------------------------------------------------------
// Purpose: Creates a collision object using the defaults in joints
// Input  : &joints - joint system to create the model in
//			*pJointName - name to give this model
// Output : static CPhysCollisionModel
//-----------------------------------------------------------------------------
CPhysCollisionModel *CJointedModel::InitCollisionModel(const char *pJointName) {
    vstd::FunctionTimingRecorder _("Phys");
    CPhysCollisionModel *pModel = GetCollisionModel(pJointName);
    if (!pModel) {
        int boneIndex = BoneIndex(pJointName);
        if (boneIndex < 0)
            return NULL;

        pModel = m_OwnedCollisionModels.emplace_back(std::make_unique<CPhysCollisionModel>()).get();
        // this name is the same as pJointName, but guaranteed to be non-volatile (we'd have to copy pJointName)
        pModel->m_name = m_pModel->localBone[boneIndex].name;
        if (m_pModel->localBone[boneIndex].parent >= 0) {
            pModel->m_parent = m_pModel->localBone[m_pModel->localBone[boneIndex].parent].name;
        }
        else {
            pModel->m_parent = NULL;
        }

        SetCollisionModelDefaults(pModel);
        AppendCollisionModel(pModel);
    }

    return pModel;
}

void CJointedModel::JointDamping(const char *pJointName, float damping) {
    CPhysCollisionModel *pModel = InitCollisionModel(pJointName);
    if (pModel) {
        pModel->m_damping = damping;
    }
}

void CJointedModel::JointRotdamping(const char *pJointName, float rotdamping) {
    CPhysCollisionModel *pModel = InitCollisionModel(pJointName);
    if (pModel) {
        pModel->m_rotdamping = rotdamping;
    }
}

void CJointedModel::JointMassBias(const char *pJointName, float massBias) {
    CPhysCollisionModel *pModel = InitCollisionModel(pJointName);
    if (pModel) {
        pModel->m_massBias = massBias;
    }
}

void CJointedModel::JointInertia(const char *pJointName, float inertia) {
    CPhysCollisionModel *pModel = InitCollisionModel(pJointName);
    if (pModel) {
        pModel->m_inertia = inertia;
    }
}


void CJointedModel::DefaultDrag(float drag) {
    m_defaultDrag = drag;
}




//-----------------------------------------------------------------------------
// Purpose: Find all verts that differ only by texture coordinates - this allows
//			us to ignore texture coordinates on collision models
// Input  : *weldTable - output table
//			*pmodel - input model
//-----------------------------------------------------------------------------
static void BuildVertWeldTable(int *weldTable, s_source_t *pmodel, float weldVertEpsilon, float weldNormalEpsilon) {
    for (int i = 0; i < pmodel->numvertices; i++) {
        bool found = false;
        for (int j = 0; j < i; j++) {
            float dist = (pmodel->vertex[j].position - pmodel->vertex[i].position).Length();
            float normalDist = DotProduct(pmodel->vertex[j].normal, pmodel->vertex[i].normal);
            if (dist <= weldVertEpsilon && normalDist > weldNormalEpsilon) {
                found = true;
                weldTable[i] = j;
                break;
            }
        }

        if (!found) {
            weldTable[i] = i;
        }
    }
}


//-----------------------------------------------------------------------------
// Purpose: Finds a CPhysCollisionModel in a linked list of models.
// Input  : *pHead -
//			*pName -
// Output : CPhysCollisionModel
//-----------------------------------------------------------------------------
CPhysCollisionModel *FindObjectInList(CPhysCollisionModel *pHead, const char *pName) {
    while (pHead) {
        if (!stricmp(pName, pHead->m_name))
            break;
        pHead = pHead->m_pNext;
    }

    return pHead;
}


//-----------------------------------------------------------------------------
// Purpose: Fix all bones to reference the remapped/collapsed bone structure
void CJointedModel::FixBoneList() {
    if (!m_isJointed)
        return;

    CPhysCollisionModel *pmodel = m_pCollisionList;
    while (pmodel) {
        int nodeIndex = FindLocalBoneNamed(pmodel->m_name);
        if (nodeIndex < 0) {
            MdlWarning("Physics for unknown bone {}\n", pmodel->m_name);
        }
        else {
            int count = 0;
            // remove simplified bones
            while (m_pModel->boneLocalToGlobal[nodeIndex] < 0) {
                if (count++ > MAXSTUDIOSRCBONES)
                    break;

                // simplified out, move up to the parent
                nodeIndex = m_pModel->localBone[nodeIndex].parent;
            }

            if (nodeIndex >= 0) {
                // bone collapse may have changed parent hierarchy, and the root name.
                // The vertices are converted to the new reference by ConvertToWorldSpace(), as well as RemapVerticesToGlobalBones()
                pmodel->m_name = m_ctx.bones.bonetable[m_pModel->boneLocalToGlobal[nodeIndex]].name;
                pmodel->m_parent = NULL;
                int parentIndex = m_pModel->localBone[nodeIndex].parent;
                if (parentIndex >= 0 && parentIndex != nodeIndex) {
                    parentIndex = m_bonemap[parentIndex];
                    if (m_pModel->boneLocalToGlobal[parentIndex] < 0) {
                        pmodel->m_parent = m_pModel->localBone[parentIndex].name;
                    }
                    else {
                        pmodel->m_parent = m_ctx.bones.bonetable[m_pModel->boneLocalToGlobal[parentIndex]].name;
                    }
                }
            }
            else {
                MdlWarning("Physics for unknown bone {}\n", pmodel->m_name);
            }
        }

        pmodel = pmodel->m_pNext;
    }
}

//-----------------------------------------------------------------------------
// Purpose: Fixup all references to parents by walking up on models whose parents
//			have no collision geometry.  Bones without geometry cannot be physically
//			simulated, so they must be removed.
//			NOTE: This is broken.  It won't work for tree structures with an empty parent
//			(i.e. 2 children attached to a parent bone that has no physics geometry - thus empty)
//			It will not convert that parent into a constraint between 2 children
// Input  : *pList -
//			*pSource -
//			*pParentName -
// Output : const char
//-----------------------------------------------------------------------------
const char *CJointedModel::FixParent(const char *pParentName) {
    while (pParentName) {
        if (FindObjectInList(m_pCollisionList, pParentName)) {
            return pParentName;
        }
        int nodeIndex = FindLocalBoneNamed(pParentName);
        if (nodeIndex < 0)
            return NULL;
        int parentIndex = m_pModel->localBone[nodeIndex].parent;
        if (parentIndex < 0) {
            break;
        }

        pParentName = m_pModel->localBone[parentIndex].name;
    }

    return NULL;
}


struct boundingvolume_t {
    Vector mins;
    Vector maxs;
};


void CreateCollide(CPhysCollisionModel *pBase, StudioConvex_t **pElements, int elementCount,
                   const boundingvolume_t &bv) {
    int i;

    if (!pBase)
        return;

    // NOTE: Must do this before building collide
    pBase->m_volume = 0;
    pBase->m_surfaceArea = 0;
    for (i = 0; i < elementCount; i++) {
        pBase->m_volume += g_StudioCollision.ConvexVolume(pElements[i]);
        pBase->m_surfaceArea += g_StudioCollision.ConvexSurfaceArea(pElements[i]);
    }

    StudioConvertConvexParams_t params;
    params.Defaults();
    params.buildOuterConvexHull = true;
    params.buildDragAxisAreas = true;
    params.checkOptimalTracing = true;
    Vector size = bv.maxs - bv.mins;

    int largest = 0;
    float minSurfaceArea = -1.0f;
    for (i = 0; i < 3; i++) {
        if (size[i] > size[largest]) {
            largest = i;
        }

        int other = (i + 1) % 3;
        int cross = (i + 2) % 3;
        float surfaceArea = size[other] * size[cross];
        if (minSurfaceArea < 0 || surfaceArea < minSurfaceArea) {
            minSurfaceArea = surfaceArea;
        }
    }
    // this can be really slow with super-large models and a low error tolerance
    // Basically you get a ray cast through each square of epsilon surface area on each OBB side
    // So compute it for 0.01% error (on the smallest side, less on larger sides)
    params.dragAreaEpsilon = clamp(minSurfaceArea * 1e-4f, 0.25f, 128.0f);

    Vector tmp = size;
    tmp[largest] = 0;
    float len = tmp.Length();
    if (len > 0) {
        float sizeRatio = size[largest] / len;
        // HACKHACK: Hardcoded size ratio to induce damping
        // This prevents long skinny objects from rolling endlessly
        if (sizeRatio > 9) {
            pBase->m_rotdamping = 1.0f;
        }
    }
    // THIS DESTROYS pConvex!!
    pBase->m_pCollisionData = g_StudioCollision.ConvertConvexToCollideParams(pElements, elementCount, params);

    // debug output for the drag area calculations
#if 0
    Msg("Drag epsilon is {:.3f}\n", params.dragAreaEpsilon);
    Vector areas = g_StudioCollision.CollideGetOrthographicAreas(pBase->m_pCollisionData);
    Msg("Drag fractions are {:.3f} {:.3f} {:.3f}\n", areas.x, areas.y, areas.z);
#endif
}


// is this list of verts contained in a slab of epsilon width?  If so, it's probably
// an error of some kind - we shouldn't be authoring flat or 2d collision models
bool IsApproximatelyPlanar(Vector **verts, int vertCount, float epsilon) {
    vstd::FunctionTimingRecorder _("Phys");
    if (vertCount < 4)
        return true;

    // If we're using an un-welded model, then this may generate a degenerate normal
    // loop to search for an actual plane
    int v0 = 1, v1 = 2;
    Vector normal;
    while (v0 < vertCount && v1 < vertCount) {
        Vector edge0 = *verts[v0] - *verts[0];
        Vector edge1 = *verts[v1] - *verts[0];

        normal = CrossProduct(edge0, edge1);
        float len = VectorNormalize(normal);
        if (len > 0.001)
            break;
        if (edge0.Length() < 0.001) {
            // verts[0] and v0 are coincident, try new verts
            v0++;
            v1++;
        }
        else {
            // v0 seems fine, try a new v1 -- it's probably coincident with v0
            v1++;
        }
    }

    // form the plane and project all of the verts into it
    float minDist = DotProduct(normal, *verts[0]);
    float maxDist = minDist;

    for (int i = 0; i < vertCount; i++) {
        float d = DotProduct(*verts[i], normal);
        if (d < minDist) {
            minDist = d;
        }
        else if (d > maxDist) {
            maxDist = d;
        }
        // at least one vert out of the plane, we've got something 3 dimensional
        if (fabsf(maxDist - minDist) > epsilon)
            return false;
    }
    return true;
}


void BuildConvexListByVertID(s_source_t *pmodel, std::vector<convexlist_t> &convexList, std::vector<int> &vertList,
                             std::vector<int> &vertID) {
    // loop through each island of verts and append it to the convex list
    convexlist_t current;
    for (int i = 0; i < pmodel->numvertices; i++) {
        // already processed this group
        if (vertID[i] < 0 || vertID[i] > pmodel->numfaces)
            continue;

        current.firstVertIndex = std::ssize(vertList);
        current.numVertIndex = 0;

        int id = vertID[i];

        for (int j = i; j < pmodel->numvertices; j++) {
            if (vertID[j] == id) {
                vertList.push_back(j);
                current.numVertIndex++;
                // don't reuse this vert
                vertID[j] = -1;
            }
        }
        convexList.push_back(current);
    }
}

// build a list of vertex indices for each connected sub-piece
void BuildSingleConvexForFaceList(s_source_t *pmodel, std::vector<convexlist_t> &convexList, std::vector<int> &vertList,
                                  const std::vector<s_face_t> &faceList) {
    vstd::FunctionTimingRecorder _("Phys");
    std::vector<int> vertID(pmodel->numvertices, -1);
    size_t i;

    for (i = 0; i < faceList.size(); i++) {
        const s_face_t &globalFace = faceList[i];
        vertID[globalFace.a] = 1;
        vertID[globalFace.b] = 1;
        vertID[globalFace.c] = 1;
    }
    BuildConvexListByVertID(pmodel, convexList, vertList, vertID);
}

static void BuildConvexListForFaceList(s_source_t *pmodel, std::vector<convexlist_t> &convexList,
                                       std::vector<int> &vertList, const std::vector<s_face_t> &faceList,
                                       float weldVertEpsilon, float weldNormalEpsilon) {
    vstd::FunctionTimingRecorder _("Phys");
    std::vector<int> weldTable(pmodel->numvertices);
    BuildVertWeldTable(weldTable.data(), pmodel, weldVertEpsilon, weldNormalEpsilon);

    int i;
    std::vector<int> vertID(pmodel->numvertices);

    // mark all verts as max faceid + 1
    for (i = 0; i < pmodel->numvertices; i++) {
        // If these verts have been welded to a lower-index vert, mark them
        // as already processed to avoid making additional convex objects out of them.
        if (weldTable[i] != i) {
            vertID[i] = -1;
        }
        else {
            vertID[i] = pmodel->numfaces + 1;
        }
    }

    Assert(convexList.empty());
    Assert(vertList.empty());

    int marked = 0;
    int faceid = 0;
    // iterate the face list, minimizing the vertID at each vert
    // until we have an iteration where no vertIDs are changed
    do {
        marked = 0;
        faceid = 0;

        // basically this flood fills ids out to the verts until each island of connected
        // verts shares a single id (so new verts got marked)
        for (i = 0; i < std::ssize(faceList); i++) {
            s_face_t globalFace = faceList[i];
            // account for welding
            globalFace.a = weldTable[globalFace.a];
            globalFace.b = weldTable[globalFace.b];
            globalFace.c = weldTable[globalFace.c];


            int newid = MIN(i, vertID[globalFace.a]);
            newid = MIN(newid, vertID[globalFace.b]);
            newid = MIN(newid, vertID[globalFace.c]);

            // mark all verts with the minimum, count the number we had to mark
            if (vertID[globalFace.a] != newid) {
                vertID[globalFace.a] = newid;
                marked++;
            }
            if (vertID[globalFace.b] != newid) {
                vertID[globalFace.b] = newid;
                marked++;
            }
            if (vertID[globalFace.c] != newid) {
                vertID[globalFace.c] = newid;
                marked++;
            }
        }
    } while (marked != 0);

    BuildConvexListByVertID(pmodel, convexList, vertList, vertID);
}


// take a list of convex elements (lists of vert indices into master vert list) and build StudioConvex_t out of them
// return true if there are no errors detected
bool BuildConvexesForLists(std::vector<StudioConvex_t *> &convexOut, const std::vector<convexlist_t> &convexList,
                           const std::vector<int> &vertList, const std::vector<Vector> &worldspaceVerts, bool bRemove2d) {
    vstd::FunctionTimingRecorder _("Phys");
    bool bValid = true;
    std::vector<Vector *> vertsThisConvex;
    for (size_t i = 0; i < convexList.size(); i++) {
        const convexlist_t &elem = convexList[i];
        vertsThisConvex.clear();
        for (int j = 0; j < elem.numVertIndex; j++) {
            // this is ok because physcollision won't modify these, but wants non-const
            Vector *pVert = const_cast<Vector *>(&worldspaceVerts[vertList[j + elem.firstVertIndex]]);
            vertsThisConvex.push_back(pVert);
        }

        // need at least 3 verts to build a StudioConvex_t
        if (vertsThisConvex.size() > 2) {
            const float g_epsilon_2d = 0.5f;
            // HACKHACK: A heuristic to detect models without smoothing groups set
            // UNDONE: Do a BSP to decompose arbitrary models to convex?
            if (IsApproximatelyPlanar(vertsThisConvex.data(), std::ssize(vertsThisConvex), g_epsilon_2d)) {
                if (bRemove2d)
                    continue;
                MdlWarning("Model has 2-dimensional geometry (less than {:.3f} inches thick on any axis)!!!\n",
                           g_epsilon_2d);
                bValid = false;
            }
            // go ahead and build it out
            auto *pConvex = g_StudioCollision.ConvexFromVerts(vertsThisConvex.data(), std::ssize(vertsThisConvex));
            if (pConvex) {
                // Got something valid, attach this convex data to the root model
                g_StudioCollision.SetConvexGameData(pConvex, 0);
                convexOut.push_back(pConvex);
            }
        }
    }

    return bValid;
}

//-----------------------------------------------------------------------------
// Purpose: Build a jointed collision model with constraints
// Output : int
//-----------------------------------------------------------------------------
int CJointedModel::ProcessJointedModel() {
    vstd::FunctionTimingRecorder _("Phys");
    MdlMessage("Processing jointed collision model\n");

    // loop through each bone and form a collision model
    for (int boneIndex = 0; boneIndex < m_pModel->numbones; boneIndex++) {
        if (!ShouldProcessBone(boneIndex))
            continue;

        std::vector<Vector> bonespaceVerts(m_pModel->numvertices);
        ConvertToBoneSpace(boneIndex, bonespaceVerts);
        std::vector<s_face_t> faceList;
        std::vector<convexlist_t> convexList;
        std::vector<int> vertList;
        std::vector<StudioConvex_t *> convexOut;
        bool bValid = false;

        for (int i = 0; i < m_pModel->meshes.size(); i++) {
            s_mesh_t *pmesh = &m_pModel->meshes[i];
            for (int j = 0; j < pmesh->numfaces; j++) {
                s_face_t *face = m_pModel->face + pmesh->faceoffset + j;
                s_face_t globalFace;
                GlobalFace(&globalFace, pmesh, face);
                if (FaceHasVertOnBone(globalFace, boneIndex)) {
                    faceList.push_back(globalFace);
                }
            }

            if (m_allowConcaveJoints) {
                BuildConvexListForFaceList(m_pModel, convexList, vertList, faceList, m_weldVertEpsilon,
                                           m_weldNormalEpsilon);
            }
            else {
                BuildSingleConvexForFaceList(m_pModel, convexList, vertList, faceList);
            }

            bValid = BuildConvexesForLists(convexOut, convexList, vertList, bonespaceVerts, m_remove2d);
        }

        if (std::ssize(convexOut) > m_maxConvex) {
            MdlWarning("COSTLY COLLISION MODEL!!!! ({} parts - {} allowed)\n", convexOut.size(), m_maxConvex);
            bValid = false;
        }

        if (!bValid && !convexOut.empty()) {
            MdlWarning("Error with convex elements of {}, building single convex!!!!\n", m_pModel->filename);
            for (size_t i = 0; i < convexOut.size(); i++) {
                g_StudioCollision.ConvexFree(convexOut[i]);
            }
            convexOut.clear();
        }

        if (!convexOut.empty()) {
            int i;

            CPhysCollisionModel *pPhys = InitCollisionModel(m_pModel->localBone[boneIndex].name);

            pPhys->m_mass = 1.0;
            pPhys->m_name = m_pModel->localBone[boneIndex].name;
            if (m_pModel->localBone[boneIndex].parent >= 0) {
                pPhys->m_parent = m_pModel->localBone[m_pModel->localBone[boneIndex].parent].name;
            }
            else {
                pPhys->m_parent = NULL;
            }

            boundingvolume_t bv;
            ClearBounds(bv.mins, bv.maxs);
            int vertCount = 0;
            for (i = 0; i < std::ssize(convexList); i++) {
                const convexlist_t &elem = convexList[i];
                for (int j = 0; j < elem.numVertIndex; j++) {
                    AddPointToBounds(bonespaceVerts[vertList[elem.firstVertIndex + j]], bv.mins, bv.maxs);
                    vertCount++;
                }
            }
            for (i = 0; i < std::ssize(convexOut); i++) {
                // Attach this convex data to this particular bone
                int globalBoneIndex = m_pModel->boneLocalToGlobal[boneIndex];
                g_StudioCollision.SetConvexGameData(convexOut[i], globalBoneIndex + 1);
            }

            CreateCollide(pPhys, convexOut.data(), std::ssize(convexOut), bv);
            MdlMessage("{:<24} ({:3} verts, {} convex elements) volume: {:4.2f}\n", pPhys->m_name, vertCount,
                      convexOut.size(), pPhys->m_volume);
            UnlinkCollisionModel(pPhys);
            AppendCollisionModel(pPhys);
        }
    }
    // remove any non-physical joints at this point
    CPhysCollisionModel *pPhys = m_pCollisionList;
    while (pPhys) {
        CPhysCollisionModel *pNext = pPhys->m_pNext;
        if (!pPhys->m_pCollisionData) {
            // removed from the list; the model stays owned by m_OwnedCollisionModels
            UnlinkCollisionModel(pPhys);
        }
        pPhys = pNext;
    }

    return 1;
}


#if 0
// debug visualization code - use this to dump out intermediate geometry files for visualization in glview.exe
void DumpToGLView(char const *pName, s_source_t *pmodel, Vector *worldVerts, int *used) {
    int i;

    for (i = 0; i < pmodel->numvertices; i++)
        used[i] = -1;

    FILE *fp = fopen(pName, "w");

    // dump the model to a glview file
    for (i = 0; i < pmodel->meshes.size(); i++) {
        s_mesh_t *pmesh = &pmodel->meshes[i];
        for (int j = 0; j < pmesh->numfaces; j++) {
            s_face_t *face = pmodel->face + pmesh->faceoffset + j;
            s_face_t globalFace;
            GlobalFace(&globalFace, pmesh, face);

            std::print(fp, "3\n");
            std::print(fp, "{:6.3f} {:6.3f} {:6.3f} 0 1 0\n", worldVerts[globalFace.b].x, worldVerts[globalFace.b].y,
                    worldVerts[globalFace.b].z);
            std::print(fp, "{:6.3f} {:6.3f} {:6.3f} 1 0 0\n", worldVerts[globalFace.a].x, worldVerts[globalFace.a].y,
                    worldVerts[globalFace.a].z);
            std::print(fp, "{:6.3f} {:6.3f} {:6.3f} 0 0 1\n", worldVerts[globalFace.c].x, worldVerts[globalFace.c].y,
                    worldVerts[globalFace.c].z);
            used[globalFace.a] = 0;
            used[globalFace.b] = 0;
            used[globalFace.c] = 0;
        }
    }

    // dump a triangle expanded around each vert to the file (to show degenerate tris' verts).
    for (i = 0; i < pmodel->numvertices; i++) {
        if (used[i] < 0)
            continue;

        std::print(fp, "3\n");
        Vector vert;
        vert = worldVerts[i] + Vector(0, 0, 5);
        std::print(fp, "{:6.3f} {:6.3f} {:6.3f} 1 0 0\n", vert.x, vert.y, vert.z);
        vert = worldVerts[i] + Vector(5, 0, -5);
        std::print(fp, "{:6.3f} {:6.3f} {:6.3f} 0 1 0\n", vert.x, vert.y, vert.z);
        vert = worldVerts[i] + Vector(-5, 0, -5);
        std::print(fp, "{:6.3f} {:6.3f} {:6.3f} 0 0 1\n", vert.x, vert.y, vert.z);
    }

    fclose(fp);
}
#endif


int CJointedModel::ProcessSingleBody() {
    // THIS CODE IS ONLY EXECUTED ON PROPS - i.e. NON-JOINTED MODELS


    static const int nMaxModels = MAX_EXTRA_COLLISION_MODELS + 1;

    if (!m_bRootCollisionIsEmpty) {
        m_ExtraModels[MAX_EXTRA_COLLISION_MODELS].m_pSrc = m_pModel;
        m_ExtraModels[MAX_EXTRA_COLLISION_MODELS].m_bConcave = m_allowConcave;
        m_ExtraModels[MAX_EXTRA_COLLISION_MODELS].m_matOffset.SetToIdentity();
    }

    // Transform all the extra models using their offset matrices. May as well do this right away.
    for (int i = 0; i < nMaxModels; i++) {
        if (m_ExtraModels[i].m_pSrc != NULL) {
            if (!m_allowConcave)
                m_ExtraModels[i].m_bConcave = false;

            // Note this doesn't even touch the verts if the offset matrix is identity (assumed no position/rotation change in that case).
            ApplyOffsetToSrcVerts(m_ExtraModels[i].m_pSrc, m_ExtraModels[i].m_matOffset);
        }
    }

    // the root collision model is now 'empty' meaning that the overall collision model will be built entirely from
    // appended sources in the extra models list. If the extra model list is empty, we've got nothing to build
    // and that's an error.

    // find and group up the concave sources into a single welded source
    s_source_t *pConcaveSrc = NULL;
    s_source_t *pFallbackSrc = NULL;

    for (int i = 0; i < nMaxModels; i++) {
        if (m_ExtraModels[i].m_pSrc != NULL) {
            if (!pFallbackSrc)
                pFallbackSrc = m_ExtraModels[i].m_pSrc;

            if (m_ExtraModels[i].m_bConcave) {
                if (!pConcaveSrc) {
                    pConcaveSrc = m_ExtraModels[i].m_pSrc;
                }
                else {
                    AddSrcToSrc(m_ctx, pConcaveSrc, m_ExtraModels[i].m_pSrc);
                }
            }
        }
    }

    if (!m_pModel) {
        if (pConcaveSrc) {
            m_pModel = pConcaveSrc;
        }
        else if (pFallbackSrc) {
            m_pModel = pFallbackSrc;
        }
        else {
            Error("No valid physics source mesh!\n");
        }
    }


    std::vector<StudioConvex_t *> convexOut;
    std::vector<convexlist_t> convexList;

    std::vector<Vector> allworldspaceVerts;

    bool bValid = true;

    // if concavity is allowed, build out pConcaveSrc
    if (pConcaveSrc && m_allowConcave) {
        std::vector<Vector> worldspaceVerts(pConcaveSrc->numvertices);
        ConvertToWorldSpace(worldspaceVerts, pConcaveSrc);

        allworldspaceVerts.insert(allworldspaceVerts.end(), worldspaceVerts.begin(), worldspaceVerts.end());

        std::vector<s_face_t> faceList;
        std::vector<int> vertList;

        for (int i = 0; i < pConcaveSrc->meshes.size(); i++) {
            s_mesh_t *pmesh = &pConcaveSrc->meshes[i];
            for (int j = 0; j < pmesh->numfaces; j++) {
                s_face_t *face = pConcaveSrc->face + pmesh->faceoffset + j;
                s_face_t globalFace;
                GlobalFace(&globalFace, pmesh, face);
                faceList.push_back(globalFace);
            }
        }
        BuildConvexListForFaceList(pConcaveSrc, convexList, vertList, faceList, m_weldVertEpsilon,
                                   m_weldNormalEpsilon);
        bValid = BuildConvexesForLists(convexOut, convexList, vertList, worldspaceVerts, m_remove2d);
    }

    // now add convex extramodel sources that are themselves convex but want to be part of the larger concave system


    // we need to do this because once we've welded a combined model together, we can't tell what pieces of it
    // used their renderable geometry to create a convex hull. The next best (but still gross) assumption is
    // that if the physics src is the same src as the renderable geo then it should get naively convex-hulled,
    // then transformed into the given offset in the (possibly concave) overall physics model.

    for (int i = 0; i < nMaxModels; i++) {
        if (m_ExtraModels[i].m_pSrc != NULL && !m_ExtraModels[i].m_bConcave) {
            s_source_t *pmodel = m_ExtraModels[i].m_pSrc;

            std::vector<Vector> worldspaceVertsExtra(pmodel->numvertices);
            ConvertToWorldSpace(worldspaceVertsExtra, pmodel);

            allworldspaceVerts.insert(allworldspaceVerts.end(), worldspaceVertsExtra.begin(), worldspaceVertsExtra.end());

            std::vector<Vector *> vertsThisConvex;

            for (Vector &vert: worldspaceVertsExtra) {
                // transform the verts using the offset
                //vert = VectorTransform( vert, m_ExtraModels[i].m_matOffset );

                // physcollision won't modify these
                vertsThisConvex.push_back(&vert);
            }

            StudioConvex_t *pConvex = g_StudioCollision.
                    ConvexFromVerts(vertsThisConvex.data(), std::ssize(vertsThisConvex));
            if (pConvex) {
                // Got something valid, attach this convex data to the root model
                g_StudioCollision.SetConvexGameData(pConvex, 0);
                convexOut.push_back(pConvex);
            }
            else {
                MdlWarning("Error with convex elements of {}!\n", pmodel->filename);
                bValid = false;
            }
        }
    }

    if (std::ssize(convexOut) > m_maxConvex) {
        if (m_convexHullCountOverride) {
            MdlWarning("Allowing costly collision model. Please be careful. ({} parts - {} normally allowed)\n",
                       convexOut.size(), m_maxConvex);
        }
        else {
            MdlWarning("COSTLY COLLISION MODEL!!!! ({} parts - {} allowed)\n", convexOut.size(), m_maxConvex);
            bValid = false;
        }
    }

    if (!bValid) {
        for (size_t i = 0; i < convexOut.size(); i++) {
            g_StudioCollision.ConvexFree(convexOut[i]);
        }
        convexOut.clear();
    }

    // either we don't want concave, or there was an error building it
    if (convexOut.empty() || !m_allowConcave) {
        convexOut.clear();

        std::vector<Vector *> vertsThisConvex;

        for (Vector &vert: allworldspaceVerts) {
            // physcollision won't modify these
            vertsThisConvex.push_back(&vert);
        }

        StudioConvex_t *pConvex = g_StudioCollision.ConvexFromVerts(vertsThisConvex.data(), std::ssize(vertsThisConvex));
        if (pConvex) {
            // Got something valid, attach this convex data to the root model
            g_StudioCollision.SetConvexGameData(pConvex, 0);
            convexOut.push_back(pConvex);
        }
        else {
            Error("Error building fallback convex hull!\n");
        }
    }

    if (!convexOut.empty()) {
        MdlMessage("Model has {} convex sub-parts\n", convexOut.size());

        CPhysCollisionModel *pPhys = m_OwnedCollisionModels.emplace_back(std::make_unique<CPhysCollisionModel>()).get();
        SetCollisionModelDefaults(pPhys);

        boundingvolume_t bv;
        ClearBounds(bv.mins, bv.maxs);
        for (int i = std::ssize(allworldspaceVerts) - 1; --i >= 0;) {
            AddPointToBounds(allworldspaceVerts[i], bv.mins, bv.maxs);
        }
        CreateCollide(pPhys, convexOut.data(), std::ssize(convexOut), bv);

        // Init mass, write routine will distribute the total mass
        pPhys->m_mass = 1.0;
        char tmp[512];
        Q_FileBase(m_pModel->filename.c_str(), tmp, sizeof(tmp));

        m_NameStorage.emplace_back(tmp);
        pPhys->m_name = m_NameStorage.back().c_str();
        pPhys->m_parent = NULL;

        AppendCollisionModel(pPhys);
    }
    return 1;
}


#define MAX_ARGS	16
#define ARG_SIZE	256

//-----------------------------------------------------------------------------
// Purpose: HACKETY HACK - get the args into a buffer.
//			This checks for overflow, but it's not very robust - shouldn't be necessary though
// Input  : pArgs[][ARG_SIZE] -
//			maxCount - array size of pargs
// Output : int - count actually used
//-----------------------------------------------------------------------------
int ReadArgs(ScriptTokenizer &qc, char pArgs[][ARG_SIZE], int maxCount) {
    int argCount = 0;

    while (argCount < maxCount && qc.TokenAvailable()) {
        qc.ExpectToken();
        strncpy(pArgs[argCount], qc.token, ARG_SIZE);
        argCount++;
    }

    return argCount;
}


//-----------------------------------------------------------------------------
// Purpose: Simple atof wrapper to keep from crashing on bad user input
// Input  : *pString -
// Output : float
//-----------------------------------------------------------------------------
float Safe_atof(const char *pString) {
    if (!pString)
        return 0;

    return atof(pString);
}

//-----------------------------------------------------------------------------
// Purpose: Simple atoi wrapper to avoid crashing on bad user input
// Input  : *pString -
// Output : int
//-----------------------------------------------------------------------------
int Safe_atoi(const char *pString) {
    if (!pString)
        return 0;

    return atoi(pString);
}

//-----------------------------------------------------------------------------
// Apply functions (include/studiomdl/apply_collision.hpp): what the QC commands do, shared with the builder API
//-----------------------------------------------------------------------------
CollisionMaterialCounts GetCollisionMaterialCounts(const StudioMdlState &ctx) {
    return {ctx.materials.materials.size(), ctx.materials.textures.size()};
}

void ApplyCollisionDropNewMaterials(StudioMdlState &ctx, s_source_t *source, const CollisionMaterialCounts &before) {
    // auto-remove any new materials/textures
    if (before.materials && before.textures &&
        (before.textures != ctx.materials.textures.size() || before.materials != ctx.materials.materials.size())) {
        ctx.materials.textures.resize(before.textures);
        ctx.materials.materials.resize(before.materials);

        source->texmap[0] = 0;
    }
}

void ApplyCollisionSource(StudioMdlState &ctx, s_source_t *source) {
    JointedModel(ctx).SetSource(source);
}

void ApplyCollisionBlankRoot(StudioMdlState &ctx) {
    JointedModel(ctx).m_bRootCollisionIsEmpty = true;
}

void ApplyCollisionJointed(StudioMdlState &ctx, const bool jointed) {
    JointedModel(ctx).m_isJointed = jointed;
}

void ApplyCollisionBeginBlock(StudioMdlState &ctx) {
    JointedModel(ctx).m_convexHullCountOverride = false;
}

void ApplyCollisionMass(StudioMdlState &ctx, const float mass) { JointedModel(ctx).SetTotalMass(mass); }

void ApplyCollisionAutoMass(StudioMdlState &ctx) { JointedModel(ctx).SetAutoMass(); }

void ApplyCollisionInertia(StudioMdlState &ctx, const float inertia) { JointedModel(ctx).DefaultInertia(inertia); }

void ApplyCollisionDamping(StudioMdlState &ctx, const float damping) { JointedModel(ctx).DefaultDamping(damping); }

void ApplyCollisionRotDamping(StudioMdlState &ctx, const float rotDamping) {
    JointedModel(ctx).DefaultRotdamping(rotDamping);
}

void ApplyCollisionDrag(StudioMdlState &ctx, const float drag) { JointedModel(ctx).DefaultDrag(drag); }

void ApplyCollisionMaxConvexPieces(StudioMdlState &ctx, const int count) { JointedModel(ctx).SetMaxConvex(count); }

void ApplyCollisionRemove2D(StudioMdlState &ctx) { JointedModel(ctx).Remove2DConvex(); }

void ApplyCollisionConcavePerJoint(StudioMdlState &ctx) { JointedModel(ctx).AllowConcaveJoints(); }

void ApplyCollisionWeldPosition(StudioMdlState &ctx, const float epsilon) {
    JointedModel(ctx).m_weldVertEpsilon = epsilon;
}

void ApplyCollisionWeldNormal(StudioMdlState &ctx, const float epsilon) {
    JointedModel(ctx).m_weldNormalEpsilon = epsilon;
}

void ApplyCollisionConcave(StudioMdlState &ctx) { JointedModel(ctx).AllowConcave(); }

void ApplyCollisionConvexHullCountOverride(StudioMdlState &ctx) {
    JointedModel(ctx).m_convexHullCountOverride = true;
}

void ApplyCollisionMassCenter(StudioMdlState &ctx, const Vector &center) { JointedModel(ctx).ForceMassCenter(center); }

void ApplyCollisionNoSelfCollisions(StudioMdlState &ctx) { JointedModel(ctx).SetNoSelfCollisions(); }

void ApplyCollisionAssumeWorldSpace(StudioMdlState &ctx) { JointedModel(ctx).m_bAssumeWorldspace = true; }

void ApplyCollisionRootBone(StudioMdlState &ctx, const std::string_view bone) {
    CJointedModel &joints = JointedModel(ctx);
    if (bone.size() >= sizeof(joints.m_rootName))
        MdlError("$rootbone: bone name '{}' is too long (the maximum is {} characters)\n", bone,
                 sizeof(joints.m_rootName) - 1);
    // save the root bone name
    std::memcpy(joints.m_rootName, bone.data(), bone.size());
    joints.m_rootName[bone.size()] = '\0';
}

void ApplyCollisionAnimatedFriction(StudioMdlState &ctx, const int minFriction, const int maxFriction,
                                    const float timeIn, const float timeHold, const float timeOut) {
    CJointedModel &joints = JointedModel(ctx);
    joints.m_flFrictionTimeIn = timeIn;
    joints.m_flFrictionTimeOut = timeOut;
    joints.m_flFrictionTimeHold = timeHold;
    joints.m_iMinAnimatedFriction = minFriction;
    joints.m_iMaxAnimatedFriction = maxFriction;
    joints.m_bHasAnimatedFriction = true;
}

// UNDONE: Automatically skip joints that will have mass that is too low?
void ApplyCollisionJointSkip(StudioMdlState &ctx, const std::string_view joint) {
    CJointedModel &joints = JointedModel(ctx);
    const std::string name(joint);
    const int boneIndex = joints.FindLocalBoneNamed(name.c_str());
    if (boneIndex < 0) {
        MdlWarning("Can't skip joint {}, not found\n", name);
    }
    else {
        joints.SkipBone(boneIndex);
    }
}

// verts from the bone named child are added to the collision model of parent
void ApplyCollisionJointMerge(StudioMdlState &ctx, const std::string_view parent, const std::string_view child) {
    CJointedModel &joints = JointedModel(ctx);
    const std::string parentName(parent);
    const std::string childName(child);
    joints.AddMergeCommand(parentName.c_str(), childName.c_str());
    joints.MergeBones(parentName.c_str(), childName.c_str());
}

void ApplyCollisionJointInertia(StudioMdlState &ctx, const std::string_view joint, const float inertia) {
    JointedModel(ctx).JointInertia(std::string(joint).c_str(), inertia);
}

void ApplyCollisionJointDamping(StudioMdlState &ctx, const std::string_view joint, const float damping) {
    JointedModel(ctx).JointDamping(std::string(joint).c_str(), damping);
}

void ApplyCollisionJointRotDamping(StudioMdlState &ctx, const std::string_view joint, const float rotDamping) {
    JointedModel(ctx).JointRotdamping(std::string(joint).c_str(), rotDamping);
}

void ApplyCollisionJointMassBias(StudioMdlState &ctx, const std::string_view joint, const float massBias) {
    JointedModel(ctx).JointMassBias(std::string(joint).c_str(), massBias);
}

const char *ApplyCollisionJointConstrainCheck(StudioMdlState &ctx, const std::string_view joint, const int axis,
                                              const float limitMin, const float limitMax) {
    CJointedModel &joints = JointedModel(ctx);
    const std::string name(joint);
    const int jointIndex = joints.FindLocalBoneNamed(name.c_str());
    if (!ctx.options.createMakefile && jointIndex < 0) {
        MdlWarning("Can't find joint {}\n", name);
        return nullptr;
    }
    const char *jointName = joints.m_pModel->localBone[jointIndex].name;

    if (axis < 0 || axis > 2 || limitMin > limitMax) {
        MdlError("Invalid joint constraint for {}\nCan't build ragdoll!\n", jointName);
    }
    return jointName;
}

void ApplyCollisionJointConstrain(StudioMdlState &ctx, const char *jointName, const int axis,
                                  const CollisionJointType type, const float limitMin, const float limitMax,
                                  const float friction) {
    jointlimit_t jointType = JOINT_FREE;
    switch (type) {
        case CollisionJointType::Free:
            jointType = JOINT_FREE;
            break;
        case CollisionJointType::Fixed:
            jointType = JOINT_FIXED;
            break;
        case CollisionJointType::Limit:
            jointType = JOINT_LIMIT;
            break;
    }
    JointedModel(ctx).AddConstraint(jointName, axis, jointType, limitMin, limitMax, friction);
}

void ApplyCollisionJointCollide(StudioMdlState &ctx, const std::string_view a, const std::string_view b) {
    JointedModel(ctx).AppendCollisionPair(std::string(a).c_str(), std::string(b).c_str());
}

void ApplyCollisionJointCollideAllToAll(StudioMdlState &ctx, const std::span<const std::string> joints) {
    CJointedModel &model = JointedModel(ctx);
    for (size_t i = 0; i < joints.size(); i++) {
        for (size_t j = 0; j < joints.size(); j++) {
            if (i != j)
                model.AppendCollisionPair(joints[i].c_str(), joints[j].c_str());
        }
    }
}

void ApplyCollisionJointNoCollide(StudioMdlState &ctx, const std::string_view a, const std::string_view b) {
    CJointedModel &joints = JointedModel(ctx);
    const std::string nameA(a);
    const std::string nameB(b);
    joints.RemoveCollisionPair(nameA.c_str(), nameB.c_str());
    joints.RemoveCollisionPair(nameB.c_str(), nameA.c_str()); // lol
}

int ApplyCollisionNextConvexSlot(StudioMdlState &ctx, const std::string_view name) {
    CJointedModel &joints = JointedModel(ctx);
    for (int i = 0; i < MAX_EXTRA_COLLISION_MODELS; i++) {
        if (joints.m_ExtraModels[i].m_pSrc == NULL)
            return i;
    }

    MdlWarning("Cannot add more than {} extra collision models. Ignoring $addconvexsrc \"{}\".\n",
               MAX_EXTRA_COLLISION_MODELS, name);
    return -1;
}

void ApplyCollisionConvexSource(StudioMdlState &ctx, const int slot, s_source_t *source) {
    extramodel_t &extra = JointedModel(ctx).m_ExtraModels[slot];
    extra.m_pSrc = source;
    extra.m_matOffset.SetToIdentity();
}

void ApplyCollisionConvexOffset(StudioMdlState &ctx, const int slot, const Vector &position, const QAngle &angles,
                                const float scale) {
    // physics model SMDs are in a different space, so this hacky conversion happens
    // to their offset matrix so the matrices fed to the src combiner are always the same.
    // see: https://intranet.valvesoftware.com/wiki/3D_Coordinate_Systems

    matrix3x4_t matLocal;
    AngleMatrix(angles, position, matLocal);

    matLocal.ScaleUpper3x3Matrix(scale * (1.0f / ctx.script.currentscale));

    matrix3x4_t matConvert;
    matConvert.InitXYZ(Vector(0, 1, 0), Vector(-1, 0, 0), Vector(0, 0, 1), Vector(0, 0, 0));
    ConcatTransforms(matLocal, matConvert.InverseTR(), matLocal);
    matrix3x4_t matRotate;
    matRotate.InitFromQAngles(QAngle(0, 90, 0));
    ConcatTransforms(matRotate, matLocal, matLocal);
    MatrixCopy(matLocal, JointedModel(ctx).m_ExtraModels[slot].m_matOffset);
}

void ApplyCollisionConvexConcave(StudioMdlState &ctx, const int slot) {
    JointedModel(ctx).m_ExtraModels[slot].m_bConcave = true;
}

void CCmd_JointConstrain(CJointedModel &joints, const char *pJointName, const char *pJointAxis, const char *pJointType,
                         const char *pLimitMin, const char *pLimitMax, const char *pFriction) {
    float limitMin = Safe_atof(pLimitMin);
    float limitMax = Safe_atof(pLimitMax);
    float friction = Safe_atof(pFriction);

    int axis = -1;
    if (pJointAxis) {
        axis = tolower(pJointAxis[0]) - 'x';
    }
    pJointName = ApplyCollisionJointConstrainCheck(joints.m_ctx, pJointName, axis, limitMin, limitMax);
    if (!pJointName)
        return;

    CollisionJointType jointType = CollisionJointType::Free;
    if (!stricmp(pJointType, "free")) {
        jointType = CollisionJointType::Free;
    }
    else if (!stricmp(pJointType, "fixed")) {
        jointType = CollisionJointType::Fixed;
    }
    else if (!stricmp(pJointType, "limit")) {
        jointType = CollisionJointType::Limit;
    }
    else {
        MdlWarning("Unknown joint type {} (must be free, fixed, or limit)\n", pJointType);
        return;
    }
    ApplyCollisionJointConstrain(joints.m_ctx, pJointName, axis, jointType, limitMin, limitMax, friction);
}
void CCmd_JointSkip(CJointedModel &joints, const char *pName) {
    ApplyCollisionJointSkip(joints.m_ctx, pName);
}
void CCmd_TotalMass(CJointedModel &joints, const char *pMass) {
    ApplyCollisionMass(joints.m_ctx, Safe_atof(pMass));
}
void CCmd_JointMerge(CJointedModel &joints, const char *pParent, const char *pChild) {
    ApplyCollisionJointMerge(joints.m_ctx, pParent, pChild);
}
void CCmd_JointRoot(CJointedModel &joints, const char *pBone) {
    ApplyCollisionRootBone(joints.m_ctx, pBone);
}
void CCmd_JoinAnimatedFriction(CJointedModel &joints, const char *pMinFriction, const char *pMaxFriction,
                               const char *pTimeIn, const char *pTimeHold, const char *pTimeOut) {
    ApplyCollisionAnimatedFriction(joints.m_ctx, Safe_atoi(pMinFriction), Safe_atoi(pMaxFriction),
                                   Safe_atof(pTimeIn), Safe_atof(pTimeHold), Safe_atof(pTimeOut));
}

//-----------------------------------------------------------------------------
// Purpose: Add a constraint to our joint system
// Input  : &joints -
//			*pJointName -
//			*pJointAxis -
//			*pJointType -
//			*pLimitMin -
//			*pLimitMax -
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: Add a constraint to our joint system
// Input  : &joints -
//			*pJointName -
//			*pJointAxis -
//			*pJointType -
//			*pLimitMin -
//			*pLimitMax -
//-----------------------------------------------------------------------------
#ifdef MDLCOMPILE
void CCmd_JointConstrain(CJointedModel &joints, int nAxis, const char *pJointName,
                         CDmeJointConstrain *pJointConstrain) {
    if (!pJointConstrain)
        return;

    const int jointIndex = FindLocalBoneNamed(joints.m_ctx, joints.m_pModel, pJointName);
    if (!joints.m_ctx.options.createMakefile && jointIndex < 0) {
        MdlWarning("Can't find joint {}\n", pJointConstrain->GetName());
        return;
    }
    pJointName = joints.m_pModel->localBone[jointIndex].name;

    const float limitMin = pJointConstrain->m_aLimitMin.Get();
    const float limitMax = pJointConstrain->m_aLimitMax.Get();
    const float friction = pJointConstrain->m_flFriction.Get();

    if (nAxis < 0 || nAxis > 2 || limitMin > limitMax) {
        MdlError("Invalid joint constraint for {}\nCan't build ragdoll!\n", pJointName);
        return;
    }

    const int nJointType = pJointConstrain->m_nType.Get();
    if (nJointType < 0 || nJointType > 2) {
        MdlWarning("Invalid joint constraint for {}, Unknown joint type {} (must be 0:free, 1:fixed, or 2:limit)\n",
                   pJointName, nJointType);
        return;
    }

    const jointlimit_t jointType = static_cast<jointlimit_t>(nJointType);
    joints.AddConstraint(pJointName, nAxis, jointType, limitMin, limitMax, friction);
}
#endif // #ifdef MDLCOMPILE


//-----------------------------------------------------------------------------
// Purpose: Remove a joint from the system (don't create physical geometry for it)
// Input  : &joints -
//			args[][ARG_SIZE] -
//			argCount -
//-----------------------------------------------------------------------------
// UNDONE: Automatically skip joints that will have mass that is too low?


//-----------------------------------------------------------------------------
// Purpose: Sets the object's mass.  The code will distribute this mass to each
//			part based on the collision model's volume
// Input  : &joints -
//			*pMass -
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: verts from the bone named pChild are added to the collision model of pParent
// Input  : *pmodel - source model
//			*pParent - destination bone name
//			*pChild - source bone name
//-----------------------------------------------------------------------------





#ifdef MDLCOMPILE
void CCmd_JoinAnimatedFriction(CJointedModel &joints, CDmeJointAnimatedFriction *pJaf) {
    if (!pJaf)
        return;

    joints.m_flFrictionTimeIn = pJaf->m_tTimeIn.Get().GetSeconds();
    joints.m_flFrictionTimeOut = pJaf->m_tTimeOut.Get().GetSeconds();
    joints.m_flFrictionTimeHold = pJaf->m_tTimeHold.Get().GetSeconds();
    joints.m_iMinAnimatedFriction = pJaf->m_nMinFriction.Get();
    joints.m_iMaxAnimatedFriction = pJaf->m_nMaxFriction.Get();
    joints.m_bHasAnimatedFriction = true;
}
#endif // #ifdef MDLCOMPILE


//-----------------------------------------------------------------------------
// Purpose: Parses all legal commands inside the $collisionjoints {} block
// Input  : &joints -
//-----------------------------------------------------------------------------
void ParseCollisionCommands(ScriptTokenizer &qc, CJointedModel &joints) {
    char command[512];

    char args[MAX_ARGS][ARG_SIZE];
    int argCount;

    StudioMdlState &ctx = joints.m_ctx;

    ApplyCollisionBeginBlock(ctx);

    while (qc.GetToken(true)) {
        if (!strcmp(qc.token, "}"))
            return;

        strcpy(command, qc.token);

        if (!stricmp(command, "$mass")) {
            argCount = ReadArgs(qc, args, 1);
            CCmd_TotalMass(joints, args[0]);
        }
        // default properties
        else if (!stricmp(command, "$automass")) {
            ApplyCollisionAutoMass(ctx);
        }
        else if (!stricmp(command, "$inertia")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionInertia(ctx, Safe_atof(args[0]));
        }
        else if (!stricmp(command, "$damping")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionDamping(ctx, Safe_atof(args[0]));
        }
        else if (!stricmp(command, "$rotdamping")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionRotDamping(ctx, Safe_atof(args[0]));
        }
        else if (!stricmp(command, "$drag")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionDrag(ctx, Safe_atof(args[0]));
        }
        else if (!stricmp(command, "$rollingDrag")) {
            argCount = ReadArgs(qc, args, 1);
            // JAY: Removed this in favor of heuristic/tuning approach
            //joints.DefaultRollingDrag( Safe_atof( args[0] ) );
        }
        else if (!stricmp(command, "$maxconvexpieces")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionMaxConvexPieces(ctx, Safe_atoi(args[0]));
        }
        else if (!stricmp(command, "$remove2d")) {
            ApplyCollisionRemove2D(ctx);
        }
        else if (!stricmp(command, "$concaveperjoint")) {
            ApplyCollisionConcavePerJoint(ctx);
        }
        else if (!stricmp(command, "$weldposition")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionWeldPosition(ctx, Safe_atof(args[0]));
        }
        else if (!stricmp(command, "$weldnormal")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionWeldNormal(ctx, Safe_atof(args[0]));
        }
        else if (!stricmp(command, "$concave")) {
            ApplyCollisionConcave(ctx);
        }
        else if (!stricmp(command, "$convexhullcountoverride")) {
            argCount = ReadArgs(qc, args, 1);
            ApplyCollisionConvexHullCountOverride(ctx);
        }
        else if (!stricmp(command, "$masscenter")) {
            argCount = ReadArgs(qc, args, 3);
            Vector center;
            center.Init(Safe_atof(args[0]), Safe_atof(args[1]), Safe_atof(args[2]));
            ApplyCollisionMassCenter(ctx, center);
        }
        // joint commands
        else if (!stricmp(command, "$jointskip")) {
            argCount = ReadArgs(qc, args, 1);
            CCmd_JointSkip(joints, args[0]);
        }
        else if (!stricmp(command, "$jointmerge")) {
            argCount = ReadArgs(qc, args, 2);
            CCmd_JointMerge(joints, args[0], args[1]);
        }
        else if (!stricmp(command, "$rootbone")) {
            argCount = ReadArgs(qc, args, 1);
            CCmd_JointRoot(joints, args[0]);
        }
        else if (!stricmp(command, "$jointconstrain")) {
            argCount = ReadArgs(qc, args, 6);
            const char *pFriction = args[5];
            if (argCount < 6) {
                pFriction = "1.0";
            }
            CCmd_JointConstrain(joints, args[0], args[1], args[2], args[3], args[4], pFriction);
        }
        // joint properties
        else if (!stricmp(command, "$jointinertia")) {
            argCount = ReadArgs(qc, args, 2);
            ApplyCollisionJointInertia(ctx, args[0], Safe_atof(args[1]));
        }
        else if (!stricmp(command, "$jointdamping")) {
            argCount = ReadArgs(qc, args, 2);
            ApplyCollisionJointDamping(ctx, args[0], Safe_atof(args[1]));
        }
        else if (!stricmp(command, "$jointrotdamping")) {
            argCount = ReadArgs(qc, args, 2);
            ApplyCollisionJointRotDamping(ctx, args[0], Safe_atof(args[1]));
        }
        else if (!stricmp(command, "$jointmassbias")) {
            argCount = ReadArgs(qc, args, 2);
            ApplyCollisionJointMassBias(ctx, args[0], Safe_atof(args[1]));
        }
        else if (!stricmp(command, "$noselfcollisions")) {
            ApplyCollisionNoSelfCollisions(ctx);
        }
        else if (!stricmp(command, "$jointcollide")) {
            argCount = ReadArgs(qc, args, 2);
            ApplyCollisionJointCollide(ctx, args[0], args[1]);
        }
        else if (!stricmp(command, "$animatedfriction")) {
            argCount = ReadArgs(qc, args, 5);

            if (argCount == 5) {
                CCmd_JoinAnimatedFriction(joints, args[0], args[1], args[2], args[3], args[4]);
            }
        }
        else if (!stricmp(command, "$assumeworldspace")) {
            ApplyCollisionAssumeWorldSpace(ctx);
        }
        else if (!stricmp(command, "$addconvexsrc")) {
            argCount = ReadArgs(qc, args, 1);
            joints.AddConvexSrc(qc, args[0]);
        }
        else if (!stricmp(command, "$jointcollidealltoall")) {
            std::vector<std::string> names;

            qc.GetToken(true);
            if (qc.token[0] == '{') {
                while (qc.GetToken(true) && names.size() < 32 && strcmp(qc.token, "}")) {
                    // the token buffer of the original was 256 bytes
                    names.emplace_back(qc.token.view().substr(0, 255));
                }
            }

            ApplyCollisionJointCollideAllToAll(ctx, names);
        }
        else if (!stricmp(command, "$jointnocollide")) {
            argCount = ReadArgs(qc, args, 2);
            ApplyCollisionJointNoCollide(ctx, args[0], args[1]);
        }
        else {
            MdlWarning("Unknown command {} in collision series\n", command);
        }
    }
}



void LoadSurfacePropsAll() {
    // Loading is disabled below, so there is nothing to load and no "already loaded" state to keep.
    // if ( !TryLoadAllSurfaceProps( GAME_PATH_ID ) )
    // {
    // 	Warning( "surfaceproperties_manifest.txt not found in the game dir, trying exe dir...\n" );
    // 	if ( !TryLoadAllSurfaceProps( EXE_PATH_ID ) )
    // 	{
    // 		#if SUPPORT_EMBEDDED_SURFPROP
    // 		// Last-resort portable fallback generated from resources/scripts.
    // 		Warning( "surfaceproperties_manifest.txt not found, loading embedded fallback...\n" );
    // 		if ( !TryLoadAllEmbeddedSurfaceProps() )
    // 			Warning( "Failed to load embedded surfaceproperties fallback!\n" );
    // 		#else
    // 		Warning( "surfaceproperties_manifest.txt not found, fallback to default property...\n" );
    // 		#endif
    // 	}
    // }
}

//-----------------------------------------------------------------------------
// Purpose: Entry point for script processing.  Delegate to necessary subroutines.
//			Parse the collisionmodel {} and collisionjoints {} chunks
// Input  : separateJoints - whether this has a constraint system or not (true if it does)
// Output : int
//-----------------------------------------------------------------------------
int DoCollisionModel(StudioMdlState &ctx, ScriptTokenizer &qc, bool separateJoints) {
    char name[512];
    s_source_t *pmodel;
    CJointedModel &joints = JointedModel(ctx);

    // name
    qc.ExpectToken();

    strcpyn(name, qc.token);

    //	CreateInterfaceFn physicsFactory = GetPhysicsFactory();
    //	g_pPhysics2 = (IPhysics2*)physicsFactory(VPHYSICS2_INTERFACE_VERSION, NULL);
    LoadSurfacePropsAll();

    const CollisionMaterialCounts materialCounts = GetCollisionMaterialCounts(ctx);

    // Special case where the input collision is empty.
    // This means that a list of collision meshes follows.
    if (!V_strcmp(name, "blank")) {
        ApplyCollisionBlankRoot(ctx);

        // joints.m_pModel is still NULL,
        // which will be handled when the additional meshes are appended.
    }
    else {
        pmodel = Load_Source(ctx, name, "", false, false, false /*,g_collisionBindToOneBone*/);
        if (!pmodel)
            return 0;

        // auto-remove any new materials/textures
        ApplyCollisionDropNewMaterials(ctx, pmodel, materialCounts);

        // all bones map to themselves by default
        ApplyCollisionSource(ctx, pmodel);
    }

    bool parseCommands = false;

    // If the next token is a { that means a data block for the collision model
    if (qc.GetToken(true)) {
        if (!strcmp(qc.token, "{")) {
            parseCommands = true;
        }
        else {
            qc.UnGetToken();
        }
    }

    if (parseCommands) {
        ParseCollisionCommands(qc, joints);
    }

    ApplyCollisionJointed(ctx, separateJoints);

    // collision script is stored in ctx.collision.jointedModel for later processing
    return 1;
}


//-----------------------------------------------------------------------------
// Purpose: Walk the list of models, add up the volume
// Input  : *pList -
// Output : float
//-----------------------------------------------------------------------------
float TotalVolume(CPhysCollisionModel *pList) {
    float volume = 0;
    while (pList) {
        volume += pList->m_volume * pList->m_massBias;
        pList = pList->m_pNext;
    }

    return volume;
}

//-----------------------------------------------------------------------------
// Purpose: Write key/value pairs out to the text of the .phy file
// Input  : &out - the file contents being built
//			*pKeyName - key name
//			outputData - type specific output data
//-----------------------------------------------------------------------------
void KeyWriteInt(std::string &out, const char *pKeyName, int outputData) {
    std::format_to(std::back_inserter(out), "\"{}\" \"{}\"\n", pKeyName, outputData);
}

void KeyWriteIntPair(std::string &out, const char *pKeyName, int outputData0, int outputData1) {
    std::format_to(std::back_inserter(out), "\"{}\" \"{},{}\"\n", pKeyName, outputData0, outputData1);
}

void KeyWriteString(std::string &out, const char *pKeyName, const char *outputData) {
    std::format_to(std::back_inserter(out), "\"{}\" \"{}\"\n", pKeyName, outputData);
}

void KeyWriteFloat(std::string &out, const char *pKeyName, float outputData) {
    std::format_to(std::back_inserter(out), "\"{}\" \"{:f}\"\n", pKeyName, outputData);
}


void CJointedModel::FixCollisionHierarchy() {
    if (m_pCollisionList) {
        CPhysCollisionModel *pPhys = m_pCollisionList;

        FixBoneList();
        // Point parents at joints that are actually in the model
        for (; pPhys; pPhys = pPhys->m_pNext) {
            pPhys->m_parent = FixParent(pPhys->m_parent);
        }

        // sort the list so parents come before children
        SortCollisionList();
        // Now remap the constraints to bones to
        // Now that bones are in order, set physics indices in main bone structure

        CJointConstraint *pList = m_pConstraintList;
        while (pList) {
            pList->m_pJointName = FixParent(pList->m_pJointName);
            pList = pList->m_pNext;
        }

        pPhys = m_pCollisionList;
        int i;
        for (i = 0; i < std::ssize(m_ctx.bones.bonetable); i++) {
            m_ctx.bones.bonetable[i].physicsBoneIndex = -1;
        }
        int index = 0;
        while (pPhys) {
            int boneIndex = FindBoneInTable(m_ctx, pPhys->m_name);
            if (boneIndex >= 0) {
                m_ctx.bones.bonetable[boneIndex].physicsBoneIndex = index;
            }
            pPhys = pPhys->m_pNext;
            index++;
        }
        for (i = 0; i < std::ssize(m_ctx.bones.bonetable); i++) {
            // if no bone was set, set to parent bone
            if (m_ctx.bones.bonetable[i].physicsBoneIndex < 0) {
                int index = m_ctx.bones.bonetable[i].parent;
                int bone = -1;
                while (index >= 0) {
                    bone = m_ctx.bones.bonetable[index].physicsBoneIndex;
                    if (bone >= 0)
                        break;
                    index = m_ctx.bones.bonetable[index].parent;
                }

                // found one?
                if (bone >= 0) {
                    m_ctx.bones.bonetable[i].physicsBoneIndex = bone;
                }
                else {
                    // just set physics to affect root
                    m_ctx.bones.bonetable[i].physicsBoneIndex = 0;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: Builds the physics/collision model.
//			This must execute after the model has been simplified!!
//-----------------------------------------------------------------------------
void CollisionModel_Build(StudioMdlState &ctx) {
    vstd::FunctionTimingRecorder _("Phys");
    CJointedModel &joints = JointedModel(ctx);
    // no collision model referenced
    if (!joints.m_pModel && !joints.m_bRootCollisionIsEmpty)
        return;

    MdlWarning("Collision building...\n");

    //	Physics2Collision_Build(&joints);

    joints.Simplify();
    if (joints.m_isJointed) {
        joints.ProcessJointedModel();
    }
    else {
        joints.ProcessSingleBody();
    }
    joints.FixCollisionHierarchy();
    MdlMessage("Collision model completed.\n");
    joints.ComputeMass();
}

static void BuildRagdollConstraint(CJointedModel &joints, CPhysCollisionModel *pPhys,
                                   constraint_ragdollparams_t &ragdoll) {
    vstd::FunctionTimingRecorder _("Phys");
    memset(&ragdoll, 0, sizeof(ragdoll));
    ragdoll.parentIndex = joints.CollisionIndex(pPhys->m_parent);
    ragdoll.childIndex = joints.CollisionIndex(pPhys->m_name);
    if (ragdoll.parentIndex < 0 || ragdoll.childIndex < 0) {
        MdlWarning("Constraint between bone {} and {}\n", pPhys->m_name, pPhys->m_parent);
        if (ragdoll.childIndex < 0)
            MdlWarning("\"{}\" does not appear in collision model!!!\n", pPhys->m_name);
        if (ragdoll.parentIndex < 0)
            MdlWarning("\"{}\" does not appear in collision model!!!\n", pPhys->m_parent);
        MdlError("Bad constraint in ragdoll\n");
    }
    CJointConstraint *pList = joints.m_pConstraintList;
    while (pList) {
        int index = joints.CollisionIndex(pList->m_pJointName);
        CPhysCollisionModel *pListModel = joints.GetCollisionModel(pList->m_pJointName);
        if (index < 0) {
            MdlError("Rotation constraint on bone \"{}\" which does not appear in collision model!!!\n",
                     pList->m_pJointName);
        }
        else if ((!pListModel->m_parent || joints.CollisionIndex(pListModel->m_parent) < 0) && stricmp(
                     pList->m_pJointName, joints.m_rootName)) {
            MdlError("Rotation constraint on bone \"{}\" which has no parent!!!\n", pList->m_pJointName);
        }
        else if (index == ragdoll.childIndex) {
            switch (pList->m_jointType) {
                case JOINT_LIMIT:
                    ragdoll.axes[pList->m_axis].
                            SetAxisFriction(pList->m_limitMin, pList->m_limitMax, pList->m_friction);
                    break;
                case JOINT_FIXED:
                    ragdoll.axes[pList->m_axis].SetAxisFriction(0, 0, 0);
                    break;
                case JOINT_FREE:
                    ragdoll.axes[pList->m_axis].SetAxisFriction(-360, 360, pList->m_friction);
                    break;
            }
        }
        pList = pList->m_pNext;
    }
}

//-----------------------------------------------------------------------------
// Purpose: Write out any data that's been saved in the globals
//-----------------------------------------------------------------------------
void CollisionModel_Write(StudioMdlState &ctx, long checkSum) {
    //	Physics2Collision_Write();

    vstd::FunctionTimingRecorder _("Phys");
    CJointedModel &joints = JointedModel(ctx);
    if (joints.m_pCollisionList) {
        CPhysCollisionModel *pPhys = joints.m_pCollisionList;

        std::string filename = ctx.paths.gameDir + "models/";
        filename += joints.m_OverrideName.empty() ? ctx.paths.outname.c_str() : joints.m_OverrideName.c_str();

        float volume = TotalVolume(pPhys);
        if (volume <= 0)
            volume = 1;

        MdlMessage("Collision model volume {:.2f} in^3\n", volume);

        vstd::paths::set_extension(filename, ".phy");

        // The file is built in memory and written with the other model files (CommitOutputFiles)
        std::string out;

        // write out the collision header (size is version)
        phyheader_t header;
        header.size = sizeof(header);
        header.id = 0;
        header.checkSum = checkSum;

        header.solidCount = 0;
        pPhys = joints.m_pCollisionList;
        while (pPhys) {
            header.solidCount++;
            pPhys = pPhys->m_pNext;
        }

        out.append(reinterpret_cast<const char *>(&header), sizeof(header));

        // Write out the binary physics collision data
        pPhys = joints.m_pCollisionList;
        while (pPhys) {
            int size = g_StudioCollision.CollideSize(pPhys->m_pCollisionData);
            out.append(reinterpret_cast<const char *>(&size), sizeof(int));
            std::vector<char> buf(size);
            g_StudioCollision.CollideWrite(buf.data(), pPhys->m_pCollisionData);
            out.append(buf.data(), size);
            pPhys = pPhys->m_pNext;
        }

        // write out the properties of each solid
        int solidIndex = 0;
        pPhys = joints.m_pCollisionList;
        while (pPhys) {
            pPhys->m_mass = ((pPhys->m_volume * pPhys->m_massBias) / volume) * joints.m_totalMass;
            if (pPhys->m_mass < 1.0)
                pPhys->m_mass = 1.0;

            out += "solid {\n";
            KeyWriteInt(out, "index", solidIndex);
            KeyWriteString(out, "name", pPhys->m_name);
            if (pPhys->m_parent) {
                KeyWriteString(out, "parent", pPhys->m_parent);
            }

            KeyWriteFloat(out, "mass", pPhys->m_mass);
            //KeyWriteFloat( out, "volume", pPhys->m_volume );

            char *pSurfaceProps = GetSurfaceProp(ctx, pPhys->m_name);

            KeyWriteString(out, "surfaceprop", pSurfaceProps);
            KeyWriteFloat(out, "damping", pPhys->m_damping);
            KeyWriteFloat(out, "rotdamping", pPhys->m_rotdamping);

            if (pPhys->m_dragCoefficient != -1) {
                KeyWriteFloat(out, "drag", pPhys->m_dragCoefficient);
            }
            KeyWriteFloat(out, "inertia", pPhys->m_inertia);
            KeyWriteFloat(out, "volume", pPhys->m_volume);
            if (pPhys->m_massBias != 1.0f) {
                KeyWriteFloat(out, "massbias", pPhys->m_massBias);
            }

            out += "}\n";
            pPhys = pPhys->m_pNext;
            solidIndex++;
        }

        // by default, write constraints from each limb to its parent
        pPhys = joints.m_pCollisionList;
        while (pPhys) {
            // check to see if bone collapse/remap has left this with parent pointing at itself
            if (pPhys->m_parent) {
                constraint_ragdollparams_t ragdoll;
                BuildRagdollConstraint(joints, pPhys, ragdoll);
                if (ragdoll.parentIndex != ragdoll.childIndex) {
                    out += "ragdollconstraint {\n";
                    KeyWriteInt(out, "parent", ragdoll.parentIndex);
                    KeyWriteInt(out, "child", ragdoll.childIndex);
                    KeyWriteFloat(out, "xmin", ragdoll.axes[0].minRotation);
                    KeyWriteFloat(out, "xmax", ragdoll.axes[0].maxRotation);
                    KeyWriteFloat(out, "xfriction", ragdoll.axes[0].torque);
                    KeyWriteFloat(out, "ymin", ragdoll.axes[1].minRotation);
                    KeyWriteFloat(out, "ymax", ragdoll.axes[1].maxRotation);
                    KeyWriteFloat(out, "yfriction", ragdoll.axes[1].torque);
                    KeyWriteFloat(out, "zmin", ragdoll.axes[2].minRotation);
                    KeyWriteFloat(out, "zmax", ragdoll.axes[2].maxRotation);
                    KeyWriteFloat(out, "zfriction", ragdoll.axes[2].torque);
                    out += "}\n";
                }
            }
            pPhys = pPhys->m_pNext;
        }
        if (joints.m_noSelfCollisions) {
            out += "collisionrules {\n";
            KeyWriteInt(out, "selfcollisions", 0);
            out += "}\n";
        }
        else if (joints.m_pCollisionPairs) {
            out += "collisionrules {\n";
            collisionpair_t *pPair = joints.m_pCollisionPairs;
            while (pPair) {
                pPair->obj0 = joints.CollisionIndex(pPair->pName0);
                pPair->obj1 = joints.CollisionIndex(pPair->pName1);
                if (pPair->obj0 >= 0 && pPair->obj1 >= 0 && pPair->obj0 != pPair->obj1) {
                    KeyWriteIntPair(out, "collisionpair", pPair->obj0, pPair->obj1);
                }
                else {
                    MdlWarning("Invalid collision pair ({}, {})\n", pPair->pName0, pPair->pName1);
                }
                pPair = pPair->pNext;
            }
            out += "}\n";
        }

        if (joints.m_bHasAnimatedFriction == true) {
            out += "animatedfriction {\n";
            KeyWriteFloat(out, "animfrictionmin", joints.m_iMinAnimatedFriction);
            KeyWriteFloat(out, "animfrictionmax", joints.m_iMaxAnimatedFriction);
            KeyWriteFloat(out, "animfrictiontimein", joints.m_flFrictionTimeIn);
            KeyWriteFloat(out, "animfrictiontimeout", joints.m_flFrictionTimeOut);
            KeyWriteFloat(out, "animfrictiontimehold", joints.m_flFrictionTimeHold);
            out += "}\n";
        }

        // block that is only parsed by the editor
        out += "editparams {\n";
        KeyWriteString(out, "rootname", joints.m_rootName);
        KeyWriteFloat(out, "totalmass", joints.m_totalMass);
        if (joints.m_allowConcave) {
            KeyWriteInt(out, "concave", 1);
        }
        for (size_t k = 0; k < joints.m_mergeList.size(); k++) {
            char buf[512];
            Q_snprintf(buf, sizeof(buf), "%s,%s", joints.m_mergeList[k].pParent.c_str(),
                       joints.m_mergeList[k].pChild.c_str());
            KeyWriteString(out, "jointmerge", buf);
        }

        out += "}\n";

        if (!joints.m_textCommands.empty()) {
            out.append(reinterpret_cast<const char *>(joints.m_textCommands.data()), joints.m_textCommands.size());
        }
        out += '\0';

        vstd::paths::fix_slashes(filename);
        SaveOutputFile(ctx, filename, out.data(), out.size());
    }
}


#ifdef MDLCOMPILE
//-----------------------------------------------------------------------------
// mdlcompile
//
// mdlcompile uses DMX instead of qc as input
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: Parses all legal commands inside the $collisionjoints {} block
// Input  : &joints -
//-----------------------------------------------------------------------------
void ParseCollisionCommands(CJointedModel &joints, CDmeCollisionModel *pCollisionModel, bool bStaticProp) {
    joints.m_isJointed = false;

    if (!pCollisionModel)
        return;

    if (pCollisionModel->m_bAutomaticMassComputation.Get()) {
        joints.SetAutoMass();
    }
    else {
        joints.SetTotalMass(pCollisionModel->m_flMass.Get());
    }

    joints.DefaultInertia(pCollisionModel->m_flInertia.Get());
    joints.DefaultDamping(pCollisionModel->m_flDamping.Get());
    joints.DefaultRotdamping(pCollisionModel->m_flRotationalDamping.Get());
    joints.DefaultDrag(pCollisionModel->m_flDrag.Get());
    joints.SetMaxConvex(pCollisionModel->m_nMaxConvexPieces.Get());

    if (pCollisionModel->m_bRemove2D.Get()) {
        joints.Remove2DConvex();
    }

    float flWeld = pCollisionModel->m_flWeldPositionTolerance.Get();
    if (flWeld < 0.0f) {
        MdlWarning("1003: Invalid negative weld position tolerance ({:f}), ignoring and using {:f}\n", flWeld,
                   joints.m_weldVertEpsilon);
    }
    else {
        joints.m_weldVertEpsilon = flWeld;
    }

    flWeld = pCollisionModel->m_flWeldNormalTolerance.Get();
    if (flWeld < 0.0f) {
        MdlWarning("1004: Invalid negative weld normal tolerance ({:f}), ignoring and using {:f}\n", flWeld,
                   joints.m_weldNormalEpsilon);
    }
    else if (flWeld > joints.m_weldNormalEpsilon) {
        MdlWarning("1005: Weld normal tolerance too high ({:f}), should be slightly less than 1, ignoring and using {:f}\n",
                   flWeld, joints.m_weldNormalEpsilon);
    }
    else {
        joints.m_weldNormalEpsilon = flWeld;
    }

    if (pCollisionModel->m_bConcave.Get()) {
        joints.AllowConcave();
    }

    if (pCollisionModel->m_bForceMassCenter.Get()) {
        joints.ForceMassCenter(pCollisionModel->m_vecMassCenter.Get());
    }

    joints.m_bAssumeWorldspace = pCollisionModel->m_bAssumeWorldSpace.Get();

    CDmeCollisionJoints *pCollisionJoints = CastElement<CDmeCollisionJoints>(pCollisionModel);
    if (!bStaticProp && pCollisionJoints) {
        if (pCollisionJoints->m_bConcavePerJoint.Get()) {
            joints.AllowConcaveJoints();
        }

        if (!pCollisionJoints->m_bSelfCollisions.Get()) {
            joints.SetNoSelfCollisions();
        }

        if (!pCollisionJoints->m_bSelfCollisions.Get()) {
            joints.SetNoSelfCollisions();
        }

        CCmd_JointRoot(joints, pCollisionJoints->m_RootBone.Get());
        CCmd_JoinAnimatedFriction(joints, pCollisionJoints->m_AnimatedFriction.GetElement());

        for (int nIndex = 0; nIndex < pCollisionJoints->m_JointSkipList.size(); ++nIndex) {
            CCmd_JointSkip(joints, pCollisionJoints->m_JointSkipList.Element(nIndex));
        }

        int nValidCollisionJointCount = 0;

        for (int i = 0; i < pCollisionJoints->m_JointList.size(); ++i) {
            const CDmeCollisionJoint *pCollisionJoint = pCollisionJoints->m_JointList.Element(i);
            if (!pCollisionJoint) {
                MdlWarning("1000: root.collisionModel.joints[ {} ] exists but undefined\n", i);
                continue;
            }

            const char *pJointName = pCollisionJoint->GetName().data();

            const int nJointIndex = FindLocalBoneNamed(joints.m_ctx, joints.m_pModel, pJointName);
            if (!joints.m_ctx.options.createMakefile && nJointIndex < 0) {
                MdlWarning(
                    "1001: root.collisionModel.joints[ {} ] refers to joint \"{}\" but that joint wasn't defined in the model\n",
                    i, pJointName);
                continue;
            }

            pJointName = joints.m_pModel->localBone[nJointIndex].name;

            joints.JointMassBias(pJointName, pCollisionJoint->m_flMassBias.Get());
            joints.JointInertia(pJointName, pCollisionJoint->m_flInertia.Get());
            joints.JointDamping(pJointName, pCollisionJoint->m_flDamping.Get());
            joints.JointRotdamping(pJointName, pCollisionJoint->m_flRotDamping.Get());

            CCmd_JointConstrain(joints, 0 /* x */, pJointName, pCollisionJoint->m_ConstrainX.GetElement());
            CCmd_JointConstrain(joints, 1 /* y */, pJointName, pCollisionJoint->m_ConstrainY.GetElement());
            CCmd_JointConstrain(joints, 2 /* z */, pJointName, pCollisionJoint->m_ConstrainZ.GetElement());

            for (int j = 0; j < pCollisionJoint->m_JointMergeList.size(); ++j) {
                CCmd_JointMerge(joints, pJointName, pCollisionJoint->m_JointMergeList.Element(j));
            }

            for (int j = 0; j < pCollisionJoint->m_JointMergeList.size(); ++j) {
                joints.AppendCollisionPair(pJointName, pCollisionJoint->m_JointCollideList.Element(j));
            }

            ++nValidCollisionJointCount;
        }

        if (nValidCollisionJointCount > 0) {
            joints.m_isJointed = true;
        }
        else {
            MdlWarning("1002: Jointed collision model defined but no valid CDmeCollisionJoints, making non-jointed\n");
        }
    }
}


//-----------------------------------------------------------------------------
// Purpose: Entry point for script processing. This version is used by preprocessed files
//-----------------------------------------------------------------------------
int DoCollisionModel(StudioMdlState &ctx, s_source_t *pSource, CDmElement *pInfo, bool bStaticProp) {
    CDmeCollisionModel *pCollisionModel = CastElement<CDmeCollisionModel>(pInfo);
    if (!pCollisionModel)
        return 0;

    LoadSurfacePropsAll();

    CJointedModel &joints = JointedModel(ctx);
    // all bones map to themselves by default
    joints.SetSource(pSource);
    ParseCollisionCommands(joints, pCollisionModel, bStaticProp);

    // collision script is stored in ctx.collision.jointedModel for later processing
    return 1;
}


//-----------------------------------------------------------------------------
// Loads collision text keyvalues from the passed string
// NOTE: It essentially is packing keyValues -> keyValues
//       but the .phy keyValues parser is more particular
//       about the format
//-----------------------------------------------------------------------------
void LoadCollisionText(StudioMdlState &ctx, const char *pszKeyValues) {
    if (!pszKeyValues)
        return;

    KeyValues *pKeyValues = new KeyValues("collisionText");
    if (!pKeyValues)
        return;

    KeyValues::AutoDelete adKeyValues(pKeyValues);

    pKeyValues->UsesEscapeSequences(true);
    if (pKeyValues->LoadFromBuffer("collisionText", pszKeyValues)) {
        CJointedModel &joints = JointedModel(ctx);
        while (pKeyValues) {
            joints.AddText(pKeyValues->GetName());

            joints.AddText(" {");
            for (KeyValues *pKv = pKeyValues->GetFirstValue(); pKv; pKv = pKv->GetNextValue()) {
                joints.AddText(" \"");
                joints.AddText(pKv->GetName());
                joints.AddText("\" \"");
                joints.AddText(pKv->GetString());
                joints.AddText("\"");
            }
            joints.AddText(" }\n");

            pKeyValues = pKeyValues->GetNextKey();
        }
    }
}

#endif // #ifdef MDLCOMPILE
