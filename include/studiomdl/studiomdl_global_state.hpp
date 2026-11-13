//
// Created by red_eye on 11/2/26.
//

#pragma once
#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "studiomdl_constants.hpp"
#include "studiomdl.h"
#include "mathlib/vector.h"
#include "tier0/platform.h"
#include "filesystem/stdfile.h"
#include "tier1_v2/auto_grow_vector.hpp"
#include "tier1_v2/fixed_string.hpp"
#include "tier1_v2/fixed_vector.hpp"

// The whole state of one studiomdl run (a single model per process), as one global: g_studiomdl.

// Command line switches and compile settings
struct StudioMdlOptions {
    bool quiet = false;
    bool verbose = false;
    bool noWarnings = false;
    int maxWarnings = -1;
    bool parseable_completion_output = false;
    bool verifyOnly = false;
    bool perf = false;
    bool printBones = false;
    bool dumpMaterials = false;
    bool dumpGraph = false;
    bool multistageGraph = false;
    bool createMakefile = false;
    bool ZBrush = false;
    bool buildPreview = false;
    bool stripLods = false;
    bool checkLengths = false;
    int dump_hboxes = 0;
    int ignore_warnings = 0;

    // bones
    bool collapse_bones_message = false;
    bool collapse_bones = false;
    bool collapse_bones_aggressive = false;
    bool useBoneInBBox = true;
    bool lockBoneLengths = false;
    bool defineBonesLockedByDefault = true;
    bool centerBonesOnVerts = false;
    bool realignbones = false;
    bool definebones = false;

    // -fitweights [<poses> | <fitting poses>,<test poses>] (experimental), see CWeightFitter
    bool fitWeights = false;
    int fitWeightsFitPoses = 24;
    int fitWeightsTestPoses = 24;

    // geometry
    bool preserveTriangleOrder = false;
    float normal_blend = 0.0f;
    int clip_texcoords = 0;
    float gamma = 0.0f;
    int maxVertexLimit = MAXSTUDIOVERTS / 3;
    int maxVertexClamp = MAXSTUDIOVERTS / 3;

    // LODs
    int minLod = 0;
    int numAllowedRootLODs = 0;
    bool bSkinnedLODs = false;

    // Flex optimization (flexoptimize.cpp): mode and minimum static part of a mesh worth splitting off
    FlexOptimizeMode_t flexOptimizeMode = FLEXOPT_AUTO;
    int32_t flexOptimizeMinStatic = 256;

    // animations and sequences
    bool noAnimblockStall = false;
    bool animblockHighRes = false;
    bool animblockLowRes = false;
    bool zeroFramesHighres = false;
    bool lCaseAllSequences = false;
    bool errorOnSeqRemapFail = false;
    bool modelIntentionallyHasZeroSequences = false;
    float defaultMotionRollback = 0.3f;
    int minSectionFrameLimit = 30;
    int sectionFrames = 30;
    float preloadTime = 1.0f;
    int maxZeroFrames = 3; // clamped from 1..4
    float minZeroFramePosDelta = 2.0f;
    float defaultFadeInTime = 0.2f;
    float defaultFadeOutTime = 0.2f;
    Vector vecMinWorldspace{MIN_COORD_INTEGER, MIN_COORD_INTEGER, MIN_COORD_INTEGER};
    Vector vecMaxWorldspace{MAX_COORD_INTEGER, MAX_COORD_INTEGER, MAX_COORD_INTEGER};
};

// Paths: UTF-8 std::string (never FixedString, its byte truncation can cut a UTF-8 sequence)
struct StudioMdlPaths {
    std::string qcPath; // the QC as given on the command line, later its file name
    std::string fullpath; // absolute UTF-8 path of the QC
    std::string qdir; // absolute QC directory, native separators, trailing separator; sources are relative to it
    std::string gameDir; // absolute game directory (outputs go to <gameDir>/models/), trailing separator
    std::vector<std::string> contentBasePaths; // content roots that mirror the game search paths (CmdLib_AddBasePath)
    std::vector<std::string> contentRelativeDirs; // CmdLib_AddNewSearchPath: directories added below every root
    std::vector<std::string> contentOriginalBasePaths; // the roots before the first CmdLib_AddNewSearchPath

    // Directory stack (UTF-8, '/' terminated): [0] is the base ($cd, or "" = relative to qdir), $pushd/$popd push/pop on top.
    // Never empty: FindCachedSource and $pushd read back().
    std::vector<std::string> cddir{std::string()};
    bool cdset = false;
    bool contentRootRelative = false; // $contentrootrelative: source paths are relative to the content root

    StudioPath outname; // the output model name ($modelname): ASCII, relative to <game>/models/
    StudioPath internalName;
    bool hasModelName = false; // $modelname was given

    std::vector<std::string> CreateMakefileDependencies; // -makefile: every source file read, once
};

