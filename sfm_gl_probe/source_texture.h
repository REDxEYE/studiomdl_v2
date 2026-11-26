#pragma once

#include <QOpenGLTexture>
#include <QStringList>
#include <QVector4D>

#include "shader_combos.h"

enum class MaterialShader { Unlit, Diffuse, Phong, Missing };
enum class MaterialBlend { Opaque, AlphaTest, Translucent, Additive, TranslucentAdditive };

struct SourceMaterial
{
    MaterialShader shader;
    ShaderCombo combo;
    MaterialBlend blend = MaterialBlend::Opaque;
    float alphaTestReference = 0.5f;
    QVector4D modulation{1, 1, 1, 1}; // pixel shader c1: linear($color * $color2), $alpha
    QStringList ignored;              // VMT parameters this renderer does not apply
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
