//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

//
// studiomdl.c: generates a studio .mdl file from a .qc script
// sources/<scriptname>.mdl.
//


#pragma warning( disable : 4244 )
#pragma warning( disable : 4237 )
#pragma warning( disable : 4305 )


#include <cstdio>
#include <cstdlib>

#include "common/scriplib.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"

int lookup_index(s_source_t *psource, int material, Vector &vertex, Vector &normal, Vector2D texcoord, int iCount,
                 const std::span<int> &bones, const std::span<float> &weights,
                 int iExtras, const float *extras) {
    int i, j, k;
    auto &entries = g_studiomdl.v_listdata;
    const int count = static_cast<int>(entries.size());

    for (i = 0; i < count; i++) {
        if (entries[i].m == material
            && DotProduct(g_StudioMdlContext.normal[i], normal) > g_studiomdl.normal_blend
            && VectorCompare(g_StudioMdlContext.vertex[i], vertex)
            && g_StudioMdlContext.texcoord[0][i].x == texcoord[0]
            && g_StudioMdlContext.texcoord[0][i].y == texcoord[1]) {
            if (g_StudioMdlContext.bone[i].numbones == iCount) {
                for (j = 0; j < iCount; j++) {
                    if (g_StudioMdlContext.bone[i].bone[j] != bones[j] || g_StudioMdlContext.bone[i].weight[j] !=
                        weights[j])
                        break;
                }
                if (j == iCount) {
                    // Assume extra floats are additional texcoords
                    for (k = 0; k < (iExtras / 2); k++) {
                        if (entries[i].t[k + 1] == -1) // Texcoord not set
                            break;
                        if (g_StudioMdlContext.texcoord[k + 1][i][0] != extras[k * 2])
                            break;
                        if (g_StudioMdlContext.texcoord[k + 1][i][1] != extras[k * 2 + 1])
                            break;
                    }

                    if (k == (iExtras / 2)) {
                        entries[i].lastref = count;
                        return i;
                    }
                }
            }
        }
    }
    if (i >= MAXSTUDIOSRCVERTS) {
        MdlError("too many indices in source: \"{}\"\n", psource->filename);
    }

    VectorCopy(vertex, g_StudioMdlContext.vertex[i]);
    VectorCopy(normal, g_StudioMdlContext.normal[i]);
    Vector2Copy(texcoord, g_StudioMdlContext.texcoord[0][i]);

    g_StudioMdlContext.bone[i].numbones = iCount;
    for (j = 0; j < iCount; j++) {
        g_StudioMdlContext.bone[i].bone[j] = bones[j];
        g_StudioMdlContext.bone[i].weight[j] = weights[j];
    }

    v_unify_t &entry = entries.emplace_back();
    entry.v = i;
    entry.m = material;
    entry.n = i;
    entry.t[0] = i;


    // Set default indices for additional texcoords to -1
    for (j = 1; j < (MAXSTUDIOTEXCOORDS); ++j) {
        entry.t[j] = -1;
    }
    // Populate additional texcoords with any extra floats
    for (j = 0; j < (iExtras / 2); j++) {
        g_StudioMdlContext.texcoord[j + 1][i][0] = extras[j * 2];
        g_StudioMdlContext.texcoord[j + 1][i][1] = extras[j * 2 + 1];
        entry.t[j + 1] = i;
    }


    entry.lastref = count;

    return i;
}

// GetNextFaceItem
// Get next item from string of space separated data
static char *GetNextFaceItem(char *pCurrentItem) {
    if (!pCurrentItem) {
        return nullptr;
    }

    char *pChar = pCurrentItem;

    //Skip any leading spaces
    while (*pChar == ' ') {
        pChar++;
    }

    pChar = strchr(pChar, ' ');

    if (!pChar) {
        return nullptr;
    }

    while (*pChar == ' ') {
        pChar++;
    }

    if ((*pChar == 0) || (*pChar == '\n')) {
        return nullptr;
    }
    return pChar;
}

