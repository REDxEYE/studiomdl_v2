#pragma once

#include <QOpenGLTexture>
#include <QStringList>
#include <QVector4D>

enum class MaterialShader { Unlit, Diffuse, Phong, Missing };

struct SourceMaterial
{
    MaterialShader shader;
    QOpenGLTexture base{QOpenGLTexture::Target2D};
    QOpenGLTexture normal{QOpenGLTexture::Target2D};
    QOpenGLTexture exponentMap{QOpenGLTexture::Target2D};
    QVector4D fresnelBoost{0, 0.5f, 1, 1};
    QVector4D tint{1, 1, 1, 4};
    float exponent = 0;
    bool alphaMask = false;
    bool luminanceMask = false;
};

QString LoadSourceMaterial(SourceMaterial &out, const QString &modelPath, const QString &material,
                           const QStringList &searchPaths);
