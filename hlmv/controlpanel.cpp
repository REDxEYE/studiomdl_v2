// Qt port of upstream/controlpanel.cpp and upstream/attachments_window.cpp. The Setup* bodies are kept
// line-for-line with the original so layout and ids match; behaviour is wired as the renderer gains it.

#include "controlpanel.h"
#include "studio_anim.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include <QClipboard>
#include <QGuiApplication>
#include <QOpenGLWindow>
#include <QRandomGenerator>

#include "mdl_mesh.h"
#include "viewport.h"

namespace
{
// upstream ReadPhysicsMaterials without vphysics: surface props are not loaded yet.
void ReadPhysicsMaterials(mxChoice *plist)
{
    plist->removeAll();
    plist->add("default");
    plist->select(0);
}

bool PopulateBoneList(mxChoice *pChoice, const QStringList &bones, bool bAlwaysAddNone = false)
{
    pChoice->removeAll();
    if (bAlwaysAddNone)
        pChoice->add("(None)");
    for (const auto &bone : bones)
        pChoice->add(bone.toUtf8().constData());
    if (bones.isEmpty() && !bAlwaysAddNone)
        pChoice->add("(None)");
    pChoice->select(0);
    return !bones.isEmpty();
}

void Fill(mxListBox *list, const QStringList &items, const char *empty)
{
    list->removeAll();
    if (items.isEmpty())
        list->add(empty);
    for (const auto &item : items)
        list->add(item.toUtf8().constData());
}
} // namespace

ControlPanel::ControlPanel(mxWindow *parent, ViewportSettings &settings)
    : mxWindow(parent, 0, 0, 0, 0, "Control Panel", mxWindow::Normal), m_settings(settings)
{
    // Win32 HLMV uses the 8 pt dialog font; the absolute layout below assumes text of that size.
    QFont small = font();
    small.setPixelSize(11);
    setFont(small);

    // frame slider
    new mxLabel(this, 5, 220, 50, 18, "Frame");
    slForceFrame = new mxSlider(this, 55, 220, 450, 18, IDC_FORCEFRAME);
    slForceFrame->setRange(0, 1.0);
    slForceFrame->setValue(0.0);
    slForceFrame->setSteps(1, 1);
    mxToolTip::add(slForceFrame, "Force To Frame");
    lForcedFrame = new mxLabel(this, 505, 220, 30, 18, "0");

    // create tabcontrol with subdialog windows
    tab = new mxTab(this, 0, 20, 0, 0, IDC_TAB);

    SetupRenderWindow(tab);
    SetupSequenceWindow(tab);
    SetupBodyWindow(tab);
    SetupFlexWindow(tab);
    SetupPhysicsWindow(tab);
    SetupSoftbodyWindow(tab);
    SetupBoneControlWindow(tab);
    SetupAttachmentsWindow(tab);
    SetupIKRuleWindow(tab);
    SetupEventWindow(tab);
    SetupMatVarWindow(tab);
    SetupSubmodelWindow(tab);
    SetupCompileWindow(tab);
}

void ControlPanel::SetupRenderWindow(mxTab *pTab)
{
    wRender = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wRender, "Render");
    cRenderMode = new mxChoice(wRender, 5, 2, 100, 22, IDC_RENDERMODE);
    cRenderMode->add("Wireframe");
    cRenderMode->add("Smoothshaded");
    cRenderMode->add("Textured");
    cRenderMode->add("BoneWeights");
    cRenderMode->add("BadVertexData");
    cRenderMode->add("UV Chart");
    cRenderMode->add("Co-LocatedVerts");
    cRenderMode->select(2);
    mxToolTip::add(cRenderMode, "Select Render Mode");

    new mxLabel(wRender, 450, 103, 200, 18, "Inspect bone weights:");
    cbBoneWeightInspectIndex = new mxChoice(wRender, 450, 120, 80, 20, IDC_BONEWEIGHTINDEX);
    lblBoneWeightInspectValues = new mxLabel(wRender, 530, 120, 600, 20, "");
    for (int i = 0; i < 32766; i++)
        cbBoneWeightInspectIndex->add(std::to_string(i).c_str());

    cbGround = new mxCheckBox(wRender, 125, 5, 150, 20, "Ground (Ctrl-G)", IDC_GROUND);
    cbMovement = new mxCheckBox(wRender, 125, 25, 150, 20, "Movement (Ctrl-M)", IDC_MOVEMENT);
    cbBackground = new mxCheckBox(wRender, 125, 45, 150, 20, "Background (Ctrl-B)", IDC_BACKGROUND);
    cbHitBoxes = new mxCheckBox(wRender, 125, 65, 150, 20, "Hit Boxes (Ctrl-H)", IDC_HITBOXES);
    cbSequenceBoxes = new mxCheckBox(wRender, 125, 85, 150, 20, "Seq. Boxes", IDC_SEQUENCEBOXES);
    cbShadow = new mxCheckBox(wRender, 125, 105, 150, 20, "Shadow (Ctrl-S)", IDC_SHADOW);
    cbSoftwareSkin = new mxCheckBox(wRender, 125, 125, 150, 20, "Software Skin", IDC_SOFTWARESKIN);
    cbOverbright2 = new mxCheckBox(wRender, 125, 145, 150, 20, "Enable Overbrightening", IDC_OVERBRIGHT2);
    cbOverbright2->setChecked(true);

    cbAttachments = new mxCheckBox(wRender, 5, 45, 120, 20, "Attachments (Ctrl-A)", IDC_ATTACHMENTS);
    cbBones = new mxCheckBox(wRender, 5, 65, 120, 20, "Bones (Ctrl-O)", IDC_BONES);
    cbNormals = new mxCheckBox(wRender, 5, 85, 120, 20, "Normals (Ctrl-N)", IDC_NORMALS);
    cbTangentFrame = new mxCheckBox(wRender, 5, 105, 120, 20, "Tangents (Ctrl-T)", IDC_TANGENTFRAME);
    cbOverlayWireframe = new mxCheckBox(wRender, 5, 125, 120, 20, "Wireframe (Ctrl-W)", IDC_OVERLAY_WIREFRAME);
    cbSpecular = new mxCheckBox(wRender, 5, 145, 120, 20, "Specular", IDC_SPECULAR);
    cbSpecular->setChecked(true);
    cbNormalMap = new mxCheckBox(wRender, 5, 25, 100, 20, "Normal Mapping", IDC_NORMALMAP);
    cbDisplacementMap = new mxCheckBox(wRender, 275, 45, 150, 20, "Displacement (Ctrl-D)", IDC_DISPLACEMENTMAP);
    cbDisplacementMap->setChecked(true);

    cbRunIK = new mxCheckBox(wRender, 275, 65, 150, 20, "Enable IK", IDC_RUNIK);
    cbEnableHead = new mxCheckBox(wRender, 275, 85, 150, 20, "Head Turn", IDC_HEADTURN);
    cbIllumPosition = new mxCheckBox(wRender, 275, 105, 150, 20, "Illum. Position", IDC_ILLUMPOSITION);
    cbPlaySounds = new mxCheckBox(wRender, 275, 125, 150, 20, "Play Sounds", IDC_PLAYSOUNDS);
    cbShowOriginAxis = new mxCheckBox(wRender, 275, 145, 150, 20, "Show Origin Axis", IDC_SHOWORIGINAXIS);

    new mxLabel(wRender, 275, 170, 45, 18, "Axis Len:");
    leOriginAxisLength = new mxSlider(wRender, 320, 165, 105, 22, IDC_ORIGINAXISLENGTH);
    leOriginAxisLength->setRange(1, 100);
    leOriginAxisLength->setValue(10);

    new mxCheckBox(wRender, 275, 5, 150, 20, "Physics Model", IDC_PHYSICSMODEL);
    cHighlightBone = new mxChoice(wRender, 275, 25, 150, 22, IDC_PHYSICSHIGHLIGHT);
    cHighlightBone->add("None");
    cHighlightBone->select(0);
    mxToolTip::add(cHighlightBone, "Select Physics Bone to highlight");

    new mxLabel(wRender, 450, 29, 60, 18, "HitBox Set:");
    cDrawHitBoxSet = new mxChoice(wRender, 510, 25, 90, 22, IDC_DRAWHITBOXSET);
    cDrawHitBoxSet->add("All");
    cDrawHitBoxSet->select(0);
    cDrawHitBoxSet->setEnabled(false);

    new mxLabel(wRender, 450, 48, 60, 18, "HitBox:");
    cDrawHitBoxNumber = new mxChoice(wRender, 510, 48, 90, 22, IDC_DRAWHITBOXNUMBER);
    cDrawHitBoxNumber->setEnabled(false);

    new mxLabel(wRender, 5, 170, 30, 18, "FOV:");
    leFOV = new mxLineEdit(wRender, 35, 165, 30, 22, "65", IDC_RENDER_FOV);

    cIncludedModels = new mxChoice(wRender, 450, 68, 250, 20, IDC_INCLUDEDMODELS);
    cIncludedModels->add("Included Models");
    cIncludedModels->add("---------------");
    cIncludedModels->setEnabled(false);
}

void ControlPanel::updateSequenceSizes(int tabWidth)
{
    int extraWidth = (tabWidth - 640);
    int extraPoseParamWidth = 0;
    if (extraWidth > 200)
    {
        extraPoseParamWidth = extraWidth - 200;
        extraWidth = 200;
    }
    if (extraWidth < 0)
        extraWidth = 0;
    extraPoseParamWidth = std::clamp(extraPoseParamWidth, 0, 200);

    for (int i = 0; i < MAX_SEQUENCES; i++)
    {
        cSequence[i]->setBounds(5, 5 + i * 22, 200 + extraWidth, 22);
        slSequence[i]->setBounds(208 + extraWidth, 5 + i * 22, 80, 18);
        rbFrameSelection[i]->setBounds(300 + extraWidth, 5 + i * 22, 35, 22);
        leSequenceFilter[i]->setBounds(520 + extraWidth + 96 + extraPoseParamWidth + 90, 5 + i * 22, 100, 18);
    }
    laFilters->setBounds(520 + extraWidth + 96 + extraPoseParamWidth + 5, 5, 80, 18);
    laGroundSpeed->setBounds(208 + extraWidth, 5, 80, 18);

    for (int i = 0; i < NUM_POSEPARAMETERS; i++)
    {
        const int x = 334, y = 2 + (i % 8) * 17;
        cPoseParameter[i]->setBounds(520 + extraWidth, y, 96 + extraPoseParamWidth, 22);
        slPoseParameter[i]->setBounds(x + extraWidth, y, 140, 16);
        lePoseParameter[i]->setBounds(x + 146 + extraWidth, y, 40, 16);
    }
}

