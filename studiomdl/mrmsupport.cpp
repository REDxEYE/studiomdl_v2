//========= Copyright � 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//


//
// studiomdl.c: generates a studio .mdl file from a .qc script
// models/<scriptname>.mdl.
//


#pragma warning( disable : 4244 )
#pragma warning( disable : 4237 )
#pragma warning( disable : 4305 )


#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include "common/scriplib.h"
#include "mathlib/mathlib.h"
#include "studio.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/sort_utils.hpp"

bool IsEnd(char const *pLine) {
    if (strncmp("end", pLine, 3) != 0)
        return false;
    // anything after "end" must be whitespace (CRLF files keep the '\r' when read in binary mode)
    for (const char *p = pLine + 3; *p; ++p) {
        if (!V_isspace(*p))
            return false;
    }
    return true;
}


static void Grab_Vertexlist(StudioMdlState &ctx, s_source_t *psource) {
    while (1) {
        if (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
            nullptr) {
            int j;
            int bone;
            Vector p;
            int iCount;
            std::array<int, 4> bones{};
            std::array<float, 4> weights{};

            ctx.input.iLinecount++;

            // check for end
            if (IsEnd(ctx.input.szLine))
                return;


            int i = sscanf(ctx.input.szLine, "%d %d %f %f %f %d %d %f %d %f %d %f %d %f",
                           &j,
                           &bone,
                           &p[0], &p[1], &p[2],
                           &iCount,
                           &bones[0], &weights[0], &bones[1], &weights[1], &bones[2], &weights[2], &bones[3],
                           &weights[3]);

            if (i == 5) {
                if (bone < 0 || bone >= psource->numbones) {
                    MdlWarning("bogus bone index\n");
                    MdlWarning("{} {} :\n{}", ctx.input.iLinecount, ctx.input.szFilename,
                               ctx.input.szLine);
                    MdlError("Exiting due to errors\n");
                }

                VectorCopy(p, ctx.input.vertex[j]);
                s_boneweight_t &boneWeight = ctx.input.bone[j];
                boneWeight.numbones = 1;
                boneWeight.bone[0] = bone;
                boneWeight.weight[0] = 1.0;
                ctx.input.sourceBone[j] = {};
            }
            else if (i > 5) {
                MakeSourceBoneWeights(iCount, bones, weights, ctx.input.sourceBone[j]);
                iCount = SortAndBalanceBones(iCount, MAXSTUDIOBONEWEIGHTS, bones, weights);

                VectorCopy(p, ctx.input.vertex[j]);
                s_boneweight_t &boneWeight = ctx.input.bone[j];
                boneWeight.numbones = iCount;
                for (i = 0; i < iCount; i++) {
                    boneWeight.bone[i] = bones[i];
                    boneWeight.weight[i] = weights[i];
                }
            }
            else {
                MdlError("{}: error on line {}: {}", ctx.input.szFilename,
                         ctx.input.iLinecount, ctx.input.szLine);
            }
        }
    }
}


static void Grab_Facelist(StudioMdlState &ctx, s_source_t *psource) {
    while (1) {
        if (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
            nullptr) {
            int j;
            s_tmpface_t f;

            ctx.input.iLinecount++;

            // check for end
            if (IsEnd(ctx.input.szLine))
                return;

            if (sscanf(ctx.input.szLine, "%d %d %d %d",
                       &j,
                       &f.a, &f.b, &f.c) == 4) {
                ctx.input.faces[j] = f;
            }
            else {
                MdlError("{}: error on line {}: {}", ctx.input.szFilename,
                         ctx.input.iLinecount, ctx.input.szLine);
            }
        }
    }
}


static void Grab_Materiallist(StudioMdlState &ctx, s_source_t *psource) {
    while (1) {
        if (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
            nullptr) {
            // char name[256];
            char path[MAX_PATH];
            rgb2_t a, d, s;
            float g;
            int j;

            ctx.input.iLinecount++;

            // check for end
            if (IsEnd(ctx.input.szLine))
                return;

            if (sscanf(ctx.input.szLine, "%d  %f %f %f %f   %f %f %f %f  %f %f %f %f  %f \"%[^\"]s",
                       &j,
                       &a.r, &a.g, &a.b, &a.a,
                       &d.r, &d.g, &d.b, &d.a,
                       &s.r, &s.g, &s.b, &s.a,
                       &g,
                       path) == 15) {
                if (path[0] == '\0') {
                    psource->texmap[j] = -1;
                }
                else if (j < sizeof(psource->texmap)) {
                    psource->texmap[j] = LookupTexture(ctx, path);
                }
                else {
                    MdlError("Too many materials, max {}\n", sizeof(psource->texmap));
                }
            }
        }
    }
}


