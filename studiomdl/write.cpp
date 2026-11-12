//===== Copyright � 1996-2008, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

//
// write.c: writes a studio .mdl file
//

#pragma warning( disable : 4244 )
#pragma warning( disable : 4237 )
#pragma warning( disable : 4305 )

#include <filesystem>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <climits>

#include "common/cmdlib.h"
#include "common/scriplib.h"
#include "studio.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/optimize.h"
#include "materialsystem/imaterial.h"
#include "mdlobjects/dmeboneflexdriver.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/perfstats.h"
#include "studiomdl/output_files.hpp"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/buffer.hpp"
#include "tier1_v2/math_utils.hpp"

int totalframes = 0;
float totalseconds = 0;
extern int numcommandnodes;


// WriteFile is the only externally visible function in this file.
// pData points to the current location in an output buffer and pStart is
// the beginning of the buffer.

bool FixupToSortedLODVertexes(studiohdr_t* pStudioHdr);

bool Clamp_RootLOD(studiohdr_t* phdr);

/*
============
WriteModel
============
*/

static int sExtraTexcoordsToWrite = 0;


#undef ALIGN16
#undef ALIGN32
#undef ALIGN4
#define ALIGN4(a) a = (uint8_t *)((uintptr_t)((uint8_t *)(a) + 3) & ~ 3)
#define ALIGN16(a) a = (uint8_t *)((uintptr_t)((uint8_t *)(a) + 15) & ~ 15)
#define ALIGN32(a) a = (uint8_t *)((uintptr_t)((uint8_t *)(a) + 31) & ~ 31)
#define ALIGN64(a) a = (uint8_t *)((uintptr_t)((uint8_t *)(a) + 63) & ~ 63)
#define ALIGN512(a) a = (uint8_t *)((uintptr_t)((uint8_t *)(a) + 511) & ~ 511)

int k_memtotal = 0;

//--------------------------------------------------------------------
// Allocate aligned memory of at least nCount * nSize bytes of memory via malloc
// Cannot be freed as the pointer returned isn't necessarily the actual start
// of the block of memory allocated from the heap due to alignment on 512 byte boundaries
// Only use allocation of the written file
//--------------------------------------------------------------------
void* kalloc(int num, int size) {
    int nMemSize = num * size;
    k_memtotal += nMemSize;

    // ensure memory alignment on maximum of ALIGN
    nMemSize += 511;
    void* ptr = malloc(nMemSize);
    memset(ptr, 0, nMemSize);
    ptr = (uint8_t*) ((size_t) ((uint8_t*) ptr + 511) & ~511);
    return ptr;
}


#define FILEBUFFER (32 * 1024 * 1024)

void WriteSeqKeyValues(vstd::Buffer &buffer, vstd::BufferPtr<mstudioseqdesc_t> pseqdesc, std::string &pKeyValue);

//-----------------------------------------------------------------------------
// Purpose: stringtable is a session global string table.
// An entry locates its base and offset field either inside a vstd::Buffer (as byte offsets, so the buffer may
// reallocate before WriteStringTable) or in plain memory (as addresses).
//-----------------------------------------------------------------------------

struct stringtable_t {
    const char* string;
    int dupindex;
    vstd::Buffer* buffer; // nullptr: base/field/addr are addresses
    uintptr_t base; // what the stored offset is relative to
    uintptr_t field; // the int receiving the offset
    uintptr_t addr; // where the string was written, same space as base
};

static int numStrings;
static stringtable_t strings[32768];

static uint8_t* ResolveStringTableLocation(const stringtable_t &entry, const uintptr_t location) {
    return entry.buffer ? entry.buffer->data() + location : reinterpret_cast<uint8_t*>(location);
}

static void BeginStringTable() {
    strings[0] = {"", -1, nullptr, 0, 0, 0};
    numStrings = 1;
}

static void AddStringTableEntry(vstd::Buffer* buffer, const uintptr_t base, const uintptr_t field, const char* string) {
    if (!string) {
        string = "";
    }

    int dupindex = -1;
    for (int i = 0; i < numStrings; i++) {
        if (!strcmp(string, strings[i].string)) {
            dupindex = i;
            break;
        }
    }
    strings[numStrings++] = {string, dupindex, buffer, base, field, 0};
}

//-----------------------------------------------------------------------------
// Purpose: add a string to the file-global string table.
//			Keep track of fixup locations
//-----------------------------------------------------------------------------
static void AddToStringTable(void* base, int* ptr, const char* string) {
    AddStringTableEntry(nullptr, reinterpret_cast<uintptr_t>(base), reinterpret_cast<uintptr_t>(ptr), string);
}

template<typename B>
static void AddToStringTable(const vstd::BufferPtr<B> &base, const vstd::BufferPtr<int> &field, const char* string) {
    AddStringTableEntry(base.buffer(), base.offset(), field.offset(), string);
}