void ParseFaceData(s_source_t *psource, int material, s_face_t *pFace) {
    vstd::FunctionTimingRecorder _("SMD");
    int index[3];
    int i, j;
    Vector p;
    Vector normal;
    Vector2D t;
    int iCount;
    std::array<int,MAXSTUDIOSRCBONES> bones{};
    std::array<float,MAXSTUDIOSRCBONES> weights{};
    int iExtras;
    float extras[(MAXSTUDIOTEXCOORDS - 1) * 2];
    int bone;

    for (j = 0; j < 3; j++) {
        memset(g_StudioMdlContext.szLine, 0, sizeof(g_StudioMdlContext.szLine));

        if (!GetLineInput()) {
            MdlError("{}: error on g_StudioMdlContext.szLine {}: {}", g_StudioMdlContext.szFilename,
                     g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
        }

        iCount = 0;
        iExtras = 0;

        i = sscanf(g_StudioMdlContext.szLine, "%d %f %f %f %f %f %f %f %f",
                   &bone,
                   &p[0], &p[1], &p[2],
                   &normal[0], &normal[1], &normal[2],
                   &t[0], &t[1]);

        if (i < 9)
            continue;

        if (bone < 0 || bone >= psource->numbones) {
            MdlError("bogus bone index\n{} {} :\n{}", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szFilename,
                     g_StudioMdlContext.szLine);
        }

        //Scale face pos
        scale_vertex(p);

        // Parse bones.
        int k;
        char *pItem = g_StudioMdlContext.szLine;
        // Skip first 9 items already parsed via sscanf above
        for (k = 0; k < 9; k++) {
            pItem = GetNextFaceItem(pItem);
        }
        // Read bone count
        if (pItem) {
            iCount = atoi(pItem);
            if (iCount > 0) {
                for (k = 0; k < iCount && k < MAXSTUDIOSRCBONES; k++) {
                    pItem = GetNextFaceItem(pItem);
                    if (!pItem) {
                        MdlError("Bone ID {} not found\n{} {} :\n{}", k, g_StudioMdlContext.iLinecount,
                                 g_StudioMdlContext.szFilename, g_StudioMdlContext.szLine);
                    }
                    bones[k] = atoi(pItem);

                    pItem = GetNextFaceItem(pItem);
                    if (!pItem) {
                        MdlError("Bone weight {} not found\n{} {} :\n{}", k, g_StudioMdlContext.iLinecount,
                                 g_StudioMdlContext.szFilename, g_StudioMdlContext.szLine);
                    }
                    weights[k] = atof(pItem);
                }
            }
            if (psource->version >= 3) {
                pItem = GetNextFaceItem(pItem);
                if (pItem) {
                    iExtras = atoi(pItem);
                    if (iExtras > 0) {
                        iExtras = MIN(iExtras, (MAXSTUDIOTEXCOORDS - 1) * 2);
                        for (int e = 0; e < iExtras; e++) {
                            pItem = GetNextFaceItem(pItem);
                            if (!pItem) {
                                MdlError("Extra data item {} not found\n{} {} :\n{}", e, g_StudioMdlContext.iLinecount,
                                         g_StudioMdlContext.szFilename,
                                         g_StudioMdlContext.szLine);
                            }
                            extras[e] = atof(pItem);
                        }
                    }
                }
            }
            // printf("%d ", iCount );

            //printf("\n");
            //exit(1);
        }

        // adjust_vertex( p );
        // scale_vertex( p );

        // move vertex position to object space.
        // VectorSubtract( p, psource->bonefixup[bone].worldorg, tmp );
        // VectorTransform(tmp, psource->bonefixup[bone].im, p );

        // move normal to object space.
        // VectorCopy( normal, tmp );
        // VectorTransform(tmp, psource->bonefixup[bone].im, normal );
        // VectorNormalize( normal );

        // invert v
        t[1] = 1.0 - t[1];

        if (iCount == 0) {
            iCount = 1;
            bones[0] = bone;
            weights[0] = 1.0;
        }
        else {
            iCount = SortAndBalanceBones(iCount, MAXSTUDIOBONEWEIGHTS, bones, weights);
        }


        index[j] = lookup_index(psource, material, p, normal, t, iCount, bones, weights, iExtras, extras);
    }

    // pFace->material = material; // BUG
    pFace->a = index[0];
    pFace->b = index[2];
    pFace->c = index[1];
    Assert(((pFace->a & 0xF0000000) == 0) && ((pFace->b & 0xF0000000) == 0) &&
        ((pFace->c & 0xF0000000) == 0));
}

void Grab_Triangles(s_source_t *psource) {
    vstd::FunctionTimingRecorder _("SMD");
    int i;
    Vector vmin, vmax;

    vmin[0] = vmin[1] = vmin[2] = 99999;
    vmax[0] = vmax[1] = vmax[2] = -99999;

    g_StudioMdlContext.faces.clear();
    g_StudioMdlContext.src_uface.clear();
    g_studiomdl.v_listdata.clear();
    g_studiomdl.v_list.clear();

    //
    // load the base triangles
    //
    int texture;
    int material;
    char texturename[MAX_PATH]{0};

    while (1) {
        if (!GetLineInput())
            break;

        // check for end
        if (IsEnd(g_StudioMdlContext.szLine))
            break;

        // Look for extra junk that we may want to avoid...
        int nLineLength = strlen(g_StudioMdlContext.szLine);
        if (nLineLength >= sizeof(texturename)) {
            MdlWarning("Unexpected data at line {}, (need a texture name) ignoring...\n",
                       g_StudioMdlContext.iLinecount);
            continue;
        }

        // strip off trailing smag
        strncpy(texturename, g_StudioMdlContext.szLine, sizeof(texturename) - 1);
        for (i = strlen(texturename) - 1; i >= 0 && !isgraph(texturename[i]); i--) {
        }
        texturename[i + 1] = '\0';

        // funky texture overrides
        for (i = 0; i < g_studiomdl.numrep; i++) {
            if (g_studiomdl.sourcetextures[i][0] == '\0') {
                strcpy(texturename, g_studiomdl.defaulttextures[i]);
                break;
            }
            if (stricmp(texturename, g_studiomdl.sourcetextures[i]) == 0) {
                strcpy(texturename, g_studiomdl.defaulttextures[i]);
                break;
            }
        }

        if (texturename[0] == '\0') {
            // weird source problem, skip them
            GetLineInput();
            GetLineInput();
            GetLineInput();
            continue;
        }

        if (stricmp(texturename, "null.bmp") == 0 || stricmp(texturename, "null.tga") == 0 ||
            stricmp(texturename, "debug/debugempty") == 0) {
            // skip all faces with the null texture on them.
            GetLineInput();
            GetLineInput();
            GetLineInput();
            continue;
        }

        texture = LookupTexture(texturename, (psource->version == 2));
        psource->texmap[texture] = texture; // hack, make it 1:1
        material = UseTextureAsMaterial(texture);

        s_face_t f;
        ParseFaceData(psource, material, &f);

        // remove degenerate triangles
        if (f.a == f.b || f.b == f.c || f.a == f.c) {
            // printf("Degenerate triangle %d %d %d\n", f.a, f.b, f.c );
            continue;
        }

        g_StudioMdlContext.src_uface.emplace_back(f);
        auto &new_face = g_StudioMdlContext.faces.emplace_back();
        new_face.material = material;
    }

    for (int i = 0; i < MAXSTUDIOTEXCOORDS; ++i) {
        if (g_StudioMdlContext.texcoord[i].Count()) {
            g_StudioMdlContext.numtexcoords[i] = static_cast<int>(g_studiomdl.v_listdata.size());
        }
    }

    BuildIndividualMeshes(psource);
}


int Load_SMD(s_source_t *psource) {
    vstd::FunctionTimingRecorder _("Parse");
    char cmd[1024];
    int option;

    // Reset smdVersion
    psource->version = 1;

    if (!OpenGlobalFile(psource->filename))
        return 0;

    if (!g_StudioMdlContext.quiet) {
        std::print("SMD MODEL {}\n", psource->filename);
    }

    g_StudioMdlContext.iLinecount = 0;

    while (GetLineInput()) {
        int numRead = sscanf(g_StudioMdlContext.szLine, "%s %d", cmd, &option);

        // Blank line
        if ((numRead == EOF) || (numRead == 0))
            continue;

        if (stricmp(cmd, "version") == 0) {
            if (option < 1 || option > 3) {
                MdlError("bad version\n");
            }
            psource->version = option;
        }
        else if (stricmp(cmd, "nodes") == 0) {
            psource->numbones = Grab_Nodes(psource->localBone);
        }
        else if (stricmp(cmd, "skeleton") == 0) {
            Grab_Animation(psource, "BindPose");
        }
        else if (stricmp(cmd, "triangles") == 0) {
            Grab_Triangles(psource);
        }
        else if (stricmp(cmd, "vertexanimation") == 0) {
            Grab_Vertexanimation(psource, "BindPose");
        }
        else if ((strncmp(cmd, "//", 2) == 0) || (strncmp(cmd, ";", 1) == 0) || (strncmp(cmd, "#", 1) == 0)) {
            ProcessSourceComment(psource, cmd);
            continue;
        }
        else {
            MdlWarning("unknown studio command \"{}\"\n", cmd);
        }
    }
    g_StudioMdlContext.inputFile.reset();

    return 1;
}