static void Grab_Texcoordlist(StudioMdlState &ctx, s_source_t *psource) {
    while (1) {
        if (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
            nullptr) {
            int j;
            Vector2D t;

            ctx.input.iLinecount++;

            // check for end
            if (IsEnd(ctx.input.szLine))
                return;

            if (sscanf(ctx.input.szLine, "%d %f %f",
                       &j,
                       &t[0], &t[1]) == 3) {
                t[1] = 1.0 - t[1];
                ctx.input.texcoord[0][j] = t;
            }
            else {
                MdlError("{}: error on line {}: {}", ctx.input.szFilename,
                         ctx.input.iLinecount, ctx.input.szLine);
            }
        }
    }
}


static void Grab_Normallist(StudioMdlState &ctx, s_source_t *psource) {
    while (1) {
        if (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
            nullptr) {
            int j;
            int bone;
            Vector n;

            ctx.input.iLinecount++;

            // check for end
            if (IsEnd(ctx.input.szLine))
                return;


            if (sscanf(ctx.input.szLine, "%d %d %f %f %f",
                       &j,
                       &bone,
                       &n[0], &n[1], &n[2]) == 5) {
                if (bone < 0 || bone >= psource->numbones) {
                    MdlWarning("bogus bone index\n");
                    MdlWarning("{} {} :\n{}", ctx.input.iLinecount, ctx.input.szFilename,
                               ctx.input.szLine);
                    MdlError("Exiting due to errors\n");
                }

                VectorCopy(n, ctx.input.normal[j]);
            }
            else {
                MdlError("{}: error on line {}: {}", ctx.input.szFilename,
                         ctx.input.iLinecount, ctx.input.szLine);
            }
        }
    }
}


static void Grab_Faceattriblist(StudioMdlState &ctx, s_source_t *psource) {
    while (1) {
        if (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
            nullptr) {
            int j;
            int smooth;
            int material;
            s_tmpface_t f;
            unsigned short s;

            ctx.input.iLinecount++;

            // check for end
            if (IsEnd(ctx.input.szLine))
                return;

            if (sscanf(ctx.input.szLine, "%d %d %d %d %d %d %d %d %d",
                       &j,
                       &material,
                       &smooth,
                       &f.ta[0], &f.tb[0], &f.tc[0],
                       &f.na, &f.nb, &f.nc) == 9) {
                f.a = ctx.input.faces[j].a;
                f.b = ctx.input.faces[j].b;
                f.c = ctx.input.faces[j].c;

                f.material = UseTextureAsMaterial(ctx, psource->texmap[material]);
                if (f.material < 0) {
                    MdlError("face {} references NULL texture {}\n", j, material);
                }

                if (1) {
                    s = f.b;
                    f.b = f.c;
                    f.c = s;
                    s = f.tb[0];
                    f.tb[0] = f.tc[0];
                    f.tc[0] = s;
                    s = f.nb;
                    f.nb = f.nc;
                    f.nc = s;
                }

                ctx.input.faces[j] = f;
            }
            else {
                MdlError("{}: error on line {}: {}", ctx.input.szFilename,
                         ctx.input.iLinecount, ctx.input.szLine);
            }
        }
    }
}


