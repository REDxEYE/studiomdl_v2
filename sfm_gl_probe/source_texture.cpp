#include "source_texture.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QOpenGLPixelTransferOptions>
#include <QtEndian>

#include "tier0/dbg.h"
#include "tier1/keyvalues.h"

// tier0's command-line parser uses the compiler diagnostic hook even in standalone tools.
[[noreturn]] void MdlErrorV(std::string_view fmt, std::format_args args) { ErrorV(fmt, args); }

namespace
{
[[noreturn]] void Fail(const QString &path, const QString &reason)
{
    throw std::runtime_error((path + QStringLiteral(": ") + reason).toStdString());
}

QByteArray ReadFile(const QString &path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        Fail(path, file.errorString());
    const auto size = file.size();
    if (size > limit)
        Fail(path, QStringLiteral("file exceeds preview size limit"));
    auto bytes = file.read(size);
    if (bytes.size() != size)
        Fail(path, QStringLiteral("incomplete read"));
    return bytes;
}

QString FindAsset(QString root, QString relative)
{
    relative.replace('\\', '/');
    if (relative.startsWith('/') || relative.contains(':'))
        return {};
    for (const auto &part : relative.split('/', Qt::SkipEmptyParts))
    {
        if (part == QStringLiteral("..") || part == QStringLiteral("."))
            return {};
        QDir dir(root);
        if (dir.exists(part))
        {
            root = dir.filePath(part);
            continue;
        }
        const auto entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        const auto match = std::ranges::find_if(entries, [&](const auto &name) { return name.compare(part, Qt::CaseInsensitive) == 0; });
        if (match == entries.end())
            return {};
        root = dir.filePath(*match);
    }
    return QFileInfo(root).isFile() ? root : QString();
}

using Texture = QOpenGLTexture;
struct Format
{
    Texture::TextureFormat internal;
    Texture::PixelFormat source;
    int bytes;
    bool compressed;