void ControlPanel::SetupSequenceWindow(mxTab *pTab)
{
    mxWindow *wSequence = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wSequence, "Sequence");

    for (int i = 0; i < MAX_SEQUENCES; i++)
    {
        cSequence[i] = new mxChoice(wSequence, 5, 5 + i * 22, 200, 22, IDC_SEQUENCE0 + i);
        mxToolTip::add(cSequence[i], "Select Sequence");
        slSequence[i] = new mxSlider(wSequence, 208, 5 + i * 22, 80, 18, IDC_SEQUENCESCALE0 + i);
        slSequence[i]->setRange(0, 1.0, 100);
        slSequence[i]->setValue(0.0);
        rbFrameSelection[i] = new mxRadioButton(wSequence, 300, 5 + i * 22, 35, 22, "", IDC_FRAMESELECTION0 + i, i == 0);
        leSequenceFilter[i] = new mxLineEdit(wSequence, 0, 0, 0, 0, "", IDC_SEQUENCEFILTER0 + i);
    }
    rbFrameSelection[0]->setChecked(true);

    laFilters = new mxLabel(wSequence, 0, 0, 0, 0, "Sequence Filters");
    slSequence[0]->setVisible(false);
    laGroundSpeed = new mxLabel(wSequence, 208, 5, 80, 18, "");

    for (int i = 0; i < NUM_POSEPARAMETERS; i++)
    {
        const int x = 334, y = 2 + (i % 8) * 17;
        cPoseParameter[i] = new mxChoice(wSequence, 520, y, 96, 22, IDC_POSEPARAMETER + i);
        cPoseParameter[i]->setVisible(false);
        slPoseParameter[i] = new mxSlider(wSequence, x, y, 140, 16, IDC_POSEPARAMETER_SCALE + i);
        slPoseParameter[i]->setRange(0.0, 1.0, 1000);
        mxToolTip::add(slPoseParameter[i], "Parameter");
        slPoseParameter[i]->setVisible(false);
        lePoseParameter[i] = new mxLineEdit(wSequence, x + 146, y, 40, 16, "X", IDC_POSEPARAMETER_VALUE + i);
        lePoseParameter[i]->setVisible(false);
    }

    slSpeedScale = new mxSlider(wSequence, 5, 115, 200, 18, IDC_SPEEDSCALE);
    slSpeedScale->setRange(0, 1.0, 100);
    slSpeedScale->setValue(1.0);
    mxToolTip::add(slSpeedScale, "Speed Scale");
    laFPS = new mxLabel(wSequence, 208, 115, 128, 22, "");

    new mxCheckBox(wSequence, 5, 142, 150, 20, "Blend Sequence Changes", IDC_BLENDSEQUENCECHANGES);
    new mxButton(wSequence, 155, 142, 80, 20, "Blend Now", IDC_BLENDNOW);
    laBlendAmount = new mxLabel(wSequence, 240, 142, 60, 20, "");

    slBlendTime = new mxSlider(wSequence, 308, 142, 200, 18, IDC_BLENDTIME);
    slBlendTime->setRange(0, 1.0, 100);
    slBlendTime->setValue(DEFAULT_BLEND_TIME);
    laBlendTime = new mxLabel(wSequence, 540, 142, 80, 22, "");

    new mxLabel(wSequence, 5, 170, 90, 18, "Activity modifiers:");
    cActivityModifiers = new mxChoice(wSequence, 105, 166, 350, 22, IDC_ACTIVITY_MODIFIERS);
    new mxCheckBox(wSequence, 460, 166, 350, 22, "Animate weapons", IDC_ANIMATEWEAPONS);
}

void ControlPanel::SetupBodyWindow(mxTab *pTab)
{
    mxWindow *wBody = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wBody, "Model");
    cBodypart = new mxChoice(wBody, 5, 5, 100, 22, IDC_BODYPART);
    mxToolTip::add(cBodypart, "Choose a bodypart");
    cSubmodel = new mxChoice(wBody, 110, 5, 100, 22, IDC_SUBMODEL);
    mxToolTip::add(cSubmodel, "Choose a submodel of current bodypart");

    cBodyGroupPreset = new mxChoice(wBody, 110, 55, 100, 22, IDC_BODYGROUPPRESET);
    mxToolTip::add(cBodyGroupPreset, "Choose a bodygroup preset");

    cController = new mxChoice(wBody, 5, 30, 100, 22, IDC_CONTROLLER);
    mxToolTip::add(cController, "Choose a bone controller");
    slController = new mxSlider(wBody, 105, 32, 100, 18, IDC_CONTROLLERVALUE);
    slController->setRange(0, 255);
    mxToolTip::add(slController, "Change current bone controller value");
    lModelInfo1 = new mxLabel(wBody, 220, 5, 120, 80, "No Model.");
    lModelInfo2 = new mxLabel(wBody, 340, 5, 120, 130, "");
    cSkin = new mxChoice(wBody, 5, 55, 100, 22, IDC_SKINS);
    mxToolTip::add(cSkin, "Choose a skin family");
    new mxLabel(wBody, 5, 170, 90, 18, "Materials used:");
    cMaterials = new mxChoice(wBody, 105, 166, 350, 22, IDC_MATERIALS);
    mxToolTip::add(cMaterials, "Select material for UV Chart view");

    lModelInfo3 = new mxLabel(wBody, 220, 100, 120, 22, "");
    lModelInfo4 = new mxLabel(wBody, 220, 118, 120, 22, "");

    cbAutoLOD = new mxCheckBox(wBody, 5, 80, 100, 20, "Auto LOD", IDC_AUTOLOD);
    cLODChoice = new mxChoice(wBody, 5, 101, 100, 22, IDC_LODCHOICE);
    mxToolTip::add(cLODChoice, "Select model LOD to render");
    new mxLabel(wBody, 5, 126, 60, 18, "LOD Switch:");
    leLODSwitch = new mxLineEdit(wBody, 70, 126, 35, 22, "", IDC_LODSWITCH);
    new mxLabel(wBody, 5, 151, 60, 18, "LOD Metric:");
    lLODMetric = new mxLabel(wBody, 70, 151, 35, 22, "");

    new mxLabel(wBody, 505, 5, 100, 18, "VMTs Loaded:");
    cMessageList = new mxListBox(wBody, 500, 25, 540, 160, IDC_MESSAGES);
    cMessageList->add("None");
    cMessageList->select(1);
    mxToolTip::add(cMessageList, "Materials (VMT files) this model has loaded");

    m_bExploreToMaterial = new mxButton(wBody, 500, 172, 100, 20, "Explore to material", IDC_EXPLORE_TO_VMT);

    new mxLabel(wBody, 785, 5, 100, 18, "Shader:");
    cShaderUsed = new mxListBox(wBody, 830, 3, 210, 28, IDC_SHADERS);
    cShaderUsed->add("Select material to show shader");
    cShaderUsed->select(0);
    mxToolTip::add(cShaderUsed, "Shader Used");

    m_bRandomizeWeaponModuleSlots = new mxButton(wBody, 390, 140, 100, 20, "Roll bodygroups", IDC_ROLL_BODYGROUPS);
    mxToolTip::add(m_bRandomizeWeaponModuleSlots, "Randomize model bodygroup selections");
}

#define FLEX_DROPDOWN_WIDTH 90
#define FLEX_SLIDER_WIDTH 100
#define FLEX_ROWS_OF_SLIDERS 8

void ControlPanel::SetupFlexWindow(mxTab *pTab)
{
    mxWindow *wFlex = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wFlex, "Flex");
    for (int i = 0; i < NUM_FLEX_SLIDERS; i++)
    {
        const int w = (i / FLEX_ROWS_OF_SLIDERS) * (FLEX_SLIDER_WIDTH + FLEX_DROPDOWN_WIDTH + 4) + 5;
        const int h = (i % FLEX_ROWS_OF_SLIDERS) * 20 + 5;
        cFlex[i] = new mxChoice(wFlex, w, h, FLEX_DROPDOWN_WIDTH, 22, IDC_FLEX + i);
        mxToolTip::add(cFlex[i], "Select Flex");
        slFlexScale[i] = new mxSlider(wFlex, w + FLEX_DROPDOWN_WIDTH, h, FLEX_SLIDER_WIDTH, 18, IDC_FLEXSCALE + i);
        slFlexScale[i]->setRange(0, 1.0);
        slFlexScale[i]->setValue(0);
        mxToolTip::add(slFlexScale[i], "Flex Scale");
    }

    const int h = FLEX_ROWS_OF_SLIDERS * 20 + 5;
    new mxButton(wFlex, 5, h + 1, FLEX_DROPDOWN_WIDTH - 2, 18, "Reset", IDC_FLEXDEFAULTS);
    int w = (FLEX_DROPDOWN_WIDTH + 4) + 5;
    new mxButton(wFlex, w, h + 1, FLEX_DROPDOWN_WIDTH - 2, 18, "Random", IDC_FLEXRANDOM);
    w += (FLEX_DROPDOWN_WIDTH + 4) + 5;
    new mxButton(wFlex, w, h + 1, FLEX_DROPDOWN_WIDTH - 2, 18, "Zero", IDC_FLEXZERO);
    w += (FLEX_DROPDOWN_WIDTH + 4) + 5;
    new mxButton(wFlex, w, h + 1, FLEX_DROPDOWN_WIDTH - 2, 18, "One", IDC_FLEXONE);
}