static void WriteStringTableEntries(vstd::Buffer* buffer, uintptr_t (*emit)(const char* string, void* context),
                                    void* context) {
    // force null at first address
    strings[0].addr = emit("", context);

    // save all the rest
    for (int i = 1; i < numStrings; i++) {
        stringtable_t &entry = strings[i];
        Assert(entry.buffer == buffer);
        // keep track of the address in case of duplication; an already stored string is reused
        entry.addr = entry.dupindex == -1 ? emit(entry.string, context) : strings[entry.dupindex].addr;
        // offset relative to the local base
        const auto offset = static_cast<intptr_t>(entry.addr) - static_cast<intptr_t>(entry.base);
        *reinterpret_cast<int*>(ResolveStringTableLocation(entry, entry.field)) = static_cast<int>(offset);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Write out stringtable
//			fixup local pointers
//-----------------------------------------------------------------------------
static uint8_t* WriteStringTable(uint8_t* pData) {
    vstd::FunctionTimingRecorder _("Write");
    WriteStringTableEntries(nullptr, [](const char* string, void* context) {
        uint8_t* &cursor = *static_cast<uint8_t**>(context);
        const auto address = reinterpret_cast<uintptr_t>(cursor);
        const size_t size = strlen(string) + 1;
        memcpy(cursor, string, size);
        cursor += size;
        return address;
    }, &pData);
    ALIGN4(pData);
    return pData;
}

static void WriteStringTable(vstd::Buffer &buffer) {
    vstd::FunctionTimingRecorder _("Write");
    WriteStringTableEntries(&buffer, [](const char* string, void* context) {
        auto &target = *static_cast<vstd::Buffer*>(context);
        const size_t size = strlen(string) + 1;
        const auto position = target.allocate<char>(size);
        memcpy(position.get(), string, size);
        return static_cast<uintptr_t>(position.offset());
    }, &buffer);
    buffer.align(4);
}


// compare function for qsort below
static int BoneNameCompare(const void* elem1, const void* elem2) {
    int index1 = *(uint8_t*) elem1;
    int index2 = *(uint8_t*) elem2;

    // compare bones by name
    return strcmpi(g_studiomdl.bonetable[index1].name, g_studiomdl.bonetable[index2].name);
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class M, class S, ProcBoneType procType>
static vstd::BufferPtr<S> WriteBaseConstraint(vstd::Buffer &buffer, M* pConstraint,
                                              const vstd::BufferPtr<mstudiobone_t> pbone) {
    if (!pConstraint)
        return {};

    const auto pProc = buffer.allocate<S>();
    buffer.align(4);

    pProc->m_slave.m_nBone = pConstraint->m_slave.m_nBone;
    pProc->m_slave.m_vBasePosition = pConstraint->m_slave.m_vBaseTranslate;
    pProc->m_slave.m_qBaseOrientation = pConstraint->m_slave.m_qBaseRotation;

    const int k = pProc->m_slave.m_nBone;
    pbone[k].procindex = pProc.template offset_from<int>(pbone + k);
    pbone[k].proctype = procType;

    pProc->m_nTargetCount = pConstraint->m_targets.Count();
    const auto pTarget = buffer.allocate<mstudioconstrainttarget_t>(pProc->m_nTargetCount);
    pProc->m_nTargetIndex = pTarget.template offset_from<int>(pProc);
    buffer.align(4);

    for (int j = 0; j < pConstraint->m_targets.Count(); ++j) {
        s_constraintbonetarget_t &target = pConstraint->m_targets[j];
        pTarget[j].m_nBone = target.m_nBone;
        pTarget[j].m_flWeight = target.m_flWeight;
        pTarget[j].m_vOffset = target.m_vOffset;
        pTarget[j].m_qOffset = target.m_qOffset;
    }

    return pProc;
}


static void WriteBoneInfo(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr_t> phdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j, k;

    // save bone info
    const auto pbones = buffer.allocate<mstudiobone_t>(g_StudioMdlContext.numbones);
    phdr->numbones = g_StudioMdlContext.numbones;
    phdr->boneindex = pbones.offset_as<int>();

    char* pSurfacePropName = GetDefaultSurfaceProp();
    AddToStringTable(phdr, phdr.field(&studiohdr_t::surfacepropindex), pSurfacePropName);
    phdr->contents = GetDefaultContents();

    for (i = 0; i < g_StudioMdlContext.numbones; i++) {
        AddToStringTable(pbones + i, (pbones + i).field(&mstudiobone_t::sznameindex), g_studiomdl.bonetable[i].name);
        pbones[i].parent = g_studiomdl.bonetable[i].parent;
        pbones[i].flags = g_studiomdl.bonetable[i].flags;
        pbones[i].procindex = 0;
        pbones[i].physicsbone = g_studiomdl.bonetable[i].physicsBoneIndex;
        pbones[i].pos = g_studiomdl.bonetable[i].pos;
        pbones[i].rot = g_studiomdl.bonetable[i].rot;
        pbones[i].posscale = g_studiomdl.bonetable[i].posscale;
        pbones[i].rotscale = g_studiomdl.bonetable[i].rotscale;
        MatrixInvert(g_studiomdl.bonetable[i].boneToPose, pbones[i].poseToBone);
        pbones[i].qAlignment = g_studiomdl.bonetable[i].qAlignment;

        AngleQuaternion(RadianEuler(g_studiomdl.bonetable[i].rot[0], g_studiomdl.bonetable[i].rot[1],
                                    g_studiomdl.bonetable[i].rot[2]),
                        pbones[i].quat);
        QuaternionAlign(pbones[i].qAlignment, pbones[i].quat, pbones[i].quat);

        pSurfacePropName = GetSurfaceProp(g_studiomdl.bonetable[i].name);
        AddToStringTable(pbones + i, (pbones + i).field(&mstudiobone_t::surfacepropidx), pSurfacePropName);
        pbones[i].contents = GetContents(g_studiomdl.bonetable[i].name);
    }

    buffer.align(4);

    // save procedural bone info
    if (g_studiomdl.numaxisinterpbones) {
        auto proc_bone = buffer.allocate<mstudioaxisinterpbone_t>(g_studiomdl.numaxisinterpbones);
        for (i = 0; i < g_studiomdl.numaxisinterpbones; i++) {
            j = g_studiomdl.axisinterpbonemap[i];
            k = g_studiomdl.axisinterpbones[j].bone;
            pbones[k].procindex = (proc_bone + i).offset_from<int>(pbones + k);
            pbones[k].proctype = ProcBoneType::AxisInterp;
            // printf("bone %d %d\n", j, pbone[k].procindex );
            proc_bone[i].control = g_studiomdl.axisinterpbones[j].control;
            proc_bone[i].axis = g_studiomdl.axisinterpbones[j].axis;
            for (k = 0; k < 6; k++) {
                VectorCopy(g_studiomdl.axisinterpbones[j].pos[k], proc_bone[i].pos[k]);
                proc_bone[i].quat[k] = g_studiomdl.axisinterpbones[j].quat[k];
            }
        }
        buffer.align(4);
    }

    if (g_studiomdl.numquatinterpbones) {
        auto proc_bone = buffer.allocate<mstudioquatinterpbone_t>(g_studiomdl.numquatinterpbones);

        for (i = 0; i < g_studiomdl.numquatinterpbones; i++) {
            j = g_studiomdl.quatinterpbonemap[i];
            k = g_studiomdl.quatinterpbones[j].bone;
            pbones[k].procindex = (proc_bone + i).offset_from<int>(pbones + k);
            pbones[k].proctype = ProcBoneType::QuatInterp;
            // printf("bone %d %d\n", j, pbone[k].procindex );
            proc_bone[i].control = g_studiomdl.quatinterpbones[j].control;

            // the count first: it sizes the allocation, which may move proc_bone's memory
            proc_bone[i].numtriggers = g_studiomdl.quatinterpbones[j].numtriggers;
            auto trigger = buffer.allocate<mstudioquatinterpinfo_t>(proc_bone[i].numtriggers);
            proc_bone[i].triggerindex = trigger.offset_from<int>(proc_bone + i);

            for (k = 0; k < proc_bone[i].numtriggers; k++) {
                trigger[k].inv_tolerance = 1.0 / g_studiomdl.quatinterpbones[j].tolerance[k];
                trigger[k].trigger = g_studiomdl.quatinterpbones[j].trigger[k];
                trigger[k].pos = g_studiomdl.quatinterpbones[j].pos[k];
                trigger[k].quat = g_studiomdl.quatinterpbones[j].quat[k];
            }
        }
        buffer.align(4);
    }

    if (g_studiomdl.numjigglebones) {
        auto jiggleInfo = buffer.allocate<mstudiojigglebone_t>(g_studiomdl.numjigglebones);

        for (i = 0; i < g_studiomdl.numjigglebones; i++) {
            j = g_studiomdl.jigglebonemap[i];
            k = g_studiomdl.jigglebones[j].bone;
            pbones[k].procindex = (jiggleInfo + i).offset_from<int>(pbones + k);
            pbones[k].proctype = ProcBoneType::Jiggle;

            jiggleInfo[i] = g_studiomdl.jigglebones[j].data;
        }
        buffer.align(4);
    }

    // write aim at bones
    if (g_studiomdl.numaimatbones) {
        auto pProc = buffer.allocate<mstudioaimatbone_t>(g_studiomdl.numaimatbones);
        for (i = 0; i < g_studiomdl.numaimatbones; i++) {
            j = g_studiomdl.aimatbonemap[i];
            k = g_studiomdl.aimatbones[j].bone;
            pbones[k].procindex = (pProc + i).offset_from<int>(pbones + k);
            pbones[k].proctype = g_studiomdl.aimatbones[j].aimAttach == -1
                                     ? ProcBoneType::AimAtBone
                                     : ProcBoneType::AimAtAttach;
            pProc[i].parent = g_studiomdl.aimatbones[j].parent;
            pProc[i].aim = g_studiomdl.aimatbones[j].aimAttach == -1
                               ? g_studiomdl.aimatbones[j].aimBone
                               : g_studiomdl.aimatbones[j].aimAttach;
            pProc[i].aimvector = g_studiomdl.aimatbones[j].aimvector;
            pProc[i].upvector = g_studiomdl.aimatbones[j].upvector;
            pProc[i].basepos = g_studiomdl.aimatbones[j].basepos;
        }
        buffer.align(4);
    }

    // Write twist bones
#if 0 // DISABLED IN CSGO
    if (g_twistbones.Count() > 0) {
        mstudiotwistbone_t* pProc = (mstudiotwistbone_t*) pData;
        pData += g_twistbones.Count() * sizeof(mstudiotwistbone_t);
        buffer.align(4);

        for (i = 0; i < g_twistbones.Count(); ++i) {
            const CTwistBone &twistBone = g_twistbones[i];
            pProc[i].m_bInverse = twistBone.m_bInverse;
            pProc[i].m_vUpVector = twistBone.m_vUpVector;
            pProc[i].m_nParentBone = twistBone.m_nParentBone;
            QuaternionInvert(twistBone.m_qBaseRotation, pProc[i].m_qBaseInv);
            pProc[i].m_nChildBone = twistBone.m_nChildBone;

            mstudiotwistbonetarget_t* pTarget = (mstudiotwistbonetarget_t*) pData;
            pProc[i].m_nTargetCount = twistBone.m_twistBoneTargets.Count();
            pProc[i].m_nTargetIndex = (uint8_t*) pTarget - (uint8_t*) &pProc[i];
            pData += twistBone.m_twistBoneTargets.Count() * sizeof(mstudiotwistbone_t);
            buffer.align(4);

            for (j = 0; j < twistBone.m_twistBoneTargets.Count(); ++j) {
                const s_constraintbonetarget_t &twistBoneTarget = twistBone.m_twistBoneTargets[j];

                k = twistBoneTarget.m_nBone;
                pTarget[j].m_nBone = k;
                pTarget[j].m_flWeight = twistBoneTarget.m_flWeight;
                pTarget[j].m_vBaseTranslate = twistBoneTarget.m_vOffset;
                pTarget[j].m_qBaseRotation = twistBoneTarget.m_qOffset;

                pbones[k].procindex = (uint8_t*) &pProc[i] - (uint8_t*) &pbones[k];
                pbones[k].proctype = j == 0 ? ProcBoneType::TwistMaster : ProcBoneType::TwistSLAVE;
            }
        }
    }
#endif

    // Write constraint bones
    if (g_studiomdl.constraintBones.Count() > 0) {
        for (int i = 0; i < g_studiomdl.constraintBones.Count(); ++i) {
            CConstraintBoneBase* pConstraintBone = g_studiomdl.constraintBones[i];
            if (!pConstraintBone)
                continue;

            {
                CPointConstraint* pConstraint = dynamic_cast<CPointConstraint*>(pConstraintBone);
                if (pConstraint) {
                    WriteBaseConstraint<CPointConstraint, mstudiopointconstraint_t, ProcBoneType::PointConstraint>(
                        buffer, pConstraint, pbones);
                    continue;
                }
            }

            {
                COrientConstraint* pConstraint = dynamic_cast<COrientConstraint*>(pConstraintBone);
                if (pConstraint) {
                    WriteBaseConstraint<COrientConstraint, mstudioorientconstraint_t, ProcBoneType::OrientConstraint>(
                        buffer, pConstraint, pbones);
                    continue;
                }
            }

            {
                CAimConstraint* pConstraint = dynamic_cast<CAimConstraint*>(pConstraintBone);

                if (pConstraint) {
                    const auto pProc =
                            WriteBaseConstraint<CAimConstraint, mstudioaimconstraint_t, ProcBoneType::AimConstraint>(
                                buffer, pConstraint, pbones);

                    if (pProc) {
                        // Local Aim Constraint Parameters
                        pProc->m_qAimOffset = pConstraint->m_qAimOffset;
                        pProc->m_vUp = pConstraint->m_vUpVector;
                        pProc->m_nUpSpaceTarget = pConstraint->m_nUpSpaceTargetBone;
                        pProc->m_nUpType = pConstraint->m_nUpType;

                        continue;
                    }
                }
            }

            {
                CParentConstraint* pConstraint = dynamic_cast<CParentConstraint*>(pConstraintBone);
                if (pConstraint) {
                    WriteBaseConstraint<CParentConstraint, mstudioparentconstraint_t, ProcBoneType::ParentConstraint>(
                        buffer, pConstraint, pbones);
                    continue;
                }
            }

            MdlWarning("Ignoring Constraint Bone: {}\n", pConstraintBone->m_slave.m_szBoneName);
        }
    }

    // map g_studiomdl.bonecontroller to bones
    for (i = 0; i < g_StudioMdlContext.numbones; i++) {
        for (j = 0; j < 6; j++) {
            pbones[i].bonecontroller[j] = -1;
        }
    }

    for (i = 0; i < g_studiomdl.bonecontrollers.size(); i++) {
        j = g_studiomdl.bonecontrollers[i].bone;
        switch (g_studiomdl.bonecontrollers[i].type & StudioMotionFlags::Types) {
            case StudioMotionFlags::X:
                pbones[j].bonecontroller[0] = i;
                break;
            case StudioMotionFlags::Y:
                pbones[j].bonecontroller[1] = i;
                break;
            case StudioMotionFlags::Z:
                pbones[j].bonecontroller[2] = i;
                break;
            case StudioMotionFlags::XR:
                pbones[j].bonecontroller[3] = i;
                break;
            case StudioMotionFlags::YR:
                pbones[j].bonecontroller[4] = i;
                break;
            case StudioMotionFlags::ZR:
                pbones[j].bonecontroller[5] = i;
                break;
            default:
                MdlError("unknown g_studiomdl.bonecontroller type\n");
        }
    }

    // save g_studiomdl.bonecontroller info
    const auto pbonecontroller = buffer.allocate<mstudiobonecontroller_t>(g_studiomdl.bonecontrollers.size());
    phdr->numbonecontrollers = g_studiomdl.bonecontrollers.size();
    phdr->bonecontrollerindex = pbonecontroller.offset_as<int>();

    for (i = 0; i < g_studiomdl.bonecontrollers.size(); i++) {
        pbonecontroller[i].bone = g_studiomdl.bonecontrollers[i].bone;
        pbonecontroller[i].inputfield = g_studiomdl.bonecontrollers[i].inputfield;
        pbonecontroller[i].type = g_studiomdl.bonecontrollers[i].type;
        pbonecontroller[i].start = g_studiomdl.bonecontrollers[i].start;
        pbonecontroller[i].end = g_studiomdl.bonecontrollers[i].end;
    }
    buffer.align(4);

    // save attachment info
    const auto pattachment = buffer.allocate<mstudioattachment_t>(g_studiomdl.attachments.size());
    phdr->numlocalattachments = g_studiomdl.attachments.size();
    phdr->localattachmentindex = pattachment.offset_as<int>();

    for (i = 0; i < g_studiomdl.attachments.size(); i++) {
        pattachment[i].localbone = g_studiomdl.attachments[i].bone;
        AddToStringTable(pattachment + i, (pattachment + i).field(&mstudioattachment_t::sznameindex),
                         g_studiomdl.attachments[i].name.c_str());
        MatrixCopy(g_studiomdl.attachments[i].local, pattachment[i].local);
        pattachment[i].flags = g_studiomdl.attachments[i].flags;
    }
    buffer.align(4);

    // save hitbox sets
    phdr->numhitboxsets = g_StudioMdlContext.hitboxsets.size();

    // Remember start spot
    auto hitboxset = buffer.allocate<mstudiohitboxset_t>(phdr->numhitboxsets);
    phdr->hitboxsetindex = hitboxset.offset_as<int>();
    buffer.align(4);

    for (int s = 0; s < g_StudioMdlContext.hitboxsets.size(); s++, hitboxset++) {
        s_hitboxset* set = &g_StudioMdlContext.hitboxsets[s];

        AddToStringTable(hitboxset, hitboxset.field(&mstudiohitboxset_t::sznameindex), set->hitboxsetname);

        hitboxset->numhitboxes = set->hitboxes.size();

        // save bbox info; hitboxindex is relative to its set
        const auto pbbox = buffer.allocate<mstudiobbox_t>(hitboxset->numhitboxes);
        hitboxset->hitboxindex = pbbox.offset_from<int>(hitboxset);
        for (i = 0; i < hitboxset->numhitboxes; i++) {
            const s_bbox_t &hitbox = set->hitboxes[i];
            pbbox[i].bone = hitbox.bone;
            pbbox[i].group = hitbox.group;
            VectorCopy(hitbox.bmin, pbbox[i].bbmin);
            VectorCopy(hitbox.bmax, pbbox[i].bbmax);
            VectorCopy(hitbox.angOffsetOrientation, pbbox[i].angOffsetOrientation);
            pbbox[i].flCapsuleRadius = hitbox.flCapsuleRadius;
            pbbox[i].szhitboxnameindex = 0;
            AddToStringTable(pbbox + i, (pbbox + i).field(&mstudiobbox_t::szhitboxnameindex), hitbox.hitboxname);
        }

        buffer.align(4);
    }

    const auto boneTable = buffer.allocate<uint8_t>(phdr->numbones);
    phdr->bonetablebynameindex = boneTable.offset_as<int>();

    // make a table in bone order and sort it with qsort
    for (i = 0; i < phdr->numbones; i++) {
        boneTable[i] = i;
    }
    qsort(boneTable.get(), phdr->numbones, sizeof(uint8_t), BoneNameCompare);
    buffer.align(4);
}

// load a preexisting model to remember its sequence names and indices
CUtlVector<CUtlString> g_vecPreexistingSequences;

void LoadPreexistingSequenceOrder(const char* pFilename) {
    g_vecPreexistingSequences.RemoveAll();

    if (!FileExists(pFilename)) {
        if (g_StudioMdlContext.errorOnSeqRemapFail)
            MdlError(
                "This model requires a sequence remapping match. Please sync to the latest model on disk before recompiling.\n");
        return;
    }

    Msg("Loading preexisting model: %s\n", pFilename);

    studiohdr_t* pStudioHdr;
    int len = LoadFile((char*) pFilename, (void**) &pStudioHdr);

    if (len && pStudioHdr && pStudioHdr->SequencesAvailable()) {
        Msg("   Found %i preexisting sequences.\n", pStudioHdr->GetNumSeq());

        for (int i = 0; i < pStudioHdr->GetNumSeq(); i++) {
            //Msg( "   Sequence %i : \"%s\"\n", i, pStudioHdr->pSeqdesc(i).pszLabel() );
            g_vecPreexistingSequences.AddToTail(pStudioHdr->pSeqdesc(i).pszLabel());
        }
    }
    else if (g_StudioMdlContext.modelIntentionallyHasZeroSequences) {
        // some models like scaffolds, intentionally don't have input sequences. Not sure if this is the best way to allow this exception.
    }
    else if (g_StudioMdlContext.errorOnSeqRemapFail) {
        MdlError("Zero-size file or no sequences. This model requires a sequence remapping match.\n");
    }
    else {
        MdlWarning("Zero-size file or no sequences.\n");
    }
}

static void WriteSequenceInfo(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr_t> phdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j, k;

    // write models to disk with this flag set false. This will force
    // the sequences to be indexed by activity whenever the g_studiomdl.model is loaded
    // from disk.
    phdr->activitylistversion = 0;
    phdr->eventsindexed = 0;

    // save g_studiomdl.sequence info
    auto pseqdesc = buffer.allocate<mstudioseqdesc_t>(g_studiomdl.sequence.Count());
    auto pbaseseqdesc = pseqdesc;
    phdr->numlocalseq = g_studiomdl.sequence.Count();
    phdr->localseqindex = pseqdesc.offset_as<int>();

    bool bErrors = false;


    // build a table to remap new sequence indices to match the preexisting model
    bool bUseSeqOrderRemapping = false;
    int nSeqOrderRemappingTable[MAXSTUDIOSEQUENCES];
    for (i = 0; i < MAXSTUDIOSEQUENCES; i++)
        nSeqOrderRemappingTable[i] = -1;

    bool bAllowSequenceRemoval = false;

    if (g_vecPreexistingSequences.Count()) {
        if (g_studiomdl.sequence.Count() < g_vecPreexistingSequences.Count() && !bAllowSequenceRemoval) {
            Msg("\n");
            MdlWarning(
                "This model has fewer sequences than its predecessor.\nPlease confirm sequence deletion: [y/n] ");
            int nInput = 0;
            do { nInput = getchar(); } while (nInput != 121 /* y */ && nInput != 110 /* n */);

            if (nInput == 110) {
                MdlError("Model contains fewer sequences than its predecessor!\n");
            }
            else if (nInput == 121) {
                bAllowSequenceRemoval = true;
            }
        }

        {
            Msg("Building sequence index remapping table...\n");

            CUtlVector<int> vecNewIndices;
            vecNewIndices.RemoveAll();

            // map current sequences to their old indices
            for (i = 0; i < g_studiomdl.sequence.Count(); i++) {
                int nIdx = g_vecPreexistingSequences.Find(g_studiomdl.sequence[i].name.c_str());
                if (nIdx >= 0) {
                    nSeqOrderRemappingTable[nIdx] = i;
                }
                else {
                    if (i < g_vecPreexistingSequences.Count()) {
                        Msg("  Found new sequence \"%s\" using index of old sequence \"%s\".\n",
                            g_studiomdl.sequence[i].name.c_str(),
                            g_vecPreexistingSequences[i].Get());
                    }
                    else {
                        Msg("  Found new sequence \"%s\".\n", g_studiomdl.sequence[i].name.c_str());
                    }

                    vecNewIndices.AddToTail(i);
                }
            }

            // slot new sequences into unused indices
            while (vecNewIndices.Count()) {
                for (i = 0; i < MAXSTUDIOSEQUENCES; i++) {
                    if (nSeqOrderRemappingTable[i] == -1) {
                        nSeqOrderRemappingTable[i] = vecNewIndices[0];
                        vecNewIndices.Remove(0);
                        break;
                    }
                }
            }

            // verify no indices are undefined
            for (i = 0; i < g_studiomdl.sequence.Count(); i++) {
                if (nSeqOrderRemappingTable[i] == -1) {
                    if (bAllowSequenceRemoval) {
                        do {
                            for (int nB = i; nB < g_vecPreexistingSequences.Count(); nB++) {
                                nSeqOrderRemappingTable[nB] = nSeqOrderRemappingTable[nB + 1];
                            }
                        } while (nSeqOrderRemappingTable[i] == -1);
                    }
                    else {
                        MdlError("Failed to reorder sequence indices.\n");
                    }
                }
                else if (nSeqOrderRemappingTable[i] != i) {
                    bUseSeqOrderRemapping = true;
                }
            }

            if (bUseSeqOrderRemapping) {
                Msg("Sequence indices need re-ordering.\n");
            }
            else {
                Msg("No re-ordering required.\n");
            }
        }
    }

    // build an inverted remapping table so autolayer sequence indices can find their sources later
    int nSeqOrderRemappingTableInv[MAXSTUDIOSEQUENCES];
    if (bUseSeqOrderRemapping) {
        for (i = 0; i < MAXSTUDIOSEQUENCES; i++)
            nSeqOrderRemappingTableInv[nSeqOrderRemappingTable[i]] = i;
    }

    int m;
    for (m = 0; m < g_studiomdl.sequence.Count(); m++, pseqdesc++) {
        if (bUseSeqOrderRemapping) {
            i = nSeqOrderRemappingTable[m];
            if (i != m) {
                Msg("   Remapping sequence %i to index %i (%s) to retain existing order.\n", i, m,
                    g_studiomdl.sequence[i].name.c_str());
            }
        }
        else {
            i = m;
        }

        AddToStringTable(pseqdesc, pseqdesc.field(&mstudioseqdesc_t::szlabelindex), g_studiomdl.sequence[i].name);
        AddToStringTable(pseqdesc, pseqdesc.field(&mstudioseqdesc_t::szactivitynameindex),
                         g_studiomdl.sequence[i].activityname);

        // offset from this descriptor back to the header
        pseqdesc->baseptr = phdr.offset_from<int>(pseqdesc);

        pseqdesc->flags = g_studiomdl.sequence[i].flags;

        pseqdesc->numblends = g_studiomdl.sequence[i].numblends;
        pseqdesc->groupsize[0] = g_studiomdl.sequence[i].groupsize[0];
        pseqdesc->groupsize[1] = g_studiomdl.sequence[i].groupsize[1];

        pseqdesc->paramindex[0] = g_studiomdl.sequence[i].paramindex[0];
        pseqdesc->paramstart[0] = g_studiomdl.sequence[i].paramstart[0];
        pseqdesc->paramend[0] = g_studiomdl.sequence[i].paramend[0];
        pseqdesc->paramindex[1] = g_studiomdl.sequence[i].paramindex[1];
        pseqdesc->paramstart[1] = g_studiomdl.sequence[i].paramstart[1];
        pseqdesc->paramend[1] = g_studiomdl.sequence[i].paramend[1];

        if (g_studiomdl.sequence[i].groupsize[0] > 1 || g_studiomdl.sequence[i].groupsize[1] > 1) {
            // save posekey values
            auto posekey = buffer.allocate<float>(pseqdesc->groupsize[0] + pseqdesc->groupsize[1]);
            pseqdesc->posekeyindex = posekey.offset_from<int>(pseqdesc);
            for (j = 0; j < pseqdesc->groupsize[0]; j++) {
                if (g_studiomdl.sequence[i].param0.IsValidIndex(j)) {
                    *(posekey++) = g_studiomdl.sequence[i].param0[j];
                }
                else {
                    *(posekey++) = NULL;
                }
                // printf("%.2f ", g_studiomdl.sequence[i].param0[j] );
            }
            for (j = 0; j < pseqdesc->groupsize[1]; j++) {
                if (g_studiomdl.sequence[i].param1.IsValidIndex(j)) {
                    *(posekey++) = g_studiomdl.sequence[i].param1[j];
                }
                else {
                    *(posekey++) = NULL;
                }
                // printf("%.2f ", g_studiomdl.sequence[i].param1[j] );
            }
            // printf("\n" );
        }

        // pseqdesc->motiontype	= g_studiomdl.sequence[i].motiontype;
        // pseqdesc->motionbone	= 0; // g_studiomdl.sequence[i].motionbone;
        // VectorCopy( g_studiomdl.sequence[i].linearmovement, pseqdesc->linearmovement );

        pseqdesc->activity = g_studiomdl.sequence[i].activity;
        pseqdesc->actweight = g_studiomdl.sequence[i].actweight;

        pseqdesc->bbmin = g_studiomdl.sequence[i].bmin;
        pseqdesc->bbmax = g_studiomdl.sequence[i].bmax;

        pseqdesc->fadeintime = g_studiomdl.sequence[i].fadeintime;
        pseqdesc->fadeouttime = g_studiomdl.sequence[i].fadeouttime;

        pseqdesc->localentrynode = g_studiomdl.sequence[i].entrynode;
        pseqdesc->localexitnode = g_studiomdl.sequence[i].exitnode;
        //pseqdesc->entryphase	= g_studiomdl.sequence[i].entryphase;
        //pseqdesc->exitphase		= g_studiomdl.sequence[i].exitphase;
        pseqdesc->nodeflags = g_studiomdl.sequence[i].nodeflags;

        // save events
        // the count first: it sizes the allocation
        pseqdesc->numevents = g_studiomdl.sequence[i].events.size();
        const auto pevent = buffer.allocate<mstudioevent_t>(pseqdesc->numevents);
        pseqdesc->eventindex = pevent.offset_from<int>(pseqdesc);
        for (j = 0; j < g_studiomdl.sequence[i].events.size(); j++) {
            k = g_studiomdl.sequence[i].panim[0][0]->numframes - 1;

            if (g_studiomdl.sequence[i].events[j].frame <= k)
                pevent[j].cycle = g_studiomdl.sequence[i].events[j].frame / ((float) k);
            else if (k == 0 && g_studiomdl.sequence[i].events[j].frame == 0)
                pevent[j].cycle = 0;
            else {
                MdlWarning("Event {} (frame {}) out of range in {}\n", g_studiomdl.sequence[i].events[j].event,
                           g_studiomdl.sequence[i].events[j].frame, g_studiomdl.sequence[i].name);
                bErrors = true;
            }

            //Adrian - Remove me once we phase out the old event system.
            if (V_isdigit(g_studiomdl.sequence[i].events[j].eventname[0])) {
                pevent[j].event = atoi(g_studiomdl.sequence[i].events[j].eventname);
                pevent[j].type = StudioEventType::OldStyle;
                pevent[j].szeventindex = 0;
            }
            else {
                AddToStringTable(pevent + j, (pevent + j).field(&mstudioevent_t::szeventindex),
                                 g_studiomdl.sequence[i].events[j].eventname);
                pevent[j].type = StudioEventType::NewStyle;
            }


            // printf("%4d : %d %f\n", pevent[j].event, g_studiomdl.sequence[i].event[j].frame, pevent[j].cycle );
            // AddToStringTable( &pevent[j], &pevent[j].szoptionindex, g_studiomdl.sequence[i].event[j].options );
            strcpy(pevent[j].options, g_studiomdl.sequence[i].events[j].options);
        }
        buffer.align(4);

        // save ikrules
        pseqdesc->numikrules = g_studiomdl.sequence[i].numikrules;

        // save autolayers
        pseqdesc->numautolayers = g_studiomdl.sequence[i].autolayers.size();
        const auto pautolayer = buffer.allocate<mstudioautolayer_t>(pseqdesc->numautolayers);
        pseqdesc->autolayerindex = pautolayer.offset_from<int>(pseqdesc);
        for (j = 0; j < g_studiomdl.sequence[i].autolayers.size(); j++) {
            pautolayer[j].iSequence = g_studiomdl.sequence[i].autolayers[j].sequence;
            pautolayer[j].iPose = g_studiomdl.sequence[i].autolayers[j].pose;
            pautolayer[j].flags = g_studiomdl.sequence[i].autolayers[j].flags;

            // autolayer indices are stored by index, so remap them now using the invertex lookup table
            if (bUseSeqOrderRemapping) {
                int nRemapAutoLayer = nSeqOrderRemappingTableInv[pautolayer[j].iSequence];
                if (nRemapAutoLayer != pautolayer[j].iSequence) {
                    Msg("       Autolayer remapping index %i to %i.\n", pautolayer[j].iSequence, nRemapAutoLayer);
                    pautolayer[j].iSequence = nRemapAutoLayer;
                }
            }

            if (!HasFlag(pautolayer[j].flags, AutoLayerFlags::Pose)) {
                pautolayer[j].start = g_studiomdl.sequence[i].autolayers[j].start / (
                                          g_studiomdl.sequence[i].panim[0][0]->numframes - 1);
                pautolayer[j].peak = g_studiomdl.sequence[i].autolayers[j].peak / (
                                         g_studiomdl.sequence[i].panim[0][0]->numframes - 1);
                pautolayer[j].tail = g_studiomdl.sequence[i].autolayers[j].tail / (
                                         g_studiomdl.sequence[i].panim[0][0]->numframes - 1);
                pautolayer[j].end = g_studiomdl.sequence[i].autolayers[j].end / (
                                        g_studiomdl.sequence[i].panim[0][0]->numframes - 1);
            }
            else {
                pautolayer[j].start = g_studiomdl.sequence[i].autolayers[j].start;
                pautolayer[j].peak = g_studiomdl.sequence[i].autolayers[j].peak;
                pautolayer[j].tail = g_studiomdl.sequence[i].autolayers[j].tail;
                pautolayer[j].end = g_studiomdl.sequence[i].autolayers[j].end;
            }
        }


        // save boneweights
        float* pweight = nullptr;
        j = 0;
        // look up previous sequence weights and try to find a match
        for (k = 0; k < m; k++) {
            j = 0;
            // only check newer boneweights than the last one
            if (pseqdesc[k - m].pBoneweight(0) > pweight) {
                pweight = pseqdesc[k - m].pBoneweight(0);
                for (j = 0; j < g_StudioMdlContext.numbones; j++) {
                    // we're not walking the linear sequence list if we're remapping, so we need to remap this check
                    int nRemap = k;
                    if (bUseSeqOrderRemapping)
                        nRemap = nSeqOrderRemappingTable[k];

                    if (g_studiomdl.sequence[i].weight[j] != g_studiomdl.sequence[nRemap].weight[j])
                        break;
                }
                if (j == g_StudioMdlContext.numbones)
                    break;
            }
        }

        // check to see if all the bones matched
        if (j < g_StudioMdlContext.numbones) {
            // allocate new block
            const auto pnewweight = buffer.allocate<float>(g_StudioMdlContext.numbones);
            pseqdesc->weightlistindex = pnewweight.offset_from<int>(pseqdesc);
            for (j = 0; j < g_StudioMdlContext.numbones; j++) {
                pnewweight[j] = g_studiomdl.sequence[i].weight[j];
            }
        }
        else {
            // use previous boneweight (pweight is current: nothing was allocated since it was read)
            pseqdesc->weightlistindex = static_cast<int>((uint8_t*) pweight - (uint8_t*) pseqdesc.get());
        }


        // save iklocks
        pseqdesc->numiklocks = g_studiomdl.sequence[i].iklocks.size();
        auto piklock = buffer.allocate<mstudioiklock_t>(pseqdesc->numiklocks);
        pseqdesc->iklockindex = piklock.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < pseqdesc->numiklocks; j++) {
            piklock->chain = g_studiomdl.sequence[i].iklocks[j].chain;
            piklock->flPosWeight = g_studiomdl.sequence[i].iklocks[j].flPosWeight;
            piklock->flLocalQWeight = g_studiomdl.sequence[i].iklocks[j].flLocalQWeight;
            piklock++;
        }

        // Write animation blend parameters
        const auto blends = buffer.allocate<short>(
            g_studiomdl.sequence[i].groupsize[0] * g_studiomdl.sequence[i].groupsize[1]);
        pseqdesc->animindexindex = blends.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < g_studiomdl.sequence[i].groupsize[0]; j++) {
            for (k = 0; k < g_studiomdl.sequence[i].groupsize[1]; k++) {
                // height value * width of row + width value
                int offset = k * g_studiomdl.sequence[i].groupsize[0] + j;

                if (g_studiomdl.sequence[i].panim[j][k]) {
                    int animindex = g_studiomdl.sequence[i].panim[j][k]->index;

                    Assert(animindex >= 0 && animindex < SHRT_MAX);

                    blends[offset] = (short) animindex;
                }
                else {
                    blends[offset] = 0;
                }
            }
        }

        // Write cycle overrides
        pseqdesc->cycleposeindex = g_studiomdl.sequence[i].cycleposeindex;

        WriteSeqKeyValues(buffer, pseqdesc, g_studiomdl.sequence[i].KeyValue);

        // Write activity modifiers
        pseqdesc->numactivitymodifiers = g_studiomdl.sequence[i].numactivitymodifiers;
        const auto pactivitymodifier = buffer.allocate<mstudioactivitymodifier_t>(pseqdesc->numactivitymodifiers);
        pseqdesc->activitymodifierindex = pactivitymodifier.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < pseqdesc->numactivitymodifiers; j++) {
            AddToStringTable(pactivitymodifier + j,
                             (pactivitymodifier + j).field(&mstudioactivitymodifier_t::sznameindex),
                             g_studiomdl.sequence[i].activitymodifier[j].name);
        }


        // save animtags
        pseqdesc->numanimtags = g_studiomdl.sequence[i].animtags.size();
        const auto panimtag = buffer.allocate<mstudioanimtag_t>(pseqdesc->numanimtags);
        pseqdesc->animtagindex = panimtag.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < g_studiomdl.sequence[i].animtags.size(); j++) {
            panimtag[j].cycle = g_studiomdl.sequence[i].animtags[j].cycle;
            AddToStringTable(panimtag + j, (panimtag + j).field(&mstudioanimtag_t::sztagindex),
                             g_studiomdl.sequence[i].animtags[j].tagname);
        }

        if (HasFlag(g_studiomdl.sequence[i].flags, StudioSequenceFlags::RootXform)) {
            int bone = findGlobalBone(g_studiomdl.sequence[i].rootDriverBoneName);
            if (bone != -1) {
                pseqdesc->rootDriverIndex = bone;
            }
            else {
                MdlError("unable to find bone {}\n", g_studiomdl.sequence[i].rootDriverBoneName);
            }
        }
        else {
            pseqdesc->rootDriverIndex = 0;
        }
        buffer.align(4);
    }

    if (bErrors) {
        MdlError("Exiting due to Errors\n");
    }

    // save transition graph
    auto pxnodename = buffer.allocate<int>(g_studiomdl.xnodenames.size());
    phdr->localnodenameindex = pxnodename.offset_as<int>();
    buffer.align(4);
    for (i = 0; i < g_studiomdl.xnodenames.size(); i++) {
        AddToStringTable(phdr, pxnodename, g_studiomdl.xnodenames[i].c_str());
        // printf("%d : %s\n", i, g_studiomdl.xnodename[i+1] );
        pxnodename++;
    }

    auto ptransition = buffer.allocate<uint8_t>(g_studiomdl.xnodenames.size() * g_studiomdl.xnodenames.size());
    phdr->numlocalnodes = g_studiomdl.xnodenames.size();
    phdr->localnodeindex = ptransition.offset_as<int>();
    buffer.align(4);
    for (i = 0; i < g_studiomdl.xnodenames.size(); i++) {
        //		printf("%2d (%12s) : ", i + 1, g_studiomdl.xnodename[i+1] );
        for (j = 0; j < g_studiomdl.xnodenames.size(); j++) {
            *ptransition++ = g_studiomdl.xnodes[i][j];
            //			printf(" %2d", g_studiomdl.xnode[i][j] );
        }
        //		printf("\n" );
    }
}

//-----------------------------------------------------------------------------
// Purpose: Stub implementation
// Input  : *group -
//-----------------------------------------------------------------------------

const studiohdr_t* studiohdr_t::FindModel(void** cache, char const* modelname) const {
    return nullptr;
}

virtualmodel_t* studiohdr_t::GetVirtualModel() const {
    return nullptr;
}

const studiohdr_t* virtualgroup_t::GetStudioHdr() const {
    return (studiohdr_t*) cache;
}

uint8_t* studiohdr_t::GetAnimBlock(int i, bool preloadIfMissing) const {
    return nullptr;
}

bool studiohdr_t::hasAnimBlockBeenPreloaded(int i) const {
    return false;
}

int studiohdr_t::GetAutoplayList(unsigned short** pOut) const {
    return 0;
}


int rawanimbytes = 0;
int animboneframes = 0;

int numAxis[4] = {0, 0, 0, 0};
int numPos[4] = {0, 0, 0, 0};
int useRaw = 0;


