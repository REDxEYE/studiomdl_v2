//
// Created by red_eye on 11/5/26.
//

#include "dmxsupport.hpp"
#include "movieobjects/dmeattributereference.h"
#include "movieobjects/dmeconnectionoperator.h"
#include "movieobjects/dmemodel.h"
#include "movieobjects/dmemesh.h"
#include "movieobjects/dmefaceset.h"
#include "movieobjects/dmematerial.h"
#include "movieobjects/dmeclip.h"
#include "movieobjects/dmechannel.h"
#include "movieobjects/dmeattachment.h"
#include "movieobjects/dmeanimationlist.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmerigconstraintoperators.h"
#include "mdlobjects/dmeeyeball.h"
#include "mdlobjects/dmeeyelid.h"
#include "mdlobjects/dmejigglebone.h"
#include "mdlobjects/dmemouth.h"


// Local includes
#include "bspflags.h"
#include "studiomdl_commands.h"
#include "mdlobjects/dmeanimblocksize.h"
#include "mdlobjects/dmeanimcmd.h"
#include "mdlobjects/dmebodygroup.h"
#include "mdlobjects/dmebodygrouplist.h"
#include "mdlobjects/dmebodypart.h"
#include "mdlobjects/dmeboneflexdriver.h"
#include "studiomdl/studiomdl.h"
#include "tier1/fmtstr.h"
#include "mdlobjects/dmebonemask.h"
#include "mdlobjects/dmebonemasklist.h"
#include "mdlobjects/dmeboneweight.h"
#include "mdlobjects/dmecollisionmodel.h"
#include "mdlobjects/dmedefinebone.h"
#include "mdlobjects/dmedefinebonelist.h"
#include "mdlobjects/dmeeyeballglobals.h"
#include "mdlobjects/dmehitbox.h"
#include "mdlobjects/dmehitboxset.h"
#include "mdlobjects/dmehitboxsetlist.h"
#include "mdlobjects/dmeik.h"
#include "mdlobjects/dmeincludemodellist.h"
#include "mdlobjects/dmelodlist.h"
#include "mdlobjects/dmematerialgroup.h"
#include "mdlobjects/dmematerialgrouplist.h"
#include "mdlobjects/dmemotioncontrol.h"
#include "mdlobjects/dmeposeparameter.h"
#include "mdlobjects/dmeposeparameterlist.h"
#include "mdlobjects/dmesequence.h"
#include "mdlobjects/dmesequencelist.h"
#include "studiomdl/collisionmodel.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"


struct BoneTransformMap_t {
    // Number of bones
    int m_nBoneCount;

    // The order in which transforms appear in this list specifies their bone indices
    CDmeTransform *m_ppTransforms[MAXSTUDIOSRCBONES];

    // m_pnDmeModelToMdl[bone index in DmeModel] == bone index in studiomdl
    int m_pnDmeModelToMdl[MAXSTUDIOSRCBONES];

    // m_pnMdlToDmeModel[bone index in studiomdl] == bone index in DmeModel
    int m_pnMdlToDmeModel[MAXSTUDIOSRCBONES];
};


static s_source_t *AllocateDmxSource(const char *pSourceName) {
    // Allocate a new source
    s_source_t *pSource = c_allocate<s_source_t>();
    g_studiomdl.sources.emplace_back(pSource);
    pSource->filename = pSourceName;
    vstd::paths::set_extension(pSource->filename, "dmx");
    VectorCopy(g_studiomdl.defaultadjust, pSource->adjust);
    pSource->scale = 1.0f;
    pSource->rotation = g_studiomdl.defaultrotation;
    return pSource;
}

static int LodDistanceCompare(const void *elem1, const void *elem2) {
    const CDmeLOD *l1 = *(const CDmeLOD **) elem1;
    const CDmeLOD *l2 = *(const CDmeLOD **) elem2;
    if (l1->m_bIsShadowLOD != l2->m_bIsShadowLOD)
        return l1->m_bIsShadowLOD ? 1 : -1;
    if (l1->m_flSwitchMetric > l2->m_flSwitchMetric)
        return 1;
    return (l1->m_flSwitchMetric == l2->m_flSwitchMetric) ? 0 : -1;
}

static bool LoadLODs(s_source_t **ppRootLODSource, CDmeLODList *pLodList, bool &bSetUpAxis, bool bStaticProp) {
    // NOTE: This is a little bit of a hack; it generates new s_source_ts
    // based on the LOD data, which is somewhat contrary to the existing
    // architecture, but is the easiest method of achieving the goal
    // of loading LODs from the mpp file without reloading the giant mpp
    // file over and over again.
    *ppRootLODSource = nullptr;
    int nCount = pLodList->m_LODs.Count();
    if (nCount == 0)
        return true;

    // Sort LODs by switch distance
    CDmeLOD **ppLODs = (CDmeLOD **) stackalloc(nCount * sizeof(CDmeLOD*));
    for (int i = 0; i < nCount; ++i) {
        ppLODs[i] = pLodList->m_LODs[i];
    }
    qsort(ppLODs, nCount, sizeof(CDmeLOD *), LodDistanceCompare);

    CDmeLOD *pRootLOD = pLodList->GetRootLOD();

    if (pRootLOD && bSetUpAxis) {
        CDmeModel *pDmeModel = pRootLOD->GetValueElement<CDmeModel>("model");
        if (pDmeModel) {
            const char *pUpAxis = pDmeModel->GetValueString("upAxis");
            if (pUpAxis) {
                if (StringHasPrefix(pUpAxis, "Y")) {
                    // rotate 90 degrees around x to move y into z
                    g_studiomdl.defaultrotation = RadianEuler(M_PI / 2.0f, 0.0f, M_PI / 2.0f);
                }
            }
            bSetUpAxis = false;
        }
    }

    char pSrcLODName[MAX_PATH];
    Q_StripExtension(pRootLOD->GetName(), pSrcLODName, sizeof(pSrcLODName));
    for (int i = 0; i < nCount; ++i) {
        CDmeLOD *pLOD = ppLODs[i];

        bool bIsShadowLOD = pLOD->m_bIsShadowLOD;
        if (bIsShadowLOD) {
            if ((g_studiomdl.gflags & STUDIOHDR_FLAGS_HASSHADOWLOD) != 0) {
                MdlError("Invalid LOD: \"%s\": Multiple Shadow LODs Defined\n", pLOD->GetName());
                return false;
            }
            g_studiomdl.gflags |= STUDIOHDR_FLAGS_HASSHADOWLOD;
        }
        else if (pLOD->m_flSwitchMetric.Get() < 0.0f) {
            MdlError("Invalid LOD: \"%s\": Negative switch value\n", pLOD->GetName());
            return false;
        }

        // Skip lower LODs if we're stripping them
        bool bIsRootLOD = (pLOD == pRootLOD);
        if (g_StudioMdlContext.stripLods && !bIsShadowLOD && (!bIsRootLOD)) {
            if (!g_StudioMdlContext.quiet) {
                Msg("Stripped lod \"%s\" @ %.1f\n", pLOD->m_Model.GetElement() ? pLOD->m_Model->GetName() : "<none>",
                    pLOD->m_flSwitchMetric);
            }
            continue;
        }

        // Now, fill her up! No model means LOD is valid, but has no geometry
        const bool bHasModel = pLOD->m_Model.GetHandle() != DMELEMENT_HANDLE_INVALID;
        const bool bHasSkeleton = pLOD->m_Skeleton.GetHandle() != DMELEMENT_HANDLE_INVALID;

        // No model & no skeleton means LOD is invalid
        if (!bHasModel && !bHasSkeleton) {
            MdlError("Invalid LOD: \"%s\": No Model or Skeleton defined\n", pLOD->GetName());
            return false;
        }

        if (g_studiomdl.ScriptLODs.Count() == MAX_NUM_LODS) {
            MdlError("Too many LODs (MAX_NUM_LODS==%d) while loading LOD: \"%s\"\n", static_cast<int>(MAX_NUM_LODS),
                     pLOD->GetName());
            return false;
        }

        // Check for overflow
        if (g_studiomdl.sources.full()) {
            MdlError("Too many source models/animations (MAXSTUDIOSEQUENCES==%d) while loading LOD: \"%s\"\n",
                     static_cast<int>(MAXSTUDIOSEQUENCES), pLOD->GetName());
            return false;
        }

        s_source_t *pLODSource = AllocateDmxSource(pLOD->m_Path.Get());

        if (!pLODSource) {
            MdlError("Couldn't allocate new source while loading LOD: \"%s\"\n", pLOD->GetName());
            return false;
        }

        if (bIsRootLOD) {
            *ppRootLODSource = pLODSource;
            pLODSource->isActiveModel = true;
        }

        if (bHasModel) {
            BoneTransformMap_t boneMap;
            if (!LoadModelAndSkeleton(pLODSource, boneMap, pLOD->m_Skeleton, pLOD->m_Model, pLOD->m_CombinationOperator,
                                      bStaticProp)) {
                MdlError("Couldn't load skeleton and model while loading LOD: \"%s\"\n", pLOD->GetName());
                return false;
            }

            LoadQcModelElements(pLODSource, g_pCurrentModel, pLOD->m_Model);
        }

        if (bIsRootLOD)
            continue;

        // Create LOD information in terms of how the old system does it
        // Shadow lod reserves -1 as switch value which uniquely identifies a shadow lod
        int j = g_studiomdl.ScriptLODs.AddToTail();
        LodScriptData_t &lod = g_studiomdl.ScriptLODs[j];
        lod.switchValue = bIsShadowLOD ? -1.0f : pLOD->m_flSwitchMetric;
        lod.EnableFacialAnimation(bIsShadowLOD ? false : !pLOD->m_bNoFlex);
        lod.StripFromModel(false);

        // We only support simple model replacement here. Other processing
        // is expected to have occurred in the preprocessing phase
        j = lod.modelReplacements.AddToTail();
        CLodScriptReplacement_t &replacement = lod.modelReplacements[j];
        replacement.SetSrcName(pSrcLODName);
        replacement.SetDstName(pLODSource->filename.c_str());
        replacement.m_pSource = pLODSource;
    }

    return true;
}

static void LoadEyeballGlobals(CDmeEyeballGlobals *pDmeEyeballGlobals) {
    if (!pDmeEyeballGlobals)
        return;

    g_studiomdl.flMaxEyeDeflection = cosf(DEG2RAD(pDmeEyeballGlobals->m_flMaxEyeDeflection.Get()));

    matrix3x4_t mDefRot;
    AngleMatrix(g_studiomdl.defaultrotation, mDefRot);
    VectorIRotate(pDmeEyeballGlobals->m_vEyePosition.Get(), mDefRot, g_studiomdl.eyeposition);
}

