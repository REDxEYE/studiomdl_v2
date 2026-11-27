// Qt port of upstream/controlpanel.cpp and upstream/attachments_window.cpp. Control IDs match upstream.

#include "controlpanel.h"
#include "studio_anim.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include <QClipboard>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QOpenGLWidget>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QVBoxLayout>

#include "mdl_mesh.h"
#include "viewport.h"

namespace {
    void AddScrollableTab(mxTab *tabs, QWidget *page, const char *title) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        tabs->add(scroll, title);
    }

    mxWindow *AddScrollableTab(mxWindow *owner, mxTab *tabs, const char *title) {
        auto *page = new mxWindow(owner, 0, 0, 0, 0);
        AddScrollableTab(tabs, page, title);
        return page;
    }

    // Don't let the longest model-supplied name determine the width of the whole panel.
    void FitModelChoice(mxChoice *choice) {
        choice->setMinimumContentsLength(12);
        choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    }

    // upstream ReadPhysicsMaterials without vphysics: surface props are not loaded yet.
    void ReadPhysicsMaterials(mxChoice* plist) {
        plist->removeAll();
        plist->add("default");
        plist->select(0);
    }

    bool PopulateBoneList(mxChoice* pChoice, const QStringList &bones, bool bAlwaysAddNone = false) {
        pChoice->removeAll();
        if (bAlwaysAddNone)
            pChoice->add("(None)");
        for (const auto &bone: bones)
            pChoice->add(bone.toUtf8().constData());
        if (bones.isEmpty() && !bAlwaysAddNone)
            pChoice->add("(None)");
        pChoice->select(0);
        return !bones.isEmpty();
    }

    void Fill(mxListBox* list, const QStringList &items, const char* empty) {
        list->removeAll();
        if (items.isEmpty())
            list->add(empty);
        for (const auto &item: items)
            list->add(item.toUtf8().constData());
    }
} // namespace

ControlPanel::ControlPanel(mxWindow* parent, ViewportSettings &settings)
    : mxWindow(parent, 0, 0, 0, 0, "Control Panel", mxWindow::Normal), m_settings(settings) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *frame = new QHBoxLayout;
    frame->addWidget(new mxLabel(this, 0, 0, 0, 0, "Frame"));
    slForceFrame = new mxSlider(this, 0, 0, 0, 0, IDC_FORCEFRAME);
    slForceFrame->setRange(0, 1.0);
    slForceFrame->setValue(0.0);
    slForceFrame->setSteps(1, 1);
    mxToolTip::add(slForceFrame, "Force To Frame");
    lForcedFrame = new mxLabel(this, 0, 0, 0, 0, "0");
    frame->addWidget(slForceFrame, 1);
    frame->addWidget(lForcedFrame);
    tab = new mxTab(this, 0, 0, 0, 0, IDC_TAB);
    layout->addWidget(tab, 1);
    layout->addLayout(frame);

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

void ControlPanel::SetupRenderWindow(mxTab* pTab) {
    wRender = AddScrollableTab(this, pTab, "Render");
    cRenderMode = new mxChoice(wRender, 0, 0, 0, 0, IDC_RENDERMODE);
    cRenderMode->add("Wireframe");
    cRenderMode->add("Smoothshaded");
    cRenderMode->add("Textured");
    cRenderMode->add("BoneWeights");
    cRenderMode->add("BadVertexData");
    cRenderMode->add("UV Chart");
    cRenderMode->add("Co-LocatedVerts");
    cRenderMode->select(2);
    mxToolTip::add(cRenderMode, "Select Render Mode");

    auto *grid = new QGridLayout(wRender);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    const auto check = [&](int row, int col, const char *label, int id) {
        auto *box = new mxCheckBox(wRender, 0, 0, 0, 0, label, id);
        grid->addWidget(box, row, col);
        return box;
    };
    grid->addWidget(cRenderMode, 0, 0);
    cbNormalMap = check(1, 0, "Normal Mapping", IDC_NORMALMAP);
    cbAttachments = check(2, 0, "Attachments (Ctrl-A)", IDC_ATTACHMENTS);
    cbBones = check(3, 0, "Bones (Ctrl-O)", IDC_BONES);
    cbNormals = check(4, 0, "Normals (Ctrl-N)", IDC_NORMALS);
    cbTangentFrame = check(5, 0, "Tangents (Ctrl-T)", IDC_TANGENTFRAME);
    cbOverlayWireframe = check(6, 0, "Wireframe (Ctrl-W)", IDC_OVERLAY_WIREFRAME);
    cbSpecular = check(7, 0, "Specular", IDC_SPECULAR);
    cbSpecular->setChecked(true);

    cbGround = check(0, 1, "Ground (Ctrl-G)", IDC_GROUND);
    cbMovement = check(1, 1, "Movement (Ctrl-M)", IDC_MOVEMENT);
    cbBackground = check(2, 1, "Background (Ctrl-B)", IDC_BACKGROUND);
    cbHitBoxes = check(3, 1, "Hit Boxes (Ctrl-H)", IDC_HITBOXES);
    cbSequenceBoxes = check(4, 1, "Seq. Boxes", IDC_SEQUENCEBOXES);
    cbShadow = check(5, 1, "Shadow (Ctrl-S)", IDC_SHADOW);
    cbSoftwareSkin = check(6, 1, "Software Skin", IDC_SOFTWARESKIN);
    cbOverbright2 = check(7, 1, "Enable Overbrightening", IDC_OVERBRIGHT2);
    cbOverbright2->setChecked(true);

    check(0, 2, "Physics Model", IDC_PHYSICSMODEL);
    cHighlightBone = new mxChoice(wRender, 0, 0, 0, 0, IDC_PHYSICSHIGHLIGHT);
    FitModelChoice(cHighlightBone);
    cHighlightBone->add("None");
    cHighlightBone->select(0);
    mxToolTip::add(cHighlightBone, "Select Physics Bone to highlight");
    grid->addWidget(cHighlightBone, 1, 2);
    cbDisplacementMap = check(2, 2, "Displacement (Ctrl-D)", IDC_DISPLACEMENTMAP);
    cbDisplacementMap->setChecked(true);
    cbRunIK = check(3, 2, "Enable IK", IDC_RUNIK);
    cbEnableHead = check(4, 2, "Head Turn", IDC_HEADTURN);
    cbIllumPosition = check(5, 2, "Illum. Position", IDC_ILLUMPOSITION);
    cbPlaySounds = check(6, 2, "Play Sounds", IDC_PLAYSOUNDS);
    cbShowOriginAxis = check(7, 2, "Show Origin Axis", IDC_SHOWORIGINAXIS);

    auto *inspect = new QGridLayout;
    grid->addLayout(inspect, 0, 3, 9, 1, Qt::AlignTop);
    inspect->addWidget(new mxLabel(wRender, 0, 0, 0, 0, "HitBox Set:"), 0, 0);
    cDrawHitBoxSet = new mxChoice(wRender, 0, 0, 0, 0, IDC_DRAWHITBOXSET);
    FitModelChoice(cDrawHitBoxSet);
    cDrawHitBoxSet->add("All");
    cDrawHitBoxSet->select(0);
    cDrawHitBoxSet->setEnabled(false);
    inspect->addWidget(cDrawHitBoxSet, 0, 1);
    inspect->addWidget(new mxLabel(wRender, 0, 0, 0, 0, "HitBox:"), 1, 0);
    cDrawHitBoxNumber = new mxChoice(wRender, 0, 0, 0, 0, IDC_DRAWHITBOXNUMBER);
    FitModelChoice(cDrawHitBoxNumber);
    cDrawHitBoxNumber->setEnabled(false);
    inspect->addWidget(cDrawHitBoxNumber, 1, 1);
    cIncludedModels = new mxChoice(wRender, 0, 0, 0, 0, IDC_INCLUDEDMODELS);
    FitModelChoice(cIncludedModels);
    cIncludedModels->add("Included Models");
    cIncludedModels->add("---------------");
    cIncludedModels->setEnabled(false);
    inspect->addWidget(cIncludedModels, 2, 0, 1, 2);
    inspect->addWidget(new mxLabel(wRender, 0, 0, 0, 0, "Inspect bone weights:"), 3, 0, 1, 2);
    cbBoneWeightInspectIndex = new mxChoice(wRender, 0, 0, 0, 0, IDC_BONEWEIGHTINDEX);
    FitModelChoice(cbBoneWeightInspectIndex);
    for (int i = 0; i < 32766; ++i)
        cbBoneWeightInspectIndex->add(std::to_string(i).c_str());
    inspect->addWidget(cbBoneWeightInspectIndex, 4, 0, 1, 2);
    lblBoneWeightInspectValues = new mxLabel(wRender, 0, 0, 0, 0, "");
    inspect->addWidget(lblBoneWeightInspectValues, 5, 0, 1, 2);
    inspect->addWidget(new mxCheckBox(wRender, 0, 0, 0, 0, "Enable Eye Look", IDC_EYELOOK), 6, 0, 1, 2);

    auto *fov = new QHBoxLayout;
    fov->addWidget(new mxLabel(wRender, 0, 0, 0, 0, "FOV:"));
    leFOV = new mxLineEdit(wRender, 0, 0, 0, 0, "65", IDC_RENDER_FOV);
    fov->addWidget(leFOV);
    grid->addLayout(fov, 8, 0);
    auto *axis = new QHBoxLayout;
    axis->addWidget(new mxLabel(wRender, 0, 0, 0, 0, "Axis Len:"));
    leOriginAxisLength = new mxSlider(wRender, 0, 0, 0, 0, IDC_ORIGINAXISLENGTH);
    leOriginAxisLength->setRange(1, 100);
    leOriginAxisLength->setValue(10);
    axis->addWidget(leOriginAxisLength, 1);
    grid->addLayout(axis, 8, 2);
}