// out: the buffer the section is appended to, the .mdl data or the .ani block data
void WriteRLEAnimationData(s_animation_t* srcanim, vstd::Buffer &out, int w) {
    int k, n;

    auto destanim = out.allocate<mstudio_rle_anim_t>();

    destanim->bone = 255;

    vstd::BufferPtr<mstudio_rle_anim_t> prevanim;

    // save animation value info
    for (int j = 0; j < g_StudioMdlContext.numbones; j++) {
        destanim->flags = StudioAnimFlags::None;
        s_compressed_t* psrcdata = &srcanim->anim[w][j];

        numPos[(psrcdata->num[0] != 0) + (psrcdata->num[1] != 0) + (psrcdata->num[2] != 0)]++;
        numAxis[(psrcdata->num[3] != 0) + (psrcdata->num[4] != 0) + (psrcdata->num[5] != 0)]++;

        if (psrcdata->num[0] + psrcdata->num[1] + psrcdata->num[2] + psrcdata->num[3] + psrcdata->num[4] +
            psrcdata->num[5] == 0) {
            // no animation, skip
            continue;
        }

        destanim->bone = j;

        // copy flags over if delta animation
        if (HasFlag(srcanim->flags, StudioSequenceFlags::Delta)) {
            destanim->flags |= StudioAnimFlags::Delta;
        }

        if ((srcanim->numframes == 1) ||
            (psrcdata->num[0] <= 2 && psrcdata->num[1] <= 2 && psrcdata->num[2] <= 2 && psrcdata->num[3] <= 2 &&
             psrcdata->num[4] <= 2 && psrcdata->num[5] <= 2)) {
            // single frame, if animation detected just store as raw
            int iFrame = MIN(w * srcanim->sectionframes, srcanim->numframes - 1);
            if (psrcdata->num[3] != 0 || psrcdata->num[4] != 0 || psrcdata->num[5] != 0) {
                Quaternion q;
                AngleQuaternion(srcanim->sanim[iFrame][j].rot, q);
                *out.allocate<Quaternion64>() = q;
                rawanimbytes += sizeof(Quaternion64);
                destanim->flags |= StudioAnimFlags::RawRot2;
            }

            if (psrcdata->num[0] != 0 || psrcdata->num[1] != 0 || psrcdata->num[2] != 0) {
                *out.allocate<Vector48>() = srcanim->sanim[iFrame][j].pos;
                rawanimbytes += sizeof(Vector48);
                destanim->flags |= StudioAnimFlags::RawPos;
            }
        }
        else {
            // look to see if storing raw quat's would have taken less space
            if (psrcdata->num[3] >= srcanim->numframes && psrcdata->num[4] >= srcanim->numframes &&
                psrcdata->num[5] >= srcanim->numframes) {
                useRaw++;
            }

            // allocate room for rotation ptrs
            const auto rotvptr = out.allocate<mstudioanim_valueptr_t>();

            // skip all position info if there's no animation
            vstd::BufferPtr<mstudioanim_valueptr_t> posvptr;
            if (psrcdata->num[0] != 0 || psrcdata->num[1] != 0 || psrcdata->num[2] != 0) {
                posvptr = out.allocate<mstudioanim_valueptr_t>();
            }

            const size_t valuesStart = out.size();
            // appends the values of one channel, sets its offset relative to vptr
            const auto writeChannel = [&](const vstd::BufferPtr<mstudioanim_valueptr_t> &vptr, const int slot,
                                          const int channel) {
                if (psrcdata->num[channel] == 0) {
                    vptr->offset[slot] = 0;
                    return;
                }
                const auto values = out.allocate<mstudioanimvalue_t>(psrcdata->num[channel]);
                vptr->offset[slot] = values.offset_from<int>(vptr);
                for (n = 0; n < psrcdata->num[channel]; n++) {
                    values[n].value = psrcdata->data[channel][n].value;
                }
            };

            // store rotation animations
            for (k = 3; k < 6; k++) {
                writeChannel(rotvptr, k - 3, k);
            }
            destanim->flags |= StudioAnimFlags::AnimRot;

            if (posvptr) {
                // store position animations
                for (k = 0; k < 3; k++) {
                    writeChannel(posvptr, k, k);
                }
                destanim->flags |= StudioAnimFlags::AnimPos;
            }
            rawanimbytes += out.size() - valuesStart;
        }

        prevanim = destanim;
        destanim->nextoffset = out.end_ptr().offset_from<int>(destanim);
        destanim = out.allocate<mstudio_rle_anim_t>();
    }

    if (prevanim) {
        prevanim->nextoffset = 0;
    }

    out.align(4);
}

void WriteFrameAnimationData(s_animation_t* srcanim, vstd::Buffer &out, int w) {
    // allocate room for header
    const auto destframeanim = out.allocate<mstudio_frame_anim_t>();

    // write flags and constants (the flags follow the header, see mstudio_frame_anim_t::pBoneFlags)
    const auto flag = out.allocate<StudioFrameFlags>(g_StudioMdlContext.numbones);

    out.align(4);

    destframeanim->constantsoffset = out.end_ptr().offset_from<int>(destframeanim);
    int framelength = 0;
    int iFrame = MIN(w * srcanim->sectionframes, srcanim->numframes - 1);

    for (int j = 0; j < g_StudioMdlContext.numbones; j++) {
        s_compressed_t* psrcdata = &srcanim->anim[w][j];

        if (psrcdata->num[3] == 0 && psrcdata->num[4] == 0 && psrcdata->num[5] == 0) {
            // no change
        }
        else if (psrcdata->num[3] <= 2 && psrcdata->num[4] <= 2 && psrcdata->num[5] <= 2) {
            flag[j] |= StudioFrameFlags::ConstRot2;
            Quaternion q;
            AngleQuaternion(srcanim->sanim[iFrame][j].rot, q);
            *out.allocate<Quaternion48S>() = q;
        }
        else {
            flag[j] |= StudioFrameFlags::AnimRot2;
            framelength += sizeof(Quaternion48S);
        }

        if (psrcdata->num[0] == 0 && psrcdata->num[1] == 0 && psrcdata->num[2] == 0) {
            // no change
        }
        else if (psrcdata->num[0] <= 2 && psrcdata->num[1] <= 2 && psrcdata->num[2] <= 2) {
            // single frame
            if (g_StudioMdlContext.animblockHighRes) {
                flag[j] |= StudioFrameFlags::ConstPos2;
                *out.allocate<Vector>() = srcanim->sanim[iFrame][j].pos;
            }
            else {
                flag[j] |= StudioFrameFlags::ConstPos;
                *out.allocate<Vector48>() = srcanim->sanim[iFrame][j].pos;
            }
        }
        else {
            // multiple frames
            if (g_StudioMdlContext.animblockHighRes) {
                flag[j] |= StudioFrameFlags::AnimPos2;
                framelength += sizeof(Vector);
            }
            else {
                flag[j] |= StudioFrameFlags::AnimPos;
                framelength += sizeof(Vector48);
            }
        }
    }

    out.align(4);

    // write raw data
    destframeanim->frameoffset = out.end_ptr().offset_from<int>(destframeanim);
    destframeanim->framelength = framelength;

    int iStartFrame = 0;
    int iEndFrame = srcanim->numframes - 1;

    if (srcanim->sectionframes > 0) {
        iStartFrame = MIN(w * srcanim->sectionframes, srcanim->numframes - 1);
        iEndFrame = MIN((w + 1) * srcanim->sectionframes, srcanim->numframes - 1);
    }

    for (iFrame = iStartFrame; iFrame <= iEndFrame; iFrame++) {
        // save animation value info
        for (int j = 0; j < g_StudioMdlContext.numbones; j++) {
            if (HasFlag(flag[j], StudioFrameFlags::AnimRot2)) {
                Quaternion q;
                AngleQuaternion(srcanim->sanim[iFrame][j].rot, q);
                *out.allocate<Quaternion48S>() = q;
            }

            if (HasFlag(flag[j], StudioFrameFlags::AnimPos)) {
                *out.allocate<Vector48>() = srcanim->sanim[iFrame][j].pos;
            }
            else if (HasFlag(flag[j], StudioFrameFlags::AnimPos2)) {
                *out.allocate<Vector>() = srcanim->sanim[iFrame][j].pos;
            }
        }
    }

    out.align(4);
}


// Writes the sections of srcanim: into local (the .mdl data, where destanimdesc lives) or, when anim blocks are
// in use, into ext (the .ani block data; nullptr when there's no .ani file).
void WriteAnimationData(s_animation_t* srcanim, const vstd::BufferPtr<mstudioanimdesc_t> destanimdesc,
                        vstd::Buffer &local, vstd::Buffer* ext) {
    vstd::FunctionTimingRecorder _("Write");

    for (int w = 0; w < srcanim->numsections; w++) {
        const bool bUseExtData = ext != nullptr && !srcanim->disableAnimblocks &&
                                 !((w * srcanim->sectionframes < srcanim->numNostallFrames) &&
                                   srcanim->isFirstSectionLocal);
        vstd::Buffer &out = bUseExtData ? *ext : local;

        const size_t startSection = out.size();

        // use frameanim if not lowres data
        if (ext != nullptr && !g_StudioMdlContext.animblockLowRes) {
            srcanim->flags |= StudioSequenceFlags::FrameAnim;
            destanimdesc->flags |= StudioSequenceFlags::FrameAnim;
        }

        if (HasFlag(srcanim->flags, StudioSequenceFlags::FrameAnim)) {
            WriteFrameAnimationData(srcanim, out, w);
        }
        else {
            WriteRLEAnimationData(srcanim, out, w);
        }

        const size_t sectionSize = out.size() - startSection;
        if (g_studiomdl.animblocksize > 0 && sectionSize > static_cast<size_t>(g_studiomdl.animblocksize)) {
            MdlWarning(
                "Single animation \"{}\" is {}. Specificed block size is {}.  Use smaller animations or increase the block size.\n",
                srcanim->name, sectionSize, g_studiomdl.animblocksize);
        }

        // write into anim blocks if needed
        if (destanimdesc->sectionindex) {
            if (bUseExtData) {
                if (g_studiomdl.animblocks.size() &&
                    static_cast<int64_t>(out.size()) - g_studiomdl.animblocks.back().start > g_studiomdl.
                    animblocksize) {
                    // advance to next animblock
                    g_studiomdl.animblocks.back().end = startSection;
                    g_studiomdl.animblocks.emplace_back(s_animblock_t{.start = static_cast<uint32_t>(startSection)});
                }

                destanimdesc->pSection(w)->animblock = g_studiomdl.animblocks.size() - 1;
                destanimdesc->pSection(w)->animindex =
                        static_cast<int>(startSection - g_studiomdl.animblocks.back().start);
            }
            else {
                destanimdesc->pSection(w)->animblock = 0;
                destanimdesc->pSection(w)->animindex = local.ptr_at<uint8_t>(startSection).offset_from<int>(
                    destanimdesc);
            }
        }
    }
}


void WriteIkErrors(s_animation_t* srcanim, vstd::Buffer &out) {
    vstd::FunctionTimingRecorder _("Write");
    int k;

    // write IK error keys
    const auto pikruledata = out.allocate<mstudioikrule_t>(srcanim->ikrules.size());
    out.align(4);

    for (int j = 0; j < srcanim->ikrules.size(); j++) {
        const auto pikrule = pikruledata + j;

        pikrule->index = srcanim->ikrules[j].index;

        pikrule->chain = srcanim->ikrules[j].chain;
        pikrule->bone = srcanim->ikrules[j].bone;
        pikrule->type = srcanim->ikrules[j].type;
        pikrule->slot = srcanim->ikrules[j].slot;
        pikrule->pos = srcanim->ikrules[j].pos;
        pikrule->q = srcanim->ikrules[j].q;
        pikrule->height = srcanim->ikrules[j].height;
        pikrule->floor = srcanim->ikrules[j].floor;
        pikrule->radius = srcanim->ikrules[j].radius;

        if (srcanim->numframes > 1.0) {
            pikrule->start = srcanim->ikrules[j].start / (srcanim->numframes - 1.0f);
            pikrule->peak = srcanim->ikrules[j].peak / (srcanim->numframes - 1.0f);
            pikrule->tail = srcanim->ikrules[j].tail / (srcanim->numframes - 1.0f);
            pikrule->end = srcanim->ikrules[j].end / (srcanim->numframes - 1.0f);
            pikrule->contact = srcanim->ikrules[j].contact / (srcanim->numframes - 1.0f);
        }
        else {
            pikrule->start = 0.0f;
            pikrule->peak = 0.0f;
            pikrule->tail = 1.0f;
            pikrule->end = 1.0f;
            pikrule->contact = 0.0f;
        }

        /*
		printf("%d %d %d %d : %.2f %.2f %.2f %.2f\n",
			srcanim->ikrule[j].start, srcanim->ikrule[j].peak, srcanim->ikrule[j].tail, srcanim->ikrule[j].end,
			pikrule->start, pikrule->peak, pikrule->tail, pikrule->end );
		*/

        pikrule->iStart = srcanim->ikrules[j].start;

#if 0
        // uncompressed
        pikrule->ikerrorindex = (pData - (uint8_t*) pikrule);
        mstudioikerror_t* perror = (mstudioikerror_t*) pData;
        pData += srcanim->ikrule[j].numerror * sizeof(*perror);

        for (k = 0; k < srcanim->ikrule[j].numerror; k++) {
            perror[k].pos = srcanim->ikrule[j].pError[k].pos;
            perror[k].q = srcanim->ikrule[j].pError[k].q;
        }
#endif
#if 1
        // skip writting the header if there's no IK data
        for (k = 0; k < 6; k++) {
            if (srcanim->ikrules[j].errorData.numanim[k]) break;
        }

        if (k == 6)
            continue;

        // compressed
        const auto pCompressed = out.allocate<mstudiocompressedikerror_t>();
        pikrule->compressedikerrorindex = pCompressed.offset_from<int>(pikrule);

        for (k = 0; k < 6; k++) {
            pCompressed->scale[k] = srcanim->ikrules[j].errorData.scale[k];
            const auto values = out.allocate<mstudioanimvalue_t>(srcanim->ikrules[j].errorData.numanim[k]);
            pCompressed->offset[k] = values.offset_from<int>(pCompressed);
            memcpy(values.get(), srcanim->ikrules[j].errorData.anim[k].data(),
                   srcanim->ikrules[j].errorData.numanim[k] * sizeof(mstudioanimvalue_t));
        }

        if (strlen(srcanim->ikrules[j].attachment) > 0) {
            // don't use string table, we're probably not in the same file.
            const size_t size = strlen(srcanim->ikrules[j].attachment) + 1;
            const auto attachment = out.allocate<char>(size);
            memcpy(attachment.get(), srcanim->ikrules[j].attachment, size);
            pikrule->szattachmentindex = attachment.offset_from<int>(pikrule);
        }

        out.align(4);

#endif
        // AddToStringTable( pikrule, &pikrule->szattachmentindex, srcanim->ikrule[j].attachment );
    }
}


void WriteLocalHierarchy(s_animation_t* srcanim, vstd::Buffer &out) {
    vstd::FunctionTimingRecorder _("Write");
    int k;

    // write hierarchy  keys
    const auto pHierarchyData = out.allocate<mstudiolocalhierarchy_t>(srcanim->localhierarchies.size());
    out.align(4);

    for (int j = 0; j < srcanim->localhierarchies.size(); j++) {
        const auto pHierarchy = pHierarchyData + j;

        pHierarchy->iBone = srcanim->localhierarchies[j].bone;
        pHierarchy->iNewParent = srcanim->localhierarchies[j].newparent;

        if (srcanim->numframes > 1.0) {
            pHierarchy->start = srcanim->localhierarchies[j].start / (srcanim->numframes - 1.0f);
            pHierarchy->peak = srcanim->localhierarchies[j].peak / (srcanim->numframes - 1.0f);
            pHierarchy->tail = srcanim->localhierarchies[j].tail / (srcanim->numframes - 1.0f);
            pHierarchy->end = srcanim->localhierarchies[j].end / (srcanim->numframes - 1.0f);
        }
        else {
            pHierarchy->start = 0.0f;
            pHierarchy->peak = 0.0f;
            pHierarchy->tail = 1.0f;
            pHierarchy->end = 1.0f;
        }

        pHierarchy->iStart = srcanim->localhierarchies[j].start;

#if 0
        // uncompressed
        pHierarchy->ikerrorindex = (pData - (uint8_t*) pHierarchy);
        mstudioikerror_t* perror = (mstudioikerror_t*) pData;
        pData += srcanim->ikrule[j].numerror * sizeof(*perror);

        for (k = 0; k < srcanim->ikrule[j].numerror; k++) {
            perror[k].pos = srcanim->ikrule[j].pError[k].pos;
            perror[k].q = srcanim->ikrule[j].pError[k].q;
        }
#endif
#if 1
        // skip writting the header if there's no IK data
        for (k = 0; k < 6; k++) {
            if (srcanim->localhierarchies[j].localData.numanim[k]) break;
        }

        if (k == 6)
            continue;

        // compressed
        const auto pCompressed = out.allocate<mstudiocompressedikerror_t>();
        pHierarchy->localanimindex = pCompressed.offset_from<int>(pHierarchy);

        for (k = 0; k < 6; k++) {
            pCompressed->scale[k] = srcanim->localhierarchies[j].localData.scale[k];
            const auto values = out.allocate<mstudioanimvalue_t>(srcanim->localhierarchies[j].localData.numanim[k]);
            pCompressed->offset[k] = values.offset_from<int>(pCompressed);
            memcpy(values.get(), srcanim->localhierarchies[j].localData.anim[k].data(),
                   srcanim->localhierarchies[j].localData.numanim[k] * sizeof(mstudioanimvalue_t));
        }

        out.align(4);

#endif
        // AddToStringTable( pHierarchy, &pHierarchy->szattachmentindex, srcanim->ikrule[j].attachment );
    }
}