static void LoadEyeballs(s_model_t *ps_model_t, CDmeLODList *pDmeLODList) {
    if (!pDmeLODList)
        return;

    matrix3x4_t mDefRot;
    AngleMatrix(g_studiomdl.defaultrotation, mDefRot);
    Vector vTmp;

    const int nEyeballCount = pDmeLODList->m_EyeballList.Count();
    if (nEyeballCount <= 0)
        return;

    for (int nEyeballIndex = 0; nEyeballIndex < nEyeballCount; ++nEyeballIndex) {
        CDmeEyeball *pDmeEyeball = pDmeLODList->m_EyeballList.Get(nEyeballIndex);
        if (!pDmeEyeball)
            continue;

        if (ps_model_t->eyeballs.full()) {
            MdlWarning("1100: Max number of eyeballs reached for model %s, ignoring eyeball %s\n",
                       ps_model_t->name.c_str(),
                       pDmeEyeball->GetName());
            continue;
        }

        int nFoundBoneIndex = -1;
        for (int nSearchBoneIndex = 0; nSearchBoneIndex < ps_model_t->source->numbones; ++nSearchBoneIndex) {
            if (!Q_stricmp(ps_model_t->source->localBone[ nSearchBoneIndex ].name,
                           pDmeEyeball->m_sParentBoneName.Get())) {
                nFoundBoneIndex = nSearchBoneIndex;
                break;
            }
        }

        if (nFoundBoneIndex < 0) {
            MdlWarning("1101: Couldn't find bone %s on model %s, ignoring eyeball %s\n",
                       pDmeEyeball->m_sParentBoneName.Get(), ps_model_t->name.c_str(), pDmeEyeball->GetName());
            continue;
        }

        const char *pszMaterialName = pDmeEyeball->m_sMaterialName.Get();
        const bool bRelative = (strchr(pszMaterialName, '/') || strchr(pszMaterialName, '\\')) ? true : false;

        const int nSearchMeshMatIndex = UseTextureAsMaterial(
            LookupTexture(pDmeEyeball->m_sMaterialName.Get(), bRelative));
        int nFoundMeshMatIndex = -1;
        for (int i = 0; i < ps_model_t->source->nummeshes; ++i) {
            const int nTmpMeshMatIndex = ps_model_t->source->meshindex[i];
            // meshes are internally stored by material index

            if (nTmpMeshMatIndex == nSearchMeshMatIndex) {
                nFoundMeshMatIndex = i;
                break;
            }
        }

        if (nFoundMeshMatIndex < 0) {
            MdlWarning("1102: Couldn't find eyeball material %s on model %s, ignoring eyeball %s\n",
                       pDmeEyeball->m_sMaterialName.Get(), ps_model_t->name.c_str(), pDmeEyeball->GetName());
            continue;
        }

        s_eyeball_t *ps_eyeball_t = &ps_model_t->eyeballs.emplace_back();
        Q_strncpy(ps_eyeball_t->name, pDmeEyeball->GetName(), sizeof(ps_eyeball_t->name));
        ps_eyeball_t->bone = nFoundBoneIndex;
        ps_eyeball_t->mesh = nFoundMeshMatIndex;
        ps_eyeball_t->radius = pDmeEyeball->m_flRadius.Get();
        ps_eyeball_t->zoffset = tan(DEG2RAD(pDmeEyeball->m_flYawAngle.Get()));
        ps_eyeball_t->iris_scale = 1.0f / pDmeEyeball->m_flIrisScale;

        // translate eyeball into bone space
        VectorITransform(pDmeEyeball->m_vPosition.Get(), ps_model_t->source->boneToPose[ps_eyeball_t->bone],
                         ps_eyeball_t->org);

        VectorIRotate(Vector(0, 0, 1), mDefRot, vTmp);
        VectorIRotate(vTmp, ps_model_t->source->boneToPose[ps_eyeball_t->bone], ps_eyeball_t->up);

        VectorIRotate(Vector(1, 0, 0), mDefRot, vTmp);
        VectorIRotate(vTmp, ps_model_t->source->boneToPose[ps_eyeball_t->bone], ps_eyeball_t->forward);

        // Not applicable
        ps_eyeball_t->upperlidflexdesc = -1;
        ps_eyeball_t->lowerlidflexdesc = -1;
    }

    // Make the standard flex controllers for eyes if required
    static const char *szEyesFlexControllers[] = {
        "eyes_updown",
        "eyes_rightleft"
    };

    for (int nNewFlexIndex = 0; nNewFlexIndex < ARRAYSIZE(szEyesFlexControllers); ++nNewFlexIndex) {
        bool bHasEyeFlexController = false;

        for (int nFlexIndex = 0; nFlexIndex < g_studiomdl.flexcontrollers.size(); ++nFlexIndex) {
            if (!Q_strcmp(szEyesFlexControllers[ nNewFlexIndex ], g_studiomdl.flexcontrollers[ nFlexIndex ].name)) {
                bHasEyeFlexController = true;
                break;
            }
        }

        // The flex controller range for eyes_updown & eyes_rightleft is default [-45, 45] because it's clamped by eyesMaxDeflection
        // and changing it based on max deflection would cause animatin changes since flex controller values are normalized [0, 1]
        // [-45, 45 ] gives a maxium range that's useful

        if (!bHasEyeFlexController) {
            if (g_studiomdl.flexcontrollers.full()) {
                MdlWarning("1103: Couldn't make eyes flexcontroller %s, too many flex controllers defined\n",
                           szEyesFlexControllers[nNewFlexIndex]);
                continue;
            }
            g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = szEyesFlexControllers[nNewFlexIndex],
                .type = "eyes",
                .min = -45.0f,
                .max = 45.0f,
            });
        }
    }
}

//-----------------------------------------------------------------------------
// Loads Body Group Lists from the preprocessed file
//-----------------------------------------------------------------------------
static bool LoadBodyGroupList(s_source_t **ppMainSource, CDmeBodyGroupList *pBodyGroupList,
                              CDmeEyeballGlobals *pDmeEyeballGlobals, bool bStaticProp, bool &bSetUpAxis) {
    *ppMainSource = nullptr;

    CDmaElementArray<CDmeBodyGroup> &dmeBodyGroups = pBodyGroupList->m_BodyGroups;
    const int nDmeBodyGroupCount = dmeBodyGroups.Count();
    if (nDmeBodyGroupCount == 0)
        return true;

    // Eyeball globals needs to be loaded after g_defaultrotation is set, which is after the first LOD is loaded
    bool bLoadEyeballGlobals = true;

    // Compute the 'main' body part
    const CDmeLODList *pMainBodyPart = pBodyGroupList->GetMainBodyPart();
    for (int i = 0; i < nDmeBodyGroupCount; ++i) {
        const CDmeBodyGroup *pDmeBodyGroup = dmeBodyGroups[i];

        s_bodypart_t *pBodyPart = &g_studiomdl.bodypart[g_studiomdl.numbodyparts];
        pBodyPart->nummodels = 0;
        if (g_studiomdl.numbodyparts == 0) {
            pBodyPart->base = 1;
        }
        else {
            pBodyPart->base = g_studiomdl.bodypart[g_studiomdl.numbodyparts - 1].base * g_studiomdl.bodypart[
                                  g_studiomdl.numbodyparts - 1].nummodels;
        }
        pBodyPart->name = pDmeBodyGroup->GetName();
        ++g_studiomdl.numbodyparts;

        // Load all body parts for this body group
        const int nDmeBodyPartCount = pDmeBodyGroup->m_BodyParts.Count();
        for (int j = 0; j < nDmeBodyPartCount; ++j) {
            s_model_t *pModel = c_allocate<s_model_t>();
            const int nModel = g_studiomdl.nummodels++;
            g_studiomdl.model[nModel] = pModel;
            pBodyPart->pmodel[pBodyPart->nummodels++] = pModel;
            pModel->scale = 1.0f;

            CDmeBodyPart *pDmeBodyPart = pDmeBodyGroup->m_BodyParts[j];
            CDmeLODList *pDmeLODList = CastElement<CDmeLODList>(pDmeBodyPart);

            if (pDmeBodyPart->LODCount() == 0 || !pDmeLODList) {
                pModel->source = AllocateDmxSource("blank");
                Q_strncpy(pModel->name, "blank", sizeof(pModel->name));
            }
            else {
                Q_strncpy(pModel->name, pDmeLODList->GetName(), sizeof(pModel->name));

                g_pCurrentModel = pModel;

                if (!LoadLODs(&pModel->source, pDmeLODList, bSetUpAxis, bStaticProp)) {
                    MdlError("Bad LOD On BodyGroup \"%s\".bodyPartList[%d]\n", pDmeBodyGroup->GetName(), j);
                    return false;
                }

                g_pCurrentModel = nullptr;

                // This needs to be done after g_defaultRotation is set, which should be set after the first LOD is loaded
                if (bLoadEyeballGlobals) {
                    bLoadEyeballGlobals = false;
                    LoadEyeballGlobals(pDmeEyeballGlobals);
                }

                LoadEyeballs(pModel, pDmeLODList);

                if (pModel->source) {
                    pModel->filename = pModel->source->filename;
                }

                PostProcessSource(pModel->source, nModel);

                if (pMainBodyPart == pDmeLODList) {
                    *ppMainSource = pModel->source;
                }
            }
        }
    }

    return true;
}


//-----------------------------------------------------------------------------
// Loads the collision model from the preprocessed file
//-----------------------------------------------------------------------------
bool LoadCollisionModel(CDmeCollisionModel *pCollisionInfo, bool bStaticProp) {
    if (!pCollisionInfo)
        return false;

    // Check for overflow - TODO: Move to AllocateSource()
    if (g_studiomdl.sources.full()) {
        MdlError("Load_Source - overflowed g_numsources loading LODs.");
        return false;
    }

    s_source_t *pCollisionSource = AllocateDmxSource(pCollisionInfo->GetName());
    if (!pCollisionSource)
        return false;

    int nummaterials = g_studiomdl.materials.size();
    int numtextures = g_studiomdl.textures.size();

    // BoneRemap[bone index in file] == bone index in studiomdl
    CDmeDag *pSkeleton = pCollisionInfo->GetValueElement<CDmeDag>("skeleton");
    if (!pSkeleton) {
        MdlError("%s(%s): No \"skeleton\" defined\n", pCollisionInfo->GetTypeString(), pCollisionInfo->GetName());
        return false;
    }

    CDmeModel *pModel = pCollisionInfo->GetValueElement<CDmeModel>("model");
    if (!pModel) {
        MdlError("%s(%s): No \"model\" defined\n", pCollisionInfo->GetTypeString(), pCollisionInfo->GetName());
        return false;
    }

    BoneTransformMap_t boneMap;
    if (!LoadModelAndSkeleton(pCollisionSource, boneMap, pSkeleton, pModel, nullptr, bStaticProp)) {
        MdlError("%s(%s): Couldn't Load Skeleton: %s(%s) & Model: %s(%s)\n",
                 pCollisionInfo->GetTypeString(), pCollisionInfo->GetName(),
                 pSkeleton->GetTypeString(), pSkeleton->GetName(),
                 pModel->GetTypeString(), pModel->GetName());
        return false;
    }

    // auto-remove any new materials/textures
    if (nummaterials && numtextures && (numtextures != g_studiomdl.textures.size() || nummaterials != g_studiomdl.
                                        materials.size())) {
        g_studiomdl.textures.resize(numtextures);
        g_studiomdl.materials.resize(nummaterials);
        pCollisionSource->texmap[0] = 0;
    }

    if (DoCollisionModel(pCollisionSource, pCollisionInfo, bStaticProp) == 0)
        return false;

    const char *pSurfaceProperty = pCollisionInfo->GetValueString("surfaceProperty");
    if (pSurfaceProperty && pSurfaceProperty[0]) {
        SetDefaultSurfaceProp(pSurfaceProperty);
    }

    return true;
}

static void LoadMaterialGroups(const CDmeMaterialGroupList *pDmeMaterialGroupList) {
    if (!pDmeMaterialGroupList)
        return;

    if (g_studiomdl.numskinref == 0)
        g_studiomdl.numskinref = g_studiomdl.textures.size();

    // Studiomdl will read in multiple $texturegroup calls but only the first one
    // is ever used, so in mdlcompile, there will only ever be one
    Assert(g_numtexturegroups == 0);

    CDmeMaterialGroup *pDmeMaterialGroup = nullptr;

    const int nMaterialGroupCount = pDmeMaterialGroupList->m_MaterialGroups.Count();
    if (nMaterialGroupCount >= ARRAYSIZE(g_studiomdl.texturegroup[ 0 ])) {
        MdlError("Too Many Material Groups, Max: %d\n", ARRAYSIZE(g_studiomdl.texturegroup[ 0 ]));
        return;
    }

    const int nTextureCount = g_studiomdl.textures.size();

    for (int nGroupIndex = 0; nGroupIndex < nMaterialGroupCount; ++nGroupIndex) {
        pDmeMaterialGroup = pDmeMaterialGroupList->m_MaterialGroups[nGroupIndex];
        if (!pDmeMaterialGroup)
            continue;

        const int nMaterialCount = pDmeMaterialGroup->m_MaterialList.Count();
        if (nGroupIndex > 0 && nMaterialCount < nTextureCount) {
            MdlWarning("Only Setting %d of %d Textures via MaterialGroup %d, Skin %d will have bad materials\n",
                       nMaterialCount, nTextureCount, nGroupIndex, nGroupIndex);
        }
        for (int nMaterialIndex = 0; nMaterialIndex < nMaterialCount; ++nMaterialIndex) {
            const int nMdlMaterialIndex = UseTextureAsMaterial(
                LookupTexture(pDmeMaterialGroup->m_MaterialList[nMaterialIndex], true));
            g_studiomdl.texturegroup[g_studiomdl.numtexturegroups][nGroupIndex][nMaterialIndex] = nMdlMaterialIndex;
            if (nGroupIndex != 0) {
                g_studiomdl.textures[nMdlMaterialIndex].parent = g_studiomdl.texturegroup[g_studiomdl.numtexturegroups][
                    0][nMaterialIndex];
            }
            g_studiomdl.numtexturelayers[g_studiomdl.numtexturegroups] = nGroupIndex + 1;
            g_studiomdl.numtexturereps[g_studiomdl.numtexturegroups] = nMaterialIndex + 1;
        }
    }

    ++g_studiomdl.numtexturegroups;
}

static void LoadBoneMergeList(CDmAttribute *pDmeBoneMergeListAttr) {
    if (!pDmeBoneMergeListAttr)
        return;

    CDmrStringArrayConst boneMergeList(pDmeBoneMergeListAttr);
    if (!boneMergeList.IsValid())
        return;

    const int nBoneMergeCount = boneMergeList.Count();
    for (int nBoneMergeIndex = 0; nBoneMergeIndex < nBoneMergeCount; ++nBoneMergeIndex) {
        g_studiomdl.BoneMerge.emplace_back(boneMergeList[nBoneMergeIndex]);
    }
}

static void SetupStaticProp(s_source_t *pSource) {
#ifdef MDLCOMPILE
    ProcessStaticProp();
    s_sequence_t *pSeq = ProcessCmdSequence("BindPose");
    s_animation_t *pAnim = ProcessImpliedAnimation(pSeq, pSource->filename.c_str());
    pSeq->panim[0][0] = pAnim;
    ProcessSequence(pSeq, 1, &pAnim, false);
#endif
}