void ControlPanel::SetupPhysicsWindow(mxTab *pTab)
{
    mxWindow *wPhysics = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wPhysics, "Physics");

    new mxLabel(wPhysics, 5, 33, 30, 18, "Mass");
    leMass = new mxLineEdit(wPhysics, 35, 30, 70, 22, "", IDC_PHYS_MASS);

    cPhysicsBone = new mxChoice(wPhysics, 5, 5, 345, 22, IDC_PHYS_BONE);
    cPhysicsBone->add("None");
    cPhysicsBone->select(0);

    new mxCheckBox(wPhysics, 5, 55, 100, 20, "Highlight", IDC_PHYSICSMODEL);
    int x = 5;
    int y = 80;

    rbConstraintAxis[0] = new mxRadioButton(wPhysics, x, y, 35, 22, "X", IDC_PHYS_CON_AXIS_X, true);
    rbConstraintAxis[1] = new mxRadioButton(wPhysics, x + 35, y, 35, 22, "Y", IDC_PHYS_CON_AXIS_Y);
    rbConstraintAxis[2] = new mxRadioButton(wPhysics, x + 70, y, 35, 22, "Z", IDC_PHYS_CON_AXIS_Z);
    rbConstraintAxis[0]->setChecked(true); // setPhysicsAxis( 0 )
    y += 25;

    new mxLabel(wPhysics, x, y, 45, 18, "Friction");
    slPhysicsFriction = new mxSlider(wPhysics, x + 50, y, 180, 18, IDC_PHYS_CON_FRICTION);
    slPhysicsFriction->setRange(0, 1000.0, 1000);
    slPhysicsFriction->setValue(0);
    slPhysicsFriction->setSteps(1, 1);
    lPhysicsFriction = new mxLabel(wPhysics, x + 230, y, 30, 18, "0");

    x = 135;
    y = 30;

    new mxLabel(wPhysics, 115, y, 30, 18, "Min");
    slPhysicsConMin = new mxSlider(wPhysics, x, y, 180, 18, IDC_PHYS_CON_MIN);
    slPhysicsConMin->setRange(-180, 180.0, 360);
    slPhysicsConMin->setValue(-90);
    slPhysicsConMin->setSteps(1, 1);
    lPhysicsConMin = new mxLabel(wPhysics, x + 180, y, 30, 18, "-90");
    y += 25;

    new mxLabel(wPhysics, 115, y, 30, 18, "Max");
    slPhysicsConMax = new mxSlider(wPhysics, x, y, 180, 18, IDC_PHYS_CON_MAX);
    slPhysicsConMax->setRange(-180.0, 180.0, 360);
    slPhysicsConMax->setValue(90.0);
    slPhysicsConMax->setSteps(1, 1);
    lPhysicsConMax = new mxLabel(wPhysics, x + 180, y, 30, 18, "90");
    y += 25;

    new mxLabel(wPhysics, 115, y, 30, 18, "Test");
    slPhysicsConTest = new mxSlider(wPhysics, x, y, 55, 18, IDC_PHYS_CON_TEST);
    slPhysicsConTest->setRange(0, 1.0, 100);
    slPhysicsConTest->setValue(0);

    cbLinked = new mxCheckBox(wPhysics, 200, y, 50, 20, "Link", IDC_PHYS_CON_LINK_LIMITS);
    mxToolTip::add(cbLinked, "Link mins/maxs to be symmetric");
    new mxButton(wPhysics, 250, y, 80, 22, "Generate QC", IDC_PHYS_QCFILE);

    x += 220;
    y = 5;
    new mxLabel(wPhysics, x, y, 55, 18, "Mass Bias");
    new mxLabel(wPhysics, x, y + 25, 55, 18, "Inertia");
    new mxLabel(wPhysics, x, y + 50, 55, 18, "Damping");
    new mxLabel(wPhysics, x, y + 75, 55, 18, "Rot Damp");
    new mxLabel(wPhysics, x, y + 100 + 3, 55, 18, "Material");
    x += 55;

    slPhysicsParamMassBias = new mxSlider(wPhysics, x, y, 76, 18, IDC_PHYS_P_MASSBIAS);
    slPhysicsParamMassBias->setRange(0, 10.0, 100);
    slPhysicsParamMassBias->setValue(1.0);
    slPhysicsParamMassBias->setSteps(1, 1);
    lPhysicsParamMassBias = new mxLabel(wPhysics, x + 80, y, 30, 18, "1.0");
    y += 25;

    slPhysicsParamInertia = new mxSlider(wPhysics, x, y, 76, 18, IDC_PHYS_P_INERTIA);
    slPhysicsParamInertia->setRange(0, 10.0, 100);
    slPhysicsParamInertia->setValue(1.0);
    slPhysicsParamInertia->setSteps(1, 1);
    lPhysicsParamInertia = new mxLabel(wPhysics, x + 80, y, 30, 18, "1.0");
    y += 25;

    slPhysicsParamDamping = new mxSlider(wPhysics, x, y, 76, 18, IDC_PHYS_P_DAMPING);
    slPhysicsParamDamping->setRange(0, 1.0, 100);
    slPhysicsParamDamping->setValue(0.01);
    slPhysicsParamDamping->setSteps(1, 1);
    lPhysicsParamDamping = new mxLabel(wPhysics, x + 80, y, 30, 18, "0.5");
    y += 25;

    slPhysicsParamRotDamping = new mxSlider(wPhysics, x, y, 76, 18, IDC_PHYS_P_ROT_DAMPING);
    slPhysicsParamRotDamping->setRange(0, 10.0, 200);
    slPhysicsParamRotDamping->setValue(0.2);
    slPhysicsParamRotDamping->setSteps(1, 1);
    lPhysicsParamRotDamping = new mxLabel(wPhysics, x + 80, y, 30, 18, "0.2");
    y += 25;

    lPhysicsMaterial = new mxLabel(wPhysics, x, y + 3, 110, 18, "default");
}

void ControlPanel::SetupSoftbodyWindow(mxTab *pTab)
{
    mxWindow *wSoftbody = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wSoftbody, "Cloth");
    leSoftbodyIterations = new mxLabel(wSoftbody, 5, 33, 90, 18, "Iterations");
    slSoftbodyIterations = new mxSlider(wSoftbody, 100, 33, 100, 18, IDC_SOFT_ITERATIONS);
    slSoftbodyIterations->setRange(1, 101, 100);
    slSoftbodyIterations->setValue(1.0f);
    slSoftbodyIterations->setSteps(1, 5);
    new mxLabel(wSoftbody, 210, 33, 60, 18, "Wind Yaw");
    slSoftbodyWindYaw = new mxSlider(wSoftbody, 285, 33, 105, 18, IDC_SOFT_WIND_YAW);
    slSoftbodyWindYaw->setRange(0, 360, 360);
    slSoftbodyWindYaw->setValue(0);
    slSoftbodyWindYaw->setSteps(1, 5);
    new mxLabel(wSoftbody, 405, 33, 80, 18, "Wind Strength");
    slSoftbodyWindStrength = new mxSlider(wSoftbody, 480, 33, 100, 18, IDC_SOFT_WIND_STRENGTH);
    slSoftbodyWindStrength->setRange(0, 1, 100);
    slSoftbodyWindStrength->setValue(0);
    slSoftbodyWindStrength->setSteps(1, 5);
    cSoftbodyCtrl = new mxChoice(wSoftbody, 5, 5, 345, 22, IDC_SOFT_BONE);
    cSoftbodyCtrl->add("None");
    cSoftbodyCtrl->select(0);
    cbSoftbodySimulate = new mxCheckBox(wSoftbody, 5, 55, 80, 20, "Simulate", IDC_SOFT_SIMULATE);
    new mxLabel(wSoftbody, 100, 55, 70, 18, "2D Stretch");
    slSoftbodySurfaceStretch = new mxSlider(wSoftbody, 170, 55, 100, 20, IDC_SOFT_SURFACE_STRETCH);
    slSoftbodySurfaceStretch->setRange(0, 3, 300);
    slSoftbodySurfaceStretch->setValue(0);
    slSoftbodySurfaceStretch->setSteps(1, 10);
    new mxLabel(wSoftbody, 270, 55, 70, 18, "1D Stretch");
    slSoftbodyThreadStretch = new mxSlider(wSoftbody, 340, 55, 100, 20, IDC_SOFT_THREAD_STRETCH);
    slSoftbodyThreadStretch->setRange(0, 3, 300);
    slSoftbodyThreadStretch->setValue(0);
    slSoftbodyThreadStretch->setSteps(1, 10);
    cbSoftbodyPolygons = new mxCheckBox(wSoftbody, 5, 80, 80, 20, "Polygons", IDC_SOFT_SHOW_POLYGONS);
    cbSoftbodyEdges = new mxCheckBox(wSoftbody, 100, 80, 80, 20, "Edges", IDC_SOFT_SHOW_EDGES);
    cbSoftbodyBases = new mxCheckBox(wSoftbody, 200, 80, 80, 20, "Bases", IDC_SOFT_SHOW_BASES);
    cbSoftbodyWind = new mxCheckBox(wSoftbody, 300, 80, 80, 20, "Wind", IDC_SOFT_SHOW_WIND);
    cbSoftbodyIndices = new mxCheckBox(wSoftbody, 400, 80, 80, 20, "Indices", IDC_SOFT_SHOW_INDICES);
    const int x = 5, y = 105;
    rbSoftbodyAxis[0] = new mxRadioButton(wSoftbody, x, y, 35, 22, "X", IDC_SOFT_CON_AXIS_X, true);
    rbSoftbodyAxis[1] = new mxRadioButton(wSoftbody, x + 35, y, 35, 22, "Y", IDC_SOFT_CON_AXIS_Y);
    rbSoftbodyAxis[2] = new mxRadioButton(wSoftbody, x + 70, y, 35, 22, "Z", IDC_SOFT_CON_AXIS_Z);
    rbSoftbodyAxis[0]->setChecked(true); // setSoftbodyAxis( 0 )
}

void ControlPanel::SetupIKRuleWindow(mxTab *pTab)
{
    mxWindow *wIKRule = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wIKRule, "IKRule");

    new mxLabel(wIKRule, 5, 5, 80, 20, "Chain:");
    cIKChain = new mxChoice(wIKRule, 90, 5, 50, 20, IDC_IKRULE_CHAIN);
    cIKChain->add("lfoot");
    cIKChain->add("rfoot");
    cIKChain->add("lhand");
    cIKChain->add("rhand");
    cIKChain->select(0);
    mxToolTip::add(cIKChain, "Select IK Chain");

    new mxLabel(wIKRule, 145, 5, 80, 20, "Type:");
    cIKType = new mxChoice(wIKRule, 230, 5, 80, 20, IDC_IKRULE_CHOICE);
    cIKType->add("footstep");
    cIKType->add("touch");
    cIKType->add("release");
    cIKType->add("attachment");
    cIKType->add("unlatch");
    cIKType->select(0);
    mxToolTip::add(cIKType, "Select IK Type");

    lIKTouch = new mxLabel(wIKRule, 315, 5, 80, 20, "Bone:");
    cIKTouch = new mxChoice(wIKRule, 400, 5, 200, 20, IDC_IKRULE_TOUCH);
    PopulateBoneList(cIKTouch, {});
    mxToolTip::add(cIKTouch, "Select Touched Bone");

    lIKAttachment = new mxLabel(wIKRule, 315, 5, 80, 20, "Attachment:");
    leIKAttachment = new mxLineEdit(wIKRule, 400, 5, 80, 20, "", IDC_IKRULE_ATTACHMENT);

    cbIKRangeToggle = new mxCheckBox(wIKRule, 5, 30, 80, 20, "Range", IDC_IKRULE_RANGE_TOGGLE);
    mxToolTip::add(cbIKRangeToggle, "Toggle range option");
    new mxButton(wIKRule, 90, 30, 30, 20, "start", IDC_IKRULE_RANGE_START_NOW);
    leIKRangeStart = new mxLineEdit2(wIKRule, 120, 30, 35, 20, "..", IDC_IKRULE_RANGE_START);
    new mxButton(wIKRule, 160, 30, 30, 20, "peak", IDC_IKRULE_RANGE_PEAK_NOW);
    leIKRangePeak = new mxLineEdit2(wIKRule, 190, 30, 35, 20, "..", IDC_IKRULE_RANGE_PEAK);
    new mxButton(wIKRule, 230, 30, 30, 20, "tail", IDC_IKRULE_RANGE_TAIL_NOW);
    leIKRangeTail = new mxLineEdit2(wIKRule, 260, 30, 35, 20, "..", IDC_IKRULE_RANGE_TAIL);
    new mxButton(wIKRule, 300, 30, 30, 20, "end", IDC_IKRULE_RANGE_END_NOW);
    leIKRangeEnd = new mxLineEdit2(wIKRule, 330, 30, 35, 20, "..", IDC_IKRULE_RANGE_END);

    cbIKContactToggle = new mxCheckBox(wIKRule, 5, 55, 80, 20, "Contact", IDC_IKRULE_CONTACT_TOGGLE);
    mxToolTip::add(cbIKContactToggle, "Toggle contact option");
    new mxButton(wIKRule, 90, 55, 30, 20, "frame", IDC_IKRULE_CONTACT_FRAME_NOW);
    leIKContactFrame = new mxLineEdit2(wIKRule, 120, 55, 35, 20, "", IDC_IKRULE_CONTACT_FRAME);

    new mxLabel(wIKRule, 5, 80, 80, 20, "Transform using:");
    cIKUsing = new mxChoice(wIKRule, 90, 80, 80, 20, IDC_IKRULE_USING);
    cIKUsing->add("neither");
    cIKUsing->add("source");
    cIKUsing->add("sequence");
    cIKUsing->select(0);
    mxToolTip::add(cIKUsing, "Choose Transform To Use");

    new mxLabel(wIKRule, 5, 105, 80, 20, "QC String:");
    leIKQCString = new mxLineEdit2(wIKRule, 90, 105, 500, 20, "", IDC_IKRULE_QC_STRING);
    UpdateIKRuleWindow();
}