// buffer: the .mdl data. blocks: the .ani data (already holding its header), nullptr when there is no .ani file.
// g_studiomdl.animblocks start/end are offsets into blocks.
static void WriteAnimations(vstd::Buffer &buffer, vstd::Buffer* blocks, const vstd::BufferPtr<studiohdr_t> phdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j;

    // save animations
    const auto panimdesc = buffer.allocate<mstudioanimdesc_t>(g_studiomdl.animations.size());
    phdr->numlocalanim = g_studiomdl.animations.size();
    phdr->localanimindex = panimdesc.offset_as<int>();
    buffer.align(4);

    // end of the committed .ani data (data past it still belongs to the animation being written)
    size_t blockData = blocks ? blocks->size() : 0;
    //      ------------ ------- ------- : ------- (-------)
    if (g_StudioMdlContext.verbose && g_studiomdl.animations.size() > 0) {
        std::print("   animation       x       y       ips    angle\n");
    }

    for (i = 0; i < g_studiomdl.animations.size(); i++) {
        s_animation_t* srcanim = g_studiomdl.animations[i];
        const auto destanim = panimdesc + i;
        Assert(srcanim);

        AddToStringTable(destanim, destanim.field(&mstudioanimdesc_t::sznameindex), srcanim->name);

        destanim->baseptr = phdr.offset_from<int>(destanim);
        destanim->fps = srcanim->fps;
        destanim->flags = srcanim->flags;

        destanim->sectionframes = srcanim->sectionframes;

        totalframes += srcanim->numframes;
        totalseconds += srcanim->numframes / srcanim->fps;

        destanim->numframes = srcanim->numframes;

        // destanim->motiontype = srcanim->motiontype;
        // destanim->motionbone = srcanim->motionbone;
        // VectorCopy( srcanim->linearpos, destanim->linearpos );
        if (g_StudioMdlContext.verbose && (srcanim->piecewisemove.size() > 0)) {
            j = srcanim->piecewisemove.size() - 1;
            if (srcanim->piecewisemove[j].pos[0] != 0 || srcanim->piecewisemove[j].pos[1] != 0) {
                float t = (srcanim->numframes - 1) / srcanim->fps;

                float r = 1 / t;

                float a = atan2(srcanim->piecewisemove[j].pos[1], srcanim->piecewisemove[j].pos[0]) * (180 / M_PI);
                float d = sqrt(DotProduct(srcanim->piecewisemove[j].pos, srcanim->piecewisemove[j].pos));
                std::print("{:>12} {:7.2f} {:7.2f} : {:7.2f} ({:7.2f}) {:.1f}\n", srcanim->name,
                           srcanim->piecewisemove[j].pos[0],
                           srcanim->piecewisemove[j].pos[1], d * r, a, t);
            }
        }

        if (srcanim->numsections > 1) {
            const auto psections = buffer.allocate<mstudioanimsections_t>(srcanim->numsections);
            destanim->sectionindex = psections.offset_from<int>(destanim);
        }

        // align all animation data to cache line boundaries
        buffer.align(16);
        if (blocks) {
            blocks->align(16);
            blockData = blocks->size();

            // allocate the first block if needed
            if (g_studiomdl.animblocks.size() == 0) {
                g_studiomdl.animblocks.emplace_back();
                g_studiomdl.animblocks.emplace_back(s_animblock_t{.start = static_cast<uint32_t>(blockData)});
            }
        }

        if (!blocks || (g_studiomdl.bonesaveframe.Count() == 0 && srcanim->numframes == 1)) {
            // hack
            srcanim->disableAnimblocks = true;
        }
        else if (g_StudioMdlContext.noAnimblockStall) {
            srcanim->isFirstSectionLocal = true;
        }

        // make sure number of preload frames is initialized
        if (srcanim->numNostallFrames == 0) {
            srcanim->numNostallFrames = srcanim->fps * g_StudioMdlContext.preloadTime;
        }

        // Offset of a position from its block: block zero is relative to me (animation data inside the .mdl),
        // the others to their start in the .ani data (slot 0 is a placeholder there)
        const auto blockRelative = [&](const int block, const size_t position) {
            const int64_t base = block == 0 ? destanim.offset() : g_studiomdl.animblocks[block].start;
            return static_cast<int>(static_cast<int64_t>(position) - base);
        };

        size_t animData;
        size_t ikData;
        size_t localHierarchy;

        if (srcanim->disableAnimblocks || srcanim->isFirstSectionLocal) {
            destanim->animblock = 0;
            animData = buffer.size();
            WriteAnimationData(srcanim, destanim, buffer, blocks);
            ikData = buffer.size();
            WriteIkErrors(srcanim, buffer);
            localHierarchy = buffer.size();
            WriteLocalHierarchy(srcanim, buffer);
        }
        else {
            animData = blocks->size();
            WriteAnimationData(srcanim, destanim, buffer, blocks);
            if (destanim->sectionindex) {
                // if sections were written, don't move the data already written to the last block
                blockData = blocks->size();
            }
            destanim->animblock = g_studiomdl.animblocks.size() - 1;
            ikData = blocks->size();
            WriteIkErrors(srcanim, *blocks);
            localHierarchy = blocks->size();
            WriteLocalHierarchy(srcanim, *blocks);
        }

        const size_t blockEnd = blocks ? blocks->size() : 0;
        if (blockData != blockEnd &&
            static_cast<int64_t>(blockEnd) - g_studiomdl.animblocks.back().start > g_studiomdl.animblocksize) {
            g_studiomdl.animblocks.back().end = blockData;
            g_studiomdl.animblocks.emplace_back(s_animblock_t{.start = static_cast<uint32_t>(blockData)});
            destanim->animblock = g_studiomdl.animblocks.size() - 1;
        }

        destanim->animindex = blockRelative(destanim->animblock, animData);

        if (srcanim->ikrules.size()) {
            destanim->numikrules = srcanim->ikrules.size();
            if (destanim->animblock == 0) {
                destanim->ikruleindex = blockRelative(destanim->animblock, ikData);
            }
            else {
                destanim->animblockikruleindex = blockRelative(destanim->animblock, ikData);
            }
        }
        if (srcanim->localhierarchies.size()) {
            destanim->numlocalhierarchy = srcanim->localhierarchies.size();
            destanim->localhierarchyindex = blockRelative(destanim->animblock, localHierarchy);
        }

        if (g_studiomdl.animblocks.size()) {
            g_studiomdl.animblocks.back().end = blockEnd;
            blockData = blockEnd;
        }
    }

    if (!g_StudioMdlContext.quiet) {
        /*
		for (i = 0; i < g_studiomdl.numanimblocks; i++)
		{
			printf("%2d (%3d:%3d): %d\n", i, g_studiomdl.animblock[i].iStartAnim, g_studiomdl.animblock[i].iEndAnim, g_studiomdl.animblock[i].end - g_studiomdl.animblock[i].start );
		}
		*/
    }

    if (!g_StudioMdlContext.quiet) {
        /*
		printf("raw anim data %d : %d\n", rawanimbytes, animboneframes );
		printf("pos  %d %d %d %d\n", numPos[0], numPos[1], numPos[2], numPos[3] );
		printf("axis %d %d %d %d : %d\n", numAxis[0], numAxis[1], numAxis[2], numAxis[3], useRaw );
		*/
    }

    // write movement keys
    for (i = 0; i < g_studiomdl.animations.size(); i++) {
        s_animation_t* anim = g_studiomdl.animations[i];

        panimdesc[i].nummovements = anim->piecewisemove.size();
        if (panimdesc[i].nummovements) {
            const auto pmove = buffer.allocate<mstudiomovement_t>(panimdesc[i].nummovements);
            panimdesc[i].movementindex = pmove.offset_from<int>(panimdesc + i);
            buffer.align(4);

            for (j = 0; j < panimdesc[i].nummovements; j++) {
                pmove[j].endframe = anim->piecewisemove[j].endframe;
                pmove[j].motionflags = anim->piecewisemove[j].flags;
                pmove[j].v0 = anim->piecewisemove[j].v0;
                pmove[j].v1 = anim->piecewisemove[j].v1;
                pmove[j].angle = RAD2DEG(anim->piecewisemove[j].rot[2]);
                VectorCopy(anim->piecewisemove[j].vector, pmove[j].vector);
                VectorCopy(anim->piecewisemove[j].pos, pmove[j].position);
            }
        }
    }

    // only write zero frames if the animation data is demand loaded
    if (!blocks)
        return;


    // calculate what bones should be have zero frame saved out
    if (g_studiomdl.bonesaveframe.Count() == 0) {
        for (j = 0; j < g_StudioMdlContext.numbones; j++) {
            if ((g_studiomdl.bonetable[j].parent == -1) ||
                (g_studiomdl.bonetable[j].posrange.Length() >= g_StudioMdlContext.minZeroFramePosDelta)) {
                g_studiomdl.bonetable[j].flags |= BoneFlags::HasSaveframePos;
            }
            if (g_StudioMdlContext.zeroFramesHighres) {
                g_studiomdl.bonetable[j].flags |= BoneFlags::HasSaveframeRot64;
            }
            else {
                g_studiomdl.bonetable[j].flags |= BoneFlags::HasSaveframeRot32;
            }


            if ((!g_StudioMdlContext.quiet) && HasFlag(g_studiomdl.bonetable[j].flags,
                                                       BoneFlags::HasSaveframePos | BoneFlags::HasSaveframeRot64 |
                                                       BoneFlags::HasSaveframeRot32)) {
                std::print("$BoneSaveFrame \"{}\"", g_studiomdl.bonetable[j].name);
                if (HasFlag(g_studiomdl.bonetable[j].flags, BoneFlags::HasSaveframePos)) {
                    std::print(" position");
                }
                if (HasFlag(g_studiomdl.bonetable[j].flags, BoneFlags::HasSaveframeRot64)) {
                    std::print(" rotation64");
                }
                else if (HasFlag(g_studiomdl.bonetable[j].flags, BoneFlags::HasSaveframeRot32)) {
                    std::print(" rotation");
                }
                if (!HasFlag(g_studiomdl.bonetable[j].flags, BoneFlags::HasSaveframePos) && g_studiomdl.bonetable[j].
                    posrange.
                    Length() > 0.1) {
                    std::print(" // ({:.2f})", g_studiomdl.bonetable[j].posrange.Length());
                }
                std::print("\n");
            }
        }
    }
    else {
        for (i = 0; i < g_studiomdl.bonesaveframe.Count(); i++) {
            j = findGlobalBone(g_studiomdl.bonesaveframe[i].name);

            if (j != -1) {
                if (g_studiomdl.bonesaveframe[i].bSavePos) {
                    g_studiomdl.bonetable[j].flags |= BoneFlags::HasSaveframePos;
                }
                if (g_studiomdl.bonesaveframe[i].bSaveRot) {
                    if (g_StudioMdlContext.zeroFramesHighres) {
                        g_studiomdl.bonetable[j].flags |= BoneFlags::HasSaveframeRot64;
                    }
                    else {
                        g_studiomdl.bonetable[j].flags |= BoneFlags::HasSaveframeRot32;
                    }
                }
                else if (g_studiomdl.bonesaveframe[i].bSaveRot64) {
                    g_studiomdl.bonetable[j].flags |= BoneFlags::HasSaveframeRot64;
                }
            }
            else {
                MdlError("Unknown $BoneSaveFrame \"{}\"\n", g_studiomdl.bonesaveframe[i].name);
            }
        }
    }

    for (j = 0; j < g_StudioMdlContext.numbones; j++) {
        ((mstudiobone_t*) phdr->pBone(j))->flags |= g_studiomdl.bonetable[j].flags;
    }

    buffer.align(4);

    // write zero frames
    for (i = 0; i < g_studiomdl.animations.size(); i++) {
        s_animation_t* anim = g_studiomdl.animations[i];
        const auto destanim = panimdesc + i;

        if (destanim->animblock != 0) {
            destanim->zeroframeindex = buffer.end_ptr().offset_from<int>(destanim);

            int k = MIN(destanim->numframes - 1, 9);
            if (HasFlag(destanim->flags, StudioSequenceFlags::Looping)) {
                k = MIN((destanim->numframes - 1) / 2, k);
            }
            destanim->zeroframespan = k;
            if (k > 2) {
                destanim->zeroframecount = MIN((destanim->numframes - 1) / destanim->zeroframespan,
                                               3); // save frames 0..24 frames
            }
            if (destanim->zeroframecount < 1)
                destanim->zeroframecount = 1;

            destanim->zeroframecount = MIN(destanim->zeroframecount, g_StudioMdlContext.maxZeroFrames);

            for (j = 0; j < g_StudioMdlContext.numbones; j++) {
                if (HasFlag(g_studiomdl.bonetable[j].flags, BoneFlags::HasSaveframePos)) {
                    for (int n = 0; n < destanim->zeroframecount; n++) {
                        *buffer.allocate<Vector48>() = anim->sanim[destanim->zeroframespan * n][j].pos;
                    }
                }
                if (HasFlag(g_studiomdl.bonetable[j].flags, BoneFlags::HasSaveframeRot64)) {
                    for (int n = 0; n < destanim->zeroframecount; n++) {
                        Quaternion q;
                        AngleQuaternion(anim->sanim[destanim->zeroframespan * n][j].rot, q);
                        *buffer.allocate<Quaternion64>() = q;
                    }
                }
                else if (HasFlag(g_studiomdl.bonetable[j].flags, BoneFlags::HasSaveframeRot32)) {
                    for (int n = 0; n < destanim->zeroframecount; n++) {
                        Quaternion q;
                        AngleQuaternion(anim->sanim[destanim->zeroframespan * n][j].rot, q);
                        *buffer.allocate<Quaternion32>() = q;
                    }
                }
            }
            buffer.align(4);

            // write zero frame IK data
            if (destanim->numikrules) {
                const auto pdestikrule = buffer.allocate<mstudioikrulezeroframe_t>(destanim->numikrules);
                destanim->ikrulezeroframeindex = pdestikrule.offset_from<int>(destanim);

                // the source rules: in the .mdl after this descriptor, or in its .ani block
                const mstudioikrule_t* psrcikrule = destanim->ikruleindex
                                                        ? reinterpret_cast<const mstudioikrule_t*>(
                                                            (uint8_t*) destanim.get() + destanim->ikruleindex)
                                                        : reinterpret_cast<const mstudioikrule_t*>(blocks->data() +
                                                            g_studiomdl.animblocks[destanim->animblock].start +
                                                            destanim->animblockikruleindex);

                for (j = 0; j < destanim->numikrules; j++) {
                    pdestikrule[j].slot = psrcikrule[j].slot;
                    pdestikrule[j].chain = psrcikrule[j].chain;
                    pdestikrule[j].start.SetFloat(psrcikrule[j].start);
                    pdestikrule[j].peak.SetFloat(psrcikrule[j].peak);
                    pdestikrule[j].tail.SetFloat(psrcikrule[j].tail);
                    pdestikrule[j].end.SetFloat(psrcikrule[j].end);
                }
            }
            buffer.align(4);
        }
    }

    buffer.align(4);
}


static void WriteTextures(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr_t> phdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j;

    // save texture info
    const auto ptexture = buffer.allocate<mstudiotexture_t>(g_studiomdl.materials.size());
    phdr->numtextures = g_studiomdl.materials.size();
    phdr->textureindex = ptexture.offset_as<int>();
    for (i = 0; i < g_studiomdl.materials.size(); i++) {
        j = g_studiomdl.materials[i];
        AddToStringTable(ptexture + i, (ptexture + i).field(&mstudiotexture_t::sznameindex),
                         g_studiomdl.textures[j].name);
    }
    buffer.align(4);

    const auto cdtextureoffset = buffer.allocate<int>(g_studiomdl.cdtextures.size());
    phdr->numcdtextures = g_studiomdl.cdtextures.size();
    phdr->cdtextureindex = cdtextureoffset.offset_as<int>();
    for (i = 0; i < g_studiomdl.cdtextures.size(); i++) {
        AddToStringTable(phdr, cdtextureoffset + i, g_studiomdl.cdtextures[i]);
    }
    buffer.align(4);

    // save texture directory info
    phdr->numskinref = g_studiomdl.numskinref;
    phdr->numskinfamilies = g_studiomdl.numskinfamilies;
    const auto pref = buffer.allocate<short>(phdr->numskinfamilies * phdr->numskinref);
    phdr->skinindex = pref.offset_as<int>();

    for (i = 0; i < phdr->numskinfamilies; i++) {
        for (j = 0; j < phdr->numskinref; j++) {
            pref[i * phdr->numskinref + j] = g_studiomdl.skinref[i][j];
        }
    }
    buffer.align(4);
}


//-----------------------------------------------------------------------------
// Write source bone transforms
//-----------------------------------------------------------------------------
// The bones are read from phdr's bone array; the counts and indices go to phdr2
static void WriteBoneTransforms(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr_t> phdr,
                                const vstd::BufferPtr<studiohdr2_t> phdr2) {
    vstd::FunctionTimingRecorder _("Write");
    const auto pBone = buffer.ptr_at<mstudiobone_t>(phdr->boneindex);
    matrix3x4_t identity;
    SetIdentityMatrix(identity);

    int nTransformCount = 0;
    for (int i = 0; i < g_StudioMdlContext.numbones; i++) {
        if (HasFlag(g_studiomdl.bonetable[i].flags, BoneFlags::AlwaysProcedural))
            continue;
        int nParent = g_studiomdl.bonetable[i].parent;

        // Transformation is necessary if either you or your parent was realigned
        if (MatricesAreEqual(identity, g_studiomdl.bonetable[i].srcRealign) &&
            ((nParent < 0) || MatricesAreEqual(identity, g_studiomdl.bonetable[nParent].srcRealign)))
            continue;

        ++nTransformCount;
    }

    // save bone transform info
    const auto pSrcBoneTransform = buffer.allocate<mstudiosrcbonetransform_t>(nTransformCount);
    phdr2->numsrcbonetransform = nTransformCount;
    phdr2->srcbonetransformindex = pSrcBoneTransform.offset_as<int>();
    int bt = 0;
    for (int i = 0; i < g_StudioMdlContext.numbones; i++) {
        if (HasFlag(g_studiomdl.bonetable[i].flags, BoneFlags::AlwaysProcedural))
            continue;
        int nParent = g_studiomdl.bonetable[i].parent;
        if (MatricesAreEqual(identity, g_studiomdl.bonetable[i].srcRealign) &&
            ((nParent < 0) || MatricesAreEqual(identity, g_studiomdl.bonetable[nParent].srcRealign)))
            continue;

        // What's going on here?
        // So, when we realign a bone, we want to do it in a way so that the child bones
        // have the same bone->world transform. If we take T as the src realignment transform
        // for the parent, P is the parent to world, and C is the child to parent, we expect
        // the child->world is constant after realignment:
        //		CtoW = P * C = ( P * T ) * ( T^-1 * C )
        // therefore Cnew = ( T^-1 * C )
        if (nParent >= 0) {
            MatrixInvert(g_studiomdl.bonetable[nParent].srcRealign, pSrcBoneTransform[bt].pretransform);
        }
        else {
            SetIdentityMatrix(pSrcBoneTransform[bt].pretransform);
        }
        MatrixCopy(g_studiomdl.bonetable[i].srcRealign, pSrcBoneTransform[bt].posttransform);
        AddToStringTable(pSrcBoneTransform + bt,
                         (pSrcBoneTransform + bt).field(&mstudiosrcbonetransform_t::sznameindex),
                         g_studiomdl.bonetable[i].name);
        ++bt;
    }
    buffer.align(4);

    if (g_StudioMdlContext.numbones > 1) {
        // write second bone table
        const auto pLinearBone = buffer.allocate<mstudiolinearbone_t>();
        phdr2->linearboneindex = pLinearBone.offset_from<int>(phdr2);

        pLinearBone->numbones = g_StudioMdlContext.numbones;

        // one array per bone field, each 4-aligned, indexed relative to pLinearBone
        const auto writeBoneBlock = [&]<typename F>(F mstudiobone_t::* srcfield, int mstudiolinearbone_t::* destindex) {
            const auto dest = buffer.template allocate<F>(g_StudioMdlContext.numbones);
            pLinearBone.get()->*destindex = dest.template offset_from<int>(pLinearBone);
            buffer.align(4);
            for (int i = 0; i < g_StudioMdlContext.numbones; i++)
                dest[i] = pBone[i].*srcfield;
        };

        writeBoneBlock(&mstudiobone_t::flags, &mstudiolinearbone_t::flagsindex);
        writeBoneBlock(&mstudiobone_t::parent, &mstudiolinearbone_t::parentindex);
        writeBoneBlock(&mstudiobone_t::pos, &mstudiolinearbone_t::posindex);
        writeBoneBlock(&mstudiobone_t::quat, &mstudiolinearbone_t::quatindex);
        writeBoneBlock(&mstudiobone_t::rot, &mstudiolinearbone_t::rotindex);
        writeBoneBlock(&mstudiobone_t::poseToBone, &mstudiolinearbone_t::posetoboneindex);
        writeBoneBlock(&mstudiobone_t::posscale, &mstudiolinearbone_t::posscaleindex);
        writeBoneBlock(&mstudiobone_t::rotscale, &mstudiolinearbone_t::rotscaleindex);
        writeBoneBlock(&mstudiobone_t::qAlignment, &mstudiolinearbone_t::qalignmentindex);
    }
}

static void WriteBodyGroupPresets(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr2_t> pStudioHdr2) {
    vstd::FunctionTimingRecorder _("Write");
    buffer.align(4);

    pStudioHdr2->m_nBodyGroupPresetCount = g_studiomdl.numbodygrouppresets;
    pStudioHdr2->m_nBodyGroupPresetIndex = 0;

    if (g_studiomdl.numbodygrouppresets <= 0)
        return;

    const auto pBodygroupPreset = buffer.allocate<mstudiobodygrouppreset_t>(g_studiomdl.numbodygrouppresets);
    pStudioHdr2->m_nBodyGroupPresetIndex = pBodygroupPreset.offset_from<int>(pStudioHdr2);
    buffer.align(4);

    for (int i = 0; i < g_studiomdl.numbodygrouppresets; i++) {
        AddToStringTable(pBodygroupPreset + i, (pBodygroupPreset + i).field(&mstudiobodygrouppreset_t::sznameindex),
                         g_studiomdl.bodygrouppresets[i].name);
        pBodygroupPreset[i].iValue = g_studiomdl.bodygrouppresets[i].iValue;
        pBodygroupPreset[i].iMask = g_studiomdl.bodygrouppresets[i].iMask;
    }
}

//-----------------------------------------------------------------------------
// Write the bone flex drivers
//-----------------------------------------------------------------------------
static void WriteBoneFlexDrivers(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr2_t> pStudioHdr2) {
    vstd::FunctionTimingRecorder _("Write");
    buffer.align(4);

    pStudioHdr2->m_nBoneFlexDriverCount = 0;
    pStudioHdr2->m_nBoneFlexDriverIndex = 0;

    CDmeBoneFlexDriverList* pDmeBoneFlexDriverList = GetElement<CDmeBoneFlexDriverList>(
        g_StudioMdlContext.hDmeBoneFlexDriverList);
    if (!pDmeBoneFlexDriverList)
        return;

    const int nBoneFlexDriverCount = pDmeBoneFlexDriverList->m_eBoneFlexDriverList.Count();
    if (nBoneFlexDriverCount <= 0)
        return;

    auto pBoneFlexDriver = buffer.allocate<mstudioboneflexdriver_t>(nBoneFlexDriverCount);
    pStudioHdr2->m_nBoneFlexDriverCount = nBoneFlexDriverCount;
    pStudioHdr2->m_nBoneFlexDriverIndex = pBoneFlexDriver.offset_from<int>(pStudioHdr2);
    buffer.align(4);

    for (int i = 0; i < nBoneFlexDriverCount; ++i) {
        CDmeBoneFlexDriver* pDmeBoneFlexDriver = pDmeBoneFlexDriverList->m_eBoneFlexDriverList[i];
        Assert(pDmeBoneFlexDriver);
        Assert(pDmeBoneFlexDriver->m_eControlList.Count() > 0);
        Assert(pDmeBoneFlexDriver->GetValue<int>("__boneIndex", -1) >= 0);

        pBoneFlexDriver->m_nBoneIndex = pDmeBoneFlexDriver->GetValue<int>("__boneIndex", 0);
        pBoneFlexDriver->m_nControlCount = pDmeBoneFlexDriver->m_eControlList.Count();

        const auto pControl = buffer.allocate<mstudioboneflexdrivercontrol_t>(pBoneFlexDriver->m_nControlCount);
        pBoneFlexDriver->m_nControlIndex = pControl.offset_from<int>(pBoneFlexDriver);

        for (int j = 0; j < pBoneFlexDriver->m_nControlCount; ++j) {
            CDmeBoneFlexDriverControl* pDmeControl = pDmeBoneFlexDriver->m_eControlList[j];
            Assert(pDmeControl);
            Assert(pDmeControl->GetValue<int>("__flexControlIndex", -1) >= 0);
            Assert(pDmeControl->m_nBoneComponent >= STUDIO_BONE_FLEX_TX);
            Assert(pDmeControl->m_nBoneComponent <= STUDIO_BONE_FLEX_TZ);

            pControl[j].m_nFlexControllerIndex = pDmeControl->GetValue<int>("__flexControlIndex", 0);
            pControl[j].m_nBoneComponent = pDmeControl->m_nBoneComponent;
            pControl[j].m_flMin = pDmeControl->m_flMin;
            pControl[j].m_flMax = pDmeControl->m_flMax;
        }

        buffer.align(4);

        ++pBoneFlexDriver;
    }
}


//-----------------------------------------------------------------------------
// Write the processed vertices
//-----------------------------------------------------------------------------
static void WriteVertices(const studiohdr_t* hdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i = 0;
    int j = 0;
    int k = 0;
    const bool extraData = (hdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) != 0;

    if (!g_studiomdl.nummodelsbeforeLOD)
        return;

    const std::string fileName = OutputModelPath(".vvd");

    if (!g_StudioMdlContext.quiet) {
        std::print("---------------------\n");
        std::print("writing {}:\n", fileName);
    }

    auto buffer = vstd::Buffer();


    const auto fileHeader = buffer.allocate<vertexFileHeader_t>();
    fileHeader->id = MODEL_VERTEX_FILE_ID;
    fileHeader->version = MODEL_VERTEX_FILE_VERSION;
    fileHeader->checksum = hdr->checksum;

    // data has no fixes and requires no fixes
    fileHeader->numFixups = 0;
    fileHeader->fixupTableStart = 0;

    // unfinalized during first pass, fixed during second pass
    // data can be considered as single lod at lod 0
    fileHeader->numLODs = 1;
    fileHeader->numLODVertexes[0] = 0;

    // store vertexes grouped by mesh order
    buffer.align(16);

    fileHeader->vertexDataStart = buffer.size();
    for (i = 0; i < g_studiomdl.nummodelsbeforeLOD; i++) {
        s_loddata_t* pLodData = g_studiomdl.model[i]->m_pLodData;

        // skip blank empty model
        if (!pLodData)
            continue;

        // save vertices
        buffer.align(16);
        // cur = (uintptr_t) pData;
        auto vertex = buffer.allocate<mstudiovertex_t>(pLodData->numvertices);
        for (j = 0; j < pLodData->numvertices; j++) {
            //			printf( "saving bone weight %d for model %d at 0x%p\n",
            //				j, i, &pbone[j] );

            const s_vertexinfo_t &lodVertex = pLodData->vertex[j];
            VectorCopy(lodVertex.position, vertex[j].m_vecPosition);
            VectorCopy(lodVertex.normal, vertex[j].m_vecNormal);
            Vector2DCopy(lodVertex.texcoord[0], vertex[j].m_vecTexCoord);

            mstudioboneweight_t* pBoneWeight = &vertex[j].m_BoneWeights;
            memset(pBoneWeight, 0, sizeof(mstudioboneweight_t));
            pBoneWeight->numbones = lodVertex.boneweight.numbones;
            for (k = 0; k < pBoneWeight->numbones; k++) {
                pBoneWeight->bone[k] = lodVertex.boneweight.bone[k];
                pBoneWeight->weight[k] = lodVertex.boneweight.weight[k];
            }
        }

        fileHeader->numLODVertexes[0] += pLodData->numvertices;

        if (!g_StudioMdlContext.quiet) {
            std::print("{} vertices \n", pLodData->numvertices);
        }
    }

    // store tangents grouped by mesh order
    buffer.align(4);
    fileHeader->tangentDataStart = buffer.size();
    for (i = 0; i < g_studiomdl.nummodelsbeforeLOD; i++) {
        s_loddata_t* pLodData = g_studiomdl.model[i]->m_pLodData;

        // skip blank empty model
        if (!pLodData)
            continue;

        // save tangent space S
        buffer.align(4);
        // cur = (uintptr_t) pData;
        auto ptangents = buffer.allocate<Vector4D>(pLodData->numvertices);
        for (j = 0; j < pLodData->numvertices; j++) {
            Vector4DCopy(pLodData->vertex[j].tangentS, ptangents[j]);
#ifdef _DEBUG
            float w = ptangents[j].w;
            Assert(w == 1.0f || w == -1.0f);
#endif
        }

        if (!g_StudioMdlContext.quiet) {
            std::print("{} tangents\n", pLodData->numvertices);
        }
    }

    if (extraData) {
        buffer.align(4);
        // cur = reinterpret_cast<uintptr_t>(pData);

        auto extraDataStart = buffer.end_ptr();
        auto pExtraheader = buffer.allocate<ExtraVertexAttributesHeader_t>();
        pExtraheader->m_count = sExtraTexcoordsToWrite;
        auto pIndex = buffer.allocate<ExtraVertexAttributeIndex_t>(sExtraTexcoordsToWrite);

        for (int e = 0; e < sExtraTexcoordsToWrite; ++e) {
            buffer.align(4);

            // Populate Index: type and byteoffset
            pIndex[e].m_type = static_cast<ExtraVertexAttributeType_t>(STUDIO_EXTRA_ATTRIBUTE_TEXCOORD0 + e + 1);
            pIndex[e].m_offset = buffer.end_ptr().offset_from<int>(extraDataStart);
            pIndex[e].m_bytes = 2 * sizeof(float);

            // store extra vertex data, one entry per vertex, order matches main vertex data
            for (i = 0; i < g_studiomdl.nummodelsbeforeLOD; i++) {
                s_loddata_t* pLodData = g_studiomdl.model[i]->m_pLodData;

                // skip blank empty model
                if (!pLodData)
                    continue;

                // save extra texcoord
                // cur = (uintptr_t) pData;
                auto pExtraTexcoord = buffer.allocate<float>(pLodData->numvertices * 2);
                for (j = 0; j < pLodData->numvertices; j++) {
                    const s_vertexinfo_t &lodVertex = pLodData->vertex[j];

                    pExtraTexcoord[j * 2 + 0] = lodVertex.texcoord[e + 1].x;
                    pExtraTexcoord[j * 2 + 1] = lodVertex.texcoord[e + 1].y;
                }

                if (!g_StudioMdlContext.quiet) {
                    std::print("extra vertex data   {:7} bytes ({} vertices)\n",
                               buffer.end_ptr().offset_from<int>(pExtraTexcoord),
                               pLodData->numvertices);
                }
            }
        }
        pExtraheader->m_totalbytes = buffer.end_ptr().offset_from<int>(extraDataStart);
    }

    if (!g_StudioMdlContext.quiet) {
        std::print("total      {:7} bytes\n", buffer.size());
    }

    // fileHeader->length = pData - pStart;
    SaveOutputFile(fileName, buffer.data(), buffer.size());
}