void ControlPanel::SetupSequenceWindow(mxTab* pTab) {
    auto *wSequence = AddScrollableTab(this, pTab, "Sequence");
    auto *layout = new QHBoxLayout(wSequence);
    layout->setContentsMargins(8, 5, 8, 5);
    auto *sequences = new QGridLayout;
    auto *pose = new QGridLayout;
    layout->addLayout(sequences, 3);
    layout->addLayout(pose, 2);
    sequences->setAlignment(Qt::AlignTop);
    pose->setAlignment(Qt::AlignTop);
    // Match the eight pose rows without shrinking the controls below their native height.
    sequences->setVerticalSpacing(2);
    pose->setVerticalSpacing(2);
    sequences->setColumnStretch(0, 2);
    sequences->setColumnStretch(1, 1);
    pose->setColumnMinimumWidth(1, 40); // Leave room to drag the handle at the minimum window width.
    pose->setColumnStretch(1, 1);

    for (int i = 0; i < MAX_SEQUENCES; i++) {
        cSequence[i] = new mxChoice(wSequence, 0, 0, 0, 0, IDC_SEQUENCE0 + i);
        FitModelChoice(cSequence[i]);
        cSequence[i]->setMinimumContentsLength(9);
        mxToolTip::add(cSequence[i], "Select Sequence");
        slSequence[i] = new mxSlider(wSequence, 0, 0, 0, 0, IDC_SEQUENCESCALE0 + i);
        slSequence[i]->setRange(0, 1.0, 100);
        slSequence[i]->setValue(0.0);
        rbFrameSelection[i] = new mxRadioButton(wSequence, 0, 0, 0, 0, "", IDC_FRAMESELECTION0 + i, i == 0);
        leSequenceFilter[i] = new mxLineEdit(wSequence, 0, 0, 0, 0, "", IDC_SEQUENCEFILTER0 + i);
        leSequenceFilter[i]->setPlaceholderText("Filter");
        leSequenceFilter[i]->setAccessibleName("Filter sequence");
        sequences->addWidget(cSequence[i], i, 0);
        sequences->addWidget(slSequence[i], i, 1);
        sequences->addWidget(rbFrameSelection[i], i, 2);
        sequences->addWidget(leSequenceFilter[i], i, 3);
    }
    rbFrameSelection[0]->setChecked(true);

    slSequence[0]->setVisible(false);
    laGroundSpeed = new mxLabel(wSequence, 0, 0, 0, 0, "");
    sequences->addWidget(laGroundSpeed, 0, 1);

    for (int i = 0; i < NUM_POSEPARAMETERS; i++) {
        cPoseParameter[i] = new mxChoice(wSequence, 0, 0, 0, 0, IDC_POSEPARAMETER + i);
        FitModelChoice(cPoseParameter[i]);
        cPoseParameter[i]->setMinimumContentsLength(9);
        cPoseParameter[i]->setVisible(false);
        slPoseParameter[i] = new mxSlider(wSequence, 0, 0, 0, 0, IDC_POSEPARAMETER_SCALE + i);
        slPoseParameter[i]->setRange(0.0, 1.0, 1000);
        mxToolTip::add(slPoseParameter[i], "Parameter");
        slPoseParameter[i]->setVisible(false);
        lePoseParameter[i] = new mxLineEdit(wSequence, 0, 0, 0, 0, "X", IDC_POSEPARAMETER_VALUE + i);
        lePoseParameter[i]->setVisible(false);
        pose->addWidget(cPoseParameter[i], i, 0);
        pose->addWidget(slPoseParameter[i], i, 1);
        pose->addWidget(lePoseParameter[i], i, 2);
    }

    auto *timing = new QHBoxLayout;
    timing->setSpacing(4);
    slSpeedScale = new mxSlider(wSequence, 0, 0, 0, 0, IDC_SPEEDSCALE);
    slSpeedScale->setRange(0, 1.0, 100);
    slSpeedScale->setValue(1.0);
    mxToolTip::add(slSpeedScale, "Speed Scale");
    laFPS = new mxLabel(wSequence, 0, 0, 0, 0, "");
    timing->addWidget(slSpeedScale, 1);
    timing->addWidget(laFPS);
    slBlendTime = new mxSlider(wSequence, 0, 0, 0, 0, IDC_BLENDTIME);
    slBlendTime->setRange(0, 1.0, 100);
    slBlendTime->setValue(DEFAULT_BLEND_TIME);
    mxToolTip::add(slBlendTime, "Blend time");
    laBlendTime = new mxLabel(wSequence, 0, 0, 0, 0, "");
    laBlendTime->setLabel("%.2f s", slBlendTime->getValue());
    timing->addWidget(new mxLabel(wSequence, 0, 0, 0, 0, "Blend time:"));
    timing->addWidget(slBlendTime, 1);
    timing->addWidget(laBlendTime);
    sequences->addLayout(timing, MAX_SEQUENCES, 0, 1, 4);

    auto *blend = new QHBoxLayout;
    blend->setSpacing(4);
    blend->addWidget(new mxCheckBox(wSequence, 0, 0, 0, 0, "Blend changes", IDC_BLENDSEQUENCECHANGES));
    blend->addWidget(new mxButton(wSequence, 0, 0, 0, 0, "Blend Now", IDC_BLENDNOW));
    laBlendAmount = new mxLabel(wSequence, 0, 0, 0, 0, "");
    blend->addWidget(laBlendAmount);
    blend->addStretch();
    sequences->addLayout(blend, MAX_SEQUENCES + 1, 0, 1, 4);

    auto *activity = new QHBoxLayout;
    activity->setSpacing(4);
    activity->addWidget(new mxLabel(wSequence, 0, 0, 0, 0, "Modifiers:"));
    cActivityModifiers = new mxChoice(wSequence, 0, 0, 0, 0, IDC_ACTIVITY_MODIFIERS);
    FitModelChoice(cActivityModifiers);
    cActivityModifiers->setMinimumContentsLength(9);
    cActivityModifiers->setAccessibleName("Activity modifiers");
    activity->addWidget(cActivityModifiers, 1);
    activity->addWidget(new mxCheckBox(wSequence, 0, 0, 0, 0, "Animate weapons", IDC_ANIMATEWEAPONS));
    sequences->addLayout(activity, MAX_SEQUENCES + 2, 0, 1, 4);
}