void ControlPanel::SetupEventWindow(mxTab *pTab)
{
    mxWindow *wEvents = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wEvents, "Events");

    new mxLabel(wEvents, 5, 5, 80, 20, "Sound:");
    mxButton *bSoundFrameNow = new mxButton(wEvents, 90, 5, 30, 20, "frame", IDC_EVENT_SOUND_FRAME_NOW);
    mxToolTip::add(bSoundFrameNow, "Set sound start at the current frame");
    leEventSoundFrame = new mxLineEdit2(wEvents, 120, 5, 35, 20, "0", IDC_EVENT_SOUND_FRAME);
    lbEventSoundName = new mxListBox(wEvents, 160, 5, 300, 170, IDC_EVENT_SOUND_NAME);
    mxToolTip::add(lbEventSoundName, "Select Sound Name"); // no soundemitter scripts yet: list stays empty

    new mxLabel(wEvents, 5, 170, 80, 20, "QC String:");
    leEventQCString = new mxLineEdit2(wEvents, 90, 170, 450, 20, "", IDC_EVENT_QC_STRING);
    BuildEventQCString();

    lEventSequence = new mxLabel(wEvents, 460, 5, 300, 20, "");
    lbEventHistory = new mxListBox(wEvents, 460, 25, 300, 150);
}

void ControlPanel::SetupBoneControlWindow(mxTab *pTab)
{
    m_pBoneWindow = new CBoneControlWindow(this, m_settings);
    pTab->add(m_pBoneWindow, "Bones");
    m_pBoneWindow->Init();
}

void ControlPanel::SetupAttachmentsWindow(mxTab *pTab)
{
    m_pAttachmentsWindow = new CAttachmentsWindow(this);
    pTab->add(m_pAttachmentsWindow, "Attachments");
    m_pAttachmentsWindow->Init();
}

void ControlPanel::SetupMatVarWindow(mxTab *pTab)
{
    mxWindow *wMatVars = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wMatVars, "Materials");

    new mxLabel(wMatVars, 2, 2, 100, 18, "Materials:");
    cMaterialList = new mxListBox(wMatVars, 0, 20, 200, 170, IDC_MATERIALVARMATS);
    cMaterialList->add("None");
    cMaterialList->select(1);
    mxToolTip::add(cMaterialList, "Materials (VMT files) this model has loaded");

    new mxLabel(wMatVars, 202, 2, 100, 18, "Material Parameters:");
    cMaterialParamList = new mxListBox(wMatVars, 200, 20, 200, 170, IDC_MATERIALVARPARAMS);
    cMaterialParamList->add("None");
    cMaterialParamList->select(1);
    mxToolTip::add(cMaterialParamList, "Material parameters of this material");

    new mxLabel(wMatVars, 405, 2, 100, 18, "Modify Parameter:");
    leMaterialParamText = new mxLineEdit2(wMatVars, 405, 25, 510, 24, "", IDC_MATVAREDIT);
    leMaterialParamText->setVisible(false);

    const auto matrixSlider = [&](mxLabel *&label, mxSlider *&slider, int y, const char *text, float min, float max, float value) {
        label = new mxLabel(wMatVars, 405, y, 70, 18, text);
        slider = new mxSlider(wMatVars, 465, y, 450, 20, IDC_MATVARSLIDERMATRIX);
        slider->setRange(min, max);
        slider->setSteps(1, 1);
        slider->setValue(value);
        label->setVisible(false);
        slider->setVisible(false);
    };
    matrixSlider(lblMatrixRotation, slMaterialParamMatrixSliderRotation, 55, "Rotation:", -180.0, 180.0, 0.0);
    matrixSlider(lblMatrixScaleX, slMaterialParamMatrixSliderScaleX, 75, "Scale X:", -5.0, 5.0, 1.0);
    matrixSlider(lblMatrixScaleY, slMaterialParamMatrixSliderScaleY, 95, "Scale Y:", -5.0, 5.0, 1.0);
    matrixSlider(lblMatrixTranslateX, slMaterialParamMatrixSliderTranslateX, 115, "Translate X:", -2.0, 2.0, 0.0);
    matrixSlider(lblMatrixTranslateY, slMaterialParamMatrixSliderTranslateY, 135, "Translate Y:", -2.0, 2.0, 0.0);

    bMaterialParamColor = new mxButton(wMatVars, 405, 55, 100, 30, "Color picker", IDC_MATVARCOLORPICKER);
    bMaterialParamColor->setVisible(false);

    slMaterialParamFloat = new mxSlider(wMatVars, 405, 55, 510, 20, IDC_MATVARSLIDERFLOAT);
    slMaterialParamFloat->setRange(-1.0, 1.0);
    slMaterialParamFloat->setValue(0.0);
    slMaterialParamFloat->setVisible(false);

    cbMaterialParamMultiEdit = new mxCheckBox(wMatVars, 505, 0, 150, 20, "Affect all loaded materials", 0);

    bMaterialParamLoad = new mxButton(wMatVars, 405, 159, 100, 20, "Replace VMT", IDC_MATVARLOAD);
    mxToolTip::add(bMaterialParamLoad, "Temporarily replace this material with a custom set of VMT parameters.");
    bMaterialParamLoad->setVisible(false);

    bMaterialParamCopyToClipboard = new mxButton(wMatVars, 510, 159, 100, 20, "Copy to clipboard", IDC_MATVARCOPYTOCLIPBOARD);
    bMaterialParamCopyToClipboard->setVisible(false);
}

void ControlPanel::SetupSubmodelWindow(mxTab *pTab)
{
    mxWindow *wSubmodels = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wSubmodels, "Submodels");

    bSubmodelAdd = new mxButton(wSubmodels, 2, 10, 90, 70, "Add Submodel", IDC_SUBMODEL_LOADMERGEDMODEL);
    bSubmodelAddSteam = new mxButton(wSubmodels, 2, 90, 90, 20, "Add [Steam]", IDC_SUBMODEL_LOADMERGEDMODEL_STEAM);
    bSubmodelRemoveAll = new mxButton(wSubmodels, 2, 120, 90, 50, "Remove All", IDC_SUBMODEL_UNLOADALLMERGEDMODELS);

    cSubmodelList = new mxListBox(wSubmodels, 100, 10, 450, 175, IDC_SUBMODEL_UPDATE_SELECTION);
    mxToolTip::add(cSubmodelList, "Select submodels to add/remove/change");

    bSubmodelRemoveSelected = new mxButton(wSubmodels, 555, 10, 120, 20, "Remove Selected", IDC_SUBMODEL_UNLOADMERGEDMODEL);
    bSubmodelRemoveSelected->setEnabled(false);

    new mxLabel(wSubmodels, 555, 45, 160, 18, "Force attach to:");
    cSubmodelAttachTo = new mxChoice(wSubmodels, 555, 60, 200, 20, IDC_SUBMODEL_UPDATE_BONESELECTION);
    cSubmodelAttachTo->setEnabled(false);

    new mxLabel(wSubmodels, 555, 80, 160, 18, "From local attach origin:");
    cSubmodelLocalAttachOrigin = new mxChoice(wSubmodels, 555, 95, 200, 20, IDC_SUBMODEL_UPDATE_BONESELECTION);
    cSubmodelLocalAttachOrigin->setEnabled(false);
}

void ControlPanel::SetupCompileWindow(mxTab *pTab)
{
    mxWindow *wCompile = new mxWindow(this, 0, 0, 0, 0);
    pTab->add(wCompile, "Compile");

    new mxLabel(wCompile, 2, 2, 200, 15, "Recent QC scripts:");

    cCompileRecentQCpaths = new mxListBox(wCompile, 2, 18, 300, 155, IDC_COMPILE_UPDATE_QCPATHSELECTION);

    bCompileQCRemoveFromList = new mxButton(wCompile, 2, 168, 160, 18, "Remove selected from list", IDC_COMPILE_REMOVEFROMLIST);
    bCompileQCRemoveFromList->setEnabled(false);

    bCompileQCWhenSelected = new mxButton(wCompile, 170, 168, 130, 18, "One-click compile [OFF]", IDC_COMPILE_SELECTEDTOGGLE);
    mxToolTip::add(bCompileQCWhenSelected, "When enabled, selecting a QC in the list will start a recompile automatically.");
    bCompileSelectedToggle = false;

    const int nButtonsLeft = 310;
    const int nButtonsTop = 22;

    lblFullQCPath = new mxLabel(wCompile, nButtonsLeft, 2, 500, 18,
                                "No QC script selected. To add a QC file, drag and drop it onto the viewport.");

    bCompileQCCompile = new mxButton(wCompile, nButtonsLeft, nButtonsTop, 120, 24, "Recompile", IDC_COMPILE_CALLSTUDIOMDL);
    bCompileQCCompile->setEnabled(false);

    const int nButtonsRight = 690;

    bCompileQCLoadModel = new mxButton(wCompile, nButtonsRight, nButtonsTop, 100, 24, "Load in HLMV", IDC_COMPILE_LOADMODELFILE);
    bCompileQCLoadModel->setEnabled(false);

    bCompileQCShowCompileOutput = new mxButton(wCompile, nButtonsRight + 110, nButtonsTop, 100, 24, "Open full log file", IDC_COMPILE_OPENLOGFILE);
    bCompileQCShowCompileOutput->setEnabled(false);

    bCompileQCBrowseToQC = new mxButton(wCompile, nButtonsRight + 220, nButtonsTop, 100, 24, "Explore to QC", IDC_COMPILE_EXPLORETOQC);
    bCompileQCBrowseToQC->setEnabled(false);

    cCompileQCOutput = new mxListBox(wCompile, nButtonsLeft, 56, 700, 130);
    lblCompileWarningOrError = new mxLabel(wCompile, nButtonsLeft, 168, 1000, 18, "");
}

void ControlPanel::BuildEventQCString()
{
    std::string qc = "{ event AE_CL_PLAYSOUND ";
    qc += leEventSoundFrame->getLabel();
    qc += " \"";
    qc += lbEventSoundName->getItemText(lbEventSoundName->getSelectedIndex());
    qc += "\" }";
    leEventQCString->setText(qc.c_str());
}

void ControlPanel::BuildIKRuleQCString()
{
    std::string qc = "ikrule ";
    qc += cIKChain->getLabel();
    qc += " ";
    const std::string type = cIKType->getLabel();
    qc += type;

    if (type == "touch")
    {
        qc += " \"";
        if (cIKTouch->getSelectedIndex() > 0)
            qc += cIKTouch->getLabel();
        qc += "\"";
    }
    else if (type == "attachment")
    {
        qc += " \"";
        qc += leIKAttachment->getLabel();
        qc += "\"";
    }

    if (cbIKRangeToggle->isChecked())
    {
        qc += " range ";
        for (auto *edit : {leIKRangeStart, leIKRangePeak, leIKRangeTail})
        {
            qc += edit->getLabel();
            qc += " ";
        }
        qc += leIKRangeEnd->getLabel();
    }

    if (cbIKContactToggle->isChecked())
    {
        qc += " contact ";
        qc += leIKContactFrame->getLabel();
    }

    const int nUsing = cIKUsing->getSelectedIndex();
    if (nUsing == 1) // source
        qc += " usesource";
    else if (nUsing == 2) // sequence
        qc += " usesequence";

    leIKQCString->setText(qc.c_str());
}

void ControlPanel::UpdateIKRuleWindow()
{
    const std::string type = cIKType->getLabel();
    const bool bIsTouch = type == "touch";
    const bool bIsAttachment = type == "attachment";

    lIKTouch->setVisible(bIsTouch);
    cIKTouch->setVisible(bIsTouch);
    lIKAttachment->setVisible(bIsAttachment);
    leIKAttachment->setVisible(bIsAttachment);

    BuildIKRuleQCString();
}

