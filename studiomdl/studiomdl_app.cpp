//
// Created by RED on 15.05.2024.
//

#include <windows.h>
#include <direct.h>

#include "studiomdl_app.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl_commands.h"
#include "common/cmdlib.h"
#include "studiomdl_errors.h"
#include "tier1/keyvalues.h"
#include "tier2/fileutils.h"
#include "studiomdl/optimize.h"
#include "common/scriplib.h"
#include "appframework/AppFramework.h"
#include "studiomdl/perfstats.h"
#include "datamodel/idatamodel.h"
#include "dmserializers/idmserializers.h"
#include "mdllib/mdllib.h"
#include "filesystem/filesystem_stdio.h"

extern StudioMdlContext g_StudioMdlContext;

CMdlLoggingListener s_MdlLoggingListener;
CStudioMDLApp s_ApplicationObject;

enum RunMode {
    RUN_MODE_BUILD,
    RUN_MODE_STRIP_MODEL,
    RUN_MODE_STRIP_VHV
} g_eRunMode = RUN_MODE_BUILD;

class CClampedSource;

static bool
CStudioMDLApp_SuggestGameInfoDirFn(CFSSteamSetupInfo const *, char *pchPathBuffer, int nBufferLength,
                                   bool *pbBubbleDirectories) {
    const char *pProcessFileName = NULL;
    int nParmCount = CommandLine()->ParmCount();
    if (nParmCount > 1) {
        pProcessFileName = CommandLine()->GetParm(nParmCount - 1);
    }

    if (pProcessFileName) {
        Q_MakeAbsolutePath(pchPathBuffer, nBufferLength, pProcessFileName);

        if (pbBubbleDirectories)
            *pbBubbleDirectories = true;

        return true;
    }

    return false;
}

int main(int argc, char **argv) {
    SetSuggestGameInfoDirFn(CStudioMDLApp_SuggestGameInfoDirFn);

    CSteamApplication s_SteamApplicationObject(&s_ApplicationObject);
    return AppMain(argc, argv, &s_SteamApplicationObject);
}

//-----------------------------------------------------------------------------
// Purpose: search through the "GamePath" key and create a mirrored version in the content path searches
//-----------------------------------------------------------------------------
void AddContentPaths() {
    // look for the "content" in the path to the initial QC file
    const char *match = "content\\";
    char *sp = strstr(qdir, match);
    if (!sp)
        return;

    // copy off everything before and including "content"
    char pre[1024];
    strncpy(pre, qdir, sp - qdir + strlen(match));
    pre[sp - qdir + strlen(match)] = '\0';
    sp = sp + strlen(match);

    // copy off everything following the word after "content"
    char post[1024];
    sp = strstr(sp + 1, "\\");
    strcpy(post, sp);

    // get a copy of the game search paths
    char paths[1024];
    g_pFullFileSystem->GetSearchPath("GAME", false, paths, sizeof(paths));
    if (!g_StudioMdlContext.quiet)
        printf("all paths:%s\n", paths);

    // pull out the game names and insert them into a content path string
    sp = strstr(paths, "game\\");
    while (sp) {
        char temp[1024];
        sp = sp + 5;
        char *sz = strstr(sp, "\\");
        if (!sz)
            return;

        strcpy(temp, pre);
        strncat(temp, sp, sz - sp);
        strcat(temp, post);
        sp = sz;
        sp = strstr(sp, "game\\");
        CmdLib_AddBasePath(temp);
        if (!g_StudioMdlContext.quiet)
            printf("content:%s\n", temp);
    }
}

//-----------------------------------------------------------------------------
// Purpose: parses the game info file to retrieve relevant settings
//-----------------------------------------------------------------------------
struct GameInfo_t g_gameinfo;

void ParseGameInfo() {
    bool bParsed = false;

    GameInfo_t gameinfoDefault{0};
    gameinfoDefault.bSupportsXBox360 = false;
    gameinfoDefault.bSupportsDX8 = true;

    auto *pKeyValues = new KeyValues("gameinfo.txt");
    if (g_pFileSystem && pKeyValues->LoadFromFile(g_pFileSystem, "gameinfo.txt")) {
        bParsed = true;
    }
    pKeyValues->deleteThis();

    if (!bParsed) {
        g_gameinfo = gameinfoDefault;
    }
}

