//
// Created by red_eye on 11/2/26.
//

#pragma once
#include <memory>
#include <string>
#include <vector>

#include "studiomdl_constants.hpp"
#include "studiomdl.h"
#include "mathlib/vector.h"
#include "tier0/platform.h"
#include "filesystem/stdfile.h"
#include "tier1_v2/fixed_string.hpp"
#include "tier1_v2/fixed_vector.hpp"

struct StudioMDLGlobalState {
    StudioMDLGlobalState() noexcept = default;

    StudioPath outname; // the output model name ($modelname): ASCII, relative to <game>/models/
    StudioPath szInternalName;
    qboolean cdset;

    float flCollisionPrecision;

    // Directory stack (UTF-8, '/' terminated): [0] is the base ($cd, or "" = relative to qdir), $pushd/$popd push/pop on top.
    // Never empty: FindCachedSource and $pushd read back().
    std::vector<std::string> cddir{std::string()};

    vstd::FixedVector<char *, 16> cdtextures;
    std::string fullpath; // absolute UTF-8 path of the QC

    StudioName rootname; // name of the root bone
    float defaultscale;
    float currentscale;
    RadianEuler defaultrotation;

    vstd::FixedVector<StudioPath, 16> defaulttextures;
    vstd::FixedVector<StudioPath, 16> sourcetextures;

    int numrep;

    float normal_blend;
    int dump_hboxes;
    int ignore_warnings;

    Vector eyeposition;
    float flMaxEyeDeflection;
    int illumpositionattachment;
    Vector illumposition;
    int illumpositionset;
    uint32_t gflags;
    std::array<Vector, 2> bbox;
    std::array<Vector, 2> cbox;
    bool wrotebbox;
    bool wrotecbox;
    bool bboxonlyverts;

    int clip_texcoords;
    bool staticprop;
    bool centerstaticprop;

    bool realignbones;
    bool definebones;
    bool bSkinnedLODs;

    uint8_t constdirectionalightdot;

    vstd::FixedVector<s_texture_t, MAXSTUDIOSKINS> textures;

    vstd::FixedVector<int, MAXSTUDIOSKINS> materials; // link into texture array

    float gamma;
    int numskinref;
    int numskinfamilies;
    int skinref[256][MAXSTUDIOSKINS]; // [skin][skinref], returns texture index
    int numtexturegroups;
    int numtexturelayers[32];
    int numtexturereps[32];
    int texturegroup[32][32][32];

    std::array<s_poseparameter_t, 32> pose; // FIXME: this shouldn't be hard coded

    // Transition nodes: node id N (1-based, as stored in sequences) is xnodename[N - 1]; xnodename.size() is the node count.
    // xnode[from - 1][to - 1] is the next node on the way (0 = no transition); a fixed matrix indexed by node id.
    vstd::FixedVector<std::string, 100> xnodenames;
    std::array<std::array<int, 100>, 100> xnodes{};
    vstd::FixedVector<std::array<int, 2>, 10000> xnodeskips;

    vstd::FixedVector<s_animblock_t, MAXSTUDIOANIMBLOCKS> animblocks;

    int animblocksize;
    StudioPath animblockname;
    int animblockmaxframes;

    int numposeparameters;

    CUtlVector<s_sequence_t> sequence;

    int numikautoplaylocks;
    s_iklock_t ikautoplaylock[16];

    vstd::FixedVector<s_cmdlist_t, MAXSTUDIOANIMS> cmdlists;
    vstd::FixedVector<s_animation_t *, MAXSTUDIOANIMS> animations;
    vstd::FixedVector<s_source_t *, MAXSTUDIOSEQUENCES> sources;

    int nummodels;
    int nummodelsbeforeLOD;
    CUtlVectorAuto<s_model_t *> model;

    vstd::FixedVector<s_flexdesc_t,MAXSTUDIOFLEXDESC> flexdescs;
    vstd::FixedVector<s_flexcontroller_t, MAXSTUDIOFLEXCTRL> flexcontrollers;
    vstd::FixedVector<s_flexkey_t, MAXSTUDIOFLEXKEYS> flexkeys;
    s_flexkey_t *defaultflexkey;
    vstd::FixedVector<s_flexrule_t, MAXSTUDIOFLEXRULES> flexrules;

    Vector defaultadjust;

    int numbodyparts;
    CUtlVectorAuto<s_bodypart_t> bodypart;

    int numbodygrouppresets;
    CUtlVectorAuto<s_bodygrouppreset_t> bodygrouppresets;

    int numweightlist;
    s_weightlist_t weightlist[MAXWEIGHTLISTS];

    int numikchains;
    s_ikchain_t ikchain[16];

    int numjigglebones;
    s_jigglebone_t jigglebones[MAXSTUDIOBONES];
    int jigglebonemap[MAXSTUDIOBONES]; // map used jigglebone's to source jigglebonebone's

