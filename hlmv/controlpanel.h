#pragma once

// Qt port of upstream/controlpanel.h: same tabs, control names and ids. Controls without a working
// backend yet are present and accept input but have no effect.

#include "hlmv_ids.h"
#include "mx.h"
#include "mdl_flex.h"

struct MdlInfo;
struct MdlMesh;
struct ViewportSettings;
class QOpenGLWindow;

class CBoneControlWindow : public mxWindow
{
public:
    explicit CBoneControlWindow(mxWindow *parent) : mxWindow(parent, 0, 0, 0, 0) {}
    void Init();
    void OnLoadModel(const MdlInfo &info);

private:
    mxChoice *m_cBone = nullptr;
    mxCheckBox *m_cBoneHighlight = nullptr;
    mxCheckBox *m_cBoneNames = nullptr;
    mxChoice *m_cSurfaceProp = nullptr;
    mxCheckBox *m_cAutoHitbox = nullptr;
    mxCheckBox *m_cShowDefaultPose = nullptr;
    mxChoice *m_cHitboxSet = nullptr;
    mxLineEdit *m_eHitboxSetName = nullptr;
    mxButton *m_bHitboxSetUpdateName = nullptr;
    mxButton *m_bAddHitboxSet = nullptr;
    mxButton *m_bDeleteHitboxSet = nullptr;
    mxChoice *m_cHitbox = nullptr;
    mxCheckBox *m_cHitboxHighlight = nullptr;
    mxLineEdit *m_eHitboxGroup = nullptr;
    mxLineEdit *m_eHitboxName = nullptr;
    mxChoice *m_cHitboxEditMode = nullptr;
    mxButton *m_bUpdateHitbox = nullptr;
    mxButton *m_bAddHitbox = nullptr;
    mxButton *m_bDeleteHitbox = nullptr;
};

class CAttachmentsWindow : public mxWindow
{
public:
    explicit CAttachmentsWindow(mxWindow *parent) : mxWindow(parent, 0, 0, 0, 0) {}
    void Init();
    void OnLoadModel(const MdlInfo &info);

private:
    mxListBox *m_cAttachmentList = nullptr;
    mxListBox *m_cBoneList = nullptr;
    mxLineEdit2 *m_cTranslation = nullptr;
    mxLineEdit2 *m_cRotation = nullptr;
    mxLineEdit2 *m_cQCString = nullptr;
};

class ControlPanel : public mxWindow
{
public:
    ControlPanel(mxWindow *parent, ViewportSettings &settings);
    int handleEvent(mxEvent *event) override;

    void setViewport(QOpenGLWindow *viewport) { m_viewport = viewport; }
    void OnLoadModel(const MdlMesh &mesh);
    void OnUnloadModel();

    // Accelerator targets from MDLViewer (upstream ControlPanel::setShow* family).
    void toggle(int id);

private:
    void SetupRenderWindow(mxTab *pTab);
    void SetupSequenceWindow(mxTab *pTab);
    void SetupBodyWindow(mxTab *pTab);
    void SetupFlexWindow(mxTab *pTab);
    void SetupPhysicsWindow(mxTab *pTab);
    void SetupSoftbodyWindow(mxTab *pTab);
    void SetupBoneControlWindow(mxTab *pTab);
    void SetupAttachmentsWindow(mxTab *pTab);
    void SetupIKRuleWindow(mxTab *pTab);
    void SetupEventWindow(mxTab *pTab);
    void SetupMatVarWindow(mxTab *pTab);
    void SetupSubmodelWindow(mxTab *pTab);
    void SetupCompileWindow(mxTab *pTab);
    void updateSequenceSizes(int tabWidth);
    void UpdateIKRuleWindow();
    void BuildIKRuleQCString();
    void BuildEventQCString();
    void SetFlexController(int index, float value); // value in controller units, like StudioModel::SetFlexController
    void UpdateFlexSliders();
    std::vector<FlexController> m_flexControllers;
    void redraw() override;

    ViewportSettings &m_settings;
    QOpenGLWindow *m_viewport = nullptr;
    mxTab *tab = nullptr;
    mxSlider *slForceFrame = nullptr;
    mxLabel *lForcedFrame = nullptr;