static void LoadBoneMaskList(CDmeBoneMaskList *pDmeBoneMaskList) {
    if (!pDmeBoneMaskList)
        return;

    CDmeBoneMask *pDmeDefaultBoneMask = pDmeBoneMaskList->m_eDefaultBoneMask.GetElement();

    for (int i = 0; i < pDmeBoneMaskList->m_BoneMaskList.Count(); ++i) {
        CDmeBoneMask *pDmeBoneMask = pDmeBoneMaskList->m_BoneMaskList.Element(i);
        if (!pDmeBoneMask)
            return;

        int nBoneMaskIndex = g_studiomdl.numweightlist;

        if (pDmeBoneMask == pDmeDefaultBoneMask) {
            nBoneMaskIndex = 0;
        }

        if (nBoneMaskIndex >= MAXWEIGHTLISTS) {
            MdlWarning("1300: Too many bone masks, max %d, ignoring %s\n", MAXWEIGHTLISTS, pDmeBoneMask->GetName());
        }

        bool bDuplicate = false;
        for (int j = 0; j < g_studiomdl.numweightlist; ++j) {
            if (g_studiomdl.weightlist[j].name && !
                Q_stricmp(g_studiomdl.weightlist[ j ].name, pDmeBoneMask->GetName())) {
                bDuplicate = true;
                MdlWarning("1301: Ignoring duplicate bone mask %s\n", pDmeBoneMask->GetName());
                break;
            }
        }

        if (bDuplicate)
            continue;

        s_weightlist_t *pWeightList = &(g_studiomdl.weightlist[nBoneMaskIndex]);

        Q_strncpy(pWeightList->name, pDmeBoneMask->GetName(), sizeof(pWeightList->name));
        pWeightList->numbones = 0;
        for (int j = 0; j < pDmeBoneMask->m_BoneWeights.Count(); ++j) {
            CDmeBoneWeight *pDmeBoneWeight = pDmeBoneMask->m_BoneWeights.Element(j);
            if (!pDmeBoneWeight)
                continue;

            int nBoneWeightIndex = pWeightList->numbones;
            if (nBoneWeightIndex >= MAXWEIGHTSPERLIST) {
                MdlWarning("1302: Too many bones in weightlist %s, ignoring weight for %s (%f)\n", pWeightList->name,
                           pDmeBoneWeight->GetName(), pDmeBoneWeight->m_flWeight.Get());
                continue;
            }

            pWeightList->bonename[nBoneWeightIndex] = strdup(pDmeBoneWeight->GetName());

            // 'weight' attribute is normally used for both rotation and position weight
            pWeightList->boneweight[nBoneWeightIndex] = pDmeBoneWeight->m_flWeight.Get();
            pWeightList->boneposweight[nBoneWeightIndex] = pWeightList->boneweight[nBoneWeightIndex];

            // Look for optional 'positionWeight' attribute, use it separately for position if it exists
            CDmAttribute *pDmePositionWeightAttr = pDmeBoneWeight->GetAttribute("positionWeight", AT_FLOAT);
            if (pDmePositionWeightAttr) {
                pWeightList->boneposweight[nBoneWeightIndex] = pDmePositionWeightAttr->GetValue<float>();
                if (pWeightList->boneweight[nBoneWeightIndex] == 0 && pWeightList->boneposweight[nBoneWeightIndex] >
                    0) {
                    MdlWarning(
                        "1303: Non-zero position weight with zero rotation weight not allowed for bone weight list %s:%s P: %f R: %f, ignoring position weight\n",
                        pWeightList->name, pWeightList->bonename[nBoneWeightIndex],
                        pWeightList->boneposweight[nBoneWeightIndex], pWeightList->boneweight[nBoneWeightIndex]);
                    pWeightList->boneposweight[nBoneWeightIndex] = pWeightList->boneweight[nBoneWeightIndex];
                }
            }

            ++pWeightList->numbones;
        }

        if (nBoneMaskIndex != 0) {
            ++g_studiomdl.numweightlist;
        }
    }
}

static void LoadPoseParameterList(CDmePoseParameterList *pDmePoseParameterList) {
    if (!pDmePoseParameterList)
        return;

    const int nPoseParameterCount = pDmePoseParameterList->m_ePoseParameterList.Count();
    for (int i = 0; i < nPoseParameterCount; ++i) {
        const CDmePoseParameter *pDmePoseParameter = pDmePoseParameterList->m_ePoseParameterList[i];

        if (g_studiomdl.numposeparameters >= MAXSTUDIOPOSEPARAM) {
            MdlWarning("1900: Too many pose parameters, ignoring from %s\n", pDmePoseParameter->GetName());
        }

        // This is like FindOrCreatePoseParamater, name is copied into g_pose[].name
        const int nPoseParameterIndex = LookupPoseParameter(pDmePoseParameter->GetName());

        s_poseparameter_t *pPoseParameter = &g_studiomdl.pose[nPoseParameterIndex];
        Q_strncpy(pPoseParameter->name, pDmePoseParameter->GetName(), sizeof(pPoseParameter->name));
        pPoseParameter->min = pDmePoseParameter->m_flMin;
        pPoseParameter->max = pDmePoseParameter->m_flMax;
        if (pDmePoseParameter->m_bWrap) {
            pPoseParameter->flags |= STUDIO_LOOPING;
            pPoseParameter->loop = pPoseParameter->max - pPoseParameter->min;
        }
        else if (pDmePoseParameter->m_bLoop) {
            pPoseParameter->flags |= STUDIO_LOOPING;
            pPoseParameter->loop = pDmePoseParameter->m_flLoopRange;
        }
    }
}

static void LoadAnimBlockSize(CDmeAnimBlockSize *pDmeAnimBlockSize) {
    if (!pDmeAnimBlockSize)
        return;

    g_studiomdl.animblocksize = pDmeAnimBlockSize->m_nSize;
    if (g_studiomdl.animblocksize < 1024) {
        g_studiomdl.animblocksize *= 1024;
    }

    g_StudioMdlContext.noAnimblockStall = !pDmeAnimBlockSize->m_bStall;
    switch (pDmeAnimBlockSize->m_nStorageType) {
        case CDmeAnimBlockSize::ANIMBLOCKSTORAGETYPE_HIRES:
            g_StudioMdlContext.animblockLowRes = false;
            g_StudioMdlContext.animblockHighRes = true;
            break;
        case CDmeAnimBlockSize::ANIMBLOCKSTORAGETYPE_LOWRES:
            // Fallthrough deliberate
        default:
            g_StudioMdlContext.animblockLowRes = true;
            g_StudioMdlContext.animblockHighRes = false;
            break;
    }
}

static void CreateBindPoseSequence(s_source_t *pMainSource) {
    s_sourceanim_t *pSourceAnim = FindSourceAnim(pMainSource, "BindPose");
    if (pSourceAnim) {
        // Create the first "BindPose" default sequence
        s_sequence_t *pSeq = ProcessCmdSequence("BindPose");
        s_animation_t *pAnim = ProcessImpliedAnimation(pSeq, pMainSource->filename.c_str());
        pSeq->panim[0][0] = pAnim;
        ProcessSequence(pSeq, 1, &pAnim, false);
    }
}

static void LoadIkChainList(CDmeSequenceList *pDmeSequenceList) {
    if (!pDmeSequenceList)
        return;

    const int nIkChainCount = pDmeSequenceList->m_eIkChainList.Count();
    for (int i = 0; i < nIkChainCount; ++i) {
        CDmeIkChain *pDmeIkChain = pDmeSequenceList->m_eIkChainList[i];
        if (!pDmeIkChain)
            continue;

        const char *pIkChainName = pDmeIkChain->GetName();

        // Check for duplicates
        int j;
        for (j = 0; j < g_studiomdl.numikchains; ++j) {
            if (!Q_stricmp(pIkChainName, g_studiomdl.ikchain[ j ].name))
                break;
        }

        if (j < g_studiomdl.numikchains) {
            if (!g_StudioMdlContext.quiet) {
                MdlWarning("1401: Duplicate IkChain: %s Ignored\n", pIkChainName);
            }
        }

        // Set defaults
        g_studiomdl.ikchain[g_studiomdl.numikchains].axis = STUDIO_Z; // Not actually used
        g_studiomdl.ikchain[g_studiomdl.numikchains].value = 0.0; // Not actually used

        g_studiomdl.ikchain[g_studiomdl.numikchains].height = 18.0;
        g_studiomdl.ikchain[g_studiomdl.numikchains].floor = 0.0;
        g_studiomdl.ikchain[g_studiomdl.numikchains].radius = 0.0;

        Q_strncpy(g_studiomdl.ikchain[g_studiomdl.numikchains].name, pIkChainName,
                  sizeof(g_studiomdl.ikchain[g_studiomdl.numikchains].name));
        Q_strncpy(g_studiomdl.ikchain[g_studiomdl.numikchains].bonename, pDmeIkChain->m_sEndJoint,
                  sizeof(g_studiomdl.ikchain[g_studiomdl.numikchains].bonename));
        g_studiomdl.ikchain[g_studiomdl.numikchains].height = pDmeIkChain->m_flHeight;
        g_studiomdl.ikchain[g_studiomdl.numikchains].floor = pDmeIkChain->m_flFloor;
        g_studiomdl.ikchain[g_studiomdl.numikchains].radius = pDmeIkChain->m_flPad / 2.0f;
        g_studiomdl.ikchain[g_studiomdl.numikchains].link[0].kneeDir = pDmeIkChain->m_vKnee;
        g_studiomdl.ikchain[g_studiomdl.numikchains].center = pDmeIkChain->m_vCenter;

        ++g_studiomdl.numikchains;
    }
}

static void CreateAnimFromSkeleton(s_source_t *pSource, const char *pSequenceName, const BoneTransformMap_t &boneMap) {
    s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim(pSource, pSequenceName);
    pSourceAnim->startframe = 0;
    pSourceAnim->endframe = 0;
    pSourceAnim->numframes = 1;

    // Default all transforms to identity
    pSourceAnim->rawanim[0] = static_cast<s_bone_t *>(calloc(pSource->numbones, sizeof(s_bone_t)));
    for (int i = 0; i < pSource->numbones; ++i) {
        pSourceAnim->rawanim[0][i].pos.Init();
        pSourceAnim->rawanim[0][i].rot.Init();
    }

    matrix3x4_t jointTransform;
    for (int nBoneIndex = 0; nBoneIndex < boneMap.m_nBoneCount; ++nBoneIndex) {
        const CDmeTransform *pDmeTransform = boneMap.m_ppTransforms[nBoneIndex];
        s_bone_t &mdlBone = pSourceAnim->rawanim[0][nBoneIndex];
        VectorCopy(pDmeTransform->GetPosition() * g_studiomdl.currentscale, mdlBone.pos);
        QuaternionAngles(pDmeTransform->GetOrientation(), mdlBone.rot);
    }

    // This copies the named animation into the poseToBone array in the source
    Build_Reference(pSource, pSequenceName);
}

// motionControl is created with its owner when authoring, but not when a file lacks it (unserializing skips
// InitAndCreate); no element means no motion extraction, like a default CDmeMotionControl
static int MotionControlFlags(const CDmeMotionControl *pMotionControl) {
    return pMotionControl ? pMotionControl->GetStudioMotionControl() : 0;
}

void LoadAnimationOptions(CDmeSequence *pDmeSimpleSequence, s_animation_t *pAnimation) {
    if (!pAnimation || !pDmeSimpleSequence)
        return;

    pAnimation->fps = pDmeSimpleSequence->m_flFPS;
    pAnimation->adjust = pDmeSimpleSequence->m_vOrigin;
    // Right now don't include rotation, but if added, likely combine it with the
    // default rotation which is already in pAnimation->rotation
    //	QuaternionAngles( pDmeSequence->m_qRotation, pAnimation->rotation );
    pAnimation->scale = pDmeSimpleSequence->m_flScale;

    pAnimation->looprestart = pDmeSimpleSequence->m_nStartLoop;

    if (pDmeSimpleSequence->m_bLoop) {
        pAnimation->flags |= STUDIO_LOOPING;
    }

    // This should be removed and handled by assemble, i.e. STUDIO_NOFORCELOOP should always be set
    if (!pDmeSimpleSequence->m_bForceLoop) {
        pAnimation->flags |= STUDIO_NOFORCELOOP;
    }

    if (pDmeSimpleSequence->m_bSnap) {
        pAnimation->flags |= STUDIO_SNAP;
    }

    if (pDmeSimpleSequence->m_bPost) {
        pAnimation->flags |= STUDIO_POST;
    }

    if (pDmeSimpleSequence->m_bAutoIk) {
        pAnimation->noAutoIK = false;
    }
    else {
        pAnimation->noAutoIK = true;
    }

    pAnimation->motionrollback = pDmeSimpleSequence->m_flMotionRollback;

    if (pDmeSimpleSequence->m_bAnimBlocks) {
        pAnimation->disableAnimblocks = false;
    }
    else {
        pAnimation->disableAnimblocks = true;
    }

    if (pDmeSimpleSequence->m_bAnimBlockStall) {
        pAnimation->isFirstSectionLocal = false;
    }
    else {
        pAnimation->isFirstSectionLocal = true;
    }

    pAnimation->motiontype = MotionControlFlags(pDmeSimpleSequence->m_eMotionControl.GetElement());
}

