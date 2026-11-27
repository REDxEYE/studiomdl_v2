#pragma once

// Game-agnostic material system: finds and parses VMTs through the game filesystem and hands the parameters
// to a ShaderSet that builds the shader-specific Material.

#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <QColor>
#include <QOpenGLTexture>
#include <QStringList>
#include <QMatrix4x4>
#include <QVector4D>

#include "game_filesystem.h"
#include "shader_api.h"

class KeyValues;
class MaterialProxies;
struct MaterialAnimatedTexture;

// BaseVSShader's BlendType_t: none, SRC_ALPHA/ONE_MINUS_SRC_ALPHA, ONE/ONE, SRC_ALPHA/ONE.
enum class MaterialBlend { None, Blend, Add, BlendAdd };

// What the renderer needs of every material; shader sets derive their parameter blocks from it.
class Material
{
public:
    Material();
    virtual ~Material();
    const Shader *shader = nullptr; // null: draw the missing-material checker
    ShaderCombo combo;
    MaterialBlend blend = MaterialBlend::None; // blending also disables depth writes (CBaseShader::EnableAlphaBlending)
    bool alphaTest = false;
    float alphaTestReference = 0.7f; // CShaderShadowDX8::SetDefaultState's GEQUAL reference
    // CBaseShader::SetInitialShadowState flags, read for every shader
    bool noCull = false, decal = false, ignoreZ = false;
    // studiorender draws these meshes in its second, translucent pass (R_StudioRenderModel)
    bool translucent() const { return effectiveBlend() != MaterialBlend::None; }
    // `blend`, with a proxy-written $alpha < 1 turning None into Blend and Add into BlendAdd.
    MaterialBlend effectiveBlend() const;
    QOpenGLTexture base{QOpenGLTexture::Target2D}; // $basetexture, or the 1x1 default
    QStringList ignored;                           // VMT parameters the shader did not read, and "proxy:" entries
                                                   // for proxies that are unsupported or skipped

    // VMT proxies (see material_proxies.h). The parent calls UpdateProxies once per frame before drawing; the
    // others are cheap reads that do no I/O; BindTexture uploads new frames lazily from bytes read at load time.
    void UpdateProxies(double seconds);
    // Binds `texture` (any texture a shader loaded through MaterialParams::getTexture, or `base`) to `unit`: its
    // current animation frame when a proxy or the VMT selects one through $frame/$detailframe/$bumpframe/..., else
    // the texture itself. BindBaseTexture() is BindTexture(base, 0).
    void BindTexture(QOpenGLTexture &texture, int unit);
    void BindBaseTexture();
    // Value a proxy wrote to `key`, e.g. "$color2": scalars replicated, vectors xyz (w = 1 under size 4).
    std::optional<QVector4D> ProxyValue(const char *key) const;
    // A proxy-written matrix such as "$basetexturetransform", laid out like VMatrix (row-major).
    std::optional<QMatrix4x4> ProxyTransform(const char *key) const;
    bool HasTimeVaryingProxies() const;

    std::unique_ptr<MaterialProxies> proxies; // null without a "Proxies" block
    std::vector<MaterialAnimatedTexture> animated; // only textures whose frame can differ from 0
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
    // file path, or "(default)"; `hasAlpha` receives ITexture::IsTranslucent.
    QString getTexture(QOpenGLTexture &texture, const char *key, QColor fallback, bool srgb, bool *hasAlpha = nullptr);
    // Loads materials/<name>.vtf directly, e.g. what a $envmap name resolves to; returns the file path.
    QString loadTexture(QOpenGLTexture &texture, const QString &name, bool srgb, bool *hasAlpha = nullptr);
    void accept(std::initializer_list<const char *> keys); // parameters without a visible effect here
    QStringList unread() const;
    KeyValues &keyValues() { return m_kv; }
    // Every VTF getTexture loaded, for texture animation.
    struct LoadedTexture
    {
        QString key, path;
        bool srgb;
        QOpenGLTexture *texture;
    };
    const std::vector<LoadedTexture> &loadedTextures() const { return m_textures; }

    [[noreturn]] void fail(const QString &reason) const;

private:
    QString m_path;
    KeyValues &m_kv;
    const GameFileSystem &m_files;
    std::set<QString> m_read;
    std::vector<LoadedTexture> m_textures;
};

// Finds `material` under the model's mounted game directories (MDL cdmaterials `searchPaths`, in order) and
// builds it with `shaders`. Throws std::runtime_error with the VMT or texture path on failure.
std::unique_ptr<Material> LoadMaterial(const ShaderSet &shaders, const QString &modelPath, const QString &material,
                                       const QStringList &searchPaths);