/*
===============
ParseScript
===============
*/
void ParseScript(const char *pExt) {
    while (1) {
        GetToken(true);
        if (endofscript)
            return;

        // Check all the commands we know about.
        int i;
        for (i = 0; i < g_nMDLCommandCount; i++) {
            if (Q_stricmp(g_Commands[i].m_pName, token))
                continue;

            g_Commands[i].m_pCmd();
            break;
        }
        if (i == g_nMDLCommandCount) {
            if (true) {
                if (!g_StudioMdlContext.createMakefile) {
                    TokenError("bad command %s\n", token);
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// For preprocessed files, all data lies in the g_fullpath.
// The DMX loader will take care of it.
//-----------------------------------------------------------------------------
bool ParsePreprocessedFile(const char *pFullPath) {
    char pFullPathBuf[MAX_PATH];
    Q_strcpy(pFullPathBuf, pFullPath);
    Q_FixSlashes(pFullPathBuf);

    if (!LoadPreprocessedFile(pFullPathBuf, 1.0f))
        return false;

    if (!g_StudioMdlContext.bHasModelName) {
        // The output name can be set via a "mdlPath" attribute on the root
        // node of the preprocessed filename.  If it wasn't set then derive it
        // from the input filename

        // The output name is directly derived from the input name
        // NOTE: We use directory names when using preprocessed files
        // Fix up passed pathname to use correct path separators otherwise
        // functions below will fail
        char pOutputBuf[MAX_PATH], pTemp[MAX_PATH], pOutputBuf2[MAX_PATH], pRelativeBuf[MAX_PATH];
        char *pOutputName = pOutputBuf;
        ComputeModFilename(pFullPathBuf, pTemp, sizeof(pTemp));
        Q_ExtractFilePath(pTemp, pOutputBuf, sizeof(pOutputBuf));
        Q_StripTrailingSlash(pOutputBuf);
        if (!Q_stricmp(Q_UnqualifiedFileName(pOutputBuf), "preprocess") ||
            !Q_stricmp(Q_UnqualifiedFileName(pOutputBuf), ".preprocess")) {
            Q_ExtractFilePath(pOutputBuf, pOutputBuf2, sizeof(pOutputBuf2));
            Q_StripTrailingSlash(pOutputBuf2);
            pOutputName = pOutputBuf2;
        }

        int nBufLen = sizeof(pOutputBuf);
        if (Q_IsAbsolutePath(pOutputName)) {
            if (!g_pFullFileSystem->FullPathToRelativePathEx(pOutputName, "GAME", pRelativeBuf, sizeof(pRelativeBuf))) {
                MdlError("Full path %s is not associated with the current mod!\n", pOutputName);
                return false;
            }
            Q_FixSlashes(pRelativeBuf);
            if (Q_strnicmp(pRelativeBuf, "models\\", 7)) {
                MdlError("Full path %s is not under the 'models' directory\n", pOutputName);
                return false;
            }
            pOutputName = pRelativeBuf + 7;
            nBufLen -= 7;
        }
        Q_SetExtension(pOutputName, "mdl", nBufLen);
        ProcessModelName(pOutputName);
    }

    return true;
}

// Used by the CheckSurfaceProps.py script.
// They specify the .mdl file and it prints out all the surface props that the model uses.
bool HandlePrintSurfaceProps(int &returnValue) {
    const char *pFilename = CommandLine()->ParmValue("-PrintSurfaceProps", (const char *) NULL);
    if (pFilename) {
        CUtlVector<char> buf;

        FILE *fp = fopen(pFilename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            buf.SetSize(ftell(fp));
            fseek(fp, 0, SEEK_SET);
            fread(buf.Base(), 1, buf.Count(), fp);

            fclose(fp);

            studiohdr_t *pHdr = (studiohdr_t *) buf.Base();

            Studio_ConvertStudioHdrToNewVersion(pHdr);

            if (pHdr->version == STUDIO_VERSION) {
                for (int i = 0; i < pHdr->numbones; i++) {
                    const mstudiobone_t *pBone = pHdr->pBone(i);
                    printf("%s\n", pBone->pszSurfaceProp());
                }

                returnValue = 0;
            } else {
                printf("-PrintSurfaceProps: '%s' is wrong version (%d should be %d).\n",
                       pFilename, pHdr->version, STUDIO_VERSION);
                returnValue = 1;
            }
        } else {
            printf("-PrintSurfaceProps: can't open '%s'\n", pFilename);
            returnValue = 1;
        }

        return true;
    } else {
        return false;
    }
}

// Used by the modelstats.pl script.
// They specify the .mdl file and it prints out perf info.
bool HandleMdlReport(int &returnValue) {
    const char *pFilename = CommandLine()->ParmValue("-mdlreport", (const char *) NULL);
    if (pFilename) {
        CUtlVector<char> buf;

        FILE *fp = fopen(pFilename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            buf.SetSize(ftell(fp));
            fseek(fp, 0, SEEK_SET);
            fread(buf.Base(), 1, buf.Count(), fp);

            fclose(fp);

            studiohdr_t *pHdr = (studiohdr_t *) buf.Base();

            Studio_ConvertStudioHdrToNewVersion(pHdr);

            if (pHdr->version == STUDIO_VERSION) {
                int flags = SPEWPERFSTATS_SHOWPERF;
                if (CommandLine()->CheckParm("-mdlreportspreadsheet", nullptr)) {
                    flags |= SPEWPERFSTATS_SPREADSHEET;
                }
                SpewPerfStats(pHdr, pFilename, flags);

                returnValue = 0;
            } else {
                printf("-mdlreport: '%s' is wrong version (%d should be %d).\n",
                       pFilename, pHdr->version, STUDIO_VERSION);
                returnValue = 1;
            }
        } else {
            printf("-mdlreport: can't open '%s'\n", pFilename);
            returnValue = 1;
        }

        return true;
    } else {
        return false;
    }
}

void
StudioMdl_ScriptLoadedCallback(const char *pFilenameLoaded, const char *pIncludedFromFileName, int nIncludeLineNumber) {
//    printf("Script loaded callback: %s",pFilenameLoaded);
}

void CreateMakefile_OutputMakefile() {
    if (!g_StudioMdlContext.bHasModelName) {
        MdlError("Can't write makefile since a target mdl hasn't been specified!");
    }
    FILE *fp = fopen("makefile.tmp", "a");
    if (!fp) {
        MdlError("can't open makefile.tmp!\n");
    }
    char mdlname[MAX_PATH];
    strcpy(mdlname, gamedir);
//	if( *g_pPlatformName )
//	{
//		strcat( mdlname, "platform_" );
//		strcat( mdlname, g_pPlatformName );
//		strcat( mdlname, "/" );
//	}
    strcat(mdlname, "models/");
    strcat(mdlname, g_outname);
    Q_StripExtension(mdlname, mdlname, sizeof(mdlname));
    strcat(mdlname, ".mdl");
    Q_FixSlashes(mdlname);

    fprintf(fp, "%s:", mdlname);
    int i;
    for (i = 0; i < g_StudioMdlContext.CreateMakefileDependencies.size(); i++) {
        fprintf(fp, " %s", g_StudioMdlContext.CreateMakefileDependencies[i].String());
    }
    fprintf(fp, "\n");
    char mkdirpath[MAX_PATH];
    strcpy(mkdirpath, mdlname);
    Q_StripFilename(mkdirpath);
    fprintf(fp, "\tmkdir \"%s\"\n", mkdirpath);
    fprintf(fp, "\t%s -quiet %s\n\n", CommandLine()->GetParm(0), g_fullpath);
    fclose(fp);
}

void ClampMaxVerticesPerModel(s_source_t *pOrigSource) {
    // check for overage
    if (pOrigSource->numvertices < g_StudioMdlContext.maxVertexLimit)
        return;

    MdlWarning("model has too many verts, cutting into multiple models\n", pOrigSource->numvertices);

    CUtlVector<CClampedSource> newSource;

    int ns = newSource.AddToTail();
    newSource[ns].Init(pOrigSource->numvertices);

    for (int m = 0; m < pOrigSource->nummeshes; m++) {
        s_mesh_t *pOrigMesh = &pOrigSource->mesh[m];

        for (int f = pOrigMesh->faceoffset; f < pOrigMesh->faceoffset + pOrigMesh->numfaces; f++) {
            // make sure all the total for all the meshes in the model don't go over limit
            int nVertsInFace = (pOrigSource->face[f].d == 0) ? 3 : 4;
            if ((newSource[ns].m_vertex.Count() + nVertsInFace) > g_StudioMdlContext.maxVertexClamp) {
                // go to the next model
                ns = newSource.AddToTail();
                newSource[ns].Init(pOrigSource->numvertices);
            }

            // build face
            s_face_t face;
            face.a = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].a, m, m);
            face.b = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].b, m, m);
            face.c = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].c, m, m);
            if (pOrigSource->face[f].d != 0)
                face.d = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].d, m, m);
            else
                face.d = 0;

            if (newSource[ns].m_mesh[m].numfaces == 0) {
                newSource[ns].m_mesh[m].faceoffset = newSource[ns].m_face.Count();
            }
            newSource[ns].m_face.AddToTail(face);
            newSource[ns].m_mesh[m].numfaces++;
        }
    }

    // Split animations into the new sub-models
    for (int n = 0; n < newSource.Count(); n++) {
        newSource[n].AddAnimations(pOrigSource);
        newSource[n].m_nummeshes = pOrigSource->nummeshes;
    }

    // copy over new meshes and animations back into initial source
    free(pOrigSource->face);
    free(pOrigSource->vertex);
    newSource[0].Copy(pOrigSource);

    for (int n = 1; n < newSource.Count(); n++) {
        // create a new internal "source"
        s_source_t *pSource = (s_source_t *) calloc(1, sizeof(s_source_t));
        g_source[g_numsources++] = pSource;

        // copy all the members, in order
        memcpy(&(pSource->filename[0]), &(pOrigSource->filename[0]), sizeof(pSource->filename));

        // copy over the faces/vertices/animations
        newSource[n].Copy(pSource);

        // copy settings
        pSource->isActiveModel = true;

        // copy skeleton
        pSource->numbones = pOrigSource->numbones;
        for (int i = 0; i < pSource->numbones; i++) {
            pSource->localBone[i] = pOrigSource->localBone[i];
            pSource->boneToPose[i] = pOrigSource->boneToPose[i];
        }


        // The following members are set up later on in the process, so we don't need to copy them here:
        //   pSource->boneflags
        //   pSource->boneref
        //   pSource->boneLocalToGlobal
        //   pSource->boneGlobalToLocal
        //   pSource->m_GlobalVertices


        // copy mesh data
        for (int i = 0; i < pSource->nummeshes; i++) {
            pSource->texmap[i] = pOrigSource->texmap[i];
            pSource->meshindex[i] = pOrigSource->meshindex[i];
        }

        // copy settings
        pSource->adjust = pOrigSource->adjust;
        pSource->scale = pOrigSource->scale;
        pSource->rotation = pOrigSource->rotation;
        pSource->bNoAutoDMXRules = pOrigSource->bNoAutoDMXRules;

        // allocate a model
        s_model_t *pModel = (s_model_t *) calloc(1, sizeof(s_model_t));
        pModel->source = pSource;
        sprintf(pModel->name, "%s%d", "clamped", n);
        int imodel = g_nummodels++;
        g_model[imodel] = pModel;

        // make it a new bodypart
        g_bodypart[g_numbodyparts].nummodels = 1;
        g_bodypart[g_numbodyparts].base =
                g_bodypart[g_numbodyparts - 1].base * g_bodypart[g_numbodyparts - 1].nummodels;
        sprintf(g_bodypart[g_numbodyparts].name, "%s%d", "clamped", n);
        g_bodypart[g_numbodyparts].pmodel[0] = pModel;
        g_numbodyparts++;

        // finally, copy flex keys
        newSource[n].CopyFlexKeys(pOrigSource, pSource, imodel);

        // NOTE: we leave attachments on the first sub-model, we don't want to duplicate those
    }
}

