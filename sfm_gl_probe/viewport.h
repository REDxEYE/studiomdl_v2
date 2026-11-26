#pragma once

#include <functional>
#include <memory>

#include <QColor>
#include <QOpenGLWindow>

#include "mdl_mesh.h"

struct ViewportSettings
{
    QColor background{64, 64, 64};
    bool specular = true;
    bool wireframe = false;
    bool centerView = false;
    std::function<void(const QString &)> reportError;
};

void ConfigureSfmViewport();
std::unique_ptr<QOpenGLWindow> CreateSfmViewport(QString vertexShader, QString fragmentShader, QString modelPath,
                                           MdlMesh mesh, bool check, bool checker, ViewportSettings *settings = nullptr);
std::unique_ptr<QOpenGLWindow> CreateSfmVcsViewport(QString shaderDirectory, QString modelPath, MdlMesh mesh,
                                                   bool check, ViewportSettings &settings);
