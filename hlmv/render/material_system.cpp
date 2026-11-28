#include "material_system.h"

#include <cmath>
#include <map>
#include <sstream>

#include <QDebug>
#include <QSaveFile>
#include <QSet>

#include "material_proxies.h"
#include "texture_upload.h"
#include "tier0/dbg.h"
#include "tier1/keyvalues.h"
#include "vmt_patch.h"

// tier0's command-line parser uses the compiler diagnostic hook even in standalone tools.
[[noreturn]] void MdlErrorV(std::string_view fmt, std::format_args args) { ErrorV(fmt, args); }

MaterialParams::MaterialParams(QString vmtPath, KeyValues &kv, const GameFileSystem &files, TextureCache &textures)
    : m_path(std::move(vmtPath)), m_kv(kv), m_files(files), m_cache(textures)
{
}

QString MaterialParams::shaderName() const { return QString::fromUtf8(m_kv.GetName()); }

bool MaterialParams::isShader(const char *name) const { return shaderName().compare(QLatin1String(name), Qt::CaseInsensitive) == 0; }

void MaterialParams::fail(const QString &reason) const { ThrowAssetError(m_path, reason); }

bool MaterialParams::has(const char *key) { return !getString(key).isEmpty(); }

int MaterialParams::getInt(const char *key, int fallback)
{
    m_read.insert(QString::fromLatin1(key).toLower());
    return m_kv.GetInt(key, fallback);
}

float MaterialParams::getFloat(const char *key, float fallback)
{
    m_read.insert(QString::fromLatin1(key).toLower());
    return m_kv.GetFloat(key, fallback);
}

QString MaterialParams::getString(const char *key)
{
    m_read.insert(QString::fromLatin1(key).toLower());
    return QString::fromUtf8(m_kv.GetString(key));
}