void ControlPanel::SetupBodyWindow(mxTab* pTab) {
    auto *wBody = AddScrollableTab(this, pTab, "Model");
    auto *layout = new QHBoxLayout(wBody);
    auto *controls = new QGridLayout;
    auto *info = new QGridLayout;
    auto *materials = new QGridLayout;
    layout->addLayout(controls, 1);
    layout->addLayout(info);
    layout->addLayout(materials, 2);
    controls->setAlignment(Qt::AlignTop);
    info->setAlignment(Qt::AlignTop);
    cBodypart = new mxChoice(wBody, 0, 0, 0, 0, IDC_BODYPART);
    mxToolTip::add(cBodypart, "Choose a bodypart");
    cSubmodel = new mxChoice(wBody, 0, 0, 0, 0, IDC_SUBMODEL);
    mxToolTip::add(cSubmodel, "Choose a submodel of current bodypart");

    cBodyGroupPreset = new mxChoice(wBody, 0, 0, 0, 0, IDC_BODYGROUPPRESET);
    mxToolTip::add(cBodyGroupPreset, "Choose a bodygroup preset");

    cController = new mxChoice(wBody, 0, 0, 0, 0, IDC_CONTROLLER);
    mxToolTip::add(cController, "Choose a bone controller");
    slController = new mxSlider(wBody, 0, 0, 0, 0, IDC_CONTROLLERVALUE);
    slController->setRange(0, 255);
    mxToolTip::add(slController, "Change current bone controller value");
    lModelInfo1 = new mxLabel(wBody, 0, 0, 0, 0, "No Model.");
    lModelInfo2 = new mxLabel(wBody, 0, 0, 0, 0, "");
    cSkin = new mxChoice(wBody, 0, 0, 0, 0, IDC_SKINS);
    mxToolTip::add(cSkin, "Choose a skin family");
    cMaterials = new mxChoice(wBody, 0, 0, 0, 0, IDC_MATERIALS);
    mxToolTip::add(cMaterials, "Select material for UV Chart view");

    lModelInfo3 = new mxLabel(wBody, 0, 0, 0, 0, "");
    lModelInfo4 = new mxLabel(wBody, 0, 0, 0, 0, "");
    m_settings.reportStats = [this](const ViewportStats &stats) {
        lModelInfo3->setLabel("Shader Draw Count: %d\nVerts: %d  Indexes: %d  Triangles: %d", stats.drawCalls,
                              stats.vertices,
                              stats.indices, stats.indices / 3);
        lModelInfo4->setLabel("Model is: %s", stats.transparent ? "transparent" : "opaque");
    };

    cbAutoLOD = new mxCheckBox(wBody, 0, 0, 0, 0, "Auto LOD", IDC_AUTOLOD);
    cLODChoice = new mxChoice(wBody, 0, 0, 0, 0, IDC_LODCHOICE);
    mxToolTip::add(cLODChoice, "Select model LOD to render");
    leLODSwitch = new mxLineEdit(wBody, 0, 0, 0, 0, "", IDC_LODSWITCH);
    lLODMetric = new mxLabel(wBody, 0, 0, 0, 0, "");
    for (auto *choice : {cBodypart, cSubmodel, cController, cSkin, cBodyGroupPreset, cLODChoice, cMaterials})
        FitModelChoice(choice);
    controls->addWidget(cBodypart, 0, 0);
    controls->addWidget(cSubmodel, 0, 1);
    controls->addWidget(cController, 1, 0);
    controls->addWidget(slController, 1, 1);
    controls->addWidget(cSkin, 2, 0);
    controls->addWidget(cBodyGroupPreset, 2, 1);
    controls->addWidget(cbAutoLOD, 3, 0);
    controls->addWidget(cLODChoice, 3, 1);
    controls->addWidget(new mxLabel(wBody, 0, 0, 0, 0, "LOD Switch:"), 4, 0);
    controls->addWidget(leLODSwitch, 4, 1);
    controls->addWidget(new mxLabel(wBody, 0, 0, 0, 0, "LOD Metric:"), 5, 0);
    controls->addWidget(lLODMetric, 5, 1);
    controls->addWidget(new mxLabel(wBody, 0, 0, 0, 0, "Materials used:"), 6, 0, 1, 2);
    controls->addWidget(cMaterials, 7, 0, 1, 2);
    info->addWidget(lModelInfo1, 0, 0, Qt::AlignTop);
    info->addWidget(lModelInfo2, 0, 1, Qt::AlignTop);
    info->addWidget(lModelInfo3, 1, 0, 1, 2);
    info->addWidget(lModelInfo4, 2, 0, 1, 2);

    materials->addWidget(new mxLabel(wBody, 0, 0, 0, 0, "VMTs Loaded:"), 0, 0);
    cMessageList = new mxListBox(wBody, 0, 0, 0, 0, IDC_MESSAGES);
    cMessageList->add("None");
    cMessageList->select(1);
    mxToolTip::add(cMessageList, "Materials (VMT files) this model has loaded");

    m_bExploreToMaterial = new mxButton(wBody, 0, 0, 0, 0, "Explore to material", IDC_EXPLORE_TO_VMT);
    materials->addWidget(cMessageList, 1, 0);
    materials->addWidget(m_bExploreToMaterial, 2, 0, 1, 2, Qt::AlignLeft);
    materials->addWidget(new mxLabel(wBody, 0, 0, 0, 0, "Shader:"), 0, 1);
    cShaderUsed = new mxListBox(wBody, 0, 0, 0, 0, IDC_SHADERS);
    cShaderUsed->add("Select material to show shader");
    cShaderUsed->select(0);
    mxToolTip::add(cShaderUsed, "Shader Used");

    materials->addWidget(cShaderUsed, 1, 1);
    m_bRandomizeWeaponModuleSlots = new mxButton(wBody, 0, 0, 0, 0, "Roll bodygroups", IDC_ROLL_BODYGROUPS);
    info->addWidget(m_bRandomizeWeaponModuleSlots, 3, 0, 1, 2, Qt::AlignLeft);
    mxToolTip::add(m_bRandomizeWeaponModuleSlots, "Randomize model bodygroup selections");
}

void ControlPanel::SetupFlexWindow(mxTab* pTab) {
    auto *wFlex = AddScrollableTab(this, pTab, "Flex");
    auto *layout = new QVBoxLayout(wFlex);
    auto *grid = new QGridLayout;
    layout->addLayout(grid);
    constexpr int rows = 8;
    for (int i = 0; i < NUM_FLEX_SLIDERS; ++i) {
        const int column = 2 * (i / rows);
        lFlex[i] = new mxLabel(wFlex, 0, 0, 0, 0, "");
        slFlexScale[i] = new mxSlider(wFlex, 0, 0, 0, 0, IDC_FLEXSCALE + i);
        grid->addWidget(lFlex[i], i % rows, column, Qt::AlignVCenter);
        grid->addWidget(slFlexScale[i], i % rows, column + 1);
        grid->setColumnStretch(column + 1, 1);
        slFlexScale[i]->setRange(0, 1.0);
        slFlexScale[i]->setValue(0);
        mxToolTip::add(slFlexScale[i], "Flex Scale");
    }

    auto *pager = new QHBoxLayout;
    pager->addWidget(new mxButton(wFlex, 0, 0, 0, 0, "Reset", IDC_FLEXDEFAULTS));
    pager->addWidget(new mxButton(wFlex, 0, 0, 0, 0, "Random", IDC_FLEXRANDOM));
    pager->addWidget(new mxButton(wFlex, 0, 0, 0, 0, "Zero", IDC_FLEXZERO));
    pager->addWidget(new mxButton(wFlex, 0, 0, 0, 0, "One", IDC_FLEXONE));
    bFlexPrev = new mxButton(wFlex, 0, 0, 0, 0, "<", IDC_FLEXPREV);
    lFlexPage = new mxLabel(wFlex, 0, 0, 0, 0, "");
    bFlexNext = new mxButton(wFlex, 0, 0, 0, 0, ">", IDC_FLEXNEXT);
    pager->addWidget(bFlexPrev);
    pager->addWidget(lFlexPage);
    pager->addWidget(bFlexNext);
    pager->addStretch();
    layout->addLayout(pager);
    layout->addStretch();
}

