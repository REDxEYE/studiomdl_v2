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

QString MaterialParams::getTexture(QOpenGLTexture &texture, const char *key, QColor fallback, bool srgb)
{
    auto name = getString(key);
    if (name.isEmpty())
    {
        UploadSolidColor(texture, fallback, srgb);
        return QStringLiteral("(default)");
    }
    if (name.endsWith(QStringLiteral(".vtf"), Qt::CaseInsensitive))
        name.chop(4);
    return loadTexture(texture, name, srgb);
}

QString MaterialParams::loadTexture(QOpenGLTexture &texture, const QString &name, bool srgb)
{
    const auto path = m_files.FindMaterial(name + QStringLiteral(".vtf"));
    if (path.isEmpty())
        fail(QStringLiteral("could not find %1.vtf").arg(name));
    UploadVtf(texture, path, srgb);
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

    const auto text = ReadAssetFile(vmt, 1024 * 1024);
    KeyValues kv("material");
    if (!kv.LoadFromBuffer(vmt.toUtf8().constData(), text.constData()))
        ThrowAssetError(vmt, QStringLiteral("invalid VMT"));
    MaterialParams params(vmt, kv, files);
    auto out = shaders.CreateMaterial(params);
    out->ignored = params.unread();
    return out;
}