void ControlPanel::redraw()
{
    if (m_viewport)
        m_viewport->update();
}

void ControlPanel::toggle(int id)
{
    for (auto *box : findChildren<QCheckBox *>())
        if (auto *check = dynamic_cast<mxCheckBox *>(box); check && check->getId() == id)
        {
            check->click(); // reports the change like a user click, as upstream's setShow* + handler did
            return;
        }
}

void ControlPanel::OnLoadModel(const MdlMesh &mesh)
{
    const auto &info = mesh.info;
    char str[512];
    std::snprintf(str, sizeof(str),
                  "Total bones: %d\nHW Bones: %d\nBatches: %d\nBone Controllers: %d\nHit Boxes: %d in %d sets\nSequences: %d\n",
                  int(info.bones.size()), int(info.bones.size()), int(mesh.batches.size()), info.boneControllers,
                  info.hitboxes, info.hitboxSets, int(info.sequences.size()));
    lModelInfo1->setLabel("%s", str);
    m_info = info;
    m_materialCount = int(mesh.textures.size());
    m_settings.bodygroups.assign(info.bodyParts.size(), 0);
    UpdateBodyInfo();

    cBodypart->removeAll();
    for (const auto &part : info.bodyParts)
        cBodypart->add(part.toUtf8().constData());
    cBodypart->select(0);
    SetBodypart(0);
    cSkin->removeAll();
    for (int i = 0; i < info.skinFamilies; ++i)
        cSkin->add(("Skin " + std::to_string(i + 1)).c_str());
    cSkin->select(0);
    cLODChoice->removeAll();
    cLODChoice->add("LOD 0");
    cLODChoice->select(0);

    cMaterials->removeAll();
    for (const auto &material : mesh.textures)
        cMaterials->add(material.toUtf8().constData());
    cMaterials->select(0);
    Fill(cMessageList, mesh.textures, "None");
    Fill(cMaterialList, mesh.textures, "None");

    initSequenceChoices();
    initBoneControllers();
    updateSpeedScale();

    PopulateBoneList(cIKTouch, info.bones);
    m_settings.skin = 0;
    m_settings.hitboxSets = mesh.hitboxSets;
    m_pBoneWindow->OnLoadModel(mesh);
    m_pAttachmentsWindow->OnLoadModel(info);
    UpdateIKRuleWindow();

    // upstream ControlPanel flex setup: every slider lists all controllers and shows the last 48 by default.
    m_flexControllers = mesh.flex.controllers;
    const int controllerCount = int(m_flexControllers.size());
    for (int j = 0; j < NUM_FLEX_SLIDERS; j++)
    {
        cFlex[j]->removeAll();
        for (const auto &controller : m_flexControllers)
            cFlex[j]->add(controller.name.toUtf8().constData());
        cFlex[j]->select(controllerCount - NUM_FLEX_SLIDERS + j); // negative selects nothing, as on Win32
    }
    m_settings.flexRaw.assign(m_flexControllers.size(), 0.f);
    for (int i = 0; i < controllerCount; i++)
        SetFlexController(i, 0.0f);
    UpdateFlexSliders();
}

void ControlPanel::SetFlexController(int index, float value)
{
    const auto &controller = m_flexControllers[index];
    if (controller.min != controller.max)
        value = (value - controller.min) / (controller.max - controller.min);
    m_settings.flexRaw[index] = std::clamp(value, 0.0f, 1.0f);
    ++m_settings.flexRevision;
}

StudioAnimation *ControlPanel::anim() const
{
    return m_settings.animation.get();
}

// upstream ComposeSequenceDisplayName
QString ControlPanel::ComposeSequenceDisplayName(int sequence) const
{
    const auto name = m_showActivities ? anim()->ActivityName(sequence) : anim()->SequenceName(sequence);
    return m_showSequenceIndices ? QStringLiteral("[%1] %2").arg(sequence).arg(name) : name;
}

int ControlPanel::GetSequenceForSelection(int slot, int selection) const
{
    const auto &table = m_selectionToSequence[slot];
    return selection >= 0 && size_t(selection) < table.size() ? table[selection] : 0;
}

int ControlPanel::GetSelectionForSequence(int slot, int sequence) const
{
    const auto &table = m_sequenceToSelection[slot];
    return sequence >= 0 && size_t(sequence) < table.size() ? table[sequence] : -1;
}

// upstream initSequenceChoices: optional faceposer-type/name sort, hidden sequences and per-slot filters.
void ControlPanel::initSequenceChoices(int onlySlot)
{
    if (!anim())
    {
        for (auto *choice : cSequence)
        {
            choice->removeAll();
            for (const auto &sequence : m_info.sequences)
                choice->add(sequence.toUtf8().constData());
            choice->select(0);
        }
        return;
    }
    const int count = anim()->SequenceCount();
    // upstream CreateSortedSequenceList: other sequences, then gestures, then postures, each by name
    std::vector<int> order(count);
    std::vector<std::pair<int, QString>> keys(count);
    for (int j = 0; j < count; j++)
    {
        order[j] = j;
        keys[j] = {anim()->FacePoserType(j), ComposeSequenceDisplayName(j)};
    }
    if (m_sortSequences)
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if (keys[a].first != keys[b].first)
                return keys[a].first < keys[b].first;
            return keys[a].second.compare(keys[b].second, Qt::CaseInsensitive) < 0;
        });

    for (int i = 0; i < MAX_SEQUENCES; i++)
    {
        if (onlySlot >= 0 && i != onlySlot)
            continue;
        auto &toSequence = m_selectionToSequence[i];
        auto &toSelection = m_sequenceToSelection[i];
        toSequence.clear();
        toSelection.assign(count, 0);
        const auto filter = leSequenceFilter[i]->text();
        cSequence[i]->removeAll();
        for (const int sequence : order)
        {
            const auto &name = keys[sequence].second;
            const bool filteredOut = !filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive);
            const bool hidden = anim()->IsHidden(sequence);
            if (!filteredOut && (m_showHidden || !hidden))
            {
                cSequence[i]->add(name.toUtf8().constData());
                toSelection[sequence] = int(toSequence.size());
                toSequence.push_back(sequence);
            }
            else
                toSelection[sequence] = toSequence.empty() ? 0 : int(toSequence.size()) - 1; // previous valid selection
        }
        if (toSequence.empty() && count > 0)
        {
            cSequence[i]->add(ComposeSequenceDisplayName(0).toUtf8().constData());
            toSequence.push_back(0);
        }
        cSequence[i]->select(0);
        if (onlySlot == -1)
            slSequence[i]->setValue(0);
        const int sequence = GetSequenceForSelection(i, 0);
        if (i == 0)
        {
            setSequence(sequence);
            showActivityModifiers(sequence);
        }
        else
            setOverlaySequence(i, sequence, slSequence[i]->getValue());
    }

    if (onlySlot == -1)
    {
        for (int i = 0; i < NUM_POSEPARAMETERS; i++)
        {
            float min, max;
            const bool used = anim()->GetPoseParameterRange(i, &min, &max);
            if (used)
            {
                cPoseParameter[i]->removeAll();
                for (int j = 0; j < anim()->PoseParameterCount(); j++)
                    cPoseParameter[i]->add(anim()->PoseParameterName(j).toUtf8().constData());
                cPoseParameter[i]->select(i);
                slPoseParameter[i]->setRange(min, max, 1000);
                mxToolTip::add(slPoseParameter[i], anim()->PoseParameterName(i).toUtf8().constData());
                lePoseParameter[i]->setLabel("%.1f", 0.0);
            }
            for (QWidget *widget : std::initializer_list<QWidget *>{cPoseParameter[i], slPoseParameter[i], lePoseParameter[i]})
            {
                widget->setEnabled(used);
                widget->setVisible(used);
            }
            slPoseParameter[i]->setValue(0.0);
            setBlend(i, 0.0);
        }
        for (int i = 0; i < anim()->PoseParameterCount(); i++)
            setBlend(i, 0.0);
    }
}

// upstream initBoneControllers
void ControlPanel::initBoneControllers()
{
    const int count = anim() ? anim()->ControllerCount() : 0;
    cController->setEnabled(count > 0);
    slController->setEnabled(count > 0);
    cController->removeAll();
    for (int i = 0; i < count; i++)
        cController->add(("Controller " + std::to_string(anim()->GetController(i).inputfield)).c_str());
    if (count > 0)
    {
        cController->select(0);
        slController->setRange(anim()->GetController(0).start, anim()->GetController(0).end);
        slController->setValue(0);
    }
}

void ControlPanel::setSequence(int sequence)
{
    const int selection = GetSelectionForSequence(0, sequence);
    if (selection >= 0)
    {
        cSequence[0]->select(selection);
        anim()->SetSequence(sequence);
    }
    else
    {
        cSequence[0]->select(0);
        anim()->SetSequence(GetSequenceForSelection(0, selection));
    }
    updateFrameSelection();
    updateGroundSpeed();
}

void ControlPanel::setOverlaySequence(int num, int sequence, float weight)
{
    const int selection = GetSelectionForSequence(num, sequence);
    if (selection >= 0)
        cSequence[num]->select(selection);
    else
    {
        cSequence[num]->select(0);
        sequence = GetSequenceForSelection(num, 0);
    }
    anim()->SetOverlaySequence(num - 1, sequence, weight);
    slSequence[num]->setValue(weight);
    updateFrameSelection();
}

void ControlPanel::showActivityModifiers(int sequence)
{
    const auto modifiers = anim()->ActivityModifiers(sequence);
    cActivityModifiers->setEnabled(!modifiers.isEmpty());
    cActivityModifiers->removeAll();
    for (const auto &modifier : modifiers)
        cActivityModifiers->add(modifier.toUtf8().constData());
    cActivityModifiers->select(0);
}

int ControlPanel::getFrameSelection() const
{
    for (int i = 0; i < MAX_SEQUENCES; i++)
        if (rbFrameSelection[i]->isChecked())
            return i;
    return 0;
}

void ControlPanel::setFrame(float frame)
{
    const int shown = anim()->SetFrame(getFrameSelection(), int(frame));
    lForcedFrame->setLabel("%3d", shown);
}

void ControlPanel::updateFrameSelection()
{
    const int maxFrame = anim()->GetMaxFrame(getFrameSelection());
    slForceFrame->setRange(0, maxFrame, maxFrame);
    slForceFrame->setSteps(1, 1);
}

void ControlPanel::updateFrameSlider()
{
    const float frame = anim()->GetFrame(getFrameSelection());
    lForcedFrame->setLabel("%3.1f", frame);
    slForceFrame->setValue(frame);
}

void ControlPanel::setSpeedScale(float scale)
{
    slSpeedScale->setValue(scale);
    m_settings.speedScale = scale;
    updateSpeedScale();
}

void ControlPanel::updateSpeedScale()
{
    laFPS->setLabel("x %.2f = %.1f fps", m_settings.speedScale, anim() ? m_settings.speedScale * anim()->GetFPS() : 0.0f);
}

void ControlPanel::setBlend(int index, float value)
{
    anim()->SetPoseParameter(index, value);
    // reset number of frames....
    updateFrameSelection();
    updateGroundSpeed();
}

void ControlPanel::updateGroundSpeed()
{
    laGroundSpeed->setLabel("Speed: %.2f", anim()->GetGroundSpeed());
}