void ControlPanel::SetupPhysicsWindow(mxTab* pTab) {
    auto *wPhysics = AddScrollableTab(this, pTab, "Physics");

    auto *mass = new mxLabel(wPhysics, 0, 0, 0, 0, "Mass");
    leMass = new mxLineEdit(wPhysics, 0, 0, 0, 0, "", IDC_PHYS_MASS);

    cPhysicsBone = new mxChoice(wPhysics, 0, 0, 0, 0, IDC_PHYS_BONE);
    cPhysicsBone->add("None");
    cPhysicsBone->select(0);

    auto *highlight = new mxCheckBox(wPhysics, 0, 0, 0, 0, "Highlight", IDC_PHYSICSMODEL);

    rbConstraintAxis[0] = new mxRadioButton(wPhysics, 0, 0, 0, 0, "X", IDC_PHYS_CON_AXIS_X, true);
    rbConstraintAxis[1] = new mxRadioButton(wPhysics, 0, 0, 0, 0, "Y", IDC_PHYS_CON_AXIS_Y);
    rbConstraintAxis[2] = new mxRadioButton(wPhysics, 0, 0, 0, 0, "Z", IDC_PHYS_CON_AXIS_Z);
    rbConstraintAxis[0]->setChecked(true); // setPhysicsAxis( 0 )

    auto *friction = new mxLabel(wPhysics, 0, 0, 0, 0, "Friction");
    slPhysicsFriction = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_CON_FRICTION);
    slPhysicsFriction->setRange(0, 1000.0, 1000);
    slPhysicsFriction->setValue(0);
    slPhysicsFriction->setSteps(1, 1);
    lPhysicsFriction = new mxLabel(wPhysics, 0, 0, 0, 0, "0");

    auto *minimum = new mxLabel(wPhysics, 0, 0, 0, 0, "Min");
    slPhysicsConMin = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_CON_MIN);
    slPhysicsConMin->setRange(-180, 180.0, 360);
    slPhysicsConMin->setValue(-90);
    slPhysicsConMin->setSteps(1, 1);
    lPhysicsConMin = new mxLabel(wPhysics, 0, 0, 0, 0, "-90");

    auto *maximum = new mxLabel(wPhysics, 0, 0, 0, 0, "Max");
    slPhysicsConMax = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_CON_MAX);
    slPhysicsConMax->setRange(-180.0, 180.0, 360);
    slPhysicsConMax->setValue(90.0);
    slPhysicsConMax->setSteps(1, 1);
    lPhysicsConMax = new mxLabel(wPhysics, 0, 0, 0, 0, "90");

    auto *test = new mxLabel(wPhysics, 0, 0, 0, 0, "Test");
    slPhysicsConTest = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_CON_TEST);
    slPhysicsConTest->setRange(0, 1.0, 100);
    slPhysicsConTest->setValue(0);

    cbLinked = new mxCheckBox(wPhysics, 0, 0, 0, 0, "Link", IDC_PHYS_CON_LINK_LIMITS);
    mxToolTip::add(cbLinked, "Link mins/maxs to be symmetric");
    auto *generate = new mxButton(wPhysics, 0, 0, 0, 0, "Generate QC", IDC_PHYS_QCFILE);

    auto *massBias = new mxLabel(wPhysics, 0, 0, 0, 0, "Mass Bias");
    auto *inertia = new mxLabel(wPhysics, 0, 0, 0, 0, "Inertia");
    auto *damping = new mxLabel(wPhysics, 0, 0, 0, 0, "Damping");
    auto *rotDamping = new mxLabel(wPhysics, 0, 0, 0, 0, "Rot Damp");
    auto *material = new mxLabel(wPhysics, 0, 0, 0, 0, "Material");

    slPhysicsParamMassBias = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_P_MASSBIAS);
    slPhysicsParamMassBias->setRange(0, 10.0, 100);
    slPhysicsParamMassBias->setValue(1.0);
    slPhysicsParamMassBias->setSteps(1, 1);
    lPhysicsParamMassBias = new mxLabel(wPhysics, 0, 0, 0, 0, "1.0");

    slPhysicsParamInertia = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_P_INERTIA);
    slPhysicsParamInertia->setRange(0, 10.0, 100);
    slPhysicsParamInertia->setValue(1.0);
    slPhysicsParamInertia->setSteps(1, 1);
    lPhysicsParamInertia = new mxLabel(wPhysics, 0, 0, 0, 0, "1.0");

    slPhysicsParamDamping = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_P_DAMPING);
    slPhysicsParamDamping->setRange(0, 1.0, 100);
    slPhysicsParamDamping->setValue(0.01);
    slPhysicsParamDamping->setSteps(1, 1);
    lPhysicsParamDamping = new mxLabel(wPhysics, 0, 0, 0, 0, "0.5");

    slPhysicsParamRotDamping = new mxSlider(wPhysics, 0, 0, 0, 0, IDC_PHYS_P_ROT_DAMPING);
    slPhysicsParamRotDamping->setRange(0, 10.0, 200);
    slPhysicsParamRotDamping->setValue(0.2);
    slPhysicsParamRotDamping->setSteps(1, 1);
    lPhysicsParamRotDamping = new mxLabel(wPhysics, 0, 0, 0, 0, "0.2");

    lPhysicsMaterial = new mxLabel(wPhysics, 0, 0, 0, 0, "default");

    FitModelChoice(cPhysicsBone);
    auto *axes = new QHBoxLayout;
    for (auto *axis : rbConstraintAxis)
        axes->addWidget(axis);
    axes->addStretch();

    auto *constraints = new QGridLayout;
    constraints->setAlignment(Qt::AlignTop);
    constraints->setColumnStretch(1, 1);
    constraints->addWidget(new QLabel("Physics bone:", wPhysics), 0, 0);
    constraints->addWidget(cPhysicsBone, 0, 1, 1, 2);
    constraints->addWidget(mass, 1, 0);
    constraints->addWidget(leMass, 1, 1);
    constraints->addWidget(highlight, 1, 2);
    constraints->addWidget(new QLabel("Axis:", wPhysics), 2, 0);
    constraints->addLayout(axes, 2, 1, 1, 2);
    constraints->addWidget(minimum, 3, 0);
    constraints->addWidget(slPhysicsConMin, 3, 1);
    constraints->addWidget(lPhysicsConMin, 3, 2);
    constraints->addWidget(maximum, 4, 0);
    constraints->addWidget(slPhysicsConMax, 4, 1);
    constraints->addWidget(lPhysicsConMax, 4, 2);
    constraints->addWidget(friction, 5, 0);
    constraints->addWidget(slPhysicsFriction, 5, 1);
    constraints->addWidget(lPhysicsFriction, 5, 2);
    constraints->addWidget(test, 6, 0);
    constraints->addWidget(slPhysicsConTest, 6, 1);
    constraints->addWidget(cbLinked, 6, 2);

    auto *properties = new QGridLayout;
    properties->setAlignment(Qt::AlignTop);
    properties->setColumnStretch(1, 1);
    properties->addWidget(massBias, 0, 0);
    properties->addWidget(slPhysicsParamMassBias, 0, 1);
    properties->addWidget(lPhysicsParamMassBias, 0, 2);
    properties->addWidget(inertia, 1, 0);
    properties->addWidget(slPhysicsParamInertia, 1, 1);
    properties->addWidget(lPhysicsParamInertia, 1, 2);
    properties->addWidget(damping, 2, 0);
    properties->addWidget(slPhysicsParamDamping, 2, 1);
    properties->addWidget(lPhysicsParamDamping, 2, 2);
    properties->addWidget(rotDamping, 3, 0);
    properties->addWidget(slPhysicsParamRotDamping, 3, 1);
    properties->addWidget(lPhysicsParamRotDamping, 3, 2);
    properties->addWidget(material, 4, 0);
    properties->addWidget(lPhysicsMaterial, 4, 1, 1, 2);
    properties->addWidget(generate, 5, 0, 1, 3, Qt::AlignLeft);

    auto *layout = new QHBoxLayout(wPhysics);
    layout->addLayout(constraints, 1);
    layout->addLayout(properties, 1);
}

void ControlPanel::SetupSoftbodyWindow(mxTab* pTab) {
    auto *wSoftbody = AddScrollableTab(this, pTab, "Cloth");
    leSoftbodyIterations = new mxLabel(wSoftbody, 0, 0, 0, 0, "Iterations");
    slSoftbodyIterations = new mxSlider(wSoftbody, 0, 0, 0, 0, IDC_SOFT_ITERATIONS);
    slSoftbodyIterations->setRange(1, 101, 100);
    slSoftbodyIterations->setValue(1.0f);
    slSoftbodyIterations->setSteps(1, 5);
    auto *yaw = new mxLabel(wSoftbody, 0, 0, 0, 0, "Wind Yaw");
    slSoftbodyWindYaw = new mxSlider(wSoftbody, 0, 0, 0, 0, IDC_SOFT_WIND_YAW);
    slSoftbodyWindYaw->setRange(0, 360, 360);
    slSoftbodyWindYaw->setValue(0);
    slSoftbodyWindYaw->setSteps(1, 5);
    auto *strength = new mxLabel(wSoftbody, 0, 0, 0, 0, "Wind Strength");
    slSoftbodyWindStrength = new mxSlider(wSoftbody, 0, 0, 0, 0, IDC_SOFT_WIND_STRENGTH);
    slSoftbodyWindStrength->setRange(0, 1, 100);
    slSoftbodyWindStrength->setValue(0);
    slSoftbodyWindStrength->setSteps(1, 5);
    cSoftbodyCtrl = new mxChoice(wSoftbody, 0, 0, 0, 0, IDC_SOFT_BONE);
    cSoftbodyCtrl->add("None");
    cSoftbodyCtrl->select(0);
    cbSoftbodySimulate = new mxCheckBox(wSoftbody, 0, 0, 0, 0, "Simulate", IDC_SOFT_SIMULATE);
    auto *surface = new mxLabel(wSoftbody, 0, 0, 0, 0, "2D Stretch");
    slSoftbodySurfaceStretch = new mxSlider(wSoftbody, 0, 0, 0, 0, IDC_SOFT_SURFACE_STRETCH);
    slSoftbodySurfaceStretch->setRange(0, 3, 300);
    slSoftbodySurfaceStretch->setValue(0);
    slSoftbodySurfaceStretch->setSteps(1, 10);
    auto *thread = new mxLabel(wSoftbody, 0, 0, 0, 0, "1D Stretch");
    slSoftbodyThreadStretch = new mxSlider(wSoftbody, 0, 0, 0, 0, IDC_SOFT_THREAD_STRETCH);
    slSoftbodyThreadStretch->setRange(0, 3, 300);
    slSoftbodyThreadStretch->setValue(0);
    slSoftbodyThreadStretch->setSteps(1, 10);
    cbSoftbodyPolygons = new mxCheckBox(wSoftbody, 0, 0, 0, 0, "Polygons", IDC_SOFT_SHOW_POLYGONS);
    cbSoftbodyEdges = new mxCheckBox(wSoftbody, 0, 0, 0, 0, "Edges", IDC_SOFT_SHOW_EDGES);
    cbSoftbodyBases = new mxCheckBox(wSoftbody, 0, 0, 0, 0, "Bases", IDC_SOFT_SHOW_BASES);
    cbSoftbodyWind = new mxCheckBox(wSoftbody, 0, 0, 0, 0, "Wind", IDC_SOFT_SHOW_WIND);
    cbSoftbodyIndices = new mxCheckBox(wSoftbody, 0, 0, 0, 0, "Indices", IDC_SOFT_SHOW_INDICES);
    rbSoftbodyAxis[0] = new mxRadioButton(wSoftbody, 0, 0, 0, 0, "X", IDC_SOFT_CON_AXIS_X, true);
    rbSoftbodyAxis[1] = new mxRadioButton(wSoftbody, 0, 0, 0, 0, "Y", IDC_SOFT_CON_AXIS_Y);
    rbSoftbodyAxis[2] = new mxRadioButton(wSoftbody, 0, 0, 0, 0, "Z", IDC_SOFT_CON_AXIS_Z);
    rbSoftbodyAxis[0]->setChecked(true); // setSoftbodyAxis( 0 )

    FitModelChoice(cSoftbodyCtrl);
    auto *layout = new QGridLayout(wSoftbody);
    layout->setAlignment(Qt::AlignTop);
    layout->setColumnStretch(1, 1);
    layout->addWidget(cSoftbodyCtrl, 0, 0, 1, 2);
    layout->addWidget(cbSoftbodySimulate, 0, 2);
    layout->addWidget(leSoftbodyIterations, 1, 0);
    layout->addWidget(slSoftbodyIterations, 1, 1);
    layout->addWidget(yaw, 2, 0);
    layout->addWidget(slSoftbodyWindYaw, 2, 1);
    layout->addWidget(strength, 3, 0);
    layout->addWidget(slSoftbodyWindStrength, 3, 1);
    layout->addWidget(surface, 4, 0);
    layout->addWidget(slSoftbodySurfaceStretch, 4, 1);
    layout->addWidget(thread, 5, 0);
    layout->addWidget(slSoftbodyThreadStretch, 5, 1);

    auto *display = new QGridLayout;
    display->setAlignment(Qt::AlignTop);
    display->addWidget(cbSoftbodyPolygons, 0, 0);
    display->addWidget(cbSoftbodyEdges, 0, 1);
    display->addWidget(cbSoftbodyBases, 0, 2);
    display->addWidget(cbSoftbodyWind, 1, 0);
    display->addWidget(cbSoftbodyIndices, 1, 1);
    for (int i = 0; i < 3; ++i)
        display->addWidget(rbSoftbodyAxis[i], 2, i);
    layout->addLayout(display, 1, 2, 5, 1);
}