static int AddToVlist(StudioMdlState &ctx, int v, int m, int n, std::array<uint32_t, MAXSTUDIOTEXCOORDS> &t, int firstref) {
    auto &entries = ctx.input.v_listdata;
    auto &heads = ctx.input.v_list;
    if (v < 0)
        MdlError("Face references an invalid vertex index {}\n", v);
    if (v >= static_cast<int>(heads.size()))
        heads.resize(v + 1, -1);

    int prev = -1;
    for (int cur = heads[v]; cur >= 0; cur = entries[cur].next) {
        v_unify_t &entry = entries[cur];
        if (entry.m == m && entry.n == n) {
            bool bMatch = true;
            for (int i = 0; (i < MAXSTUDIOTEXCOORDS) && bMatch; ++i) {
                if (entry.t[i] != t[i]) {
                    bMatch = false;
                }
            }
            if (bMatch) {
                entry.refcount++;
                return cur;
            }
        }
        prev = cur;
    }

    if (entries.size() >= MAXSTUDIOSRCVERTS) {
        MdlError("Too many unified vertices\n");
    }

    const int index = static_cast<int>(entries.size());
    v_unify_t &entry = entries.emplace_back();
    entry.lastref = -1;
    entry.refcount = 1;
    entry.v = v;
    entry.m = m;
    entry.n = n;
    for (int i = 0; i < MAXSTUDIOTEXCOORDS; ++i) {
        entry.t[i] = t[i];
    }

    if (prev >= 0) {
        entries[prev].next = index;
    }
    else {
        heads[v] = index;
    }

    return index;
}


void UnifyIndices(StudioMdlState &ctx, s_source_t *psource) {
    vstd::FunctionTimingRecorder _("DMX");
    int i;

    s_face_t uface;

    // reset the unified vertices; the vectors keep their capacity for the next source
    ctx.input.v_listdata.clear();
    ctx.input.v_list.assign(static_cast<int>(ctx.input.vertex.size()), -1);

    // create an list of all the
    ctx.input.src_uface.resize(ctx.input.faces.size());
    for (i = 0; i < ctx.input.faces.size(); i++) {
        uface.a = AddToVlist(ctx, ctx.input.faces[i].a, ctx.input.faces[i].material,
                             ctx.input.faces[i].na, ctx.input.faces[i].ta, static_cast<int>(ctx.input.vertex.size()));
        uface.b = AddToVlist(ctx, ctx.input.faces[i].b, ctx.input.faces[i].material,
                             ctx.input.faces[i].nb, ctx.input.faces[i].tb, static_cast<int>(ctx.input.vertex.size()));
        uface.c = AddToVlist(ctx, ctx.input.faces[i].c, ctx.input.faces[i].material,
                             ctx.input.faces[i].nc, ctx.input.faces[i].tc, static_cast<int>(ctx.input.vertex.size()));
        uface.d = 0xFFFFFFFF;

        if (ctx.input.faces[i].d != 0xFFFFFFFF) {
            uface.d = AddToVlist(ctx, ctx.input.faces[i].d, ctx.input.faces[i].material,
                                 ctx.input.faces[i].nd, ctx.input.faces[i].td,
                                 static_cast<int>(ctx.input.vertex.size()));
        }

        // keep an original copy
        ctx.input.src_uface[i] = uface;
    }
}

void CalcModelTangentSpaces(StudioMdlState &ctx, s_source_t *pSrc);


//-----------------------------------------------------------------------------
// Builds a list of unique vertices in a source
//-----------------------------------------------------------------------------
static void BuildUniqueVertexList(StudioMdlState &ctx, s_source_t *pSource, const int *pDesiredToVList) {
    // allocate memory
    pSource->vertex = ctx.arena.CreateArray<s_vertexinfo_t>(pSource->numvertices);

    int numValidTexcoords = 1;

    for (int i = 1; i < MAXSTUDIOTEXCOORDS; ++i) {
        if (static_cast<int>(ctx.input.texcoord[i].size())) {
            numValidTexcoords++;
        }
        else {
            break;
        }
    }

    // create arrays of unique vertexes, normals, texcoords.
    for (int i = 0; i < pSource->numvertices; i++) {
        int j = pDesiredToVList[i];

        s_vertexinfo_t &vertex = pSource->vertex[i];
        VectorCopy(ctx.input.vertex[ctx.input.v_listdata[j].v], vertex.position);
        VectorCopy(ctx.input.normal[ctx.input.v_listdata[j].n], vertex.normal);

        vertex.boneweight.numbones = ctx.input.bone[ctx.input.v_listdata[j].v].numbones;
        int k;
        for (k = 0; k < MAXSTUDIOBONEWEIGHTS; k++) {
            vertex.boneweight.bone[k] = ctx.input.bone[ctx.input.v_listdata[j].v].bone[k];
            vertex.boneweight.weight[k] = ctx.input.bone[ctx.input.v_listdata[j].v].weight[k];
        }
        const int v = ctx.input.v_listdata[j].v;
        vertex.sourceWeights = v < static_cast<int>(ctx.input.sourceBone.size()) ? ctx.input.sourceBone[v]
                                                                                    : s_sourceboneweight_t{};

        vertex.texcoord.resize(numValidTexcoords);

        for (k = 0; k < numValidTexcoords; ++k) {
            Vector2Copy(ctx.input.texcoord[k][ctx.input.v_listdata[j].t[k]], vertex.texcoord[k]);
        }

        // store a bunch of other info
        vertex.material = ctx.input.v_listdata[j].m;

#if 0
        pSource->vertexInfo[i].firstref = v_listdata[j].firstref;
        pSource->vertexInfo[i].lastref = v_listdata[j].lastref;
#endif
        // printf("%4d : %2d :  %6.2f %6.2f %6.2f\n", i, psource->boneweight[i].bone[0], psource->vertex[i][0], psource->vertex[i][1], psource->vertex[i][2] );
    }
}