void SetSkinValues() {
    int i, j;
    int index;

    // Check all textures to see if we have relative paths specified
    for (i = 0; i < g_numtextures; i++) {
        if (g_texture[i].flags & RELATIVE_TEXTURE_PATH_SPECIFIED) {
            // Add an empty path to prepend if anything specifies a relative path
            cdtextures[numcdtextures] = 0;
            ++numcdtextures;
            break;
        }
    }

    if (numcdtextures == 0) {
        char szName[256];

        // strip down till it finds "models"
        strcpyn(szName, g_fullpath);
        while (szName[0] != '\0' && strnicmp("models", szName, 6) != 0) {
            strcpy(&szName[0], &szName[1]);
        }
        if (szName[0] != '\0') {
            Q_StripFilename(szName);
            strcat(szName, "/");
        } else {
//			if( *g_pPlatformName )
//			{
//				strcat( szName, "platform_" );
//				strcat( szName, g_pPlatformName );
//				strcat( szName, "/" );
//			}
            strcpy(szName, "models/");
            strcat(szName, g_outname);
            Q_StripExtension(szName, szName, sizeof(szName));
            strcat(szName, "/");
        }
        cdtextures[0] = strdup(szName);
        numcdtextures = 1;
    }

    for (i = 0; i < g_numtextures; i++) {
        char szName[256];
        Q_StripExtension(g_texture[i].name, szName, sizeof(szName));
        Q_strncpy(g_texture[i].name, szName, sizeof(g_texture[i].name));
    }

    // build texture groups
    for (i = 0; i < MAXSTUDIOSKINS; i++) {
        for (j = 0; j < MAXSTUDIOSKINS; j++) {
            g_skinref[i][j] = j;
        }
    }
    index = 0;
    for (i = 0; i < g_numtexturelayers[0]; i++) {
        for (j = 0; j < g_numtexturereps[0]; j++) {
            g_skinref[i][g_texturegroup[0][0][j]] = g_texturegroup[0][i][j];
        }
    }

    if (i != 0) {
        g_numskinfamilies = i;
    } else {
        g_numskinfamilies = 1;
    }
    g_numskinref = g_numtextures;

    // printf ("width: %i  height: %i\n",width, height);
    /*
	printf ("adjusted width: %i height: %i  top : %i  left: %i\n",
			pmesh->skinwidth, pmesh->skinheight, pmesh->skintop, pmesh->skinleft );
	*/
}

