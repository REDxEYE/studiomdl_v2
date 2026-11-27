#pragma once

// Game-agnostic material system: finds and parses VMTs through the game filesystem and hands the parameters
// to a ShaderSet that builds the shader-specific Material.

#include <initializer_list>
#include <memory>
#include <set>

#include <QColor>
#include <QOpenGLTexture>
#include <QStringList>
#include <QVector4D>

#include "game_filesystem.h"
#include "shader_api.h"

class KeyValues;

enum class MaterialBlend { Opaque, AlphaTest, Translucent, Additive, TranslucentAdditive };

// What the renderer needs of every material; shader sets derive their parameter blocks from it.
class Material
{
public:
    virtual ~Material() = default;
    const Shader *shader = nullptr; // null: draw the missing-material checker
    ShaderCombo combo;
    MaterialBlend blend = MaterialBlend::Opaque;
    float alphaTestReference = 0.5f;
    QOpenGLTexture base{QOpenGLTexture::Target2D}; // $basetexture, or the 1x1 default
    QStringList ignored;                           // VMT parameters the shader did not read
};

// One VMT's parameters for ShaderSet::CreateMaterial. Every key a shader reads (or Accepts) counts as
// applied; the rest end up in Material::ignored so unsupported features are reported, not silently dropped.
class MaterialParams
{
public:
    MaterialParams(QString vmtPath, KeyValues &kv, const GameFileSystem &files);

    const QString &path() const { return m_path; }
    QString shaderName() const;
    bool isShader(const char *name) const;

    bool has(const char *key);
    int getInt(const char *key, int fallback = 0);
    float getFloat(const char *key, float fallback = 0);
    QString getString(const char *key);
    // [r g b] in 0..1 or {r g b} in 0..255, like Source's material vector parser; leaves `value` when absent.
    void getVector(const char *key, QVector4D &value);
    // Loads texture `key`, or a 1x1 `fallback` when absent (Source binds its default textures). Returns the
    // file path, or "(default)".
    QString getTexture(QOpenGLTexture &texture, const char *key, QColor fallback, bool srgb);
    // Loads materials/<name>.vtf directly, e.g. what a $envmap name resolves to; returns the file path.
    QString loadTexture(QOpenGLTexture &texture, const QString &name, bool srgb);
    void accept(std::initializer_list<const char *> keys); // parameters without a visible effect here
    QStringList unread() const;

    [[noreturn]] void fail(const QString &reason) const;

private:
    QString m_path;
    KeyValues &m_kv;
    const GameFileSystem &m_files;
    std::set<QString> m_read;
};

// Finds `material` under the model's mounted game directories (MDL cdmaterials `searchPaths`, in order) and
// builds it with `shaders`. Throws std::runtime_error with the VMT or texture path on failure.
std::unique_ptr<Material> LoadMaterial(const ShaderSet &shaders, const QString &modelPath, const QString &material,
                                       const QStringList &searchPaths);