    qsizetype size(int w, int h) const
    {
        return compressed ? qsizetype((w + 3) / 4) * ((h + 3) / 4) * bytes : qsizetype(w) * h * bytes;
    }
};

Format VtfFormat(uint32_t id, const QString &path, bool srgb = false)
{
    switch (id) // Source ImageFormat IDs.
    {
    case 0: return {srgb ? Texture::SRGB8_Alpha8 : Texture::RGBA8_UNorm, Texture::RGBA, 4, false};
    case 2: return {srgb ? Texture::SRGB8 : Texture::RGB8_UNorm, Texture::RGB, 3, false};
    case 3: return {srgb ? Texture::SRGB8 : Texture::RGB8_UNorm, Texture::BGR, 3, false};
    case 12: return {srgb ? Texture::SRGB8_Alpha8 : Texture::RGBA8_UNorm, Texture::BGRA, 4, false};
    case 13: return {srgb ? Texture::SRGB_DXT1 : Texture::RGB_DXT1, Texture::RGB, 8, true};
    case 14: return {srgb ? Texture::SRGB_Alpha_DXT3 : Texture::RGBA_DXT3, Texture::RGBA, 16, true};
    case 15: return {srgb ? Texture::SRGB_Alpha_DXT5 : Texture::RGBA_DXT5, Texture::RGBA, 16, true};
    case 16: return {srgb ? Texture::SRGB8 : Texture::RGB8_UNorm, Texture::BGRA, 4, false};
    case 20: return {srgb ? Texture::SRGB_Alpha_DXT1 : Texture::RGBA_DXT1, Texture::RGBA, 8, true};
    default: Fail(path, QStringLiteral("unsupported VTF image format %1").arg(id));
    }
}

void UploadVtf(Texture &texture, const QString &path, bool srgb)
{
    const auto bytes = ReadFile(path, 128 * 1024 * 1024);
    const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
    const auto u32 = [&](int offset) { return qFromLittleEndian<uint32_t>(data + offset); };
    const auto u16 = [&](int offset) { return qFromLittleEndian<uint16_t>(data + offset); };
    if (bytes.size() < 64 || u32(0) != 0x00465456 || u32(4) != 7 || u32(8) > 5)
        Fail(path, QStringLiteral("expected a PC VTF 7.0–7.5 header"));
    const auto version = u32(8);
    const auto headerSize = u32(12);
    const int width = u16(16), height = u16(18), mipCount = data[56];
    const auto flags = u32(20);
    // shortcut: only single-frame 2D textures; add frame/face/volume selection when materials need it.
    if (headerSize < (version >= 3 ? 80u : version >= 2 ? 65u : 64u) || headerSize > bytes.size() ||
        !width || !height || !mipCount || mipCount > std::bit_width(unsigned(std::max(width, height))) ||
        u16(24) != 1 || (flags & 0x4000) || (version >= 2 && u16(63) != 1))
        Fail(path, QStringLiteral("invalid header or unsupported animated, cube, or volume texture"));

    qsizetype offset = headerSize;
    if (version >= 3)
    {
        const auto count = u32(68);
        if (count > (headerSize - 80) / 8)
            Fail(path, QStringLiteral("truncated VTF resource directory"));
        offset = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (u32(80 + i * 8) == 0x30) // High-resolution image resource, with external data.
            {
                offset = u32(84 + i * 8);
                break;
            }
    }
    else if (data[61] && data[62])
        offset += VtfFormat(u32(57), path).size(data[61], data[62]);

    const auto format = VtfFormat(u32(52), path, srgb);
    qsizetype end = offset;
    for (int mip = mipCount - 1; mip >= 0; --mip)
        end += format.size(std::max(1, width >> mip), std::max(1, height >> mip));
    if (offset < headerSize || end > bytes.size())
        Fail(path, QStringLiteral("VTF image data outside file bounds"));

    texture.setFormat(format.internal);
    texture.setSize(width, height);
    texture.setMipLevels(mipCount);
    texture.setAutoMipMapGenerationEnabled(false);
    texture.allocateStorage(format.source, Texture::UInt8);
    if (!texture.isStorageAllocated())
        Fail(path, QStringLiteral("OpenGL texture allocation failed"));
    QOpenGLPixelTransferOptions unpack;
    unpack.setAlignment(1);
    for (int mip = mipCount - 1; mip >= 0; --mip)
    {
        const auto length = format.size(std::max(1, width >> mip), std::max(1, height >> mip));
        if (format.compressed)
            texture.setCompressedData(mip, int(length), data + offset);
        else
            texture.setData(mip, format.source, Texture::UInt8, data + offset, &unpack);
        offset += length;
    }
    texture.setWrapMode(Texture::DirectionS, flags & 4 ? Texture::ClampToEdge : Texture::Repeat);
    texture.setWrapMode(Texture::DirectionT, flags & 8 ? Texture::ClampToEdge : Texture::Repeat);
    const bool nearest = flags & 1;
    const auto filter = nearest ? Texture::Nearest : Texture::Linear;
    texture.setMinMagFilters(!nearest && mipCount > 1 && !(flags & 0x100) ? Texture::LinearMipMapLinear : filter, filter);
    texture.setMipLevelRange(0, mipCount - 1);
}
// Game directories in Source search order, from SFM's usermod/gameinfo.txt when present (SFM runs
// -game usermod, which mounts every mod) or else the model's own mod. Loose files only: VPKs are skipped.
QStringList MountedGameDirs(const QDir &modDir)
{
    QDir root = modDir;
    root.cdUp();
    const auto usermod = root.filePath(QStringLiteral("usermod/gameinfo.txt"));
    const auto info = QFileInfo::exists(usermod) ? usermod : modDir.filePath(QStringLiteral("gameinfo.txt"));
    QStringList dirs;
    const auto add = [&](const QString &path) {
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty() && QFileInfo(canonical).isDir() && !dirs.contains(canonical))
            dirs.append(canonical);
    };
    if (QFileInfo::exists(info))
    {
        KeyValues kv("GameInfo");
        const auto text = ReadFile(info, 1024 * 1024);
        auto *fileSystem = kv.LoadFromBuffer(info.toUtf8().constData(), text.constData()) ? kv.FindKey("FileSystem") : nullptr;
        auto *searchPaths = fileSystem ? fileSystem->FindKey("SearchPaths") : nullptr;
        for (auto *entry = searchPaths ? searchPaths->GetFirstValue() : nullptr; entry; entry = entry->GetNextValue())
        {
            // Keys are '+'-joined path IDs such as "Game", "game+mod" or "platform".
            const auto ids = QString::fromUtf8(entry->GetName()).toLower().split('+');
            if (!ids.contains(QStringLiteral("game")))
                continue;
            auto path = QString::fromUtf8(entry->GetString()).replace('\\', '/');
            path.replace(QStringLiteral("|gameinfo_path|"), QFileInfo(info).absolutePath() + '/', Qt::CaseInsensitive);
            path.replace(QStringLiteral("|all_source_engine_paths|"), root.absolutePath() + '/', Qt::CaseInsensitive);
            if (QFileInfo(path).isRelative())
                path = root.filePath(path);
            if (path.endsWith(QStringLiteral(".vpk"), Qt::CaseInsensitive))
                continue; // shortcut: VPK archives are not mounted; add vpklib reading when needed
            if (path.endsWith(QStringLiteral("/*")))
            {
                const QDir parent(path.chopped(2));
                for (const auto &child : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                    add(parent.filePath(child));
            }
            else
                add(path);
        }
    }
    add(modDir.absolutePath()); // the model's own mod even when gameinfo does not list it
    return dirs;
}

QString FindMaterialAsset(const QStringList &gameDirs, const QString &relative)
{
    for (const auto &dir : gameDirs)
        if (auto path = FindAsset(QDir(dir).filePath(QStringLiteral("materials")), relative); !path.isEmpty())
            return path;
    return {};
}
} // namespace

