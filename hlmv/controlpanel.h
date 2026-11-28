#pragma once

// Qt port of upstream/controlpanel.h: same tabs, control names and ids. Controls without a working
// backend yet are present and accept input but have no effect.

#include "hlmv_ids.h"
#include "mx.h"
#include "mdl_mesh.h"
#include "material_system.h"
#include "mdl_jiggle.h"

struct ViewportSettings;
class StudioAnimation;
class QOpenGLWidget;
class QDoubleSpinBox;
class QFormLayout;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;

class CBoneControlWindow : public mxWindow
{
public:
    CBoneControlWindow(mxWindow *panel, ViewportSettings &settings)
        : mxWindow(panel, 0, 0, 0, 0), m_panel(panel), m_settings(settings) {}
    void Init();
    void OnLoadModel(const MdlMesh &mesh);
    int handleEvent(mxEvent *event) override;

private:
    void OnBoneSelected(int bone);
    void ComputeHitboxSetList(int select);
    void ComputeHitboxList();
    void OnHitboxSelected(int index);
    void OnAutogenerateHitboxes(bool checked);
    void UpdateHighlights();
    QString SerializeQC() const;
    std::vector<MdlHitbox> *CurrentSet();

    mxWindow *m_panel; // redraws the viewport
    ViewportSettings &m_settings;
    QStringList m_bones, m_surfaceProps; // surface props are edited here and only feed Generate QC
    std::vector<int> m_parents;
    int m_bone = 0;
    int m_hitbox = -1;                   // index in the current set, -1 = none
    std::vector<int> m_boneHitboxes;     // current set's hitboxes on m_bone, in m_cHitbox order
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

// Jiggle Bones tab: the loaded model's $jigglebone rules. Edits (including new rules on plain bones) apply to the
// viewer's copy of the model at once; Save rebuilds the .mdl and moves its new checksum into the .vvd/.vtx/.phy.
class CJiggleWindow : public mxWindow
{
public:
    CJiggleWindow(mxWindow *panel, ViewportSettings &settings) : mxWindow(panel, 0, 0, 0, 0), m_panel(panel), m_settings(settings) {}
    void Init();
    void OnLoadModel(const QString &path, const QStringList &bones);

private:
    int Selected() const;
    void Apply(int bone, const std::optional<JiggleParams> &rule); // to the preview and m_current
    void UpdateRows();
    void ShowRule();
    void Save();

    struct Field
    {
        QDoubleSpinBox *spin;
        float JiggleParams::*member;
        bool angle; // shown in degrees, stored in radians
    };
    mxWindow *m_panel; // redraws the viewport
    ViewportSettings &m_settings;
    QString m_path;
    QStringList m_bones;
    std::vector<JiggleState> m_states;
    std::vector<std::optional<JiggleParams>> m_saved, m_current, m_stash; // on disk, previewed, last removed
    std::vector<Field> m_fields;
    std::vector<std::pair<QGroupBox *, uint32_t>> m_groups;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QCheckBox *m_enabled = nullptr, *m_highlight = nullptr, *m_lengthFlex = nullptr;
    QWidget *m_editor = nullptr;
    QPushButton *m_revert = nullptr, *m_save = nullptr;
    QLabel *m_status = nullptr;
};

class ControlPanel : public mxWindow
{
public:
    ControlPanel(mxWindow *parent, ViewportSettings &settings);
    int handleEvent(mxEvent *event) override;

    void setViewport(QOpenGLWidget *viewport) { m_viewport = viewport; }
    void OnLoadModel(const MdlMesh &mesh, const QString &path);
    void OnUnloadModel();

    // Accelerator targets from MDLViewer (upstream ControlPanel::setShow* family).
    void toggle(int id);

    // Sequence tab, driven by MDLViewer's animation clock (upstream updateFrameSlider/updateGroundSpeed/updateTransitionAmount).
    void OnAnimationTick();
    // View menu: Show Activities / Show hidden / Show sequence numbers / Sort sequences.
    void setSequenceListOption(int id, bool on);

private:
    void SetupRenderWindow(mxTab *pTab);
    void SetupSequenceWindow(mxTab *pTab);
    void SetupBodyWindow(mxTab *pTab);
    void SetupFlexWindow(mxTab *pTab);
    void SetupPhysicsWindow(mxTab *pTab);
    void SetupSoftbodyWindow(mxTab *pTab);
    void SetupBoneControlWindow(mxTab *pTab);
    void SetupJiggleWindow(mxTab *pTab);
    void SetupAttachmentsWindow(mxTab *pTab);
    void SetupIKRuleWindow(mxTab *pTab);
    void SetupEventWindow(mxTab *pTab);
    void SetupMatVarWindow(mxTab *pTab);
    void SetupSubmodelWindow(mxTab *pTab);
    void SetupCompileWindow(mxTab *pTab);
    void UpdateIKRuleWindow();
    void BuildIKRuleQCString();
    void BuildEventQCString();
    void SetFlexController(size_t index, float value) const; // value in controller units, like StudioModel::SetFlexController
    void UpdateFlexSliders();
    void UpdateFlexPage();

