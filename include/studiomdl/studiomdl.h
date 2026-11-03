//===== Copyright � 1996-2008, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

#pragma once
#include "studiomdl_constants.hpp"

#include <vector>
#include <deque>
#include <array>
#include <cstdio>
#include "tier0/basetypes.h"
#include "tier1/utlvector.h"
#include "tier1/utlsymbol.h"
#include "tier1/utlstring.h"
#include "mathlib/vector.h"
#include "studio.h"
#include "datamodel/dmelementhandle.h"
#include "worldsize.h"
#include "tier1_v2/fixed_string.hpp"
#include "tier1_v2/fixed_vector.hpp"

using StudioName = vstd::FixedString<MAXSTUDIONAME>;
using StudioPath = vstd::FixedString<MAX_PATH>;

struct LodScriptData_t;
struct s_flexkey_t;
struct s_flexcontroller_t;
struct s_flexcontrollerremap_t;
struct s_combinationrule_t;
struct s_combinationcontrol_t;

class CDmeVertexDeltaData;

class CDmeCombinationOperator;

int KeyValueTextSize(std::vector<char> *pKeyValue);

const char *KeyValueText(std::vector<char> *pKeyValue);

extern vec_t Q_rint(vec_t in);

extern void WriteModelFiles(void);

// --------------------------------------------------------------------

template<class T>
class CUtlVectorAuto : public CUtlVector<T> {
    // typedef CUtlVectorAuto< T, CUtlVector<T > > BaseClass;
public:
    T &operator[](int i);
};

template<typename T>
inline T &CUtlVectorAuto<T>::operator[](int i) {
    this->EnsureCount(i + 1);
    Assert(this->IsValidIndex(i));
    return this->Base()[i];
}

//-----------------------------------------------------------------------------
// Assigns a default surface property to the entire model
//-----------------------------------------------------------------------------
struct SurfacePropName_t {
    char m_pJointName[128];
    char m_pSurfaceProp[128];
};

//////////////////////////////////////////////////////////////////////////
// Purpose: contains settings specified in gameinfo.txt
//////////////////////////////////////////////////////////////////////////

struct GameInfo_t {
    bool bSupportsXBox360;
    bool bSupportsDX8;
};

extern struct GameInfo_t g_gameinfo;

// --------------------------------------------------------------------

struct s_trianglevert_t {
    int vertindex;
    int normindex; // index into normal array
    int s, t;
    float u, v;
};

struct s_boneweight_t {
    int numbones;
    std::array<int, MAXSTUDIOBONEWEIGHTS> bone;
    std::array<float, MAXSTUDIOBONEWEIGHTS> weight;
};

struct s_tmpface_t {
    int material{};
    uint32_t a, b, c, d;
    uint32_t na, nb, nc, nd;
    std::array<uint32_t, MAXSTUDIOTEXCOORDS> ta{0xFFFFFFFF};
    std::array<uint32_t, MAXSTUDIOTEXCOORDS> tb{0xFFFFFFFF};
    std::array<uint32_t, MAXSTUDIOTEXCOORDS> tc{0xFFFFFFFF};
    std::array<uint32_t, MAXSTUDIOTEXCOORDS> td{0xFFFFFFFF}; // d used by subd quads, otherwise 0xFFFFFFFF

    s_tmpface_t() {
        a = b = c = d = 0xFFFFFFFF;
        na = nb = nc = nd = 0xFFFFFFFF;
        for (int i = 0; i < MAXSTUDIOTEXCOORDS; ++i) { ta[i] = tb[i] = tc[i] = td[i] = 0xFFFFFFFF; }
    }
};

struct s_face_t {
    s_face_t() { a = b = c = d = 0xFFFFFFFF; }

    uint32_t a, b, c, d; // d used by subd quads
};

struct s_vertexinfo_t {
    int material;
    int mesh;
    Vector position;
    Vector normal;
    Vector4D tangentS;
    int numTexcoord;
    std::array<Vector2D, MAXSTUDIOTEXCOORDS> texcoord;
    s_boneweight_t boneweight;
};


//============================================================================

// dstudiobone_t bone[MAXSTUDIOBONES];
struct s_bonefixup_t {
    matrix3x4_t m;
};

struct s_bonetable_t {
    StudioName name; // bone name for symbolic links
    int parent; // parent bone
    bool split;
    int bonecontroller; // -1 == 0
    Vector pos; // default pos
    Vector posscale; // pos values scale
    RadianEuler rot; // default pos
    Vector rotscale; // rotation values scale
    int group; // hitgroup
    Vector bmin, bmax; // bounding box
    bool bPreDefined;
    matrix3x4_t rawLocalOriginal; // original transform of preDefined bone
    matrix3x4_t rawLocal;
    matrix3x4_t srcRealign;
    bool bPreAligned;
    matrix3x4_t boneToPose;
    uint32_t flags;
    int proceduralindex;
    int physicsBoneIndex;
    int surfacePropIndex;
    Quaternion qAlignment;
    bool bDontCollapse;
    Vector posrange;
};


extern int findGlobalBone(const char *name); // finds a named bone in the global bone table