//-----------------------------------------------------------------------------
// Computes the maximum absolute value of any component of all vertex animation
// pos (x,y,z) normal (x,y,z) or wrinkle
//
// Returns the fixed point scale and also sets appropriate values & flags in
// passed studiohdr_t
//-----------------------------------------------------------------------------
float ComputeVertAnimFixedPointScale(studiohdr_t* pStudioHdr) {
    float flVertAnimRange = 0.0f;

    for (int j = 0; j < g_studiomdl.flexkeys.size(); ++j) {
        if (g_studiomdl.flexkeys[j].numvanims <= 0)
            continue;

        const bool bWrinkleVAnim = (g_studiomdl.flexkeys[j].vanimtype == Wrinkle);

        s_vertanim_t* pVertAnim = g_studiomdl.flexkeys[j].vanim;

        for (int k = 0; k < g_studiomdl.flexkeys[j].numvanims; ++k) {
            if (fabs(pVertAnim->pos.x) > flVertAnimRange) {
                flVertAnimRange = fabs(pVertAnim->pos.x);
            }

            if (fabs(pVertAnim->pos.y) > flVertAnimRange) {
                flVertAnimRange = fabs(pVertAnim->pos.y);
            }

            if (fabs(pVertAnim->pos.z) > flVertAnimRange) {
                flVertAnimRange = fabs(pVertAnim->pos.z);
            }

            if (fabs(pVertAnim->normal.x) > flVertAnimRange) {
                flVertAnimRange = fabs(pVertAnim->normal.x);
            }

            if (fabs(pVertAnim->normal.y) > flVertAnimRange) {
                flVertAnimRange = fabs(pVertAnim->normal.y);
            }

            if (fabs(pVertAnim->normal.z) > flVertAnimRange) {
                flVertAnimRange = fabs(pVertAnim->normal.z);
            }

            if (bWrinkleVAnim) {
                if (fabs(pVertAnim->wrinkle) > flVertAnimRange) {
                    flVertAnimRange = fabs(pVertAnim->wrinkle);
                }
            }

            pVertAnim++;
        }
    }

    // Legacy value
    float flVertAnimFixedPointScale = 1.0 / 4096.0f;

    if (flVertAnimRange > 0.0f) {
        if (flVertAnimRange > 32767) {
            MdlWarning("Flex value too large: {:.2f}, Max: 32767\n", flVertAnimRange);

            flVertAnimFixedPointScale = 1.0f;
        }
        else {
            const float flTmpScale = flVertAnimRange / 32767.0f;
            if (flTmpScale > flVertAnimFixedPointScale) {
                flVertAnimFixedPointScale = flTmpScale;
            }
        }
    }

    if (flVertAnimFixedPointScale != 1.0f / 4096.0f) {
        pStudioHdr->flags |= STUDIOHDR_FLAGS_VERT_ANIM_FIXED_POINT_SCALE;
        pStudioHdr->flVertAnimFixedPointScale = flVertAnimFixedPointScale;
    }

    return flVertAnimFixedPointScale;
}


static void WriteModel(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr_t> phdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j, k, m;
    s_source_t* psource;
    s_vertanim_t* pvanim;

    // vertex data is written to external file, offsets kept internal
    // track expected external base to store proper offsets
    uintptr_t externalVertexIndex = 0;
    uintptr_t externalTangentsIndex = 0;

    // write bodypart info
    const auto pbodypart = buffer.allocate<mstudiobodyparts_t>(g_studiomdl.numbodyparts);
    phdr->numbodyparts = g_studiomdl.numbodyparts;
    phdr->bodypartindex = pbodypart.offset_as<int>();

    const auto pmodel = buffer.allocate<mstudiomodel_t>(g_studiomdl.nummodelsbeforeLOD);

    for (i = 0, j = 0; i < g_studiomdl.numbodyparts; i++) {
        AddToStringTable(pbodypart + i, (pbodypart + i).field(&mstudiobodyparts_t::sznameindex),
                         g_studiomdl.bodypart[i].name);
        pbodypart[i].nummodels = g_studiomdl.bodypart[i].nummodels;
        pbodypart[i].base = g_studiomdl.bodypart[i].base;
        pbodypart[i].modelindex = (pmodel + j).offset_from<int>(pbodypart + i);
        j += g_studiomdl.bodypart[i].nummodels;
    }
    buffer.align(4);

    // write global flex names
    auto pflexdesc = buffer.allocate<mstudioflexdesc_t>(g_studiomdl.flexdescs.size());
    phdr->numflexdesc = g_studiomdl.flexdescs.size();
    phdr->flexdescindex = pflexdesc.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < g_studiomdl.flexdescs.size(); j++) {
        AddToStringTable(pflexdesc, pflexdesc.field(&mstudioflexdesc_t::szFACSindex), g_studiomdl.flexdescs[j].FACS);
        pflexdesc++;
    }

    // write global flex controllers
    const auto pflexcontrollers = buffer.allocate<mstudioflexcontroller_t>(g_studiomdl.flexcontrollers.size());
    phdr->numflexcontrollers = g_studiomdl.flexcontrollers.size();
    phdr->flexcontrollerindex = pflexcontrollers.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < g_studiomdl.flexcontrollers.size(); j++) {
        const auto pflexcontroller = pflexcontrollers + j;
        AddToStringTable(pflexcontroller, pflexcontroller.field(&mstudioflexcontroller_t::sznameindex),
                         g_studiomdl.flexcontrollers[j].name);
        AddToStringTable(pflexcontroller, pflexcontroller.field(&mstudioflexcontroller_t::sztypeindex),
                         g_studiomdl.flexcontrollers[j].type);
        pflexcontroller->min = g_studiomdl.flexcontrollers[j].min;
        pflexcontroller->max = g_studiomdl.flexcontrollers[j].max;
        pflexcontroller->localToGlobal = -1;
    }

    // write flex rules
    auto pflexrule = buffer.allocate<mstudioflexrule_t>(g_studiomdl.flexrules.size());
    phdr->numflexrules = g_studiomdl.flexrules.size();
    phdr->flexruleindex = pflexrule.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < g_studiomdl.flexrules.size(); j++) {
        pflexrule->flex = g_studiomdl.flexrules[j].flex;
        pflexrule->numops = g_studiomdl.flexrules[j].numops;

        const auto pflexop = buffer.allocate<mstudioflexop_t>(pflexrule->numops);
        pflexrule->opindex = pflexop.offset_from<int>(pflexrule);

        for (i = 0; i < pflexrule->numops; i++) {
            pflexop[i].op = g_studiomdl.flexrules[j].op[i].op;
            pflexop[i].d.index = g_studiomdl.flexrules[j].op[i].d.index;
        }

        buffer.align(4);

        pflexrule++;
    }

    // write global flex controller information
    phdr->numflexcontrollerui = 0;
    phdr->flexcontrolleruiindex = buffer.end_ptr().offset_as<int>();

    // Loop through all defined controllers and create a UI structure for them
    // All actual controllers will be defined as a member of some ui structure
    // and all actual controllers can only be a member of one ui structure
    std::vector<bool> pControllerHandled(g_studiomdl.flexcontrollers.size(), false);

    for (j = 0; j < g_studiomdl.flexcontrollers.size(); ++j) {
        // Don't handle controls twice
        if (pControllerHandled[j])
            continue;

        const s_flexcontroller_t &flexcontroller = g_studiomdl.flexcontrollers[j];

        const auto pFlexControllerUI = buffer.allocate<mstudioflexcontrollerui_t>();
        const auto uiName = pFlexControllerUI.field(&mstudioflexcontrollerui_t::sznameindex);
        // offset from this UI entry to flex controller index
        const auto controllerOffset = [&](const int index) {
            return (pflexcontrollers + index).offset_from<int>(pFlexControllerUI);
        };

        bool found = false;

        // See if this controller is in the remap table
        for (k = 0; k < g_StudioMdlContext.FlexControllerRemap.size(); ++k) {
            s_flexcontrollerremap_t &remap = g_StudioMdlContext.FlexControllerRemap[k];
            if (j == remap.m_Index || j == remap.m_LeftIndex || j == remap.m_RightIndex || j == remap.m_MultiIndex) {
                AddToStringTable(pFlexControllerUI, uiName, remap.m_Name);

                pFlexControllerUI->stereo = remap.m_bIsStereo;
                if (pFlexControllerUI->stereo) {
                    Assert(!pControllerHandled[remap.m_LeftIndex]);
                    pFlexControllerUI->szindex0 = controllerOffset(remap.m_LeftIndex);
                    pControllerHandled[remap.m_LeftIndex] = true;

                    Assert(!pControllerHandled[remap.m_RightIndex]);
                    pFlexControllerUI->szindex1 = controllerOffset(remap.m_RightIndex);
                    pControllerHandled[remap.m_RightIndex] = true;
                }
                else {
                    Assert(!pControllerHandled[remap.m_Index]);
                    pFlexControllerUI->szindex0 = controllerOffset(remap.m_Index);
                    pControllerHandled[remap.m_Index] = true;
                    pFlexControllerUI->szindex1 = 0;
                }

                pFlexControllerUI->remaptype = remap.m_RemapType;
                if (pFlexControllerUI->remaptype == Nway ||
                    pFlexControllerUI->remaptype == Eyelid) {
                    Assert(remap.m_MultiIndex != -1);
                    Assert(!pControllerHandled[remap.m_MultiIndex]);
                    pFlexControllerUI->szindex2 = controllerOffset(remap.m_MultiIndex);
                    pControllerHandled[remap.m_MultiIndex] = true;
                }
                else {
                    pFlexControllerUI->szindex2 = 0;
                }

                found = true;
                break;
            }
        }

        if (!found) {
            pFlexControllerUI->remaptype = Passthru;
            pFlexControllerUI->szindex2 = 0; // Unused in this case

            if (j < g_studiomdl.flexcontrollers.size() - 1 &&
                StringAfterPrefixCaseSensitive(flexcontroller.name, "right_") &&
                StringAfterPrefixCaseSensitive(g_studiomdl.flexcontrollers[j + 1].name, "left_") &&
                !Q_strcmp(StringAfterPrefixCaseSensitive(flexcontroller.name, "right_"),
                          StringAfterPrefixCaseSensitive(g_studiomdl.flexcontrollers[j + 1].name, "left_"))) {
                AddToStringTable(pFlexControllerUI, uiName, flexcontroller.name + 6);

                pFlexControllerUI->stereo = true;

                Assert(!pControllerHandled[j + 1]);
                pFlexControllerUI->szindex0 = controllerOffset(j + 1);
                pControllerHandled[j + 1] = true;

                Assert(!pControllerHandled[j]);
                pFlexControllerUI->szindex1 = controllerOffset(j);
                pControllerHandled[j] = true;
            }
            else if (j > 0 &&
                     StringAfterPrefixCaseSensitive(flexcontroller.name, "left_") &&
                     StringAfterPrefixCaseSensitive(g_studiomdl.flexcontrollers[j - 1].name, "right_") &&
                     !Q_strcmp(StringAfterPrefixCaseSensitive(flexcontroller.name, "left_"),
                               StringAfterPrefixCaseSensitive(g_studiomdl.flexcontrollers[j - 1].name, "right_"))) {
                AddToStringTable(pFlexControllerUI, uiName, flexcontroller.name + 5);

                pFlexControllerUI->stereo = true;

                Assert(!pControllerHandled[j]);
                pFlexControllerUI->szindex0 = controllerOffset(j);
                pControllerHandled[j] = true;

                Assert(!pControllerHandled[j - 1]);
                pFlexControllerUI->szindex1 = controllerOffset(j - 1);
                pControllerHandled[j - 1] = true;
            }
            else {
                AddToStringTable(pFlexControllerUI, uiName, flexcontroller.name);
                pFlexControllerUI->stereo = false;
                pFlexControllerUI->szindex0 = controllerOffset(j);
                pFlexControllerUI->szindex1 = 0; // Unused in this case
                pControllerHandled[j] = true;
            }
        }

        phdr->numflexcontrollerui++;
    }
    buffer.align(4);

#ifdef _DEBUG
    for (j = 0; j < g_studiomdl.flexcontrollers.size(); ++j) {
        Assert(pControllerHandled[j]);
    }
#endif // _DEBUG

    // write ik chains
    auto pikchain = buffer.allocate<mstudioikchain_t>(g_studiomdl.numikchains);
    phdr->numikchains = g_studiomdl.numikchains;
    phdr->ikchainindex = pikchain.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < g_studiomdl.numikchains; j++) {
        AddToStringTable(pikchain, pikchain.field(&mstudioikchain_t::sznameindex), g_studiomdl.ikchain[j].name);
        pikchain->numlinks = g_studiomdl.ikchain[j].numlinks;

        const auto piklink = buffer.allocate<mstudioiklink_t>(pikchain->numlinks);
        pikchain->linkindex = piklink.offset_from<int>(pikchain);

        for (i = 0; i < pikchain->numlinks; i++) {
            piklink[i].bone = g_studiomdl.ikchain[j].link[i].bone;
            piklink[i].kneeDir = g_studiomdl.ikchain[j].link[i].kneeDir;
        }

        pikchain++;
    }

    // save autoplay locks
    const auto piklock = buffer.allocate<mstudioiklock_t>(g_studiomdl.numikautoplaylocks);
    phdr->numlocalikautoplaylocks = g_studiomdl.numikautoplaylocks;
    phdr->localikautoplaylockindex = piklock.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < g_studiomdl.numikautoplaylocks; j++) {
        piklock[j].chain = g_studiomdl.ikautoplaylock[j].chain;
        piklock[j].flPosWeight = g_studiomdl.ikautoplaylock[j].flPosWeight;
        piklock[j].flLocalQWeight = g_studiomdl.ikautoplaylock[j].flLocalQWeight;
    }

    // save mouth info
    const auto pmouth = buffer.allocate<mstudiomouth_t>(g_studiomdl.nummouths);
    phdr->nummouths = g_studiomdl.nummouths;
    phdr->mouthindex = pmouth.offset_as<int>();
    buffer.align(4);

    for (i = 0; i < g_studiomdl.nummouths; i++) {
        pmouth[i].bone = g_studiomdl.mouth[i].bone;
        VectorCopy(g_studiomdl.mouth[i].forward, pmouth[i].forward);
        pmouth[i].flexdesc = g_studiomdl.mouth[i].flexdesc;
    }

    // save pose parameters
    const auto ppose = buffer.allocate<mstudioposeparamdesc_t>(g_studiomdl.numposeparameters);
    phdr->numlocalposeparameters = g_studiomdl.numposeparameters;
    phdr->localposeparamindex = ppose.offset_as<int>();
    buffer.align(4);

    for (i = 0; i < g_studiomdl.numposeparameters; i++) {
        AddToStringTable(ppose + i, (ppose + i).field(&mstudioposeparamdesc_t::sznameindex), g_studiomdl.pose[i].name);
        ppose[i].start = g_studiomdl.pose[i].min;
        ppose[i].end = g_studiomdl.pose[i].max;
        ppose[i].flags = g_studiomdl.pose[i].flags;
        ppose[i].loop = g_studiomdl.pose[i].loop;
    }

    if (!g_StudioMdlContext.quiet) {
        std::print("ik/pose    {:7} bytes\n", buffer.size());
    }

    const float flVertAnimFixedPointScale = ComputeVertAnimFixedPointScale(phdr.get());

    // Check all source models for extra texcoords
    // If any exist, add model flag to indicate that extra vertex data will be appended to the VVD file
    bool bExtraVertexData = false;
    sExtraTexcoordsToWrite = 0;
    for (int i = 0; (i < phdr->numbodyparts) && !bExtraVertexData; ++i) {
        for (int j = 0; j < g_studiomdl.bodypart[i].nummodels; ++j) {
            if (g_studiomdl.bodypart[i].pmodel[j] && g_studiomdl.bodypart[i].pmodel[j]->source->numvertices > 0) {
                if (g_studiomdl.bodypart[i].pmodel[j] && g_studiomdl.bodypart[i].pmodel[j]->source->vertex[0].
                    texcoord.size() > 1) {
                    bExtraVertexData = true;
                    sExtraTexcoordsToWrite = g_studiomdl.bodypart[i].pmodel[j]->source->vertex[0].texcoord.size() - 1;
                    break;
                }
            }
        }
    }

    if (bExtraVertexData) {
        phdr->flags |= STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA;
    }

    // write model
    for (i = 0; i < g_studiomdl.nummodelsbeforeLOD; i++) {
        int n = 0;

        const auto pModel = pmodel + i;

        Q_strncpy(pModel->name, g_studiomdl.model[i]->filename.c_str(), sizeof(pModel->name));

        psource = g_studiomdl.model[i]->source;
        s_loddata_t* pLodData = g_studiomdl.model[i]->m_pLodData;

        // save mesh info
        if (pLodData) {
            pModel->numvertices = pLodData->numvertices;
        }
        else {
            // empty model
            pModel->numvertices = 0;
        }

        if (pModel->numvertices >= MAXSTUDIOVERTS) {
            // We have to check this here so that we don't screw up decal
            // vert caching in the runtime.
            MdlError("Too many verts in model. ({} verts, MAXSTUDIOVERTS=={})\n",
                     pModel->numvertices, (int) MAXSTUDIOVERTS);
        }

        const auto pmesh = buffer.allocate<mstudiomesh_t>(psource->meshes.size());
        pModel->meshindex = pmesh.offset_from<int>(pModel);
        buffer.align(4);

        pModel->nummeshes = psource->meshes.size();
        for (m = 0; m < pModel->nummeshes; m++) {
            n = psource->meshes[m].material; // pLodData->mesh is indexed by material id

            pmesh[m].material = n;
            pmesh[m].modelindex = pModel.offset_from<int>(pmesh + m);
            pmesh[m].numvertices = pLodData->mesh[n].numvertices;
            pmesh[m].vertexoffset = pLodData->mesh[n].vertexoffset;
        }

        // set expected base offsets to external data
        externalVertexIndex = (externalVertexIndex + 15) & ~uintptr_t(15);
        pModel->vertexindex = externalVertexIndex;
        externalVertexIndex += pModel->numvertices * sizeof(mstudiovertex_t);

        // set expected base offsets to external data
        externalTangentsIndex = (externalTangentsIndex + 3) & ~uintptr_t(3);
        pModel->tangentsindex = externalTangentsIndex;
        externalTangentsIndex += pModel->numvertices * sizeof(Vector4D);

        // save eyeballs
        const auto peyeball = buffer.allocate<mstudioeyeball_t>(g_studiomdl.model[i]->eyeballs.size());
        pModel->numeyeballs = g_studiomdl.model[i]->eyeballs.size();
        pModel->eyeballindex = peyeball.offset_from<int>(pModel);

        buffer.align(4);
        for (j = 0; j < g_studiomdl.model[i]->eyeballs.size(); j++) {
            k = g_studiomdl.model[i]->eyeballs[j].mesh;
            pmesh[k].materialtype = 1; // FIXME: tag custom material
            pmesh[k].materialparam = j; // FIXME: tag custom material

            peyeball[j].bone = g_studiomdl.model[i]->eyeballs[j].bone;
            VectorCopy(g_studiomdl.model[i]->eyeballs[j].org, peyeball[j].org);
            peyeball[j].zoffset = g_studiomdl.model[i]->eyeballs[j].zoffset;
            peyeball[j].radius = g_studiomdl.model[i]->eyeballs[j].radius;
            VectorCopy(g_studiomdl.model[i]->eyeballs[j].up, peyeball[j].up);
            VectorCopy(g_studiomdl.model[i]->eyeballs[j].forward, peyeball[j].forward);
            peyeball[j].iris_scale = g_studiomdl.model[i]->eyeballs[j].iris_scale;

            for (k = 0; k < 3; k++) {
                peyeball[j].upperflexdesc[k] = g_studiomdl.model[i]->eyeballs[j].upperflexdesc[k];
                peyeball[j].lowerflexdesc[k] = g_studiomdl.model[i]->eyeballs[j].lowerflexdesc[k];
                peyeball[j].uppertarget[k] = g_studiomdl.model[i]->eyeballs[j].uppertarget[k];
                peyeball[j].lowertarget[k] = g_studiomdl.model[i]->eyeballs[j].lowertarget[k];
            }

            peyeball[j].upperlidflexdesc = g_studiomdl.model[i]->eyeballs[j].upperlidflexdesc;
            peyeball[j].lowerlidflexdesc = g_studiomdl.model[i]->eyeballs[j].lowerlidflexdesc;
        }

        if (!g_StudioMdlContext.quiet) {
            std::print("eyeballs   {:7} bytes ({} eyeballs)\n", buffer.size(), g_studiomdl.model[i]->eyeballs.size());
        }

        // move flexes into individual meshes
        for (m = 0; m < pModel->nummeshes; m++) {
            int numflexkeys[MAXSTUDIOFLEXKEYS];
            pmesh[m].numflexes = 0;

            // initialize array
            for (j = 0; j < g_studiomdl.flexkeys.size(); j++) {
                numflexkeys[j] = 0;
            }

            // count flex instances per mesh
            for (j = 0; j < g_studiomdl.flexkeys.size(); j++) {
                if (g_studiomdl.flexkeys[j].imodel == i) {
                    for (k = 0; k < g_studiomdl.flexkeys[j].numvanims; k++) {
                        n = g_studiomdl.flexkeys[j].vanim[k].vertex - pmesh[m].vertexoffset;
                        if (n >= 0 && n < pmesh[m].numvertices) {
                            if (numflexkeys[j]++ == 0) {
                                pmesh[m].numflexes++;
                            }
                        }
                    }
                }
            }

            if (pmesh[m].numflexes) {
                auto pflex = buffer.allocate<mstudioflex_t>(pmesh[m].numflexes);
                pmesh[m].flexindex = pflex.offset_from<int>(pmesh + m);
                buffer.align(4);

                for (j = 0; j < g_studiomdl.flexkeys.size(); j++) {
                    if (!numflexkeys[j])
                        continue;

                    pflex->flexdesc = g_studiomdl.flexkeys[j].flexdesc;
                    pflex->target0 = g_studiomdl.flexkeys[j].target0;
                    pflex->target1 = g_studiomdl.flexkeys[j].target1;
                    pflex->target2 = g_studiomdl.flexkeys[j].target2;
                    pflex->target3 = g_studiomdl.flexkeys[j].target3;
                    pflex->numverts = numflexkeys[j];
                    pflex->flexpair = g_studiomdl.flexkeys[j].flexpair;
                    pflex->vertanimtype = g_studiomdl.flexkeys[j].vanimtype;

                    pvanim = g_studiomdl.flexkeys[j].vanim;

                    const bool bWrinkleVAnim = (pflex->vertanimtype == Wrinkle);
                    const int nVAnimDeltaSize = bWrinkleVAnim
                                                    ? sizeof(mstudiovertanim_wrinkle_t)
                                                    : sizeof(mstudiovertanim_t);

                    // byte pointer: the delta size depends on the type
                    auto pvertanim = buffer.allocate<uint8_t>(pflex->numverts * nVAnimDeltaSize);
                    pflex->vertindex = pvertanim.offset_from<int>(pflex);
                    buffer.align(4);

                    for (k = 0; k < g_studiomdl.flexkeys[j].numvanims; k++) {
                        n = g_studiomdl.flexkeys[j].vanim[k].vertex - pmesh[m].vertexoffset;
                        if (n >= 0 && n < pmesh[m].numvertices) {
                            auto* pdelta = reinterpret_cast<mstudiovertanim_t*>(pvertanim.get());
                            pdelta->index = n;
                            pdelta->speed = 255.0F * pvanim->speed;
                            pdelta->side = 255.0F * pvanim->side;

                            pdelta->SetDeltaFloat(pvanim->pos);
                            pdelta->SetNDeltaFloat(pvanim->normal);

                            if (bWrinkleVAnim) {
                                reinterpret_cast<mstudiovertanim_wrinkle_t*>(pdelta)->SetWrinkleFixed(
                                    pvanim->wrinkle, flVertAnimFixedPointScale);
                            }

                            pvertanim += nVAnimDeltaSize;
                        }
                        pvanim++;
                    }
                    pflex++;
                }
            }
        }

        if (!g_StudioMdlContext.quiet) {
            std::print("flexes     {:7} bytes ({} flexes)\n", buffer.size(), g_studiomdl.flexkeys.size());
        }
    }


    buffer.align(4);

    auto pincludemodel = buffer.allocate<mstudiomodelgroup_t>(g_studiomdl.includemodels.size());
    phdr->numincludemodels = g_studiomdl.includemodels.size();
    phdr->includemodelindex = pincludemodel.offset_as<int>();

    for (i = 0; i < g_studiomdl.includemodels.size(); i++) {
        AddToStringTable(pincludemodel, pincludemodel.field(&mstudiomodelgroup_t::sznameindex),
                         g_studiomdl.includemodels[i].name);
        pincludemodel++;
    }

    // save animblock group info (block offsets are already relative to the .ani data)
    const auto panimblock = buffer.allocate<mstudioanimblock_t>(g_studiomdl.animblocks.size());
    phdr->numanimblocks = g_studiomdl.animblocks.size();
    phdr->animblockindex = panimblock.offset_as<int>();
    buffer.align(4);

    for (i = 1; i < g_studiomdl.animblocks.size(); i++) {
        panimblock[i].datastart = g_studiomdl.animblocks[i].start;
        panimblock[i].dataend = g_studiomdl.animblocks[i].end;
    }
    AddToStringTable(phdr, phdr.field(&studiohdr_t::szanimblocknameindex), g_studiomdl.animblockname);
}