int FindWeightList(const char *pWeightListName) {
    for (int i = 0; i < g_studiomdl.numweightlist; ++i) {
        if (!Q_stricmp(pWeightListName, g_studiomdl.weightlist[ i ].name))
            return i;
    }

    return -1;
}

bool HandleDmeAnimCmdAlign(s_animcmd_t *pAnimCmd, CDmeAnimCmd *pDmeAnimCmd) {
    if (!pDmeAnimCmd)
        return false;

    CDmeAnimCmdAlign *pDmeAnimCmdAlign = CastElement<CDmeAnimCmdAlign>(pDmeAnimCmd);
    if (!pDmeAnimCmdAlign)
        return false;

    CDmeSequenceBase *pDmeSequenceBase = pDmeAnimCmdAlign->m_eAnimation.GetElement();
    if (!pDmeSequenceBase) {
        MdlWarning("1608: No DmeSequence specified for %s:%s\n",
                   pDmeAnimCmdAlign->GetTypeString(),
                   pDmeAnimCmd->GetName());
        return false;
    }

    s_animation_t *pExtAnim = LookupAnimation(pDmeSequenceBase->GetName());
    if (!pExtAnim) {
        MdlWarning("1604: Unknown animation \"%s\" specified for %s:%s\n",
                   pDmeSequenceBase->GetName(),
                   pDmeAnimCmdAlign->GetTypeString(),
                   pDmeAnimCmd->GetName());
        return false;
    }

    pAnimCmd->cmd = CMD_AO;
    pAnimCmd->u.ao.ref = pExtAnim;
    pAnimCmd->u.ao.pBonename = pDmeAnimCmdAlign->m_sBoneName.IsEmpty()
                                   ? nullptr
                                   : strdup(pDmeAnimCmdAlign->m_sBoneName);
    pAnimCmd->u.ao.srcframe = pDmeAnimCmdAlign->m_nSourceFrame;
    pAnimCmd->u.ao.destframe = pDmeAnimCmdAlign->m_nDestinatonFrame;
    pAnimCmd->u.ao.motiontype = MotionControlFlags(pDmeAnimCmdAlign->m_eMotionControl.GetElement());

    return true;
}

void LoadAnimationCommands(CDmeSequence *pDmeSimpleSequence, s_animation_t *pAnimation) {
    if (!pDmeSimpleSequence || !pAnimation)
        return;

    const int nAnimCmdCount = pDmeSimpleSequence->m_eAnimationCommandList.Count();
    for (int i = 0; i < nAnimCmdCount; ++i) {
        CDmeAnimCmd *pDmeAnimCmd = pDmeSimpleSequence->m_eAnimationCommandList[i];
        if (!pDmeAnimCmd)
            continue;

        if (pAnimation->cmds.full()) {
            MdlWarning("1600: Too many animation commands for anim: %s, ignoring from %d:%s\n",
                       pAnimation->name.c_str(), i, pDmeAnimCmd->GetName());
            return;
        }

        s_animcmd_t *pAnimCmd = &pAnimation->cmds.emplace_back();

        if (pDmeAnimCmd->IsA(CDmeAnimCmdFixupLoop::GetStaticTypeSymbol())) {
            CDmeAnimCmdFixupLoop *pDmeAnimCmdFixupLoop = CastElement<CDmeAnimCmdFixupLoop>(pDmeAnimCmd);
            pAnimCmd->cmd = CMD_FIXUP;
            pAnimCmd->u.fixuploop.start = pDmeAnimCmdFixupLoop->m_nStartFrame;
            pAnimCmd->u.fixuploop.end = pDmeAnimCmdFixupLoop->m_nEndFrame;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdWeightList::GetStaticTypeSymbol())) {
            CDmeAnimCmdWeightList *pDmeAnimCmdWeightList = CastElement<CDmeAnimCmdWeightList>(pDmeAnimCmd);

            const int nWeightListIndex = FindWeightList(pDmeAnimCmdWeightList->m_sWeightListName);
            if (nWeightListIndex < 0) {
                MdlWarning("1602: Unknown weightList \"%s\" specified for %s animation command %s.%s[%d] %s\n",
                           pDmeAnimCmdWeightList->m_sWeightListName.Get(),
                           pDmeAnimCmdWeightList->GetTypeString(),
                           pDmeSimpleSequence->GetName(),
                           pDmeSimpleSequence->m_eAnimationCommandList.GetAttribute()->GetName(), i,
                           pDmeAnimCmd->GetName());
                continue;
            }
            else {
                pAnimCmd->cmd = CMD_WEIGHTS;
                pAnimCmd->u.weightlist.index = nWeightListIndex;
            }
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdSubtract::GetStaticTypeSymbol())) {
            CDmeAnimCmdSubtract *pDmeAnimCmdSubtract = CastElement<CDmeAnimCmdSubtract>(pDmeAnimCmd);
            CDmeSequenceBase *pDmeSequenceBase = pDmeAnimCmdSubtract->m_eAnimation.GetElement();
            if (pDmeSequenceBase) {
                s_animation_t *pExtAnim = LookupAnimation(pDmeSequenceBase->GetName());
                if (pExtAnim) {
                    pAnimCmd->cmd = CMD_SUBTRACT;
                    pAnimCmd->u.subtract.ref = pExtAnim;
                    pAnimCmd->u.subtract.frame = pDmeAnimCmdSubtract->m_nFrame;
                    if (!pDmeAnimCmd->IsA(CDmeAnimCmdPreSubtract::GetStaticTypeSymbol())) {
                        pAnimCmd->u.subtract.flags |= STUDIO_POST;
                    }
                }
                else {
                    MdlWarning("1603: Unknown animation \"%s\" specified for %s %s.%s[%d] %s\n",
                               pDmeSequenceBase->GetName(),
                               pDmeAnimCmdSubtract->GetTypeString(),
                               pDmeSimpleSequence->GetName(),
                               pDmeSimpleSequence->m_eAnimationCommandList.GetAttribute()->GetName(), i,
                               pDmeAnimCmd->GetName());
                    continue;
                }
            }
            else {
                MdlWarning("1607: No DmeSequenceBase specified for %s %s.%s[%d] %s\n",
                           pDmeAnimCmdSubtract->GetTypeString(),
                           pDmeSimpleSequence->GetName(),
                           pDmeSimpleSequence->m_eAnimationCommandList.GetAttribute()->GetName(), i,
                           pDmeAnimCmd->GetName());
                continue;
            }
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdAlign::GetStaticTypeSymbol())) {
            if (!HandleDmeAnimCmdAlign(pAnimCmd, pDmeAnimCmd))
                continue;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdRotateTo::GetStaticTypeSymbol())) {
            CDmeAnimCmdRotateTo *pDmeAnimCmdRotateTo = CastElement<CDmeAnimCmdRotateTo>(pDmeAnimCmd);

            pAnimCmd->cmd = CMD_ANGLE;
            pAnimCmd->u.angle.angle = pDmeAnimCmdRotateTo->m_flAngle;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdWalkFrame::GetStaticTypeSymbol())) {
            CDmeAnimCmdWalkFrame *pDmeAnimCmdWalkFrame = CastElement<CDmeAnimCmdWalkFrame>(pDmeAnimCmd);

            pAnimCmd->cmd = CMD_MOTION;
            pAnimCmd->u.motion.iEndFrame = pDmeAnimCmdWalkFrame->m_nEndFrame;
            pAnimCmd->u.motion.motiontype = MotionControlFlags(pDmeAnimCmdWalkFrame->m_eMotionControl.GetElement());
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdDerivative::GetStaticTypeSymbol())) {
            CDmeAnimCmdDerivative *pDmeAnimCmdDerivative = CastElement<CDmeAnimCmdDerivative>(pDmeAnimCmd);

            pAnimCmd->cmd = CMD_DERIVATIVE;
            pAnimCmd->u.derivative.scale = pDmeAnimCmdDerivative->m_flScale;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdDerivative::GetStaticTypeSymbol())) {
            CDmeAnimCmdDerivative *pDmeAnimCmdDerivative = CastElement<CDmeAnimCmdDerivative>(pDmeAnimCmd);

            pAnimCmd->cmd = CMD_DERIVATIVE;
            pAnimCmd->u.derivative.scale = pDmeAnimCmdDerivative->m_flScale;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdLinearDelta::GetStaticTypeSymbol())) {
            pAnimCmd->cmd = CMD_LINEARDELTA;
            pAnimCmd->u.linear.flags |= STUDIO_AL_POST;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdSplineDelta::GetStaticTypeSymbol())) {
            pAnimCmd->cmd = CMD_LINEARDELTA;
            pAnimCmd->u.linear.flags |= STUDIO_AL_POST;
            pAnimCmd->u.linear.flags |= STUDIO_AL_SPLINE;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdCompress::GetStaticTypeSymbol())) {
            CDmeAnimCmdCompress *pDmeAnimCmdCompress = CastElement<CDmeAnimCmdCompress>(pDmeAnimCmd);

            pAnimCmd->cmd = CMD_COMPRESS;
            pAnimCmd->u.compress.frames = pDmeAnimCmdCompress->m_nSkipFrames;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdNumFrames::GetStaticTypeSymbol())) {
            CDmeAnimCmdNumFrames *pDmeAnimCmdNumFrames = CastElement<CDmeAnimCmdNumFrames>(pDmeAnimCmd);

            pAnimCmd->cmd = CMD_NUMFRAMES;
            pAnimCmd->u.compress.frames = pDmeAnimCmdNumFrames->m_nFrames;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdLocalHierarchy::GetStaticTypeSymbol())) {
            CDmeAnimCmdLocalHierarchy *pDmeAnimCmdLocalHierarchy = CastElement<CDmeAnimCmdLocalHierarchy>(pDmeAnimCmd);

            pAnimCmd->cmd = CMD_LOCALHIERARCHY;
            pAnimCmd->u.localhierarchy.pBonename = strdup(pDmeAnimCmdLocalHierarchy->m_sBoneName.Get());
            pAnimCmd->u.localhierarchy.pParentname = strdup(pDmeAnimCmdLocalHierarchy->m_sParentBoneName.Get());
            pAnimCmd->u.localhierarchy.start = pDmeAnimCmdLocalHierarchy->m_flStartFrame;
            pAnimCmd->u.localhierarchy.peak = pDmeAnimCmdLocalHierarchy->m_flPeakFrame;
            pAnimCmd->u.localhierarchy.tail = pDmeAnimCmdLocalHierarchy->m_flTailFrame;
            pAnimCmd->u.localhierarchy.end = pDmeAnimCmdLocalHierarchy->m_flEndFrame;
        }
        else if (pDmeAnimCmd->IsA(CDmeAnimCmdNoAnimation::GetStaticTypeSymbol())) {
            pAnimCmd->cmd = CMD_NOANIMATION;
        }
        else {
            MdlWarning("1601: Unhandled DmeAnimCmd %s (%s)\n", pDmeAnimCmd->GetTypeString(), pDmeAnimCmd->GetName());
            continue;
        }
    }
}