struct s_renamebone_t {
    char from[MAXSTUDIONAME];
    char to[MAXSTUDIONAME];
};


const char *RenameBone(const char *pName); // returns new name if available, else return pName.


struct s_importbone_t {
    char name[MAXSTUDIONAME];
    char parent[MAXSTUDIONAME];
    matrix3x4_t rawLocal;
    bool bPreAligned;
    matrix3x4_t srcRealign;
    bool bUnlocked;
};


struct s_includemodel_t {
    StudioName name;
};


struct s_bbox_t {
    char name[MAXSTUDIONAME]; // bone name
    char hitboxname[MAXSTUDIONAME]; // hitbox name
    int bone;
    int group; // hitgroup
    int model;
    Vector bmin, bmax; // bounding box
    QAngle angOffsetOrientation;
    float flCapsuleRadius;
};

#define MAXSTUDIOHITBOXSETNAME 64

struct s_hitboxset {
    char hitboxsetname[MAXSTUDIOHITBOXSETNAME];

    int numhitboxes;

    std::array<s_bbox_t, MAXSTUDIOSRCBONES> hitbox;
};


struct s_hitgroup_t {
    int models;
    int group;
    StudioName name; // bone name
};


struct s_bonecontroller_t {
    StudioName name;
    int bone;
    uint32_t type;
    int inputfield;
    float start;
    float end;
};


struct s_screenalignedbone_t {
    char name[MAXSTUDIONAME];
    int flags;
};


struct s_worldalignedbone_t {
    StudioName name;
    int flags;
};


struct s_attachment_t {
    StudioName name;
    StudioName bonename;
    int bone;
    uint32_t type;
    int flags;
    matrix3x4_t local;

    bool operator==(const s_attachment_t &rhs) const;
};


#define IS_ABSOLUTE        0x0001
#define IS_RIGID        0x0002


struct s_bonemerge_t {
    StudioName bonename;
};


struct s_alwayssetup_t {
    StudioName bonename;
};


struct s_mouth_t {
    StudioName bonename;
    int bone;
    Vector forward;
    int flexdesc;
};

struct s_node_t {
    StudioName name;
    int parent;
};

struct s_bone_t {
    Vector pos;
    RadianEuler rot;
};

struct s_linearmove_t {
    int endframe; // frame when pos, rot is valid.
    int flags; // type of motion.  Only linear, linear accel, and linear decel is allowed
    float v0;
    float v1;
    Vector vector; // movement vector
    Vector pos; // final position
    RadianEuler rot; // final rotation
};


#define CMD_WEIGHTS    1
#define CMD_SUBTRACT 2
#define CMD_AO        3
#define CMD_MATCH    4
#define CMD_FIXUP    5
#define CMD_ANGLE    6
#define CMD_IKFIXUP    7
#define CMD_IKRULE    8
#define CMD_MOTION    9
#define CMD_REFMOTION    10
#define CMD_DERIVATIVE 11
#define    CMD_NOANIMATION 12
#define CMD_LINEARDELTA 13
#define CMD_SPLINEDELTA 14
#define CMD_COMPRESS 15
#define CMD_NUMFRAMES 16
#define CMD_COUNTERROTATE 17
#define CMD_SETBONE 18
#define CMD_WORLDSPACEBLEND 19
#define CMD_MATCHBLEND 20
#define CMD_LOCALHIERARCHY 21
#define CMD_FORCEBONEPOSROT 22
#define CMD_REVERSE 23
#define CMD_APPENDANIM 24
#define CMD_BONEDRIVER 25
#define CMD_NOANIM_KEEPDURATION 26

struct s_animation_t;
struct s_ikrule_t;


struct s_motion_t {
    int motiontype;
    int iStartFrame; // starting frame to apply motion over
    int iEndFrame; // end frame to apply motion over
    int iSrcFrame; // frame that matches the "reference" animation
    s_animation_t *pRefAnim; // animation to match
    int iRefFrame; // reference animation's frame to match
};


struct s_animcmd_t {
    int cmd;

    union {
        struct {
            int index;
        } weightlist;

        struct {
            s_animation_t *ref;
            int frame;
            int flags;
        } subtract;

        struct {
            s_animation_t *ref;
            int motiontype;
            int srcframe;
            int destframe;
            char *pBonename;
        } ao;

        struct {
            s_animation_t *ref;
            int srcframe;
            int destframe;
            int destpre;
            int destpost;
        } match;

        struct {
            s_animation_t *ref;
            int startframe;
            int loops;
        } world;

        struct {
            int start;
            int end;
        } fixuploop;

        struct {
            float angle;
        } angle;

        struct {
            s_ikrule_t *pRule;
        } ikfixup;

        struct {
            s_ikrule_t *pRule;
        } ikrule;

        struct {
            float scale;
        } derivative;

        struct {
            int flags;
        } linear;

        struct {
            int frames;
        } compress;

        struct {
            int frames;
        } numframes;

        struct {
            char *pBonename;
            bool bHasTarget;
            float targetAngle[3];
        } counterrotate;

        struct {
            char *pBonename;
            char *pParentname;
            int start;
            int peak;
            int tail;
            int end;
        } localhierarchy;

