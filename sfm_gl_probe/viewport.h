#pragma once

#include <functional>
#include <memory>

#include <QColor>
#include <QOpenGLWindow>

#include "mdl_mesh.h"

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
    bool centerView = false;
    bool centerVerts = false;
    std::function<void(const QString &)> reportError;
    std::vector<float> flexRaw; // per flex controller, 0..1 like HLMV's StudioModel::m_flexweight
    int flexRevision = 0;       // bump after changing flexRaw
};

void ConfigureSfmViewport();
std::unique_ptr<QOpenGLWindow> CreateSfmViewport(QString vertexShader, QString fragmentShader, QString modelPath,
                                           MdlMesh mesh, bool check, bool checker, ViewportSettings *settings = nullptr);
std::unique_ptr<QOpenGLWindow> CreateSfmVcsViewport(QString shaderDirectory, QString modelPath, MdlMesh mesh,
                                                   bool check, ViewportSettings &settings);