// QC parse state that applies to the commands after it
struct StudioMdlScriptState {
    float defaultscale = 0.0f;
    float currentscale = 0.0f;
    RadianEuler defaultrotation;
    Vector defaultadjust;
    StudioName rootname; // name of the root bone
    char *szInCurrentSeqName = nullptr;
};

// The source file being loaded (SMD/VTA/OBJ/DMX) and its unified geometry
struct StudioMdlInput {
    std::string szFilename; // source file being parsed
    std::unique_ptr<CStdFile> inputFile; // the open SMD/VTA/OBJ (OpenGlobalFile)
    vstd::FixedString<4096> szLine;
    int iLinecount = 0;

    // Raw geometry of the loaded sources; faces index into it. DMX and OBJ append, SMD and VRM write by index.
    vstd::AutoGrowVector<Vector> vertex;
    vstd::AutoGrowVector<s_boneweight_t> bone; // per vertex
    vstd::AutoGrowVector<Vector> normal;
    std::array<vstd::AutoGrowVector<Vector2D>, MAXSTUDIOTEXCOORDS> texcoord;

    std::vector<s_tmpface_t> faces;
    std::vector<s_face_t> src_uface; // max res unified faces

    // Unified vertices of the source being loaded: one per distinct (position, material, normal, texcoords), grown on demand
    std::vector<v_unify_t> v_listdata;
    // Per source position: v_listdata index of its first entry, -1 = none; entries chain through v_unify_t::next
    std::vector<int> v_list;
};

// Model wide values written to the .mdl header
struct StudioMdlHeader {
    uint32_t gflags = 0;
    uint32_t checksum = 0;
    Vector eyeposition;
    float flMaxEyeDeflection = 0.0f;
    Vector illumposition;
    int illumpositionattachment = 0;
    int illumpositionset = 0;
    uint8_t constdirectionalightdot = 0;
    std::array<Vector, 2> bbox{};
    std::array<Vector, 2> cbox{};
    bool wrotebbox = false;
    bool wrotecbox = false;
    bool bboxonlyverts = false;
    bool staticprop = false;
    bool centerstaticprop = false;
    float collisionPrecision = 0.0f; // $collisionprecision
    std::string KeyValueText;
};

struct StudioMdlMaterials {
    vstd::FixedVector<s_texture_t, MAXSTUDIOSKINS> textures;
    vstd::FixedVector<int, MAXSTUDIOSKINS> materials; // link into texture array
    vstd::FixedVector<char *, 16> cdtextures;

    // -t replacements: sourcetextures[i] is replaced by defaulttextures[i] (an empty source replaces every texture)
    vstd::FixedVector<StudioPath, 16> defaulttextures;
    vstd::FixedVector<StudioPath, 16> sourcetextures;

    // $texturegroup tables, [group][skin family][index] = material; only the first group is used
    vstd::FixedVector<std::vector<std::vector<int> >, 32> texturegroups;
    // [skin family][material] = material used by that family, one row per family, all as wide as the textures
    vstd::FixedVector<std::vector<int>, MAXSTUDIOSKINS> skinref;

    // alias materials created by the flex optimization as (alias material, base material)
    std::vector<std::pair<int, int> > flexAliasMaterials;

    vstd::FixedString<128> pDefaultSurfaceProp{"default"};
    std::vector<SurfacePropName_t> JointSurfaceProp;
};

struct StudioMdlBones {
    static constexpr uint32_t MAX_SRC_BONES = MAXSTUDIOSRCBONES;
    static constexpr uint32_t MAX_BONES = MAXSTUDIOBONES;
    
    vstd::FixedVector<s_bonetable_t, MAX_SRC_BONES> bonetable;

    vstd::FixedVector<s_renamebone_t, MAX_SRC_BONES> renamedbone;
    vstd::FixedVector<s_renamebone_t, MAX_SRC_BONES> szRenameBoneSubstr;
    vstd::FixedVector<StudioName, MAX_SRC_BONES> szStripBonePrefix;
    vstd::FixedVector<s_importbone_t, MAX_SRC_BONES> importbone;
    std::vector<std::string> collapse; // $alwayscollapse

    vstd::FixedVector<s_forcedhierarchy_t, MAX_BONES> forcedhierarchy;
    vstd::FixedVector<s_forcedrealign_t, MAX_BONES> forcedrealign;
    vstd::FixedVector<s_limitrotation_t, MAX_BONES> limitrotation;
    std::vector<s_bonesaveframe_t> bonesaveframe;
    std::vector<s_bonemerge_t> bone_merge;
    std::vector<s_alwayssetup_t> bone_always_setup;