    int numaxisinterpbones;
    s_axisinterpbone_t axisinterpbones[MAXSTUDIOBONES];
    int axisinterpbonemap[MAXSTUDIOBONES]; // map used axisinterpbone's to source axisinterpbone's

    int numquatinterpbones;
    s_quatinterpbone_t quatinterpbones[MAXSTUDIOBONES];
    int quatinterpbonemap[MAXSTUDIOBONES]; // map used quatinterpbone's to source axisinterpbone's

    int numaimatbones;
    s_aimatbone_t aimatbones[MAXSTUDIOBONES];
    int aimatbonemap[MAXSTUDIOBONES]; // map used aimatpbone's to source aimatpbone's (may be optimized out)

    CUtlVector<CTwistBone> twistbones;
    CUtlVector<CConstraintBoneBase *> constraintBones;

    int numforcedhierarchy;
    s_forcedhierarchy_t forcedhierarchy[MAXSTUDIOBONES];

    int numforcedrealign;
    s_forcedrealign_t forcedrealign[MAXSTUDIOBONES];

    int numlimitrotation;
    s_limitrotation_t limitrotation[MAXSTUDIOBONES];

    CUtlVector<s_bonesaveframe_t> bonesaveframe;

    // Unified vertices of the source being loaded: one per distinct (position, material, normal, texcoords), grown on demand
    std::vector<v_unify_t> v_listdata;
    // Per source position: v_listdata index of its first entry, -1 = none; entries chain through v_unify_t::next
    std::vector<int> v_list;

    CUtlVector<LodScriptData_t> ScriptLODs;
    CUtlVector<char *> collapse;

    std::array<s_bonetable_t, MAXSTUDIOSRCBONES> bonetable;
    int numrenamedbones;

    std::array<s_renamebone_t, MAXSTUDIOSRCBONES> renamedbone;

    vstd::FixedVector<StudioName,MAXSTUDIOSRCBONES> szStripBonePrefix;

    std::array<s_renamebone_t, MAXSTUDIOSRCBONES> szRenameBoneSubstr;
    int numRenameBoneSubstr;

    int numimportbones;
    std::array<s_importbone_t, MAXSTUDIOSRCBONES> importbone;

    vstd::FixedVector<s_includemodel_t, 128> includemodels;

    vstd::FixedVector<s_hitgroup_t, MAXSTUDIOSRCBONES> hitgroups;

    vstd::FixedVector<s_bonecontroller_t, MAXSTUDIOSRCBONES> bonecontrollers;

    std::array<s_screenalignedbone_t, MAXSTUDIOSRCBONES> screenalignedbone;
    int numscreenalignedbones;

    std::array<s_worldalignedbone_t, MAXSTUDIOSRCBONES> worldalignedbone;
    int numworldalignedbones;

    vstd::FixedVector<s_attachment_t, MAXSTUDIOSRCBONES> attachments;

    std::vector<s_bonemerge_t> bone_merge;

    std::vector<s_alwayssetup_t> bone_always_setup;

    std::array<s_mouth_t, MAXSTUDIOSRCBONES> mouth; // ?? skins?
    int nummouths;
    uint32_t checksum;
};

struct StudioMdlContext {
    unsigned parseable_completion_output: 1;
    unsigned collapse_bones_message: 1;
    unsigned collapse_bones: 1;
    unsigned collapse_bones_aggressive: 1;
    unsigned quiet: 1;
    unsigned checkLengths: 1;
    unsigned printBones: 1;
    unsigned perf: 1;
    unsigned dumpGraph: 1;
    unsigned multistageGraph: 1;
    unsigned verbose: 1;
    unsigned createMakefile: 1;
    unsigned ZBrush: 1;
    unsigned verifyOnly: 1;
    unsigned useBoneInBBox: 1;
    unsigned lockBoneLengths: 1;
    unsigned defineBonesLockedByDefault: 1;
    unsigned fastBuild: 1;
    unsigned buildPreview: 1;
    unsigned preserveTriangleOrder: 1;
    unsigned centerBonesOnVerts: 1;
    unsigned dumpMaterials: 1;
    unsigned stripLods: 1;
    unsigned noAnimblockStall: 1;
    unsigned animblockHighRes: 1;
    unsigned animblockLowRes: 1;
    unsigned zeroFramesHighres: 1;
    unsigned lCaseAllSequences: 1;
    unsigned errorOnSeqRemapFail: 1;
    unsigned modelIntentionallyHasZeroSequences: 1;
    unsigned contentRootRelative: 1;
    unsigned hasModelName: 1;
    unsigned noWarnings: 1;
    int maxWarnings = -1;