void LoadIkRuleList(CDmeSequence *pDmeSimpleSequence, s_animation_t *pAnimation) {
    if (!pDmeSimpleSequence)
        return;

    matrix3x4_t mDefRot;
    AngleMatrix(g_studiomdl.defaultrotation, mDefRot);

    // See if we have any IkRules to load
    const int nIkRuleCount = pDmeSimpleSequence->m_eIkRuleList.Count();
    if (nIkRuleCount <= 0)
        return;

    for (int i = 0; i < nIkRuleCount; ++i) {
        CDmeIkRule *pDmeIkRule = pDmeSimpleSequence->m_eIkRuleList[i];
        if (!pDmeIkRule)
            continue;

        if (pAnimation->cmds.full()) {
            // TODO: Issue a warning
            MdlWarning("1503: Too many animation commands for anim: %s, ignoring from %s:%s\n",
                       pAnimation->name.c_str(), pDmeIkRule->GetTypeString(), pDmeIkRule->GetName());
            return;
        }

        // Find the IkChain
        CDmeIkChain *pDmeIkChain = pDmeIkRule->m_eIkChain.GetElement();
        if (!pDmeIkChain) {
            MdlError("1501: No IkChain element assigned to %s:%s\n", pDmeIkRule->GetTypeString(),
                     pDmeIkRule->GetName());
            continue;
        }

        int nIkChainIndex = -1;
        for (int j = 0; j < g_studiomdl.numikchains; ++j) {
            if (!Q_stricmp(pDmeIkChain->GetName(), g_studiomdl.ikchain[ j ].name)) {
                nIkChainIndex = j;
                break;
            }
        }

        if (nIkChainIndex < 0) {
            MdlWarning("1504: Cannot find IkChain referenced by IkRule %s:%s, ignoring\n", pDmeIkRule->GetName(),
                       pDmeIkRule->GetTypeString());
            continue;
        }

        // calloc sets memory to 0
        s_ikrule_t *pIkRule = reinterpret_cast<s_ikrule_t *>(calloc(1, sizeof(s_ikrule_t)));
        if (!pIkRule) {
            MdlWarning("1502: Cannot allocate memory for IkRule %s:%s, ignoring\n", pDmeIkRule->GetName(),
                       pDmeIkRule->GetTypeString());
            return;
        }

        pIkRule->chain = nIkChainIndex;
        pIkRule->slot = nIkChainIndex; // Default slot

        CDmeIkTouchRule *pDmeIkTouchRule = CastElement<CDmeIkTouchRule>(pDmeIkRule);
        CDmeIkFootstepRule *pDmeIkFootstepRule = CastElement<CDmeIkFootstepRule>(pDmeIkRule);
        CDmeIkAttachmentRule *pDmeIkAttachmentRule = CastElement<CDmeIkAttachmentRule>(pDmeIkRule);
        CDmeIkReleaseRule *pDmeIkReleaseRule = CastElement<CDmeIkReleaseRule>(pDmeIkRule);

        if (pDmeIkTouchRule) {
            pIkRule->type = IK_SELF;
            Q_strncpy(pIkRule->bonename, pDmeIkTouchRule->m_sBoneName, sizeof(pIkRule->bonename));
        }
        else if (pDmeIkFootstepRule) {
            pIkRule->type = IK_GROUND;
            pIkRule->height = pDmeIkFootstepRule->GetValue<float>("height", g_studiomdl.ikchain[pIkRule->chain].height);
            pIkRule->floor = pDmeIkFootstepRule->GetValue<float>("floor", g_studiomdl.ikchain[pIkRule->chain].floor);
            pIkRule->radius = pDmeIkFootstepRule->GetValue<float>(
                                  "pad", g_studiomdl.ikchain[pIkRule->chain].radius * 2.0f) / 2.0f;
            if (pDmeIkFootstepRule->HasAttribute("contact", AT_INT)) {
                pIkRule->contact = pDmeIkFootstepRule->GetValue<int>("contact");
            }
        }
        else if (pDmeIkAttachmentRule) {
            pIkRule->type = IK_ATTACHMENT;
            Q_strncpy(pIkRule->attachment, pDmeIkAttachmentRule->m_sAttachmentName, sizeof(pIkRule->attachment));

            if (pDmeIkAttachmentRule->HasAttribute("fallbackBone", AT_STRING)) {
                Q_strncpy(pIkRule->bonename, pDmeIkAttachmentRule->GetValueString("fallbackBone"),
                          sizeof(pIkRule->bonename));
            }

            if (pDmeIkAttachmentRule->HasAttribute("fallbackPosition", AT_VECTOR3)) {
                VectorIRotate(pDmeIkAttachmentRule->GetValue<Vector>("fallbackPosition"), mDefRot, pIkRule->pos);
                pIkRule->bone = -1;
            }

            if (pDmeIkAttachmentRule->HasAttribute("fallbackRotation", AT_QUATERNION)) {
                // TODO: Adjust rotation as above for position adjustment?
                pIkRule->q = pDmeIkAttachmentRule->GetValue<Quaternion>("fallbackRotation");
                pIkRule->bone = -1;
            }
        }
        else if (pDmeIkReleaseRule) {
            pIkRule->type = IK_RELEASE;
        }
        else {
            MdlWarning("1500: Unknown IkRuleType %s:%s, ignoring\n", pDmeIkRule->GetName(),
                       pDmeIkRule->GetTypeString());
            continue;
        }

        switch (pDmeIkRule->m_nUseType) {
            case CDmeIkRule::USE_SEQUENCE:
                pIkRule->usesequence = true;
                pIkRule->usesource = false;
                break;
            case CDmeIkRule::USE_SOURCE:
                pIkRule->usesequence = false;
                pIkRule->usesource = true;
                break;
            case CDmeIkRule::USE_NONE:
            default:
                // Nothing
                break;
        }

        CDmeIkRange *pDmeIkRange = pDmeIkRule->m_eRange.GetElement();
        if (pDmeIkRange) {
            pIkRule->start = pDmeIkRange->m_nStartFrame;
            pIkRule->peak = pDmeIkRange->m_nMaxStartFrame;
            pIkRule->tail = pDmeIkRange->m_nMaxEndFrame;
            pIkRule->end = pDmeIkRange->m_nEndFrame;
        }


        pAnimation->cmds.emplace_back(s_animcmd_t{
            .cmd = CMD_IKRULE,
            .u = {.ikrule = {.pRule = pIkRule}}
        });
    }
}


static void LoadSequenceList(CDmeMultiSequence *pDmeMultiSequence, CUtlVector<s_animation_t *> &animations) {
    if (!pDmeMultiSequence)
        return;

    const int nSequenceCount = pDmeMultiSequence->m_eSequenceList.Count();
    for (int i = 0; i < nSequenceCount; ++i) {
        CDmeSequence *pDmeSubSequence = pDmeMultiSequence->m_eSequenceList[i];
        if (!pDmeSubSequence)
            continue;

        s_animation_t *pAnim = LookupAnimation(pDmeSubSequence->GetName());
        if (!pAnim) {
            MdlWarning("1208: DmeSequence %s: Couldn't find referenced animation: %s\n", pDmeMultiSequence->GetName(),
                       pDmeSubSequence->GetName());
            continue;
        }

        // TODO: Limit to 64?
        animations.AddToTail(pAnim);
    }
}

static void LoadSequenceBlends(CDmeMultiSequence *pDmeMultiSequence, s_sequence_t *pSequence) {
    if (!pSequence || !pDmeMultiSequence)
        return;

    const int nBlendCount = pDmeMultiSequence->m_eBlendList.Count();
    for (int i = 0; i < nBlendCount; ++i) {
        CDmeSequenceBlendBase *pDmeSequenceBlendBase = pDmeMultiSequence->m_eBlendList[i];
        if (!pDmeSequenceBlendBase)
            continue;

        if (pDmeSequenceBlendBase->IsA(CDmeSequenceBlend::GetStaticTypeSymbol())) {
            CDmeSequenceBlend *pDmeSequenceBlend = CastElement<CDmeSequenceBlend>(pDmeSequenceBlendBase);

            const int i = (pSequence->paramindex[0] != -1) ? 1 : 0;
            const int j = LookupPoseParameter(pDmeSequenceBlend->m_sPoseParameterName.Get());
            pSequence->paramindex[i] = j;
            pSequence->paramattachment[i] = -1;
            pSequence->paramstart[i] = pDmeSequenceBlend->m_flParamStart;
            pSequence->paramend[i] = pDmeSequenceBlend->m_flParamEnd;

            g_studiomdl.pose[j].min = MIN(g_studiomdl.pose[j].min, pSequence->paramstart[i]);
            g_studiomdl.pose[j].min = MIN(g_studiomdl.pose[j].min, pSequence->paramend[i]);
            g_studiomdl.pose[j].max = MAX(g_studiomdl.pose[j].max, pSequence->paramstart[i]);
            g_studiomdl.pose[j].max = MAX(g_studiomdl.pose[j].max, pSequence->paramend[i]);
        }
        else if (pDmeSequenceBlendBase->IsA(CDmeSequenceCalcBlend::GetStaticTypeSymbol())) {
            // TODO: This really isn't an animation command but the distinction might not be important?
            CDmeSequenceCalcBlend *pDmeSequenceCalcBlend = CastElement<CDmeSequenceCalcBlend>(pDmeSequenceBlendBase);

            const int i = (pSequence->paramindex[0] != -1) ? 1 : 0;
            const int j = LookupPoseParameter(pDmeSequenceCalcBlend->m_sPoseParameterName.Get());
            pSequence->paramindex[i] = j;
            pSequence->paramattachment[i] = LookupAttachment(pDmeSequenceCalcBlend->m_sAttachmentName.Get());
            pSequence->paramcontrol[i] = MotionControlFlags(pDmeSequenceCalcBlend->m_eMotionControl.GetElement());

            if (pSequence->paramattachment[i] < 0) {
                MdlWarning("1606: Unknown Attachment For %s - %s.%s = %s\n",
                           pDmeSequenceCalcBlend->GetTypeString(),
                           pDmeSequenceCalcBlend->GetName(),
                           pDmeSequenceCalcBlend->m_sAttachmentName.GetAttribute()->GetName(),
                           pDmeSequenceCalcBlend->m_sAttachmentName.Get());
            }

            continue; // Don't add it as an animation command
        }
    }
}

void LoadBlendRefCompCenter(CDmeMultiSequence *pDmeMultiSequence, s_sequence_t *pSequence) {
    if (!pDmeMultiSequence || !pSequence)
        return;

    CDmeSequence *pDmeBlendRefSequence = CastElement<CDmeSequence>(pDmeMultiSequence->m_eBlendRef.GetElement());
    if (pDmeBlendRefSequence) {
        const char *pszBlendRef = pDmeBlendRefSequence->GetName();
        if (pszBlendRef && *pszBlendRef) {
            pSequence->paramanim = LookupAnimation(pszBlendRef);
            if (pSequence->paramanim == nullptr) {
                MdlWarning("1202: DmeSequence %s: Unknown .blendRef animation: %s\n", pDmeMultiSequence->GetName(),
                           pszBlendRef);
            }
        }
    }

    CDmeSequence *pDmeBlendRefComp = CastElement<CDmeSequence>(pDmeMultiSequence->m_eBlendComp.GetElement());
    if (pDmeBlendRefComp) {
        const char *pszBlendComp = pDmeBlendRefComp->GetName();
        if (pszBlendComp && *pszBlendComp) {
            pSequence->paramcompanim = LookupAnimation(pszBlendComp);
            if (pSequence->paramcompanim == nullptr) {
                MdlWarning("1203: DmeSequence %s: Unknown .blendComp animation: %s\n", pDmeMultiSequence->GetName(),
                           pszBlendComp);
            }
        }
    }

    CDmeSequence *pDmeBlendRefCenter = CastElement<CDmeSequence>(pDmeMultiSequence->m_eBlendCenter.GetElement());
    if (pDmeBlendRefCenter) {
        const char *pszBlendCenter = pDmeBlendRefCenter->GetName();
        if (pszBlendCenter && *pszBlendCenter) {
            pSequence->paramcenter = LookupAnimation(pszBlendCenter);
            if (pSequence->paramcenter == nullptr) {
                MdlWarning("1204: DmeSequence %s: Unknown .blendCenter animation: %s\n", pDmeMultiSequence->GetName(),
                           pszBlendCenter);
            }
        }
    }
}

void LoadAnimationEventList(CDmeSequenceBase *pDmeSequenceBase, s_sequence_t *pSequence) {
    if (!pDmeSequenceBase || !pSequence)
        return;

    CDmAttribute *pBlah = pDmeSequenceBase->GetAttribute("animationEventList");
    int nFoo = 0;
    if (pBlah) {
        nFoo = CDmrGenericArrayConst(pBlah).Count();
    }
    const int nAnimEventCount = pDmeSequenceBase->m_eAnimationEventList.Count();
    for (int i = 0; i < nAnimEventCount; ++i) {
        CDmeAnimationEvent *pDmeAnimationEvent = pDmeSequenceBase->m_eAnimationEventList[i];
        if (!pDmeAnimationEvent)
            continue;

        // Not sure why this is +1... always one special event??
        if (pSequence->events.size() + 1 >= pSequence->events.capacity()) {
            MdlError("1800: Too many %s's on %s:%s, ignoring from %s\n",
                     pDmeAnimationEvent->GetTypeString(),
                     pDmeSequenceBase->GetTypeString(),
                     pDmeSequenceBase->GetName(),
                     pDmeAnimationEvent->GetName());
            return;
        }

        auto &event = pSequence->events.emplace_back(s_event_t{
            .frame = pDmeAnimationEvent->m_nFrame,
            .eventname = pDmeAnimationEvent->GetName(),
        });

        if (!pDmeAnimationEvent->m_sDataString.IsEmpty()) {
            event.options = pDmeAnimationEvent->m_sDataString;
        }
    }
}