    // Render
    mxWindow *wRender = nullptr;
    mxChoice *cRenderMode = nullptr, *cbBoneWeightInspectIndex = nullptr, *cHighlightBone = nullptr;
    mxChoice *cDrawHitBoxSet = nullptr, *cDrawHitBoxNumber = nullptr, *cIncludedModels = nullptr;
    mxLabel *lblBoneWeightInspectValues = nullptr;
    mxCheckBox *cbGround = nullptr, *cbMovement = nullptr, *cbBackground = nullptr, *cbHitBoxes = nullptr;
    mxCheckBox *cbSequenceBoxes = nullptr, *cbShadow = nullptr, *cbSoftwareSkin = nullptr, *cbOverbright2 = nullptr;
    mxCheckBox *cbAttachments = nullptr, *cbBones = nullptr, *cbNormals = nullptr, *cbTangentFrame = nullptr;
    mxCheckBox *cbOverlayWireframe = nullptr, *cbSpecular = nullptr, *cbNormalMap = nullptr, *cbDisplacementMap = nullptr;
    mxCheckBox *cbRunIK = nullptr, *cbEnableHead = nullptr, *cbIllumPosition = nullptr, *cbPlaySounds = nullptr;
    mxCheckBox *cbShowOriginAxis = nullptr;
    mxSlider *leOriginAxisLength = nullptr;
    mxLineEdit *leFOV = nullptr;

    // Sequence
    mxChoice *cSequence[MAX_SEQUENCES] = {};
    mxSlider *slSequence[MAX_SEQUENCES] = {};
    mxRadioButton *rbFrameSelection[MAX_SEQUENCES] = {};
    mxLineEdit *leSequenceFilter[MAX_SEQUENCES] = {};
    mxLabel *laFilters = nullptr, *laGroundSpeed = nullptr, *laFPS = nullptr, *laBlendAmount = nullptr, *laBlendTime = nullptr;
    mxChoice *cPoseParameter[NUM_POSEPARAMETERS] = {};
    mxSlider *slPoseParameter[NUM_POSEPARAMETERS] = {};
    mxLineEdit *lePoseParameter[NUM_POSEPARAMETERS] = {};
    mxSlider *slSpeedScale = nullptr, *slBlendTime = nullptr;
    mxChoice *cActivityModifiers = nullptr;

    // Model
    mxChoice *cBodypart = nullptr, *cSubmodel = nullptr, *cBodyGroupPreset = nullptr, *cController = nullptr;
    mxSlider *slController = nullptr;
    mxLabel *lModelInfo1 = nullptr, *lModelInfo2 = nullptr, *lModelInfo3 = nullptr, *lModelInfo4 = nullptr, *lLODMetric = nullptr;
    mxChoice *cSkin = nullptr, *cMaterials = nullptr, *cLODChoice = nullptr;
    mxCheckBox *cbAutoLOD = nullptr;
    mxLineEdit *leLODSwitch = nullptr;
    mxListBox *cMessageList = nullptr, *cShaderUsed = nullptr;
    mxButton *m_bExploreToMaterial = nullptr, *m_bRandomizeWeaponModuleSlots = nullptr;

    // Flex
    mxChoice *cFlex[NUM_FLEX_SLIDERS] = {};
    mxSlider *slFlexScale[NUM_FLEX_SLIDERS] = {};

    // Physics
    mxLineEdit *leMass = nullptr;
    mxChoice *cPhysicsBone = nullptr;
    mxRadioButton *rbConstraintAxis[3] = {};
    mxSlider *slPhysicsFriction = nullptr, *slPhysicsConMin = nullptr, *slPhysicsConMax = nullptr, *slPhysicsConTest = nullptr;
    mxLabel *lPhysicsFriction = nullptr, *lPhysicsConMin = nullptr, *lPhysicsConMax = nullptr;
    mxCheckBox *cbLinked = nullptr;
    mxSlider *slPhysicsParamMassBias = nullptr, *slPhysicsParamInertia = nullptr, *slPhysicsParamDamping = nullptr, *slPhysicsParamRotDamping = nullptr;
    mxLabel *lPhysicsParamMassBias = nullptr, *lPhysicsParamInertia = nullptr, *lPhysicsParamDamping = nullptr, *lPhysicsParamRotDamping = nullptr;
    mxLabel *lPhysicsMaterial = nullptr;