void ControlPanel::SetupIKRuleWindow(mxTab* pTab) {
    auto *wIKRule = AddScrollableTab(this, pTab, "IKRule");

    auto *chain = new mxLabel(wIKRule, 0, 0, 0, 0, "Chain:");
    cIKChain = new mxChoice(wIKRule, 0, 0, 0, 0, IDC_IKRULE_CHAIN);
    cIKChain->add("lfoot");
    cIKChain->add("rfoot");
    cIKChain->add("lhand");
    cIKChain->add("rhand");
    cIKChain->select(0);
    mxToolTip::add(cIKChain, "Select IK Chain");

    auto *type = new mxLabel(wIKRule, 0, 0, 0, 0, "Type:");
    cIKType = new mxChoice(wIKRule, 0, 0, 0, 0, IDC_IKRULE_CHOICE);
    cIKType->add("footstep");
    cIKType->add("touch");
    cIKType->add("release");
    cIKType->add("attachment");
    cIKType->add("unlatch");
    cIKType->select(0);
    mxToolTip::add(cIKType, "Select IK Type");

    lIKTouch = new mxLabel(wIKRule, 0, 0, 0, 0, "Bone:");
    cIKTouch = new mxChoice(wIKRule, 0, 0, 0, 0, IDC_IKRULE_TOUCH);
    PopulateBoneList(cIKTouch, {});
    mxToolTip::add(cIKTouch, "Select Touched Bone");

    lIKAttachment = new mxLabel(wIKRule, 0, 0, 0, 0, "Attachment:");
    leIKAttachment = new mxLineEdit(wIKRule, 0, 0, 0, 0, "", IDC_IKRULE_ATTACHMENT);

    cbIKRangeToggle = new mxCheckBox(wIKRule, 0, 0, 0, 0, "Range", IDC_IKRULE_RANGE_TOGGLE);
    mxToolTip::add(cbIKRangeToggle, "Toggle range option");
    auto *start = new mxButton(wIKRule, 0, 0, 0, 0, "start", IDC_IKRULE_RANGE_START_NOW);
    leIKRangeStart = new mxLineEdit2(wIKRule, 0, 0, 0, 0, "..", IDC_IKRULE_RANGE_START);
    auto *peak = new mxButton(wIKRule, 0, 0, 0, 0, "peak", IDC_IKRULE_RANGE_PEAK_NOW);
    leIKRangePeak = new mxLineEdit2(wIKRule, 0, 0, 0, 0, "..", IDC_IKRULE_RANGE_PEAK);
    auto *tail = new mxButton(wIKRule, 0, 0, 0, 0, "tail", IDC_IKRULE_RANGE_TAIL_NOW);
    leIKRangeTail = new mxLineEdit2(wIKRule, 0, 0, 0, 0, "..", IDC_IKRULE_RANGE_TAIL);
    auto *end = new mxButton(wIKRule, 0, 0, 0, 0, "end", IDC_IKRULE_RANGE_END_NOW);
    leIKRangeEnd = new mxLineEdit2(wIKRule, 0, 0, 0, 0, "..", IDC_IKRULE_RANGE_END);

    cbIKContactToggle = new mxCheckBox(wIKRule, 0, 0, 0, 0, "Contact", IDC_IKRULE_CONTACT_TOGGLE);
    mxToolTip::add(cbIKContactToggle, "Toggle contact option");
    auto *frame = new mxButton(wIKRule, 0, 0, 0, 0, "frame", IDC_IKRULE_CONTACT_FRAME_NOW);
    leIKContactFrame = new mxLineEdit2(wIKRule, 0, 0, 0, 0, "", IDC_IKRULE_CONTACT_FRAME);

    auto *transform = new mxLabel(wIKRule, 0, 0, 0, 0, "Transform using:");
    cIKUsing = new mxChoice(wIKRule, 0, 0, 0, 0, IDC_IKRULE_USING);
    cIKUsing->add("neither");
    cIKUsing->add("source");
    cIKUsing->add("sequence");
    cIKUsing->select(0);
    mxToolTip::add(cIKUsing, "Choose Transform To Use");

    auto *qc = new mxLabel(wIKRule, 0, 0, 0, 0, "QC String:");
    leIKQCString = new mxLineEdit2(wIKRule, 0, 0, 0, 0, "", IDC_IKRULE_QC_STRING);

    FitModelChoice(cIKTouch);
    auto *layout = new QGridLayout(wIKRule);
    layout->setAlignment(Qt::AlignTop);
    auto *target = new QHBoxLayout;
    target->addWidget(chain);
    target->addWidget(cIKChain);
    target->addWidget(type);
    target->addWidget(cIKType);
    target->addWidget(lIKTouch);
    target->addWidget(cIKTouch, 1);
    target->addWidget(lIKAttachment);
    target->addWidget(leIKAttachment, 1);
    target->addStretch();
    layout->addLayout(target, 0, 0, 1, 5);

    layout->addWidget(cbIKRangeToggle, 1, 0);
    layout->addWidget(start, 1, 1);
    layout->addWidget(leIKRangeStart, 1, 2);
    layout->addWidget(peak, 1, 3);
    layout->addWidget(leIKRangePeak, 1, 4);
    layout->addWidget(tail, 2, 1);
    layout->addWidget(leIKRangeTail, 2, 2);
    layout->addWidget(end, 2, 3);
    layout->addWidget(leIKRangeEnd, 2, 4);
    layout->addWidget(cbIKContactToggle, 3, 0);
    layout->addWidget(frame, 3, 1);
    layout->addWidget(leIKContactFrame, 3, 2);
    layout->addWidget(transform, 4, 0);
    layout->addWidget(cIKUsing, 4, 1, 1, 2);
    layout->addWidget(qc, 5, 0);
    layout->addWidget(leIKQCString, 5, 1, 1, 4);

    UpdateIKRuleWindow();
}

void ControlPanel::SetupEventWindow(mxTab* pTab) {
    auto *wEvents = AddScrollableTab(this, pTab, "Events");

    auto *sound = new mxLabel(wEvents, 0, 0, 0, 0, "Sound:");
    mxButton* bSoundFrameNow = new mxButton(wEvents, 0, 0, 0, 0, "frame", IDC_EVENT_SOUND_FRAME_NOW);
    mxToolTip::add(bSoundFrameNow, "Set sound start at the current frame");
    leEventSoundFrame = new mxLineEdit2(wEvents, 0, 0, 0, 0, "0", IDC_EVENT_SOUND_FRAME);
    lbEventSoundName = new mxListBox(wEvents, 0, 0, 0, 0, IDC_EVENT_SOUND_NAME);
    mxToolTip::add(lbEventSoundName, "Select Sound Name"); // no soundemitter scripts yet: list stays empty

    auto *qc = new mxLabel(wEvents, 0, 0, 0, 0, "QC String:");
    leEventQCString = new mxLineEdit2(wEvents, 0, 0, 0, 0, "", IDC_EVENT_QC_STRING);
    BuildEventQCString();

    lEventSequence = new mxLabel(wEvents, 0, 0, 0, 0, "");
    lbEventHistory = new mxListBox(wEvents, 0, 0, 0, 0);

    auto *layout = new QGridLayout(wEvents);
    auto *frame = new QHBoxLayout;
    frame->addWidget(sound);
    frame->addWidget(bSoundFrameNow);
    frame->addWidget(leEventSoundFrame);
    layout->addLayout(frame, 0, 0);
    layout->addWidget(lEventSequence, 0, 1);
    layout->addWidget(lbEventSoundName, 1, 0);
    layout->addWidget(lbEventHistory, 1, 1);
    auto *output = new QHBoxLayout;
    output->addWidget(qc);
    output->addWidget(leEventQCString, 1);
    layout->addLayout(output, 2, 0, 1, 2);
}