void LoadLayerList(CDmeSequenceBase *pDmeSequenceBase, s_sequence_t *pSequence) {
    if (!pDmeSequenceBase || !pSequence)
        return;

    const int nLayerCount = pDmeSequenceBase->m_eLayerList.Count();
    for (int i = 0; i < nLayerCount; ++i) {
        CDmeSequenceLayerBase *pDmeLayerBase = pDmeSequenceBase->m_eLayerList[i];
        if (!pDmeLayerBase)
            continue;

        CDmeSequenceBase *pDmeRefSeq = pDmeLayerBase->m_eAnimation.GetElement();
        if (!pDmeRefSeq) {
            // TODO: Warning message - No sequence specified
            continue;
        }

        if (pSequence->numautolayers >= ARRAYSIZE(pSequence->autolayer)) {
            // TODO: Warning message - Too many layers
            break;
        }

        Q_strncpy(pSequence->autolayer[pSequence->numautolayers].name, pDmeRefSeq->GetName(),
                  sizeof(pSequence->autolayer[pSequence->numautolayers].name));

        CDmeSequenceAddLayer *pDmeAddLayer = CastElement<CDmeSequenceAddLayer>(pDmeSequenceBase->m_eLayerList[i]);
        if (pDmeAddLayer) {
            // Nothing to do
        }

        CDmeSequenceBlendLayer *pDmeBlendLayer = CastElement<CDmeSequenceBlendLayer>(pDmeSequenceBase->m_eLayerList[i]);
        if (pDmeBlendLayer) {
            pSequence->autolayer[pSequence->numautolayers].start = pDmeBlendLayer->m_flStartFrame;
            pSequence->autolayer[pSequence->numautolayers].peak = pDmeBlendLayer->m_flPeakFrame;
            pSequence->autolayer[pSequence->numautolayers].tail = pDmeBlendLayer->m_flTailFrame;
            pSequence->autolayer[pSequence->numautolayers].end = pDmeBlendLayer->m_flEndFrame;
            pSequence->autolayer[pSequence->numautolayers].flags |= pDmeBlendLayer->m_bCrossfade ? STUDIO_AL_XFADE : 0;
            pSequence->autolayer[pSequence->numautolayers].flags |= pDmeBlendLayer->m_bSpline ? STUDIO_AL_SPLINE : 0;
            pSequence->autolayer[pSequence->numautolayers].flags |= pDmeBlendLayer->m_bNoBlend ? STUDIO_AL_NOBLEND : 0;
            pSequence->autolayer[pSequence->numautolayers].flags |= pDmeBlendLayer->m_bLocal ? STUDIO_AL_LOCAL : 0;
            pSequence->flags |= pDmeBlendLayer->m_bLocal ? STUDIO_AL_LOCAL : 0;
            if (!pDmeBlendLayer->m_sPoseParameterName.IsEmpty()) {
                pSequence->autolayer[pSequence->numautolayers].flags |= STUDIO_AL_POSE;
                pSequence->autolayer[pSequence->numautolayers].pose = LookupPoseParameter(
                    pDmeBlendLayer->m_sPoseParameterName.Get());
            }
        }

        pSequence->numautolayers++;
    }
}

void LoadIkLockList(CDmeSequenceBase *pDmeSequenceBase, s_sequence_t *pSequence) {
    if (!pDmeSequenceBase || !pSequence)
        return;

    const int nIkLockCount = pDmeSequenceBase->m_eIkLockList.Count();
    for (int i = 0; i < nIkLockCount; ++i) {
        CDmeIkLock *pDmeIkLock = pDmeSequenceBase->m_eIkLockList[i];
        if (!pDmeIkLock)
            continue;

        // TODO: Check for duplicates?
        CDmeIkChain *pDmeIkChain = pDmeIkLock->m_eIkChain.GetElement();
        if (pDmeIkChain) {
            Q_strncpy(pSequence->iklock[pSequence->numiklocks].name, pDmeIkChain->GetName(),
                      sizeof(pSequence->iklock[pSequence->numiklocks].name));
        }
        else {
            MdlError("1700: No IkChain element assigned to %s:%s\n", pDmeIkLock->GetTypeString(),
                     pDmeIkLock->GetName());
        }
        pSequence->iklock[pSequence->numiklocks].flPosWeight = clamp(pDmeIkLock->m_flLockPosition.Get(), 0.0f, 1.0f);
        pSequence->iklock[pSequence->numiklocks].flLocalQWeight = RemapValClamped(
            pDmeIkLock->m_flLockRotation, 0.0f, 1.0f, 1.0f, 0.0f); // Invert it for historical reasons

        pSequence->numiklocks++;
    }
}

static int LoadAndCreateSequences(s_source_t *pMainSource, CDmeSequenceList *pSequenceList, bool &bSetUpAxis) {
    CreateBindPoseSequence(pMainSource);

    LoadIkChainList(pSequenceList);

    int nSequenceAddedCount = 0;

    char szSeqName[MAX_PATH];
    const int nSequenceCount = pSequenceList ? pSequenceList->m_Sequences.Count() : 0;

    CUtlVector<CDmeSequenceBase *> sortedSequenceList;

    // Add all CDmeSequence elements to the list first
    for (int nSequenceIndex = 0; nSequenceIndex < nSequenceCount; ++nSequenceIndex) {
        CDmeSequenceBase *pDmeSequenceBase = pSequenceList->m_Sequences[nSequenceIndex];
        if (!pDmeSequenceBase) {
            MdlWarning("1201: Empty DmeSequence %s[ %d ]\n", pSequenceList->GetName(), nSequenceIndex);
            continue;
        }

        const char *pszSeqName = pDmeSequenceBase->GetName();
        if (pszSeqName == nullptr || *pszSeqName == '\0') {
            MdlWarning("1200: Ignoring Unnamed Sequence On %s[%d]\n", pSequenceList->GetName(), nSequenceIndex);
            continue;
        }

        sortedSequenceList.AddToTail(pDmeSequenceBase);
    }

    qsort(sortedSequenceList.Base(), sortedSequenceList.Count(), sizeof(CDmeSequenceBase *),
          CDmeSequenceBase::QSortFunction);

    // Process the reordered DmeSequenceBase list
    for (int nSequenceIndex = 0; nSequenceIndex < sortedSequenceList.Count(); ++nSequenceIndex) {
        CUtlVector<s_animation_t *> animations;

        CDmeSequenceBase *pDmeSequenceBase = sortedSequenceList[nSequenceIndex];
        // Already checked for NULL & unnamed

        const char *pszSeqName = pDmeSequenceBase->GetName();
        Q_StripExtension(pszSeqName, szSeqName, sizeof(szSeqName));

        s_sequence_t *pSeq = ProcessCmdSequence(szSeqName);

        {
            CDmeSequence *pDmeSimpleSequence = CastElement<CDmeSequence>(pDmeSequenceBase);
            CDmeMultiSequence *pDmeMultiSequence = CastElement<CDmeMultiSequence>(pDmeSequenceBase);
            if (!pDmeSimpleSequence && !pDmeMultiSequence) {
                MdlWarning("1209: Invalid DmeSequence %s[ %d ], not Simple or Multi\n", pSequenceList->GetName(),
                           nSequenceIndex);
                continue;
            }

            if (pDmeSimpleSequence) {
                CDmeModel *pDmeModel = CastElement<CDmeModel>(pDmeSimpleSequence->m_eSkeleton.GetElement());

                if (bSetUpAxis && pDmeModel) {
                    const char *pUpAxis = pDmeModel->GetValueString("upAxis");
                    if (pUpAxis) {
                        if (StringHasPrefix(pUpAxis, "Y")) {
                            // rotate 90 degrees around x to move y into z
                            g_studiomdl.defaultrotation = RadianEuler(M_PI / 2.0f, 0.0f, M_PI / 2.0f);
                        }
                    }
                    bSetUpAxis = false;
                }

                CDmeDag *pDmeSkeleton = pDmeSimpleSequence->m_eSkeleton.GetElement();
                if (!pDmeSkeleton) {
                    MdlWarning("1205: Ignoring Sequence %s, No Skeleton Specified\n", pszSeqName);
                    continue;
                }

                s_source_t *pSource = AllocateDmxSource(szSeqName);

                BoneTransformMap_t boneMap;
                if (!LoadModelAndSkeleton(pSource, boneMap, pDmeSkeleton, pDmeModel, nullptr, false)) {
                    MdlWarning("1206: Sequence %s: Ignoring Sequence, Couldn't Load Specified Skeleton: %s\n",
                               pszSeqName, pDmeSkeleton->GetName());
                    continue;
                }

                CDmeAnimationList *pDmeAnimationList = pDmeSimpleSequence->m_eAnimationList.GetElement();
                s_animation_t *pAnim = nullptr;

                if (pDmeAnimationList) {
                    // Simple sequence created from animation data
                    LoadAnimations(pSource, pDmeAnimationList, g_studiomdl.currentscale, boneMap);

                    // Check for various CDmeAnimCmd's which may or may not be flagged with bDelta
                    bool bDmeAnimCmdDelta = false;
                    for (int i = 0; i < pDmeSimpleSequence->m_eAnimationCommandList.Count(); ++i) {
                        if (pDmeSimpleSequence->m_eAnimationCommandList[i]->IsA(
                                CDmeAnimCmdSubtract::GetStaticTypeSymbol()) ||
                            pDmeSimpleSequence->m_eAnimationCommandList[i]->IsA(
                                CDmeAnimCmdPreSubtract::GetStaticTypeSymbol()) ||
                            pDmeSimpleSequence->m_eAnimationCommandList[i]->IsA(
                                CDmeAnimCmdLinearDelta::GetStaticTypeSymbol()) ||
                            pDmeSimpleSequence->m_eAnimationCommandList[i]->IsA(
                                CDmeAnimCmdSplineDelta::GetStaticTypeSymbol())) {
                            bDmeAnimCmdDelta = true;
                            pSeq->flags |= STUDIO_DELTA;
                        }
                    }

                    {
                        // allocate animation entry
                        auto *anim = g_studiomdl.animations.emplace_back(c_allocate<s_animation_t>());
                        anim->index = g_studiomdl.animations.size() - 1;
                        pAnim = anim;

                        pAnim->isImplied = true;

                        pAnim->startframe = 0;
                        pAnim->endframe = MAXSTUDIOANIMFRAMES - 1;

                        pAnim->name.format("@%s", pSeq->name.c_str());
                        pAnim->filename = pSource->filename;

                        VectorCopy(g_studiomdl.defaultadjust, pAnim->adjust);
                        pAnim->scale = 1.0f;

                        // Don't re-orient delta animations
                        if (bDmeAnimCmdDelta || !(pDmeSimpleSequence->m_bDelta || pDmeSimpleSequence->m_bPreDelta)) {
                            pAnim->rotation = g_studiomdl.defaultrotation;
                        }

                        pAnim->fps = 30;
                        pAnim->motionrollback = g_StudioMdlContext.defaultMotionRollback;

                        pAnim->source = pSource;
                        const int nSourceAnimCount = pAnim->source->m_Animations.Count();
                        if (nSourceAnimCount > 0) {
                            s_sourceanim_t *pSourceAnim = &pAnim->source->m_Animations[nSourceAnimCount - 1];
                            Q_strncpy(pAnim->animationname,
                                      pAnim->source->m_Animations[nSourceAnimCount - 1].animationname,
                                      sizeof(pAnim->animationname));
                            if (pAnim->startframe < pSourceAnim->startframe) {
                                pAnim->startframe = pSourceAnim->startframe;
                            }

                            if (pAnim->endframe > pSourceAnim->endframe) {
                                pAnim->endframe = pSourceAnim->endframe;
                            }
                        }
                        else {
                            Q_strncpy(pAnim->animationname, "", sizeof(pAnim->animationname));
                        }

                        pAnim->numframes = pAnim->endframe - pAnim->startframe + 1;
                    }
                }
                else {
                    // Simple sequence created from a single pose
                    CreateAnimFromSkeleton(pSource, szSeqName, boneMap);

                    pAnim = ProcessImpliedAnimation(pSeq, pSource->filename.c_str());
                }

                Assert(pAnim);
                animations.AddToTail(pAnim);

                // Hack to allow animations commands to refer to same sequence in a simple DmeSequence
                pSeq->panim[0][0] = pAnim;

                // Animation Options
                LoadAnimationOptions(pDmeSimpleSequence, pAnim);

                // Animation Commands
                LoadAnimationCommands(pDmeSimpleSequence, pAnim);

                // IkRules
                LoadIkRuleList(pDmeSimpleSequence, pAnim);

                AddBodyAttachments(pSource);
            }

            // Only apply to multi-sequences
            if (pDmeMultiSequence) {
                LoadSequenceList(pDmeMultiSequence, animations);

                LoadSequenceBlends(pDmeMultiSequence, pSeq);

                LoadBlendRefCompCenter(pDmeMultiSequence, pSeq);

                pSeq->groupsize[0] = pDmeMultiSequence->m_nBlendWidth.Get();
            }
        }

        LoadAnimationEventList(pDmeSequenceBase, pSeq);

        // Same As ParseSequence
        CDmeSequenceActivity *pDmeSequenceActivity = pDmeSequenceBase->m_eActivity.GetElement();
        if (pDmeSequenceActivity) {
            const char *pszActivityName = pDmeSequenceActivity->GetName();
            if (pszActivityName && Q_strlen(pszActivityName) > 0) {
                Q_strncpy(pSeq->activityname, pszActivityName, sizeof(pSeq->activityname));
                pSeq->actweight = pDmeSequenceActivity->m_nWeight.Get();

                for (int nModifierIndex = 0; nModifierIndex < pDmeSequenceActivity->m_sModifierList.Count(); ++
                     nModifierIndex) {
                    strcpyn(pSeq->activitymodifier[ pSeq->numactivitymodifiers++ ].name,
                            pDmeSequenceActivity->m_sModifierList.Get( nModifierIndex ));

                    if (pSeq->numactivitymodifiers == MAXSTUDIOACTIVITYMODIFIERS)
                        break;
                }

                if (pDmeSequenceActivity->m_sModifierList.Count() > MAXSTUDIOACTIVITYMODIFIERS) {
                    MdlWarning("1210: Too many activity modifiers (%d) on DmeSequence %s, only using first %d\n",
                               pDmeSequenceActivity->m_sModifierList.Count(),
                               pSeq->name.c_str(),
                               MAXSTUDIOACTIVITYMODIFIERS);
                }
            }
        }

        if (pDmeSequenceBase->m_bLoop) {
            pSeq->flags |= STUDIO_LOOPING;
        }

        if (pDmeSequenceBase->m_bSnap) {
            pSeq->flags |= STUDIO_SNAP;
        }

        if (pDmeSequenceBase->m_bPost) {
            pSeq->flags |= STUDIO_POST;
        }

        if (pDmeSequenceBase->m_bHidden) {
            pSeq->flags |= STUDIO_HIDDEN;
        }

        if (pDmeSequenceBase->m_bDelta) {
            pSeq->flags |= STUDIO_DELTA;
            pSeq->flags |= STUDIO_POST;
        }

        if (pDmeSequenceBase->m_bWorldSpace) {
            pSeq->flags |= STUDIO_WORLD;
            pSeq->flags |= STUDIO_POST;
        }

        if (pDmeSequenceBase->m_bPreDelta) {
            pSeq->flags |= STUDIO_DELTA;
        }

        if (pDmeSequenceBase->m_bAutoPlay) {
            pSeq->flags |= STUDIO_AUTOPLAY;
        }

        if (pDmeSequenceBase->m_bRealtime) {
            pSeq->flags |= STUDIO_REALTIME;
        }

        // AddLayer/BlendLayer
        LoadLayerList(pDmeSequenceBase, pSeq);

        // IkLock
        LoadIkLockList(pDmeSequenceBase, pSeq);

        pSeq->fadeintime = pDmeSequenceBase->m_flFadeIn.Get();
        pSeq->fadeouttime = pDmeSequenceBase->m_flFadeOut.Get();

        const CUtlString &entryNode = pDmeSequenceBase->m_sEntryNode.Get();
        const CUtlString &exitNode = pDmeSequenceBase->m_sExitNode.Get();
        if (!entryNode.IsEmpty()) {
            if (!exitNode.IsEmpty()) {
                pSeq->entrynode = LookupXNode(entryNode.Get());
                pSeq->exitnode = LookupXNode(exitNode.Get());
                if (pDmeSequenceBase->m_bReverseNodeTransition) {
                    pSeq->nodeflags |= 1;
                }
            }
            else {
                pSeq->entrynode = pSeq->exitnode = LookupXNode(entryNode.Get());
            }
        }

        if (animations.Count() > 0) {
            ProcessSequence(pSeq, animations.Count(), animations.Base(), false);
            ++nSequenceAddedCount;
        }
        else {
            MdlWarning("1207: DmeSequence %s: No animations created or referenced, ignoring\n", pszSeqName);
        }

        const CUtlString &keyValues = pDmeSequenceBase->m_sKeyValues.Get();
        const int nKeyValuesLength = keyValues.Length();
        if (nKeyValuesLength > 0) {
            pSeq->KeyValue.append(keyValues.Get(), nKeyValuesLength);
        }
    }

    return nSequenceAddedCount;
}

