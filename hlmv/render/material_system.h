#pragma once

// Game-agnostic material system: finds and parses VMTs through the game filesystem and hands the parameters
// to a ShaderSet that builds the shader-specific Material.

#include <initializer_list>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

#include <QColor>
#include <QOpenGLTexture>
#include <QStringList>
#include <QMatrix4x4>
#include <QVector4D>

#include "game_filesystem.h"
#include "shader_api.h"
#include "vmt_patch.h"

class KeyValues;
class MaterialProxies;
struct MaterialAnimatedTexture;

// BaseVSShader's BlendType_t: none, SRC_ALPHA/ONE_MINUS_SRC_ALPHA, ONE/ONE, SRC_ALPHA/ONE.
enum class MaterialBlend { None, Blend, Add, BlendAdd };

// VTF uploads shared by every material built with the cache: a material rebuilt after an edit, or another material
// naming the same file, reuses the GL texture instead of reading and uploading the file again. The cache only observes
// textures; each lives while a material holds it.
class TextureCache
{
public:
    struct Texture
    {
        std::shared_ptr<QOpenGLTexture> texture;
        uint32_t flags = 0; // VTF flags
        int frames = 1;     // animation frames
        QByteArray bytes;   // the file, kept only with frames > 1 so other frames upload on demand
    };
    // The VTF at `path` uploaded for `target`, sRGB or linear, alpha read as 1 when `opaqueAlpha` (needs
    // QOpenGLTexture::Swizzle). Throws std::runtime_error with the path on failure.
    Texture load(const QString &path, QOpenGLTexture::Target target, bool srgb, bool opaqueAlpha);

private:
    struct Entry
    {
        std::weak_ptr<QOpenGLTexture> texture;
        Texture info; // texture member unset
    };
    std::map<std::tuple<QString, int, bool, bool>, Entry> m_entries;
};

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
    // The GL texture a slot shows: the shared VTF upload MaterialParams loaded for it, else the slot itself.
    QOpenGLTexture &Resolve(QOpenGLTexture &slot);
    // Value a proxy wrote to `key`, e.g. "$color2": scalars replicated, vectors xyz (w = 1 under size 4).
    std::optional<QVector4D> ProxyValue(const char *key) const;
    // A proxy-written matrix such as "$basetexturetransform", laid out like VMatrix (row-major).
    std::optional<QMatrix4x4> ProxyTransform(const char *key) const;
    bool HasTimeVaryingProxies() const;

    std::unique_ptr<MaterialProxies> proxies; // null without a "Proxies" block
    std::vector<MaterialAnimatedTexture> animated; // only textures whose frame can differ from 0
    std::vector<std::pair<const QOpenGLTexture *, std::shared_ptr<QOpenGLTexture>>> shared; // slot -> TextureCache upload
};

// One VMT's parameters for ShaderSet::CreateMaterial. Every key a shader reads (or Accepts) counts as
// applied; the rest end up in Material::ignored so unsupported features are reported, not silently dropped.
class MaterialParams
{
public:
    MaterialParams(QString vmtPath, KeyValues &kv, const GameFileSystem &files, TextureCache &textures);

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
    // file path, or "(default)"; `hasAlpha` receives ITexture::IsTranslucent; `opaqueAlpha` reads the alpha channel as 1.
    QString getTexture(QOpenGLTexture &texture, const char *key, QColor fallback, bool srgb, bool *hasAlpha = nullptr,
                       bool opaqueAlpha = false);
    // Loads materials/<name>.vtf directly, e.g. what a $envmap name resolves to; returns the file path.
    QString loadTexture(QOpenGLTexture &texture, const QString &name, bool srgb, bool *hasAlpha = nullptr);
    void accept(std::initializer_list<const char *> keys); // parameters without a visible effect here
    QStringList unread() const;
    KeyValues &keyValues() { return m_kv; }
    // Every VTF getTexture loaded while the VMT can animate textures (proxies or frame parameters).
    struct LoadedTexture
    {
        QString key;
        QOpenGLTexture *texture; // the shader's slot
        TextureCache::Texture upload;
        QString path;
        bool srgb;
    };
    const std::vector<LoadedTexture> &loadedTextures() const { return m_textures; }
    // The slots MaterialParams filled from the TextureCache, for Material::shared.
    const auto &sharedTextures() const { return m_shared; }

    [[noreturn]] void fail(const QString &reason) const;

private:
    QString m_path;
    KeyValues &m_kv;
    const GameFileSystem &m_files;
    TextureCache &m_cache;
    std::vector<std::pair<const QOpenGLTexture *, std::shared_ptr<QOpenGLTexture>>> m_shared;
    TextureCache::Texture upload(QOpenGLTexture &slot, const QString &name, bool srgb, bool opaqueAlpha, QString &path);
    std::set<QString> m_read;
    std::vector<LoadedTexture> m_textures;
};

// Finds `material` under the model's mounted game directories (MDL cdmaterials `searchPaths`, in order) and
// builds it with `shaders`, with the Materials tab's `edits` applied over the VMT's parameters and its textures from
// `textures`. Throws std::runtime_error with the VMT or texture path on failure.
std::unique_ptr<Material> LoadMaterial(const ShaderSet &shaders, const QString &modelPath, const QString &material,
                                       const QStringList &searchPaths, TextureCache &textures,
                                       const MaterialEdits &edits = {});

// What the Materials tab lists for a model material: the VMT LoadMaterial reads and its root parameters after patch
// expansion and the fallback block, first occurrence of each name, in file order.
struct MaterialSource
{
    QString vmt, shader;
    std::vector<std::pair<QString, QString>> params;
};
MaterialSource ReadMaterialSource(const QString &modelPath, const QString &material, const QStringList &searchPaths);

// Writes `edits` into `vmt` with PatchVmtText, keeping its layout. Throws std::runtime_error, leaving the file untouched,
// when the patched file would not read back with exactly the edited parameters or cannot be written.
void SaveMaterialEdits(const QString &modelPath, const QString &vmt, const MaterialEdits &edits);
