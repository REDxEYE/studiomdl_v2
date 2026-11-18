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

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <optional>
#include <span>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "common/cmdlib.h"
#include "common/scriplib.h"
#include "studio.h"
#include "interfaces/interfaces.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/optimize.h"
#include "mdlobjects/dmeboneflexdriver.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/perfstats.h"
#include "studiomdl/output_files.hpp"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl/studiomdl_helpers.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/buffer.hpp"
#include "tier1_v2/math_utils.hpp"

// WriteModelFiles builds the .mdl/.ani/.vvd/.vtx data in vstd::Buffer objects; every section writer appends to the
// buffer and stores reallocation-safe vstd::BufferPtr positions instead of raw pointers.

bool FixupToSortedLODVertexes(StudioMdlState &ctx, studiohdr_t* pStudioHdr);

bool Clamp_RootLOD(StudioMdlState &ctx, const studiohdr_t* phdr);

void WriteSeqKeyValues(vstd::Buffer &buffer, vstd::BufferPtr<mstudioseqdesc_t> pseqdesc, std::string &pKeyValue);

//-----------------------------------------------------------------------------
// Purpose: stringtable is the string table of the file being written (a local of the writer that owns it).
// An entry locates its base and offset field as byte offsets inside the buffer that receives the strings, so the
// buffer may reallocate before WriteStringTable.
//-----------------------------------------------------------------------------

struct stringtable_t {
    const char* string;
    int dupindex; // earlier entry with the same string, -1 when this is the first
    vstd::Buffer* buffer;
    size_t base; // what the stored offset is relative to
    size_t field; // the int receiving the offset
    size_t addr; // where the string was written
};

using StringTable = std::vector<stringtable_t>;

static void BeginStringTable(StringTable &strings) {
    strings.assign(1, {"", -1, nullptr, 0, 0, 0});
}

//-----------------------------------------------------------------------------
// Purpose: add a string to the string table.
//			Keep track of fixup locations
//-----------------------------------------------------------------------------
template<typename B>
static void AddToStringTable(StringTable &strings, const vstd::BufferPtr<B> &base, const vstd::BufferPtr<int> &field,
                             const char* string) {
    if (!string) {
        string = "";
    }

    const auto dup = std::ranges::find_if(strings, [string](const stringtable_t &entry) {
        return strcmp(string, entry.string) == 0;
    });
    const int dupindex = dup == strings.end() ? -1 : static_cast<int>(dup - strings.begin());
    strings.push_back({string, dupindex, base.buffer(), base.offset(), field.offset(), 0});
}

//-----------------------------------------------------------------------------
// Purpose: Write out stringtable
//			fixup local pointers
//-----------------------------------------------------------------------------
static void WriteStringTable(StringTable &strings, vstd::Buffer &buffer) {
    vstd::FunctionTimingRecorder _("Write");
    const auto emit = [&buffer](const char* string) {
        const size_t size = strlen(string) + 1;
        const auto position = buffer.allocate<char>(size);
        memcpy(position.get(), string, size);
        return position.offset();
    };

    // force null at first address
    strings[0].addr = emit("");

    // save all the rest
    for (size_t i = 1; i < strings.size(); i++) {
        stringtable_t &entry = strings[i];
        Assert(entry.buffer == &buffer);
        // keep track of the address in case of duplication; an already stored string is reused
        entry.addr = entry.dupindex == -1 ? emit(entry.string) : strings[entry.dupindex].addr;
        // offset relative to the local base; memcpy: the field may be unaligned (packed .vtx structs)
        const int offset = buffer.ptr_at<uint8_t>(entry.addr).offset_from<int>(buffer.ptr_at<uint8_t>(entry.base));
        memcpy(buffer.ptr_at<uint8_t>(entry.field).get(), &offset, sizeof(offset));
    }
    buffer.align(4);
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

    pProc->m_nTargetCount = std::ssize(pConstraint->m_targets);
    const auto pTarget = buffer.allocate<mstudioconstrainttarget_t>(pProc->m_nTargetCount);
    pProc->m_nTargetIndex = pTarget.template offset_from<int>(pProc);
    buffer.align(4);

    for (int j = 0; j < std::ssize(pConstraint->m_targets); ++j) {
        s_constraintbonetarget_t &target = pConstraint->m_targets[j];
        pTarget[j].m_nBone = target.m_nBone;
        pTarget[j].m_flWeight = target.m_flWeight;
        pTarget[j].m_vOffset = target.m_vOffset;
        pTarget[j].m_qOffset = target.m_qOffset;
    }

    return pProc;
}