    // ---- Paths: UTF-8 std::string (never FixedString, its byte truncation can cut a UTF-8 sequence) ----
    std::string qcPath;  // the QC as given on the command line, later its file name
    std::string qdir;    // absolute QC directory, native separators, trailing separator; sources are relative to it
    std::string gameDir; // absolute game directory (outputs go to <gameDir>/models/), trailing separator
    std::vector<std::string> contentBasePaths; // content roots that mirror the game search paths (CmdLib_AddBasePath)
    std::vector<std::string> contentRelativeDirs;      // CmdLib_AddNewSearchPath: directories added below every root
    std::vector<std::string> contentOriginalBasePaths; // the roots before the first CmdLib_AddNewSearchPath

    int minLod;
    int numAllowedRootLODs;
    float defaultMotionRollback;
    float CollisionPrecision;
    int minSectionFrameLimit;
    int sectionFrames;
    float preloadTime;
    int maxZeroFrames; // clamped from 1..4
    float minZeroFramePosDelta;
    float defaultFadeInTime;
    float defaultFadeOutTime;
    std::string szFilename; // source file being parsed
    std::unique_ptr<CStdFile> inputFile; // the open SMD/VTA/OBJ (OpenGlobalFile)
    vstd::FixedString<4096> szLine;
    int iLinecount;

    Vector vecMinWorldspace;
    Vector vecMaxWorldspace;
    DmElementHandle_t hDmeBoneFlexDriverList;

    int numtexcoords[MAXSTUDIOTEXCOORDS];
    CUtlVectorAuto<Vector2D> texcoord[MAXSTUDIOTEXCOORDS];

    std::vector<s_hitboxset> hitboxsets;
    std::string KeyValueText;
    std::vector<s_flexcontrollerremap_t> FlexControllerRemap;
    std::vector<CUtlSymbol> CreateMakefileDependencies;
    vstd::FixedString<128> pDefaultSurfaceProp;
    CUtlVector<SurfacePropName_t> JointSurfaceProp;

    char *szInCurrentSeqName;
    std::vector<CUtlString> AllowedActivityNames;

    int maxVertexLimit = MAXSTUDIOVERTS / 3;
    int maxVertexClamp = MAXSTUDIOVERTS / 3;

    // Flex optimization (flexoptimize.cpp): mode, minimum static part of a mesh worth splitting off, and the
    // alias materials it created as (alias material, base material)
    FlexOptimizeMode_t flexOptimizeMode = FLEXOPT_AUTO;
    int32_t flexOptimizeMinStatic = 256;
    std::vector<std::pair<int, int> > flexAliasMaterials;

    int numverts = 0;
    CUtlVectorAuto<Vector> vertex;
    CUtlVectorAuto<s_boneweight_t> bone;

    int numnormals = 0;
    CUtlVectorAuto<Vector> normal;

    std::vector<s_tmpface_t> faces;
    // int numfaces = 0;
    std::vector<s_face_t> src_uface; // max res unified faces
    int numbones = 0;

    StudioMdlContext()
        : parseable_completion_output(0),
          collapse_bones_message(0),
          collapse_bones(0),
          collapse_bones_aggressive(0),
          quiet(0),
          checkLengths(0),
          printBones(0),
          perf(0),
          dumpGraph(0),
          multistageGraph(0),
          verbose(0),
          createMakefile(0),
          ZBrush(0),
          verifyOnly(0),
          useBoneInBBox(1),
          lockBoneLengths(0),
          defineBonesLockedByDefault(1),
          fastBuild(0),
          buildPreview(0),
          preserveTriangleOrder(0),
          centerBonesOnVerts(0),
          dumpMaterials(0),
          stripLods(0),
          noAnimblockStall(0),
          animblockHighRes(0),
          animblockLowRes(0),
          zeroFramesHighres(0),
          lCaseAllSequences(0),
          errorOnSeqRemapFail(0),
          modelIntentionallyHasZeroSequences(0),
          contentRootRelative(0),
          minLod(0),
          numAllowedRootLODs(0),
          hasModelName(0),
          noWarnings(0),
          defaultMotionRollback(0.3f),
          minSectionFrameLimit(30),
          sectionFrames(30),
          preloadTime(1.0f),
          maxZeroFrames(3),
          minZeroFramePosDelta(2.0f),
          defaultFadeInTime(0.2f),
          defaultFadeOutTime(0.2f),
          iLinecount(0),
          vecMinWorldspace(MIN_COORD_INTEGER, MIN_COORD_INTEGER, MIN_COORD_INTEGER),
          vecMaxWorldspace(MAX_COORD_INTEGER, MAX_COORD_INTEGER, MAX_COORD_INTEGER),
          hDmeBoneFlexDriverList(DMELEMENT_HANDLE_INVALID),
          szInCurrentSeqName(nullptr),
          pDefaultSurfaceProp("default") {
    }
};

extern StudioMDLGlobalState g_studiomdl;
extern StudioMdlContext g_StudioMdlContext;