static void AssignMeshIDs(studiohdr_t* pStudioHdr) {
    vstd::FunctionTimingRecorder _("Write");

    int numMeshes = 0;
    for (int i = 0; i < pStudioHdr->numbodyparts; i++) {
        mstudiobodyparts_t* pStudioBodyPart = pStudioHdr->pBodypart(i);
        for (int j = 0; j < pStudioBodyPart->nummodels; j++) {
            mstudiomodel_t* pStudioModel = pStudioBodyPart->pModel(j);
            for (int m = 0; m < pStudioModel->nummeshes; m++) {
                // get each mesh
                mstudiomesh_t* pStudioMesh = pStudioModel->pMesh(m);
                pStudioMesh->meshid = numMeshes + m;
            }
            numMeshes += pStudioModel->nummeshes;
        }
    }
}


void LoadMaterials(studiohdr_t* phdr) {
    return;
    /*
	int					i, j;

	// get index of each material
	if( phdr->textureindex != 0 )
	{
		for( i = 0; i < phdr->numtextures; i++ )
		{
			char szPath[MAX_PATH];
//			IMaterial *pMaterial = nullptr;
			// search through all specified directories until a valid material is found
			for( j = 0; j < phdr->numcdtextures && IsErrorMaterial( pMaterial ); j++ )
			{
				strcpy( szPath, phdr->pCdtexture( j ) );
				strcat( szPath, phdr->pTexture( i )->pszName( ) );

				pMaterial = g_pMaterialSystem->FindMaterial( szPath, TEXTURE_GROUP_OTHER, false );
			}
			if( IsErrorMaterial( pMaterial ) && !g_StudioMdlContext.quiet )
			{
				// hack - if it isn't found, go through the motions of looking for it again
				// so that the materialsystem will give an error.
				for( j = 0; j < phdr->numcdtextures; j++ )
				{
					strcpy( szPath, phdr->pCdtexture( j ) );
					strcat( szPath, phdr->pTexture( i )->pszName( ) );
					g_pMaterialSystem->FindMaterial( szPath, TEXTURE_GROUP_OTHER, true );
				}
			}

			phdr->pTexture( i )->material = pMaterial;

			// FIXME: hack, needs proper client side material system interface
			bool found = false;
			IMaterialVar *clientShaderVar = pMaterial->FindVar( "$clientShader", &found, false );
			if( found )
			{
				if (stricmp( clientShaderVar->GetStringValue(), "MouthShader") == 0)
				{
					phdr->pTexture( i )->flags = 1;
				}
				phdr->pTexture( i )->used = 1;
			}
		}
	}*/
}


// Appends the key values text with a null terminator (nothing when empty); returns where it starts
static vstd::BufferPtr<char> WriteKeyValueText(vstd::Buffer &buffer, std::string &pKeyValue) {
    const int size = KeyValueTextSize(pKeyValue);
    const auto text = buffer.allocate<char>(size ? size + 1 : 0);
    if (size) {
        memcpy(text.get(), KeyValueText(pKeyValue), size); // allocate zero-filled the terminator
    }
    return text;
}

void WriteKeyValues(vstd::Buffer &buffer, const vstd::BufferPtr<studiohdr_t> phdr, std::string &pKeyValue) {
    vstd::FunctionTimingRecorder _("Write");
    const auto text = WriteKeyValueText(buffer, pKeyValue);
    phdr->keyvalueindex = text.offset_as<int>();
    phdr->keyvaluesize = KeyValueTextSize(pKeyValue) ? KeyValueTextSize(pKeyValue) + 1 : 0;
    buffer.align(4);
}


void CapKeyValues() {
    const char* headCap = "mdlkeyvalue\n{\n";
    const char* tailCap = "}\\n";
    if (g_StudioMdlContext.KeyValueText.size()) {
        g_StudioMdlContext.KeyValueText.insert(g_StudioMdlContext.KeyValueText.begin(), headCap,
                                               headCap + std::strlen(headCap));
        g_StudioMdlContext.KeyValueText.insert(g_StudioMdlContext.KeyValueText.end(), tailCap,
                                               tailCap + std::strlen(tailCap));
    }
}

void WriteQCPath() {
    char relativeQCDir[4096] = {};
    g_pFullFileSystem->FullPathToRelativePathEx(g_StudioMdlContext.qdir.c_str(), "CONTENT", relativeQCDir,
                                                sizeof(relativeQCDir));
    const std::string relativeQCPath = std::string(relativeQCDir) + std::string(
                                           vstd::paths::file_name(g_studiomdl.fullpath));

    if (!relativeQCPath.empty()) {
        const std::string block = "qc_path {\n\"value\" \"" + relativeQCPath + "\" }\n";
        g_StudioMdlContext.KeyValueText.insert(g_StudioMdlContext.KeyValueText.end(), block.begin(), block.end());
    }
}

void WriteSeqKeyValues(vstd::Buffer &buffer, const vstd::BufferPtr<mstudioseqdesc_t> pseqdesc,
                       std::string &pKeyValue) {
    const auto text = WriteKeyValueText(buffer, pKeyValue);
    pseqdesc->keyvalueindex = text.offset_from<int>(pseqdesc);
    pseqdesc->keyvaluesize = KeyValueTextSize(pKeyValue) ? KeyValueTextSize(pKeyValue) + 1 : 0;
    buffer.align(4);
}


void EnsureFileDirectoryExists(const std::string &fileName) {
    std::error_code ec;
    std::filesystem::create_directories(vstd::paths::to_fs(fileName).parent_path(), ec);
}