void ControlPanel::SaveSelectedSequences()
{
    for (int i = 0; i < MAX_SEQUENCES; i++)
    {
        const int index = cSequence[i]->getSelectedIndex();
        m_savedSequences[i] = index >= 0 ? GetSequenceForSelection(i, index) : 0;
        if (i > 0)
            m_savedWeights[i] = slSequence[i]->getValue();
    }
}

void ControlPanel::RestoreSelectedSequences()
{
    for (int i = 0; i < MAX_SEQUENCES; i++)
        if (i == 0)
            setSequence(m_savedSequences[i]);
        else
            setOverlaySequence(i, m_savedSequences[i], m_savedWeights[i]);
}

void ControlPanel::OnAnimationTick()
{
    if (!anim())
        return;
    updateFrameSlider();
    updateGroundSpeed();
    laBlendAmount->setLabel("%.3f %%", 100.0f * anim()->GetTransitionAmount());
}

void ControlPanel::setSequenceListOption(int id, bool on)
{
    bool &option = id == IDC_VIEW_ACTIVITIES ? m_showActivities
                 : id == IDC_VIEW_HIDDEN ? m_showHidden
                 : id == IDC_VIEW_SEQUENCE_INDICES ? m_showSequenceIndices
                                                   : m_sortSequences;
    option = on;
    if (!anim())
        return;
    SaveSelectedSequences();
    initSequenceChoices();
    RestoreSelectedSequences();
}

// upstream ControlPanel::setBodypart: list the part's submodels and select its current one.
void ControlPanel::SetBodypart(int part)
{
    cSubmodel->removeAll();
    if (part < 0 || size_t(part) >= m_info.bodyModels.size())
        return;
    for (const auto &model : m_info.bodyModels[part])
        cSubmodel->add(model.isEmpty() ? "blank" : model.toUtf8().constData());
    cSubmodel->select(m_settings.bodygroups[part]);
}

void ControlPanel::UpdateBodyInfo()
{
    // StudioModel::SetBodygroup's body index: each part's submodel times the product of earlier parts' counts.
    int body = 0, base = 1;
    for (size_t i = 0; i < m_info.bodyModels.size() && i < m_settings.bodygroups.size(); ++i)
    {
        body += m_settings.bodygroups[i] * base;
        base *= int(m_info.bodyModels[i].size());
    }
    const auto extent = m_info.hullMax - m_info.hullMin;
    char str[512];
    std::snprintf(str, sizeof(str),
                  "Materials: %d\nSkin Families: %d\nBodyparts: %d\nAttachments: %d\nBody index: %d\n"
                  "Hull extent (X): %.2f\nHull extent (Y): %.2f\nHull extent (Z): %.2f\n",
                  m_materialCount, m_info.skinFamilies, int(m_info.bodyParts.size()), int(m_info.attachments.size()), body,
                  std::abs(extent.x()), std::abs(extent.y()), std::abs(extent.z()));
    lModelInfo2->setLabel("%s", str);
}

void ControlPanel::UpdateFlexSliders()
{
    for (int i = 0; i < NUM_FLEX_SLIDERS; i++)
    {
        const int index = cFlex[i]->getSelectedIndex();
        slFlexScale[i]->setValue(index >= 0 ? m_settings.flexRaw[index] : 0.f);
    }
    redraw();
}

void ControlPanel::OnUnloadModel()
{
    lModelInfo1->setLabel("No Model.");
    lModelInfo2->setLabel("");
    m_info = {};
    m_settings.bodygroups.clear();
}

int ControlPanel::handleEvent(mxEvent *event)
{
    if (event->event == mxEvent::Size)
    {
        tab->setBounds(0, 0, event->width, std::max(0, event->height - 20));
        updateSequenceSizes(event->width);
        return 1;
    }
    if (event->event != mxEvent::Action)
        return 0;

    switch (event->action)
    {
    case IDC_SKINS:
        if (const int skin = cSkin->getSelectedIndex(); skin >= 0)
        {
            m_settings.skin = skin;
            redraw();
        }
        break;

    case IDC_BODYPART:
        SetBodypart(cBodypart->getSelectedIndex());
        break;

    case IDC_SUBMODEL:
        if (const int part = cBodypart->getSelectedIndex(), model = cSubmodel->getSelectedIndex();
            part >= 0 && size_t(part) < m_settings.bodygroups.size() && model >= 0)
        {
            m_settings.bodygroups[part] = model;
            UpdateBodyInfo();
            redraw();
        }
        break;

    case IDC_ROLL_BODYGROUPS:
        for (size_t i = 0; i < m_settings.bodygroups.size(); ++i)
            m_settings.bodygroups[i] = int(QRandomGenerator::global()->bounded(qsizetype(m_info.bodyModels[i].size())));
        SetBodypart(cBodypart->getSelectedIndex());
        UpdateBodyInfo();
        redraw();
        break;

    case IDC_RENDERMODE:
        // shortcut: only Wireframe and Textured have renderer paths; other modes draw textured until ported.
        m_settings.wireframe = cRenderMode->getSelectedIndex() == 0;
        redraw();
        break;

    case IDC_OVERLAY_WIREFRAME:
        m_settings.wireframeOverlay = cbOverlayWireframe->isChecked();
        redraw();
        break;

    case IDC_SHOWORIGINAXIS:
        m_settings.showOriginAxis = cbShowOriginAxis->isChecked();
        redraw();
        break;

    case IDC_ORIGINAXISLENGTH:
        m_settings.originAxisLength = leOriginAxisLength->getValue();
        redraw();
        break;

    case IDC_BONES:
        m_settings.showBones = cbBones->isChecked();
        redraw();
        break;

    case IDC_ATTACHMENTS:
        m_settings.showAttachments = cbAttachments->isChecked();
        redraw();
        break;

    case IDC_HITBOXES:
        m_settings.showHitboxes = cbHitBoxes->isChecked();
        redraw();
        break;

    case IDC_NORMALS:
        m_settings.showNormals = cbNormals->isChecked();
        redraw();
        break;

    case IDC_TANGENTFRAME:
        m_settings.showTangents = cbTangentFrame->isChecked();
        redraw();
        break;

    case IDC_RENDER_FOV:
    {
        // Ignore partial input while typing; tan() is unusable outside (0, 180).
        bool ok = false;
        const float fov = leFOV->text().toFloat(&ok);
        if (ok && fov > 0 && fov < 180)
        {
            m_settings.fov = fov;
            redraw();
        }
        break;
    }

    case IDC_SPECULAR:
        m_settings.specular = cbSpecular->isChecked();
        redraw();
        break;

    case IDC_FORCEFRAME:
        if (anim())
        {
            setFrame(slForceFrame->getValue());
            setSpeedScale(0); // stop the animation
        }
        break;

    case IDC_SPEEDSCALE:
        setSpeedScale(slSpeedScale->getValue());
        break;

    case IDC_BLENDTIME:
        laBlendTime->setLabel("%.2f s", slBlendTime->getValue());
        if (anim())
            anim()->SetBlendTime(slBlendTime->getValue());
        break;

    case IDC_BLENDSEQUENCECHANGES:
        if (anim())
            anim()->blendSequenceChanges = dynamic_cast<QCheckBox *>(event->widget)->isChecked();
        break;

    case IDC_BLENDNOW:
        if (anim())
            anim()->StartBlending();
        break;

    case IDC_RUNIK:
        if (anim())
            anim()->enableIK = cbRunIK->isChecked();
        break;

    case IDC_CONTROLLER:
        if (const int index = cController->getSelectedIndex(); anim() && index >= 0)
        {
            slController->setRange(anim()->GetController(index).start, anim()->GetController(index).end);
            slController->setValue(0);
        }
        break;

    case IDC_CONTROLLERVALUE:
        if (const int index = cController->getSelectedIndex(); anim() && index >= 0)
            anim()->SetController(anim()->GetController(index).inputfield, slController->getValue());
        break;

    case IDC_PHYS_CON_FRICTION:
        lPhysicsFriction->setLabel("%d", int(slPhysicsFriction->getValue()));
        break;

    case IDC_PHYS_CON_MIN:
        lPhysicsConMin->setLabel("%d", int(slPhysicsConMin->getValue()));
        break;

    case IDC_PHYS_CON_MAX:
        lPhysicsConMax->setLabel("%d", int(slPhysicsConMax->getValue()));
        break;

    case IDC_PHYS_P_MASSBIAS:
        lPhysicsParamMassBias->setLabel("%.1f", slPhysicsParamMassBias->getValue());
        break;

    case IDC_PHYS_P_INERTIA:
        lPhysicsParamInertia->setLabel("%.1f", slPhysicsParamInertia->getValue());
        break;

    case IDC_PHYS_P_DAMPING:
        lPhysicsParamDamping->setLabel("%.2f", slPhysicsParamDamping->getValue());
        break;

    case IDC_PHYS_P_ROT_DAMPING:
        lPhysicsParamRotDamping->setLabel("%.2f", slPhysicsParamRotDamping->getValue());
        break;

    case IDC_IKRULE_CHOICE:
        UpdateIKRuleWindow();
        break;

    case IDC_IKRULE_CHAIN:
    case IDC_IKRULE_TOUCH:
    case IDC_IKRULE_ATTACHMENT:
    case IDC_IKRULE_RANGE_TOGGLE:
    case IDC_IKRULE_RANGE_START:
    case IDC_IKRULE_RANGE_PEAK:
    case IDC_IKRULE_RANGE_TAIL:
    case IDC_IKRULE_RANGE_END:
    case IDC_IKRULE_CONTACT_TOGGLE:
    case IDC_IKRULE_CONTACT_FRAME:
    case IDC_IKRULE_USING:
        BuildIKRuleQCString();
        break;

    case IDC_EVENT_SOUND_FRAME:
    case IDC_EVENT_SOUND_NAME:
        BuildEventQCString();
        break;

    case IDC_COMPILE_SELECTEDTOGGLE:
        bCompileSelectedToggle = !bCompileSelectedToggle;
        bCompileQCWhenSelected->setLabel(bCompileSelectedToggle ? "One-click compile [ON]" : "One-click compile [OFF]");
        break;

    case IDC_FLEXDEFAULTS:
    case IDC_FLEXZERO:
    case IDC_FLEXONE:
    case IDC_FLEXRANDOM:
        for (int i = 0; i < int(m_flexControllers.size()); i++)
            SetFlexController(i, event->action == IDC_FLEXONE ? 1.0f
                                 : event->action == IDC_FLEXRANDOM ? float(std::rand()) / float(RAND_MAX)
                                 : 0.0f);
        UpdateFlexSliders();
        break;

    default:
        if (event->action >= IDC_FLEX && event->action < IDC_FLEX + NUM_FLEX_SLIDERS)
        {
            const int index = cFlex[event->action - IDC_FLEX]->getSelectedIndex();
            if (index >= 0)
                slFlexScale[event->action - IDC_FLEX]->setValue(m_settings.flexRaw[index]);
        }
        else if (event->action >= IDC_FLEXSCALE && event->action < IDC_FLEXSCALE + NUM_FLEX_SLIDERS)
        {
            // SetFlexControllerRaw: the slider value is already normalized.
            const int index = cFlex[event->action - IDC_FLEXSCALE]->getSelectedIndex();
            if (index >= 0)
            {
                m_settings.flexRaw[index] = std::clamp(slFlexScale[event->action - IDC_FLEXSCALE]->getValue(), 0.0f, 1.0f);
                ++m_settings.flexRevision;
                redraw();
            }
        }
        else if (!anim())
            break;
        else if (event->action >= IDC_SEQUENCE0 && event->action < IDC_SEQUENCE0 + MAX_SEQUENCES ||
                 event->action >= IDC_SEQUENCESCALE0 && event->action < IDC_SEQUENCESCALE0 + MAX_SEQUENCES)
        {
            const int i = event->action < IDC_SEQUENCESCALE0 ? event->action - IDC_SEQUENCE0 : event->action - IDC_SEQUENCESCALE0;
            const int index = cSequence[i]->getSelectedIndex();
            const int sequence = index >= 0 ? GetSequenceForSelection(i, index) : 0;
            if (i == 0)
            {
                if (index >= 0)
                {
                    setSequence(sequence);
                    showActivityModifiers(sequence);
                }
            }
            else
                setOverlaySequence(i, sequence, slSequence[i]->getValue());
        }
        else if (event->action >= IDC_SEQUENCEFILTER0 && event->action < IDC_SEQUENCEFILTER0 + MAX_SEQUENCES)
        {
            SaveSelectedSequences();
            initSequenceChoices(event->action - IDC_SEQUENCEFILTER0);
            RestoreSelectedSequences();
        }
        else if (event->action >= IDC_FRAMESELECTION0 && event->action < IDC_FRAMESELECTION0 + MAX_SEQUENCES)
            updateFrameSelection();
        else if (event->action >= IDC_POSEPARAMETER && event->action < IDC_POSEPARAMETER + NUM_POSEPARAMETERS)
        {
            const int index = event->action - IDC_POSEPARAMETER;
            const int poseparam = cPoseParameter[index]->getSelectedIndex();
            float min, max;
            if (anim()->GetPoseParameterRange(poseparam, &min, &max))
            {
                slPoseParameter[index]->setRange(min, max, 1000);
                slPoseParameter[index]->setValue(anim()->GetPoseParameter(poseparam));
                lePoseParameter[index]->setLabel("%.1f", anim()->GetPoseParameter(poseparam));
            }
        }
        else if (event->action >= IDC_POSEPARAMETER_SCALE && event->action < IDC_POSEPARAMETER_SCALE + NUM_POSEPARAMETERS)
        {
            const int index = event->action - IDC_POSEPARAMETER_SCALE;
            setBlend(cPoseParameter[index]->getSelectedIndex(), slPoseParameter[index]->getValue());
            lePoseParameter[index]->setLabel("%.1f", slPoseParameter[index]->getValue());
        }
        else if (event->action >= IDC_POSEPARAMETER_VALUE && event->action < IDC_POSEPARAMETER_VALUE + NUM_POSEPARAMETERS)
        {
            // upstream applies the typed value on key press; the Qt line edit reports every edit
            const int index = event->action - IDC_POSEPARAMETER_VALUE;
            const int poseparam = cPoseParameter[index]->getSelectedIndex();
            bool ok = false;
            const float value = lePoseParameter[index]->text().toFloat(&ok);
            if (ok)
            {
                setBlend(poseparam, value);
                slPoseParameter[index]->setValue(anim()->GetPoseParameter(poseparam));
            }
        }
        // Remaining controls need physics or compilation, which the port lacks so far.
        break;
    }
    return 1;
}

