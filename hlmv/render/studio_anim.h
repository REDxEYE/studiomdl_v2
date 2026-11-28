#pragma once

// Port of upstream HLMV StudioModel's animation half (studio_render.cpp AdvanceFrame/SetUpBones,
// studio_utils.cpp sequence/layer/pose parameter helpers) on top of the engine's bone setup
// (libs/bonesetup IBoneSetup/CIKContext, libs/utils CStudioHdr). Engine types stay in the .cpp.

#include <memory>
#include <vector>

#include <QMatrix4x4>
#include <QString>
#include <QStringList>

#include "mdl_jiggle.h"

class StudioAnimation
{
public:
    static constexpr int MaxLayers = 8; // upstream MAXSTUDIOANIMLAYERS
    static constexpr float DefaultBlendTime = 0.2f;

    explicit StudioAnimation(const QString &mdlPath); // throws std::runtime_error
    ~StudioAnimation();
    StudioAnimation(const StudioAnimation &) = delete;
    StudioAnimation &operator=(const StudioAnimation &) = delete;

    int BoneCount() const;
    int SequenceCount() const;
    QString SequenceName(int sequence) const;
    QString ActivityName(int64_t sequence) const;
    bool IsHidden(int64_t sequence) const;     // STUDIO_HIDDEN
    int FacePoserType(int64_t sequence) const; // keyvalues faceposer { type }: 2 posture, 1 gesture, 0 other
    QStringList ActivityModifiers(int sequence) const;

    int PoseParameterCount() const;
    QString PoseParameterName(int parameter) const;
    bool GetPoseParameterRange(int parameter, float *min, float *max) const;
    float SetPoseParameter(int parameter, float value); // value in parameter units; returns the clamped value
    float GetPoseParameter(int parameter) const;

    struct Controller
    {
        int inputfield;
        float start, end;
    };
    int ControllerCount() const;
    Controller GetController(int index) const;
    float SetController(int inputfield, float value); // upstream passes mstudiobonecontroller_t::inputfield

    int SetSequence(int sequence);
    int GetSequence() const;
    int SetOverlaySequence(int layer, int sequence, float weight); // layer 0..MaxLayers-1
    int GetOverlaySequence(int layer) const;

    // HLMV's idle loop: realtime drives IK latching and autoplay sequences, scaledDt advances cycles.
    void Advance(float realDt, float speedScale);
    float GetCycle(int layer) const; // layer 0 = main sequence, 1.. = overlays (upstream numbering)
    float GetFrame(int layer) const;
    int GetMaxFrame(int layer) const;
    int SetFrame(int layer, int frame);
    float GetFPS() const;
    float GetDuration(int sequence) const;
    float GetGroundSpeed() const;

    void SetBlendTime(float seconds);
    void StartBlending();
    float GetTransitionAmount() const;
    bool blendSequenceChanges = false;
    bool enableIK = false;

    // Bone-to-model matrices for the current pose. `modelToView` is the camera transform jiggle bones react to.
    void SetUpBones(std::vector<QMatrix4x4> &boneToModel, const QMatrix4x4 &modelToView = {});

    // The main model's jiggle rule of `bone`, and changing it in the viewer's copy of the MDL (SaveMdlJiggles writes
    // the files). SetJiggle returns false, changing nothing, when the bone has another procedural rule.
    std::optional<JiggleParams> GetJiggle(int bone) const;
    bool SetJiggle(int bone, const std::optional<JiggleParams> &params);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