static void LoadIncludeModelList(CDmeIncludeModelList *pIncludeModelList) {
    if (!pIncludeModelList)
        return;

    const int nIncludeModelsCount = pIncludeModelList->m_IncludeModels.Count();
    for (int ni = 0; ni < nIncludeModelsCount; ++ni) {
        if (g_studiomdl.includemodels.full()) {
            MdlError("Too Many Include Models while including: \"%s\", Max: %d\n",
                     pIncludeModelList->m_IncludeModels[ni], g_studiomdl.includemodels.capacity());
        }
        auto &includemodel = g_studiomdl.includemodels.emplace_back(pIncludeModelList->m_IncludeModels[ni]);
        Q_FixSlashes(includemodel.name, '/');
    }
}

static bool IsZero(const QAngle &qa, float tolerance = 0.01f) {
    return (qa.x > -tolerance && qa.x < tolerance &&
            qa.y > -tolerance && qa.y < tolerance &&
            qa.z > -tolerance && qa.z < tolerance);
}

static void UnderscoreSplitAndSortStrings(const char *pszString, CUtlVector<CUtlString> &splitAndSortedString) {
    splitAndSortedString.RemoveAll();

    const int nStrLen = Q_strlen(pszString);
    if (nStrLen <= 0)
        return;

    char *pszTmpString = reinterpret_cast<char *>(stackalloc(( nStrLen + 1 ) * sizeof( char )));
    if (!pszTmpString)
        return;

    Q_memset(pszTmpString, 0, ( nStrLen + 1 ) * sizeof( char ));
    Q_strncpy(pszTmpString, pszString, nStrLen + 1);

    char *pszEnd = nullptr;
    for (char *pszName = pszTmpString; pszName && *pszName; pszName = pszEnd) {
        pszEnd = strchr(pszName, '_');
        if (!pszEnd) {
            splitAndSortedString.AddToTail(CUtlString(pszName));
        }
        else {
            *pszEnd = '\0';
            ++pszEnd;
            splitAndSortedString.AddToTail(CUtlString(pszName));
        }
    }

    // Insertion sort
    for (int i = 1; i < splitAndSortedString.Count(); ++i) {
        const CUtlString sValue = splitAndSortedString[i];
        int j = i - 1;
        while (j >= 0 && Q_stricmp(splitAndSortedString[j].Get(), sValue.Get()) > 0) {
            splitAndSortedString[j + 1] = splitAndSortedString[j];
            --j;
        }
        splitAndSortedString[j + 1] = sValue;
    }
}

static bool GetContentsDescription(int &nContentsDescription, CDmElement *pDmElement) {
    nContentsDescription = CONTENTS_SOLID;

    if (!pDmElement)
        return false;

    CDmAttribute *pDmeContentsDescriptionAttr = pDmElement->GetAttribute("contentsDescription");
    if (!pDmeContentsDescriptionAttr)
        return false;

    if (pDmeContentsDescriptionAttr->GetType() != AT_STRING)
        return false;

    const char *pszContentsDesc = pDmeContentsDescriptionAttr->GetValueString();
    if (!pszContentsDesc || Q_strlen(pszContentsDesc) <= 0)
        return false;

    struct ContentDesc_t {
        const char *m_pszName;
        int m_nContentDescriptionFlags;
    };

    ContentDesc_t contentDescs[] = {
        {"notsolid", CONTENTS_EMPTY},
        {"monster", CONTENTS_MONSTER},
        {"ladder", CONTENTS_LADDER},
        {"solid", CONTENTS_SOLID},
        {"solid_monster", CONTENTS_SOLID | CONTENTS_MONSTER},
        {"solid_ladder", CONTENTS_SOLID | CONTENTS_LADDER},
        {"solid_monster_ladder", CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_LADDER},
        {"grate", CONTENTS_GRATE},
        {"grate_monster", CONTENTS_GRATE | CONTENTS_MONSTER},
        {"grate_ladder", CONTENTS_GRATE | CONTENTS_LADDER},
        {"grate_monster_ladder", CONTENTS_GRATE | CONTENTS_MONSTER | CONTENTS_LADDER},
    };

    CUtlVector<CUtlString> userContentsDesc;
    UnderscoreSplitAndSortStrings(pszContentsDesc, userContentsDesc);

    CUtlVector<CUtlString> validContentsDesc;

    for (int i = 0; i < ARRAYSIZE(contentDescs); ++i) {
        UnderscoreSplitAndSortStrings(contentDescs[i].m_pszName, validContentsDesc);
        if (userContentsDesc.Count() != validContentsDesc.Count())
            continue;

        bool bFound = true;

        for (int j = 0; j < userContentsDesc.Count(); ++j) {
            if (Q_stricmp(userContentsDesc[j].Get(), validContentsDesc[j].Get())) {
                bFound = false;
                break;
            }
        }

        if (bFound) {
            nContentsDescription = contentDescs[i].m_nContentDescriptionFlags;
            return true;
        }
    }

    MdlWarning("2100: Invalid \"contentsDescription\" \"%s\" on \"%s\"\n",
               pszContentsDesc, pDmElement->GetName());

    static bool bWarned = false;
    if (!bWarned) {
        bWarned = true;

        CUtlString sWarn;

        const int nContentDescsCount = ARRAYSIZE(contentDescs);
        if (nContentDescsCount > 0) {
            sWarn = contentDescs[0].m_pszName;

            for (int i = 1; i < nContentDescsCount; ++i) {
                sWarn += ", ";
                sWarn += contentDescs[i].m_pszName;
            }
        }

        MdlWarning("      Valid ones are: %s\n", sWarn.Get());
    }

    return false;
}

static void LoadDefineBoneList(CDmeDefineBoneList *pDmeDefineBoneList) {
    if (!pDmeDefineBoneList)
        return;

    CDmeDefineBone *pDmeDefineBone = nullptr;
    s_importbone_t *pImportBone = nullptr;

    const int nDefineBoneCount = pDmeDefineBoneList->m_DefineBones.Count();
    for (int ni = 0; ni < nDefineBoneCount; ++ni) {
        pDmeDefineBone = pDmeDefineBoneList->m_DefineBones[ni];
        if (!pDmeDefineBone)
            continue;

        if (g_studiomdl.numimportbones >= ARRAYSIZE(g_studiomdl.importbone)) {
            MdlError("Too Many Define Bones while processing: \"%s\", Max: %d\n", pDmeDefineBone->GetName(),
                     ARRAYSIZE(g_studiomdl.importbone));
            break;
        }

        pImportBone = &g_studiomdl.importbone[g_studiomdl.numimportbones++];

        Q_strncpy(pImportBone->name, pDmeDefineBone->GetName(), MAXSTUDIONAME);
        Q_strncpy(pImportBone->parent, pDmeDefineBone->m_Parent.Get(), MAXSTUDIONAME);

        AngleMatrix(pDmeDefineBone->m_Rotation.Get(), pDmeDefineBone->m_Translation.Get(), pImportBone->rawLocal);

        const Vector &rt = pDmeDefineBone->m_RealignTranslation.Get();
        const QAngle &rr = pDmeDefineBone->m_RealignRotation.Get();
        if (!rt.IsZero() || !IsZero(rr)) {
            pImportBone->bPreAligned = true;
            AngleMatrix(pDmeDefineBone->m_RealignRotation.Get(), pDmeDefineBone->m_RealignTranslation.Get(),
                        pImportBone->srcRealign);
        }
        else {
            pImportBone->bPreAligned = false;
            SetIdentityMatrix(pImportBone->srcRealign);
        }

        int nContentsDescription = CONTENTS_SOLID;
        if (GetContentsDescription(nContentsDescription, pDmeDefineBone)) {
            ContentsName_t *pContentsName = nullptr;

            for (int j = 0; j < s_JointContents.Count(); ++j) {
                if (!Q_stricmp(s_JointContents[j].m_pJointName, pDmeDefineBone->GetName())) {
                    pContentsName = &s_JointContents[j];
                    break;
                }
            }

            if (!pContentsName) {
                pContentsName = &s_JointContents[s_JointContents.AddToTail()];
                Q_strncpy(pContentsName->m_pJointName, pDmeDefineBone->GetName(),
                          ARRAYSIZE(pContentsName->m_pJointName));
            }

            pContentsName->m_nContents = nContentsDescription;
        }
    }
}

static s_hitboxset *AllocateHitboxSet() {
    return &g_StudioMdlContext.hitboxsets.emplace_back();
}

static s_bbox_t *AllocateHitbox(s_hitboxset *pHitboxSet) {
    if (!pHitboxSet->hitboxes.full())
        return &pHitboxSet->hitboxes.emplace_back();

    MdlWarning("Too many hitboxes request for hitbox set \"%s\", max %d\n", pHitboxSet->hitboxsetname.c_str(),
               pHitboxSet->hitboxes.capacity());
    return nullptr;
}