static void WriteBoneInfo(StudioMdlState &ctx, StringTable &strings, vstd::Buffer &buffer,
                          const vstd::BufferPtr<studiohdr_t> phdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j, k;

    // save bone info
    const auto pbones = buffer.allocate<mstudiobone_t>(std::ssize(ctx.bones.bonetable));
    phdr->numbones = std::ssize(ctx.bones.bonetable);
    phdr->boneindex = pbones.offset_as<int>();

    char* pSurfacePropName = GetDefaultSurfaceProp(ctx);
    AddToStringTable(strings, phdr, phdr.field(&studiohdr_t::surfacepropindex), pSurfacePropName);
    phdr->contents = GetDefaultContents(ctx);

    for (i = 0; i < std::ssize(ctx.bones.bonetable); i++) {
        AddToStringTable(strings, pbones + i, (pbones + i).field(&mstudiobone_t::sznameindex),
                         ctx.bones.bonetable[i].name);
        pbones[i].parent = ctx.bones.bonetable[i].parent;
        pbones[i].flags = ctx.bones.bonetable[i].flags;
        pbones[i].procindex = 0;
        pbones[i].physicsbone = ctx.bones.bonetable[i].physicsBoneIndex;
        pbones[i].pos = ctx.bones.bonetable[i].pos;
        pbones[i].rot = ctx.bones.bonetable[i].rot;
        pbones[i].posscale = ctx.bones.bonetable[i].posscale;
        pbones[i].rotscale = ctx.bones.bonetable[i].rotscale;
        MatrixInvert(ctx.bones.bonetable[i].boneToPose, pbones[i].poseToBone);
        pbones[i].qAlignment = ctx.bones.bonetable[i].qAlignment;

        AngleQuaternion(RadianEuler(ctx.bones.bonetable[i].rot[0], ctx.bones.bonetable[i].rot[1],
                                    ctx.bones.bonetable[i].rot[2]),
                        pbones[i].quat);
        QuaternionAlign(pbones[i].qAlignment, pbones[i].quat, pbones[i].quat);

        pSurfacePropName = GetSurfaceProp(ctx, ctx.bones.bonetable[i].name);
        AddToStringTable(strings, pbones + i, (pbones + i).field(&mstudiobone_t::surfacepropidx), pSurfacePropName);
        pbones[i].contents = GetContents(ctx, ctx.bones.bonetable[i].name);
    }

    buffer.align(4);

    // save procedural bone info
    if (std::ssize(ctx.bones.axisinterpbonemap)) {
        auto proc_bone = buffer.allocate<mstudioaxisinterpbone_t>(std::ssize(ctx.bones.axisinterpbonemap));
        for (i = 0; i < std::ssize(ctx.bones.axisinterpbonemap); i++) {
            j = ctx.bones.axisinterpbonemap[i];
            k = ctx.bones.axisinterpbones[j].bone;
            pbones[k].procindex = (proc_bone + i).offset_from<int>(pbones + k);
            pbones[k].proctype = ProcBoneType::AxisInterp;
            // printf("bone %d %d\n", j, pbone[k].procindex );
            proc_bone[i].control = ctx.bones.axisinterpbones[j].control;
            proc_bone[i].axis = ctx.bones.axisinterpbones[j].axis;
            for (k = 0; k < 6; k++) {
                VectorCopy(ctx.bones.axisinterpbones[j].pos[k], proc_bone[i].pos[k]);
                proc_bone[i].quat[k] = ctx.bones.axisinterpbones[j].quat[k];
            }
        }
        buffer.align(4);
    }

    if (std::ssize(ctx.bones.quatinterpbonemap)) {
        auto proc_bone = buffer.allocate<mstudioquatinterpbone_t>(std::ssize(ctx.bones.quatinterpbonemap));

        for (i = 0; i < std::ssize(ctx.bones.quatinterpbonemap); i++) {
            j = ctx.bones.quatinterpbonemap[i];
            k = ctx.bones.quatinterpbones[j].bone;
            pbones[k].procindex = (proc_bone + i).offset_from<int>(pbones + k);
            pbones[k].proctype = ProcBoneType::QuatInterp;
            // printf("bone %d %d\n", j, pbone[k].procindex );
            proc_bone[i].control = ctx.bones.quatinterpbones[j].control;

            // the count first: it sizes the allocation, which may move proc_bone's memory
            proc_bone[i].numtriggers = ctx.bones.quatinterpbones[j].numtriggers;
            auto trigger = buffer.allocate<mstudioquatinterpinfo_t>(proc_bone[i].numtriggers);
            proc_bone[i].triggerindex = trigger.offset_from<int>(proc_bone + i);

            for (k = 0; k < proc_bone[i].numtriggers; k++) {
                trigger[k].inv_tolerance = 1.0 / ctx.bones.quatinterpbones[j].tolerance[k];
                trigger[k].trigger = ctx.bones.quatinterpbones[j].trigger[k];
                trigger[k].pos = ctx.bones.quatinterpbones[j].pos[k];
                trigger[k].quat = ctx.bones.quatinterpbones[j].quat[k];
            }
        }
        buffer.align(4);
    }

    if (std::ssize(ctx.bones.jigglebonemap)) {
        auto jiggleInfo = buffer.allocate<mstudiojigglebone_t>(std::ssize(ctx.bones.jigglebonemap));

        for (i = 0; i < std::ssize(ctx.bones.jigglebonemap); i++) {
            j = ctx.bones.jigglebonemap[i];
            k = ctx.bones.jigglebones[j].bone;
            pbones[k].procindex = (jiggleInfo + i).offset_from<int>(pbones + k);
            pbones[k].proctype = ProcBoneType::Jiggle;

            jiggleInfo[i] = ctx.bones.jigglebones[j].data;
        }
        buffer.align(4);
    }

    // write aim at bones
    if (std::ssize(ctx.bones.aimatbonemap)) {
        auto pProc = buffer.allocate<mstudioaimatbone_t>(std::ssize(ctx.bones.aimatbonemap));
        for (i = 0; i < std::ssize(ctx.bones.aimatbonemap); i++) {
            j = ctx.bones.aimatbonemap[i];
            k = ctx.bones.aimatbones[j].bone;
            pbones[k].procindex = (pProc + i).offset_from<int>(pbones + k);
            pbones[k].proctype = ctx.bones.aimatbones[j].aimAttach == -1
                                     ? ProcBoneType::AimAtBone
                                     : ProcBoneType::AimAtAttach;
            pProc[i].parent = ctx.bones.aimatbones[j].parent;
            pProc[i].aim = ctx.bones.aimatbones[j].aimAttach == -1
                               ? ctx.bones.aimatbones[j].aimBone
                               : ctx.bones.aimatbones[j].aimAttach;
            pProc[i].aimvector = ctx.bones.aimatbones[j].aimvector;
            pProc[i].upvector = ctx.bones.aimatbones[j].upvector;
            pProc[i].basepos = ctx.bones.aimatbones[j].basepos;
        }
        buffer.align(4);
    }

    // Write constraint bones
    if (std::ssize(ctx.bones.constraintBones) > 0) {
        for (int i = 0; i < std::ssize(ctx.bones.constraintBones); ++i) {
            CConstraintBoneBase* pConstraintBone = ctx.bones.constraintBones[i];
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

    // map ctx.bonecontroller to bones
    for (i = 0; i < std::ssize(ctx.bones.bonetable); i++) {
        for (j = 0; j < 6; j++) {
            pbones[i].bonecontroller[j] = -1;
        }
    }

    for (i = 0; i < ctx.bones.bonecontrollers.size(); i++) {
        j = ctx.bones.bonecontrollers[i].bone;
        switch (ctx.bones.bonecontrollers[i].type & StudioMotionFlags::Types) {
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
                MdlError("unknown ctx.bonecontroller type\n");
        }
    }

    // save ctx.bonecontroller info
    const auto pbonecontroller = buffer.allocate<mstudiobonecontroller_t>(ctx.bones.bonecontrollers.size());
    phdr->numbonecontrollers = ctx.bones.bonecontrollers.size();
    phdr->bonecontrollerindex = pbonecontroller.offset_as<int>();

    for (i = 0; i < ctx.bones.bonecontrollers.size(); i++) {
        pbonecontroller[i].bone = ctx.bones.bonecontrollers[i].bone;
        pbonecontroller[i].inputfield = ctx.bones.bonecontrollers[i].inputfield;
        pbonecontroller[i].type = ctx.bones.bonecontrollers[i].type;
        pbonecontroller[i].start = ctx.bones.bonecontrollers[i].start;
        pbonecontroller[i].end = ctx.bones.bonecontrollers[i].end;
    }
    buffer.align(4);

    // save attachment info
    const auto pattachment = buffer.allocate<mstudioattachment_t>(ctx.bones.attachments.size());
    phdr->numlocalattachments = ctx.bones.attachments.size();
    phdr->localattachmentindex = pattachment.offset_as<int>();

    for (i = 0; i < ctx.bones.attachments.size(); i++) {
        pattachment[i].localbone = ctx.bones.attachments[i].bone;
        AddToStringTable(strings, pattachment + i, (pattachment + i).field(&mstudioattachment_t::sznameindex),
                         ctx.bones.attachments[i].name.c_str());
        MatrixCopy(ctx.bones.attachments[i].local, pattachment[i].local);
        pattachment[i].flags = ctx.bones.attachments[i].flags;
    }
    buffer.align(4);

    // save hitbox sets
    phdr->numhitboxsets = ctx.bones.hitboxsets.size();

    // Remember start spot
    auto hitboxset = buffer.allocate<mstudiohitboxset_t>(phdr->numhitboxsets);
    phdr->hitboxsetindex = hitboxset.offset_as<int>();
    buffer.align(4);

    for (int s = 0; s < ctx.bones.hitboxsets.size(); s++, hitboxset++) {
        s_hitboxset* set = &ctx.bones.hitboxsets[s];

        AddToStringTable(strings, hitboxset, hitboxset.field(&mstudiohitboxset_t::sznameindex), set->hitboxsetname);

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
            AddToStringTable(strings, pbbox + i, (pbbox + i).field(&mstudiobbox_t::szhitboxnameindex),
                             hitbox.hitboxname);
        }

        buffer.align(4);
    }

    const auto boneTable = buffer.allocate<uint8_t>(phdr->numbones);
    phdr->bonetablebynameindex = boneTable.offset_as<int>();

    // a table in bone order, sorted by bone name
    const std::span<uint8_t> sortedBones(boneTable.get(), phdr->numbones);
    std::iota(sortedBones.begin(), sortedBones.end(), uint8_t{0});
    std::ranges::stable_sort(sortedBones, [&ctx](const uint8_t a, const uint8_t b) {
        return strcmpi(ctx.bones.bonetable[a].name, ctx.bones.bonetable[b].name) < 0;
    });
    buffer.align(4);
}

// load a preexisting model to remember its sequence names and indices
static void LoadPreexistingSequenceOrder(StudioMdlState &ctx, const std::string &fileName,
                                         std::vector<std::string> &preexistingSequences) {
    preexistingSequences.clear();

    if (!FileExists(fileName.c_str())) {
        if (ctx.options.errorOnSeqRemapFail)
            MdlError(
                "This model requires a sequence remapping match. Please sync to the latest model on disk before recompiling.\n");
        return;
    }

    MdlMessage("Loading preexisting model: {}\n", fileName);

    auto data = LoadOutputFile(ctx, fileName);
    studiohdr_t* pStudioHdr = data && data->size() >= sizeof(studiohdr_t)
                                  ? data->reinterpret_at<studiohdr_t>(0)
                                  : nullptr;

    if (pStudioHdr && pStudioHdr->SequencesAvailable()) {
        MdlMessage("   Found {} preexisting sequences.\n", pStudioHdr->GetNumSeq());

        for (int i = 0; i < pStudioHdr->GetNumSeq(); i++) {
            preexistingSequences.emplace_back(pStudioHdr->pSeqdesc(i).pszLabel());
        }
    }
    else if (ctx.options.modelIntentionallyHasZeroSequences) {
        // some models like scaffolds, intentionally don't have input sequences. Not sure if this is the best way to allow this exception.
    }
    else if (ctx.options.errorOnSeqRemapFail) {
        MdlError("Zero-size file or no sequences. This model requires a sequence remapping match.\n");
    }
    else {
        MdlWarning("Zero-size file or no sequences.\n");
    }
}

static void WriteSequenceInfo(StudioMdlState &ctx, StringTable &strings, vstd::Buffer &buffer,
                              const vstd::BufferPtr<studiohdr_t> phdr,
                              const std::vector<std::string> &preexistingSequences) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j, k;

    // write models to disk with this flag set false. This will force
    // the sequences to be indexed by activity whenever the ctx.body.model is loaded
    // from disk.
    phdr->activitylistversion = 0;
    phdr->eventsindexed = 0;

    // save ctx.anim.sequence info
    auto pseqdesc = buffer.allocate<mstudioseqdesc_t>(std::ssize(ctx.anim.sequence));
    phdr->numlocalseq = std::ssize(ctx.anim.sequence);
    phdr->localseqindex = pseqdesc.offset_as<int>();

    bool bErrors = false;


    // build a table to remap new sequence indices to match the preexisting model
    bool bUseSeqOrderRemapping = false;
    std::array<int, MAXSTUDIOSEQUENCES> nSeqOrderRemappingTable;
    nSeqOrderRemappingTable.fill(-1);

    bool bAllowSequenceRemoval = false;
    const int numPreexisting = static_cast<int>(preexistingSequences.size());

    if (numPreexisting) {
        if (std::ssize(ctx.anim.sequence) < numPreexisting && !bAllowSequenceRemoval) {
            MdlMessage("\n");
            MdlWarning(
                "This model has fewer sequences than its predecessor.\nPlease confirm sequence deletion: [y/n] ");
            int nInput = 0;
            do { nInput = getchar(); } while (nInput != 'y' && nInput != 'n');

            if (nInput == 'n') {
                MdlError("Model contains fewer sequences than its predecessor!\n");
            }
            bAllowSequenceRemoval = true;
        }

        {
            MdlMessage("Building sequence index remapping table...\n");

            std::vector<int> vecNewIndices;

            // map current sequences to their old indices
            for (i = 0; i < std::ssize(ctx.anim.sequence); i++) {
                const auto old = std::ranges::find(preexistingSequences, ctx.anim.sequence[i].name.view());
                if (old != preexistingSequences.end()) {
                    nSeqOrderRemappingTable[old - preexistingSequences.begin()] = i;
                }
                else {
                    if (i < numPreexisting) {
                        MdlMessage("  Found new sequence \"{}\" using index of old sequence \"{}\".\n",
                                   ctx.anim.sequence[i].name, preexistingSequences[i]);
                    }
                    else {
                        MdlMessage("  Found new sequence \"{}\".\n", ctx.anim.sequence[i].name);
                    }

                    vecNewIndices.push_back(i);
                }
            }

            // slot new sequences into unused indices, in order
            auto newIndex = vecNewIndices.begin();
            for (int &slot: nSeqOrderRemappingTable) {
                if (newIndex == vecNewIndices.end())
                    break;
                if (slot == -1)
                    slot = *newIndex++;
            }

            // verify no indices are undefined
            for (i = 0; i < std::ssize(ctx.anim.sequence); i++) {
                if (nSeqOrderRemappingTable[i] == -1) {
                    if (bAllowSequenceRemoval) {
                        do {
                            for (int nB = i; nB < numPreexisting; nB++) {
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
                MdlMessage("Sequence indices need re-ordering.\n");
            }
            else {
                MdlMessage("No re-ordering required.\n");
            }
        }
    }

    // build an inverted remapping table so autolayer sequence indices can find their sources later
    std::array<int, MAXSTUDIOSEQUENCES> nSeqOrderRemappingTableInv{};
    if (bUseSeqOrderRemapping) {
        for (i = 0; i < MAXSTUDIOSEQUENCES; i++) {
            // unused slots stay -1 and map nothing
            if (nSeqOrderRemappingTable[i] >= 0)
                nSeqOrderRemappingTableInv[nSeqOrderRemappingTable[i]] = i;
        }
    }

    int m;
    for (m = 0; m < std::ssize(ctx.anim.sequence); m++, pseqdesc++) {
        if (bUseSeqOrderRemapping) {
            i = nSeqOrderRemappingTable[m];
            if (i != m) {
                MdlMessage("   Remapping sequence {} to index {} ({}) to retain existing order.\n", i, m,
                           ctx.anim.sequence[i].name);
            }
        }
        else {
            i = m;
        }

        AddToStringTable(strings, pseqdesc, pseqdesc.field(&mstudioseqdesc_t::szlabelindex),
                         ctx.anim.sequence[i].name);
        AddToStringTable(strings, pseqdesc, pseqdesc.field(&mstudioseqdesc_t::szactivitynameindex),
                         ctx.anim.sequence[i].activityname);

        // offset from this descriptor back to the header
        pseqdesc->baseptr = phdr.offset_from<int>(pseqdesc);

        pseqdesc->flags = ctx.anim.sequence[i].flags;

        pseqdesc->numblends = ctx.anim.sequence[i].numblends;
        pseqdesc->groupsize[0] = ctx.anim.sequence[i].groupsize[0];
        pseqdesc->groupsize[1] = ctx.anim.sequence[i].groupsize[1];

        pseqdesc->paramindex[0] = ctx.anim.sequence[i].paramindex[0];
        pseqdesc->paramstart[0] = ctx.anim.sequence[i].paramstart[0];
        pseqdesc->paramend[0] = ctx.anim.sequence[i].paramend[0];
        pseqdesc->paramindex[1] = ctx.anim.sequence[i].paramindex[1];
        pseqdesc->paramstart[1] = ctx.anim.sequence[i].paramstart[1];
        pseqdesc->paramend[1] = ctx.anim.sequence[i].paramend[1];

        if (ctx.anim.sequence[i].groupsize[0] > 1 || ctx.anim.sequence[i].groupsize[1] > 1) {
            // save posekey values
            auto posekey = buffer.allocate<float>(pseqdesc->groupsize[0] + pseqdesc->groupsize[1]);
            pseqdesc->posekeyindex = posekey.offset_from<int>(pseqdesc);
            for (j = 0; j < pseqdesc->groupsize[0]; j++) {
                *(posekey++) = j < std::ssize(ctx.anim.sequence[i].param0) ? ctx.anim.sequence[i].param0[j] : 0.0f;
            }
            for (j = 0; j < pseqdesc->groupsize[1]; j++) {
                *(posekey++) = j < std::ssize(ctx.anim.sequence[i].param1) ? ctx.anim.sequence[i].param1[j] : 0.0f;
            }
        }

        // pseqdesc->motiontype	= ctx.anim.sequence[i].motiontype;
        // pseqdesc->motionbone	= 0; // ctx.anim.sequence[i].motionbone;
        // VectorCopy( ctx.anim.sequence[i].linearmovement, pseqdesc->linearmovement );

        pseqdesc->activity = ctx.anim.sequence[i].activity;
        pseqdesc->actweight = ctx.anim.sequence[i].actweight;

        pseqdesc->bbmin = ctx.anim.sequence[i].bmin;
        pseqdesc->bbmax = ctx.anim.sequence[i].bmax;

        pseqdesc->fadeintime = ctx.anim.sequence[i].fadeintime;
        pseqdesc->fadeouttime = ctx.anim.sequence[i].fadeouttime;

        pseqdesc->localentrynode = ctx.anim.sequence[i].entrynode;
        pseqdesc->localexitnode = ctx.anim.sequence[i].exitnode;
        //pseqdesc->entryphase	= ctx.anim.sequence[i].entryphase;
        //pseqdesc->exitphase		= ctx.anim.sequence[i].exitphase;
        pseqdesc->nodeflags = ctx.anim.sequence[i].nodeflags;

        // save events
        // the count first: it sizes the allocation
        pseqdesc->numevents = ctx.anim.sequence[i].events.size();
        const auto pevent = buffer.allocate<mstudioevent_t>(pseqdesc->numevents);
        pseqdesc->eventindex = pevent.offset_from<int>(pseqdesc);
        for (j = 0; j < ctx.anim.sequence[i].events.size(); j++) {
            k = ctx.anim.sequence[i].panim[0][0]->numframes - 1;

            if (ctx.anim.sequence[i].events[j].frame <= k)
                pevent[j].cycle = ctx.anim.sequence[i].events[j].frame / ((float) k);
            else if (k == 0 && ctx.anim.sequence[i].events[j].frame == 0)
                pevent[j].cycle = 0;
            else {
                MdlWarning("Event {} (frame {}) out of range in {}\n", ctx.anim.sequence[i].events[j].event,
                           ctx.anim.sequence[i].events[j].frame, ctx.anim.sequence[i].name);
                bErrors = true;
            }

            //Adrian - Remove me once we phase out the old event system.
            if (V_isdigit(ctx.anim.sequence[i].events[j].eventname[0])) {
                pevent[j].event = atoi(ctx.anim.sequence[i].events[j].eventname);
                pevent[j].type = StudioEventType::OldStyle;
                pevent[j].szeventindex = 0;
            }
            else {
                AddToStringTable(strings, pevent + j, (pevent + j).field(&mstudioevent_t::szeventindex),
                                 ctx.anim.sequence[i].events[j].eventname);
                pevent[j].type = StudioEventType::NewStyle;
            }

            // the buffer is zero-filled, which terminates the copied text
            const std::string_view options = ctx.anim.sequence[i].events[j].options.view();
            options.copy(pevent[j].options, sizeof(pevent[j].options) - 1);
        }
        buffer.align(4);

        // save ikrules
        pseqdesc->numikrules = ctx.anim.sequence[i].numikrules;

        // save autolayers
        pseqdesc->numautolayers = ctx.anim.sequence[i].autolayers.size();
        const auto pautolayer = buffer.allocate<mstudioautolayer_t>(pseqdesc->numautolayers);
        pseqdesc->autolayerindex = pautolayer.offset_from<int>(pseqdesc);
        for (j = 0; j < ctx.anim.sequence[i].autolayers.size(); j++) {
            pautolayer[j].iSequence = ctx.anim.sequence[i].autolayers[j].sequence;
            pautolayer[j].iPose = ctx.anim.sequence[i].autolayers[j].pose;
            pautolayer[j].flags = ctx.anim.sequence[i].autolayers[j].flags;

            // autolayer indices are stored by index, so remap them now using the invertex lookup table
            if (bUseSeqOrderRemapping) {
                int nRemapAutoLayer = nSeqOrderRemappingTableInv[pautolayer[j].iSequence];
                if (nRemapAutoLayer != pautolayer[j].iSequence) {
                    MdlMessage("       Autolayer remapping index {} to {}.\n", pautolayer[j].iSequence,
                               nRemapAutoLayer);
                    pautolayer[j].iSequence = nRemapAutoLayer;
                }
            }

            if (!HasFlag(pautolayer[j].flags, AutoLayerFlags::Pose)) {
                pautolayer[j].start = ctx.anim.sequence[i].autolayers[j].start / (
                                          ctx.anim.sequence[i].panim[0][0]->numframes - 1);
                pautolayer[j].peak = ctx.anim.sequence[i].autolayers[j].peak / (
                                         ctx.anim.sequence[i].panim[0][0]->numframes - 1);
                pautolayer[j].tail = ctx.anim.sequence[i].autolayers[j].tail / (
                                         ctx.anim.sequence[i].panim[0][0]->numframes - 1);
                pautolayer[j].end = ctx.anim.sequence[i].autolayers[j].end / (
                                        ctx.anim.sequence[i].panim[0][0]->numframes - 1);
            }
            else {
                pautolayer[j].start = ctx.anim.sequence[i].autolayers[j].start;
                pautolayer[j].peak = ctx.anim.sequence[i].autolayers[j].peak;
                pautolayer[j].tail = ctx.anim.sequence[i].autolayers[j].tail;
                pautolayer[j].end = ctx.anim.sequence[i].autolayers[j].end;
            }
        }


        // save boneweights
        // look up previous sequence weights and try to find a match
        size_t weightOffset = 0; // buffer offset of the last weight list compared
        j = 0;
        for (k = 0; k < m; k++) {
            j = 0;
            const auto prevseqdesc = pseqdesc + (k - m);
            const auto prevWeights = static_cast<size_t>(static_cast<std::ptrdiff_t>(prevseqdesc.offset()) +
                                                         prevseqdesc->weightlistindex);
            // only check newer boneweights than the last one
            if (prevWeights > weightOffset) {
                weightOffset = prevWeights;
                for (j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
                    // we're not walking the linear sequence list if we're remapping, so we need to remap this check
                    int nRemap = k;
                    if (bUseSeqOrderRemapping)
                        nRemap = nSeqOrderRemappingTable[k];

                    if (ctx.anim.sequence[i].weight[j] != ctx.anim.sequence[nRemap].weight[j])
                        break;
                }
                if (j == std::ssize(ctx.bones.bonetable))
                    break;
            }
        }

        // check to see if all the bones matched
        if (j < std::ssize(ctx.bones.bonetable)) {
            // allocate new block
            const auto pnewweight = buffer.allocate<float>(std::ssize(ctx.bones.bonetable));
            pseqdesc->weightlistindex = pnewweight.offset_from<int>(pseqdesc);
            for (j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
                pnewweight[j] = ctx.anim.sequence[i].weight[j];
            }
        }
        else {
            // use previous boneweight
            pseqdesc->weightlistindex = buffer.ptr_at<float>(weightOffset).offset_from<int>(pseqdesc);
        }


        // save iklocks
        pseqdesc->numiklocks = ctx.anim.sequence[i].iklocks.size();
        auto piklock = buffer.allocate<mstudioiklock_t>(pseqdesc->numiklocks);
        pseqdesc->iklockindex = piklock.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < pseqdesc->numiklocks; j++) {
            piklock->chain = ctx.anim.sequence[i].iklocks[j].chain;
            piklock->flPosWeight = ctx.anim.sequence[i].iklocks[j].flPosWeight;
            piklock->flLocalQWeight = ctx.anim.sequence[i].iklocks[j].flLocalQWeight;
            piklock++;
        }

        // Write animation blend parameters
        const auto blends = buffer.allocate<short>(
            ctx.anim.sequence[i].groupsize[0] * ctx.anim.sequence[i].groupsize[1]);
        pseqdesc->animindexindex = blends.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < ctx.anim.sequence[i].groupsize[0]; j++) {
            for (k = 0; k < ctx.anim.sequence[i].groupsize[1]; k++) {
                // height value * width of row + width value
                int offset = k * ctx.anim.sequence[i].groupsize[0] + j;

                if (ctx.anim.sequence[i].panim[j][k]) {
                    int animindex = ctx.anim.sequence[i].panim[j][k]->index;

                    Assert(animindex >= 0 && animindex < SHRT_MAX);

                    blends[offset] = (short) animindex;
                }
                else {
                    blends[offset] = 0;
                }
            }
        }

        // Write cycle overrides
        pseqdesc->cycleposeindex = ctx.anim.sequence[i].cycleposeindex;

        WriteSeqKeyValues(buffer, pseqdesc, ctx.anim.sequence[i].KeyValue);

        // Write activity modifiers
        pseqdesc->numactivitymodifiers = ctx.anim.sequence[i].numactivitymodifiers;
        const auto pactivitymodifier = buffer.allocate<mstudioactivitymodifier_t>(pseqdesc->numactivitymodifiers);
        pseqdesc->activitymodifierindex = pactivitymodifier.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < pseqdesc->numactivitymodifiers; j++) {
            AddToStringTable(strings, pactivitymodifier + j,
                             (pactivitymodifier + j).field(&mstudioactivitymodifier_t::sznameindex),
                             ctx.anim.sequence[i].activitymodifier[j].name);
        }


        // save animtags
        pseqdesc->numanimtags = ctx.anim.sequence[i].animtags.size();
        const auto panimtag = buffer.allocate<mstudioanimtag_t>(pseqdesc->numanimtags);
        pseqdesc->animtagindex = panimtag.offset_from<int>(pseqdesc);
        buffer.align(4);

        for (j = 0; j < ctx.anim.sequence[i].animtags.size(); j++) {
            panimtag[j].cycle = ctx.anim.sequence[i].animtags[j].cycle;
            AddToStringTable(strings, panimtag + j, (panimtag + j).field(&mstudioanimtag_t::sztagindex),
                             ctx.anim.sequence[i].animtags[j].tagname);
        }

        if (HasFlag(ctx.anim.sequence[i].flags, StudioSequenceFlags::RootXform)) {
            int bone = findGlobalBone(ctx, ctx.anim.sequence[i].rootDriverBoneName);
            if (bone != -1) {
                pseqdesc->rootDriverIndex = bone;
            }
            else {
                MdlError("unable to find bone {}\n", ctx.anim.sequence[i].rootDriverBoneName);
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
    auto pxnodename = buffer.allocate<int>(ctx.anim.xnodenames.size());
    phdr->localnodenameindex = pxnodename.offset_as<int>();
    buffer.align(4);
    for (i = 0; i < ctx.anim.xnodenames.size(); i++) {
        AddToStringTable(strings, phdr, pxnodename, ctx.anim.xnodenames[i].c_str());
        // printf("%d : %s\n", i, ctx.xnodename[i+1] );
        pxnodename++;
    }

    auto ptransition = buffer.allocate<uint8_t>(ctx.anim.xnodenames.size() * ctx.anim.xnodenames.size());
    phdr->numlocalnodes = ctx.anim.xnodenames.size();
    phdr->localnodeindex = ptransition.offset_as<int>();
    buffer.align(4);
    for (i = 0; i < ctx.anim.xnodenames.size(); i++) {
        //		printf("%2d (%12s) : ", i + 1, ctx.xnodename[i+1] );
        for (j = 0; j < ctx.anim.xnodenames.size(); j++) {
            *ptransition++ = ctx.anim.xnodes[i][j];
            //			printf(" %2d", ctx.xnode[i][j] );
        }
        //		printf("\n" );
    }
}

//-----------------------------------------------------------------------------
// Purpose: Stub implementation
// Input  : *group -
//-----------------------------------------------------------------------------

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


// out: the buffer the section is appended to, the .mdl data or the .ani block data
void WriteRLEAnimationData(StudioMdlState &ctx, s_animation_t* srcanim, vstd::Buffer &out, int w) {
    int k, n;

    auto destanim = out.allocate<mstudio_rle_anim_t>();

    destanim->bone = 255;

    vstd::BufferPtr<mstudio_rle_anim_t> prevanim;

    // save animation value info
    for (int j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
        destanim->flags = StudioAnimFlags::None;
        s_compressed_t* psrcdata = &srcanim->anim[w][j];

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
                destanim->flags |= StudioAnimFlags::RawRot2;
            }

            if (psrcdata->num[0] != 0 || psrcdata->num[1] != 0 || psrcdata->num[2] != 0) {
                *out.allocate<Vector48>() = srcanim->sanim[iFrame][j].pos;
                destanim->flags |= StudioAnimFlags::RawPos;
            }
        }
        else {
            // allocate room for rotation ptrs
            const auto rotvptr = out.allocate<mstudioanim_valueptr_t>();

            // skip all position info if there's no animation
            vstd::BufferPtr<mstudioanim_valueptr_t> posvptr;
            if (psrcdata->num[0] != 0 || psrcdata->num[1] != 0 || psrcdata->num[2] != 0) {
                posvptr = out.allocate<mstudioanim_valueptr_t>();
            }

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

void WriteFrameAnimationData(StudioMdlState &ctx, s_animation_t* srcanim, vstd::Buffer &out, int w) {
    // allocate room for header
    const auto destframeanim = out.allocate<mstudio_frame_anim_t>();

    // write flags and constants (the flags follow the header, see mstudio_frame_anim_t::pBoneFlags)
    const auto flag = out.allocate<StudioFrameFlags>(std::ssize(ctx.bones.bonetable));

    out.align(4);

    destframeanim->constantsoffset = out.end_ptr().offset_from<int>(destframeanim);
    int framelength = 0;
    int iFrame = MIN(w * srcanim->sectionframes, srcanim->numframes - 1);

    for (int j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
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
            if (ctx.options.animblockHighRes) {
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
            if (ctx.options.animblockHighRes) {
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
        for (int j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
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
void WriteAnimationData(StudioMdlState &ctx, s_animation_t* srcanim, const vstd::BufferPtr<mstudioanimdesc_t> destanimdesc,
                        vstd::Buffer &local, vstd::Buffer* ext) {
    vstd::FunctionTimingRecorder _("Write");

    for (int w = 0; w < srcanim->numsections; w++) {
        const bool bUseExtData = ext != nullptr && !srcanim->disableAnimblocks &&
                                 !((w * srcanim->sectionframes < srcanim->numNostallFrames) &&
                                   srcanim->isFirstSectionLocal);
        vstd::Buffer &out = bUseExtData ? *ext : local;

        const size_t startSection = out.size();

        // use frameanim if not lowres data
        if (ext != nullptr && !ctx.options.animblockLowRes) {
            srcanim->flags |= StudioSequenceFlags::FrameAnim;
            destanimdesc->flags |= StudioSequenceFlags::FrameAnim;
        }

        if (HasFlag(srcanim->flags, StudioSequenceFlags::FrameAnim)) {
            WriteFrameAnimationData(ctx, srcanim, out, w);
        }
        else {
            WriteRLEAnimationData(ctx, srcanim, out, w);
        }

        const size_t sectionSize = out.size() - startSection;
        if (ctx.anim.animblocksize > 0 && sectionSize > static_cast<size_t>(ctx.anim.animblocksize)) {
            MdlWarning(
                "Single animation \"{}\" is {}. Specificed block size is {}.  Use smaller animations or increase the block size.\n",
                srcanim->name, sectionSize, ctx.anim.animblocksize);
        }

        // write into anim blocks if needed
        if (destanimdesc->sectionindex) {
            if (bUseExtData) {
                if (ctx.anim.animblocks.size() &&
                    static_cast<int64_t>(out.size()) - ctx.anim.animblocks.back().start > ctx.anim.animblocksize) {
                    // advance to next animblock
                    ctx.anim.animblocks.back().end = startSection;
                    ctx.anim.animblocks.emplace_back(s_animblock_t{.start = static_cast<uint32_t>(startSection)});
                }

                destanimdesc->pSection(w)->animblock = ctx.anim.animblocks.size() - 1;
                destanimdesc->pSection(w)->animindex =
                        static_cast<int>(startSection - ctx.anim.animblocks.back().start);
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

        // AddToStringTable( pHierarchy, &pHierarchy->szattachmentindex, srcanim->ikrule[j].attachment );
    }
}


// buffer: the .mdl data. blocks: the .ani data (already holding its header), nullptr when there is no .ani file.
// ctx.anim.animblocks start/end are offsets into blocks. totalframes/totalseconds accumulate the animation lengths.
static void WriteAnimations(StudioMdlState &ctx, StringTable &strings, vstd::Buffer &buffer, vstd::Buffer* blocks,
                            const vstd::BufferPtr<studiohdr_t> phdr, int &totalframes, float &totalseconds) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j;

    // save animations
    const auto panimdesc = buffer.allocate<mstudioanimdesc_t>(ctx.anim.animations.size());
    phdr->numlocalanim = ctx.anim.animations.size();
    phdr->localanimindex = panimdesc.offset_as<int>();
    buffer.align(4);

    // end of the committed .ani data (data past it still belongs to the animation being written)
    size_t blockData = blocks ? blocks->size() : 0;
    //      ------------ ------- ------- : ------- (-------)
    if (ctx.options.verbose && ctx.anim.animations.size() > 0) {
        MdlPrint("   animation       x       y       ips    angle\n");
    }

    for (i = 0; i < ctx.anim.animations.size(); i++) {
        s_animation_t* srcanim = ctx.anim.animations[i];
        const auto destanim = panimdesc + i;
        Assert(srcanim);

        AddToStringTable(strings, destanim, destanim.field(&mstudioanimdesc_t::sznameindex), srcanim->name);

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
        if (ctx.options.verbose && (srcanim->piecewisemove.size() > 0)) {
            j = srcanim->piecewisemove.size() - 1;
            if (srcanim->piecewisemove[j].pos[0] != 0 || srcanim->piecewisemove[j].pos[1] != 0) {
                float t = (srcanim->numframes - 1) / srcanim->fps;

                float r = 1 / t;

                float a = atan2(srcanim->piecewisemove[j].pos[1], srcanim->piecewisemove[j].pos[0]) * (180 / M_PI);
                float d = sqrt(DotProduct(srcanim->piecewisemove[j].pos, srcanim->piecewisemove[j].pos));
                MdlPrint("{:>12} {:7.2f} {:7.2f} : {:7.2f} ({:7.2f}) {:.1f}\n", srcanim->name,
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
            if (ctx.anim.animblocks.size() == 0) {
                ctx.anim.animblocks.emplace_back();
                ctx.anim.animblocks.emplace_back(s_animblock_t{.start = static_cast<uint32_t>(blockData)});
            }
        }

        if (!blocks || (std::ssize(ctx.bones.bonesaveframe) == 0 && srcanim->numframes == 1)) {
            // hack
            srcanim->disableAnimblocks = true;
        }
        else if (ctx.options.noAnimblockStall) {
            srcanim->isFirstSectionLocal = true;
        }

        // make sure number of preload frames is initialized
        if (srcanim->numNostallFrames == 0) {
            srcanim->numNostallFrames = srcanim->fps * ctx.options.preloadTime;
        }

        // Offset of a position from its block: block zero is relative to me (animation data inside the .mdl),
        // the others to their start in the .ani data (slot 0 is a placeholder there)
        const auto blockRelative = [&](const int block, const size_t position) {
            const int64_t base = block == 0 ? destanim.offset() : ctx.anim.animblocks[block].start;
            return static_cast<int>(static_cast<int64_t>(position) - base);
        };

        size_t animData;
        size_t ikData;
        size_t localHierarchy;

        if (srcanim->disableAnimblocks || srcanim->isFirstSectionLocal) {
            destanim->animblock = 0;
            animData = buffer.size();
            WriteAnimationData(ctx, srcanim, destanim, buffer, blocks);
            ikData = buffer.size();
            WriteIkErrors(srcanim, buffer);
            localHierarchy = buffer.size();
            WriteLocalHierarchy(srcanim, buffer);
        }
        else {
            animData = blocks->size();
            WriteAnimationData(ctx, srcanim, destanim, buffer, blocks);
            if (destanim->sectionindex) {
                // if sections were written, don't move the data already written to the last block
                blockData = blocks->size();
            }
            destanim->animblock = ctx.anim.animblocks.size() - 1;
            ikData = blocks->size();
            WriteIkErrors(srcanim, *blocks);
            localHierarchy = blocks->size();
            WriteLocalHierarchy(srcanim, *blocks);
        }

        const size_t blockEnd = blocks ? blocks->size() : 0;
        if (blockData != blockEnd &&
            static_cast<int64_t>(blockEnd) - ctx.anim.animblocks.back().start > ctx.anim.animblocksize) {
            ctx.anim.animblocks.back().end = blockData;
            ctx.anim.animblocks.emplace_back(s_animblock_t{.start = static_cast<uint32_t>(blockData)});
            destanim->animblock = ctx.anim.animblocks.size() - 1;
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

        if (ctx.anim.animblocks.size()) {
            ctx.anim.animblocks.back().end = blockEnd;
            blockData = blockEnd;
        }
    }

    if (!ctx.options.quiet) {
        /*
		for (i = 0; i < ctx.numanimblocks; i++)
		{
			printf("%2d (%3d:%3d): %d\n", i, ctx.animblock[i].iStartAnim, ctx.animblock[i].iEndAnim, ctx.animblock[i].end - ctx.animblock[i].start );
		}
		*/
    }

    // write movement keys
    for (i = 0; i < ctx.anim.animations.size(); i++) {
        s_animation_t* anim = ctx.anim.animations[i];

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
    if (std::ssize(ctx.bones.bonesaveframe) == 0) {
        for (j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
            if ((ctx.bones.bonetable[j].parent == -1) ||
                (ctx.bones.bonetable[j].posrange.Length() >= ctx.options.minZeroFramePosDelta)) {
                ctx.bones.bonetable[j].flags |= BoneFlags::HasSaveframePos;
            }
            if (ctx.options.zeroFramesHighres) {
                ctx.bones.bonetable[j].flags |= BoneFlags::HasSaveframeRot64;
            }
            else {
                ctx.bones.bonetable[j].flags |= BoneFlags::HasSaveframeRot32;
            }


            if ((!ctx.options.quiet) && HasFlag(ctx.bones.bonetable[j].flags,
                                                       BoneFlags::HasSaveframePos | BoneFlags::HasSaveframeRot64 |
                                                       BoneFlags::HasSaveframeRot32)) {
                MdlPrint("$BoneSaveFrame \"{}\"", ctx.bones.bonetable[j].name);
                if (HasFlag(ctx.bones.bonetable[j].flags, BoneFlags::HasSaveframePos)) {
                    MdlPrint(" position");
                }
                if (HasFlag(ctx.bones.bonetable[j].flags, BoneFlags::HasSaveframeRot64)) {
                    MdlPrint(" rotation64");
                }
                else if (HasFlag(ctx.bones.bonetable[j].flags, BoneFlags::HasSaveframeRot32)) {
                    MdlPrint(" rotation");
                }
                if (!HasFlag(ctx.bones.bonetable[j].flags, BoneFlags::HasSaveframePos) && ctx.bones.bonetable[j].
                    posrange.
                    Length() > 0.1) {
                    MdlPrint(" // ({:.2f})", ctx.bones.bonetable[j].posrange.Length());
                }
                MdlPrint("\n");
            }
        }
    }
    else {
        for (i = 0; i < std::ssize(ctx.bones.bonesaveframe); i++) {
            j = findGlobalBone(ctx, ctx.bones.bonesaveframe[i].name);

            if (j != -1) {
                if (ctx.bones.bonesaveframe[i].bSavePos) {
                    ctx.bones.bonetable[j].flags |= BoneFlags::HasSaveframePos;
                }
                if (ctx.bones.bonesaveframe[i].bSaveRot) {
                    if (ctx.options.zeroFramesHighres) {
                        ctx.bones.bonetable[j].flags |= BoneFlags::HasSaveframeRot64;
                    }
                    else {
                        ctx.bones.bonetable[j].flags |= BoneFlags::HasSaveframeRot32;
                    }
                }
                else if (ctx.bones.bonesaveframe[i].bSaveRot64) {
                    ctx.bones.bonetable[j].flags |= BoneFlags::HasSaveframeRot64;
                }
            }
            else {
                MdlError("Unknown $BoneSaveFrame \"{}\"\n", ctx.bones.bonesaveframe[i].name);
            }
        }
    }

    for (j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
        ((mstudiobone_t*) phdr->pBone(j))->flags |= ctx.bones.bonetable[j].flags;
    }

    buffer.align(4);

    // write zero frames
    for (i = 0; i < ctx.anim.animations.size(); i++) {
        s_animation_t* anim = ctx.anim.animations[i];
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

            destanim->zeroframecount = MIN(destanim->zeroframecount, ctx.options.maxZeroFrames);

            for (j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
                if (HasFlag(ctx.bones.bonetable[j].flags, BoneFlags::HasSaveframePos)) {
                    for (int n = 0; n < destanim->zeroframecount; n++) {
                        *buffer.allocate<Vector48>() = anim->sanim[destanim->zeroframespan * n][j].pos;
                    }
                }
                if (HasFlag(ctx.bones.bonetable[j].flags, BoneFlags::HasSaveframeRot64)) {
                    for (int n = 0; n < destanim->zeroframecount; n++) {
                        Quaternion q;
                        AngleQuaternion(anim->sanim[destanim->zeroframespan * n][j].rot, q);
                        *buffer.allocate<Quaternion64>() = q;
                    }
                }
                else if (HasFlag(ctx.bones.bonetable[j].flags, BoneFlags::HasSaveframeRot32)) {
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
                const auto psrcikrule = destanim->ikruleindex
                                            ? buffer.ptr_at<mstudioikrule_t>(destanim.offset() + destanim->ikruleindex)
                                            : blocks->ptr_at<mstudioikrule_t>(
                                                ctx.anim.animblocks[destanim->animblock].start +
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


static void WriteTextures(StudioMdlState &ctx, StringTable &strings, vstd::Buffer &buffer,
                          const vstd::BufferPtr<studiohdr_t> phdr) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j;

    // save texture info
    const auto ptexture = buffer.allocate<mstudiotexture_t>(ctx.materials.materials.size());
    phdr->numtextures = ctx.materials.materials.size();
    phdr->textureindex = ptexture.offset_as<int>();
    for (i = 0; i < ctx.materials.materials.size(); i++) {
        j = ctx.materials.materials[i];
        AddToStringTable(strings, ptexture + i, (ptexture + i).field(&mstudiotexture_t::sznameindex),
                         ctx.materials.textures[j].name);
    }
    buffer.align(4);

    const auto cdtextureoffset = buffer.allocate<int>(ctx.materials.cdtextures.size());
    phdr->numcdtextures = ctx.materials.cdtextures.size();
    phdr->cdtextureindex = cdtextureoffset.offset_as<int>();
    for (i = 0; i < ctx.materials.cdtextures.size(); i++) {
        AddToStringTable(strings, phdr, cdtextureoffset + i, ctx.materials.cdtextures[i]);
    }
    buffer.align(4);

    // save texture directory info: one row per skin family, all rows as wide as the first
    const auto &skinref = ctx.materials.skinref;
    phdr->numskinref = skinref.empty() ? 0 : static_cast<int>(skinref[0].size());
    phdr->numskinfamilies = static_cast<int>(skinref.size());
    const auto pref = buffer.allocate<short>(phdr->numskinfamilies * phdr->numskinref);
    phdr->skinindex = pref.offset_as<int>();

    for (i = 0; i < phdr->numskinfamilies; i++) {
        for (j = 0; j < phdr->numskinref; j++) {
            pref[i * phdr->numskinref + j] = skinref[i][j];
        }
    }
    buffer.align(4);
}


//-----------------------------------------------------------------------------
// Write source bone transforms
//-----------------------------------------------------------------------------
// The bones are read from phdr's bone array; the counts and indices go to phdr2
static void WriteBoneTransforms(StudioMdlState &ctx, StringTable &strings, vstd::Buffer &buffer,
                                const vstd::BufferPtr<studiohdr_t> phdr,
                                const vstd::BufferPtr<studiohdr2_t> phdr2) {
    vstd::FunctionTimingRecorder _("Write");
    const auto pBone = buffer.ptr_at<mstudiobone_t>(phdr->boneindex);
    matrix3x4_t identity;
    SetIdentityMatrix(identity);

    int nTransformCount = 0;
    for (int i = 0; i < std::ssize(ctx.bones.bonetable); i++) {
        if (HasFlag(ctx.bones.bonetable[i].flags, BoneFlags::AlwaysProcedural))
            continue;
        int nParent = ctx.bones.bonetable[i].parent;

        // Transformation is necessary if either you or your parent was realigned
        if (MatricesAreEqual(identity, ctx.bones.bonetable[i].srcRealign) &&
            ((nParent < 0) || MatricesAreEqual(identity, ctx.bones.bonetable[nParent].srcRealign)))
            continue;

        ++nTransformCount;
    }

    // save bone transform info
    const auto pSrcBoneTransform = buffer.allocate<mstudiosrcbonetransform_t>(nTransformCount);
    phdr2->numsrcbonetransform = nTransformCount;
    phdr2->srcbonetransformindex = pSrcBoneTransform.offset_as<int>();
    int bt = 0;
    for (int i = 0; i < std::ssize(ctx.bones.bonetable); i++) {
        if (HasFlag(ctx.bones.bonetable[i].flags, BoneFlags::AlwaysProcedural))
            continue;
        int nParent = ctx.bones.bonetable[i].parent;
        if (MatricesAreEqual(identity, ctx.bones.bonetable[i].srcRealign) &&
            ((nParent < 0) || MatricesAreEqual(identity, ctx.bones.bonetable[nParent].srcRealign)))
            continue;

        // What's going on here?
        // So, when we realign a bone, we want to do it in a way so that the child bones
        // have the same bone->world transform. If we take T as the src realignment transform
        // for the parent, P is the parent to world, and C is the child to parent, we expect
        // the child->world is constant after realignment:
        //		CtoW = P * C = ( P * T ) * ( T^-1 * C )
        // therefore Cnew = ( T^-1 * C )
        if (nParent >= 0) {
            MatrixInvert(ctx.bones.bonetable[nParent].srcRealign, pSrcBoneTransform[bt].pretransform);
        }
        else {
            SetIdentityMatrix(pSrcBoneTransform[bt].pretransform);
        }
        MatrixCopy(ctx.bones.bonetable[i].srcRealign, pSrcBoneTransform[bt].posttransform);
        AddToStringTable(strings, pSrcBoneTransform + bt,
                         (pSrcBoneTransform + bt).field(&mstudiosrcbonetransform_t::sznameindex),
                         ctx.bones.bonetable[i].name);
        ++bt;
    }
    buffer.align(4);

    if (std::ssize(ctx.bones.bonetable) > 1) {
        // write second bone table
        const auto pLinearBone = buffer.allocate<mstudiolinearbone_t>();
        phdr2->linearboneindex = pLinearBone.offset_from<int>(phdr2);

        pLinearBone->numbones = std::ssize(ctx.bones.bonetable);

        // one array per bone field, each 4-aligned, indexed relative to pLinearBone
        const auto writeBoneBlock = [&]<typename F>(F mstudiobone_t::* srcfield, int mstudiolinearbone_t::* destindex) {
            const auto dest = buffer.template allocate<F>(std::ssize(ctx.bones.bonetable));
            pLinearBone.get()->*destindex = dest.template offset_from<int>(pLinearBone);
            buffer.align(4);
            for (int i = 0; i < std::ssize(ctx.bones.bonetable); i++)
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

static void WriteBodyGroupPresets(StudioMdlState &ctx, StringTable &strings, vstd::Buffer &buffer,
                                  const vstd::BufferPtr<studiohdr2_t> pStudioHdr2) {
    vstd::FunctionTimingRecorder _("Write");
    buffer.align(4);

    pStudioHdr2->m_nBodyGroupPresetCount = std::ssize(ctx.body.bodygrouppresets);
    pStudioHdr2->m_nBodyGroupPresetIndex = 0;

    if (std::ssize(ctx.body.bodygrouppresets) <= 0)
        return;

    const auto pBodygroupPreset = buffer.allocate<mstudiobodygrouppreset_t>(std::ssize(ctx.body.bodygrouppresets));
    pStudioHdr2->m_nBodyGroupPresetIndex = pBodygroupPreset.offset_from<int>(pStudioHdr2);
    buffer.align(4);

    for (int i = 0; i < std::ssize(ctx.body.bodygrouppresets); i++) {
        AddToStringTable(strings, pBodygroupPreset + i,
                         (pBodygroupPreset + i).field(&mstudiobodygrouppreset_t::sznameindex),
                         ctx.body.bodygrouppresets[i].name);
        pBodygroupPreset[i].iValue = ctx.body.bodygrouppresets[i].iValue;
        pBodygroupPreset[i].iMask = ctx.body.bodygrouppresets[i].iMask;
    }
}

//-----------------------------------------------------------------------------
// Write the bone flex drivers
//-----------------------------------------------------------------------------
static void WriteBoneFlexDrivers(StudioMdlState &ctx, vstd::Buffer &buffer,
                                 const vstd::BufferPtr<studiohdr2_t> pStudioHdr2) {
    vstd::FunctionTimingRecorder _("Write");
    buffer.align(4);

    pStudioHdr2->m_nBoneFlexDriverCount = 0;
    pStudioHdr2->m_nBoneFlexDriverIndex = 0;

    CDmeBoneFlexDriverList* pDmeBoneFlexDriverList = GetElement<CDmeBoneFlexDriverList>(
        ctx.flex.hDmeBoneFlexDriverList);
    if (!pDmeBoneFlexDriverList)
        return;

    const int nBoneFlexDriverCount = pDmeBoneFlexDriverList->m_eBoneFlexDriverList.size();
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
        pBoneFlexDriver->m_nControlCount = pDmeBoneFlexDriver->m_eControlList.size();

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
// Write the processed vertices. extraTexcoordsToWrite: extra texcoord sets per vertex (set by WriteModel)
//-----------------------------------------------------------------------------
static void WriteVertices(StudioMdlState &ctx, const studiohdr_t* hdr, const int extraTexcoordsToWrite) {
    vstd::FunctionTimingRecorder _("Write");
    int i = 0;
    int j = 0;
    int k = 0;
    const bool extraData = (hdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) != 0;

    if (!ctx.body.nummodelsbeforeLOD)
        return;

    const std::string fileName = OutputModelPath(ctx, ".vvd");

    if (!ctx.options.quiet) {
        MdlPrint("---------------------\n");
        MdlPrint("writing {}:\n", fileName);
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
    for (i = 0; i < ctx.body.nummodelsbeforeLOD; i++) {
        s_loddata_t* pLodData = ctx.body.model[i]->m_pLodData;

        // skip blank empty model
        if (!pLodData)
            continue;

        // save vertices
        buffer.align(16);
        auto vertex = buffer.allocate<mstudiovertex_t>(pLodData->numvertices);
        for (j = 0; j < pLodData->numvertices; j++) {
            //			printf( "saving bone weight %d for model %d at 0x%p\n",
            //				j, i, &pbone[j] );

            const s_vertexinfo_t &lodVertex = pLodData->vertex[j];
            VectorCopy(lodVertex.position, vertex[j].m_vecPosition);
            VectorCopy(lodVertex.normal, vertex[j].m_vecNormal);
            Vector2DCopy(lodVertex.texcoord[0], vertex[j].m_vecTexCoord);

            mstudioboneweight_t* pBoneWeight = &vertex[j].m_BoneWeights; // zero-filled by allocate
            pBoneWeight->numbones = lodVertex.boneweight.numbones;
            for (k = 0; k < pBoneWeight->numbones; k++) {
                pBoneWeight->bone[k] = lodVertex.boneweight.bone[k];
                pBoneWeight->weight[k] = lodVertex.boneweight.weight[k];
            }
        }

        fileHeader->numLODVertexes[0] += pLodData->numvertices;

        if (!ctx.options.quiet) {
            MdlPrint("{} vertices \n", pLodData->numvertices);
        }
    }

    // store tangents grouped by mesh order
    buffer.align(4);
    fileHeader->tangentDataStart = buffer.size();
    for (i = 0; i < ctx.body.nummodelsbeforeLOD; i++) {
        s_loddata_t* pLodData = ctx.body.model[i]->m_pLodData;

        // skip blank empty model
        if (!pLodData)
            continue;

        // save tangent space S
        buffer.align(4);
        auto ptangents = buffer.allocate<Vector4D>(pLodData->numvertices);
        for (j = 0; j < pLodData->numvertices; j++) {
            Vector4DCopy(pLodData->vertex[j].tangentS, ptangents[j]);
#ifdef _DEBUG
            float w = ptangents[j].w;
            Assert(w == 1.0f || w == -1.0f);
#endif
        }

        if (!ctx.options.quiet) {
            MdlPrint("{} tangents\n", pLodData->numvertices);
        }
    }

    if (extraData) {
        buffer.align(4);

        auto extraDataStart = buffer.end_ptr();
        auto pExtraheader = buffer.allocate<ExtraVertexAttributesHeader_t>();
        pExtraheader->m_count = extraTexcoordsToWrite;
        auto pIndex = buffer.allocate<ExtraVertexAttributeIndex_t>(extraTexcoordsToWrite);

        for (int e = 0; e < extraTexcoordsToWrite; ++e) {
            buffer.align(4);

            // Populate Index: type and byteoffset
            pIndex[e].m_type = static_cast<ExtraVertexAttributeType_t>(STUDIO_EXTRA_ATTRIBUTE_TEXCOORD0 + e + 1);
            pIndex[e].m_offset = buffer.end_ptr().offset_from<int>(extraDataStart);
            pIndex[e].m_bytes = 2 * sizeof(float);

            // store extra vertex data, one entry per vertex, order matches main vertex data
            for (i = 0; i < ctx.body.nummodelsbeforeLOD; i++) {
                s_loddata_t* pLodData = ctx.body.model[i]->m_pLodData;

                // skip blank empty model
                if (!pLodData)
                    continue;

                // save extra texcoord
                auto pExtraTexcoord = buffer.allocate<float>(pLodData->numvertices * 2);
                for (j = 0; j < pLodData->numvertices; j++) {
                    const s_vertexinfo_t &lodVertex = pLodData->vertex[j];

                    pExtraTexcoord[j * 2 + 0] = lodVertex.texcoord[e + 1].x;
                    pExtraTexcoord[j * 2 + 1] = lodVertex.texcoord[e + 1].y;
                }

                if (!ctx.options.quiet) {
                    MdlPrint("extra vertex data   {:7} bytes ({} vertices)\n",
                               buffer.end_ptr().offset_from<int>(pExtraTexcoord),
                               pLodData->numvertices);
                }
            }
        }
        pExtraheader->m_totalbytes = buffer.end_ptr().offset_from<int>(extraDataStart);
    }

    if (!ctx.options.quiet) {
        MdlPrint("total      {:7} bytes\n", buffer.size());
    }

    SaveOutputFile(ctx, fileName, std::move(buffer));
}


//-----------------------------------------------------------------------------
// Computes the maximum absolute value of any component of all vertex animation
// pos (x,y,z) normal (x,y,z) or wrinkle
//
// Returns the fixed point scale and also sets appropriate values & flags in
// passed studiohdr_t
//-----------------------------------------------------------------------------
float ComputeVertAnimFixedPointScale(StudioMdlState &ctx, studiohdr_t* pStudioHdr) {
    float flVertAnimRange = 0.0f;

    for (const auto &flexkey: ctx.flex.flexkeys) {
        if (flexkey.numvanims <= 0)
            continue;

        const bool bWrinkleVAnim = (flexkey.vanimtype == Wrinkle);

        for (const s_vertanim_t &vanim: std::span(flexkey.vanim, flexkey.numvanims)) {
            flVertAnimRange = std::max({
                flVertAnimRange,
                std::abs(vanim.pos.x), std::abs(vanim.pos.y), std::abs(vanim.pos.z),
                std::abs(vanim.normal.x), std::abs(vanim.normal.y), std::abs(vanim.normal.z)
            });
            if (bWrinkleVAnim) {
                flVertAnimRange = std::max(flVertAnimRange, std::abs(vanim.wrinkle));
            }
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


// extraTexcoordsToWrite: receives the number of extra texcoord sets per vertex, for WriteVertices
static void WriteModel(StudioMdlState &ctx, StringTable &strings, vstd::Buffer &buffer,
                       const vstd::BufferPtr<studiohdr_t> phdr, int &extraTexcoordsToWrite) {
    vstd::FunctionTimingRecorder _("Write");
    int i, j, k, m;
    s_source_t* psource;
    s_vertanim_t* pvanim;

    // vertex data is written to external file, offsets kept internal
    // track expected external base to store proper offsets
    uintptr_t externalVertexIndex = 0;
    uintptr_t externalTangentsIndex = 0;

    // write bodypart info
    const auto pbodypart = buffer.allocate<mstudiobodyparts_t>(std::ssize(ctx.body.bodypart));
    phdr->numbodyparts = std::ssize(ctx.body.bodypart);
    phdr->bodypartindex = pbodypart.offset_as<int>();

    const auto pmodel = buffer.allocate<mstudiomodel_t>(ctx.body.nummodelsbeforeLOD);

    for (i = 0, j = 0; i < std::ssize(ctx.body.bodypart); i++) {
        AddToStringTable(strings, pbodypart + i, (pbodypart + i).field(&mstudiobodyparts_t::sznameindex),
                         ctx.body.bodypart[i].name);
        pbodypart[i].nummodels = std::ssize(ctx.body.bodypart[i].pmodel);
        pbodypart[i].base = ctx.body.bodypart[i].base;
        pbodypart[i].modelindex = (pmodel + j).offset_from<int>(pbodypart + i);
        j += std::ssize(ctx.body.bodypart[i].pmodel);
    }
    buffer.align(4);

    // write global flex names
    auto pflexdesc = buffer.allocate<mstudioflexdesc_t>(ctx.flex.flexdescs.size());
    phdr->numflexdesc = ctx.flex.flexdescs.size();
    phdr->flexdescindex = pflexdesc.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < ctx.flex.flexdescs.size(); j++) {
        AddToStringTable(strings, pflexdesc, pflexdesc.field(&mstudioflexdesc_t::szFACSindex),
                         ctx.flex.flexdescs[j].FACS);
        pflexdesc++;
    }

    // write global flex controllers
    const auto pflexcontrollers = buffer.allocate<mstudioflexcontroller_t>(ctx.flex.flexcontrollers.size());
    phdr->numflexcontrollers = ctx.flex.flexcontrollers.size();
    phdr->flexcontrollerindex = pflexcontrollers.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < ctx.flex.flexcontrollers.size(); j++) {
        const auto pflexcontroller = pflexcontrollers + j;
        AddToStringTable(strings, pflexcontroller, pflexcontroller.field(&mstudioflexcontroller_t::sznameindex),
                         ctx.flex.flexcontrollers[j].name);
        AddToStringTable(strings, pflexcontroller, pflexcontroller.field(&mstudioflexcontroller_t::sztypeindex),
                         ctx.flex.flexcontrollers[j].type);
        pflexcontroller->min = ctx.flex.flexcontrollers[j].min;
        pflexcontroller->max = ctx.flex.flexcontrollers[j].max;
        pflexcontroller->localToGlobal = -1;
    }

    // write flex rules
    auto pflexrule = buffer.allocate<mstudioflexrule_t>(ctx.flex.flexrules.size());
    phdr->numflexrules = ctx.flex.flexrules.size();
    phdr->flexruleindex = pflexrule.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < ctx.flex.flexrules.size(); j++) {
        pflexrule->flex = ctx.flex.flexrules[j].flex;
        pflexrule->numops = ctx.flex.flexrules[j].numops;

        const auto pflexop = buffer.allocate<mstudioflexop_t>(pflexrule->numops);
        pflexrule->opindex = pflexop.offset_from<int>(pflexrule);

        for (i = 0; i < pflexrule->numops; i++) {
            pflexop[i].op = ctx.flex.flexrules[j].op[i].op;
            pflexop[i].d.index = ctx.flex.flexrules[j].op[i].d.index;
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
    std::vector<bool> pControllerHandled(ctx.flex.flexcontrollers.size(), false);

    for (j = 0; j < ctx.flex.flexcontrollers.size(); ++j) {
        // Don't handle controls twice
        if (pControllerHandled[j])
            continue;

        const s_flexcontroller_t &flexcontroller = ctx.flex.flexcontrollers[j];

        const auto pFlexControllerUI = buffer.allocate<mstudioflexcontrollerui_t>();
        const auto uiName = pFlexControllerUI.field(&mstudioflexcontrollerui_t::sznameindex);
        // offset from this UI entry to flex controller index
        const auto controllerOffset = [&](const int index) {
            return (pflexcontrollers + index).offset_from<int>(pFlexControllerUI);
        };

        bool found = false;

        // See if this controller is in the remap table
        for (k = 0; k < ctx.flex.FlexControllerRemap.size(); ++k) {
            s_flexcontrollerremap_t &remap = ctx.flex.FlexControllerRemap[k];
            if (j == remap.m_Index || j == remap.m_LeftIndex || j == remap.m_RightIndex || j == remap.m_MultiIndex) {
                AddToStringTable(strings, pFlexControllerUI, uiName, remap.m_Name.c_str());

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

            if (j < ctx.flex.flexcontrollers.size() - 1 &&
                StringAfterPrefixCaseSensitive(flexcontroller.name, "right_") &&
                StringAfterPrefixCaseSensitive(ctx.flex.flexcontrollers[j + 1].name, "left_") &&
                !Q_strcmp(StringAfterPrefixCaseSensitive(flexcontroller.name, "right_"),
                          StringAfterPrefixCaseSensitive(ctx.flex.flexcontrollers[j + 1].name, "left_"))) {
                AddToStringTable(strings, pFlexControllerUI, uiName, flexcontroller.name + 6);

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
                     StringAfterPrefixCaseSensitive(ctx.flex.flexcontrollers[j - 1].name, "right_") &&
                     !Q_strcmp(StringAfterPrefixCaseSensitive(flexcontroller.name, "left_"),
                               StringAfterPrefixCaseSensitive(ctx.flex.flexcontrollers[j - 1].name, "right_"))) {
                AddToStringTable(strings, pFlexControllerUI, uiName, flexcontroller.name + 5);

                pFlexControllerUI->stereo = true;

                Assert(!pControllerHandled[j]);
                pFlexControllerUI->szindex0 = controllerOffset(j);
                pControllerHandled[j] = true;

                Assert(!pControllerHandled[j - 1]);
                pFlexControllerUI->szindex1 = controllerOffset(j - 1);
                pControllerHandled[j - 1] = true;
            }
            else {
                AddToStringTable(strings, pFlexControllerUI, uiName, flexcontroller.name);
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
    for (j = 0; j < ctx.flex.flexcontrollers.size(); ++j) {
        Assert(pControllerHandled[j]);
    }
#endif // _DEBUG

    // write ik chains
    auto pikchain = buffer.allocate<mstudioikchain_t>(std::ssize(ctx.anim.ikchain));
    phdr->numikchains = std::ssize(ctx.anim.ikchain);
    phdr->ikchainindex = pikchain.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < std::ssize(ctx.anim.ikchain); j++) {
        AddToStringTable(strings, pikchain, pikchain.field(&mstudioikchain_t::sznameindex), ctx.anim.ikchain[j].name);
        pikchain->numlinks = ctx.anim.ikchain[j].numlinks;

        const auto piklink = buffer.allocate<mstudioiklink_t>(pikchain->numlinks);
        pikchain->linkindex = piklink.offset_from<int>(pikchain);

        for (i = 0; i < pikchain->numlinks; i++) {
            piklink[i].bone = ctx.anim.ikchain[j].link[i].bone;
            piklink[i].kneeDir = ctx.anim.ikchain[j].link[i].kneeDir;
        }

        pikchain++;
    }

    // save autoplay locks
    const auto piklock = buffer.allocate<mstudioiklock_t>(std::ssize(ctx.anim.ikautoplaylock));
    phdr->numlocalikautoplaylocks = std::ssize(ctx.anim.ikautoplaylock);
    phdr->localikautoplaylockindex = piklock.offset_as<int>();
    buffer.align(4);

    for (j = 0; j < std::ssize(ctx.anim.ikautoplaylock); j++) {
        piklock[j].chain = ctx.anim.ikautoplaylock[j].chain;
        piklock[j].flPosWeight = ctx.anim.ikautoplaylock[j].flPosWeight;
        piklock[j].flLocalQWeight = ctx.anim.ikautoplaylock[j].flLocalQWeight;
    }

    // save mouth info
    const auto pmouth = buffer.allocate<mstudiomouth_t>(std::ssize(ctx.flex.mouth));
    phdr->nummouths = std::ssize(ctx.flex.mouth);
    phdr->mouthindex = pmouth.offset_as<int>();
    buffer.align(4);

    for (i = 0; i < std::ssize(ctx.flex.mouth); i++) {
        pmouth[i].bone = ctx.flex.mouth[i].bone;
        VectorCopy(ctx.flex.mouth[i].forward, pmouth[i].forward);
        pmouth[i].flexdesc = ctx.flex.mouth[i].flexdesc;
    }

    // save pose parameters
    const auto ppose = buffer.allocate<mstudioposeparamdesc_t>(std::ssize(ctx.anim.pose));
    phdr->numlocalposeparameters = std::ssize(ctx.anim.pose);
    phdr->localposeparamindex = ppose.offset_as<int>();
    buffer.align(4);

    for (i = 0; i < std::ssize(ctx.anim.pose); i++) {
        AddToStringTable(strings, ppose + i, (ppose + i).field(&mstudioposeparamdesc_t::sznameindex),
                         ctx.anim.pose[i].name);
        ppose[i].start = ctx.anim.pose[i].min;
        ppose[i].end = ctx.anim.pose[i].max;
        ppose[i].flags = ctx.anim.pose[i].flags;
        ppose[i].loop = ctx.anim.pose[i].loop;
    }

    if (!ctx.options.quiet) {
        MdlPrint("ik/pose    {:7} bytes\n", buffer.size());
    }

    const float flVertAnimFixedPointScale = ComputeVertAnimFixedPointScale(ctx, phdr.get());

    // Check all source models for extra texcoords
    // If any exist, add model flag to indicate that extra vertex data will be appended to the VVD file
    bool bExtraVertexData = false;
    extraTexcoordsToWrite = 0;
    for (int i = 0; (i < phdr->numbodyparts) && !bExtraVertexData; ++i) {
        for (int j = 0; j < std::ssize(ctx.body.bodypart[i].pmodel); ++j) {
            if (ctx.body.bodypart[i].pmodel[j] && ctx.body.bodypart[i].pmodel[j]->source->numvertices > 0) {
                if (ctx.body.bodypart[i].pmodel[j] && ctx.body.bodypart[i].pmodel[j]->source->vertex[0].
                    texcoord.size() > 1) {
                    bExtraVertexData = true;
                    extraTexcoordsToWrite = ctx.body.bodypart[i].pmodel[j]->source->vertex[0].texcoord.size() - 1;
                    break;
                }
            }
        }
    }

    if (bExtraVertexData) {
        phdr->flags |= STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA;
    }

    // write model
    for (i = 0; i < ctx.body.nummodelsbeforeLOD; i++) {
        int n = 0;

        const auto pModel = pmodel + i;

        Q_strncpy(pModel->name, ctx.body.model[i]->filename.c_str(), sizeof(pModel->name));

        psource = ctx.body.model[i]->source;
        s_loddata_t* pLodData = ctx.body.model[i]->m_pLodData;

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
        const auto peyeball = buffer.allocate<mstudioeyeball_t>(ctx.body.model[i]->eyeballs.size());
        pModel->numeyeballs = ctx.body.model[i]->eyeballs.size();
        pModel->eyeballindex = peyeball.offset_from<int>(pModel);

        buffer.align(4);
        for (j = 0; j < ctx.body.model[i]->eyeballs.size(); j++) {
            k = ctx.body.model[i]->eyeballs[j].mesh;
            pmesh[k].materialtype = 1; // FIXME: tag custom material
            pmesh[k].materialparam = j; // FIXME: tag custom material

            peyeball[j].bone = ctx.body.model[i]->eyeballs[j].bone;
            VectorCopy(ctx.body.model[i]->eyeballs[j].org, peyeball[j].org);
            peyeball[j].zoffset = ctx.body.model[i]->eyeballs[j].zoffset;
            peyeball[j].radius = ctx.body.model[i]->eyeballs[j].radius;
            VectorCopy(ctx.body.model[i]->eyeballs[j].up, peyeball[j].up);
            VectorCopy(ctx.body.model[i]->eyeballs[j].forward, peyeball[j].forward);
            peyeball[j].iris_scale = ctx.body.model[i]->eyeballs[j].iris_scale;

            for (k = 0; k < 3; k++) {
                peyeball[j].upperflexdesc[k] = ctx.body.model[i]->eyeballs[j].upperflexdesc[k];
                peyeball[j].lowerflexdesc[k] = ctx.body.model[i]->eyeballs[j].lowerflexdesc[k];
                peyeball[j].uppertarget[k] = ctx.body.model[i]->eyeballs[j].uppertarget[k];
                peyeball[j].lowertarget[k] = ctx.body.model[i]->eyeballs[j].lowertarget[k];
            }

            peyeball[j].upperlidflexdesc = ctx.body.model[i]->eyeballs[j].upperlidflexdesc;
            peyeball[j].lowerlidflexdesc = ctx.body.model[i]->eyeballs[j].lowerlidflexdesc;
        }

        if (!ctx.options.quiet) {
            MdlPrint("eyeballs   {:7} bytes ({} eyeballs)\n", buffer.size(), ctx.body.model[i]->eyeballs.size());
        }

        // move flexes into individual meshes
        for (m = 0; m < pModel->nummeshes; m++) {
            int numflexkeys[MAXSTUDIOFLEXKEYS];
            pmesh[m].numflexes = 0;

            // initialize array
            for (j = 0; j < ctx.flex.flexkeys.size(); j++) {
                numflexkeys[j] = 0;
            }

            // count flex instances per mesh
            for (j = 0; j < ctx.flex.flexkeys.size(); j++) {
                if (ctx.flex.flexkeys[j].imodel == i) {
                    for (k = 0; k < ctx.flex.flexkeys[j].numvanims; k++) {
                        n = ctx.flex.flexkeys[j].vanim[k].vertex - pmesh[m].vertexoffset;
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

                for (j = 0; j < ctx.flex.flexkeys.size(); j++) {
                    if (!numflexkeys[j])
                        continue;

                    pflex->flexdesc = ctx.flex.flexkeys[j].flexdesc;
                    pflex->target0 = ctx.flex.flexkeys[j].target0;
                    pflex->target1 = ctx.flex.flexkeys[j].target1;
                    pflex->target2 = ctx.flex.flexkeys[j].target2;
                    pflex->target3 = ctx.flex.flexkeys[j].target3;
                    pflex->numverts = numflexkeys[j];
                    pflex->flexpair = ctx.flex.flexkeys[j].flexpair;
                    pflex->vertanimtype = ctx.flex.flexkeys[j].vanimtype;

                    pvanim = ctx.flex.flexkeys[j].vanim;

                    const bool bWrinkleVAnim = (pflex->vertanimtype == Wrinkle);
                    const int nVAnimDeltaSize = bWrinkleVAnim
                                                    ? sizeof(mstudiovertanim_wrinkle_t)
                                                    : sizeof(mstudiovertanim_t);

                    // byte pointer: the delta size depends on the type
                    auto pvertanim = buffer.allocate<uint8_t>(pflex->numverts * nVAnimDeltaSize);
                    pflex->vertindex = pvertanim.offset_from<int>(pflex);
                    buffer.align(4);

                    for (k = 0; k < ctx.flex.flexkeys[j].numvanims; k++) {
                        n = ctx.flex.flexkeys[j].vanim[k].vertex - pmesh[m].vertexoffset;
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

        if (!ctx.options.quiet) {
            MdlPrint("flexes     {:7} bytes ({} flexes)\n", buffer.size(), ctx.flex.flexkeys.size());
        }
    }


    buffer.align(4);

    auto pincludemodel = buffer.allocate<mstudiomodelgroup_t>(ctx.body.includemodels.size());
    phdr->numincludemodels = ctx.body.includemodels.size();
    phdr->includemodelindex = pincludemodel.offset_as<int>();

    for (i = 0; i < ctx.body.includemodels.size(); i++) {
        AddToStringTable(strings, pincludemodel, pincludemodel.field(&mstudiomodelgroup_t::sznameindex),
                         ctx.body.includemodels[i].name);
        pincludemodel++;
    }

    // save animblock group info (block offsets are already relative to the .ani data)
    const auto panimblock = buffer.allocate<mstudioanimblock_t>(ctx.anim.animblocks.size());
    phdr->numanimblocks = ctx.anim.animblocks.size();
    phdr->animblockindex = panimblock.offset_as<int>();
    buffer.align(4);

    for (i = 1; i < ctx.anim.animblocks.size(); i++) {
        panimblock[i].datastart = ctx.anim.animblocks[i].start;
        panimblock[i].dataend = ctx.anim.animblocks[i].end;
    }
    AddToStringTable(strings, phdr, phdr.field(&studiohdr_t::szanimblocknameindex), ctx.anim.animblockname);
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


void CapKeyValues(StudioMdlState &ctx) {
    const char* headCap = "mdlkeyvalue\n{\n";
    const char* tailCap = "}\\n";
    if (ctx.header.KeyValueText.size()) {
        ctx.header.KeyValueText.insert(ctx.header.KeyValueText.begin(), headCap,
                                               headCap + std::strlen(headCap));
        ctx.header.KeyValueText.insert(ctx.header.KeyValueText.end(), tailCap,
                                               tailCap + std::strlen(tailCap));
    }
}

void WriteQCPath(StudioMdlState &ctx) {
    std::string relativeQCPath;
    if (ctx.paths.qcPathEntry) {
        relativeQCPath = *ctx.paths.qcPathEntry;
    }
    else {
        char relativeQCDir[4096] = {};
        g_pFullFileSystem->FullPathToRelativePathEx(ctx.paths.qdir.c_str(), IBaseFileSystem::PathID::Content,
                                                    relativeQCDir, sizeof(relativeQCDir));
        relativeQCPath = std::string(relativeQCDir) + std::string(vstd::paths::file_name(ctx.paths.fullpath));
    }

    if (!relativeQCPath.empty()) {
        const std::string block = "qc_path {\n\"value\" \"" + relativeQCPath + "\" }\n";
        ctx.header.KeyValueText.insert(ctx.header.KeyValueText.end(), block.begin(), block.end());
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

// The .vvd as the vertex accessors read it: mstudiomodel_t::GetVertexData(pStudioHdr) calls CacheVertexData, which
// finds this through studiohdr2_t::pVertexBase ("used by tools only that don't cache, but persist mdl's peer data").
// WriteModelFiles points pVertexBase at it once the .vvd is written; FixupMDLFile clears it with the other runtime
// pointers before the .mdl is stored.
struct VertexFileCache {
    StudioMdlState &ctx;
    std::optional<vstd::Buffer> data{}; // loaded by the first accessor call
};

void WriteModelFiles(StudioMdlState &ctx) {
    vstd::FunctionTimingRecorder _("Write");
    std::string blockFileName; // .ani
    size_t total = 0;
    std::string filename;
    int totalframes = 0;
    float totalseconds = 0;
    std::vector<std::string> preexistingSequences; // sequence names of the model on disk, in their order
    int extraTexcoordsToWrite = 0;
    StringTable strings;

    // static, like the never-freed buffers they replace: the later steps keep using the header
    vstd::Buffer model_data;
    vstd::Buffer block_data; // .ani
    vstd::Buffer* blocks = nullptr;
    vstd::BufferPtr<studiohdr_t> pblockhdr;

    vstd::paths::strip_extension(ctx.paths.outname);

    if (ctx.anim.animblocksize != 0) {
        // write the non-default ctx.anim.sequence group data to separate files
        ctx.anim.animblockname.format("models/{}.ani", ctx.paths.outname);

        blockFileName = ctx.paths.gameDir + ctx.anim.animblockname.c_str();

        if (*ctx.paths.internalName) {
            vstd::paths::strip_extension(ctx.paths.internalName);
            ctx.anim.animblockname.format("models/{}.ani", ctx.paths.internalName);
        }

        EnsureFileDirectoryExists(blockFileName);

        blocks = &block_data;
        pblockhdr = blocks->allocate<studiohdr_t>();
        pblockhdr->id = IDSTUDIOANIMGROUPHEADER;
        pblockhdr->version = STUDIO_VERSION;
    }

    //
    // write the ctx.body.model output file
    //
    const auto phdr = model_data.allocate<studiohdr_t>();

    phdr->id = IDSTUDIOHEADER;
    phdr->version = STUDIO_VERSION;

    ctx.paths.outname.append(".mdl");

    filename = OutputModelPath(ctx, ".mdl");


    // Create the directory.
    EnsureFileDirectoryExists(filename);


    if (!ctx.options.quiet) {
        MdlPrint("---------------------\n");
        MdlPrint("writing {}:\n", filename);
    }

    LoadPreexistingSequenceOrder(ctx, filename, preexistingSequences);


    if (ctx.options.parseable_completion_output) {
        char szRelativePath[260];
        V_MakeRelativePath(filename.c_str(), getenv("VGAME"), szRelativePath, sizeof(szRelativePath));
        MdlPrint("\nOUTPUT MODEL: {}\n", szRelativePath);
    }


    phdr->eyeposition = ctx.header.eyeposition;
    phdr->illumposition = ctx.header.illumposition;

    if (!ctx.header.wrotebbox && std::ssize(ctx.anim.sequence) > 0) {
        ctx.header.bbox[0] = ctx.anim.sequence[0].bmin;
        ctx.header.bbox[1] = ctx.anim.sequence[0].bmax;
        //        CollisionModel_ExpandBBox(bbox[0], bbox[1]);
        ctx.anim.sequence[0].bmin = ctx.header.bbox[0];
        ctx.anim.sequence[0].bmax = ctx.header.bbox[1];
    }
    if (!ctx.header.wrotecbox) {
        // no default clipping box, just use per-sequence box
        ctx.header.cbox[0] = vec3_origin;
        ctx.header.cbox[1] = vec3_origin;
    }

    phdr->hull_min = ctx.header.bbox[0];
    phdr->hull_max = ctx.header.bbox[1];
    phdr->view_bbmin = ctx.header.cbox[0];
    phdr->view_bbmax = ctx.header.cbox[1];

    phdr->flags = ctx.header.gflags;
    phdr->mass = 1;
    phdr->constdirectionallightdot = ctx.header.constdirectionalightdot;

    if (ctx.options.numAllowedRootLODs > 0) {
        phdr->numAllowedRootLODs = ctx.options.numAllowedRootLODs;
    }

    // FIXME: Remove when we up the model version
    const auto phdr2 = model_data.allocate<studiohdr2_t>();
    phdr->studiohdr2index = phdr2.offset_as<int>();

    phdr2->illumpositionattachmentindex = ctx.header.illumpositionattachment;
    phdr2->flMaxEyeDeflection = ctx.header.flMaxEyeDeflection;

    BeginStringTable(strings);

    if (*ctx.paths.internalName) {
        V_strncpy(phdr->name, ctx.paths.internalName, sizeof(phdr->name) - 1);
        AddToStringTable(strings, phdr2, phdr2.field(&studiohdr2_t::sznameindex), ctx.paths.internalName);
    }
    else {
        V_strncpy(phdr->name, ctx.paths.outname, sizeof(phdr->name) - 1);
        AddToStringTable(strings, phdr2, phdr2.field(&studiohdr2_t::sznameindex), ctx.paths.outname);
    }

    WriteBoneInfo(ctx, strings, model_data, phdr);
    if (!ctx.options.quiet) {
        MdlPrint("bones      {:7} bytes ({})\n", model_data.size() - total, std::ssize(ctx.bones.bonetable));
    }
    total = model_data.size();

    WriteAnimations(ctx, strings, model_data, blocks, phdr, totalframes, totalseconds);
    if (!ctx.options.quiet) {
        MdlPrint("animations {:7} bytes ({} anims) ({} frames) [{}:{:02}]\n", model_data.size() - total,
                   ctx.anim.animations.size(),
                   totalframes,
                   (int) totalseconds / 60, (int) totalseconds % 60);
    }
    total = model_data.size();

    WriteSequenceInfo(ctx, strings, model_data, phdr, preexistingSequences);
    if (!ctx.options.quiet) {
        MdlPrint("sequences  {:7} bytes ({} seq) \n", model_data.size() - total, std::ssize(ctx.anim.sequence));
    }
    total = model_data.size();

    WriteModel(ctx, strings, model_data, phdr, extraTexcoordsToWrite);

    if (!ctx.options.quiet) {
        MdlPrint("models     {:7} bytes\n", model_data.size() - total);
    }

    total = model_data.size();

    WriteTextures(ctx, strings, model_data, phdr);
    if (!ctx.options.quiet) {
        MdlPrint("textures   {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteQCPath(ctx);

    CapKeyValues(ctx);

    WriteKeyValues(model_data, phdr, ctx.header.KeyValueText);
    if (!ctx.options.quiet) {
        MdlPrint("keyvalues  {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteBoneTransforms(ctx, strings, model_data, phdr, phdr2);
    if (!ctx.options.quiet) {
        MdlPrint("bone transforms  {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteBoneFlexDrivers(ctx, model_data, phdr2);
    if (!ctx.options.quiet) {
        MdlPrint("bone flex driver {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteBodyGroupPresets(ctx, strings, model_data, phdr2);
    if (!ctx.options.quiet) {
        MdlPrint("bodygroup presets {:7} bytes\n", model_data.size() - total);
    }
    total = model_data.size();

    WriteStringTable(strings, model_data);

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

    if (ctx.options.verifyOnly)
        return;


    if (!ctx.options.quiet) {
        MdlPrint("collision  {:7} bytes\n", model_data.size() - total);
    }

    // the .mdl data is complete: nothing allocates in it anymore, so raw pointers into it stay valid
    studiohdr_t* const pStudioHdr = phdr.get();

    AssignMeshIDs(pStudioHdr);

    pStudioHdr->length = static_cast<int>(model_data.size());
    if (!ctx.options.quiet) {
        MdlPrint("total      {:7}\n", pStudioHdr->length);
    }

    SaveOutputFile(ctx, filename, model_data.data(), pStudioHdr->length);

    if (blocks) {
        const int length = static_cast<int>(blocks->size());
        pblockhdr->length = length;

        if (!ctx.options.quiet) {
            MdlPrint("---------------------\n");
            MdlPrint("writing {}:\n", ctx.anim.animblockname);
            MdlPrint("blocks	   {:7}\n", ctx.anim.animblocks.size());
            MdlPrint("total      {:7}\n", length);
        }

        // nothing reads the .ani data after this
        SaveOutputFile(ctx, blockFileName, std::move(*blocks));
    }

    ctx.header.checksum = pStudioHdr->checksum;
    if (pStudioHdr->numbodyparts != 0) {
        // vertices have become an external peer data store
        // write now prior to impending vertex access from any further code
        // vertex accessors hide shifting vertex data
        WriteVertices(ctx, pStudioHdr, extraTexcoordsToWrite);

        // vertex accessors read the .vvd through pVertexBase until FixupMDLFile clears it
        VertexFileCache vertexFileCache{ctx};
        pStudioHdr->pStudioHdr2()->pVertexBase = &vertexFileCache;

#ifdef _DEBUG
        int bodyPartID;
        for (bodyPartID = 0; bodyPartID < pStudioHdr->numbodyparts; bodyPartID++) {
            mstudiobodyparts_t* pBodyPart = pStudioHdr->pBodypart(bodyPartID);
            int modelID;
            for (modelID = 0; modelID < pBodyPart->nummodels; modelID++) {
                mstudiomodel_t* pModel = pBodyPart->pModel(modelID);
                const mstudio_modelvertexdata_t* vertData = pModel->GetVertexData(pStudioHdr);
                Assert(vertData); // This can only return nullptr on X360 for now
                int vertID;
                for (vertID = 0; vertID < pModel->numvertices; vertID++) {
                    Vector4D* pTangentS = vertData->TangentS(vertID);
                    Assert(pTangentS->w == -1.0f || pTangentS->w == 1.0f);
                }
            }
        }
#endif

        // the optimizer only reads the source bodyparts
        OptimizedModel::WriteOptimizedFiles(ctx, pStudioHdr, ctx.body.bodypart.data());

        // now have external finalized vtx (windings) and vvd (vertexes)
        // re-open files, sort vertexes, perform fixups, and rewrite
        // purposely isolated as a post process for stability
        if (!FixupToSortedLODVertexes(ctx, pStudioHdr)) {
            MdlError("Aborted vertex sort fixup on '{}':\n", filename);
        }

        if (!Clamp_RootLOD(ctx, pStudioHdr)) {
            MdlError("Aborted root lod shift '{}':\n", filename);
        }
    }

    // NOTE!  If you don't want to go through the effort of loading studiorender for perf reasons,
    // make sure spewFlags ends up being zero.
    unsigned int spewFlags = SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS;

    if (ctx.options.perf) {
        spewFlags |= SPEWPERFSTATS_SHOWPERF;
    }
    if (spewFlags) {
        SpewPerfStats(ctx, pStudioHdr, filename.c_str(), spewFlags);
    }

}

const vertexFileHeader_t* mstudiomodel_t::CacheVertexData(void* pModelData) {
    // pModelData is the studiohdr_t being written; its studiohdr2_t::pVertexBase holds the VertexFileCache set up by
    // WriteModelFiles
    Assert(pModelData != nullptr);
    const studiohdr_t* pStudioHdr = static_cast<const studiohdr_t*>(pModelData);
    void* pVertexBase = pStudioHdr->pStudioHdr2()->pVertexBase;
    Assert(pVertexBase != nullptr);
    VertexFileCache &cache = *static_cast<VertexFileCache*>(pVertexBase);

    if (cache.data) {
        return cache.data->reinterpret_at<vertexFileHeader_t>(0);
    }

    // load and persist the vertex file
    const std::string filename = OutputModelPath(cache.ctx, ".vvd");
    cache.data = LoadOutputFile(cache.ctx, filename);
    if (!cache.data) {
        MdlError("Error Vertex File: '{}' is missing\n", filename);
    }
    const auto* pVertexHdr = cache.data->reinterpret_at<vertexFileHeader_t>(0);

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

struct usedVertex_t {
    int32_t meshVertID;
    int32_t finalMeshVertID;
    int32_t vertexOffset;
    int32_t lodFlags;
};

struct lodMeshInfo_t {
    int offsets[MAX_NUM_LODS];
    int numVertexes[MAX_NUM_LODS];
};

struct vertexPool_t {
    std::vector<usedVertex_t> vertexList;
    std::vector<int> vertexMap{};
    lodMeshInfo_t lodMeshInfo{};
};


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
// UsedVertexLess
//
// Sort order of the lod sorted vertex list
//-----------------------------------------------------------------------------
static bool UsedVertexLess(const usedVertex_t &a, const usedVertex_t &b) {
    // determine highest (lowest detail) lod
    // forces grouping into discrete MAX_NUM_LODS sections
    const int lodA = Q_log2(a.lodFlags);
    const int lodB = Q_log2(b.lodFlags);

    // descending sort (LodN..Lod0)
    if (lodA != lodB)
        return lodA > lodB;

    // within same lod, sub sort (ascending) by mesh, within same mesh by vertex
    return std::tie(a.vertexOffset, a.meshVertID) < std::tie(b.vertexOffset, b.meshVertID);
}

//-----------------------------------------------------------------------------
// Key of the sorted-to-unsorted mapping: (mesh vertex offset, mesh relative vertex id)
//-----------------------------------------------------------------------------
static uint64_t UsedVertexKey(const int32_t vertexOffset, const int32_t meshVertID) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(vertexOffset)) << 32) | static_cast<uint32_t>(meshVertID);
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
    std::ranges::sort(vertexList, UsedVertexLess);

    // build a mapping table from mesh relative indexes to the flat lod sorted array
    std::unordered_map<uint64_t, int> usedVertexMap;
    usedVertexMap.reserve(numVertexes);
    for (k = 0; k < numVertexes; k++) {
        usedVertexMap.try_emplace(UsedVertexKey(vertexList[k].vertexOffset, vertexList[k].meshVertID), k);
    }
    vertexOffset = 0;
    for (i = 0; i < numVertexPools; i++) {
        pPool = &vertexPools[i];
        for (j = 0; j < pPool->vertexList.size(); j++) {
            // search flattened sorted vertexes
            const auto found = usedVertexMap.find(UsedVertexKey(vstd::as_signed_checked<int>(vertexOffset), j));
            Assert(found != usedVertexMap.end());
            pPool->vertexMap[j] = found->second;
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
static bool FixupVVDFile(StudioMdlState &ctx, const std::string_view fileName, const studiohdr_t* pStudioHdr,
                         const vstd::Buffer &pVtxBuff,
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

    auto oldVvdBuffOpt = LoadOutputFile(ctx, fileName);
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
    // the same data plus the fixup table and alignment padding
    buffer.reserve(oldVvdBuff.size() + numFixups * sizeof(vertexFileFixup_t) + 32);

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
        (void)buffer.allocate<uint8_t>(pExtraHeader_old->m_totalbytes);
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
    SaveOutputFile(ctx, fileName, std::move(buffer));

    // success
    return true;
}

//-----------------------------------------------------------------------------
// FixupVTXFile
//
// VTX files get their windings remapped.
//-----------------------------------------------------------------------------
bool FixupVTXFile(StudioMdlState &ctx, const std::string_view fileName, const studiohdr_t* pStudioHdr,
                  const std::vector<vertexPool_t> &vertexPools, const std::vector<usedVertex_t> &vertexList) {
    vstd::FunctionTimingRecorder _("Write");

    auto vtxBuffOpt = LoadOutputFile(ctx, fileName);
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

    SaveOutputFile(ctx, fileName, std::move(vtxBuff));

    return true;
}

//-----------------------------------------------------------------------------
// FixupMDLFile
//
// MDL files get flexes/vertex/tangent data offsets fixed
//-----------------------------------------------------------------------------
bool FixupMDLFile(StudioMdlState &ctx, const std::string_view fileName, const studiohdr_t* pStudioHdr,
                  const vstd::Buffer &pVtxBuff, const std::vector<vertexPool_t> &vertexPools,
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

    // the vertex accessors' .vvd (see VertexFileCache)
    pStudioHdr->pStudioHdr2()->pVertexBase = nullptr;

    // Clear vertex data

    {
        SaveOutputFile(ctx, fileName, pStudioHdr, pStudioHdr->length);
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
bool FixupToSortedLODVertexes(StudioMdlState &ctx, studiohdr_t* pStudioHdr) {
    vstd::FunctionTimingRecorder _("Write");
    std::vector<vertexPool_t> vertexPools;
    std::vector<usedVertex_t> vertexList;

    // determine lod usage per vertex
    // all vtx files enumerate model's lod verts, but differ in their mesh makeup
    // use xxx.dx90.vtx to establish which vertexes are used by each lod
    auto vtxBufferOpt = LoadOutputFile(ctx, OutputModelPath(ctx, ".dx90.vtx"));
    if (!vtxBufferOpt) {
        MdlError("Failed to load .dx90.vtx file for LOD fixup");
        return false;
    }
    const auto vtxBuffer = std::move(vtxBufferOpt.value());

    // build the sorted vertex tables
    if (!BuildSortedVertexList(pStudioHdr, vtxBuffer, vertexPools, vertexList)) {
        // data sync error
        return false;
    }

    // fixup ???.vvd
    if (!FixupVVDFile(ctx, OutputModelPath(ctx, ".vvd"), pStudioHdr, vtxBuffer, vertexPools, vertexList)) {
        // data error
        return false;
    }

    if (!FixupVTXFile(ctx, OutputModelPath(ctx, ".dx90.vtx"), pStudioHdr, vertexPools, vertexList)) {
        // data error
        return false;
    }

    // fixup ???.mdl
    if (!FixupMDLFile(ctx, OutputModelPath(ctx, ".mdl"), pStudioHdr, vtxBuffer, vertexPools, vertexList)) {
        // data error
        return false;
    }

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


static vstd::Buffer LoadOutputFileOrError(StudioMdlState &ctx, const std::string &fileName) {
    auto data = LoadOutputFile(ctx, fileName);
    if (!data) {
        MdlError("Failed to load '{}' for the root LOD clamp\n", fileName);
    }
    return std::move(*data);
}

static void Clamp_MDL_LODS(StudioMdlState &ctx, const std::string &fileName, const int rootLOD) {
    auto mdlData = LoadOutputFileOrError(ctx, fileName);

    Studio_SetRootLOD(mdlData.reinterpret_at<studiohdr_t>(0), rootLOD);

    SaveOutputFile(ctx, fileName, std::move(mdlData));
}


static void Clamp_VVD_LODS(StudioMdlState &ctx, const std::string &fileName, const int rootLOD, const bool bExtraData) {
    const auto vvdData = LoadOutputFileOrError(ctx, fileName);
    const auto* pTempVvdHdr = vvdData.reinterpret_at<vertexFileHeader_t>(0);

    const int newLength = Studio_VertexDataSize(pTempVvdHdr, rootLOD, true, bExtraData);

    vstd::Buffer newData(static_cast<size_t>(newLength)); // zero-filled
    auto* pNewVvdHdr = newData.reinterpret_at<vertexFileHeader_t>(0);

    Studio_LoadVertexes(pTempVvdHdr, pNewVvdHdr, rootLOD, true, bExtraData);

    if (!ctx.options.quiet) {
        MdlPrint("---------------------\n");
        MdlPrint("writing {}:\n", fileName);
        MdlPrint("vertices   ({} vertices)\n", pNewVvdHdr->numLODVertexes[0]);
    }

    SaveOutputFile(ctx, fileName, std::move(newData));
}


// Rewrites the .vtx without the LODs above rootLOD; their headers point at the root LOD's data
static void Clamp_VTX_LODS(StudioMdlState &ctx, const std::string &fileName, const int rootLOD,
                           const studiohdr_t* pStudioHdr) {
    namespace vtx = OptimizedModel;

    auto vtxData = LoadOutputFileOrError(ctx, fileName);
    auto* pVtxHdr = vtxData.reinterpret_at<vtx::FileHeader_t>(0);

    vstd::Buffer buffer;
    buffer.reserve(vtxData.size());

    // header
    const auto newHdr = buffer.allocate<vtx::FileHeader_t>();
    buffer.align(4);
    newHdr->version = pVtxHdr->version;
    newHdr->vertCacheSize = pVtxHdr->vertCacheSize;
    newHdr->maxBonesPerStrip = pVtxHdr->maxBonesPerStrip;
    newHdr->maxBonesPerFace = pVtxHdr->maxBonesPerFace;
    newHdr->maxBonesPerVert = pVtxHdr->maxBonesPerVert;
    newHdr->checkSum = pVtxHdr->checkSum;
    newHdr->numLODs = pVtxHdr->numLODs;

    // material replacement list
    const auto newLists = buffer.allocate<vtx::MaterialReplacementListHeader_t>(pVtxHdr->numLODs);
    newHdr->materialReplacementListOffset = newLists.offset_from<int>(newHdr);

    StringTable strings;
    BeginStringTable(strings);

    // allocate replacement list arrays
    for (int nLodID = rootLOD; nLodID < pVtxHdr->numLODs; nLodID++) {
        auto* pReplacementList = pVtxHdr->pMaterialReplacementList(nLodID);
        const auto newList = newLists + nLodID;

        newList->numReplacements = pReplacementList->numReplacements;
        const auto newReplacements = buffer.allocate<vtx::MaterialReplacementHeader_t>(newList->numReplacements);
        newList->replacementOffset = newReplacements.offset_from<int>(newList);

        for (int i = 0; i < pReplacementList->numReplacements; i++) {
            auto* pReplacement = pReplacementList->pMaterialReplacement(i);
            const auto newReplacement = newReplacements + i;

            newReplacement->materialID = pReplacement->materialID;
            AddToStringTable(strings, newReplacement,
                             newReplacement.field(&vtx::MaterialReplacementHeader_t::replacementMaterialNameOffset),
                             pReplacement->pMaterialReplacementName());
        }
    }
    WriteStringTable(strings, buffer);

    // link previous LODs to higher LODs
    const auto rootList = newLists + rootLOD;
    for (int nLodID = 0; nLodID < rootLOD; nLodID++) {
        const auto newList = newLists + nLodID;
        newList->numReplacements = rootList->numReplacements;
        newList->replacementOffset = rootList->replacementOffset + rootList.offset_from<int>(newList);
    }

    // body parts
    newHdr->numBodyParts = pStudioHdr->numbodyparts;
    const auto newBodyParts = buffer.allocate<vtx::BodyPartHeader_t>(newHdr->numBodyParts);
    newHdr->bodyPartOffset = newBodyParts.offset_from<int>(newHdr);

    // Iterate over every body part...
    for (int i = 0; i < pStudioHdr->numbodyparts; i++) {
        const mstudiobodyparts_t* pBodyPart = pStudioHdr->pBodypart(i);
        auto* pVtxBodyPart = pVtxHdr->pBodyPart(i);
        const auto newBodyPart = newBodyParts + i;

        newBodyPart->numModels = pBodyPart->nummodels;
        const auto newModels = buffer.allocate<vtx::ModelHeader_t>(newBodyPart->numModels);
        newBodyPart->modelOffset = newModels.offset_from<int>(newBodyPart);

        // Iterate over every submodel...
        for (int j = 0; j < pBodyPart->nummodels; ++j) {
            const mstudiomodel_t* pModel = pBodyPart->pModel(j);
            auto* pVtxModel = pVtxBodyPart->pModel(j);
            const auto newModel = newModels + j;

            newModel->numLODs = pVtxModel->numLODs;
            const auto newLODs = buffer.allocate<vtx::ModelLODHeader_t>(newModel->numLODs);
            newModel->lodOffset = newLODs.offset_from<int>(newModel);
            buffer.align(4);

            for (int nLodID = rootLOD; nLodID < pVtxModel->numLODs; nLodID++) {
                auto* pVtxLOD = pVtxModel->pLOD(nLodID);
                const auto newLOD = newLODs + nLodID;

                newLOD->numMeshes = pVtxLOD->numMeshes;
                newLOD->switchPoint = pVtxLOD->switchPoint;
                const auto newMeshes = buffer.allocate<vtx::MeshHeader_t>(newLOD->numMeshes);
                newLOD->meshOffset = newMeshes.offset_from<int>(newLOD);
                buffer.align(4);

                // Iterate over all the meshes....
                for (int k = 0; k < pModel->nummeshes; ++k) {
                    Assert(pModel->nummeshes == pVtxLOD->numMeshes);
                    auto* pVtxMesh = pVtxLOD->pMesh(k);
                    const auto newMesh = newMeshes + k;

                    newMesh->numStripGroups = pVtxMesh->numStripGroups;
                    newMesh->flags = pVtxMesh->flags;
                    const auto newStripGroups = buffer.allocate<vtx::StripGroupHeader_t>(newMesh->numStripGroups);
                    newMesh->stripGroupHeaderOffset = newStripGroups.offset_from<int>(newMesh);

                    for (int m = 0; m < pVtxMesh->numStripGroups; m++) {
                        auto* pStripGroup = pVtxMesh->pStripGroup(m);
                        const auto newStripGroup = newStripGroups + m;

                        newStripGroup->numVerts = pStripGroup->numVerts;
                        const auto newVerts = buffer.allocate<vtx::Vertex_t>(pStripGroup->numVerts);
                        newStripGroup->vertOffset = newVerts.offset_from<int>(newStripGroup);
                        memcpy(newVerts.get(), pStripGroup->pVertex(0), pStripGroup->numVerts * sizeof(vtx::Vertex_t));

                        newStripGroup->numIndices = pStripGroup->numIndices;
                        const auto newIndices = buffer.allocate<unsigned short>(pStripGroup->numIndices);
                        newStripGroup->indexOffset = newIndices.offset_from<int>(newStripGroup);
                        memcpy(newIndices.get(), pStripGroup->pIndex(0), pStripGroup->numIndices * sizeof(unsigned short));

                        newStripGroup->numStrips = pStripGroup->numStrips;
                        const auto newStrips = buffer.allocate<vtx::StripHeader_t>(pStripGroup->numStrips);
                        newStripGroup->stripOffset = newStrips.offset_from<int>(newStripGroup);

                        newStripGroup->flags = pStripGroup->flags;

                        for (int n = 0; n < pStripGroup->numStrips; n++) {
                            auto* pStrip = pStripGroup->pStrip(n);
                            const auto newStrip = newStrips + n;

                            newStrip->numIndices = pStrip->numIndices;
                            newStrip->indexOffset = pStrip->indexOffset;

                            newStrip->numVerts = pStrip->numVerts;
                            newStrip->vertOffset = pStrip->vertOffset;

                            newStrip->numBones = pStrip->numBones;
                            newStrip->flags = pStrip->flags;

                            newStrip->numBoneStateChanges = pStrip->numBoneStateChanges;
                            const auto newChanges =
                                    buffer.allocate<vtx::BoneStateChangeHeader_t>(pStrip->numBoneStateChanges);
                            newStrip->boneStateChangeOffset = newChanges.offset_from<int>(newStrip);
                            memcpy(newChanges.get(), pStrip->pBoneStateChange(0),
                                   pStrip->numBoneStateChanges * sizeof(vtx::BoneStateChangeHeader_t));
                        }
                    }
                }
            }

            // link previous LODs to higher LODs
            const auto rootVtxLOD = newLODs + rootLOD;
            for (int nLodID = 0; nLodID < rootLOD; nLodID++) {
                const auto newLOD = newLODs + nLodID;
                newLOD->numMeshes = rootVtxLOD->numMeshes;
                newLOD->switchPoint = pVtxModel->pLOD(nLodID)->switchPoint;
                newLOD->meshOffset = rootVtxLOD->meshOffset + rootVtxLOD.offset_from<int>(newLOD);
            }
        }
    }

    if (!ctx.options.quiet) {
        MdlPrint("writing {}:\n", fileName);
        MdlPrint("everything ({} bytes)\n", buffer.size());
    }

    SaveOutputFile(ctx, fileName, std::move(buffer));
}


bool Clamp_RootLOD(StudioMdlState &ctx, const studiohdr_t* phdr) {
    vstd::FunctionTimingRecorder _("Write");

    const bool extraData = (phdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) != 0;

    int rootLOD = ctx.options.minLod;

    if (rootLOD > std::ssize(ctx.body.ScriptLODs) - 1) {
        rootLOD = std::ssize(ctx.body.ScriptLODs) - 1;
    }

    if (rootLOD == 0) {
        return true;
    }

    // shift the files so that g_minLod is the root LOD
    Clamp_MDL_LODS(ctx, OutputModelPath(ctx, ".mdl"), rootLOD);
    Clamp_VVD_LODS(ctx, OutputModelPath(ctx, ".vvd"), rootLOD, extraData);
    Clamp_VTX_LODS(ctx, OutputModelPath(ctx, ".dx90.vtx"), rootLOD, phdr);

    return true;
}