//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
void ConsistencyCheckSurfaceProp() {
    for (int i = g_StudioMdlContext.JointSurfaceProp.Count(); --i >= 0;) {
        int j = findGlobalBone(g_StudioMdlContext.JointSurfaceProp[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint surface property for joint\n"
                       "    \"%s\" which either doesn't exist or was optimized out.\n",
                       g_StudioMdlContext.JointSurfaceProp[i].m_pJointName);
        }
    }
}

//-----------------------------------------------------------------------------
// Checks specified contents
//-----------------------------------------------------------------------------
void ConsistencyCheckContents() {
    for (int i = s_JointContents.Count(); --i >= 0;) {
        int j = findGlobalBone(s_JointContents[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint contents for joint\n"
                       "    \"%s\" which either doesn't exist or was optimized out.\n",
                       g_StudioMdlContext.JointSurfaceProp[i].m_pJointName);
        }
    }
}

//-----------------------------------------------------------------------------
// WriteFileToDisk
//	Equivalent to g_pFullFileSystem->WriteFile( pFileName, pPath, buf ), but works
//	for relative paths.
//-----------------------------------------------------------------------------
bool WriteFileToDisk(const char *pFileName, const char *pPath, CUtlBuffer &buf) {
    // For some reason calling full filesystem will write into hl2 root dir
    // return g_pFullFileSystem->WriteFile( pFileName, pPath, buf );

    FILE *f = fopen(pFileName, "wb");
    if (!f)
        return false;

    fwrite(buf.Base(), 1, buf.TellPut(), f);
    fclose(f);
    return true;
}

//-----------------------------------------------------------------------------
// WriteBufferToFile
//	Helper to concatenate file base and extension.
//-----------------------------------------------------------------------------
bool WriteBufferToFile(CUtlBuffer &buf, const char *szFilebase, const char *szExt) {
    char szFilename[1024];
    Q_snprintf(szFilename, ARRAYSIZE(szFilename), "%s%s", szFilebase, szExt);
    return WriteFileToDisk(szFilename, nullptr, buf);
}

//-----------------------------------------------------------------------------
// LoadBufferFromFile
//	Loads the buffer from file, return true on success, false otherwise.
//  If bError is true prints an error upon failure.
//-----------------------------------------------------------------------------
bool LoadBufferFromFile(CUtlBuffer &buffer, const char *szFilebase, const char *szExt, bool bError = true) {
    char szFilename[1024];
    Q_snprintf(szFilename, ARRAYSIZE(szFilename), "%s%s", szFilebase, szExt);

    if (g_pFullFileSystem->ReadFile(szFilename, nullptr, buffer))
        return true;

    if (bError)
        MdlError("Failed to open '%s'!\n", szFilename);

    return false;
}

bool Load3ModelBuffers(CUtlBuffer &bufMDL, CUtlBuffer &bufVVD, CUtlBuffer &bufVTX, const char *szFilebase) {
    // Load up the mdl file
    if (!LoadBufferFromFile(bufMDL, szFilebase, ".mdl"))
        return false;

    // Load up the vvd file
    if (!LoadBufferFromFile(bufVVD, szFilebase, ".vvd"))
        return false;

    // Load up the dx90.vtx file
    if (!LoadBufferFromFile(bufVTX, szFilebase, ".dx90.vtx"))
        return false;

    return true;
}

