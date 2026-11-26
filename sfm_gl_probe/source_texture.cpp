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
} // namespace

QString LoadSourceMaterial(SourceMaterial &out, const QString &modelPath, const QString &material,
                           const QStringList &searchPaths, bool phong)
{
    QDir root = QFileInfo(modelPath).absoluteDir();
    while (root.dirName().compare(QStringLiteral("models"), Qt::CaseInsensitive) != 0)
        if (!root.cdUp())
            Fail(modelPath, QStringLiteral("expected the model under a game directory's models/ folder"));
    root.cdUp();
    const auto materials = root.filePath(QStringLiteral("materials"));
    QString vmt;
    for (const auto &path : searchPaths)
    {
        const auto name = path.isEmpty() ? material : path + '/' + material;
        vmt = FindAsset(materials, name + QStringLiteral(".vmt"));
        if (!vmt.isEmpty())
            break;
    }
    if (vmt.isEmpty())
        Fail(modelPath, QStringLiteral("could not find loose VMT for %1").arg(material));

    const auto text = ReadFile(vmt, 1024 * 1024);
    KeyValues kv("material");
    if (!kv.LoadFromBuffer(vmt.toUtf8().constData(), text.constData()))
        Fail(vmt, QStringLiteral("invalid VMT"));
    const auto shader = QString::fromUtf8(kv.GetName());
    if (shader.compare(QStringLiteral("VertexLitGeneric"), Qt::CaseInsensitive) != 0 &&
        shader.compare(QStringLiteral("UnlitGeneric"), Qt::CaseInsensitive) != 0)
        Fail(vmt, QStringLiteral("unsupported material shader %1").arg(shader));
    if ((kv.GetInt("$phong") != 0) != phong)
        Fail(vmt, phong ? QStringLiteral("material does not enable $phong") :
                         QStringLiteral("$phong requires the Phong shader pair"));
    const auto upload = [&](Texture &texture, QString name, bool srgb) {
        if (!name.endsWith(QStringLiteral(".vtf"), Qt::CaseInsensitive))
            name += QStringLiteral(".vtf");
        const auto path = FindAsset(materials, name);
        if (path.isEmpty())
            Fail(vmt, QStringLiteral("could not find %1").arg(name));
        UploadVtf(texture, path, srgb);
        return path;
    };
    const auto base = QString::fromUtf8(kv.GetString("$basetexture"));
    if (base.isEmpty())
        Fail(vmt, QStringLiteral("no $basetexture"));
    const auto vtf = upload(out.base, base, true);
    if (phong)
    {
        // shortcut: the original no-warp/no-rim/no-cubemap combo; add shader selection before supporting those features.
        const auto parseVector = [&](const char *key, QVector4D &value) {
            const auto text = kv.GetString(key);
            if (text.empty())
                return;
            if (text.size() < 2 || text.front() != '[' || text.back() != ']')
                Fail(vmt, QStringLiteral("expected [r g b] for %1").arg(QString::fromLatin1(key)));
            std::istringstream input(std::string(text.substr(1, text.size() - 2)));
            float x, y, z;
            if (!(input >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                Fail(vmt, QStringLiteral("invalid %1").arg(QString::fromLatin1(key)));
            value = {x, y, z, value.w()};
        };
        out.exponent = kv.GetFloat("$phongexponent");
        out.fresnelBoost.setW(kv.GetFloat("$phongboost", 1));
        if (!std::isfinite(out.exponent) || out.exponent < 0 || !std::isfinite(out.fresnelBoost.w()))
            Fail(vmt, QStringLiteral("invalid Phong exponent or boost"));
        parseVector("$phongfresnelranges", out.fresnelBoost);
        parseVector("$phongtint", out.tint);
        out.alphaMask = kv.GetInt("$basemapalphaphongmask") != 0;
        out.luminanceMask = kv.GetInt("$basemapluminancephongmask") != 0;
        const auto uploadOrDefault = [&](Texture &texture, const char *key, QColor fallback) {
            const auto name = QString::fromUtf8(kv.GetString(key));
            if (!name.isEmpty())
                upload(texture, name, false);
            else
            {
                QImage image(1, 1, QImage::Format_RGBA8888);
                image.fill(fallback);
                texture.setData(image, Texture::DontGenerateMipMaps);
                texture.setMinMagFilters(Texture::Nearest, Texture::Nearest);
            }
        };
        uploadOrDefault(out.normal, "$bumpmap", QColor(128, 128, 255));
        uploadOrDefault(out.exponentMap, "$phongexponenttexture", Qt::white);
    }
    return vtf;
}
