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
#include "tier1_v2/string_utils.hpp"

static bool SameSourceWeights(const s_sourceboneweight_t &a, const s_sourceboneweight_t &b) {
    return a.numbones == b.numbones && std::equal(a.bone.begin(), a.bone.begin() + a.numbones, b.bone.begin()) &&
           std::equal(a.weight.begin(), a.weight.begin() + a.numbones, b.weight.begin());
}

// Unifies on the reduced influences (bones/weights) and, with -fitweights (which uses them), on the full list too.
// Without it, a vertex keeps the full list of the first corner that made it.
static int lookup_index(StudioMdlState &ctx, s_source_t *psource, int material, Vector &vertex, Vector &normal,
                        Vector2D texcoord, int iCount, const std::span<int> &bones, const std::span<float> &weights,
                        const s_sourceboneweight_t &sourceWeights, int iExtras, const float *extras,
                        SourceCornerIndex *indexed, uint32_t sourceVertex) {
    vstd::FunctionTimingRecorder _("SMD lookup");
    int i, j, k;
    auto &entries = ctx.input.v_listdata;
    const int count = static_cast<int>(entries.size());

    for (i = indexed ? indexed->first[sourceVertex] : 0;
         indexed ? i >= 0 : i < count;
         i = indexed ? indexed->next[i] : i + 1) {
        if (entries[i].m == material
            && DotProduct(ctx.input.normal[i], normal) > ctx.options.normal_blend
            && (indexed || VectorCompare(ctx.input.vertex[i], vertex))
            && ctx.input.texcoord[0][i].x == texcoord[0]
            && ctx.input.texcoord[0][i].y == texcoord[1]) {
            if (ctx.input.bone[i].numbones == iCount) {
                for (j = 0; j < iCount; j++) {
                    if (ctx.input.bone[i].bone[j] != bones[j] || ctx.input.bone[i].weight[j] !=
                        weights[j])
                        break;
                }
                if (j == iCount && (!ctx.options.fitWeights || SameSourceWeights(ctx.input.sourceBone[i], sourceWeights))) {
                    // Assume extra floats are additional texcoords
                    for (k = 0; k < (iExtras / 2); k++) {
                        if (entries[i].t[k + 1] == -1) // Texcoord not set
                            break;
                        if (ctx.input.texcoord[k + 1][i][0] != extras[k * 2])
                            break;
                        if (ctx.input.texcoord[k + 1][i][1] != extras[k * 2 + 1])
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
    i = count; // the indexed chain ends at -1; new entries always follow the existing source vertices
    if (i >= MAXSTUDIOSRCVERTS) {
        MdlError("too many indices in source: \"{}\"\n", psource->filename);
    }

    VectorCopy(vertex, ctx.input.vertex[i]);
    VectorCopy(normal, ctx.input.normal[i]);
    Vector2Copy(texcoord, ctx.input.texcoord[0][i]);

    s_boneweight_t &boneWeight = ctx.input.bone[i];
    boneWeight.numbones = iCount;
    for (j = 0; j < iCount; j++) {
        boneWeight.bone[j] = bones[j];
        boneWeight.weight[j] = weights[j];
    }
    ctx.input.sourceBone[i] = sourceWeights;

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
        Vector2D &extra = ctx.input.texcoord[j + 1][i];
        extra[0] = extras[j * 2];
        extra[1] = extras[j * 2 + 1];
        entry.t[j + 1] = i;
    }


    entry.lastref = count;
    if (indexed) {
        indexed->next.push_back(-1);
        int &last = indexed->last[sourceVertex];
        if (last < 0)
            indexed->first[sourceVertex] = i;
        else
            indexed->next[last] = i;
        last = i;
    }

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

int AddSourceCorner(StudioMdlState &ctx, s_source_t *psource, const int material, SourceCorner &corner,
                    SourceCornerIndex *indexed, uint32_t sourceVertex) {
    // Scale face pos
    scale_vertex(ctx, corner.position);

    // invert v
    corner.uv[1] = 1.0 - corner.uv[1];

    int iCount = corner.numLinks;
    s_sourceboneweight_t sourceWeights;
    if (iCount == 0) {
        iCount = 1;
        corner.bones[0] = corner.bone;
        corner.weights[0] = 1.0;
    }
    else {
        MakeSourceBoneWeights(iCount, corner.bones, corner.weights, sourceWeights);
        iCount = SortAndBalanceBones(iCount, MAXSTUDIOBONEWEIGHTS, corner.bones, corner.weights);
    }

    return lookup_index(ctx, psource, material, corner.position, corner.normal, corner.uv, iCount, corner.bones,
                        corner.weights, sourceWeights, corner.numExtras, corner.extras.data(), indexed, sourceVertex);
}

bool AddSourceTriangle(StudioMdlState &ctx, const int material, const std::array<int, 3> &corners) {
    s_face_t f;
    // pFace->material = material; // BUG
    f.a = corners[0];
    f.b = corners[2];
    f.c = corners[1];
    Assert(((f.a & 0xF0000000) == 0) && ((f.b & 0xF0000000) == 0) && ((f.c & 0xF0000000) == 0));

    // remove degenerate triangles
    if (f.a == f.b || f.b == f.c || f.a == f.c) {
        // printf("Degenerate triangle %d %d %d\n", f.a, f.b, f.c );
        return false;
    }

    ctx.input.src_uface.emplace_back(f);
    auto &new_face = ctx.input.faces.emplace_back();
    new_face.material = material;
    return true;
}

void AddSourceQuad(StudioMdlState &ctx, const int material, const std::array<int, 4> &corners) {
    s_face_t f;
    f.a = corners[0];
    f.b = corners[1];
    f.c = corners[2];
    f.d = corners[3];

    ctx.input.src_uface.emplace_back(f);
    auto &new_face = ctx.input.faces.emplace_back();
    new_face.material = material;
}

static std::array<int, 3> ParseFaceData(StudioMdlState &ctx, s_source_t *psource, int material) {
    vstd::FunctionTimingRecorder _("SMD");
    std::array<int, 3> index{};
    int i, j;
    SourceCorner corner;

    for (j = 0; j < 3; j++) {
        memset(ctx.input.szLine, 0, sizeof(ctx.input.szLine));

        if (!GetLineInput(ctx)) {
            MdlError("{}: error on g_studiomdl.input.szLine {}: {}", ctx.input.szFilename,
                     ctx.input.iLinecount, ctx.input.szLine);
        }

        corner.numLinks = 0;
        corner.numExtras = 0;

        i = sscanf(ctx.input.szLine, "%d %f %f %f %f %f %f %f %f",
                   &corner.bone,
                   &corner.position[0], &corner.position[1], &corner.position[2],
                   &corner.normal[0], &corner.normal[1], &corner.normal[2],
                   &corner.uv[0], &corner.uv[1]);

        if (i < 9)
            continue;

        if (corner.bone < 0 || corner.bone >= psource->numbones) {
            MdlError("bogus bone index\n{} {} :\n{}", ctx.input.iLinecount, ctx.input.szFilename,
                     ctx.input.szLine);
        }

        // Parse bones.
        int k;
        char *pItem = ctx.input.szLine;
        // Skip first 9 items already parsed via sscanf above
        for (k = 0; k < 9; k++) {
            pItem = GetNextFaceItem(pItem);
        }
        // Read bone count
        if (pItem) {
            const int iCount = atoi(pItem);
            corner.numLinks = iCount;
            if (iCount > 0) {
                for (k = 0; k < iCount && k < MAXSTUDIOSRCBONES; k++) {
                    pItem = GetNextFaceItem(pItem);
                    if (!pItem) {
                        MdlError("Bone ID {} not found\n{} {} :\n{}", k, ctx.input.iLinecount,
                                 ctx.input.szFilename, ctx.input.szLine);
                    }
                    corner.bones[k] = atoi(pItem);

                    pItem = GetNextFaceItem(pItem);
                    if (!pItem) {
                        MdlError("Bone weight {} not found\n{} {} :\n{}", k, ctx.input.iLinecount,
                                 ctx.input.szFilename, ctx.input.szLine);
                    }
                    corner.weights[k] = atof(pItem);
                }
            }
            if (psource->version >= 3) {
                pItem = GetNextFaceItem(pItem);
                if (pItem) {
                    int iExtras = atoi(pItem);
                    if (iExtras > 0) {
                        iExtras = MIN(iExtras, (MAXSTUDIOTEXCOORDS - 1) * 2);
                        corner.numExtras = iExtras;
                        for (int e = 0; e < iExtras; e++) {
                            pItem = GetNextFaceItem(pItem);
                            if (!pItem) {
                                MdlError("Extra data item {} not found\n{} {} :\n{}", e, ctx.input.iLinecount,
                                         ctx.input.szFilename,
                                         ctx.input.szLine);
                            }
                            corner.extras[e] = atof(pItem);
                        }
                    }
                }
            }
            // printf("%d ", iCount );

            //printf("\n");
            //exit(1);
        }

        // adjust_vertex( p );

        // move vertex position to object space.
        // VectorSubtract( p, psource->bonefixup[bone].worldorg, tmp );
        // VectorTransform(tmp, psource->bonefixup[bone].im, p );

        // move normal to object space.
        // VectorCopy( normal, tmp );
        // VectorTransform(tmp, psource->bonefixup[bone].im, normal );
        // VectorNormalize( normal );

        index[j] = AddSourceCorner(ctx, psource, material, corner);
    }

    return index;
}

void BeginSourceTriangles(StudioMdlState &ctx) {
    ctx.input.faces.clear();
    ctx.input.src_uface.clear();
    ctx.input.v_listdata.clear();
    ctx.input.v_list.clear();

    // the additional texcoord sets of an earlier source must not leak into this one
    for (size_t layer = 1; layer < ctx.input.texcoord.size(); layer++) {
        ctx.input.texcoord[layer].clear();
    }
}

int ResolveSourceMaterial(StudioMdlState &ctx, s_source_t *psource, const std::string_view textureName) {
    int i;
    char texturename[MAX_PATH]{0};
    Q_strncpy(texturename, std::string(textureName).c_str(), sizeof(texturename));

    // funky texture overrides
    for (i = 0; i < std::ssize(ctx.materials.defaulttextures); i++) {
        if (ctx.materials.sourcetextures[i][0] == '\0') {
            strcpy(texturename, ctx.materials.defaulttextures[i]);
            break;
        }
        if (vstd::iequals(texturename, ctx.materials.sourcetextures[i])) {
            strcpy(texturename, ctx.materials.defaulttextures[i]);
            break;
        }
    }

    if (texturename[0] == '\0') {
        // weird source problem, skip them
        return -1;
    }

    if (stricmp(texturename, "null.bmp") == 0 || stricmp(texturename, "null.tga") == 0 ||
        stricmp(texturename, "debug/debugempty") == 0) {
        // skip all faces with the null texture on them.
        return -1;
    }

    const int texture = static_cast<int>(LookupTexture(ctx, texturename, (psource->version == 2)));
    psource->texmap[texture] = texture; // hack, make it 1:1
    return static_cast<int>(UseTextureAsMaterial(ctx, texture));
}

void EndSourceTriangles(StudioMdlState &ctx, s_source_t *psource) {
    // every layer that has data covers all unified vertices
    for (auto &layer: ctx.input.texcoord) {
        if (!layer.empty()) {
            layer.resize(ctx.input.v_listdata.size());
        }
    }

    BuildIndividualMeshes(ctx, psource);
}

void Grab_Triangles(StudioMdlState &ctx, s_source_t *psource) {
    vstd::FunctionTimingRecorder _("SMD");
    int i;

    BeginSourceTriangles(ctx);

    //
    // load the base triangles
    //
    char texturename[MAX_PATH]{0};

    while (1) {
        if (!GetLineInput(ctx))
            break;

        // check for end
        if (IsEnd(ctx.input.szLine))
            break;

        // Look for extra junk that we may want to avoid...
        int nLineLength = strlen(ctx.input.szLine);
        if (nLineLength >= sizeof(texturename)) {
            MdlWarning("Unexpected data at line {}, (need a texture name) ignoring...\n",
                       ctx.input.iLinecount);
            continue;
        }

        // strip off trailing smag
        strncpy(texturename, ctx.input.szLine, sizeof(texturename) - 1);
        for (i = strlen(texturename) - 1; i >= 0 && !isgraph(texturename[i]); i--) {
        }
        texturename[i + 1] = '\0';

        const int material = ResolveSourceMaterial(ctx, psource, texturename);
        if (material < 0) {
            // skip all faces with this texture
            GetLineInput(ctx);
            GetLineInput(ctx);
            GetLineInput(ctx);
            continue;
        }

        AddSourceTriangle(ctx, material, ParseFaceData(ctx, psource, material));
    }

    EndSourceTriangles(ctx, psource);
}


int Load_SMD(StudioMdlState &ctx, s_source_t *psource) {
    vstd::FunctionTimingRecorder _("Parse");
    char cmd[1024];
    int option;

    // Reset smdVersion
    psource->version = 1;

    if (!OpenGlobalFile(ctx, psource->filename))
        return 0;

    if (!ctx.options.quiet) {
        MdlPrint("SMD MODEL {}\n", psource->filename);
    }

    ctx.input.iLinecount = 0;

    while (GetLineInput(ctx)) {
        int numRead = sscanf(ctx.input.szLine, "%s %d", cmd, &option);

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
            psource->numbones = Grab_Nodes(ctx, psource->localBone);
        }
        else if (stricmp(cmd, "skeleton") == 0) {
            Grab_Animation(ctx, psource, "BindPose");
        }
        else if (stricmp(cmd, "triangles") == 0) {
            Grab_Triangles(ctx, psource);
        }
        else if (stricmp(cmd, "vertexanimation") == 0) {
            Grab_Vertexanimation(ctx, psource, "BindPose");
        }
        else if ((strncmp(cmd, "//", 2) == 0) || (strncmp(cmd, ";", 1) == 0) || (strncmp(cmd, "#", 1) == 0)) {
            ProcessSourceComment(psource, cmd);
            continue;
        }
        else {
            MdlWarning("unknown studio command \"{}\"\n", cmd);
        }
    }
    ctx.input.inputFile.reset();

    return 1;
}