void WriteModelFiles() {
    vstd::FunctionTimingRecorder _("Write");
    std::string blockFileName; // .ani
    size_t total = 0;
    std::string filename;

    // static, like the never-freed buffers they replace: the later steps keep using the header
    vstd::Buffer model_data;
    vstd::Buffer block_data; // .ani
    vstd::Buffer* blocks = nullptr;
    vstd::BufferPtr<studiohdr_t> pblockhdr;

    vstd::paths::strip_extension(g_studiomdl.outname);

    if (g_studiomdl.animblocksize != 0) {
        // write the non-default g_studiomdl.sequence group data to separate files
        g_studiomdl.animblockname.format("models/{}.ani", g_studiomdl.outname);

        blockFileName = g_StudioMdlContext.gameDir + g_studiomdl.animblockname.c_str();

        if (*g_studiomdl.internalName) {
            vstd::paths::strip_extension(g_studiomdl.internalName);
            g_studiomdl.animblockname.format("models/{}.ani", g_studiomdl.internalName);
        }

        EnsureFileDirectoryExists(blockFileName);

        blocks = &block_data;
        pblockhdr = blocks->allocate<studiohdr_t>();
        pblockhdr->id = IDSTUDIOANIMGROUPHEADER;
        pblockhdr->version = STUDIO_VERSION;
    }

    //
    // write the g_studiomdl.model output file
    //
    const auto phdr = model_data.allocate<studiohdr_t>();

    phdr->id = IDSTUDIOHEADER;
    phdr->version = STUDIO_VERSION;

    strcat(g_studiomdl.outname, ".mdl");

    filename = OutputModelPath(".mdl");


    // Create the directory.
    EnsureFileDirectoryExists(filename);


    if (!g_StudioMdlContext.quiet) {
        std::print("---------------------\n");
        std::print("writing {}:\n", filename);
    }

    LoadPreexistingSequenceOrder(filename.c_str());


    if (g_StudioMdlContext.parseable_completion_output) {
        char szRelativePath[260];
        V_MakeRelativePath(filename.c_str(), getenv("VGAME"), szRelativePath, sizeof(szRelativePath));
        std::print("\nOUTPUT MODEL: {}\n", szRelativePath);
    }


    phdr->eyeposition = g_studiomdl.eyeposition;
    phdr->illumposition = g_studiomdl.illumposition;

    if (!g_studiomdl.wrotebbox && g_studiomdl.sequence.Count() > 0) {
        g_studiomdl.bbox[0] = g_studiomdl.sequence[0].bmin;
        g_studiomdl.bbox[1] = g_studiomdl.sequence[0].bmax;
        //        CollisionModel_ExpandBBox(bbox[0], bbox[1]);
        g_studiomdl.sequence[0].bmin = g_studiomdl.bbox[0];
        g_studiomdl.sequence[0].bmax = g_studiomdl.bbox[1];
    }
    if (!g_studiomdl.wrotecbox) {
        // no default clipping box, just use per-sequence box
        g_studiomdl.cbox[0] = vec3_origin;
        g_studiomdl.cbox[1] = vec3_origin;
    }

    phdr->hull_min = g_studiomdl.bbox[0];
    phdr->hull_max = g_studiomdl.bbox[1];
    phdr->view_bbmin = g_studiomdl.cbox[0];
    phdr->view_bbmax = g_studiomdl.cbox[1];

    phdr->flags = g_studiomdl.gflags;
    phdr->mass = 1;
    phdr->constdirectionallightdot = g_studiomdl.constdirectionalightdot;

    if (g_StudioMdlContext.numAllowedRootLODs > 0) {
        phdr->numAllowedRootLODs = g_StudioMdlContext.numAllowedRootLODs;
    }

    // FIXME: Remove when we up the model version
    const auto phdr2 = model_data.allocate<studiohdr2_t>();
    phdr->studiohdr2index = phdr2.offset_as<int>();

    phdr2->illumpositionattachmentindex = g_studiomdl.illumpositionattachment;
    phdr2->flMaxEyeDeflection = g_studiomdl.flMaxEyeDeflection;

    BeginStringTable();

    if (*g_studiomdl.internalName) {
        V_strncpy(phdr->name, g_studiomdl.internalName, sizeof(phdr->name) - 1);
        AddToStringTable(phdr2, phdr2.field(&studiohdr2_t::sznameindex), g_studiomdl.internalName);
    }
    else {
        V_strncpy(phdr->name, g_studiomdl.outname, sizeof(phdr->name) - 1);
        AddToStringTable(phdr2, phdr2.field(&studiohdr2_t::sznameindex), g_studiomdl.outname);
    }

    WriteBoneInfo(model_data, phdr);
    if (!g_StudioMdlContext.quiet) {
        std::print("bones      {:7} bytes ({})\n", model_data.size() - total, g_StudioMdlContext.numbones);
    }
    total = model_data.size();

    WriteAnimations(model_data, blocks, phdr);
    if (!g_StudioMdlContext.quiet) {
        std::print("animations {:7} bytes ({} anims) ({} frames) [{}:{:02}]\n", model_data.size() - total,
                   g_studiomdl.animations.size(),
                   totalframes,
                   (int) totalseconds / 60, (int) totalseconds % 60);
    }
    total = model_data.size();

    WriteSequenceInfo(model_data, phdr);
    if (!g_StudioMdlContext.quiet) {
        std::print("sequences  {:7} bytes ({} seq) \n", model_data.size() - total, g_studiomdl.sequence.Count());
    }
    total = model_data.size();

    WriteModel(model_data, phdr);

    if (!g_StudioMdlContext.quiet) {
        std::print("models     {:7} bytes\n", model_data.size() - total);
    }

    total = model_data.size();

    WriteTextures(model_data, phdr);
    if (!g_StudioMdlContext.quiet) {
        std::print("textures   {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteQCPath();

    CapKeyValues();

    WriteKeyValues(model_data, phdr, g_StudioMdlContext.KeyValueText);
    if (!g_StudioMdlContext.quiet) {
        std::print("keyvalues  {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteBoneTransforms(model_data, phdr, phdr2);
    if (!g_StudioMdlContext.quiet) {
        std::print("bone transforms  {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteBoneFlexDrivers(model_data, phdr2);
    if (!g_StudioMdlContext.quiet) {
        std::print("bone flex driver {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteBodyGroupPresets(model_data, phdr2);
    if (!g_StudioMdlContext.quiet) {
        std::print("bodygroup presets {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteStringTable(model_data);

    total = model_data.size();

    // 32-bit words, like Valve's 32-bit studiomdl (long is 64-bit on LP64 and read 4 bytes past each word);
    // unsigned arithmetic: same bits as the original signed int version, without signed overflow
    // The checksum field lies inside the summed range and is updated every step, so its word reads the running value
    uint32_t checksum = 0;
    phdr->checksum = 0;
    for (size_t offset = 0; offset < total; offset += 4) {
        uint32_t word;
        memcpy(&word, model_data.data() + offset, sizeof(word));
        checksum = (checksum << 1) + ((checksum & 0x8000000) ? 1 : 0) + word;
        phdr->checksum = (int32_t) checksum;
    }

    if (g_StudioMdlContext.verifyOnly)
        return;


    if (!g_StudioMdlContext.quiet) {
        std::print("collision  {:7} bytes\n", model_data.size() - total);
    }

    // the .mdl data is complete: nothing allocates in it anymore, so raw pointers into it stay valid
    studiohdr_t* const pStudioHdr = phdr.get();

    AssignMeshIDs(pStudioHdr);

    pStudioHdr->length = static_cast<int>(model_data.size());
    if (!g_StudioMdlContext.quiet) {
        std::print("total      {:7}\n", pStudioHdr->length);
    }

    // Load materials for this model via the material system so that the
    // optimizer can ask questions about the materials.
    LoadMaterials(pStudioHdr);

    SaveOutputFile(filename.c_str(), model_data.data(), pStudioHdr->length);

    if (blocks) {
        const int length = static_cast<int>(blocks->size());
        pblockhdr->length = length;

        if (!g_StudioMdlContext.quiet) {
            std::print("---------------------\n");
            std::print("writing {}:\n", g_studiomdl.animblockname);
            std::print("blocks	   {:7}\n", g_studiomdl.animblocks.size());
            std::print("total      {:7}\n", length);
        }

        // nothing reads the .ani data after this
        SaveOutputFile(blockFileName, std::move(*blocks));
    }

    g_studiomdl.checksum = pStudioHdr->checksum;
    if (pStudioHdr->numbodyparts != 0) {
        // vertices have become an external peer data store
        // write now prior to impending vertex access from any further code
        // vertex accessors hide shifting vertex data
        WriteVertices(pStudioHdr);

#ifdef _DEBUG
        int bodyPartID;
        for (bodyPartID = 0; bodyPartID < pStudioHdr->numbodyparts; bodyPartID++) {
            mstudiobodyparts_t* pBodyPart = pStudioHdr->pBodypart(bodyPartID);
            int modelID;
            for (modelID = 0; modelID < pBodyPart->nummodels; modelID++) {
                mstudiomodel_t* pModel = pBodyPart->pModel(modelID);
                const mstudio_modelvertexdata_t* vertData = pModel->GetVertexData();
                Assert(vertData); // This can only return nullptr on X360 for now
                int vertID;
                for (vertID = 0; vertID < pModel->numvertices; vertID++) {
                    Vector4D* pTangentS = vertData->TangentS(vertID);
                    Assert(pTangentS->w == -1.0f || pTangentS->w == 1.0f);
                }
            }
        }
#endif

        s_bodypart_t* pBodyParts = (s_bodypart_t*) calloc(pStudioHdr->numbodyparts, sizeof(s_bodypart_t));
        for (int i = 0; i < pStudioHdr->numbodyparts; i++) {
            pBodyParts[i] = g_studiomdl.bodypart[i];
        }
        OptimizedModel::WriteOptimizedFiles(pStudioHdr, pBodyParts);
        free(pBodyParts);

        // now have external finalized vtx (windings) and vvd (vertexes)
        // re-open files, sort vertexes, perform fixups, and rewrite
        // purposely isolated as a post process for stability
        if (!FixupToSortedLODVertexes(pStudioHdr)) {
            MdlError("Aborted vertex sort fixup on '{}':\n", filename);
        }

        if (!Clamp_RootLOD(pStudioHdr)) {
            MdlError("Aborted root lod shift '{}':\n", filename);
        }
    }

    // NOTE!  If you don't want to go through the effort of loading studiorender for perf reasons,
    // make sure spewFlags ends up being zero.
    unsigned int spewFlags = SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS;

    if (g_StudioMdlContext.perf) {
        spewFlags |= SPEWPERFSTATS_SHOWPERF;
    }
    if (spewFlags) {
        SpewPerfStats(pStudioHdr, filename.c_str(), spewFlags);
    }

    // everything above read back and rewrote the outputs in memory; this is the only write to disk
    CommitOutputFiles();
}

const vertexFileHeader_t* mstudiomodel_t::CacheVertexData(void* pModelData) {
    static vertexFileHeader_t* pVertexHdr;

    Assert(pModelData == nullptr);

    // studiomdl is a single model process, can simply persist data in static
    if (pVertexHdr) {
        return pVertexHdr;
    }

    // load and persist the vertex file
    const std::string filename = OutputModelPath(".vvd");
    LoadOutputFile(filename, (void**) &pVertexHdr);

    // check id
    if (pVertexHdr->id != MODEL_VERTEX_FILE_ID) {
        MdlError("Error Vertex File: '{}' (id {} should be {})\n", filename, pVertexHdr->id, MODEL_VERTEX_FILE_ID);
    }

    // check version
    if (pVertexHdr->version != MODEL_VERTEX_FILE_VERSION) {
        MdlError("Error Vertex File: '{}' (version {} should be {})\n", filename, pVertexHdr->version,
                 MODEL_VERTEX_FILE_VERSION);
    }

    return pVertexHdr;
}

typedef struct {
    int32_t meshVertID;
    int32_t finalMeshVertID;
    int32_t vertexOffset;
    int32_t lodFlags;
} usedVertex_t;

typedef struct {
    int offsets[MAX_NUM_LODS];
    int numVertexes[MAX_NUM_LODS];
} lodMeshInfo_t;

struct vertexPool_t {
    std::vector<usedVertex_t> vertexList;
    std::vector<int> vertexMap{};
    lodMeshInfo_t lodMeshInfo{};
};

#define ALIGN(b, s)        (((uintptr_t)(b)+(s)-1)&~((s)-1))

//-----------------------------------------------------------------------------
// FindVertexOffsets
//
// Iterate sorted vertex list to determine mesh starts and counts.
//-----------------------------------------------------------------------------
void FindVertexOffsets(const int32_t vertexOffset, int offsets[MAX_NUM_LODS], int counts[MAX_NUM_LODS], int numLods,
                       const std::vector<usedVertex_t> &vertexList) {
    int k;

    // vertexOffset uniquely identifies a single mesh's vertexes in lod vertex sorted list
    // lod vertex list is sorted from lod N..lod 0
    for (int i = numLods - 1; i >= 0; i--) {
        offsets[i] = 0;
        counts[i] = 0;

        int lodFlags = (1 << (i + 1)) - 1;
        for (int j = 0; j < vertexList.size(); j++) {
            // determine start of mesh at desired lod
            if (vertexList[j].lodFlags > lodFlags)
                continue;
            if (vertexList[j].vertexOffset != vertexOffset)
                continue;

            for (k = j; k < vertexList.size(); k++) {
                // determine end of mesh at desired lod
                if (vertexList[k].vertexOffset != vertexOffset)
                    break;
                if (!(vertexList[k].lodFlags & (1 << i)))
                    break;
            }

            offsets[i] = j;
            counts[i] = k - j;
            break;
        }
    }
}

//-----------------------------------------------------------------------------
// _CompareUsedVertexes
//
// qsort callback
//-----------------------------------------------------------------------------
static int _CompareUsedVertexes(const void* a, const void* b) {
    usedVertex_t* pVertexA = (usedVertex_t*) a;
    usedVertex_t* pVertexB = (usedVertex_t*) b;

    // determine highest (lowest detail) lod
    // forces grouping into discrete MAX_NUM_LODS sections
    int lodA = Q_log2(pVertexA->lodFlags);
    int lodB = Q_log2(pVertexB->lodFlags);

    // descending sort (LodN..Lod0)
    int sort = lodB - lodA;
    if (sort)
        return sort;

    // within same lod, sub sort (ascending) by mesh
    sort = pVertexA->vertexOffset - pVertexB->vertexOffset;
    if (sort)
        return sort;

    // within same mesh, sub sort (ascending) by vertex
    sort = pVertexA->meshVertID - pVertexB->meshVertID;
    return sort;
}

//-----------------------------------------------------------------------------
// UsedVertexLookup_t is used to accelerate the sorted-to-unsorted mapping
//
// qsort callback
//-----------------------------------------------------------------------------
struct UsedVertexLookup_t {
    int32_t vertexOffset;
    int32_t meshVertID;
    int32_t index;
};

bool UsedVertexCompareFunc(const UsedVertexLookup_t &a, const UsedVertexLookup_t &b) {
    return ((a.vertexOffset == b.vertexOffset) && (a.meshVertID == b.meshVertID));
}

unsigned int UsedVertexKeyFunc(const UsedVertexLookup_t &a) {
    return Hash8(&a);
}

//-----------------------------------------------------------------------------
// BuildSortedVertexList
//
// Generates the sorted vertex list. Routine is purposely serial to
// ensure vertex integrity.
//-----------------------------------------------------------------------------
bool BuildSortedVertexList(const studiohdr_t* pStudioHdr, const vstd::Buffer &vtxBuffer,
                           std::vector<vertexPool_t> &vertexPools, std::vector<usedVertex_t> &vertexList) {
    const OptimizedModel::BodyPartHeader_t* pBodyPartHdr;
    const OptimizedModel::ModelHeader_t* pModelHdr;
    const OptimizedModel::ModelLODHeader_t* pModelLODHdr;
    const OptimizedModel::MeshHeader_t* pMeshHdr;
    const OptimizedModel::StripGroupHeader_t* pStripGroupHdr;
    const OptimizedModel::Vertex_t* pStripVertex;
    mstudiobodyparts_t* pStudioBodyPart;
    mstudiomodel_t* pStudioModel;
    mstudiomesh_t* pStudioMesh;
    vertexPool_t* pPool;
    int index;
    int currLod;
    uint32_t vertexOffset;
    int i, j, k, m, n, p;
    int poolStart;
    int numVertexPools;
    uint32_t numVertexes;
    int numMeshVertexes;
    int offsets[MAX_NUM_LODS];
    int counts[MAX_NUM_LODS];
    int finalMeshVertID;
    int baseMeshVertID;

    const auto* pVtxHdr = vtxBuffer.reinterpret_at<OptimizedModel::FileHeader_t>(0);

    // determine number of vertex pools
    if (pStudioHdr->numbodyparts != pVtxHdr->numBodyParts)
        return false;
    numVertexPools = 0;
    for (i = 0; i < pVtxHdr->numBodyParts; i++) {
        pBodyPartHdr = pVtxHdr->pBodyPart(i);
        pStudioBodyPart = pStudioHdr->pBodypart(i);
        if (pStudioBodyPart->nummodels != pBodyPartHdr->numModels)
            return false;

        // the model's subordinate lods only reference from a single top level pool
        // no new verts are created for sub lods
        // each model's subordinate mesh dictates its own vertex pool
        for (j = 0; j < pBodyPartHdr->numModels; j++) {
            pStudioModel = pStudioBodyPart->pModel(j);
            numVertexPools += pStudioModel->nummeshes;
        }
    }

    // allocate pools
    vertexPools.resize(numVertexPools);

    // iterate lods, mark referenced indexes
    numVertexPools = 0;
    for (i = 0; i < pVtxHdr->numBodyParts; i++) {
        pBodyPartHdr = pVtxHdr->pBodyPart(i);
        pStudioBodyPart = pStudioHdr->pBodypart(i);

        for (j = 0; j < pBodyPartHdr->numModels; j++) {
            pModelHdr = pBodyPartHdr->pModel(j);
            pStudioModel = pStudioBodyPart->pModel(j);

            // allocate each mesh's vertex list
            poolStart = numVertexPools;
            for (k = 0; k < pStudioModel->nummeshes; k++) {
                // track the expected relative offset into a flattened vertex list
                vertexOffset = 0;
                for (m = 0; m < poolStart + k; m++)
                    vertexOffset += vertexPools[m].vertexList.size();

                pStudioMesh = pStudioModel->pMesh(k);
                numMeshVertexes = pStudioMesh->numvertices;
                if (numMeshVertexes) {
                    vertexPools[numVertexPools].vertexList.resize(numMeshVertexes);
                    vertexPools[numVertexPools].vertexMap.resize(numMeshVertexes);
                    auto &usedVertexes = vertexPools[numVertexPools].vertexList;
                    auto &vertexMap = vertexPools[numVertexPools].vertexMap;

                    for (n = 0; n < numMeshVertexes; n++) {
                        // setup mapping
                        // due to the hierarchical layout, the vertID's map per mesh's pool
                        // a linear layout of the vertexes requires a unique signature to achieve a remap
                        // the offset and index form a unique signature
                        usedVertexes[n].meshVertID = n;
                        usedVertexes[n].finalMeshVertID = -1;
                        usedVertexes[n].vertexOffset = vstd::as_signed_checked<int>(vertexOffset);
                        usedVertexes[n].lodFlags = 0;
                        vertexMap[n] = n;
                    }
                }
                numVertexPools++;
            }

            // iterate all lods
            for (currLod = 0; currLod < pVtxHdr->numLODs; currLod++) {
                pModelLODHdr = pModelHdr->pLOD(currLod);

                if (pModelLODHdr->numMeshes != pStudioModel->nummeshes)
                    return false;

                for (k = 0; k < pModelLODHdr->numMeshes; k++) {
                    pMeshHdr = pModelLODHdr->pMesh(k);
                    pStudioMesh = pStudioModel->pMesh(k);
                    for (m = 0; m < pMeshHdr->numStripGroups; m++) {
                        pStripGroupHdr = pMeshHdr->pStripGroup(m);

                        // sanity check the indexes have 100% coverage of the vertexes
                        std::vector vertices(pStripGroupHdr->numVerts, -1);

                        for (n = 0; n < pStripGroupHdr->numIndices; n++) {
                            index = *pStripGroupHdr->pIndex(n);
                            if (index < 0 || index >= pStripGroupHdr->numVerts)
                                return false;
                            vertices[index] = index;
                        }

                        // sanity check for coverage
                        for (n = 0; n < pStripGroupHdr->numVerts; n++) {
                            if (vertices[n] != n)
                                return false;
                        }

                        // iterate vertexes
                        pPool = &vertexPools[poolStart + k];
                        for (n = 0; n < pStripGroupHdr->numVerts; n++) {
                            pStripVertex = pStripGroupHdr->pVertex(n);

                            if (pStripVertex->origMeshVertID < 0 || pStripVertex->origMeshVertID >= pPool->vertexList.
                                size())
                                return false;

                            // arrange binary flags for numerical sorting
                            // the lowest detail lod's verts at the top, the root lod's verts at the bottom
                            pPool->vertexList[pStripVertex->origMeshVertID].lodFlags |= 1 << currLod;
                        }
                    }
                }
            }
        }
    }

    // flatten the vertex pool hierarchy into a linear sequence
    numVertexes = 0;
    for (i = 0; i < numVertexPools; i++)
        numVertexes += vertexPools[i].vertexList.size();
    vertexList.reserve(numVertexes);
    numVertexes = 0;
    for (i = 0; i < numVertexPools; i++) {
        pPool = &vertexPools[i];
        for (j = 0; j < pPool->vertexList.size(); j++) {
            if (!pPool->vertexList[j].lodFlags) {
                // found an orphaned vertex that is unreferenced at any lod strip winding
                // don't know how these occur or who references them
                // cannot cull the orphaned vertexes, otherwise vertex counts are wrong
                // every vertex must be remapped
                // force the vertex to belong to the lowest lod
                // lod flags must be nonzero for proper sorted runs
                pPool->vertexList[j].lodFlags = 1 << (pVtxHdr->numLODs - 1);
            }
        }
        vertexList.insert(vertexList.end(), pPool->vertexList.begin(), pPool->vertexList.end());
        numVertexes += pPool->vertexList.size();
    }

    // sort the vertexes based on lod flags
    // the sort dictates the linear sequencing of the .vvd data file
    // the vtx file indexes get remapped to the new sort order
    qsort(vertexList.data(), numVertexes, sizeof(usedVertex_t), _CompareUsedVertexes);

    // build a mapping table from mesh relative indexes to the flat lod sorted array
    CUtlHash<UsedVertexLookup_t> usedVertexHash(vstd::as_signed_checked<int>(numVertexes), 0, 0, UsedVertexCompareFunc,
                                                UsedVertexKeyFunc);
    for (k = 0; k < numVertexes; k++) {
        UsedVertexLookup_t usedVertexLookup = {vertexList[k].vertexOffset, vertexList[k].meshVertID, k};
        usedVertexHash.Insert(usedVertexLookup);
    }
    vertexOffset = 0;
    for (i = 0; i < numVertexPools; i++) {
        pPool = &vertexPools[i];
        for (j = 0; j < pPool->vertexList.size(); j++) {
            // search flattened sorted vertexes
            UsedVertexLookup_t usedVertexLookup = {vstd::as_signed_checked<int>(vertexOffset), j, -1};
            UtlHashHandle_t handle = usedVertexHash.Find(usedVertexLookup);
            Assert(handle != usedVertexHash.InvalidHandle());
            pPool->vertexMap[j] = usedVertexHash[handle].index;
        }
        vertexOffset += pPool->vertexList.size();
    }

    // build offsets and counts that identifies mesh's distribution across lods
    // calculate final fixed vertex location if vertexes were gathered to mesh order from lod sorted list
    finalMeshVertID = 0;
    poolStart = 0;
    for (i = 0; i < pStudioHdr->numbodyparts; i++) {
        pStudioBodyPart = pStudioHdr->pBodypart(i);
        for (j = 0; j < pStudioBodyPart->nummodels; j++) {
            pStudioModel = pStudioBodyPart->pModel(j);
            for (m = 0; m < pStudioModel->nummeshes; m++) {
                // track the expected offset into linear vertexes
                vertexOffset = 0;
                for (n = 0; n < poolStart + m; n++)
                    vertexOffset += vertexPools[n].vertexList.size();

                // skip counting if there's no vertices in this mesh
                if (pStudioModel->pMesh(m)->numvertices == 0) {
                    for (n = 0; n < pVtxHdr->numLODs; n++) {
                        counts[n] = 0;
                    }
                }
                else {
                    // vertexOffset works as unique key to identify vertexes for a specific mesh
                    // a mesh's verts are distributed, but guaranteed sequential in the lod sorted vertex list
                    // determine base index and offset and run length for target mesh for all lod levels
                    FindVertexOffsets(vstd::as_signed_checked<int>(vertexOffset), offsets, counts, pVtxHdr->numLODs,
                                      vertexList);
                }

                for (n = 0; n < pVtxHdr->numLODs; n++) {
                    if (!counts[n])
                        offsets[n] = 0;

                    vertexPools[poolStart + m].lodMeshInfo.offsets[n] = offsets[n];
                    vertexPools[poolStart + m].lodMeshInfo.numVertexes[n] = counts[n];
                }

                // iterate using calculated offsets to walk each mesh
                // set its expected final vertex id, which is its "gathered" index relative to mesh
                baseMeshVertID = finalMeshVertID;
                for (n = pVtxHdr->numLODs - 1; n >= 0; n--) {
                    // iterate each vert in the mesh
                    // vertex id is relative to
                    for (p = 0; p < counts[n]; p++) {
                        vertexList[offsets[n] + p].finalMeshVertID = finalMeshVertID - baseMeshVertID;
                        finalMeshVertID++;
                    }
                }
            }
            poolStart += pStudioModel->nummeshes;
        }
    }

    // safety check
    // every referenced vertex should have been remapped correctly
    // some models do have orphaned vertexes, ignore these
    for (i = 0; i < numVertexes; i++) {
        if (vertexList[i].lodFlags && vertexList[i].finalMeshVertID == -1) {
            // should never happen, data occurred in unknown manner
            // don't build corrupted data
            return false;
        }
    }


    // success
    return true;
}

//-----------------------------------------------------------------------------
// FixupVVDFile
//
// VVD files get vertexes remapped to a flat lod sorted order.
//-----------------------------------------------------------------------------
static bool FixupVVDFile(const std::string_view fileName, const studiohdr_t* pStudioHdr, const vstd::Buffer& pVtxBuff,
                         const std::vector<vertexPool_t> &vertexPools, const std::vector<usedVertex_t> &vertexList) {
    vstd::FunctionTimingRecorder _("Write");
    mstudiobodyparts_t* pStudioBodyPart;
    mstudiomodel_t* pStudioModel;
    mstudiomesh_t* pStudioMesh;
    mstudiovertex_t* pVertex_old;
    Vector4D* pTangent_old;
    uint8_t* pExtraData_old = nullptr;
    const lodMeshInfo_t* pLodMeshInfo;
    uint8_t* pVertexBase_old;
    uint8_t* pTangentBase_old;
    uint8_t* pExtraDataBase_old = nullptr;
    vstd::BufferPtr<uint8_t> pExtraData_new;
    int i;
    int j;
    int k;
    int n;
    int p;
    int numFixups;
    int numFlat;
    int oldIndex;
    int mask;
    int maxCount;
    int numMeshes;
    bool bExtraData = (pStudioHdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) != 0;
    ExtraVertexAttributeIndex_t* pExtraIndex_old = nullptr;
    ExtraVertexAttributesHeader_t* pExtraHeader_old = nullptr;
    vstd::BufferPtr<uint8_t> pExtraDataBase_new;

    auto* vtxHdr = pVtxBuff.reinterpret_at<OptimizedModel::FileHeader_t>(0);

    auto oldVvdBuffOpt = LoadOutputFile(fileName);
    if (!oldVvdBuffOpt) {
        MdlError("Failed to load old VVD file for fixup");
    }
    auto oldVvdBuff = std::move(oldVvdBuffOpt.value());


    auto* oldHdr = oldVvdBuff.reinterpret_at<vertexFileHeader_t>(0);
    if (oldHdr->numLODs != 1) {
        // file has wrong expected state
        return false;
    }

    // meshes need relocation fixup from lod order back to mesh order
    numFixups = 0;
    numMeshes = 0;
    for (i = 0; i < pStudioHdr->numbodyparts; i++) {
        pStudioBodyPart = pStudioHdr->pBodypart(i);
        for (j = 0; j < pStudioBodyPart->nummodels; j++) {
            pStudioModel = pStudioBodyPart->pModel(j);
            for (k = 0; k < pStudioModel->nummeshes; k++) {
                pStudioMesh = pStudioModel->pMesh(k);
                if (!pStudioMesh->numvertices) {
                    // no vertexes for this mesh, skip it
                    continue;
                }
                for (n = vtxHdr->numLODs - 1; n >= 0; n--) {
                    pLodMeshInfo = &vertexPools[numMeshes + k].lodMeshInfo;
                    if (!pLodMeshInfo->numVertexes[n]) {
                        // no vertexes for this portion of the mesh at this lod, skip it
                        continue;
                    }
                    numFixups++;
                }
            }
            numMeshes += k;
        }
    }
    if (numMeshes == 1 || numFixups == 1 || vtxHdr->numLODs == 1) {
        // no fixup required for a single mesh
        // no fixup required for single lod
        // no fixup required when mesh data is contiguous as expected
        numFixups = 0;
    }
    vstd::Buffer buffer;
    buffer.reserve(FILEBUFFER);

    // setup headers
    auto newHdr = buffer.allocate<vertexFileHeader_t>();

    // clone and fixup new header
    *newHdr = *oldHdr;
    newHdr->numLODs = vtxHdr->numLODs;
    newHdr->numFixups = numFixups;

    // skip new fixup table
    buffer.align(4);

    auto pFixupTable = buffer.allocate<vertexFileFixup_t>(numFixups);
    newHdr->fixupTableStart = vstd::as_signed_checked<int>(pFixupTable.offset());

    // skip new vertex data
    buffer.align(16);
    auto pVertex_new = buffer.allocate<mstudiovertex_t>(vertexList.size());
    newHdr->vertexDataStart = vstd::as_signed_checked<int>(pVertex_new.offset());

    // skip new tangent data
    buffer.align(16);
    auto pTangent_new = buffer.allocate<Vector4D>(vertexList.size());
    newHdr->tangentDataStart = vstd::as_signed_checked<int>(pTangent_new.offset());

    pVertexBase_old = reinterpret_cast<uint8_t*>(oldHdr) + oldHdr->vertexDataStart;
    pTangentBase_old = reinterpret_cast<uint8_t*>(oldHdr) + oldHdr->tangentDataStart;

    // skip extra vertex data
    if (bExtraData) {
        MdlError("Extra data is only for CSGO, we dont support that for SFM");
        pExtraDataBase_old = pTangentBase_old + vertexList.size() * sizeof(Vector4D);
        pExtraHeader_old = reinterpret_cast<ExtraVertexAttributesHeader_t*>(pExtraDataBase_old);
        pExtraIndex_old = reinterpret_cast<ExtraVertexAttributeIndex_t*>(pExtraHeader_old + 1);
        pExtraDataBase_new = buffer.end_ptr();
        auto pExtraHeader_new = buffer.allocate<ExtraVertexAttributesHeader_t>();
        auto pExtraIndex_new = buffer.allocate<ExtraVertexAttributeIndex_t>();
        *pExtraHeader_new = *pExtraHeader_old;
        *pExtraIndex_new = *pExtraIndex_old;
        buffer.allocate<uint8_t>(pExtraHeader_old->m_totalbytes);
    }

    // determine number of aggregate verts towards root lod
    // loader can truncate read according to desired root lod
    maxCount = -1;
    for (n = vtxHdr->numLODs - 1; n >= 0; n--) {
        mask = 1 << n;
        for (p = 0; p < vertexList.size(); p++) {
            if (mask & vertexList[p].lodFlags) {
                if (maxCount < p)
                    maxCount = p;
            }
        }
        newHdr->numLODVertexes[n] = maxCount + 1;
    }
    for (n = vtxHdr->numLODs; n < MAX_NUM_LODS; n++) {
        // ripple the last valid lod entry all the way down
        newHdr->numLODVertexes[n] = newHdr->numLODVertexes[vtxHdr->numLODs - 1];
    }

    // build mesh relocation fixup table
    if (numFixups) {
        int numOutFixups = 0;
        numMeshes = 0;
        for (i = 0; i < pStudioHdr->numbodyparts; i++) {
            pStudioBodyPart = pStudioHdr->pBodypart(i);
            for (j = 0; j < pStudioBodyPart->nummodels; j++) {
                pStudioModel = pStudioBodyPart->pModel(j);
                for (k = 0; k < pStudioModel->nummeshes; k++) {
                    pStudioMesh = pStudioModel->pMesh(k);
                    if (!pStudioMesh->numvertices) {
                        // not vertexes for this mesh, skip it
                        continue;
                    }
                    for (n = vtxHdr->numLODs - 1; n >= 0; n--) {
                        pLodMeshInfo = &vertexPools[numMeshes + k].lodMeshInfo;
                        if (!pLodMeshInfo->numVertexes[n]) {
                            // no vertexes for this portion of the mesh at this lod, skip it
                            continue;
                        }
                        pFixupTable[numOutFixups].lod = n;
                        pFixupTable[numOutFixups].numVertexes = pLodMeshInfo->numVertexes[n];
                        pFixupTable[numOutFixups].sourceVertexID = pLodMeshInfo->offsets[n];
                        numOutFixups++;
                    }
                }
                numMeshes += pStudioModel->nummeshes;
            }
        }
        if (numOutFixups != numFixups) {
            // logic sync error, final calc should match precalc, otherwise memory corruption
            return false;
        }
    }

    // generate offsets to vertexes
    numFlat = 0;
    std::vector<mstudiovertex_t*> flatVertexes(vertexList.size());
    std::vector<Vector4D*> flatTangents(vertexList.size());
    std::vector<uint8_t*> flatExtraData(pExtraHeader_old ? vertexList.size() * pExtraHeader_old->m_count : 0);

    for (i = 0; i < pStudioHdr->numbodyparts; i++) {
        pStudioBodyPart = pStudioHdr->pBodypart(i);
        for (j = 0; j < pStudioBodyPart->nummodels; j++) {
            pStudioModel = pStudioBodyPart->pModel(j);
            pVertex_old = (mstudiovertex_t*) &pVertexBase_old[pStudioModel->vertexindex];
            pTangent_old = (Vector4D*) &pTangentBase_old[pStudioModel->tangentsindex];
            for (k = 0; k < pStudioModel->nummeshes; k++) {
                // get each mesh's vertexes
                pStudioMesh = pStudioModel->pMesh(k);
                for (n = 0; n < pStudioMesh->numvertices; n++) {
                    // old vertex pools are per model, separated per mesh by a start offset
                    // vertexes are then isolated subpools per mesh
                    // build the flat linear array of lookup pointers
                    flatVertexes[numFlat] = &pVertex_old[pStudioMesh->vertexoffset + n];
                    flatTangents[numFlat] = &pTangent_old[pStudioMesh->vertexoffset + n];

                    if (bExtraData) {
                        for (int e = 0; e < pExtraHeader_old->m_count; ++e) {
                            int offset = pExtraIndex_old[e].m_offset;
                            int bytesPerVertex = pExtraIndex_old[e].m_bytes;
                            pExtraData_old = pExtraDataBase_old + offset +
                                             (pStudioModel->vertexindex / sizeof(mstudiovertex_t)) * bytesPerVertex;
                            flatExtraData[e * vertexList.size() + numFlat] = &pExtraData_old[
                                (pStudioMesh->vertexoffset + n) * bytesPerVertex];
                        }
                    }

                    numFlat++;
                }
            }
        }
    }

    // write in lod sorted order
    for (i = 0; i < vertexList.size(); i++) {
        // iterate sorted order, remap old vert location to new vert location
        oldIndex = vertexList[i].vertexOffset + vertexList[i].meshVertID;
        pVertex_new[i] = *flatVertexes[oldIndex];
        pTangent_new[i] = *flatTangents[oldIndex];

        if (bExtraData) {
            MdlError("Extra data is only for CSGO, we dont support that for SFM");
            for (int e = 0; e < pExtraHeader_old->m_count; ++e) {
                int offset = pExtraIndex_old[e].m_offset;
                int bytesPerVertex = pExtraIndex_old[e].m_bytes;
                pExtraData_new = pExtraDataBase_new + offset;
                memcpy(&pExtraData_new[i * bytesPerVertex],
                       flatExtraData[e * vertexList.size() + oldIndex],
                       bytesPerVertex);
            }
        }
    }
    SaveOutputFile(fileName, std::move(buffer));

    // success
    return true;
}

//-----------------------------------------------------------------------------
// FixupVTXFile
//
// VTX files get their windings remapped.
//-----------------------------------------------------------------------------
bool FixupVTXFile(const std::string_view fileName, const studiohdr_t* pStudioHdr,
                  const std::vector<vertexPool_t> &vertexPools, const std::vector<usedVertex_t> &vertexList) {
    vstd::FunctionTimingRecorder _("Write");

    auto vtxBuffOpt = LoadOutputFile(fileName);
    if (!vtxBuffOpt) {
        MdlError("Failed to load VTX file({}) for fixup", fileName);
        return false;
    }
    auto vtxBuff = std::move(vtxBuffOpt.value());
    auto vtxHdr = vtxBuff.reinterpret_at<OptimizedModel::FileHeader_t>(0);

    // iterate all lod's windings
    uint32_t poolStart = 0;
    for (uint32_t i = 0; i < vtxHdr->numBodyParts; i++) {
        OptimizedModel::BodyPartHeader_t* pBodyPartHdr = vtxHdr->pBodyPart(i);
        mstudiobodyparts_t* pStudioBodyPart = pStudioHdr->pBodypart(i);

        for (int j = 0; j < pBodyPartHdr->numModels; j++) {
            OptimizedModel::ModelHeader_t* pModelHdr = pBodyPartHdr->pModel(j);
            mstudiomodel_t* pStudioModel = pStudioBodyPart->pModel(j);

            // iterate all lods
            for (int currLod = 0; currLod < vtxHdr->numLODs; currLod++) {
                OptimizedModel::ModelLODHeader_t* pModelLODHdr = pModelHdr->pLOD(currLod);

                if (pModelLODHdr->numMeshes != pStudioModel->nummeshes)
                    return false;

                for (int k = 0; k < pModelLODHdr->numMeshes; k++) {
                    // track the expected relative offset into the flat vertexes
                    uint32_t vertexOffset = 0;
                    for (uint32_t m = 0; m < poolStart + k; m++)
                        vertexOffset += vertexPools[m].vertexList.size();

                    OptimizedModel::MeshHeader_t* pMeshHdr = pModelLODHdr->pMesh(k);
                    for (uint32_t m = 0; m < pMeshHdr->numStripGroups; m++) {
                        OptimizedModel::StripGroupHeader_t* pStripGroupHdr = pMeshHdr->pStripGroup(m);

                        for (int n = 0; n < pStripGroupHdr->numVerts; n++) {
                            OptimizedModel::Vertex_t* pStripVertex = pStripGroupHdr->pVertex(n);

                            // remap old mesh relative vertex index to absolute flat sorted list
                            int newMeshVertID = vertexPools[poolStart + k].vertexMap[pStripVertex->origMeshVertID];

                            // map to expected final fixed vertex locations
                            // final fixed vertex location is performed by runtime loading code
                            newMeshVertID = vertexList[newMeshVertID].finalMeshVertID;

                            // fixup to expected
                            pStripVertex->origMeshVertID = newMeshVertID;
                        }
                    }
                }
            }
            poolStart += pStudioModel->nummeshes;
        }
    }

    SaveOutputFile(fileName, std::move(vtxBuff));

    return true;
}

//-----------------------------------------------------------------------------
// FixupMDLFile
//
// MDL files get flexes/vertex/tangent data offsets fixed
//-----------------------------------------------------------------------------
bool FixupMDLFile(const std::string_view fileName, const studiohdr_t* pStudioHdr,
                  const vstd::Buffer& pVtxBuff, const std::vector<vertexPool_t> &vertexPools,
                  const std::vector<usedVertex_t> &vertexList) {
    mstudiobodyparts_t* pStudioBodyPart;
    mstudiomodel_t* pStudioModel;
    mstudiomesh_t* pStudioMesh;
    int i;
    int j;
    int m;
    int n;
    int p;

    auto* pVtxHdr = pVtxBuff.reinterpret_at<OptimizedModel::FileHeader_t>(0);

    int numLODs = pVtxHdr->numLODs;

    int numMeshes = 0;
    for (i = 0; i < pStudioHdr->numbodyparts; i++) {
        pStudioBodyPart = pStudioHdr->pBodypart(i);

        for (j = 0; j < pStudioBodyPart->nummodels; j++) {
            pStudioModel = pStudioBodyPart->pModel(j);

            for (m = 0; m < pStudioModel->nummeshes; m++) {
                // get each mesh
                pStudioMesh = pStudioModel->pMesh(m);
                const lodMeshInfo_t* pLodMeshInfo = &vertexPools[numMeshes + m].lodMeshInfo;

                for (n = 0; n < numLODs; n++) {
                    // the root lod, contains all the lower detail lods verts
                    // tally the verts that are at each lod
                    int total = 0;
                    for (p = n; p < numLODs; p++)
                        total += pLodMeshInfo->numVertexes[p];

                    // embed the fixup for loader
                    pStudioMesh->vertexdata.numLODVertexes[n] = total;
                }
                for (p = n; p < MAX_NUM_LODS; p++) {
                    // duplicate last valid lod to end of list
                    pStudioMesh->vertexdata.numLODVertexes[p] = pStudioMesh->vertexdata.numLODVertexes[numLODs - 1];
                }

                // fix the flexes
                for (n = 0; n < pStudioMesh->numflexes; n++) {
                    mstudioflex_t* pStudioFlex = pStudioMesh->pFlex(n);

                    uint8_t* pvanim = pStudioFlex->pBaseVertanim();
                    int nVAnimSizeBytes = pStudioFlex->VertAnimSizeBytes();

                    for (p = 0; p < pStudioFlex->numverts; p++, pvanim += nVAnimSizeBytes) {
                        mstudiovertanim_t* pStudioVertAnim = (mstudiovertanim_t*) (pvanim);

                        if (pStudioVertAnim->index < 0 || pStudioVertAnim->index >= pStudioMesh->numvertices)
                            return false;

                        // remap old mesh relative vertex index to absolute flat sorted list
                        int newMeshVertID = vertexPools[numMeshes + m].vertexMap[pStudioVertAnim->index];

                        // map to expected final fixed vertex locations
                        // final fixed vertex location is performed by runtime loading code
                        newMeshVertID = vertexList[newMeshVertID].finalMeshVertID;

                        // fixup to expected
                        pStudioVertAnim->index = newMeshVertID;
                    }
                }
            }
            numMeshes += pStudioModel->nummeshes;
        }
    }

    // Reset any pointer values to zero before writing out final mdl.
    // This allows better testing of the studiomdl tool -
    // mdl files can be compared more easily from one run to another.
    for (i = 0; i < pStudioHdr->numbodyparts; i++) {
        pStudioBodyPart = pStudioHdr->pBodypart(i);

        for (j = 0; j < pStudioBodyPart->nummodels; j++) {
            pStudioModel = pStudioBodyPart->pModel(j);
            // runtime pointers set by the vertex accessors while optimizing; meaningless on disk
            pStudioModel->vertexdata.pVertexData = nullptr;
            pStudioModel->vertexdata.pTangentData = nullptr;
            pStudioModel->vertexdata.pExtraData = nullptr;

            for (m = 0; m < pStudioModel->nummeshes; m++) {
                pStudioMesh = pStudioModel->pMesh(m);
                pStudioMesh->vertexdata.modelvertexdata = nullptr;
            }
        }
    }
    if (pStudioHdr->textureindex != 0) {
        for (i = 0; i < pStudioHdr->numtextures; i++) {
            pStudioHdr->pTexture(i)->material = nullptr;
        }
    }

    // Clear vertex data

    {
        SaveOutputFile(fileName, pStudioHdr, pStudioHdr->length);
    }

    // success
    return true;
}

//-----------------------------------------------------------------------------
// FixupToSortedLODVertexes
//
// VVD files get vertexes fixed to a flat sorted order, ascending in lower detail lod usage
// VTX files get their windings remapped to the sort.
//-----------------------------------------------------------------------------
bool FixupToSortedLODVertexes(studiohdr_t* pStudioHdr) {
    vstd::FunctionTimingRecorder _("Write");
    std::vector<vertexPool_t> vertexPools;
    std::vector<usedVertex_t> vertexList;
    int numVertexes;
    int numVertexPools;

    // determine lod usage per vertex
    // all vtx files enumerate model's lod verts, but differ in their mesh makeup
    // use xxx.dx90.vtx to establish which vertexes are used by each lod
    auto vtxBufferOpt = LoadOutputFile(OutputModelPath(".dx90.vtx"));
    if (!vtxBufferOpt) {
        MdlError("Failed to load .dx90.vtx file for LOD fixup");
        return false;
    }
    auto vtxBuffer = std::move(vtxBufferOpt.value());

    // build the sorted vertex tables
    if (!BuildSortedVertexList(pStudioHdr, vtxBuffer, vertexPools, vertexList)) {
        // data sync error
        return false;
    }

    // fixup ???.vvd
    if (!FixupVVDFile(OutputModelPath(".vvd"), pStudioHdr, vtxBuffer, vertexPools, vertexList)) {
        // data error
        return false;
    }

    if (!FixupVTXFile(OutputModelPath(".dx90.vtx"), pStudioHdr, vertexPools, vertexList)) {
        // data error
        return false;
    }

    // fixup ???.mdl
    if (!FixupMDLFile(OutputModelPath(".mdl"), pStudioHdr, vtxBuffer, vertexPools, vertexList)) {
        // data error
        return false;
    }

    // free the tables
    // for (int i = 0; i < numVertexPools; i++) {
    //     if (pVertexPools[i].pVertexList)
    //         free(pVertexPools[i].pVertexList);
    //     if (pVertexPools[i].pVertexMap)
    //         free(pVertexPools[i].pVertexMap);
    // }
    // if (numVertexPools)
    //     free(pVertexPools);


    // success
    return true;
}


uint8_t IsByte(int val) {
    if (val < 0 || val > 0xFF) {
        MdlError("byte conversion out of range {}\n", val);
    }
    return val;
}

char IsChar(int val) {
    if (val < -0x80 || val > 0x7F) {
        MdlError("char conversion out of range {}\n", val);
    }
    return val;
}

int IsInt24(int val) {
#ifdef PEDANTIC_STUDIOMDL
    if (val < -0x800000 || val > 0x7FFFFF) {
        MdlError("int24 conversion out of range {}\n", val);
    }
#endif
    return val;
}


short IsShort(int val) {
    if (val < -0x8000 || val > 0x7FFF) {
        MdlError("short conversion out of range {}\n", val);
    }
    return val;
}

unsigned short IsUShort(int val) {
    if (val < 0 || val > 0xFFFF) {
        MdlError("ushort conversion out of range {}\n", val);
    }
    return val;
}


bool Clamp_MDL_LODS(const char* fileName, int rootLOD) {
    studiohdr_t* pStudioHdr;

    int len = LoadOutputFile(fileName, (void**) &pStudioHdr);

    Studio_SetRootLOD(pStudioHdr, rootLOD);

#if 0
    // shift down bone LOD masks
    int iBone;
    for (iBone = 0; iBone < pStudioHdr->numbones; iBone++) {
        const mstudiobone_t* pBone = pStudioHdr->pBone(iBone);

        int nLodID;
        for (nLodID = 0; nLodID < rootLOD; nLodID++) {
            BoneFlags iLodMask = BoneUsedByVertexAtLod(nLodID);

            if (HasFlag(pBone->flags, BoneUsedByVertexAtLod(rootLOD))) {
                pBone->flags = pBone->flags | iLodMask;
            }
            else {
                pBone->flags = pBone->flags & (~iLodMask);
            }
        }
    }
#endif

    {
        SaveOutputFile(fileName, pStudioHdr, len);
    }

    return true;
}


bool Clamp_VVD_LODS(const char* fileName, int rootLOD, bool bExtraData) {
    vertexFileHeader_t* pTempVvdHdr;

    int len = LoadOutputFile(fileName, (void**) &pTempVvdHdr);

    int newLength = Studio_VertexDataSize(pTempVvdHdr, rootLOD, true, bExtraData);

    // printf("was %d now %d\n", len, newLength );

    vertexFileHeader_t* pNewVvdHdr = (vertexFileHeader_t*) calloc(newLength, 1);

    Studio_LoadVertexes(pTempVvdHdr, pNewVvdHdr, rootLOD, true, bExtraData);

    if (!g_StudioMdlContext.quiet) {
        std::print("---------------------\n");
        std::print("writing {}:\n", fileName);
        std::print("vertices   ({} vertices)\n", pNewVvdHdr->numLODVertexes[0]);
    }

    // pNewVvdHdr->length = newLength;

    {
        SaveOutputFile(fileName, pNewVvdHdr, newLength);
    }

    return true;
}


bool Clamp_VTX_LODS(const char* fileName, int rootLOD, studiohdr_t* pStudioHdr) {
    int i, j;
    int nLodID;

    OptimizedModel::FileHeader_t* pVtxHdr;

    int len = LoadOutputFile(fileName, (void**) &pVtxHdr);

    OptimizedModel::FileHeader_t* pNewVtxHdr = (OptimizedModel::FileHeader_t*) calloc(FILEBUFFER, 1);

    uint8_t* pData = (uint8_t*) pNewVtxHdr;
    pData += sizeof(OptimizedModel::FileHeader_t);
    ALIGN4(pData);

    // header
    pNewVtxHdr->version = pVtxHdr->version;
    pNewVtxHdr->vertCacheSize = pVtxHdr->vertCacheSize;
    pNewVtxHdr->maxBonesPerStrip = pVtxHdr->maxBonesPerStrip;
    pNewVtxHdr->maxBonesPerFace = pVtxHdr->maxBonesPerFace;
    pNewVtxHdr->maxBonesPerVert = pVtxHdr->maxBonesPerVert;
    pNewVtxHdr->checkSum = pVtxHdr->checkSum;
    pNewVtxHdr->numLODs = pVtxHdr->numLODs;

    // material replacement list
    pNewVtxHdr->materialReplacementListOffset = (pData - (uint8_t*) pNewVtxHdr);
    pData += pVtxHdr->numLODs * sizeof(OptimizedModel::MaterialReplacementListHeader_t);
    // ALIGN4( pData );

    BeginStringTable();

    // allocate replacement list arrays
    for (nLodID = rootLOD; nLodID < pVtxHdr->numLODs; nLodID++) {
        OptimizedModel::MaterialReplacementListHeader_t* pReplacementList = pVtxHdr->pMaterialReplacementList(nLodID);
        OptimizedModel::MaterialReplacementListHeader_t* pNewReplacementList = pNewVtxHdr->pMaterialReplacementList(
            nLodID);

        pNewReplacementList->numReplacements = pReplacementList->numReplacements;
        pNewReplacementList->replacementOffset = (pData - (uint8_t*) pNewReplacementList);
        pData += pNewReplacementList->numReplacements * sizeof(OptimizedModel::MaterialReplacementHeader_t);
        // ALIGN4( pData );

        for (i = 0; i < pReplacementList->numReplacements; i++) {
            OptimizedModel::MaterialReplacementHeader_t* pReplacement = pReplacementList->pMaterialReplacement(i);
            OptimizedModel::MaterialReplacementHeader_t* pNewReplacement = pNewReplacementList->pMaterialReplacement(i);

            pNewReplacement->materialID = pReplacement->materialID;
            AddToStringTable(pNewReplacement, &pNewReplacement->replacementMaterialNameOffset,
                             pReplacement->pMaterialReplacementName());
        }
    }
    pData = WriteStringTable(pData);

    // link previous LODs to higher LODs
    for (nLodID = 0; nLodID < rootLOD; nLodID++) {
        OptimizedModel::MaterialReplacementListHeader_t* pRootReplacementList = pNewVtxHdr->pMaterialReplacementList(
            rootLOD);
        OptimizedModel::MaterialReplacementListHeader_t* pNewReplacementList = pNewVtxHdr->pMaterialReplacementList(
            nLodID);

        int delta = (uint8_t*) pRootReplacementList - (uint8_t*) pNewReplacementList;

        pNewReplacementList->numReplacements = pRootReplacementList->numReplacements;
        pNewReplacementList->replacementOffset = pRootReplacementList->replacementOffset + delta;
    }

    // body parts
    pNewVtxHdr->numBodyParts = pStudioHdr->numbodyparts;
    pNewVtxHdr->bodyPartOffset = (pData - (uint8_t*) pNewVtxHdr);
    pData += pNewVtxHdr->numBodyParts * sizeof(OptimizedModel::BodyPartHeader_t);
    // ALIGN4( pData );

    // Iterate over every body part...
    for (i = 0; i < pStudioHdr->numbodyparts; i++) {
        mstudiobodyparts_t* pBodyPart = pStudioHdr->pBodypart(i);
        OptimizedModel::BodyPartHeader_t* pVtxBodyPart = pVtxHdr->pBodyPart(i);
        OptimizedModel::BodyPartHeader_t* pNewVtxBodyPart = pNewVtxHdr->pBodyPart(i);

        pNewVtxBodyPart->numModels = pBodyPart->nummodels;
        pNewVtxBodyPart->modelOffset = (pData - (uint8_t*) pNewVtxBodyPart);
        pData += pNewVtxBodyPart->numModels * sizeof(OptimizedModel::ModelHeader_t);
        // ALIGN4( pData );

        // Iterate over every submodel...
        for (j = 0; j < pBodyPart->nummodels; ++j) {
            mstudiomodel_t* pModel = pBodyPart->pModel(j);
            OptimizedModel::ModelHeader_t* pVtxModel = pVtxBodyPart->pModel(j);
            OptimizedModel::ModelHeader_t* pNewVtxModel = pNewVtxBodyPart->pModel(j);

            pNewVtxModel->numLODs = pVtxModel->numLODs;
            pNewVtxModel->lodOffset = (pData - (uint8_t*) pNewVtxModel);
            pData += pNewVtxModel->numLODs * sizeof(OptimizedModel::ModelLODHeader_t);
            ALIGN4(pData);

            for (nLodID = rootLOD; nLodID < pVtxModel->numLODs; nLodID++) {
                OptimizedModel::ModelLODHeader_t* pVtxLOD = pVtxModel->pLOD(nLodID);
                OptimizedModel::ModelLODHeader_t* pNewVtxLOD = pNewVtxModel->pLOD(nLodID);

                pNewVtxLOD->numMeshes = pVtxLOD->numMeshes;
                pNewVtxLOD->switchPoint = pVtxLOD->switchPoint;
                pNewVtxLOD->meshOffset = (pData - (uint8_t*) pNewVtxLOD);
                pData += pNewVtxLOD->numMeshes * sizeof(OptimizedModel::MeshHeader_t);
                ALIGN4(pData);

                // Iterate over all the meshes....
                for (int k = 0; k < pModel->nummeshes; ++k) {
                    Assert(pModel->nummeshes == pVtxLOD->numMeshes);
                    //					mstudiomesh_t* pMesh = pModel->pMesh(k);
                    OptimizedModel::MeshHeader_t* pVtxMesh = pVtxLOD->pMesh(k);
                    OptimizedModel::MeshHeader_t* pNewVtxMesh = pNewVtxLOD->pMesh(k);

                    pNewVtxMesh->numStripGroups = pVtxMesh->numStripGroups;
                    pNewVtxMesh->flags = pVtxMesh->flags;
                    pNewVtxMesh->stripGroupHeaderOffset = (pData - (uint8_t*) pNewVtxMesh);
                    pData += pNewVtxMesh->numStripGroups * sizeof(OptimizedModel::StripGroupHeader_t);

                    // printf("part %d : model %d : lod %d : mesh %d : strips %d : offset %d\n", i, j, nLodID, k, pVtxMesh->numStripGroups, pVtxMesh->stripGroupHeaderOffset );

                    for (int m = 0; m < pVtxMesh->numStripGroups; m++) {
                        OptimizedModel::StripGroupHeader_t* pStripGroup = pVtxMesh->pStripGroup(m);
                        OptimizedModel::StripGroupHeader_t* pNewStripGroup = pNewVtxMesh->pStripGroup(m);

                        // int delta = ((byte *)pStripGroup - (byte *)pVtxHdr) - ((byte *)pNewStripGroup - (byte *)pNewVtxHdr);

                        pNewStripGroup->numVerts = pStripGroup->numVerts;
                        pNewStripGroup->vertOffset = (pData - (uint8_t*) pNewStripGroup);
                        int size = pNewStripGroup->numVerts * sizeof(OptimizedModel::Vertex_t);
                        memcpy(pData, pStripGroup->pVertex(0), size);
                        pData += size;

                        pNewStripGroup->numIndices = pStripGroup->numIndices;
                        pNewStripGroup->indexOffset = (pData - (uint8_t*) pNewStripGroup);
                        size = pNewStripGroup->numIndices * sizeof(unsigned short);
                        memcpy(pData, pStripGroup->pIndex(0), size);
                        pData += size;

                        pNewStripGroup->numStrips = pStripGroup->numStrips;
                        pNewStripGroup->stripOffset = (pData - (uint8_t*) pNewStripGroup);
                        size = pNewStripGroup->numStrips * sizeof(OptimizedModel::StripHeader_t);
                        pData += size;

                        pNewStripGroup->flags = pStripGroup->flags;

                        /*
						printf("\tnumVerts %d %d :\n", pStripGroup->numVerts, pStripGroup->vertOffset );
						printf("\tnumIndices %d %d :\n", pStripGroup->numIndices, pStripGroup->indexOffset );
						printf("\tnumStrips %d %d :\n", pStripGroup->numStrips, pStripGroup->stripOffset );
						*/

                        for (int n = 0; n < pStripGroup->numStrips; n++) {
                            OptimizedModel::StripHeader_t* pStrip = pStripGroup->pStrip(n);
                            OptimizedModel::StripHeader_t* pNewStrip = pNewStripGroup->pStrip(n);

                            pNewStrip->numIndices = pStrip->numIndices;
                            pNewStrip->indexOffset = pStrip->indexOffset;

                            pNewStrip->numVerts = pStrip->numVerts;
                            pNewStrip->vertOffset = pStrip->vertOffset;

                            pNewStrip->numBones = pStrip->numBones;
                            pNewStrip->flags = pStrip->flags;

                            pNewStrip->numBoneStateChanges = pStrip->numBoneStateChanges;
                            pNewStrip->boneStateChangeOffset = (pData - (uint8_t*) pNewStrip);
                            size = pNewStrip->numBoneStateChanges * sizeof(OptimizedModel::BoneStateChangeHeader_t);
                            memcpy(pData, pStrip->pBoneStateChange(0), size);
                            pData += size;

                            /*
							printf("\t\tnumIndices %d %d :\n", pNewStrip->numIndices, pNewStrip->indexOffset );
							printf("\t\tnumVerts %d %d :\n", pNewStrip->numVerts, pNewStrip->vertOffset );
							printf("\t\tnumBoneStateChanges %d %d :\n", pNewStrip->numBoneStateChanges, pNewStrip->boneStateChangeOffset );
							*/
                            // printf("(%d)\n", delta );
                        }
                        // printf("(%d)\n", delta );
                    }
                }
            }
        }
    }

    // Iterate over every body part...
    for (i = 0; i < pStudioHdr->numbodyparts; i++) {
        mstudiobodyparts_t* pBodyPart = pStudioHdr->pBodypart(i);

        // Iterate over every submodel...
        for (j = 0; j < pBodyPart->nummodels; ++j) {
            // link previous LODs to higher LODs
            for (nLodID = 0; nLodID < rootLOD; nLodID++) {
                OptimizedModel::ModelLODHeader_t* pVtxLOD = pVtxHdr->pBodyPart(i)->pModel(j)->pLOD(nLodID);
                OptimizedModel::ModelLODHeader_t* pRootVtxLOD = pNewVtxHdr->pBodyPart(i)->pModel(j)->pLOD(rootLOD);
                OptimizedModel::ModelLODHeader_t* pNewVtxLOD = pNewVtxHdr->pBodyPart(i)->pModel(j)->pLOD(nLodID);

                pNewVtxLOD->numMeshes = pRootVtxLOD->numMeshes;
                pNewVtxLOD->switchPoint = pVtxLOD->switchPoint;

                int delta = (uint8_t*) pRootVtxLOD - (uint8_t*) pNewVtxLOD;
                pNewVtxLOD->meshOffset = pRootVtxLOD->meshOffset + delta;
            }
        }
    }

    int newLen = pData - (uint8_t*) pNewVtxHdr;
    // printf("len %d : %d\n", len, newLen );

    // pNewVtxHdr->length = newLen;

    if (!g_StudioMdlContext.quiet) {
        std::print("writing {}:\n", fileName);
        std::print("everything ({} bytes)\n", newLen);
    }

    {
        SaveOutputFile(fileName, pNewVtxHdr, newLen);
    }

    free(pNewVtxHdr);

    return true;
}


bool Clamp_RootLOD(studiohdr_t* phdr) {
    vstd::FunctionTimingRecorder _("Write");

    const bool extraData = (phdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) != 0;

    int rootLOD = g_StudioMdlContext.minLod;

    if (rootLOD > g_studiomdl.ScriptLODs.Count() - 1) {
        rootLOD = g_studiomdl.ScriptLODs.Count() - 1;
    }

    if (rootLOD == 0) {
        return true;
    }

    // shift the files so that g_minLod is the root LOD
    Clamp_MDL_LODS(OutputModelPath(".mdl").c_str(), rootLOD);
    Clamp_VVD_LODS(OutputModelPath(".vvd").c_str(), rootLOD, extraData);

    for (uint32_t i = 0; i < 1; i++) {
        // fixup ???.vtx
        Clamp_VTX_LODS(OutputModelPath(".dx90.vtx").c_str(), rootLOD, phdr);
    }

    return true;
}