        struct {
            char *pBonename;
            bool bDoPos;
            float pos[3];
            bool bDoRot;
            float rot[3];
            bool bRotIsLocal;
        } forceboneposrot;

        struct {
            char *pBonename;
            int iAxis;
            float value;
            int start;
            int peak;
            int tail;
            int end;
            bool all;
        } bonedriver;

        struct {
            s_animation_t *ref;
        } appendanim;

        struct s_motion_t motion;
    } u;
};

struct s_streamdata_t {
    Vector pos;
    Quaternion q;
};


struct s_animationstream_t {
    // source animations
    int numerror;
    s_streamdata_t *pError;
    // compressed animations
    float scale[6];
    int numanim[6];
    mstudioanimvalue_t *anim[6];
};

struct s_ikrule_t {
    int chain;

    int index;
    int type;
    int slot;
    char bonename[MAXSTUDIONAME];
    char attachment[MAXSTUDIONAME];
    int bone;
    Vector pos;
    Quaternion q;
    float height;
    float floor;
    float radius;

    int start;
    int peak;
    int tail;
    int end;

    int contact;

    bool usesequence;
    bool usesource;

    int flags;

    s_animationstream_t errorData;
};

struct s_localhierarchy_t {
    int bone;
    int newparent;

    int start;
    int peak;
    int tail;
    int end;

    s_animationstream_t localData;
};


struct s_source_t;


struct s_compressed_t {
    int num[6];
    mstudioanimvalue_t *data[6];
};

struct s_animation_t {
    bool isImplied;
    bool isOverride;
    bool doesOverride;
    int index;
    StudioName name;
    StudioPath filename;

    /*
    int				animsubindex;

    // For sharing outside of current .mdl file
    bool			shared_group_checkvalidity;
    bool			shared_group_valid;
    char			shared_animgroup_file[ MAX_PATH ]; // share file name
    char			shared_animgroup_name[ MAXSTUDIONAME ]; // group name in share file
    int				shared_group_subindex;
    studioanimhdr_t *shared_group_header;
    */

    float fps;
    int startframe;
    int endframe;
    int flags;
    // animations processed (time shifted, linearized, and bone adjusted ) from source animations
    CUtlVectorAuto<s_bone_t *> sanim; // [MAXSTUDIOANIMFRAMES]; // [frame][bones];

    int motiontype;

    int fudgeloop;
    int looprestart; // new starting frame for looping animations
    float looprestartpercent;

    // piecewise linear motion
    int numpiecewisekeys;
    s_linearmove_t piecewisemove[MAXSTUDIOMOVEKEYS];

    // default adjustments
    Vector adjust;
    float scale; // ????
    RadianEuler rotation;

    s_source_t *source;
    StudioPath animationname;

    Vector bmin;
    Vector bmax;

    int numframes;

    // compressed animation data
    int numsections;
    int sectionframes;
    CUtlVectorAuto<CUtlVectorAuto<s_compressed_t> > anim;

    // int				weightlist;
    float weight[MAXSTUDIOSRCBONES];
    float posweight[MAXSTUDIOSRCBONES];

    vstd::FixedVector<s_animcmd_t, MAXSTUDIOCMDS> cmds;

    int numikrules;
    s_ikrule_t ikrule[MAXSTUDIOIKRULES];
    bool noAutoIK;

    int numlocalhierarchy;
    s_localhierarchy_t localhierarchy[MAXSTUDIOIKRULES];

    float motionrollback;

    bool disableAnimblocks; // no demand loading
    bool isFirstSectionLocal; // first block of a section isn't demand loaded
    int numNostallFrames; // number of frames to keep in memory (modulo segement size)

    int rootDriverIndex;
};


struct s_cmdlist_t {
    using AnimCmdVector = vstd::FixedVector<s_animcmd_t,MAXSTUDIOCMDS>;

    StudioName name;
    AnimCmdVector cmds;
};


struct s_iklock_t {
    char name[MAXSTUDIONAME];
    int chain;
    float flPosWeight;
    float flLocalQWeight;
};


struct s_animtag_t {
    int tag;
    float cycle;
    char tagname[MAXSTUDIONAME];
};

struct s_event_t {
    int event;
    int frame;
    char options[64];
    char eventname[MAXSTUDIONAME];
};

struct s_autolayer_t {
    char name[MAXSTUDIONAME];
    int sequence;
    int flags;
    int pose;
    float start;
    float peak;
    float tail;
    float end;
};

struct s_activitymodifier_t {
    int id;
    char name[64];
};

class s_sequence_t {
public:
    char name[MAXSTUDIONAME];
    char activityname[MAXSTUDIONAME]; // index into the string table, the name of this activity.

    int flags;
    // float			fps;
    // int				numframes;

    int activity;
    int actweight;

    int numanimtags;
    s_animtag_t animtags[MAXSTUDIOTAGS];

    int numevents;
    s_event_t event[MAXSTUDIOEVENTS];

    int numblends;
    int groupsize[2];
    CUtlVectorAuto<CUtlVectorAuto<s_animation_t *> > panim; // [MAXSTUDIOBLENDS][MAXSTUDIOBLENDS];