void CBoneControlWindow::Init()
{
    int left = 5;
    const int top = 0;

    // Bone selection list
    new mxLabel(this, left + 3, top + 4, 30, 18, "Bone");
    m_cBone = new mxChoice(this, left, top + 20, 260, 22, IDC_BONE_BONELIST);
    m_cBone->add("None");
    m_cBone->select(0);
    mxToolTip::add(m_cBone, "Select a bone to modify");

    m_cBoneHighlight = new mxCheckBox(this, left, top + 45, 90, 20, "Highlight", IDC_BONE_HIGHLIGHT_BONE);
    mxToolTip::add(m_cBoneHighlight, "Toggle display of the bone being modified");

    m_cBoneNames = new mxCheckBox(this, left + 90, top + 45, 140, 20, "Names", IDC_BONE_NAMES);
    mxToolTip::add(m_cBoneNames, "Toggle bone names");

    new mxLabel(this, left + 3, top + 68, 100, 18, "Bone Surface Prop");
    m_cSurfaceProp = new mxChoice(this, left, top + 85, 140, 22, IDC_BONE_SURFACEPROP);
    mxToolTip::add(m_cSurfaceProp, "Select a surface property to apply to the bone");
    ReadPhysicsMaterials(m_cSurfaceProp);

    mxButton *btnApplyChild = new mxButton(this, left, top + 115, 140, 20, "Apply to Children", IDC_BONE_APPLY_TO_CHILDREN);
    mxToolTip::add(btnApplyChild, "Apply the surface property to all child bones");

    m_cAutoHitbox = new mxCheckBox(this, left, top + 140, 140, 20, "Autogenerate Hitboxes", IDC_BONE_USE_AUTOGENERATED_HITBOXES);
    mxToolTip::add(m_cAutoHitbox, "When this is checked, studiomdl will automatically generate hitboxes");

    m_cShowDefaultPose = new mxCheckBox(this, left, top + 160, 140, 20, "Show Default Pose", IDC_BONE_SHOW_DEFAULT_POSE);
    mxToolTip::add(m_cShowDefaultPose, "Toggles display of the default physics pose");

    left = 160;

    new mxLabel(this, left + 3, top + 50, 80, 18, "Hitbox Set");
    m_cHitboxSet = new mxChoice(this, left, top + 66, 100, 22, IDC_BONE_HITBOXSET);
    m_cHitboxSet->add("unnamed");
    m_cHitboxSet->select(0);
    mxToolTip::add(m_cHitboxSet, "Change hitbox set");

    m_eHitboxSetName = new mxLineEdit(this, left, top + 96, 100, 18, "", IDC_BONE_HITBOXSETNAME_EDIT);
    mxToolTip::add(m_eHitboxSetName, "Type in a name and hit the set button");

    m_bHitboxSetUpdateName = new mxButton(this, left, top + 116, 100, 16, "Set Name", IDC_BONE_HITBOXSETNAME);
    mxToolTip::add(m_bHitboxSetUpdateName, "Press to set hitbox set name from above text field");

    m_bAddHitboxSet = new mxButton(this, left, top + 140, 100, 20, "Add set", IDC_BONE_HITBOXADDSET);
    mxToolTip::add(m_bAddHitboxSet, "Add a hitbox set");
    m_bDeleteHitboxSet = new mxButton(this, left, top + 162, 100, 20, "Delete set", IDC_BONE_HITBOXDELETESET);
    mxToolTip::add(m_bDeleteHitboxSet, "Remove a hitbox set");

    left += 120;

    new mxLabel(this, left + 3, top + 4, 30, 18, "Hitbox");
    m_cHitbox = new mxChoice(this, left, top + 20, 100, 22, IDC_BONE_HITBOXLIST);
    m_cHitbox->add("None");
    m_cHitbox->select(0);
    mxToolTip::add(m_cHitbox, "Select a hitbox to modify");

    m_cHitboxHighlight = new mxCheckBox(this, left + 3, top + 45, 100, 20, "Highlight Hitbox", IDC_BONE_HIGHLIGHT_HITBOX);
    mxToolTip::add(m_cHitboxHighlight, "Toggle display of the hitbox being modified");

    new mxLabel(this, left + 3, top + 80, 80, 18, "Hitbox Group");
    m_eHitboxGroup = new mxLineEdit(this, left + 80, top + 75, 50, 22, "", IDC_BONE_HITBOX_GROUP);
    mxToolTip::add(m_eHitboxGroup, "The group of the current hitbox");

    new mxLabel(this, left + 133, top + 80, 80, 18, "Hitbox Name");
    m_eHitboxName = new mxLineEdit(this, left + 133 + 80, top + 75, 50, 22, "", IDC_BONE_HITBOX_NAME);
    m_eHitboxName->setEnabled(false);
    mxToolTip::add(m_eHitboxName, "The name of the current hitbox");

    new mxLabel(this, left + 3, top + 110, 80, 18, "Edit Hitbox:");
    m_cHitboxEditMode = new mxChoice(this, left + 80, top + 105, 200, 22, IDC_BONE_HITBOX_EDITMODE);
    m_cHitboxEditMode->add("Rotate");
    m_cHitboxEditMode->add("Translate BB Min");
    m_cHitboxEditMode->add("Translate BB Max");
    m_cHitboxEditMode->select(0);

    m_bUpdateHitbox = new mxButton(this, left, top + 163, 100, 20, "Update Hitbox", IDC_BONE_UPDATE_HITBOX);
    mxToolTip::add(m_bUpdateHitbox, "Apply hitbox group, origin, and size to the hitbox");

    mxButton *btnLoadHitbox = new mxButton(this, left + 110, top + 163, 70, 20, "Load .HBX", IDC_BONE_LOAD_HITBOXES);
    mxToolTip::add(btnLoadHitbox, "Load hitboxes from a .hbx file");

    mxButton *btnSaveHitbox = new mxButton(this, left + 190, top + 163, 70, 20, "Save .HBX", IDC_BONE_SAVE_HITBOXES);
    mxToolTip::add(btnSaveHitbox, "Save hitboxes to a .hbx file");

    left += 160;

    mxButton *btnGenerateQC = new mxButton(this, left, top + 10, 100, 20, "Generate QC", IDC_BONE_GENERATEQC);
    mxToolTip::add(btnGenerateQC, "Copy a .qc file snippet to the clipboard");

    m_bAddHitbox = new mxButton(this, left, top + 30, 100, 20, "Add Hitbox", IDC_BONE_ADD_HITBOX);
    m_bDeleteHitbox = new mxButton(this, left, top + 50, 100, 20, "Delete Hitbox", IDC_BONE_DELETE_HITBOX);
    mxToolTip::add(m_bAddHitbox, "Create a new hitbox attached to the current bone");
    mxToolTip::add(m_bDeleteHitbox, "Delete the currently selected hitbox");
}

std::vector<MdlHitbox> *CBoneControlWindow::CurrentSet()
{
    auto &sets = m_settings.hitboxSets;
    return m_settings.hitboxSet >= 0 && size_t(m_settings.hitboxSet) < sets.size() ? &sets[m_settings.hitboxSet].boxes : nullptr;
}

void CBoneControlWindow::OnLoadModel(const MdlMesh &mesh)
{
    m_bones = mesh.info.bones;
    m_surfaceProps = mesh.info.surfaceProps;
    m_parents = mesh.boneParents;
    // shortcut: lists "default" plus the model's own surface props; read surfaceproperties*.txt when vphysics is ported.
    QStringList props{QStringLiteral("default")};
    for (const auto &prop : m_surfaceProps)
        if (!props.contains(prop, Qt::CaseInsensitive))
            props.append(prop);
    m_cSurfaceProp->removeAll();
    for (const auto &prop : props)
        m_cSurfaceProp->add(prop.toUtf8().constData());

    m_cAutoHitbox->setChecked(mesh.info.autoHitboxes);
    OnAutogenerateHitboxes(mesh.info.autoHitboxes);
    PopulateBoneList(m_cBone, m_bones);
    ComputeHitboxSetList(0);
    OnBoneSelected(0);
}

void CBoneControlWindow::ComputeHitboxSetList(int select)
{
    m_cHitboxSet->removeAll();
    for (const auto &set : m_settings.hitboxSets)
        m_cHitboxSet->add(set.name.toUtf8().constData());
    m_cHitboxSet->select(select);
    m_settings.hitboxSet = select;
}