void UsageAndExit() {
    MdlError("Bad or missing options\n"
             #ifdef MDLCOMPILE
             "usage: mdlcompile [options] <file.mc>\n"
             #else
             "usage: studiomdl [options] <file.qc>\n"
             #endif
             "options:\n"
             "[-a <normal_blend_angle>]\n"
             "[-checklengths]\n"
             "[-d] - dump glview files\n"
             "[-definebones]\n"
             "[-f] - flip all triangles\n"
             "[-fullcollide] - don't truncate really big collisionmodels\n"
             "[-game <gamedir>]\n"
             "[-h] - dump hboxes\n"
             "[-i] - ignore warnings\n"
             "[-minlod <lod>] - truncate to highest detail <lod>\n"
             "[-n] - tag bad normals\n"
             "[-perf] report perf info upon compiling model\n"
             "[-printbones]\n"
             "[-printgraph]\n"
             "[-quiet] - operate silently\n"
             "[-r] - tag reversed\n"
             "[-t <texture>]\n"
             "[-x360] - generate xbox360 output\n"
             "[-nox360] - disable xbox360 output(default)\n"
             "[-fastbuild] - write a single vertex windings file\n"
             "[-nowarnings] - disable warnings\n"
             "[-dumpmaterials] - dump out material names\n"
             "[-mdlreport] model.mdl - report perf info\n"
             "[-mdlreportspreadsheet] - report perf info as a comma-delimited spreadsheet\n"
             "[-striplods] - use only lod0\n"
             "[-overridedefinebones] - equivalent to specifying $unlockdefinebones in " SRC_FILE_EXT " file\n"
             "[-stripmodel] - process binary model files and strip extra lod data\n"
             "[-stripvhv] - strip hardware verts to match the stripped model\n"
             "[-vsi] - generate stripping information .vsi file - can be used on .mdl files too\n"
             "[-allowdebug]\n"
             "[-ihvtest]\n"
             "[-overridedefinebones]\n"
             "[-verbose]\n"
             "[-makefile]\n"
             "[-verify]\n"
             "[-fastbuild]\n"
             "[-maxwarnings]\n"
             "[-preview]\n"
             "[-dumpmaterials]\n"
             "[-basedir]\n"
             "[-tempcontent]\n"
             "[-nop4]\n"
    );
}

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
int CStudioMDLApp::Main() {
    g_StudioMdlContext.numverts = g_StudioMdlContext.numnormals = g_StudioMdlContext.numfaces = 0;
    for (int &g_numtexcoord: g_StudioMdlContext.numtexcoords) {
        g_numtexcoord = 0;
    }

    // This bit of hackery allows us to access files on the harddrive
    g_pFullFileSystem->AddSearchPath("", "LOCAL", PATH_ADD_TO_HEAD);

    int nReturnValue;
    if (HandleMdlReport(nReturnValue))
        return false;

    // Don't bother with undo here
    g_pDataModel->SetUndoEnabled(false);

    AddContentPaths();

    ParseGameInfo();

    if (!g_StudioMdlContext.quiet) {
        printf("qdir:    \"%s\"\n", qdir);
        printf("gamedir: \"%s\"\n", gamedir);
        printf("g_path:  \"%s\"\n", g_StudioMdlContext.g_path);
    }

    switch (g_eRunMode) {
        case RUN_MODE_STRIP_MODEL:
            return Main_StripModel();

        case RUN_MODE_STRIP_VHV:
            return Main_StripVhv();

        case RUN_MODE_BUILD:
        default:
            break;
    }

    const char *pExt = Q_GetFileExtension(g_StudioMdlContext.g_path);

    // Look for the presence of a .mdl file (only -vsi is currently supported for .mdl files)
    if (pExt && !Q_stricmp(pExt, "mdl")) {
        if (g_StudioMdlContext.bMakeVsi)
            return Main_MakeVsi();

        printf("ERROR: " SRC_FILE_EXT " or .dmx file should be specified to build.\n");
        return 1;
    }

    if (!g_StudioMdlContext.quiet)
        printf("Building binary model files...\n");

    bool bLoadingPreprocessedFile = false;
#ifdef MDLCOMPILE
    if ( pExt && !Q_stricmp( pExt, "mpp" ) )
    {
        bLoadingPreprocessedFile = true;
        // Handle relative path names because g_path is appended onto qdir which is the
        // absolute path to the file minus the filename
        Q_FileBase( g_path, g_path, sizeof( g_path ) );
        Q_DefaultExtension( g_path, "mpp" , sizeof( g_path ) );
    }
    if ( !pExt && !bLoadingPreprocessedFile )
    {
#endif
    Q_FileBase(g_StudioMdlContext.g_path, g_StudioMdlContext.g_path, sizeof(g_StudioMdlContext.g_path));
    Q_DefaultExtension(g_StudioMdlContext.g_path, SRC_FILE_EXT, sizeof(g_StudioMdlContext.g_path));
    if (!pExt) {
        pExt = SRC_FILE_EXT;
    }
#ifdef MDLCOMPILE
    }
#endif

    if (!g_StudioMdlContext.quiet) {
        printf("Working on \"%s\"\n", g_StudioMdlContext.g_path);
    }

    // Set up script loading callback, discarding default callback
    (void) SetScriptLoadedCallback(StudioMdl_ScriptLoadedCallback);

    // load the script
    if (!bLoadingPreprocessedFile) {
        LoadScriptFile(g_StudioMdlContext.g_path);
    }

    strcpy(g_fullpath, g_StudioMdlContext.g_path);
    strcpy(g_fullpath, ExpandPath(g_fullpath));
    strcpy(g_fullpath, ExpandArg(g_fullpath));

    // default to having one entry in the LOD list that doesn't do anything so
    // that we don't have to do any special cases for the first LOD.
    g_ScriptLODs.Purge();
    g_ScriptLODs.AddToTail(); // add an empty one
    g_ScriptLODs[0].switchValue = 0.0f;

    //
    // parse it
    //
    ClearModel();

//	strcpy( g_pPlatformName, "" );
    if (bLoadingPreprocessedFile) {
        if (!ParsePreprocessedFile(g_fullpath)) {
            MdlError("Invalid MPP File: %s\n", g_StudioMdlContext.g_path);
            return 1;
        }
    } else {
        ParseScript(pExt);
    }

    if (!g_StudioMdlContext.createMakefile) {
        int nCount = g_numsources;
        for (int i = 0; i < nCount; i++) {
            if (g_source[i]->isActiveModel) {
                ClampMaxVerticesPerModel(g_source[i]);
            }
        }

        SetSkinValues();

        SimplifyModel();

        ConsistencyCheckSurfaceProp();
        ConsistencyCheckContents();
        // ValidateSharedAnimationGroups();

        WriteModelFiles();
    }

    if (g_StudioMdlContext.createMakefile) {
        CreateMakefile_OutputMakefile();
    } else if (g_StudioMdlContext.bMakeVsi) {
        Q_snprintf(g_StudioMdlContext.g_path, ARRAYSIZE(g_StudioMdlContext.g_path), "%smodels/%s", gamedir, g_outname);
        Main_MakeVsi();
    }

    if (!g_StudioMdlContext.quiet) {
        printf("\nCompleted \"%s\"\n", g_StudioMdlContext.g_path);
    }

    if (g_StudioMdlContext.parseable_completion_output) {
        printf("\nRESULT: SUCCESS\n");
    }

    g_pDataModel->UnloadFile(DMFILEID_INVALID);

    return 0;
}

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
bool CStudioMDLApp::Create() {
    // Ensure that cmdlib spew function & associated state is initialized
    InstallSpewFunction();
    // Override the cmdlib spew function
    LoggingSystem_PushLoggingState();
    LoggingSystem_RegisterLoggingListener(&s_MdlLoggingListener);

    MathLib_Init(2.2f, 2.2f, 0.0f, 2.0f, false, true, true, true);

    SetUnhandledExceptionFilter(VExceptionFilter);


    if (CommandLine()->ParmCount() == 1) {
        UsageAndExit();
        return false;
    }

    int nReturnValue;
    if (HandlePrintSurfaceProps(nReturnValue))
        return false;

    if (!ParseArguments())
        return false;


    AddSystem(g_pDataModel, VDATAMODEL_INTERFACE_VERSION);
    AddSystem(g_pDmElementFramework, VDMELEMENTFRAMEWORK_VERSION);
    AddSystem(g_pDmSerializers, DMSERIALIZERS_INTERFACE_VERSION);

    return true;
}