void ControlPanel::SetupBoneControlWindow(mxTab* pTab) {
    m_pBoneWindow = new CBoneControlWindow(this, m_settings);
    AddScrollableTab(pTab, m_pBoneWindow, "Bones");
    m_pBoneWindow->Init();
}

void ControlPanel::SetupAttachmentsWindow(mxTab* pTab) {
    m_pAttachmentsWindow = new CAttachmentsWindow(this);
    AddScrollableTab(pTab, m_pAttachmentsWindow, "Attachments");
    m_pAttachmentsWindow->Init();
}

void ControlPanel::SetupMatVarWindow(mxTab* pTab) {
    auto *wMatVars = AddScrollableTab(this, pTab, "Materials");

    auto *names = new mxLabel(wMatVars, 0, 0, 0, 0, "Materials:");
    cMaterialList = new mxListBox(wMatVars, 0, 0, 0, 0, IDC_MATERIALVARMATS);
    cMaterialList->add("None");
    cMaterialList->select(1);
    mxToolTip::add(cMaterialList, "Materials (VMT files) this model has loaded");

    auto *parameters = new mxLabel(wMatVars, 0, 0, 0, 0, "Material Parameters:");
    cMaterialParamList = new mxListBox(wMatVars, 0, 0, 0, 0, IDC_MATERIALVARPARAMS);
    cMaterialParamList->add("None");
    cMaterialParamList->select(1);
    mxToolTip::add(cMaterialParamList, "Material parameters of this material");

    auto *modify = new mxLabel(wMatVars, 0, 0, 0, 0, "Modify Parameter:");
    leMaterialParamText = new mxLineEdit2(wMatVars, 0, 0, 0, 0, "", IDC_MATVAREDIT);
    leMaterialParamText->setVisible(false);

    const auto matrixSlider = [&](mxLabel*&label, mxSlider*&slider, const char* text, float min, float max,
                                  float value) {
        label = new mxLabel(wMatVars, 0, 0, 0, 0, text);
        slider = new mxSlider(wMatVars, 0, 0, 0, 0, IDC_MATVARSLIDERMATRIX);
        slider->setRange(min, max);
        slider->setSteps(1, 1);
        slider->setValue(value);
        label->setVisible(false);
        slider->setVisible(false);
    };
    matrixSlider(lblMatrixRotation, slMaterialParamMatrixSliderRotation, "Rotation:", -180.0, 180.0, 0.0);
    matrixSlider(lblMatrixScaleX, slMaterialParamMatrixSliderScaleX, "Scale X:", -5.0, 5.0, 1.0);
    matrixSlider(lblMatrixScaleY, slMaterialParamMatrixSliderScaleY, "Scale Y:", -5.0, 5.0, 1.0);
    matrixSlider(lblMatrixTranslateX, slMaterialParamMatrixSliderTranslateX, "Translate X:", -2.0, 2.0, 0.0);
    matrixSlider(lblMatrixTranslateY, slMaterialParamMatrixSliderTranslateY, "Translate Y:", -2.0, 2.0, 0.0);

    bMaterialParamColor = new mxButton(wMatVars, 0, 0, 0, 0, "Color picker", IDC_MATVARCOLORPICKER);
    bMaterialParamColor->setVisible(false);

    slMaterialParamFloat = new mxSlider(wMatVars, 0, 0, 0, 0, IDC_MATVARSLIDERFLOAT);
    slMaterialParamFloat->setRange(-1.0, 1.0);
    slMaterialParamFloat->setValue(0.0);
    slMaterialParamFloat->setVisible(false);

    cbMaterialParamMultiEdit = new mxCheckBox(wMatVars, 0, 0, 0, 0, "Affect all loaded materials", 0);

    bMaterialParamLoad = new mxButton(wMatVars, 0, 0, 0, 0, "Replace VMT", IDC_MATVARLOAD);
    mxToolTip::add(bMaterialParamLoad, "Temporarily replace this material with a custom set of VMT parameters.");
    bMaterialParamLoad->setVisible(false);

    bMaterialParamCopyToClipboard = new mxButton(wMatVars, 0, 0, 0, 0, "Copy to clipboard",
                                                 IDC_MATVARCOPYTOCLIPBOARD);
    bMaterialParamCopyToClipboard->setVisible(false);

    auto *layout = new QGridLayout(wMatVars);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 2);
    layout->addWidget(names, 0, 0);
    layout->addWidget(parameters, 0, 1);
    layout->addWidget(cMaterialList, 1, 0);
    layout->addWidget(cMaterialParamList, 1, 1);

    auto *editor = new QVBoxLayout;
    auto *title = new QHBoxLayout;
    title->addWidget(modify);
    title->addWidget(cbMaterialParamMultiEdit);
    editor->addLayout(title);
    editor->addWidget(leMaterialParamText);
    editor->addWidget(bMaterialParamColor, 0, Qt::AlignLeft);
    editor->addWidget(slMaterialParamFloat);
    auto *matrix = new QGridLayout;
    const std::pair<mxLabel *, mxSlider *> rows[] = {
        {lblMatrixRotation, slMaterialParamMatrixSliderRotation},
        {lblMatrixScaleX, slMaterialParamMatrixSliderScaleX},
        {lblMatrixScaleY, slMaterialParamMatrixSliderScaleY},
        {lblMatrixTranslateX, slMaterialParamMatrixSliderTranslateX},
        {lblMatrixTranslateY, slMaterialParamMatrixSliderTranslateY}
    };
    for (int i = 0; i < 5; ++i) {
        matrix->addWidget(rows[i].first, i, 0);
        matrix->addWidget(rows[i].second, i, 1);
    }
    editor->addLayout(matrix);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(bMaterialParamLoad);
    buttons->addWidget(bMaterialParamCopyToClipboard);
    buttons->addStretch();
    editor->addLayout(buttons);
    editor->addStretch();
    layout->addLayout(editor, 0, 2, 2, 1);
}

void ControlPanel::SetupSubmodelWindow(mxTab* pTab) {
    auto *wSubmodels = AddScrollableTab(this, pTab, "Submodels");

    bSubmodelAdd = new mxButton(wSubmodels, 0, 0, 0, 0, "Add Submodel", IDC_SUBMODEL_LOADMERGEDMODEL);
    bSubmodelAddSteam = new mxButton(wSubmodels, 0, 0, 0, 0, "Add [Steam]", IDC_SUBMODEL_LOADMERGEDMODEL_STEAM);
    bSubmodelRemoveAll = new mxButton(wSubmodels, 0, 0, 0, 0, "Remove All", IDC_SUBMODEL_UNLOADALLMERGEDMODELS);

    cSubmodelList = new mxListBox(wSubmodels, 0, 0, 0, 0, IDC_SUBMODEL_UPDATE_SELECTION);
    mxToolTip::add(cSubmodelList, "Select submodels to add/remove/change");

    bSubmodelRemoveSelected = new mxButton(wSubmodels, 0, 0, 0, 0, "Remove Selected",
                                           IDC_SUBMODEL_UNLOADMERGEDMODEL);
    bSubmodelRemoveSelected->setEnabled(false);

    auto *attach = new mxLabel(wSubmodels, 0, 0, 0, 0, "Force attach to:");
    cSubmodelAttachTo = new mxChoice(wSubmodels, 0, 0, 0, 0, IDC_SUBMODEL_UPDATE_BONESELECTION);
    cSubmodelAttachTo->setEnabled(false);

    auto *origin = new mxLabel(wSubmodels, 0, 0, 0, 0, "From local attach origin:");
    cSubmodelLocalAttachOrigin = new mxChoice(wSubmodels, 0, 0, 0, 0, IDC_SUBMODEL_UPDATE_BONESELECTION);
    cSubmodelLocalAttachOrigin->setEnabled(false);

    FitModelChoice(cSubmodelAttachTo);
    FitModelChoice(cSubmodelLocalAttachOrigin);
    auto *buttons = new QVBoxLayout;
    buttons->addWidget(bSubmodelAdd);
    buttons->addWidget(bSubmodelAddSteam);
    buttons->addWidget(bSubmodelRemoveAll);
    buttons->addStretch();
    auto *details = new QVBoxLayout;
    details->addWidget(bSubmodelRemoveSelected);
    details->addWidget(attach);
    details->addWidget(cSubmodelAttachTo);
    details->addWidget(origin);
    details->addWidget(cSubmodelLocalAttachOrigin);
    details->addStretch();
    auto *layout = new QHBoxLayout(wSubmodels);
    layout->addLayout(buttons);
    layout->addWidget(cSubmodelList, 1);
    layout->addLayout(details);
}