    int32_t FlexIndex(int32_t slot) const; // controller shown by slider `slot` on the current page, or -1
    void SetBodypart(int part);
    void UpdateBodyInfo();
    std::vector<FlexController> m_flexControllers;
    MdlInfo m_info;          // loaded model's header data, for the Model tab
    int m_materialCount = 0;
    // Materials tab: the VMT each MDL texture reads, what the viewport reported when building it, and the edits.
    enum class ParamType { Text, Scalar, Bool, Vec2, Vec3, Color, Transform }; // Type choice order
    struct MaterialEntry
    {
        QString name;          // MDL texture name
        MaterialSource source; // vmt empty when sourceError is set
        QString sourceError, buildError;
        QStringList ignored; // parameters the shader did not read
    };
    std::vector<MaterialEntry> m_materials;
    QString m_modelPath;
    QStringList m_materialSearchPaths;
    QStringList m_paramKeys;                   // cMaterialParamList rows, lower-case
    std::map<QString, ParamType> m_paramTypes; // Type choice overrides by lower-case name
    int SelectedMaterial() const;
    std::vector<int> TargetMaterials() const; // the selected material, or all with "Affect all loaded materials"
    QString SelectedParam() const;
    std::optional<QString> MaterialParamValue(int material, const QString &key) const; // edited, else the VMT's
    ParamType MaterialParamType(const QString &key) const;
    void SetMaterialParam(const std::vector<int> &materials, const QString &key, const std::optional<QString> &value);
    void ReadMaterialSources();
    void FillMaterialParams(const QString &select = {});
    void UpdateMaterialParamRows();
    void ShowMaterialParamEditor();
    void SyncMaterials();
    // Sequence tab (upstream ControlPanel sequence helpers)
    StudioAnimation *anim() const;
    QString ComposeSequenceDisplayName(int64_t sequence) const;
    void initSequenceChoices(int onlySlot = -1);
    void initBoneControllers();

    int64_t GetSequenceForSelection(int64_t slot, int64_t selection) const;

    int64_t GetSelectionForSequence(int64_t slot, int64_t sequence) const;
    void setSequence(int sequence);
    void setOverlaySequence(int num, int sequence, float weight);
    void showActivityModifiers(int sequence);
    int getFrameSelection() const;
    void setFrame(float frame);
    void updateFrameSelection();
    void updateFrameSlider();
    void setSpeedScale(float scale);
    void updateSpeedScale();
    void setBlend(int index, float value);
    void updateGroundSpeed();
    void SaveSelectedSequences();
    void RestoreSelectedSequences();
    std::vector<int> m_selectionToSequence[MAX_SEQUENCES], m_sequenceToSelection[MAX_SEQUENCES];
    int m_savedSequences[MAX_SEQUENCES] = {};
    float m_savedWeights[MAX_SEQUENCES] = {};
    bool m_showActivities = false, m_showHidden = false, m_showSequenceIndices = false, m_sortSequences = false;
    void redraw() override;

    ViewportSettings &m_settings;
    QOpenGLWidget *m_viewport = nullptr;
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
    mxLabel *laGroundSpeed = nullptr, *laFPS = nullptr, *laBlendAmount = nullptr, *laBlendTime = nullptr;
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

    // Flex: pages of NUM_FLEX_SLIDERS controllers instead of upstream's per-slider controller dropdowns
    mxLabel *lFlex[NUM_FLEX_SLIDERS] = {};
    mxSlider *slFlexScale[NUM_FLEX_SLIDERS] = {};
    mxLabel *lFlexPage = nullptr;
    mxButton *bFlexPrev = nullptr, *bFlexNext = nullptr;
    int m_flexPage = 0;

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
    CJiggleWindow *m_pJiggleWindow = nullptr;

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
    mxCheckBox *cbMaterialParamMultiEdit = nullptr, *cbMaterialParamBool = nullptr;
    mxChoice *cMaterialParamType = nullptr, *cMaterialNewParamType = nullptr;
    mxLineEdit *leMaterialParamName = nullptr;
    mxButton *bMaterialParamAdd = nullptr, *bMaterialParamRemove = nullptr, *bMaterialRevert = nullptr, *bMaterialSync = nullptr;
    mxLabel *lMaterialParamType = nullptr, *lMaterialStatus = nullptr;
    QFormLayout *m_materialParamForm = nullptr;
    QWidget *wMaterialParamComponents = nullptr;   // component spin boxes (+ colour picker) of Vec2/Vec3/Color
    QDoubleSpinBox *sbMaterialParamComponents[3] = {};
    QLineEdit *leMaterialParamFilter = nullptr;

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