void CStudioMDLApp::Destroy() {
    LoggingSystem_PopLoggingState();
}

extern CFileSystem_Stdio g_FileSystem_Stdio;

bool CStudioMDLApp::PreInit() {
    CreateInterfaceFn factory = GetFactory();
    ConnectTier1Libraries(&factory, 1);
    ConnectTier2Libraries(&factory, 1);
    g_pFullFileSystem = &g_FileSystem_Stdio;
    g_pFileSystem = g_pFullFileSystem;
    if (!g_pFullFileSystem || !g_pDataModel) {
        Warning("StudioMDL is missing a required interface!\n");
        return false;
    }

    if (!SetupSearchPaths(g_StudioMdlContext.g_path, false, true))
        return false;

    // NOTE: This is necessary to get the cmdlib filesystem stuff to work.
    g_pFileSystem = g_pFullFileSystem;

    // NOTE: This is stuff copied out of cmdlib necessary to get
    // the tools in cmdlib working
    FileSystem_SetupStandardDirectories(g_StudioMdlContext.g_path, GetGameInfoPath());
    return true;
}

void CStudioMDLApp::PostShutdown() {
//    DisconnectTier3Libraries();
    DisconnectTier2Libraries();
    DisconnectTier1Libraries();
}

//-----------------------------------------------------------------------------
// Method which parses arguments
//-----------------------------------------------------------------------------
bool CStudioMDLApp::ParseArguments() {
    g_currentscale = g_defaultscale = 1.0;
    g_defaultrotation = RadianEuler(0, 0, M_PI / 2);

    // skip weightlist 0
    g_numweightlist = 1;

    eyeposition = Vector(0, 0, 0);
    gflags = 0;
    numrep = 0;

    normal_blend = cos(DEG2RAD(2.0));

    g_gamma = 2.2;

    g_staticprop = false;
    g_centerstaticprop = false;

    g_realignbones = false;
    g_constdirectionalightdot = 0;

    g_bDumpGLViewFiles = false;
    g_StudioMdlContext.quiet = false;

    g_illumpositionattachment = 0;
    g_flMaxEyeDeflection = 0.0f;

    g_StudioMdlContext.collapse_bones_message = false;

    int argc = CommandLine()->ParmCount();
    int i;
    for (i = 1; i < argc - 1; i++) {
        const char *pArgv = CommandLine()->GetParm(i);
        if (pArgv[0] != '-')
            continue;

        if (!Q_stricmp(pArgv, "-collapsereport")) {
            g_StudioMdlContext.collapse_bones_message = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-parsecompletion")) {
            // reliably prints output we can parse for automatically
            g_StudioMdlContext.parseable_completion_output = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-allowdebug")) {
            // Ignore, used by interface system to catch debug builds checked into release tree
            continue;
        }

        if (!Q_stricmp(pArgv, "-mdlreport")) {
            // Will reparse later, ignore rest of arguments.
            return true;
        }

        if (!Q_stricmp(pArgv, "-mdlreportspreadsheet")) {
            // Will reparse later, ignore for now.
            continue;
        }

        if (!Q_stricmp(pArgv, "-overridedefinebones")) {
            g_StudioMdlContext.defineBonesLockedByDefault = false;
            continue;
        }

        if (!Q_stricmp(pArgv, "-striplods")) {
            g_StudioMdlContext.stripLods = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-stripmodel")) {
            g_eRunMode = RUN_MODE_STRIP_MODEL;
            continue;
        }

        if (!Q_stricmp(pArgv, "-stripvhv")) {
            g_eRunMode = RUN_MODE_STRIP_VHV;
            continue;
        }

        if (!Q_stricmp(pArgv, "-vsi")) {
            g_StudioMdlContext.bMakeVsi = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-quiet")) {
            g_StudioMdlContext.quiet = true;
            g_StudioMdlContext.verbose = false;
            continue;
        }

        if (!Q_stricmp(pArgv, "-verbose")) {
            g_StudioMdlContext.quiet = false;
            g_StudioMdlContext.verbose = true;
            continue;
        }


        if (!Q_stricmp(pArgv, "-checklengths")) {
            g_StudioMdlContext.checkLengths = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-printbones")) {
            g_StudioMdlContext.printBones = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-perf")) {
            g_StudioMdlContext.perf = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-printgraph")) {
            g_StudioMdlContext.dumpGraph = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-definebones")) {
            g_definebones = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-makefile")) {
            g_StudioMdlContext.createMakefile = true;
            g_StudioMdlContext.quiet = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-verify")) {
            g_StudioMdlContext.verifyOnly = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-minlod")) {
            g_StudioMdlContext.minLod = atoi(CommandLine()->GetParm(++i));
            continue;
        }

        if (!Q_stricmp(pArgv, "-fastbuild")) {
            g_StudioMdlContext.fastBuild = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-nowarnings")) {
            g_StudioMdlContext.bNoWarnings = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-maxwarnings")) {
            g_StudioMdlContext.g_maxWarnings = atoi(CommandLine()->GetParm(++i));
            continue;
        }

        if (!Q_stricmp(pArgv, "-preview")) {
            g_StudioMdlContext.buildPreview = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-dumpmaterials")) {
            g_StudioMdlContext.dumpMaterials = true;
            continue;
        }

        if (pArgv[1] && pArgv[2] == '\0') {
            switch (pArgv[1]) {
                case 't':
                    i++;
                    strcpy(defaulttexture[numrep], pArgv);
                    if (i < argc - 2 && CommandLine()->GetParm(i + 1)[0] != '-') {
                        i++;
                        strcpy(sourcetexture[numrep], pArgv);
                        printf("Replacing %s with %s\n", sourcetexture[numrep], defaulttexture[numrep]);
                    }
                    printf("Using default texture: %s\n", defaulttexture);
                    numrep++;
                    break;
                case 'a':
                    i++;
                    normal_blend = cos(DEG2RAD(verify_atof(pArgv)));
                    break;
                case 'h':
                    dump_hboxes = 1;
                    break;
                case 'i':
                    ignore_warnings = 1;
                    break;
                case 'd':
                    g_bDumpGLViewFiles = true;
                    break;
//			case 'p':
//				i++;
//				strcpy( qproject, pArgv );
//				break;
            }
        }
    }

    if (i >= argc) {
        // misformed arguments
        // otherwise generating unintended results
        UsageAndExit();
        return false;
    }

    const char *pArgv = CommandLine()->GetParm(i);
    Q_strncpy(g_StudioMdlContext.g_path, pArgv, sizeof(g_StudioMdlContext.g_path));
    if (Q_IsAbsolutePath(g_StudioMdlContext.g_path)) {
        // Set the working directory to be the path of the qc file
        // so the relative-file fopen code works
        char pQCDir[MAX_PATH];
        Q_ExtractFilePath(g_StudioMdlContext.g_path, pQCDir, sizeof(pQCDir));
        _chdir(pQCDir);
    }
    Q_StripExtension(pArgv, g_outname, sizeof(g_outname));
    return true;
}