void ControlPanel::SetupCompileWindow(mxTab* pTab) {
    auto *wCompile = AddScrollableTab(this, pTab, "Compile");

    auto *recent = new mxLabel(wCompile, 0, 0, 0, 0, "Recent QC scripts:");

    cCompileRecentQCpaths = new mxListBox(wCompile, 0, 0, 0, 0, IDC_COMPILE_UPDATE_QCPATHSELECTION);

    bCompileQCRemoveFromList = new mxButton(wCompile, 0, 0, 0, 0, "Remove selected from list",
                                            IDC_COMPILE_REMOVEFROMLIST);
    bCompileQCRemoveFromList->setEnabled(false);

    bCompileQCWhenSelected = new mxButton(wCompile, 0, 0, 0, 0, "One-click compile [OFF]",
                                          IDC_COMPILE_SELECTEDTOGGLE);
    mxToolTip::add(bCompileQCWhenSelected,
                   "When enabled, selecting a QC in the list will start a recompile automatically.");
    bCompileSelectedToggle = false;

    lblFullQCPath = new mxLabel(wCompile, 0, 0, 0, 0,
                                "No QC script selected. To add a QC file, drag and drop it onto the viewport.");

    bCompileQCCompile = new mxButton(wCompile, 0, 0, 0, 0, "Recompile",
                                     IDC_COMPILE_CALLSTUDIOMDL);
    bCompileQCCompile->setEnabled(false);

    bCompileQCLoadModel = new mxButton(wCompile, 0, 0, 0, 0, "Load in HLMV",
                                       IDC_COMPILE_LOADMODELFILE);
    bCompileQCLoadModel->setEnabled(false);

    bCompileQCShowCompileOutput = new mxButton(wCompile, 0, 0, 0, 0,
                                               "Open full log file", IDC_COMPILE_OPENLOGFILE);
    bCompileQCShowCompileOutput->setEnabled(false);

    bCompileQCBrowseToQC = new mxButton(wCompile, 0, 0, 0, 0, "Explore to QC",
                                        IDC_COMPILE_EXPLORETOQC);
    bCompileQCBrowseToQC->setEnabled(false);

    cCompileQCOutput = new mxListBox(wCompile, 0, 0, 0, 0);
    lblCompileWarningOrError = new mxLabel(wCompile, 0, 0, 0, 0, "");

    lblFullQCPath->setWordWrap(true);
    lblCompileWarningOrError->setWordWrap(true);
    for (auto *list : {cCompileRecentQCpaths, cCompileQCOutput}) {
        list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        list->setMinimumHeight(4 * list->fontMetrics().lineSpacing());
    }
    auto *layout = new QGridLayout(wCompile);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 2);
    layout->setRowStretch(1, 1);
    layout->addWidget(recent, 0, 0);
    layout->addWidget(lblFullQCPath, 0, 1);
    layout->addWidget(cCompileRecentQCpaths, 1, 0);
    layout->addWidget(cCompileQCOutput, 1, 1);
    auto *listActions = new QVBoxLayout;
    listActions->addWidget(bCompileQCRemoveFromList);
    listActions->addWidget(bCompileQCWhenSelected);
    layout->addLayout(listActions, 2, 0);
    auto *outputActions = new QGridLayout;
    outputActions->addWidget(bCompileQCCompile, 0, 0);
    outputActions->addWidget(bCompileQCLoadModel, 0, 1);
    outputActions->addWidget(bCompileQCShowCompileOutput, 1, 0);
    outputActions->addWidget(bCompileQCBrowseToQC, 1, 1);
    layout->addLayout(outputActions, 2, 1);
    layout->addWidget(lblCompileWarningOrError, 3, 0, 1, 2);
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

    m_flexControllers = mesh.flex.controllers;
    const int controllerCount = int(m_flexControllers.size());
    m_flexPage = 0;
    m_settings.flexRaw.assign(m_flexControllers.size(), 0.f);
    for (int i = 0; i < controllerCount; i++)
        SetFlexController(i, 0.0f);
    UpdateFlexPage();
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
                  "Materials: %d\nSkin Families: %d\nBodyparts: %d\nAttachments: %d\nBody index: %d\n",
                  //"Hull extent (X): %.2f\nHull extent (Y): %.2f\nHull extent (Z): %.2f\n",
                  m_materialCount, m_info.skinFamilies, int(m_info.bodyParts.size()), int(m_info.attachments.size()), body
                  //std::abs(extent.x()), std::abs(extent.y()), std::abs(extent.z())
                  );
    lModelInfo2->setLabel("%s", str);
}

int ControlPanel::FlexIndex(int slot) const
{
    const int index = m_flexPage * NUM_FLEX_SLIDERS + slot;
    return index < int(m_flexControllers.size()) ? index : -1;
}

void ControlPanel::UpdateFlexPage()
{
    const int pages = std::max(1, (int(m_flexControllers.size()) + NUM_FLEX_SLIDERS - 1) / NUM_FLEX_SLIDERS);
    m_flexPage = std::clamp(m_flexPage, 0, pages - 1);
    for (int i = 0; i < NUM_FLEX_SLIDERS; i++)
    {
        const int index = FlexIndex(i);
        const auto name = index >= 0 ? m_flexControllers[index].name : QString();
        lFlex[i]->setText(name);
        lFlex[i]->setToolTip(name);
        slFlexScale[i]->setEnabled(index >= 0);
    }
    lFlexPage->setLabel("Page %d / %d", m_flexPage + 1, pages);
    bFlexPrev->setEnabled(m_flexPage > 0);
    bFlexNext->setEnabled(m_flexPage + 1 < pages);
    UpdateFlexSliders();
}

