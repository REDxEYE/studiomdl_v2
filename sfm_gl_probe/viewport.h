#pragma once

#include <functional>
#include <memory>

#include <QColor>
#include <QOpenGLWindow>

#include "mdl_mesh.h"

class StudioAnimation;

struct ViewportSettings
{
    QColor background{64, 64, 64};
    QColor light{255, 255, 255};
    QColor ambient{77, 77, 77}; // HLMV default 0.3
    bool specular = true;
    bool wireframe = false;
    bool wireframeOverlay = false;
    bool showOriginAxis = false;
    float originAxisLength = 10.0f;
    bool showBones = false;
    bool showAttachments = false;
    bool showHitboxes = false;
    bool showNormals = false;
    bool showTangents = false;
    std::vector<int> bodygroups; // selected submodel per body part
    int skin = 0;                // skin family
    std::vector<MdlHitboxSet> hitboxSets; // Bones tab's editable copy of the model's sets
    int hitboxSet = 0;
    int highlightBone = -1;      // HLMV's highlightBone; -1 = none
    int highlightHitbox = -1;    // index in the current set; out of range dims every box
    bool showBoneNames = false;
    bool centerView = false;
    bool centerVerts = false;
    std::shared_ptr<StudioAnimation> animation; // poses skinned models each frame; null draws the rest pose
    float speedScale = 1.0f;                    // HLMV's speedScale: animation clock multiplier
    float fov = 65.0f; // horizontal degrees, like HLMV's PerspectiveX
    std::function<void(const QString &)> reportError;
    std::vector<float> flexRaw; // per flex controller, 0..1 like HLMV's StudioModel::m_flexweight
    int flexRevision = 0;       // bump after changing flexRaw
};

void ConfigureSfmViewport();
std::unique_ptr<QOpenGLWindow> CreateSfmViewport(QString vertexShader, QString fragmentShader, QString modelPath,
                                           MdlMesh mesh, bool check, bool checker, ViewportSettings *settings = nullptr);
std::unique_ptr<QOpenGLWindow> CreateSfmVcsViewport(QString shaderDirectory, QString modelPath, MdlMesh mesh,
                                                   bool check, ViewportSettings &settings);