    // Cloth
    mxLabel *leSoftbodyIterations = nullptr;
    mxSlider *slSoftbodyIterations = nullptr, *slSoftbodyWindYaw = nullptr, *slSoftbodyWindStrength = nullptr;
    mxSlider *slSoftbodySurfaceStretch = nullptr, *slSoftbodyThreadStretch = nullptr;
    mxChoice *cSoftbodyCtrl = nullptr;
    mxCheckBox *cbSoftbodySimulate = nullptr, *cbSoftbodyPolygons = nullptr, *cbSoftbodyEdges = nullptr;
    mxCheckBox *cbSoftbodyBases = nullptr, *cbSoftbodyWind = nullptr, *cbSoftbodyIndices = nullptr;
    mxRadioButton *rbSoftbodyAxis[3] = {};

    // Bones, attachments
    CBoneControlWindow *m_pBoneWindow = nullptr;
    CAttachmentsWindow *m_pAttachmentsWindow = nullptr;

    // IKRule
    mxChoice *cIKChain = nullptr, *cIKType = nullptr, *cIKTouch = nullptr, *cIKUsing = nullptr;
    mxLabel *lIKTouch = nullptr, *lIKAttachment = nullptr;
    mxLineEdit *leIKAttachment = nullptr;
    mxCheckBox *cbIKRangeToggle = nullptr, *cbIKContactToggle = nullptr;
    mxLineEdit2 *leIKRangeStart = nullptr, *leIKRangePeak = nullptr, *leIKRangeTail = nullptr, *leIKRangeEnd = nullptr;
    mxLineEdit2 *leIKContactFrame = nullptr, *leIKQCString = nullptr;

    // Events
    mxLineEdit2 *leEventSoundFrame = nullptr, *leEventQCString = nullptr;
    mxListBox *lbEventSoundName = nullptr, *lbEventHistory = nullptr;
    mxLabel *lEventSequence = nullptr;

    // Materials
    mxListBox *cMaterialList = nullptr, *cMaterialParamList = nullptr;
    mxLineEdit2 *leMaterialParamText = nullptr;
    mxLabel *lblMatrixRotation = nullptr, *lblMatrixScaleX = nullptr, *lblMatrixScaleY = nullptr;
    mxLabel *lblMatrixTranslateX = nullptr, *lblMatrixTranslateY = nullptr;
    mxSlider *slMaterialParamMatrixSliderRotation = nullptr, *slMaterialParamMatrixSliderScaleX = nullptr;
    mxSlider *slMaterialParamMatrixSliderScaleY = nullptr, *slMaterialParamMatrixSliderTranslateX = nullptr;
    mxSlider *slMaterialParamMatrixSliderTranslateY = nullptr, *slMaterialParamFloat = nullptr;
    mxButton *bMaterialParamColor = nullptr, *bMaterialParamLoad = nullptr, *bMaterialParamCopyToClipboard = nullptr;
    mxCheckBox *cbMaterialParamMultiEdit = nullptr;

    // Submodels
    mxButton *bSubmodelAdd = nullptr, *bSubmodelAddSteam = nullptr, *bSubmodelRemoveAll = nullptr, *bSubmodelRemoveSelected = nullptr;
    mxListBox *cSubmodelList = nullptr;
    mxChoice *cSubmodelAttachTo = nullptr, *cSubmodelLocalAttachOrigin = nullptr;

    // Compile
    mxListBox *cCompileRecentQCpaths = nullptr, *cCompileQCOutput = nullptr;
    mxButton *bCompileQCRemoveFromList = nullptr, *bCompileQCWhenSelected = nullptr, *bCompileQCCompile = nullptr;
    mxButton *bCompileQCLoadModel = nullptr, *bCompileQCShowCompileOutput = nullptr, *bCompileQCBrowseToQC = nullptr;
    mxLabel *lblFullQCPath = nullptr, *lblCompileWarningOrError = nullptr;
    bool bCompileSelectedToggle = false;
};