QString LoadSourceMaterial(SourceMaterial &out, const QString &modelPath, const QString &material,
                           const QStringList &searchPaths)
{
    QDir root = QFileInfo(modelPath).absoluteDir();
    while (root.dirName().compare(QStringLiteral("models"), Qt::CaseInsensitive) != 0)
        if (!root.cdUp())
            Fail(modelPath, QStringLiteral("expected the model under a game directory's models/ folder"));
    root.cdUp();
    const auto gameDirs = MountedGameDirs(root);
    QString vmt;
    for (const auto &path : searchPaths)
    {
        const auto name = path.isEmpty() ? material : path + '/' + material;
        vmt = FindMaterialAsset(gameDirs, name + QStringLiteral(".vmt"));
        if (!vmt.isEmpty())
            break;
    }
    if (vmt.isEmpty())
        Fail(modelPath, QStringLiteral("could not find loose VMT for %1 in %2").arg(material, gameDirs.join(QStringLiteral(", "))));

    const auto text = ReadFile(vmt, 1024 * 1024);
    KeyValues kv("material");
    if (!kv.LoadFromBuffer(vmt.toUtf8().constData(), text.constData()))
        Fail(vmt, QStringLiteral("invalid VMT"));
    const auto shader = QString::fromUtf8(kv.GetName());
    if (shader.compare(QStringLiteral("VertexLitGeneric"), Qt::CaseInsensitive) != 0 &&
        shader.compare(QStringLiteral("UnlitGeneric"), Qt::CaseInsensitive) != 0)
        Fail(vmt, QStringLiteral("unsupported material shader %1").arg(shader));
    const bool unlit = shader.compare(QStringLiteral("UnlitGeneric"), Qt::CaseInsensitive) == 0;
    out.shader = unlit ? MaterialShader::Unlit : kv.GetInt("$phong") ? MaterialShader::Phong : MaterialShader::Diffuse;

    // [r g b] in 0..1 or {r g b} in 0..255, like Source's material vector parser.
    const auto parseVector = [&](const char *key, QVector4D &value) {
        const auto text = kv.GetString(key);
        if (text.empty())
            return;
        const bool bytes = text.front() == '{';
        if (text.size() < 2 || (text.front() != '[' && !bytes) || text.back() != (bytes ? '}' : ']'))
            Fail(vmt, QStringLiteral("expected [r g b] or {r g b} for %1").arg(QString::fromLatin1(key)));
        std::istringstream input(std::string(text.substr(1, text.size() - 2)));
        float x, y, z;
        if (!(input >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            Fail(vmt, QStringLiteral("invalid %1").arg(QString::fromLatin1(key)));
        const float scale = bytes ? 1.f / 255.f : 1.f;
        value = {x * scale, y * scale, z * scale, value.w()};
    };

    // Source binds its white texture when $basetexture is absent (SFM HLMV renders such meshes white).
    const auto uploadOrDefault = [&](Texture &texture, const char *key, QColor fallback, bool srgb) {
        auto name = QString::fromUtf8(kv.GetString(key));
        if (name.isEmpty())
        {
            QImage image(1, 1, QImage::Format_RGBA8888);
            image.fill(fallback);
            texture.setFormat(srgb ? Texture::SRGB8_Alpha8 : Texture::RGBA8_UNorm);
            texture.setData(image, Texture::DontGenerateMipMaps);
            texture.setMinMagFilters(Texture::Nearest, Texture::Nearest);
            return QStringLiteral("(white)");
        }
        if (!name.endsWith(QStringLiteral(".vtf"), Qt::CaseInsensitive))
            name += QStringLiteral(".vtf");
        const auto path = FindMaterialAsset(gameDirs, name);
        if (path.isEmpty())
            Fail(vmt, QStringLiteral("could not find %1").arg(name));
        UploadVtf(texture, path, srgb);
        return path;
    };
    const auto vtf = uploadOrDefault(out.base, "$basetexture", Qt::white, true);

    // Pixel shader c1 (SetModulationPixelShaderDynamicState_LinearColorSpace): GammaToLinear($color * $color2), $alpha.
    QVector4D color{1, 1, 1, 1}, color2{1, 1, 1, 1};
    parseVector("$color", color);
    parseVector("$color2", color2);
    const auto linear = [](float gamma) {
        gamma = std::clamp(gamma, 0.f, 1.f);
        return gamma <= 0.04045f ? gamma / 12.92f : std::pow((gamma + 0.055f) / 1.055f, 2.4f);
    };
    const auto tint = color * color2;
    out.modulation = {linear(tint.x()), linear(tint.y()), linear(tint.z()), std::clamp(kv.GetFloat("$alpha", 1), 0.f, 1.f)};

    const bool translucent = kv.GetInt("$translucent") != 0, additive = kv.GetInt("$additive") != 0;
    out.blend = additive ? (translucent ? MaterialBlend::TranslucentAdditive : MaterialBlend::Additive)
        : translucent ? MaterialBlend::Translucent
        : kv.GetInt("$alphatest") ? MaterialBlend::AlphaTest : MaterialBlend::Opaque;
    if (const float reference = kv.GetFloat("$alphatestreference"); reference > 0 && reference <= 1)
        out.alphaTestReference = reference;

    ComboFeatures features;
    features.unlit = unlit;
    features.phong = out.shader == MaterialShader::Phong;
    features.halfLambert = kv.GetInt("$halflambert") != 0;
    // SFM phong draw (0x100380F0): bPhongHalfLambert is true unless $phongdisablehalflambert is set.
    features.phongHalfLambert = kv.GetInt("$phongdisablehalflambert") == 0;
    features.pixelFog = kv.GetInt("$vertexfog") == 0;
    out.combo = SelectCombo(features);

    if (out.shader == MaterialShader::Phong)
    {
        out.exponent = kv.GetFloat("$phongexponent");
        out.fresnelBoost.setW(kv.GetFloat("$phongboost", 1));
        if (!std::isfinite(out.exponent) || out.exponent < 0 || !std::isfinite(out.fresnelBoost.w()))
            Fail(vmt, QStringLiteral("invalid Phong exponent or boost"));
        parseVector("$phongfresnelranges", out.fresnelBoost);
        parseVector("$phongtint", out.tint);
        out.alphaMask = kv.GetInt("$basemapalphaphongmask") != 0;
        out.luminanceMask = kv.GetInt("$basemapluminancephongmask") != 0;
        uploadOrDefault(out.normal, "$bumpmap", QColor(128, 128, 255), false);
        uploadOrDefault(out.exponentMap, "$phongexponenttexture", Qt::white, false);
    }

    // Report what the selected combo ignores, so missing features are visible instead of silently wrong.
    static const QStringList applied = {
        "$basetexture", "$color", "$color2", "$alpha", "$translucent", "$additive", "$alphatest",
        "$alphatestreference", "$halflambert", "$model", "$surfaceprop", "$nocull", "$phong", "$vertexfog",
        "$phongexponent", "$phongboost", "$phongfresnelranges", "$phongtint", "$basemapalphaphongmask",
        "$basemapluminancephongmask", "$bumpmap", "$phongexponenttexture", "$phongdisablehalflambert"};
    for (auto *key = kv.GetFirstValue(); key; key = key->GetNextValue())
    {
        const auto name = QString::fromUtf8(key->GetName()).toLower();
        const bool phongOnly = name == "$bumpmap" || name.startsWith("$phong") || name.startsWith("$basemap");
        if (name.startsWith('$') && (!applied.contains(name) || (phongOnly && out.shader != MaterialShader::Phong)))
            out.ignored.append(name);
    }
    return vtf;
}