//-----------------------------------------------------------------------------
// sort new vertices by materials, last used
//-----------------------------------------------------------------------------
static int vlistCompare(const void *elem1, const void *elem2, void *context) {
    const auto &v_listdata = *static_cast<const decltype(StudioMdlInput::v_listdata) *>(context);
    const int i1 = *(const int *) elem1;
    const int i2 = *(const int *) elem2;
    const v_unify_t *u1 = &v_listdata[i1];
    const v_unify_t *u2 = &v_listdata[i2];

    // sort by material
    if (u1->m < u2->m)
        return -1;
    if (u1->m > u2->m)
        return 1;

    // sort by last used
    if (u1->lastref < u2->lastref)
        return -1;
    if (u1->lastref > u2->lastref)
        return 1;

    // vertices of one face share lastref: keep creation order, qsort is not stable on every CRT
    return (i1 > i2) - (i1 < i2);
}

static void SortVerticesByMaterial(StudioMdlState &ctx, int *pDesiredToVList, int *pVListToDesired) {
    const int count = static_cast<int>(ctx.input.v_listdata.size());
    for (int i = 0; i < count; i++) {
        pDesiredToVList[i] = i;
    }
    vstd::qsort_with_context(pDesiredToVList, count, sizeof(int), vlistCompare, &ctx.input.v_listdata);
    for (int i = 0; i < count; i++) {
        pVListToDesired[pDesiredToVList[i]] = i;
    }
}


//-----------------------------------------------------------------------------
// sort new faces by materials, last used
//-----------------------------------------------------------------------------
static int faceCompare(const void *elem1, const void *elem2, void *context) {
    const auto &faces = *static_cast<const decltype(StudioMdlInput::faces) *>(context);
    int i1 = *(int *) elem1;
    int i2 = *(int *) elem2;

    // sort by material
    if (faces[i1].material < faces[i2].material)
        return -1;
    if (faces[i1].material > faces[i2].material)
        return 1;

    // sort by original usage
    if (i1 < i2)
        return -1;
    if (i1 > i2)
        return 1;

    return 0;
}

static void SortFacesByMaterial(StudioMdlState &ctx, int *pDesiredToSrcFace) {
    // NOTE: Unlike SortVerticesByMaterial, srcFaceToDesired isn't needed, so we're not computing it
    for (int i = 0; i < ctx.input.faces.size(); i++) {
        pDesiredToSrcFace[i] = i;
    }
    vstd::qsort_with_context(pDesiredToSrcFace, ctx.input.faces.size(), sizeof(int), faceCompare, &ctx.input.faces);
}