void MaterialParams::getVector(const char *key, QVector4D &value)
{
    const auto text = getString(key).toStdString();
    if (text.empty())
        return;
    const bool bytes = text.front() == '{';
    if (text.size() < 2 || (text.front() != '[' && !bytes) || text.back() != (bytes ? '}' : ']'))
        fail(QStringLiteral("expected [r g b] or {r g b} for %1").arg(QString::fromLatin1(key)));
    std::istringstream input(text.substr(1, text.size() - 2));
    float x, y, z;
    if (!(input >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        fail(QStringLiteral("invalid %1").arg(QString::fromLatin1(key)));
    const float scale = bytes ? 1.f / 255.f : 1.f;
    value = {x * scale, y * scale, z * scale, value.w()};
}

QString MaterialParams::getTexture(QOpenGLTexture &texture, const char *key, QColor fallback, bool srgb, bool *hasAlpha,
                                   bool opaqueAlpha)
{
    auto name = getString(key);
    if (name.isEmpty())
    {
        UploadSolidColor(texture, fallback, srgb);
        if (hasAlpha)
            *hasAlpha = fallback.alpha() != 255;
        return QStringLiteral("(default)");
    }
    if (name.endsWith(QStringLiteral(".vtf"), Qt::CaseInsensitive))
        name.chop(4);
    QString path;
    auto loaded = upload(texture, name, srgb, opaqueAlpha, path);
    if (hasAlpha)
        *hasAlpha = VtfHasAlpha(loaded.flags);
    if (m_kv.FindKey("Proxies") || m_kv.FindKey("$frame") || m_kv.FindKey("$detailframe") ||
        m_kv.FindKey("$bumpframe") || m_kv.FindKey("$envmapmaskframe"))
        m_textures.push_back({QString::fromLatin1(key).toLower(), &texture, std::move(loaded), path, srgb});
    return path;
}

QString MaterialParams::loadTexture(QOpenGLTexture &texture, const QString &name, bool srgb, bool *hasAlpha)
{
    QString path;
    const auto loaded = upload(texture, name, srgb, false, path);
    if (hasAlpha)
        *hasAlpha = VtfHasAlpha(loaded.flags);
    return path;
}

TextureCache::Texture MaterialParams::upload(QOpenGLTexture &slot, const QString &name, bool srgb, bool opaqueAlpha, QString &path)
{
    path = m_files.FindMaterial(name + QStringLiteral(".vtf"));
    if (path.isEmpty())
        fail(QStringLiteral("could not find %1.vtf").arg(name));
    auto loaded = m_cache.load(path, slot.target(), srgb, opaqueAlpha);
    m_shared.emplace_back(&slot, loaded.texture);
    return loaded;
}

TextureCache::Texture TextureCache::load(const QString &path, QOpenGLTexture::Target target, bool srgb, bool opaqueAlpha)
{
    std::erase_if(m_entries, [](const auto &entry) { return entry.second.texture.expired(); });
    const std::tuple<QString, int, bool, bool> key{path, int(target), srgb, opaqueAlpha};
    if (const auto it = m_entries.find(key); it != m_entries.end())
    {
        auto out = it->second.info;
        out.texture = it->second.texture.lock();
        return out;
    }
    Texture out;
    auto bytes = ReadAssetFile(path, 128 * 1024 * 1024);
    out.texture = std::make_shared<QOpenGLTexture>(target);
    out.flags = UploadVtf(*out.texture, path, bytes, srgb);
    out.frames = std::max(1, VtfFrameCount(bytes));
    if (out.frames > 1)
        out.bytes = std::move(bytes);
    if (opaqueAlpha)
        out.texture->setSwizzleMask(QOpenGLTexture::SwizzleAlpha, QOpenGLTexture::OneValue);
    auto &entry = m_entries[key];
    entry.texture = out.texture;
    entry.info = out;
    entry.info.texture.reset();
    return out;
}

void MaterialParams::accept(std::initializer_list<const char *> keys)
{
    for (const auto *key : keys)
        m_read.insert(QString::fromLatin1(key).toLower());
}

QStringList MaterialParams::unread() const
{
    QStringList names;
    for (auto *key = m_kv.GetFirstValue(); key; key = key->GetNextValue())
        if (const auto name = QString::fromUtf8(key->GetName()).toLower(); name.startsWith('$') && !m_read.contains(name))
            names.append(name);
    return names;
}

// A texture animated through a frame variable, its other frames uploaded on first use from the bytes read at load.
struct MaterialAnimatedTexture
{
    QOpenGLTexture *target;
    std::string frameVar;
    QString path;
    QByteArray bytes;
    bool srgb, failed = false;
    std::vector<std::unique_ptr<QOpenGLTexture>> frames; // [0] unused: the target itself is frame 0
};

Material::Material() = default;
Material::~Material() = default;

MaterialBlend Material::effectiveBlend() const
{
    const auto alpha = proxies ? proxies->alpha() : std::nullopt;
    if (!alpha || *alpha >= 1)
        return blend;
    return blend == MaterialBlend::None ? MaterialBlend::Blend : blend == MaterialBlend::Add ? MaterialBlend::BlendAdd : blend;
}

void Material::UpdateProxies(double seconds)
{
    if (proxies)
        proxies->update(seconds);
}

bool Material::HasTimeVaryingProxies() const { return proxies && proxies->timeVarying(); }
std::optional<QVector4D> Material::ProxyValue(const char *key) const { return proxies ? proxies->value(key) : std::nullopt; }
std::optional<QMatrix4x4> Material::ProxyTransform(const char *key) const { return proxies ? proxies->transform(key) : std::nullopt; }

void Material::BindBaseTexture() { BindTexture(base, 0); }

QOpenGLTexture &Material::Resolve(QOpenGLTexture &slot)
{
    for (const auto &[from, texture] : shared)
        if (from == &slot)
            return *texture;
    return slot;
}

void Material::BindTexture(QOpenGLTexture &texture, int unit)
{
    for (auto &animation : animated)
    {
        if (animation.target != &texture || animation.failed)
            continue;
        const int count = int(animation.frames.size());
        const int frame = ((proxies->intValue(animation.frameVar.c_str()) % count) + count) % count;
        if (!frame)
            break;
        auto &shown = animation.frames[frame];
        if (!shown)
            try
            {
                auto &shared = Resolve(texture);
                auto fresh = std::make_unique<QOpenGLTexture>(shared.target());
                UploadVtf(*fresh, animation.path, animation.bytes, animation.srgb, frame);
                if (QOpenGLTexture::hasFeature(QOpenGLTexture::Swizzle) &&
                    shared.swizzleMask(QOpenGLTexture::SwizzleAlpha) == QOpenGLTexture::OneValue)
                    fresh->setSwizzleMask(QOpenGLTexture::SwizzleAlpha, QOpenGLTexture::OneValue);
                shown = std::move(fresh);
            }
            catch (const std::exception &error)
            {
                animation.failed = true; // keep showing frame 0
                qWarning().noquote() << animation.path << "could not upload frame" << frame << ':' << error.what();
                break;
            }
        return shown->bind(unit);
    }
    Resolve(texture).bind(unit);
}

namespace
{
// CMaterial's fallback blocks (cmaterial.cpp FindBuiltinFallbackBlock, then the shader's _DX9 fallback): the first
// matching sub-block, named "<condition>" or "<shader>_<condition>", overrides the root parameters.
// shortcut: fixed SFM hardware (dxlevel 95, no gpu_level/hdr/srgb blocks); make it per-game config with other games.
void ApplyFallbackBlock(KeyValues &kv)
{
    const std::string shader(kv.GetName());
    KeyValues *block = nullptr;
    for (const std::string condition : kVmtFallbackConditions)
        if ((block = kv.FindKey(condition)) || (block = kv.FindKey(shader + '_' + condition)))
            break;
    if (!block)
        return;
    for (auto *value = block->GetFirstValue(); value; value = value->GetNextValue())
        kv.SetString(value->GetName(), value->GetString());
}

void LoadVmtText(KeyValues &kv, const QString &path, const QByteArray &text)
{
    if (!kv.LoadFromBuffer(path.toUtf8().constData(), text.constData()))
        ThrowAssetError(path, QStringLiteral("invalid VMT"));
}

void LoadVmt(KeyValues &kv, const QString &path) { LoadVmtText(kv, path, ReadAssetFile(path, 1024 * 1024)); }

// cmaterial.cpp InsertKeyValues: src's values into dst (only keys dst already has when `existing`); "replace" also
// recurses into sub-blocks both sides have.
void InsertKeyValues(KeyValues &dst, KeyValues &src, bool existing)
{
    for (auto *value = src.GetFirstValue(); value; value = value->GetNextValue())
        if (!existing || dst.FindKey(value->GetName()))
            dst.SetString(value->GetName(), value->GetString());
    if (existing)
        for (auto *block = dst.GetFirstTrueSubKey(); block; block = block->GetNextTrueSubKey())
            if (auto *from = src.FindKey(block->GetName()); from && from->GetDataType() == KeyValues::TYPE_NONE)
                InsertKeyValues(*block, *from, true);
}

// cmaterial.cpp ExpandPatchFile: a "patch" VMT includes another VMT (at most 10 deep); every patch's insert/replace
// values accumulate (included files overwrite keys, as MergeKeyValues does) and apply to the final VMT.
void ExpandPatch(KeyValues &kv, const GameFileSystem &files, const QString &vmt)
{
    KeyValues insert("insert"), replace("replace");
    auto path = vmt;
    for (int depth = 0; QString::fromUtf8(kv.GetName()).compare(QStringLiteral("patch"), Qt::CaseInsensitive) == 0; ++depth)
    {
        if (depth == 10)
            ThrowAssetError(vmt, QStringLiteral("patch includes nest deeper than 10"));
        for (auto [section, into] : {std::pair{"insert", &insert}, std::pair{"replace", &replace}})
            if (auto *from = kv.FindKey(section))
                for (auto *value = from->GetFirstValue(); value; value = value->GetNextValue())
                    into->SetString(value->GetName(), value->GetString());
        const auto include = QString::fromUtf8(kv.GetString("include"));
        if (include.isEmpty())
            ThrowAssetError(path, QStringLiteral("patch VMT without include"));
        const auto next = files.Find(include);
        if (next.isEmpty())
            ThrowAssetError(path, QStringLiteral("could not find patch include %1").arg(include));
        path = next;
        kv.Clear();
        LoadVmt(kv, path);
    }
    InsertKeyValues(kv, insert, false);
    InsertKeyValues(kv, replace, true);
}

QString FindVmt(const GameFileSystem &files, const QString &modelPath, const QString &material, const QStringList &searchPaths)
{
    for (const auto &path : searchPaths)
        if (auto vmt = files.FindMaterial((path.isEmpty() ? material : path + '/' + material) + QStringLiteral(".vmt")); !vmt.isEmpty())
            return vmt;
    ThrowAssetError(modelPath, QStringLiteral("could not find loose VMT for %1 in %2").arg(material, files.dirs().join(QStringLiteral(", "))));
}

// The parameters the shader sees: `text` (the VMT at `vmt`) after patch includes and the fallback block.
void ReadVmt(KeyValues &kv, const GameFileSystem &files, const QString &vmt, const QByteArray &text)
{
    LoadVmtText(kv, vmt, text);
    ExpandPatch(kv, files, vmt);
    ApplyFallbackBlock(kv);
}

// KeyValues stores numeric values as numbers and prints them back as "%d"/"%f": compare and show numbers in one form.
QString NormalizedValue(const QString &value)
{
    bool number = false;
    const float f = value.toFloat(&number);
    return number ? QString::number(f, 'g', 7) : value;
}

// Root values by lower-case name; duplicates read as the first, like KeyValues::GetString.
std::map<QString, QString> RootValues(KeyValues &kv)
{
    std::map<QString, QString> values;
    for (auto *value = kv.GetFirstValue(); value; value = value->GetNextValue())
        values.try_emplace(QString::fromUtf8(value->GetName()).toLower(), NormalizedValue(QString::fromUtf8(value->GetString())));
    return values;
}
} // namespace

MaterialSource ReadMaterialSource(const QString &modelPath, const QString &material, const QStringList &searchPaths)
{
    const auto files = GameFileSystem::ForModel(modelPath);
    MaterialSource source;
    source.vmt = FindVmt(files, modelPath, material, searchPaths);
    KeyValues kv("material");
    ReadVmt(kv, files, source.vmt, ReadAssetFile(source.vmt, 1024 * 1024));
    source.shader = QString::fromUtf8(kv.GetName());
    QSet<QString> seen;
    for (auto *value = kv.GetFirstValue(); value; value = value->GetNextValue())
        if (auto name = QString::fromUtf8(value->GetName()); !seen.contains(name.toLower()))
        {
            seen.insert(name.toLower());
            source.params.emplace_back(std::move(name), NormalizedValue(QString::fromUtf8(value->GetString())));
        }
    return source;
}

void SaveMaterialEdits(const QString &modelPath, const QString &vmt, const MaterialEdits &edits)
{
    const auto files = GameFileSystem::ForModel(modelPath);
    const auto text = ReadAssetFile(vmt, 1024 * 1024);
    KeyValues before("material");
    ReadVmt(before, files, vmt, text);
    auto expected = RootValues(before);
    for (const auto &[key, value] : edits)
        if (!value)
            expected.erase(key);
        else
            expected[key] = NormalizedValue(*value);
    QByteArray patched;
    try
    {
        patched = PatchVmtText(QString::fromUtf8(text), edits).toUtf8();
    }
    catch (const std::runtime_error &error)
    {
        ThrowAssetError(vmt, QString::fromUtf8(error.what()));
    }
    // Refuse to write a file that would not read back as edited (e.g. a value an included VMT or fallback block wins).
    KeyValues after("material");
    ReadVmt(after, files, vmt, patched);
    const auto actual = RootValues(after);
    if (actual != expected || QString::fromUtf8(after.GetName()) != QString::fromUtf8(before.GetName()))
    {
        for (const auto &[key, value] : edits)
            if (const auto it = actual.find(key); !value ? it != actual.end() : it == actual.end() || it->second != NormalizedValue(*value))
                ThrowAssetError(vmt, QStringLiteral("%1 is set elsewhere (an included VMT or a fallback block); edit it there").arg(key));
        ThrowAssetError(vmt, QStringLiteral("edits would change other parameters; not written"));
    }
    QSaveFile file(vmt);
    if (!file.open(QIODevice::WriteOnly) || file.write(patched) != patched.size() || !file.commit())
        ThrowAssetError(vmt, file.errorString());
}

std::unique_ptr<Material> LoadMaterial(const ShaderSet &shaders, const QString &modelPath, const QString &material,
                                       const QStringList &searchPaths, TextureCache &textures, const MaterialEdits &edits)
{
    const auto files = GameFileSystem::ForModel(modelPath);
    const auto vmt = FindVmt(files, modelPath, material, searchPaths);
    KeyValues kv("material");
    ReadVmt(kv, files, vmt, ReadAssetFile(vmt, 1024 * 1024));
    for (const auto &[key, value] : edits)
    {
        const auto name = key.toUtf8();
        const std::string_view view(name.constData(), name.size());
        if (value)
            kv.SetString(view, value->toUtf8().constData());
        else
            while (auto *entry = kv.FindKey(view))
            {
                kv.RemoveSubKey(entry);
                delete entry;
            }
    }

    MaterialParams params(vmt, kv, files, textures);
    auto out = shaders.CreateMaterial(params);
    out->shared = params.sharedTextures();
    // Frame variable each texture key is animated through.
    static const std::pair<const char *, const char *> frameVars[] = {
        {"$basetexture", "$frame"}, {"$basetexture2", "$frame2"}, {"$detail", "$detailframe"}, {"$bumpmap", "$bumpframe"},
        {"$envmapmask", "$envmapmaskframe"}};
    const auto frameVarOf = [&](const QString &key) -> const char * {
        for (const auto &[texture, frame] : frameVars)
            if (key == QLatin1String(texture))
                return frame;
        return nullptr;
    };
    if (kv.FindKey("Proxies") || !params.loadedTextures().empty())
    {
        auto proxies = std::make_unique<MaterialProxies>(kv, [&](const QString &var) {
            for (const auto &texture : params.loadedTextures())
                if (texture.key == var && frameVarOf(var))
                    return texture.upload.frames;
            return 0;
        });
        for (const auto &texture : params.loadedTextures())
            if (const char *frameVar = frameVarOf(texture.key);
                frameVar && (proxies->writes(frameVar) || proxies->intValue(frameVar) != 0))
            {
                if (const int count = texture.upload.frames; count > 1)
                {
                    params.accept({frameVar});
                    out->animated.push_back({texture.texture, frameVar, texture.path, texture.upload.bytes, texture.srgb, false, {}});
                    out->animated.back().frames.resize(count);
                }
            }
        if (proxies->declared() || !out->animated.empty())
        {
            out->ignored += proxies->unsupported();
            out->proxies = std::move(proxies);
            out->UpdateProxies(0);
        }
    }
    // CBaseShader::SetInitialShadowState, for every shader
    out->noCull = params.getInt("$nocull") != 0;
    out->decal = params.getInt("$decal") != 0;
    out->ignoreZ = params.getInt("$ignorez") != 0;
    out->ignored += params.unread();
    return out;
}