//-----------------------------------------------------------------------------
// Studiomdl hooks to call the stripping routines:
//	Main_StripVhv
//	Main_StripModel
//-----------------------------------------------------------------------------

int CStudioMDLApp::Main_StripVhv() {
    if (!g_StudioMdlContext.quiet) {
        printf("Stripping vhv data...\n");
    }

    if (!mdllib) {
        printf("ERROR: mdllib is not available!\n");
        return 1;
    }

    Q_StripExtension(g_StudioMdlContext.g_path, g_StudioMdlContext.g_path, sizeof(g_StudioMdlContext.g_path));
    char *pExt = g_StudioMdlContext.g_path + strlen(g_StudioMdlContext.g_path);
    *pExt = 0;

    //
    // ====== Load files
    //

    // Load up the vhv file
    CUtlBuffer bufVHV;
    if (!LoadBufferFromFile(bufVHV, g_StudioMdlContext.g_path, ".vhv"))
        return 1;

    // Load up the info.strip file
    CUtlBuffer bufRemapping;
    if (!LoadBufferFromFile(bufRemapping, g_StudioMdlContext.g_path, ".info.strip", false) &&
        !LoadBufferFromFile(bufRemapping, g_StudioMdlContext.g_path, ".vsi"))
        return 1;

    //
    // ====== Process file contents
    //

    bool bResult = false;
    {
        LoggingSystem_SetChannelSpewLevelByName("ModelLib", LS_MESSAGE);

        IMdlStripInfo *pMdlStripInfo = NULL;

        if (mdllib->CreateNewStripInfo(&pMdlStripInfo)) {
            pMdlStripInfo->UnSerialize(bufRemapping);
            bResult = pMdlStripInfo->StripHardwareVertsBuffer(bufVHV);
        }

        if (pMdlStripInfo)
            pMdlStripInfo->DeleteThis();
    }

    if (!bResult) {
        printf("ERROR: stripping failed!\n");
        return 1;
    }

    //
    // ====== Save out processed data
    //

    // Save vhv
    if (!WriteBufferToFile(bufVHV, g_StudioMdlContext.g_path, ".vhv.strip")) {
        printf("ERROR: Failed to save '%s'!\n", g_StudioMdlContext.g_path);
        return 1;
    }

    return 0;
}