// upstream ComputeHitboxList: the hitboxes of the current set that hang off the selected bone.
void CBoneControlWindow::ComputeHitboxList()
{
    m_boneHitboxes.clear();
    m_cHitbox->removeAll();
    if (const auto *boxes = CurrentSet())
        for (size_t i = 0; i < boxes->size(); ++i)
            if ((*boxes)[i].bone == m_bone)
            {
                m_boneHitboxes.push_back(int(i));
                m_cHitbox->add(std::to_string(i).c_str());
            }
    if (m_boneHitboxes.empty())
        m_cHitbox->add("None");
    OnHitboxSelected(0);
}

void CBoneControlWindow::OnBoneSelected(int bone)
{
    if (bone < 0 || bone >= m_bones.size())
        return;
    m_bone = bone;
    const int prop = m_cSurfaceProp->findText(m_surfaceProps.value(bone), Qt::MatchFixedString);
    m_cSurfaceProp->select(std::max(prop, 0));
    ComputeHitboxList();
}

void CBoneControlWindow::OnHitboxSelected(int index)
{
    m_cHitbox->select(index);
    const auto *boxes = CurrentSet();
    m_hitbox = boxes && index >= 0 && size_t(index) < m_boneHitboxes.size() ? m_boneHitboxes[index] : -1;
    if (m_hitbox >= 0)
    {
        m_eHitboxGroup->setLabel("%d", (*boxes)[m_hitbox].group);
        m_eHitboxName->setText((*boxes)[m_hitbox].name.toUtf8().constData());
    }
    else
    {
        m_eHitboxGroup->setText("");
        m_eHitboxName->setText("");
    }
    UpdateHighlights();
}

void CBoneControlWindow::OnAutogenerateHitboxes(bool checked)
{
    for (QWidget *widget : std::initializer_list<QWidget *>{m_eHitboxGroup, m_eHitboxName, m_bUpdateHitbox, m_bAddHitbox, m_bDeleteHitbox,
                                                             m_cHitboxSet, m_eHitboxSetName, m_bHitboxSetUpdateName,
                                                             m_bAddHitboxSet, m_bDeleteHitboxSet})
        widget->setEnabled(!checked);
}

void CBoneControlWindow::UpdateHighlights()
{
    m_settings.highlightBone = m_cBoneHighlight->isChecked() ? m_bone : -1;
    // Without a selected hitbox, an out-of-range index dims every box, as upstream's numbones + 1.
    const auto *boxes = CurrentSet();
    m_settings.highlightHitbox = !m_cHitboxHighlight->isChecked() ? -1 : m_hitbox >= 0 ? m_hitbox : int(boxes ? boxes->size() : 0);
    m_panel->redraw();
}

// upstream SerializeQC: surface props (only where they differ from the parent) and, unless autogenerated, every hitbox set.
QString CBoneControlWindow::SerializeQC() const
{
    QString qc = QStringLiteral("// .qc block generated by HLMV begins.\n\n");
    qc += QStringLiteral("$surfaceprop \"%1\"\n").arg(m_surfaceProps.value(0));
    for (qsizetype i = 1; i < m_surfaceProps.size(); ++i)
        if (const int parent = m_parents[i]; parent < 0 || m_surfaceProps[i].compare(m_surfaceProps[parent], Qt::CaseInsensitive))
            qc += QStringLiteral("$jointsurfaceprop \"%1\"\t \"%2\"\n").arg(m_bones[i], m_surfaceProps[i]);
    if (!m_cAutoHitbox->isChecked())
    {
        qc += QLatin1Char('\n');
        for (const auto &set : m_settings.hitboxSets)
        {
            qc += QStringLiteral("\n$hboxset \"%1\"\n\n").arg(set.name);
            for (const auto &box : set.boxes)
            {
                char numbers[256];
                std::snprintf(numbers, sizeof(numbers), "  %7.2f %7.2f %7.2f  %7.2f %7.2f %7.2f  %7.2f %7.2f %7.2f %7.2f",
                              box.min.x(), box.min.y(), box.min.z(), box.max.x(), box.max.y(), box.max.z(),
                              box.angles.x(), box.angles.y(), box.angles.z(), box.radius);
                qc += QStringLiteral("$hbox %1 \"%2\"\t").arg(box.group).arg(m_bones.value(box.bone)) + QLatin1String(numbers);
                if (!box.name.isEmpty())
                    qc += QStringLiteral(" \"%1\"").arg(box.name);
                qc += QLatin1Char('\n');
            }
        }
    }
    qc += QStringLiteral("\n// .qc block generated by HLMV ends.\n\n");
    return qc;
}

int CBoneControlWindow::handleEvent(mxEvent *event)
{
    if (event->event != mxEvent::Action)
        return 0;
    const bool loaded = !m_bones.isEmpty();
    auto *boxes = CurrentSet();
    switch (event->action)
    {
    case IDC_BONE_BONELIST:
        OnBoneSelected(m_cBone->getSelectedIndex());
        break;

    case IDC_BONE_NAMES:
        m_settings.showBoneNames = m_cBoneNames->isChecked();
        m_panel->redraw();
        break;

    case IDC_BONE_HIGHLIGHT_BONE:
    case IDC_BONE_HIGHLIGHT_HITBOX:
        UpdateHighlights();
        break;

    case IDC_BONE_HITBOXLIST:
        OnHitboxSelected(m_cHitbox->getSelectedIndex());
        break;

    case IDC_BONE_HITBOXSET:
        m_settings.hitboxSet = m_cHitboxSet->getSelectedIndex();
        ComputeHitboxList();
        break;

    case IDC_BONE_HITBOXADDSET:
        if (loaded)
        {
            m_settings.hitboxSets.push_back({QString::asprintf("set%02d", int(m_settings.hitboxSets.size()) + 1), {}});
            ComputeHitboxSetList(int(m_settings.hitboxSets.size()) - 1);
            ComputeHitboxList();
        }
        break;

    case IDC_BONE_HITBOXDELETESET:
        if (m_settings.hitboxSet > 0) // upstream never deletes the first set
        {
            m_settings.hitboxSets.erase(m_settings.hitboxSets.begin() + m_settings.hitboxSet);
            ComputeHitboxSetList(0);
            ComputeHitboxList();
        }
        break;

    case IDC_BONE_HITBOXSETNAME:
        if (boxes && !m_eHitboxSetName->text().isEmpty())
        {
            const int set = m_settings.hitboxSet;
            m_settings.hitboxSets[set].name = m_eHitboxSetName->text();
            ComputeHitboxSetList(set);
        }
        break;

    case IDC_BONE_HITBOX_GROUP:
    case IDC_BONE_UPDATE_HITBOX:
        if (boxes && m_hitbox >= 0)
        {
            auto &box = (*boxes)[m_hitbox];
            box.group = m_eHitboxGroup->text().toInt();
            if (event->action == IDC_BONE_UPDATE_HITBOX)
                box.name = m_eHitboxName->text();
            m_panel->redraw();
        }
        break;

    case IDC_BONE_ADD_HITBOX:
        if (boxes && loaded)
        {
            boxes->push_back({m_bone, 0, QVector3D(-8, -8, -8), QVector3D(8, 8, 8), QVector3D(), 0, QString()});
            ComputeHitboxList();
            OnHitboxSelected(int(m_boneHitboxes.size()) - 1);
        }
        break;

    case IDC_BONE_DELETE_HITBOX:
        if (boxes && m_hitbox >= 0)
        {
            boxes->erase(boxes->begin() + m_hitbox);
            ComputeHitboxList();
        }
        break;

    case IDC_BONE_USE_AUTOGENERATED_HITBOXES:
        OnAutogenerateHitboxes(m_cAutoHitbox->isChecked());
        break;

    case IDC_BONE_SURFACEPROP:
        if (m_bone < m_surfaceProps.size())
            m_surfaceProps[m_bone] = m_cSurfaceProp->getLabel();
        break;

    case IDC_BONE_APPLY_TO_CHILDREN:
        // Bones are stored parent-first, so one forward pass reaches every descendant.
        for (qsizetype i = m_bone + 1; i < m_surfaceProps.size(); ++i)
            for (int parent = m_parents[i]; parent >= 0; parent = m_parents[parent])
                if (parent == m_bone)
                {
                    m_surfaceProps[i] = m_surfaceProps[m_bone];
                    break;
                }
        break;

    case IDC_BONE_GENERATEQC:
        if (loaded)
            QGuiApplication::clipboard()->setText(SerializeQC());
        break;

    default:
        // Show Default Pose needs physics; Edit Hitbox needs the viewport drag widget; .HBX needs the datamodel.
        return 0;
    }
    return 1;
}

void CAttachmentsWindow::Init()
{
    int left = 5, top = 0;

    new mxLabel(this, left + 3, top + 4, 60, 18, "Attachment");
    m_cAttachmentList = new mxListBox(this, left, top + 20, 260, 100, IDC_ATTACHMENT_LIST);
    m_cAttachmentList->add("None");
    m_cAttachmentList->select(0);
    mxToolTip::add(m_cAttachmentList, "Select an attachment to modify");

    left = 280;
    new mxLabel(this, left + 3, top + 4, 60, 18, "Attach To Bone");
    m_cBoneList = new mxListBox(this, left, top + 20, 260, 100, IDC_ATTACHMENT_LIST_BONES);
    m_cBoneList->add("None");
    m_cBoneList->select(0);
    mxToolTip::add(m_cBoneList, "Select a bone to attach to");

    left = 5;
    top = 120;
    new mxLabel(this, left + 3, top + 4, 60, 18, "Translation");
    m_cTranslation = new mxLineEdit2(this, left + 70, top, 90, 25, "10 20 30", IDC_ATTACHMENT_TRANSLATION);

    left = 170;
    new mxLabel(this, left + 3, top + 4, 60, 18, "Rotation");
    m_cRotation = new mxLineEdit2(this, left + 70, top, 90, 25, "0 90 180", IDC_ATTACHMENT_ROTATION);

    top = 145;
    left = 5;
    new mxLabel(this, left, top, 60, 18, "QC String");
    m_cQCString = new mxLineEdit2(this, left + 70, top, 400, 25, "$attachment \"controlpanel0_ur\" \"Vgui\" -22 -15 4 rotate 0 0 0",
                                  IDC_ATTACHMENT_QC_STRING);

    // Nudge buttons: translate X/Y/Z then rotate X/Y/Z, each large/small minus and plus.
    static const char *const labels[6][4] = {
        {"-X", "-x", "+x", "+X"}, {"-Y", "-y", "+y", "+Y"}, {"-Z", "-z", "+z", "+Z"},
        {"-XR", "-xr", "+xr", "+XR"}, {"-YR", "-yr", "+yr", "+YR"}, {"-ZR", "-zr", "+zr", "+ZR"},
    };
    static const int offsets[4] = {0, 30, 70, 100};
    top = 5;
    left = 550;
    for (int row = 0; row < 6; ++row)
    {
        for (int i = 0; i < 4; ++i)
            new mxButton(this, left + offsets[i], top, 30, 20, labels[row][i], IDC_ATTACHMENT_NUDGE);
        top += row == 2 ? 30 : 20;
    }
}

void CAttachmentsWindow::OnLoadModel(const MdlInfo &info)
{
    Fill(m_cAttachmentList, info.attachments, "None");
    Fill(m_cBoneList, info.bones, "None");
}