    // procedural bones: every definition; after TagProceduralBones the maps hold the indices of the used ones
    vstd::FixedVector<s_jigglebone_t, MAX_BONES> jigglebones;
    vstd::FixedVector<int, MAX_BONES> jigglebonemap;
    vstd::FixedVector<s_axisinterpbone_t, MAX_BONES> axisinterpbones;
    vstd::FixedVector<int, MAX_BONES> axisinterpbonemap;
    vstd::FixedVector<s_quatinterpbone_t, MAX_BONES> quatinterpbones;
    vstd::FixedVector<int, MAX_BONES> quatinterpbonemap;
    vstd::FixedVector<s_aimatbone_t, MAX_BONES> aimatbones;
    vstd::FixedVector<int, MAX_BONES> aimatbonemap;
    std::vector<CTwistBone> twistbones;
    std::vector<CConstraintBoneBase *> constraintBones;
    vstd::FixedVector<s_screenalignedbone_t, MAX_SRC_BONES> screenalignedbone;
    vstd::FixedVector<s_worldalignedbone_t, MAX_SRC_BONES> worldalignedbone;

    vstd::FixedVector<s_bonecontroller_t, MAX_SRC_BONES> bonecontrollers;
    vstd::FixedVector<s_attachment_t, MAX_SRC_BONES> attachments;
    vstd::FixedVector<s_hitgroup_t, MAX_SRC_BONES> hitgroups;
    std::vector<s_hitboxset> hitboxsets;
};

struct StudioMdlAnimation {
    vstd::FixedVector<s_source_t *, MAXSTUDIOSEQUENCES> sources;
    vstd::FixedVector<s_animation_t *, MAXSTUDIOANIMS> animations;
    vstd::FixedVector<s_cmdlist_t, MAXSTUDIOANIMS> cmdlists;
    std::deque<s_sequence_t> sequence; // deque: appending never moves the (large) sequences parsing points into
    std::vector<std::string> AllowedActivityNames; // $allowactivityname

    vstd::FixedVector<s_poseparameter_t, MAXSTUDIOPOSEPARAM> pose;

    // Transition nodes: node id N (1-based, as stored in sequences) is xnodename[N - 1]; xnodename.size() is the node count.
    // xnode[from - 1][to - 1] is the next node on the way (0 = no transition); a fixed matrix indexed by node id.
    vstd::FixedVector<std::string, 100> xnodenames;
    std::array<std::array<int, 100>, 100> xnodes{};
    vstd::FixedVector<std::array<int, 2>, 10000> xnodeskips;

    int animblocksize = 0;
    StudioPath animblockname;
    int animblockmaxframes = 0;
    vstd::FixedVector<s_animblock_t, MAXSTUDIOANIMBLOCKS> animblocks;

    vstd::FixedVector<s_weightlist_t, MAXWEIGHTLISTS> weightlist; // [0] is the default weightlist

    vstd::FixedVector<s_ikchain_t, 16> ikchain;
    vstd::FixedVector<s_iklock_t, 16> ikautoplaylock;
};

struct StudioMdlFlex {
    vstd::FixedVector<s_flexdesc_t, MAXSTUDIOFLEXDESC> flexdescs;
    vstd::FixedVector<s_flexcontroller_t, MAXSTUDIOFLEXCTRL> flexcontrollers;
    vstd::FixedVector<s_flexkey_t, MAXSTUDIOFLEXKEYS> flexkeys;
    s_flexkey_t *defaultflexkey = nullptr;
    vstd::FixedVector<s_flexrule_t, MAXSTUDIOFLEXRULES> flexrules;
    std::vector<s_flexcontrollerremap_t> FlexControllerRemap;
    DmElementHandle_t hDmeBoneFlexDriverList = DMELEMENT_HANDLE_INVALID;

    vstd::AutoGrowVector<s_mouth_t> mouth; // $mouth <index>: written by index
};

// Models, bodyparts and LODs
struct StudioMdlBody {
    std::vector<s_model_t *> model;
    // model count when LoadLODSources ran (0 before); the models processed from then on
    int nummodelsbeforeLOD = 0;

    std::vector<s_bodypart_t> bodypart;
    std::vector<s_bodygrouppreset_t> bodygrouppresets;

    // Appends a bodypart; its base is the bodygroup value stride after the bodyparts before it
    s_bodypart_t &AddBodyPart() {
        const int base = bodypart.empty() ? 1 : bodypart.back().base * static_cast<int>(bodypart.back().pmodel.size());
        s_bodypart_t &part = bodypart.emplace_back();
        part.base = base;
        return part;
    }

    // Appends a model; returns its index
    int AddModel(s_model_t *pModel) {
        model.push_back(pModel);
        return static_cast<int>(model.size()) - 1;
    }

    vstd::FixedVector<s_includemodel_t, 128> includemodels;
    std::vector<LodScriptData_t> ScriptLODs; // [0] is the root LOD
};

struct StudioMdlState {
    StudioMdlOptions options;
    StudioMdlPaths paths;
    StudioMdlScriptState script;
    StudioMdlInput input;
    StudioMdlHeader header;
    StudioMdlMaterials materials;
    StudioMdlBones bones;
    StudioMdlAnimation anim;
    StudioMdlFlex flex;
    StudioMdlBody body;
};

extern StudioMdlState g_studiomdl;