static void LoadHitboxSetList(const CDmeHitboxSetList *pDmeHitboxSetList) {
    if (!pDmeHitboxSetList)
        return;

    const int nHitboxSetCount = pDmeHitboxSetList->m_HitboxSetList.Count();
    for (int nHitBoxSetIndex = 0; nHitBoxSetIndex < nHitboxSetCount; ++nHitBoxSetIndex) {
        CDmeHitboxSet *pSrcHitboxSet = pDmeHitboxSetList->m_HitboxSetList[nHitBoxSetIndex];

        // Add a new hitboxset
        s_hitboxset *pHitboxSet = AllocateHitboxSet();
        Q_strncpy(pHitboxSet->hitboxsetname, pSrcHitboxSet->GetName(), sizeof(pHitboxSet->hitboxsetname));

        int nHitboxCount = pSrcHitboxSet->m_HitboxList.Count();
        for (int nHitboxIndex = 0; nHitboxIndex < nHitboxCount; ++nHitboxIndex) {
            CDmeHitbox *pSrcHitbox = pSrcHitboxSet->m_HitboxList[nHitboxIndex];

            // Find bone
            s_bonetable_t *pStudioBone = nullptr;
            const int nBoneIndex = findGlobalBone(pSrcHitbox->m_sBoneName);
            if (nBoneIndex >= 0 && nBoneIndex < g_StudioMdlContext.numbones) {
                pStudioBone = &g_studiomdl.bonetable[nBoneIndex];
            }

            // Find bone
            for (int nBoneIndex = 0; nBoneIndex < g_StudioMdlContext.numbones; ++nBoneIndex) {
            }

            s_bbox_t *pHitbox = AllocateHitbox(pHitboxSet);
            Q_strncpy(pHitbox->name, pSrcHitbox->m_sBoneName, sizeof(pHitbox->name));
            Q_strncpy(pHitbox->hitboxname, pSrcHitbox->GetName(), sizeof(pHitbox->hitboxname));
            pHitbox->group = pSrcHitbox->m_nGroupId;
            pHitbox->bmin = pSrcHitbox->m_vMinBounds;
            pHitbox->bmax = pSrcHitbox->m_vMaxBounds;

            if (!pSrcHitbox->m_sSurfaceProperty.IsEmpty()) {
                const char *pSurfaceProp = FindSurfaceProp(pHitbox->name);
                if (pSurfaceProp && Q_stricmp(pSurfaceProp, pSrcHitbox->m_sSurfaceProperty)) {
                    MdlWarning("Hitbox surface property \"%s\" for bone \"%s\" conflicts with existing \"%s\"",
                               pSrcHitbox->m_sSurfaceProperty.Get(), pSrcHitbox->m_sBoneName.Get(), pSurfaceProp);
                }
                else {
                    AddSurfaceProp(pHitbox->name, pSrcHitbox->m_sSurfaceProperty);
                }
            }
        }
    }
}

static void LoadBoneFlexDriverList(const CDmeBoneFlexDriverList *pDmeBoneFlexDriverList) {
    if (!pDmeBoneFlexDriverList)
        return;

    const CDmeBoneFlexDriverList *pDmeTmp = GetElement<CDmeBoneFlexDriverList>(
        g_StudioMdlContext.hDmeBoneFlexDriverList);
    if (pDmeTmp != nullptr) {
        char szTmpBuf0[40];
        UniqueIdToString(pDmeTmp->GetId(), szTmpBuf0, sizeof(szTmpBuf0));
        char szTmpBuf1[40];
        UniqueIdToString(pDmeBoneFlexDriverList->GetId(), szTmpBuf1, sizeof(szTmpBuf1));

        MdlError("DmeBoneFlexDriverList already defined (%s:%s), ignoring (%s:%s)\n",
                 pDmeTmp->GetName(), szTmpBuf0,
                 pDmeBoneFlexDriverList->GetName(), szTmpBuf1);

        return;
    }

    CDmeBoneFlexDriverList *pDmeCopy = pDmeBoneFlexDriverList->Copy();
    pDmeCopy->SetFileId(DMFILEID_INVALID, TD_DEEP);

    g_StudioMdlContext.hDmeBoneFlexDriverList = pDmeCopy->GetHandle();
}

static void LoadKeyValues(const char *pszKeyValues) {
    if (!pszKeyValues)
        return;

    const int nKeyValueCount = Q_strlen(pszKeyValues);
    if (nKeyValueCount <= 0)
        return;

    const char *pszHeader = "mdlkeyvalue\n{\n";
    const char *pszFooter = "}\n";

    g_StudioMdlContext.KeyValueText.append(pszHeader);
    g_StudioMdlContext.KeyValueText.append(pszKeyValues, nKeyValueCount);
    g_StudioMdlContext.KeyValueText.append(pszFooter);
}

static void LoadGlobalFlags( CDmElement *pDmeRoot )
{
    // Get default rotation as a matrix
    matrix3x4_t mDefRot;
    AngleMatrix( g_studiomdl.defaultrotation, mDefRot );

    // QC $bbox
    CDmeBBox *pDmeBBox = pDmeRoot->GetValueElement< CDmeBBox >( "bbox" );
    if ( pDmeBBox )
    {
        ITransformAABB( mDefRot, pDmeBBox->m_vMinBounds, pDmeBBox->m_vMaxBounds, g_studiomdl.bbox[0], g_studiomdl.bbox[1] );
    }

    if ( pDmeRoot->HasAttribute( "opacity" ) )
    {
        switch ( pDmeRoot->GetValue< int >( "opacity" ) )
        {
            case 0:	// Auto, do nothing
                g_studiomdl.gflags &= ~STUDIOHDR_FLAGS_FORCE_OPAQUE;
                g_studiomdl.gflags &= ~STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
                break;
            case 1:	// opaque
                // Opaque has precedence over Mostly Opaque (TRANSLUCENT_TWOPASS)
                g_studiomdl.gflags |= STUDIOHDR_FLAGS_FORCE_OPAQUE;
                g_studiomdl.gflags &= ~STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
                break;
            case 2:	// mostly opaque
                // Opaque has precedence over Mostly Opaque (TRANSLUCENT_TWOPASS)
                if ( ( g_studiomdl.gflags & STUDIOHDR_FLAGS_FORCE_OPAQUE) == 0)
                {
                    g_studiomdl.gflags |= STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
                }
                break;
        }
    }

    if ( pDmeRoot->HasAttribute( "illuminationPosition", AT_VECTOR3 ) )
    {
        VectorIRotate( pDmeRoot->GetValue< Vector >( "illuminationPosition"), mDefRot, g_studiomdl.illumposition );
    }

    if ( pDmeRoot->GetValue< bool >( "ambientBoost", false ) )
    {
        g_studiomdl.gflags |= STUDIOHDR_FLAGS_AMBIENT_BOOST;
    }

    if ( pDmeRoot->GetValue< bool >( "subdivisionSurface", false ) )
    {
        g_studiomdl.gflags |= STUDIOHDR_FLAGS_SUBDIVISION_SURFACE;
    }

    if ( pDmeRoot->GetValue< bool >( "doNotCastShadows", false ) )
    {
        g_studiomdl.gflags |= STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS;
    }

    if ( pDmeRoot->GetValue< bool >( "castTextureShadows", false ) )
    {
        // Valve set DO_NOT_CAST_SHADOWS here; $casttextureshadows sets CAST_TEXTURE_SHADOWS
        g_studiomdl.gflags |= STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS;
    }

    // CDmeAssetRoot names the attribute "noForcedFace" (dmeasset.cpp), so accept both spellings
    if ( pDmeRoot->GetValue< bool >( "noForcedFade", false ) || pDmeRoot->GetValue< bool >( "noForcedFace", false ) )
    {
        g_studiomdl.gflags |= STUDIOHDR_FLAGS_NO_FORCED_FADE;
    }

    int nContentsDescription = CONTENTS_SOLID;
    if ( GetContentsDescription( nContentsDescription, pDmeRoot ) )
    {
        s_nDefaultContents = nContentsDescription;
    }
}

//-----------------------------------------------------------------------------
// Main entry point for loading preprocessed files
//-----------------------------------------------------------------------------
#ifdef MDLCOMPILE
// Everything in the root of a loaded .mpp; false (after MdlError) on failure
static bool LoadPreprocessedRoot(CDmElement *pRoot, const std::string &fullPath) {
    const char *pMdlPath = pRoot->GetValueString("mdlPath");
    if (pMdlPath && *pMdlPath) {
        // This is a hack... really the model path should be able to be anywhere relative to the game
        // directory but currently "models/" is prepended everywhere... would like to get rid of that
        // pattern but would also like to limit changes required for mdlcompile
        if (!Q_strnicmp(pMdlPath, "models", 6) && (pMdlPath[6] == '/' || pMdlPath[6] == '\\')) {
            ProcessModelName(pMdlPath + 7);
        }
        else {
            ProcessModelName(pMdlPath);
        }
    }

    // Get whether we're doing skinned LODs from the pre-process file
    g_studiomdl.bSkinnedLODs = pRoot->GetValue<bool>("skinnedLODs", false);

    // Load model info
    LoadModelInfo(pRoot, fullPath.c_str());

    // Find out if it's marked as a static prop
    const bool bStaticProp = pRoot->GetValue<bool>("staticProp");

    bool bSetUpAxis = true;
    s_source_t *pMainSource = nullptr;

    CDmeBodyGroupList *pBodyGroupList = pRoot->GetValueElement<CDmeBodyGroupList>("bodyGroupList");
    if (pBodyGroupList) {
        // Loads all body groups
        if (!LoadBodyGroupList(&pMainSource, pBodyGroupList,
                               pRoot->GetValueElement<CDmeEyeballGlobals>("eyeballGlobals"), bStaticProp, bSetUpAxis))
            return false;

        CDmeCollisionModel *pCollisionModel = pRoot->GetValueElement<CDmeCollisionModel>("collisionModel");
        if (pCollisionModel && !LoadCollisionModel(pCollisionModel, bStaticProp))
            return false;

        LoadCollisionText(pRoot->GetValueString("collisionText"));

        // Deal with material groups.  Ok to pass NULL for CDmeMaterialGroupList
        LoadMaterialGroups(pRoot->GetValueElement<CDmeMaterialGroupList>("materialGroupList"));

        // Deal with bone merge directives.  Ok to pass NULL DmAttribute for string array
        LoadBoneMergeList(pBodyGroupList->GetAttribute("boneMergeList"));

        // Deal with bone merge directives.  Ok to pass NULL DmAttribute for string array
        // TODO: Bone keep list is a little funny, right now just treat the same as bone
        //       merge list which will ensure the bone is always present.  This will also
        //       increase the bone priority and make the bone available to the server
        LoadBoneMergeList(pBodyGroupList->GetAttribute("boneKeepList"));
    }
    else {
        if (bStaticProp) {
            MdlError("0002: Static prop specified but no body groups present\n");
            return false;
        }

        // This MPP has no body groups, so maybe it's an animation only MPP?
        pMainSource = AllocateDmxSource("anim");
    }

    // Deal with static props
    if (bStaticProp && pBodyGroupList) {
        // FIXME: This source should come from the skeleton;
        // need to figure out if static props can deal with multiple sources
        SetupStaticProp(pMainSource);
        return true;
    }

    // Nothing after here is applied if this is a static prop
    LoadBoneMaskList(pRoot->GetValueElement<CDmeBoneMaskList>("boneMaskList"));

    // Deal with pose parameters.  Ok to pass NULL for CDmePoseParameterList
    LoadPoseParameterList(pRoot->GetValueElement<CDmePoseParameterList>("poseParameterList"));

    // Deal with animBlockSize, Ok to pass NULL for CDmeAnimBlockSize
    LoadAnimBlockSize(pRoot->GetValueElement<CDmeAnimBlockSize>("animBlockSize"));

    // Deal with sequences.  Ok to pass NULL for CDmeSequenceList
    const int nSequenceCount = LoadAndCreateSequences(pMainSource,
                                                      pRoot->GetValueElement<CDmeSequenceList>("sequenceList"),
                                                      bSetUpAxis);
    if (nSequenceCount == 0 && !pBodyGroupList) {
        MdlError("0003: MPP has no body groups and no animations\n");
        return false;
    }

    // Deal with include models.  Ok to pass NULL for CDmeIncludeModelList
    LoadIncludeModelList(pRoot->GetValueElement<CDmeIncludeModelList>("includeModelList"));

    // Deal with define bones.  Ok to pass NULL for CDmeDefineBoneList
    LoadDefineBoneList(pRoot->GetValueElement<CDmeDefineBoneList>("defineBoneList"));

    // Deal with hitbox sets.  Ok to pass NULL for CDmeHitboxSetList
    LoadHitboxSetList(pRoot->GetValueElement<CDmeHitboxSetList>("hitboxSetList"));

    // Deal with boneFlexDriver
    LoadBoneFlexDriverList(pRoot->GetValueElement<CDmeBoneFlexDriverList>("boneFlexDriverList"));

    // At this point, reorienting of skeleton defined stuff will likely be required

    // Deal with KeyValues, Ok to pass NULL or empty string
    LoadKeyValues(pRoot->GetValueString("keyValues"));

    LoadGlobalFlags(pRoot);

    return true;
}
#endif // MDLCOMPILE

//-----------------------------------------------------------------------------
// Main entry point for loading preprocessed files
//-----------------------------------------------------------------------------
bool LoadPreprocessedFile(const char *pFileName, float flScale) {
#ifndef MDLCOMPILE
    return false;
#else
    // use the full search tree, including mod hierarchy to find the file
    std::string fullPath;
    if (!GetGlobalFilePath(pFileName, fullPath)) {
        MdlError("Invalid MPP Filename: %s\n", pFileName);
        return false;
    }

    // When reading, keep the CRLF; this will make ReadFile read it in binary format
    // and also append a couple 0s to the end of the buffer.
    CDmElement *pRoot;
    if (g_pDataModel->RestoreFromFile(fullPath.c_str(), nullptr, nullptr, &pRoot) == DMFILEID_INVALID) {
        MdlError("0001: Couldn't Load MPP File: %s\n", fullPath.c_str());
        return false;
    }

    if (!g_StudioMdlContext.quiet) {
        Msg("Loaded Preprocessed File %s\n", fullPath.c_str());
    }

    const bool bLoaded = LoadPreprocessedRoot(pRoot, fullPath);
    g_pDataModel->RemoveFileId(pRoot->GetFileId());
    return bLoaded;
#endif // MDLCOMPILE
}