int CStudioMDLApp::Main_MakeVsi() {
    if (!mdllib) {
        printf("ERROR: mdllib is not available!\n");
        return 1;
    }

    Q_StripExtension(g_StudioMdlContext.g_path, g_StudioMdlContext.g_path, sizeof(g_StudioMdlContext.g_path));
    char *pExt = g_StudioMdlContext.g_path + strlen(g_StudioMdlContext.g_path);
    *pExt = 0;

    // Load up the files
    CUtlBuffer bufMDL;
    CUtlBuffer bufVVD;
    CUtlBuffer bufVTX;
    if (!Load3ModelBuffers(bufMDL, bufVVD, bufVTX, g_StudioMdlContext.g_path))
        return 1;

    //
    // ====== Process file contents
    //

    CUtlBuffer bufMappingTable;
    bool bResult = false;
    {
        if (!g_StudioMdlContext.quiet) {
            printf("---------------------\n");
            printf("Generating .vsi stripping information...\n");

            LoggingSystem_SetChannelSpewLevelByName("ModelLib", LS_MESSAGE);
        }

        IMdlStripInfo *pMdlStripInfo = NULL;

        bResult =
                mdllib->StripModelBuffers(bufMDL, bufVVD, bufVTX, &pMdlStripInfo) &&
                pMdlStripInfo->Serialize(bufMappingTable);

        if (pMdlStripInfo)
            pMdlStripInfo->DeleteThis();
    }

    if (!bResult) {
        printf("ERROR: stripping failed!\n");
        return 1;
    }

    //
    // ====== Save out processed data
    //

    // Save remapping data using "P4 edit -> save -> P4 add"  approach
    sprintf(pExt, ".vsi");
//	CP4AutoEditAddFile _auto_edit_vsi( g_path );

    if (!WriteFileToDisk(g_StudioMdlContext.g_path, nullptr, bufMappingTable)) {
        printf("ERROR: Failed to save '%s'!\n", g_StudioMdlContext.g_path);
        return 1;
    } else if (!g_StudioMdlContext.quiet) {
        printf("Generated .vsi stripping information.\n");
    }

    return 0;
}

int CStudioMDLApp::Main_StripModel() {
    if (!g_StudioMdlContext.quiet) {
        printf("Stripping binary model files...\n");
    }

    if (!mdllib) {
        printf("ERROR: mdllib is not available!\n");
        return 1;
    }

    Q_FileBase(g_StudioMdlContext.g_path, g_StudioMdlContext.g_path, sizeof(g_StudioMdlContext.g_path));
    char *pExt = g_StudioMdlContext.g_path + strlen(g_StudioMdlContext.g_path);
    *pExt = 0;

    // Load up the files
    CUtlBuffer bufMDL;
    CUtlBuffer bufVVD;
    CUtlBuffer bufVTX;
    if (!Load3ModelBuffers(bufMDL, bufVVD, bufVTX, g_StudioMdlContext.g_path))
        return 1;

    //
    // ====== Process file contents
    //

    CUtlBuffer bufMappingTable;
    bool bResult = false;
    {
        LoggingSystem_SetChannelSpewLevelByName("ModelLib", LS_MESSAGE);

        IMdlStripInfo *pMdlStripInfo = NULL;

        bResult =
                mdllib->StripModelBuffers(bufMDL, bufVVD, bufVTX, &pMdlStripInfo) &&
                pMdlStripInfo->Serialize(bufMappingTable);

        if (pMdlStripInfo)
            pMdlStripInfo->DeleteThis();
    }

    if (!bResult) {
        printf("ERROR: stripping failed!\n");
        return 1;
    }

    //
    // ====== Save out processed data
    //

    // Save mdl
    sprintf(pExt, ".mdl.strip");
    if (!WriteFileToDisk(g_StudioMdlContext.g_path, nullptr, bufMDL)) {
        printf("ERROR: Failed to save '%s'!\n", g_StudioMdlContext.g_path);
        return 1;
    }

    // Save vvd
    sprintf(pExt, ".vvd.strip");
    if (!WriteFileToDisk(g_StudioMdlContext.g_path, nullptr, bufVVD)) {
        printf("ERROR: Failed to save '%s'!\n", g_StudioMdlContext.g_path);
        return 1;
    }

    // Save vtx
    sprintf(pExt, ".vtx.strip");
    if (!WriteFileToDisk(g_StudioMdlContext.g_path, nullptr, bufVTX)) {
        printf("ERROR: Failed to save '%s'!\n", g_StudioMdlContext.g_path);
        return 1;
    }

    // Save remapping data
    sprintf(pExt, ".info.strip");
    if (!WriteFileToDisk(g_StudioMdlContext.g_path, nullptr, bufMappingTable)) {
        printf("ERROR: Failed to save '%s'!\n", g_StudioMdlContext.g_path);
        return 1;
    }

    return 0;
}