    int paramindex[2];
    float paramstart[2];
    float paramend[2];
    int paramattachment[2];
    int paramcontrol[2];
    CUtlVectorAuto<float> param0; // [MAXSTUDIOBLENDS];
    CUtlVectorAuto<float> param1; // [MAXSTUDIOBLENDS];
    s_animation_t *paramanim;
    s_animation_t *paramcompanim;
    s_animation_t *paramcenter;

    // Vector			automovepos[MAXSTUDIOANIMATIONS];
    // Vector			automoveangle[MAXSTUDIOANIMATIONS];

    int animindex;

    Vector bmin;
    Vector bmax;

    float fadeintime;
    float fadeouttime;

    int entrynode;
    int exitnode;
    int nodeflags;
    float entryphase;
    float exitphase;

    int numikrules;

    int numautolayers;
    s_autolayer_t autolayer[64];

    float weight[MAXSTUDIOSRCBONES];

    s_iklock_t iklock[64];
    int numiklocks;

    int cycleposeindex;

    std::vector<char> KeyValue;

    int numactivitymodifiers;
    s_activitymodifier_t activitymodifier[MAXSTUDIOACTIVITYMODIFIERS];

    int rootDriverIndex;
    char rootDriverBoneName[MAXSTUDIONAME];
};


struct s_animblock_t {
    int iStartAnim;
    int iEndAnim;
    uint8_t *start;
    uint8_t *end;
};


struct s_poseparameter_t {
    char name[MAXSTUDIONAME];
    float min;
    float max;
    int flags;
    float loop;
};


struct rgb_t {
    uint8_t r, g, b;
};

struct rgb2_t {
    float r, g, b, a;
};

// FIXME: what about texture overrides inline with loading models
enum TextureFlags_t {
    RELATIVE_TEXTURE_PATH_SPECIFIED = 0x1
};

struct s_texture_t {
    char name[MAX_PATH];
    int flags;
    int parent;
    int material;
    float width;
    float height;
    float dPdu;
    float dPdv;
};


struct s_mesh_t {
    uint32_t numvertices;
    uint32_t vertexoffset;

    int numfaces;
    int faceoffset;
};


struct s_vertanim_t {
    int vertex;
    float speed;
    float side;
    Vector pos;
    Vector normal;
    float wrinkle;
};

struct s_lodvertexinfo_t : public s_vertexinfo_t {
    int lodFlag;
};

// processed aggregate lod pools
struct s_loddata_t {
    int numvertices;
    s_lodvertexinfo_t *vertex;

    int numfaces;
    s_face_t *face;

    s_mesh_t mesh[MAXSTUDIOSKINS];

    // remaps verts from an lod's source mesh to this all-lod processed aggregate pool
    int *pMeshVertIndexMaps[MAX_NUM_LODS];
};

// Animations stored in raw off-disk source files.  Raw data should be not processed.
class s_sourceanim_t {
public:
    char animationname[MAX_PATH];
    int numframes;
    int startframe;
    int endframe;
    CUtlVectorAuto<s_bone_t *> rawanim;

    // vertex animation
    bool newStyleVertexAnimations; // new style doesn't store a base pose in vertex anim[0]
    int *vanim_mapcount; // local verts map to N target verts
    int **vanim_map; // local vertices to target vertices mapping list
    int *vanim_flag; // local vert does animate

    int numvanims[MAXSTUDIOANIMFRAMES];
    s_vertanim_t *vanim[MAXSTUDIOANIMFRAMES]; // [frame][vertex]
};

// raw off-disk source files.  Raw data should be not processed.
struct s_source_t {
    char filename[MAX_PATH];
    int version; // Version number from SMD file, otherwise 0
    bool isActiveModel;

    // local skeleton hierarchy
    int numbones;
    std::array<s_node_t, MAXSTUDIOSRCBONES> localBone;
    std::array<matrix3x4_t, MAXSTUDIOSRCBONES> boneToPose; // converts bone local data into initial pose data

    // bone remapping
    int boneflags[MAXSTUDIOSRCBONES]; // attachment, vertex, etc flags for this bone
    int boneref[MAXSTUDIOSRCBONES]; // flags for this and child bones
    int boneLocalToGlobal[MAXSTUDIOSRCBONES]; // bonemap : local bone to world bone mapping
    int boneGlobalToLocal[MAXSTUDIOSRCBONES]; // boneimap : world bone to local bone mapping

    int texmap[MAXSTUDIOSKINS * 4]; // map local MAX materials to unique textures

    // per material mesh
    int nummeshes;
    int meshindex[MAXSTUDIOSKINS] = {0}; // mesh to skin index
    s_mesh_t mesh[MAXSTUDIOSKINS] = {0};

    // vertices defined in "local" space (not remapped to global bones)
    int numvertices;
    s_vertexinfo_t *vertex;

    // vertices defined in "global" space (remapped to global bones)
    CUtlVector<s_vertexinfo_t> m_GlobalVertices;

    int numfaces;
    s_face_t *face; // vertex indexs per face

    // raw skeletal animation
    CUtlVector<s_sourceanim_t> m_Animations;

    // default adjustments
    Vector adjust;
    float scale; // ????
    RadianEuler rotation;