//-----------------------------------------------------------------------------
// Builds mesh structures in the source
//-----------------------------------------------------------------------------
static void PointMeshesToVertexAndFaceData(StudioMdlState &ctx, s_source_t *pSource, int *pDesiredToSrcFace) {
    // A mesh is a set of faces + vertices that all use 1 material; count them per material id first
    std::array<s_mesh_t, MAXSTUDIOSKINS> perMaterial{};
    for (int m = 0; m < MAXSTUDIOSKINS; m++) {
        perMaterial[m].material = m;
        perMaterial[m].numvertices = 0;
        perMaterial[m].vertexoffset = pSource->numvertices;

        perMaterial[m].numfaces = 0;
        perMaterial[m].faceoffset = pSource->numfaces;
    }

    // find first and count of vertices per material
    for (int i = 0; i < pSource->numvertices; i++) {
        int m = pSource->vertex[i].material;
        perMaterial[m].numvertices++;
        if (perMaterial[m].vertexoffset > i) {
            perMaterial[m].vertexoffset = i;
        }
    }

    // find first and count of faces per material
    for (int i = 0; i < pSource->numfaces; i++) {
        int m = ctx.input.faces[pDesiredToSrcFace[i]].material;

        perMaterial[m].numfaces++;
        if (perMaterial[m].faceoffset > i) {
            perMaterial[m].faceoffset = i;
        }
    }

    // the meshes are the materials with faces, in material order
    pSource->meshes.clear();
    for (const s_mesh_t &mesh: perMaterial) {
        if (mesh.numfaces)
            pSource->meshes.push_back(mesh);
    }
}


//-----------------------------------------------------------------------------
// Builds the face list in the mesh
//-----------------------------------------------------------------------------
static void BuildFaceList(StudioMdlState &ctx, s_source_t *pSource, int *pVListToDesired, int *pDesiredToSrcFace) {
    pSource->face = ctx.arena.CreateArray<s_face_t>(pSource->numfaces);
    for (const s_mesh_t &mesh: pSource->meshes) {
        for (int i = mesh.faceoffset; i < mesh.numfaces + mesh.faceoffset; i++) {
            int j = pDesiredToSrcFace[i];

            // NOTE: per-face vertex indices a,b,c,d are mesh relative (hence the subtraction), while ctx.input.src_uface are model relative
            pSource->face[i].a = pVListToDesired[ctx.input.src_uface[j].a] - mesh.vertexoffset;
            pSource->face[i].b = pVListToDesired[ctx.input.src_uface[j].b] - mesh.vertexoffset;
            pSource->face[i].c = pVListToDesired[ctx.input.src_uface[j].c] - mesh.vertexoffset;

            if (ctx.input.src_uface[j].d != 0xFFFFFFFF) {
                pSource->face[i].d = pVListToDesired[ctx.input.src_uface[j].d] - mesh.vertexoffset;
            }

            Assert(((pSource->face[i].a & 0xF0000000) == 0) && ((pSource->face[i].b & 0xF0000000) == 0) &&
                ((pSource->face[i].c & 0xF0000000) == 0) && (((pSource->face[i].d & 0xF0000000) == 0) || (pSource->face[
                    i].d == 0xFFFFFFFF)));
            // printf("%3d : %4d %4d %4d %4d\n", i, pSource->face[i].a, pSource->face[i].b, pSource->face[i].c, pSource->face[i].d );
        }
    }
}


//-----------------------------------------------------------------------------
// Remaps the vertex animations based on the new vertex ordering
//-----------------------------------------------------------------------------
static void RemapVertexAnimations(s_source_t *pSource, int *pVListToDesired) {
    vstd::AutoGrowVector<int> temp;
    const int nAnimationCount = std::ssize(pSource->m_Animations);
    for (int i = 0; i < nAnimationCount; ++i) {
        s_sourceanim_t &anim = pSource->m_Animations[i];
        if (!anim.newStyleVertexAnimations)
            continue;

        for (int j = 0; j < MAXSTUDIOANIMFRAMES; ++j) {
            int nVAnimCount = anim.numvanims[j];
            if (nVAnimCount == 0)
                continue;

            // Copy off the initial vertex data
            // Have to do it in 2 loops because it'll overwrite itself if we do it in 1
            for (int k = 0; k < nVAnimCount; ++k) {
                temp[k] = anim.vanim[j][k].vertex;
            }

            for (int k = 0; k < nVAnimCount; ++k) {
                // NOTE: vertex animations are model relative, not mesh relative
                anim.vanim[j][k].vertex = pVListToDesired[temp[k]];
            }
        }
    }
}


