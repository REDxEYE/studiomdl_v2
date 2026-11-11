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


void Grab_Vertexlist(s_source_t *psource) {
    while (1) {
        if (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
            nullptr) {
            int j;
            int bone;
            Vector p;
            int iCount;
            std::array<int, 4> bones{};
            std::array<float, 4> weights{};

            g_StudioMdlContext.iLinecount++;

            // check for end
            if (IsEnd(g_StudioMdlContext.szLine))
                return;


            int i = sscanf(g_StudioMdlContext.szLine, "%d %d %f %f %f %d %d %f %d %f %d %f %d %f",
                           &j,
                           &bone,
                           &p[0], &p[1], &p[2],
                           &iCount,
                           &bones[0], &weights[0], &bones[1], &weights[1], &bones[2], &weights[2], &bones[3],
                           &weights[3]);

            if (i == 5) {
                if (bone < 0 || bone >= psource->numbones) {
                    MdlWarning("bogus bone index\n");
                    MdlWarning("{} {} :\n{}", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szFilename,
                               g_StudioMdlContext.szLine);
                    MdlError("Exiting due to errors\n");
                }

                VectorCopy(p, g_StudioMdlContext.vertex[j]);
                g_StudioMdlContext.bone[j].numbones = 1;
                g_StudioMdlContext.bone[j].bone[0] = bone;
                g_StudioMdlContext.bone[j].weight[0] = 1.0;
            }
            else if (i > 5) {
                iCount = SortAndBalanceBones(iCount, MAXSTUDIOBONEWEIGHTS, bones, weights);

                VectorCopy(p, g_StudioMdlContext.vertex[j]);
                g_StudioMdlContext.bone[j].numbones = iCount;
                for (i = 0; i < iCount; i++) {
                    g_StudioMdlContext.bone[j].bone[i] = bones[i];
                    g_StudioMdlContext.bone[j].weight[i] = weights[i];
                }
            }
            else {
                MdlError("{}: error on line {}: {}", g_StudioMdlContext.szFilename,
                         g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            }
        }
    }
}


void Grab_Facelist(s_source_t *psource) {
    while (1) {
        if (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
            nullptr) {
            int j;
            s_tmpface_t f;

            g_StudioMdlContext.iLinecount++;

            // check for end
            if (IsEnd(g_StudioMdlContext.szLine))
                return;

            if (sscanf(g_StudioMdlContext.szLine, "%d %d %d %d",
                       &j,
                       &f.a, &f.b, &f.c) == 4) {
                g_StudioMdlContext.faces[j] = f;
            }
            else {
                MdlError("{}: error on line {}: {}", g_StudioMdlContext.szFilename,
                         g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            }
        }
    }
}


void Grab_Materiallist(s_source_t *psource) {
    while (1) {
        if (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
            nullptr) {
            // char name[256];
            char path[MAX_PATH];
            rgb2_t a, d, s;
            float g;
            int j;

            g_StudioMdlContext.iLinecount++;

            // check for end
            if (IsEnd(g_StudioMdlContext.szLine))
                return;

            if (sscanf(g_StudioMdlContext.szLine, "%d  %f %f %f %f   %f %f %f %f  %f %f %f %f  %f \"%[^\"]s",
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
                    psource->texmap[j] = LookupTexture(path);
                }
                else {
                    MdlError("Too many materials, max {}\n", sizeof(psource->texmap));
                }
            }
        }
    }
}


void Grab_Texcoordlist(s_source_t *psource) {
    while (1) {
        if (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
            nullptr) {
            int j;
            Vector2D t;

            g_StudioMdlContext.iLinecount++;

            // check for end
            if (IsEnd(g_StudioMdlContext.szLine))
                return;

            if (sscanf(g_StudioMdlContext.szLine, "%d %f %f",
                       &j,
                       &t[0], &t[1]) == 3) {
                t[1] = 1.0 - t[1];
                g_StudioMdlContext.texcoord[0][j][0] = t[0];
                g_StudioMdlContext.texcoord[0][j][1] = t[1];
            }
            else {
                MdlError("{}: error on line {}: {}", g_StudioMdlContext.szFilename,
                         g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            }
        }
    }
}


void Grab_Normallist(s_source_t *psource) {
    while (1) {
        if (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
            nullptr) {
            int j;
            int bone;
            Vector n;

            g_StudioMdlContext.iLinecount++;

            // check for end
            if (IsEnd(g_StudioMdlContext.szLine))
                return;


            if (sscanf(g_StudioMdlContext.szLine, "%d %d %f %f %f",
                       &j,
                       &bone,
                       &n[0], &n[1], &n[2]) == 5) {
                if (bone < 0 || bone >= psource->numbones) {
                    MdlWarning("bogus bone index\n");
                    MdlWarning("{} {} :\n{}", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szFilename,
                               g_StudioMdlContext.szLine);
                    MdlError("Exiting due to errors\n");
                }

                VectorCopy(n, g_StudioMdlContext.normal[j]);
            }
            else {
                MdlError("{}: error on line {}: {}", g_StudioMdlContext.szFilename,
                         g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            }
        }
    }
}


void Grab_Faceattriblist(s_source_t *psource) {
    while (1) {
        if (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
            nullptr) {
            int j;
            int smooth;
            int material;
            s_tmpface_t f;
            unsigned short s;

            g_StudioMdlContext.iLinecount++;

            // check for end
            if (IsEnd(g_StudioMdlContext.szLine))
                return;

            if (sscanf(g_StudioMdlContext.szLine, "%d %d %d %d %d %d %d %d %d",
                       &j,
                       &material,
                       &smooth,
                       &f.ta[0], &f.tb[0], &f.tc[0],
                       &f.na, &f.nb, &f.nc) == 9) {
                f.a = g_StudioMdlContext.faces[j].a;
                f.b = g_StudioMdlContext.faces[j].b;
                f.c = g_StudioMdlContext.faces[j].c;

                f.material = UseTextureAsMaterial(psource->texmap[material]);
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

                g_StudioMdlContext.faces[j] = f;
            }
            else {
                MdlError("{}: error on line {}: {}", g_StudioMdlContext.szFilename,
                         g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            }
        }
    }
}


int AddToVlist(int v, int m, int n, std::array<uint32_t, MAXSTUDIOTEXCOORDS> &t, int firstref) {
    auto &entries = g_studiomdl.v_listdata;
    auto &heads = g_studiomdl.v_list;
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


void UnifyIndices(s_source_t *psource) {
    vstd::FunctionTimingRecorder _("DMX");
    int i;

    s_face_t uface;

    // reset the unified vertices; the vectors keep their capacity for the next source
    g_studiomdl.v_listdata.clear();
    g_studiomdl.v_list.assign(g_StudioMdlContext.numverts, -1);

    // create an list of all the
    g_StudioMdlContext.src_uface.resize(g_StudioMdlContext.faces.size());
    for (i = 0; i < g_StudioMdlContext.faces.size(); i++) {
        uface.a = AddToVlist(g_StudioMdlContext.faces[i].a, g_StudioMdlContext.faces[i].material,
                             g_StudioMdlContext.faces[i].na, g_StudioMdlContext.faces[i].ta, g_StudioMdlContext.numverts);
        uface.b = AddToVlist(g_StudioMdlContext.faces[i].b, g_StudioMdlContext.faces[i].material,
                             g_StudioMdlContext.faces[i].nb, g_StudioMdlContext.faces[i].tb, g_StudioMdlContext.numverts);
        uface.c = AddToVlist(g_StudioMdlContext.faces[i].c, g_StudioMdlContext.faces[i].material,
                             g_StudioMdlContext.faces[i].nc, g_StudioMdlContext.faces[i].tc, g_StudioMdlContext.numverts);
        uface.d = 0xFFFFFFFF;

        if (g_StudioMdlContext.faces[i].d != 0xFFFFFFFF) {
            uface.d = AddToVlist(g_StudioMdlContext.faces[i].d, g_StudioMdlContext.faces[i].material,
                                 g_StudioMdlContext.faces[i].nd, g_StudioMdlContext.faces[i].td,
                                 g_StudioMdlContext.numverts);
        }

        // keep an original copy
        g_StudioMdlContext.src_uface[i] = uface;
    }
}

void CalcModelTangentSpaces(s_source_t *pSrc);


//-----------------------------------------------------------------------------
// Builds a list of unique vertices in a source
//-----------------------------------------------------------------------------
static void BuildUniqueVertexList(s_source_t *pSource, const int *pDesiredToVList) {
    // allocate memory
    pSource->vertex = (s_vertexinfo_t *) calloc(pSource->numvertices, sizeof(s_vertexinfo_t));

    int numValidTexcoords = 1;

    for (int i = 1; i < MAXSTUDIOTEXCOORDS; ++i) {
        if (g_StudioMdlContext.numtexcoords[i]) {
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
        VectorCopy(g_StudioMdlContext.vertex[g_studiomdl.v_listdata[j].v], vertex.position);
        VectorCopy(g_StudioMdlContext.normal[g_studiomdl.v_listdata[j].n], vertex.normal);

        vertex.boneweight.numbones = g_StudioMdlContext.bone[g_studiomdl.v_listdata[j].v].numbones;
        int k;
        for (k = 0; k < MAXSTUDIOBONEWEIGHTS; k++) {
            vertex.boneweight.bone[k] = g_StudioMdlContext.bone[g_studiomdl.v_listdata[j].v].bone[k];
            vertex.boneweight.weight[k] = g_StudioMdlContext.bone[g_studiomdl.v_listdata[j].v].weight[k];
        }

        vertex.texcoord.resize(numValidTexcoords);

        for (k = 0; k < numValidTexcoords; ++k) {
            Vector2Copy(g_StudioMdlContext.texcoord[k][g_studiomdl.v_listdata[j].t[k]], vertex.texcoord[k]);
        }

        // store a bunch of other info
        vertex.material = g_studiomdl.v_listdata[j].m;

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
static int vlistCompare(const void *elem1, const void *elem2) {
    const int i1 = *(const int *) elem1;
    const int i2 = *(const int *) elem2;
    const v_unify_t *u1 = &g_studiomdl.v_listdata[i1];
    const v_unify_t *u2 = &g_studiomdl.v_listdata[i2];

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

static void SortVerticesByMaterial(int *pDesiredToVList, int *pVListToDesired) {
    const int count = static_cast<int>(g_studiomdl.v_listdata.size());
    for (int i = 0; i < count; i++) {
        pDesiredToVList[i] = i;
    }
    qsort(pDesiredToVList, count, sizeof(int), vlistCompare);
    for (int i = 0; i < count; i++) {
        pVListToDesired[pDesiredToVList[i]] = i;
    }
}


//-----------------------------------------------------------------------------
// sort new faces by materials, last used
//-----------------------------------------------------------------------------
static int faceCompare(const void *elem1, const void *elem2) {
    int i1 = *(int *) elem1;
    int i2 = *(int *) elem2;

    // sort by material
    if (g_StudioMdlContext.faces[i1].material < g_StudioMdlContext.faces[i2].material)
        return -1;
    if (g_StudioMdlContext.faces[i1].material > g_StudioMdlContext.faces[i2].material)
        return 1;

    // sort by original usage
    if (i1 < i2)
        return -1;
    if (i1 > i2)
        return 1;

    return 0;
}

static void SortFacesByMaterial(int *pDesiredToSrcFace) {
    // NOTE: Unlike SortVerticesByMaterial, srcFaceToDesired isn't needed, so we're not computing it
    for (int i = 0; i < g_StudioMdlContext.faces.size(); i++) {
        pDesiredToSrcFace[i] = i;
    }
    qsort(pDesiredToSrcFace, g_StudioMdlContext.faces.size(), sizeof(int), faceCompare);
}


//-----------------------------------------------------------------------------
// Builds mesh structures in the source
//-----------------------------------------------------------------------------
static void PointMeshesToVertexAndFaceData(s_source_t *pSource, int *pDesiredToSrcFace) {
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
        int m = g_StudioMdlContext.faces[pDesiredToSrcFace[i]].material;

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
static void BuildFaceList(s_source_t *pSource, int *pVListToDesired, int *pDesiredToSrcFace) {
    pSource->face = (s_face_t *) calloc(pSource->numfaces, sizeof(s_face_t));
    for (const s_mesh_t &mesh: pSource->meshes) {
        for (int i = mesh.faceoffset; i < mesh.numfaces + mesh.faceoffset; i++) {
            int j = pDesiredToSrcFace[i];

            // NOTE: per-face vertex indices a,b,c,d are mesh relative (hence the subtraction), while g_StudioMdlContext.src_uface are model relative
            pSource->face[i].a = pVListToDesired[g_StudioMdlContext.src_uface[j].a] - mesh.vertexoffset;
            pSource->face[i].b = pVListToDesired[g_StudioMdlContext.src_uface[j].b] - mesh.vertexoffset;
            pSource->face[i].c = pVListToDesired[g_StudioMdlContext.src_uface[j].c] - mesh.vertexoffset;

            if (g_StudioMdlContext.src_uface[j].d != 0xFFFFFFFF) {
                pSource->face[i].d = pVListToDesired[g_StudioMdlContext.src_uface[j].d] - mesh.vertexoffset;
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
    CUtlVectorAuto<int> temp;
    int nAnimationCount = pSource->m_Animations.Count();
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
void BuildIndividualMeshes(s_source_t *pSource) {
    vstd::FunctionTimingRecorder _("DMX");
    const size_t numvlist = g_studiomdl.v_listdata.size();
    int *v_listsort = (int *) malloc(numvlist * sizeof(int)); // map desired order to vlist entry
    int *v_ilistsort = (int *) malloc(numvlist * sizeof(int)); // map vlist entry to desired order
    int *facesort = (int *) malloc(g_StudioMdlContext.faces.size() * sizeof(int)); // map desired order to src_face entry

    SortVerticesByMaterial(v_listsort, v_ilistsort);
    SortFacesByMaterial(facesort);

    pSource->numvertices = static_cast<int>(numvlist);
    pSource->numfaces = g_StudioMdlContext.faces.size();

    BuildUniqueVertexList(pSource, v_listsort);
    PointMeshesToVertexAndFaceData(pSource, facesort);
    BuildFaceList(pSource, v_ilistsort, facesort);
    RemapVertexAnimations(pSource, v_ilistsort);
    CalcModelTangentSpaces(pSource);

    free(facesort);
    free(v_ilistsort);
    free(v_listsort);
}


void Grab_MRMFaceupdates(s_source_t *psource) {
    while (1) {
        if (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
            nullptr) {
            g_StudioMdlContext.iLinecount++;

            // check for end
            if (IsEnd(g_StudioMdlContext.szLine))
                return;
        }
    }
}

int Load_VRM(s_source_t *psource) {
    char cmd[1024];
    int option;

    if (!OpenGlobalFile(psource->filename)) {
        return 0;
    }

    if (!g_StudioMdlContext.quiet) {
        std::print("grabbing {}\n", psource->filename);
    }

    g_StudioMdlContext.iLinecount = 0;

    while (g_StudioMdlContext.inputFile->Gets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine)) !=
           nullptr) {
        g_StudioMdlContext.iLinecount++;
        sscanf(g_StudioMdlContext.szLine, "%1023s %d", cmd, &option);
        if (stricmp(cmd, "version") == 0) {
            if (option != 2) {
                MdlError("bad version\n");
            }
        }
        else if (stricmp(cmd, "name") == 0) {
        }
        else if (stricmp(cmd, "vertices") == 0) {
            g_StudioMdlContext.numverts = option;
        }
        else if (stricmp(cmd, "faces") == 0) {
            g_StudioMdlContext.faces.resize(option);
        }
        else if (stricmp(cmd, "materials") == 0) {
            // doesn't matter;
        }
        else if (stricmp(cmd, "texcoords") == 0) {
            g_StudioMdlContext.numtexcoords[0] = option;
            if (option == 0)
                MdlError("model has no texture coordinates\n");
        }
        else if (stricmp(cmd, "normals") == 0) {
            g_StudioMdlContext.numnormals = option;
        }
        else if (stricmp(cmd, "tristrips") == 0) {
            // should be 0;
        }

        else if (stricmp(cmd, "vertexlist") == 0) {
            Grab_Vertexlist(psource);
        }
        else if (stricmp(cmd, "facelist") == 0) {
            Grab_Facelist(psource);
        }
        else if (stricmp(cmd, "materiallist") == 0) {
            Grab_Materiallist(psource);
        }
        else if (stricmp(cmd, "texcoordlist") == 0) {
            Grab_Texcoordlist(psource);
        }
        else if (stricmp(cmd, "normallist") == 0) {
            Grab_Normallist(psource);
        }
        else if (stricmp(cmd, "faceattriblist") == 0) {
            Grab_Faceattriblist(psource);
        }

        else if (stricmp(cmd, "MRM") == 0) {
        }
        else if (stricmp(cmd, "MRMvertices") == 0) {
        }
        else if (stricmp(cmd, "MRMfaces") == 0) {
        }
        else if (stricmp(cmd, "MRMfaceupdates") == 0) {
            Grab_MRMFaceupdates(psource);
        }

        else if (stricmp(cmd, "nodes") == 0) {
            psource->numbones = Grab_Nodes(psource->localBone);
        }
        else if (stricmp(cmd, "skeleton") == 0) {
            Grab_Animation(psource, "BindPose");
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

    UnifyIndices(psource);
    BuildIndividualMeshes(psource);

    g_StudioMdlContext.inputFile.reset();

    return 1;
}