void ControlPanel::UpdateFlexSliders()
{
    for (int i = 0; i < NUM_FLEX_SLIDERS; i++)
    {
        const int index = FlexIndex(i);
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

    case IDC_EYELOOK:
        m_settings.eyeLook = dynamic_cast<QCheckBox *>(event->widget)->isChecked();
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

    case IDC_FLEXPREV:
    case IDC_FLEXNEXT:
        m_flexPage += event->action == IDC_FLEXNEXT ? 1 : -1;
        UpdateFlexPage();
        break;

    default:
        if (event->action >= IDC_FLEXSCALE && event->action < IDC_FLEXSCALE + NUM_FLEX_SLIDERS)
        {
            // SetFlexControllerRaw: the slider value is already normalized.
            const int index = FlexIndex(event->action - IDC_FLEXSCALE);
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

void CBoneControlWindow::Init() {
    // Bone selection list
    auto *boneLabel = new mxLabel(this, 0, 0, 0, 0, "Bone");
    m_cBone = new mxChoice(this, 0, 0, 0, 0, IDC_BONE_BONELIST);
    m_cBone->add("None");
    m_cBone->select(0);
    mxToolTip::add(m_cBone, "Select a bone to modify");

    m_cBoneHighlight = new mxCheckBox(this, 0, 0, 0, 0, "Highlight", IDC_BONE_HIGHLIGHT_BONE);
    mxToolTip::add(m_cBoneHighlight, "Toggle display of the bone being modified");

    m_cBoneNames = new mxCheckBox(this, 0, 0, 0, 0, "Names", IDC_BONE_NAMES);
    mxToolTip::add(m_cBoneNames, "Toggle bone names");

    auto *surface = new mxLabel(this, 0, 0, 0, 0, "Bone Surface Prop");
    m_cSurfaceProp = new mxChoice(this, 0, 0, 0, 0, IDC_BONE_SURFACEPROP);
    mxToolTip::add(m_cSurfaceProp, "Select a surface property to apply to the bone");
    ReadPhysicsMaterials(m_cSurfaceProp);

    mxButton* btnApplyChild = new mxButton(this, 0, 0, 0, 0, "Apply to Children",
                                           IDC_BONE_APPLY_TO_CHILDREN);
    mxToolTip::add(btnApplyChild, "Apply the surface property to all child bones");

    m_cAutoHitbox = new mxCheckBox(this, 0, 0, 0, 0, "Autogenerate Hitboxes",
                                   IDC_BONE_USE_AUTOGENERATED_HITBOXES);
    mxToolTip::add(m_cAutoHitbox, "When this is checked, studiomdl will automatically generate hitboxes");

    m_cShowDefaultPose = new mxCheckBox(this, 0, 0, 0, 0, "Show Default Pose",
                                        IDC_BONE_SHOW_DEFAULT_POSE);
    mxToolTip::add(m_cShowDefaultPose, "Toggles display of the default physics pose");

    auto *setLabel = new mxLabel(this, 0, 0, 0, 0, "Hitbox Set");
    m_cHitboxSet = new mxChoice(this, 0, 0, 0, 0, IDC_BONE_HITBOXSET);
    m_cHitboxSet->add("unnamed");
    m_cHitboxSet->select(0);
    mxToolTip::add(m_cHitboxSet, "Change hitbox set");

    m_eHitboxSetName = new mxLineEdit(this, 0, 0, 0, 0, "", IDC_BONE_HITBOXSETNAME_EDIT);
    mxToolTip::add(m_eHitboxSetName, "Type in a name and hit the set button");

    m_bHitboxSetUpdateName = new mxButton(this, 0, 0, 0, 0, "Set Name", IDC_BONE_HITBOXSETNAME);
    mxToolTip::add(m_bHitboxSetUpdateName, "Press to set hitbox set name from above text field");

    m_bAddHitboxSet = new mxButton(this, 0, 0, 0, 0, "Add set", IDC_BONE_HITBOXADDSET);
    mxToolTip::add(m_bAddHitboxSet, "Add a hitbox set");
    m_bDeleteHitboxSet = new mxButton(this, 0, 0, 0, 0, "Delete set", IDC_BONE_HITBOXDELETESET);
    mxToolTip::add(m_bDeleteHitboxSet, "Remove a hitbox set");

    auto *hitboxLabel = new mxLabel(this, 0, 0, 0, 0, "Hitbox");
    m_cHitbox = new mxChoice(this, 0, 0, 0, 0, IDC_BONE_HITBOXLIST);
    m_cHitbox->add("None");
    m_cHitbox->select(0);
    mxToolTip::add(m_cHitbox, "Select a hitbox to modify");

    m_cHitboxHighlight = new mxCheckBox(this, 0, 0, 0, 0, "Highlight Hitbox",
                                        IDC_BONE_HIGHLIGHT_HITBOX);
    mxToolTip::add(m_cHitboxHighlight, "Toggle display of the hitbox being modified");

    auto *groupLabel = new mxLabel(this, 0, 0, 0, 0, "Hitbox Group");
    m_eHitboxGroup = new mxLineEdit(this, 0, 0, 0, 0, "", IDC_BONE_HITBOX_GROUP);
    mxToolTip::add(m_eHitboxGroup, "The group of the current hitbox");

    auto *nameLabel = new mxLabel(this, 0, 0, 0, 0, "Hitbox Name");
    m_eHitboxName = new mxLineEdit(this, 0, 0, 0, 0, "", IDC_BONE_HITBOX_NAME);
    m_eHitboxName->setEnabled(false);
    mxToolTip::add(m_eHitboxName, "The name of the current hitbox");

    auto *editLabel = new mxLabel(this, 0, 0, 0, 0, "Edit Hitbox:");
    m_cHitboxEditMode = new mxChoice(this, 0, 0, 0, 0, IDC_BONE_HITBOX_EDITMODE);
    m_cHitboxEditMode->add("Rotate");
    m_cHitboxEditMode->add("Translate BB Min");
    m_cHitboxEditMode->add("Translate BB Max");
    m_cHitboxEditMode->select(0);

    m_bUpdateHitbox = new mxButton(this, 0, 0, 0, 0, "Update Hitbox", IDC_BONE_UPDATE_HITBOX);
    mxToolTip::add(m_bUpdateHitbox, "Apply hitbox group, origin, and size to the hitbox");

    mxButton* btnLoadHitbox = new mxButton(this, 0, 0, 0, 0, "Load .HBX", IDC_BONE_LOAD_HITBOXES);
    mxToolTip::add(btnLoadHitbox, "Load hitboxes from a .hbx file");

    mxButton* btnSaveHitbox = new mxButton(this, 0, 0, 0, 0, "Save .HBX", IDC_BONE_SAVE_HITBOXES);
    mxToolTip::add(btnSaveHitbox, "Save hitboxes to a .hbx file");

    mxButton* btnGenerateQC = new mxButton(this, 0, 0, 0, 0, "Generate QC", IDC_BONE_GENERATEQC);
    mxToolTip::add(btnGenerateQC, "Copy a .qc file snippet to the clipboard");

    m_bAddHitbox = new mxButton(this, 0, 0, 0, 0, "Add Hitbox", IDC_BONE_ADD_HITBOX);
    m_bDeleteHitbox = new mxButton(this, 0, 0, 0, 0, "Delete Hitbox", IDC_BONE_DELETE_HITBOX);
    mxToolTip::add(m_bAddHitbox, "Create a new hitbox attached to the current bone");
    mxToolTip::add(m_bDeleteHitbox, "Delete the currently selected hitbox");

    for (auto *choice : {m_cBone, m_cSurfaceProp, m_cHitboxSet, m_cHitbox})
        FitModelChoice(choice);
    auto *layout = new QGridLayout(this);
    layout->setAlignment(Qt::AlignTop);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setVerticalSpacing(4);
    auto *bone = new QVBoxLayout;
    bone->setSpacing(4);
    bone->addWidget(boneLabel);
    bone->addWidget(m_cBone);
    auto *display = new QHBoxLayout;
    display->addWidget(m_cBoneHighlight);
    display->addWidget(m_cBoneNames);
    bone->addLayout(display);
    bone->addWidget(surface);
    bone->addWidget(m_cSurfaceProp);
    bone->addWidget(btnApplyChild);
    bone->addWidget(m_cAutoHitbox);
    bone->addWidget(m_cShowDefaultPose);
    bone->addStretch();
    layout->addLayout(bone, 0, 0);

    auto *set = new QVBoxLayout;
    set->addWidget(setLabel);
    set->addWidget(m_cHitboxSet);
    set->addWidget(m_eHitboxSetName);
    set->addWidget(m_bHitboxSetUpdateName);
    set->addWidget(m_bAddHitboxSet);
    set->addWidget(m_bDeleteHitboxSet);
    set->addStretch();
    layout->addLayout(set, 0, 1);

    auto *box = new QGridLayout;
    box->setVerticalSpacing(4);
    box->setAlignment(Qt::AlignTop);
    box->addWidget(hitboxLabel, 0, 0);
    box->addWidget(m_cHitbox, 0, 1);
    box->addWidget(m_cHitboxHighlight, 1, 0, 1, 2);
    box->addWidget(groupLabel, 2, 0);
    box->addWidget(m_eHitboxGroup, 2, 1);
    box->addWidget(nameLabel, 3, 0);
    box->addWidget(m_eHitboxName, 3, 1);
    box->addWidget(editLabel, 4, 0);
    box->addWidget(m_cHitboxEditMode, 4, 1);
    box->addWidget(m_bAddHitbox, 5, 0);
    box->addWidget(m_bDeleteHitbox, 5, 1);
    box->addWidget(m_bUpdateHitbox, 6, 0, 1, 2);
    layout->addLayout(box, 0, 2);

    auto *files = new QHBoxLayout;
    files->addWidget(btnGenerateQC);
    files->addWidget(btnLoadHitbox);
    files->addWidget(btnSaveHitbox);
    files->addStretch();
    layout->addLayout(files, 1, 0, 1, 3);
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

void CAttachmentsWindow::Init() {
    auto *attachment = new mxLabel(this, 0, 0, 0, 0, "Attachment");
    m_cAttachmentList = new mxListBox(this, 0, 0, 0, 0, IDC_ATTACHMENT_LIST);
    m_cAttachmentList->add("None");
    m_cAttachmentList->select(0);
    mxToolTip::add(m_cAttachmentList, "Select an attachment to modify");

    auto *bone = new mxLabel(this, 0, 0, 0, 0, "Attach To Bone");
    m_cBoneList = new mxListBox(this, 0, 0, 0, 0, IDC_ATTACHMENT_LIST_BONES);
    m_cBoneList->add("None");
    m_cBoneList->select(0);
    mxToolTip::add(m_cBoneList, "Select a bone to attach to");

    auto *translation = new mxLabel(this, 0, 0, 0, 0, "Translation");
    m_cTranslation = new mxLineEdit2(this, 0, 0, 0, 0, "10 20 30", IDC_ATTACHMENT_TRANSLATION);

    auto *rotation = new mxLabel(this, 0, 0, 0, 0, "Rotation");
    m_cRotation = new mxLineEdit2(this, 0, 0, 0, 0, "0 90 180", IDC_ATTACHMENT_ROTATION);

    auto *qc = new mxLabel(this, 0, 0, 0, 0, "QC String");
    m_cQCString = new mxLineEdit2(this, 0, 0, 0, 0,
                                  "$attachment \"controlpanel0_ur\" \"Vgui\" -22 -15 4 rotate 0 0 0",
                                  IDC_ATTACHMENT_QC_STRING);

    // Nudge buttons: translate X/Y/Z then rotate X/Y/Z, each large/small minus and plus.
    static const char* const labels[6][4] = {
        {"-X", "-x", "+x", "+X"}, {"-Y", "-y", "+y", "+Y"}, {"-Z", "-z", "+z", "+Z"},
        {"-XR", "-xr", "+xr", "+XR"}, {"-YR", "-yr", "+yr", "+YR"}, {"-ZR", "-zr", "+zr", "+ZR"},
    };

    auto *layout = new QGridLayout(this);
    layout->addWidget(attachment, 0, 0);
    layout->addWidget(bone, 0, 1);
    layout->addWidget(m_cAttachmentList, 1, 0);
    layout->addWidget(m_cBoneList, 1, 1);
    auto *fields = new QGridLayout;
    fields->addWidget(translation, 0, 0);
    fields->addWidget(m_cTranslation, 0, 1);
    fields->addWidget(rotation, 0, 2);
    fields->addWidget(m_cRotation, 0, 3);
    fields->addWidget(qc, 1, 0);
    fields->addWidget(m_cQCString, 1, 1, 1, 3);
    layout->addLayout(fields, 2, 0, 1, 2);

    auto *nudge = new QGridLayout;
    nudge->setAlignment(Qt::AlignTop);
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 4; ++col)
            nudge->addWidget(new mxButton(this, 0, 0, 0, 0, labels[row][col], IDC_ATTACHMENT_NUDGE), row, col);
    layout->addLayout(nudge, 0, 2, 3, 1);
}

void CAttachmentsWindow::OnLoadModel(const MdlInfo &info) {
    Fill(m_cAttachmentList, info.attachments, "None");
    Fill(m_cBoneList, info.bones, "None");
}