//-----------------------------------------------------------------------------
// Sorts vertices by material type, re-maps data structures that refer to those vertices
// to use the new indices
//-----------------------------------------------------------------------------
void BuildIndividualMeshes(StudioMdlState &ctx, s_source_t *pSource) {
    vstd::FunctionTimingRecorder _("DMX");
    const size_t numvlist = ctx.input.v_listdata.size();
    std::vector<int> v_listsort(numvlist); // map desired order to vlist entry
    std::vector<int> v_ilistsort(numvlist); // map vlist entry to desired order
    std::vector<int> facesort(ctx.input.faces.size()); // map desired order to src_face entry

    SortVerticesByMaterial(ctx, v_listsort.data(), v_ilistsort.data());
    SortFacesByMaterial(ctx, facesort.data());

    pSource->numvertices = static_cast<int>(numvlist);
    pSource->numfaces = ctx.input.faces.size();

    BuildUniqueVertexList(ctx, pSource, v_listsort.data());
    PointMeshesToVertexAndFaceData(ctx, pSource, facesort.data());
    BuildFaceList(ctx, pSource, v_ilistsort.data(), facesort.data());
    RemapVertexAnimations(pSource, v_ilistsort.data());
    CalcModelTangentSpaces(ctx, pSource);

    ctx.input.unifiedVertexOrder = std::move(v_listsort);
}


static void Grab_MRMFaceupdates(StudioMdlState &ctx, s_source_t *psource) {
    while (1) {
        if (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
            nullptr) {
            ctx.input.iLinecount++;

            // check for end
            if (IsEnd(ctx.input.szLine))
                return;
        }
    }
}

int Load_VRM(StudioMdlState &ctx, s_source_t *psource) {
    char cmd[1024];
    int option;

    if (!OpenGlobalFile(ctx, psource->filename)) {
        return 0;
    }

    if (!ctx.options.quiet) {
        MdlPrint("grabbing {}\n", psource->filename);
    }

    ctx.input.iLinecount = 0;

    while (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) !=
           nullptr) {
        ctx.input.iLinecount++;
        sscanf(ctx.input.szLine, "%1023s %d", cmd, &option);
        if (stricmp(cmd, "version") == 0) {
            if (option != 2) {
                MdlError("bad version\n");
            }
        }
        else if (stricmp(cmd, "name") == 0) {
        }
        else if (stricmp(cmd, "vertices") == 0) {
            ctx.input.vertex.resize(option);
            ctx.input.bone.resize(option);
        }
        else if (stricmp(cmd, "faces") == 0) {
            ctx.input.faces.resize(option);
        }
        else if (stricmp(cmd, "materials") == 0) {
            // doesn't matter;
        }
        else if (stricmp(cmd, "texcoords") == 0) {
            ctx.input.texcoord[0].resize(option);
            if (option == 0)
                MdlError("model has no texture coordinates\n");
        }
        else if (stricmp(cmd, "normals") == 0) {
            ctx.input.normal.resize(option);
        }
        else if (stricmp(cmd, "tristrips") == 0) {
            // should be 0;
        }

        else if (stricmp(cmd, "vertexlist") == 0) {
            Grab_Vertexlist(ctx, psource);
        }
        else if (stricmp(cmd, "facelist") == 0) {
            Grab_Facelist(ctx, psource);
        }
        else if (stricmp(cmd, "materiallist") == 0) {
            Grab_Materiallist(ctx, psource);
        }
        else if (stricmp(cmd, "texcoordlist") == 0) {
            Grab_Texcoordlist(ctx, psource);
        }
        else if (stricmp(cmd, "normallist") == 0) {
            Grab_Normallist(ctx, psource);
        }
        else if (stricmp(cmd, "faceattriblist") == 0) {
            Grab_Faceattriblist(ctx, psource);
        }

        else if (stricmp(cmd, "MRM") == 0) {
        }
        else if (stricmp(cmd, "MRMvertices") == 0) {
        }
        else if (stricmp(cmd, "MRMfaces") == 0) {
        }
        else if (stricmp(cmd, "MRMfaceupdates") == 0) {
            Grab_MRMFaceupdates(ctx, psource);
        }

        else if (stricmp(cmd, "nodes") == 0) {
            psource->numbones = Grab_Nodes(ctx, psource->localBone);
        }
        else if (stricmp(cmd, "skeleton") == 0) {
            Grab_Animation(ctx, psource, "BindPose");
        }
        /*
                else if (stricmp( cmd, "triangles" ) == 0) {
                    Grab_Triangles( psource );
                }
        */
        else {
            MdlError("unknown VRM command : {} \n", cmd);
        }
    }

    UnifyIndices(ctx, psource);
    BuildIndividualMeshes(ctx, psource);

    ctx.input.inputFile.reset();

    return 1;
}