    // Flex keys stored in the source data
    bool bNoAutoDMXRules;
    CUtlVector<s_flexkey_t> m_FlexKeys;

    // Combination controls stored in the source data
    CUtlVector<s_combinationcontrol_t> m_CombinationControls;

    // Combination rules stored in the source data
    CUtlVector<s_combinationrule_t> m_CombinationRules;

    // Flexcontroller remaps
    CUtlVector<s_flexcontrollerremap_t> m_FlexControllerRemaps;

    // Attachment points stored in the SMD/DMX/etc. file
    CUtlVector<s_attachment_t> m_Attachments;

    // Information about how flex controller remaps map into flex rules
    int m_nKeyStartIndex; // The index at which the flex keys for this model start in the global list
    CUtlVector<int> m_rawIndexToRemapSourceIndex;
    CUtlVector<int> m_rawIndexToRemapLocalIndex;
    CUtlVector<int> m_leftRemapIndexToGlobalFlexControllIndex;
    CUtlVector<int> m_rightRemapIndexToGlobalFlexControllIndex;
};


struct s_eyeball_t {
    char name[MAXSTUDIONAME];
    int index;
    int bone;
    Vector org;
    float zoffset;
    float radius;
    Vector up;
    Vector forward;

    int mesh;
    float iris_scale;

    int upperlidflexdesc;
    int upperflexdesc[3];
    float uppertarget[3];

    int lowerlidflexdesc;
    int lowerflexdesc[3];
    float lowertarget[3];
};

struct s_model_t {
    char name[MAXSTUDIONAME];
    char filename[MAX_PATH];

    // needs local scaling and rotation paramaters
    s_source_t *source; // index into source table

    float scale; // UNUSED

    float boundingradius;

    Vector boundingbox[MAXSTUDIOSRCBONES][2];

    int numattachments;
    s_attachment_t attachment[32];

    int numeyeballs;
    s_eyeball_t eyeball[4];

    // References to sources which are the LODs for this model
    CUtlVector<s_source_t *> m_LodSources;

    // processed aggregate lod data
    s_loddata_t *m_pLodData;
};


struct s_flexdesc_t {
    StudioName FACS; // FACS identifier
};


int Add_Flexdesc(const char *name);


struct s_flexcontroller_t {
    StudioName name;
    StudioName type;
    float min;
    float max;
};


struct s_flexcontrollerremap_t {
    CUtlString m_Name;
    FlexControllerRemapType_t m_RemapType;
    bool m_bIsStereo;
    std::vector<CUtlString> m_RawControls;
    int m_Index; ///< The model relative index of the slider control for value for this if it's not split, -1 otherwise
    int m_LeftIndex; ///< The model relative index of the left slider control for this if it's split, -1 otherwise
    int m_RightIndex; ///< The model relative index of the right slider control for this if it's split, -1 otherwise
    int m_MultiIndex; ///< The model relative index of the value slider control for this if it's multi, -1 otherwise
    CUtlString m_EyesUpDownFlexName; // The name of the eyes up/down flex controller
    int m_EyesUpDownFlexController; // The global index of the Eyes Up/Down Flex Controller
    int m_BlinkController; // The global index of the Blink Up/Down Flex Controller
};

struct s_flexkey_t {
    int flexdesc;
    int flexpair;

    s_source_t *source; // index into source table
    StudioPath animationname;

    int imodel;
    int frame;

    float target0;
    float target1;
    float target2;
    float target3;

    float split;
    float decay;

    // extracted and remapped vertex animations
    int numvanims;
    s_vertanim_t *vanim;
    int vanimtype;
    int weighttable;
};


#define MAX_OPS 512

struct s_flexop_t {
    int op;

    union {
        int index;
        float value;
    } d;
};

struct s_flexrule_t {
    int flex;
    int numops;
    s_flexop_t op[MAX_OPS];
};


struct s_combinationcontrol_t {
    char name[MAX_PATH];
};

struct s_combinationrule_t {
    // The 'ints' here are indices into the m_Controls array
    CUtlVector<int> m_Combination;
    CUtlVector<CUtlVector<int> > m_Dominators;

    // The index into the flexkeys to put the result in
    // (should affect both left + right if the key is sided)
    int m_nFlex;
};


struct s_bodypart_t {
    char name[MAXSTUDIONAME];
    int nummodels;
    int base;
    CUtlVectorAuto<s_model_t *> pmodel;

    s_bodypart_t() {
        memset(this, 0, sizeof(s_bodypart_t));
    }
};


struct s_bodygrouppreset_t {
    char name[MAXSTUDIONAME];
    int iValue;
    int iMask;

    s_bodygrouppreset_t() {
        memset(this, 0, sizeof(s_bodygrouppreset_t));
    }
};


#define MAXWEIGHTLISTS    128
#define MAXWEIGHTSPERLIST    (MAXSTUDIOBONES)

struct s_weightlist_t {
    // weights, indexed by numbones per weightlist
    char name[MAXSTUDIONAME];
    int numbones;
    char *bonename[MAXWEIGHTSPERLIST];
    float boneweight[MAXWEIGHTSPERLIST];
    float boneposweight[MAXWEIGHTSPERLIST];

