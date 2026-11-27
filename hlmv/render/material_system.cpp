#include "material_system.h"

#include <cmath>
#include <sstream>

#include "texture_upload.h"
#include "tier0/dbg.h"
#include "tier1/keyvalues.h"

// tier0's command-line parser uses the compiler diagnostic hook even in standalone tools.
[[noreturn]] void MdlErrorV(std::string_view fmt, std::format_args args) { ErrorV(fmt, args); }

MaterialParams::MaterialParams(QString vmtPath, KeyValues &kv, const GameFileSystem &files)
    : m_path(std::move(vmtPath)), m_kv(kv), m_files(files)
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

QString MaterialParams::getTexture(QOpenGLTexture &texture, const char *key, QColor fallback, bool srgb, bool *hasAlpha)
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
    return loadTexture(texture, name, srgb, hasAlpha);
}

QString MaterialParams::loadTexture(QOpenGLTexture &texture, const QString &name, bool srgb, bool *hasAlpha)
{
    const auto path = m_files.FindMaterial(name + QStringLiteral(".vtf"));
    if (path.isEmpty())
        fail(QStringLiteral("could not find %1.vtf").arg(name));
    const auto flags = UploadVtf(texture, path, srgb);
    if (hasAlpha)
        *hasAlpha = VtfHasAlpha(flags);
    return path;
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

namespace
{
// CMaterial's fallback blocks (cmaterial.cpp FindBuiltinFallbackBlock, then the shader's _DX9 fallback): the first
// matching sub-block, named "<condition>" or "<shader>_<condition>", overrides the root parameters.
// shortcut: fixed SFM hardware (dxlevel 95, no gpu_level/hdr/srgb blocks); make it per-game config with other games.
void ApplyFallbackBlock(KeyValues &kv)
{
    const std::string shader(kv.GetName());
    KeyValues *block = nullptr;
    for (const std::string condition : {">=DX90_20b", ">=DX90", ">DX90", "dx9", "DX9"})
        if ((block = kv.FindKey(condition)) || (block = kv.FindKey(shader + '_' + condition)))
            break;
    if (!block)
        return;
    for (auto *value = block->GetFirstValue(); value; value = value->GetNextValue())
        kv.SetString(value->GetName(), value->GetString());
}

void LoadVmt(KeyValues &kv, const QString &path)
{
    const auto text = ReadAssetFile(path, 1024 * 1024);
    if (!kv.LoadFromBuffer(path.toUtf8().constData(), text.constData()))
        ThrowAssetError(path, QStringLiteral("invalid VMT"));
}

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
} // namespace

std::unique_ptr<Material> LoadMaterial(const ShaderSet &shaders, const QString &modelPath, const QString &material,
                                       const QStringList &searchPaths)
{
    const auto files = GameFileSystem::ForModel(modelPath);
    QString vmt;
    for (const auto &path : searchPaths)
    {
        vmt = files.FindMaterial((path.isEmpty() ? material : path + '/' + material) + QStringLiteral(".vmt"));
        if (!vmt.isEmpty())
            break;
    }
    if (vmt.isEmpty())
        ThrowAssetError(modelPath, QStringLiteral("could not find loose VMT for %1 in %2").arg(material, files.dirs().join(QStringLiteral(", "))));

    KeyValues kv("material");
    LoadVmt(kv, vmt);
    ExpandPatch(kv, files, vmt);
    ApplyFallbackBlock(kv);
    MaterialParams params(vmt, kv, files);
    auto out = shaders.CreateMaterial(params);
    // CBaseShader::SetInitialShadowState, for every shader
    out->noCull = params.getInt("$nocull") != 0;
    out->decal = params.getInt("$decal") != 0;
    out->ignoreZ = params.getInt("$ignorez") != 0;
    out->ignored = params.unread();
    return out;
}