    // weights, indexed by global bone index
    float weight[MAXSTUDIOBONES];
    float posweight[MAXSTUDIOBONES];
};


struct s_iklink_t {
    int bone;
    Vector kneeDir;
};

struct s_ikchain_t {
    char name[MAXSTUDIONAME];
    char bonename[MAXSTUDIONAME];
    int axis;
    float value;
    int numlinks;
    s_iklink_t link[10]; // hip, knee, ankle, toes...
    float height;
    float radius;
    float floor;
    Vector center;
};


struct s_jigglebone_t {
    int flags;
    char bonename[MAXSTUDIONAME];
    int bone;

    mstudiojigglebone_t data; // the actual jiggle properties
};


struct s_axisinterpbone_t {
    int flags;
    char bonename[MAXSTUDIONAME];
    int bone;
    char controlname[MAXSTUDIONAME];
    int control;
    int axis;
    Vector pos[6];
    Quaternion quat[6];
};


struct s_quatinterpbone_t {
    int flags;
    char bonename[MAXSTUDIONAME];
    int bone;
    char parentname[MAXSTUDIONAME];
    // int				parent;
    char controlparentname[MAXSTUDIONAME];
    // int				controlparent;
    char controlname[MAXSTUDIONAME];
    int control;
    int numtriggers;
    Vector size;
    Vector basepos;
    float percentage;
    float tolerance[32];
    Quaternion trigger[32];
    Vector pos[32];
    Quaternion quat[32];
};


struct s_aimatbone_t {
    char bonename[MAXSTUDIONAME];
    int bone;
    char parentname[MAXSTUDIONAME];
    int parent;
    char aimname[MAXSTUDIONAME];
    int aimAttach;
    int aimBone;
    Vector aimvector;
    Vector upvector;
    Vector basepos;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct s_constraintbonetarget_t {
    char m_szBoneName[MAXSTUDIONAME];
    int m_nBone;
    float m_flWeight;
    Vector m_vOffset;
    Quaternion m_qOffset;

    bool operator==(const s_constraintbonetarget_t &rhs) const;

    bool operator!=(const s_constraintbonetarget_t &rhs) const { return !(*this == rhs); }
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct s_constraintboneslave_t {
    char m_szBoneName[MAXSTUDIONAME];
    int m_nBone;
    Vector m_vBaseTranslate;
    Quaternion m_qBaseRotation;

    bool operator==(const s_constraintboneslave_t &rhs) const;

    bool operator!=(const s_constraintboneslave_t &rhs) const { return !(*this == rhs); }
};

//-----------------------------------------------------------------------------
// Clamp meshes into N vertex sizes so as to not overrun
//-----------------------------------------------------------------------------
// TODO: It may be better to go ahead and create a new "source", since there's other limits besides just vertices per mesh, such as total verts per model.
class CClampedSource {
public:
    CClampedSource() : m_nummeshes(0) {
    };

    void Init(int numvertices);;

    // per material mesh
    int m_nummeshes;
    int m_meshindex[MAXSTUDIOSKINS]; // mesh to skin index
    s_mesh_t m_mesh[MAXSTUDIOSKINS];

    // vertices defined in "local" space (not remapped to global bones)
    CUtlVector<int> m_nOrigMap; // maps the original index to the new index
    CUtlVector<s_vertexinfo_t> m_vertex;
    CUtlVector<s_face_t> m_face;
    CUtlVector<s_sourceanim_t> m_Animations;

    int AddNewVert(s_source_t *pOrigSource, int nVert, int nSrcMesh, int nDstMesh, int nPreOffset = 0);

    void AddAnimations(const s_source_t *pOrigSource);

    void DestroyAnimations(s_source_t *pNewSource);

    void Copy(s_source_t *pDstSource);

    void CopyFlexKeys(const s_source_t *pOrigSource, s_source_t *pNewSource, int imodel);
};

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CTwistBone {
public:
    bool m_bInverse;
    Vector m_vUpVector;
    char m_szParentBoneName[MAXSTUDIONAME];
    int m_nParentBone;
    Quaternion m_qBaseRotation;
    char m_szChildBoneName[MAXSTUDIONAME];
    int m_nChildBone;

    CUtlVector<s_constraintbonetarget_t> m_twistBoneTargets;

    CTwistBone() {
        m_bInverse = false;
        m_vUpVector.Init();
        m_szParentBoneName[0] = '\0';
        m_nParentBone = -1;
        m_qBaseRotation.Init();
        m_szChildBoneName[0] = '\0';
        m_nChildBone = -1;
    }
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CConstraintBoneBase {
public:
    virtual ~CConstraintBoneBase() {
    }

    CUtlVector<s_constraintbonetarget_t> m_targets;
    s_constraintboneslave_t m_slave;

    bool operator==(const CConstraintBoneBase &rhs) const;

    bool operator!=(const CConstraintBoneBase &rhs) const { return !(*this == rhs); }
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CPointConstraint : public CConstraintBoneBase {
public:
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class COrientConstraint : public CConstraintBoneBase {
public:
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CAimConstraint : public CConstraintBoneBase {
public:
    CAimConstraint() {
        m_nUpSpaceTargetBone = -1;
    }

    Quaternion m_qAimOffset;
    Vector m_vUpVector;
    char m_szUpSpaceTargetBone[MAXSTUDIONAME];
    int m_nUpSpaceTargetBone;
    int m_nUpType; // CConstraintBones::AimConstraintUpType_t
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CParentConstraint : public CConstraintBoneBase {
public:
};


struct s_forcedhierarchy_t {
    char parentname[MAXSTUDIONAME];
    char childname[MAXSTUDIONAME];
    char subparentname[MAXSTUDIONAME];
};


struct s_forcedrealign_t {
    char name[MAXSTUDIONAME];
    RadianEuler rot;
};


struct s_limitrotation_t {
    char name[MAXSTUDIONAME];
    int numseq;
    char *sequencename[64];
};


extern int BuildTris(s_trianglevert_t (*x)[3], s_mesh_t *y, uint8_t **ppdata);


struct s_bonesaveframe_t {
    char name[MAXSTUDIOHITBOXSETNAME];
    bool bSavePos;
    bool bSaveRot;
    bool bSaveRot64;
};


int OpenGlobalFile(char *src);

bool GetGlobalFilePath(const char *pSrc, char *pFullPath, int nMaxLen);

s_source_t *Load_Source(const char *filename, const char *ext, bool reverse = false, bool isActiveModel = false,
                        bool bUseCache = true);

void ApplyOffsetToSrcVerts(s_source_t *pModel, matrix3x4_t matOffset);

void AddSrcToSrc(s_source_t *pOrigSource, s_source_t *pAppendSource, matrix3x4_t matOffset);

void AddSrcToSrc(s_source_t *pOrigSource, s_source_t *pAppendSource);

int Load_VRM(s_source_t *psource);

int Load_SMD(s_source_t *psource);

int Load_VTA(s_source_t *psource);

int Load_OBJ(s_source_t *psource);

int Load_DMX(s_source_t *psource);

int AppendVTAtoOBJ(s_source_t *psource, char *filename, int frame);

void Build_Reference(s_source_t *psource, const char *pAnimName);

int Grab_Nodes(std::array<s_node_t, MAXSTUDIOSRCBONES> &pnodes);

void Grab_Animation(s_source_t *psource, const char *pAnimName);

// Processes source comment line and extracts information about the data file
void ProcessSourceComment(s_source_t *psource, const char *pCommentString);

// Processes original content file "szOriginalContentFile" that was used to generate
// data file "szDataFile"
void ProcessOriginalContentFile(const char *szDataFile, const char *szOriginalContentFile);

//-----------------------------------------------------------------------------
// Utility methods to get or add animation data from sources
//-----------------------------------------------------------------------------
s_sourceanim_t *FindSourceAnim(s_source_t *pSource, const char *pAnimName);

const s_sourceanim_t *FindSourceAnim(const s_source_t *pSource, const char *pAnimName);

s_sourceanim_t *FindOrAddSourceAnim(s_source_t *pSource, const char *pAnimName);

// Adds flexkey data to a particular source
void AddFlexKey(s_source_t *pSource, CDmeCombinationOperator *pComboOp, const char *pFlexKeyName);

// Adds combination data to the source
void AddCombination(s_source_t *pSource, CDmeCombinationOperator *pCombination);

int LookupTexture(const char *pTextureName, bool bRelativePath = false);

int UseTextureAsMaterial(int textureindex);

int MaterialToTexture(int material);

int LookupAttachment(const char *name);

void ClearModel(void);

void SimplifyModel(void);

void CollapseBones(void);

void adjust_vertex(float *org);

void scale_vertex(Vector &org);

void clip_rotations(RadianEuler &rot);

void clip_rotations(Vector &rot);

char *stristr(const char *string, const char *string2);

#define strcpyn(a, b) strncpy( a, b, sizeof( a ) )

void CalcBoneTransforms(s_animation_t *panimation, int frame, matrix3x4_t *pBoneToWorld);

void CalcBoneTransforms(s_animation_t *panimation, s_animation_t *pbaseanimation, int frame, matrix3x4_t *pBoneToWorld);

void CalcBoneTransformsCycle(s_animation_t *panimation, s_animation_t *pbaseanimation, float flCycle,
                             matrix3x4_t *pBoneToWorld);

void
BuildRawTransforms(const s_source_t *psource, const char *pAnimationName, int frame, float scale, Vector const &shift,
                   RadianEuler const &rotate, int flags, matrix3x4_t *boneToWorld);

void BuildRawTransforms(const s_source_t *psource, const char *pAnimationName, int frame, matrix3x4_t *boneToWorld);

void TranslateAnimations(const s_source_t *pSource, const matrix3x4_t *pSrcBoneToWorld, matrix3x4_t *pDestBoneToWorld);

// Returns surface property for a given joint
char *GetSurfaceProp(const char *pJointName);

int GetContents(const char *pJointName);

char *GetDefaultSurfaceProp();

int GetDefaultContents();

// Did we read 'end'
bool IsEnd(const char *pLine);

// Parses an LOD command
void Cmd_LOD(const char *cmdname);

void Cmd_ShadowLOD(void);

// Fixes up the LOD source files
void FixupLODSources();

// Get model LOD source
s_source_t *GetModelLODSource(const char *pModelName,
                              const LodScriptData_t &scriptLOD, bool *pFound);


void LoadLODSources(void);

void ConvertBoneTreeCollapsesToReplaceBones(void);

void FixupReplacedBones(void);

void UnifyLODs(void);

void SpewBoneUsageStats(void);

void MarkParentBoneLODs(void);

//void CheckAutoShareAnimationGroup( const char *animation_name );

/*
=================
=================
*/

extern bool GetLineInput(void);


struct v_unify_t {
    int refcount;
    int lastref;
    int firstref;
    int v;
    int m;
    int n;
    int t[MAXSTUDIOTEXCOORDS];
    v_unify_t *next; // pointer to next entry with same v
};

int SortAndBalanceBones(int iCount, int iMaxCount, int bones[], float weights[]);

void Grab_Vertexanimation(s_source_t *psource, const char *pAnimationName);

extern void BuildIndividualMeshes(s_source_t *psource);

//-----------------------------------------------------------------------------
// A little class used to deal with replacement commands
//-----------------------------------------------------------------------------

class CLodScriptReplacement_t {
public:
    void SetSrcName(const char *pSrcName) {
        if (m_pSrcName) {
            delete[] m_pSrcName;
        }
        m_pSrcName = new char[strlen(pSrcName) + 1];
        strcpy(m_pSrcName, pSrcName);
    }

    void SetDstName(const char *pDstName) {
        if (m_pDstName) {
            delete[] m_pDstName;
        }
        m_pDstName = new char[strlen(pDstName) + 1];
        strcpy(m_pDstName, pDstName);
    }

    const char *GetSrcName() const {
        return m_pSrcName;
    }

    const char *GetDstName() const {
        return m_pDstName;
    }

    CLodScriptReplacement_t() {
        m_pSrcName = nullptr;
        m_pDstName = nullptr;
        m_pSource = nullptr;
    }

    ~CLodScriptReplacement_t() {
        delete[] m_pSrcName;
        delete[] m_pDstName;
    }

    s_source_t *m_pSource;

private:
    char *m_pSrcName;
    char *m_pDstName;
};


struct LodScriptData_t {
public:
    float switchValue;
    CUtlVector<CLodScriptReplacement_t> modelReplacements;
    CUtlVector<CLodScriptReplacement_t> boneReplacements;
    CUtlVector<CLodScriptReplacement_t> boneTreeCollapses;
    CUtlVector<CLodScriptReplacement_t> materialReplacements;
    CUtlVector<CLodScriptReplacement_t> meshRemovals;


    void EnableFacialAnimation(bool val) {
        m_bFacialAnimation = val;
    }

    bool GetFacialAnimationEnabled() const {
        return m_bFacialAnimation;
    }

    void StripFromModel(bool val) {
        m_bStrippedFromModel = val;
    }

    bool IsStrippedFromModel() const {
        return m_bStrippedFromModel;
    }

    LodScriptData_t() {
        m_bFacialAnimation = true;
        m_bStrippedFromModel = false;
    }

private:
    bool m_bFacialAnimation;
    bool m_bStrippedFromModel;
};


// the first time these are called, the name of the model/QC file is printed so that when
// running in batch mode, no echo, when dumping to a file, it can be determined which file is broke.
void MdlError(const char *pMsg, ...);

void MdlWarning(const char *pMsg, ...);

void CreateMakefile_AddDependency(const char *pFileName);

void EnsureDependencyFileCheckedIn(const char *pFileName);

void AddSurfaceProp(const char *pBoneName, const char *pSurfaceProperty);

char *FindSurfaceProp(const char *pBoneName);

bool ComparePath(const char *a, const char *b);

void SetDefaultSurfaceProp(const char *pSurfaceProperty);

void PostProcessSource(s_source_t *pSource, int imodel);

uint8_t IsByte(int val);

char IsChar(int val);

int IsInt24(int val);

short IsShort(int val);

unsigned short IsUShort(int val);

//-----------------------------------------------------------------------------
// Assigns a default contents to the entire model
//-----------------------------------------------------------------------------
struct ContentsName_t {
    char m_pJointName[128];
    int m_nContents;
};

extern int s_nDefaultContents; // in studiomdl.cpp
extern CUtlVector<ContentsName_t> s_JointContents; // in studiomdl.cpp

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
enum EyelidType_t {
    kLowerer = 0,
    kNeutral = 1,
    kRaiser = 2,
    kEyelidTypeCount = 3
};


//-----------------------------------------------------------------------------
// Used to point to the current s_model_t when loading QcModelElements from DMX
//-----------------------------------------------------------------------------
extern s_model_t *g_pCurrentModel;


int verify_atoi(const char *token);

float verify_atof(const char *token);

int LookupPoseParameter(const char *name);

void ProcessModelName(const char *pModelName);

enum FlexOptimizeMode_t : int {
    FLEXOPT_OFF,
    FLEXOPT_AUTO, // split flexed meshes only if the flex cache budget is exceeded
    FLEXOPT_ON,
};
