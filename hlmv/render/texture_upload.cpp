#include "texture_upload.h"

#include <algorithm>
#include <bit>

#include <QImage>
#include <QOpenGLPixelTransferOptions>
#include <QtEndian>

#include "game_filesystem.h"

namespace
{
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
    default: ThrowAssetError(path, QStringLiteral("unsupported VTF image format %1").arg(id));
    }
}
} // namespace

int VtfFrameCount(const QByteArray &bytes)
{
    const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
    return bytes.size() < 64 || qFromLittleEndian<uint32_t>(data) != 0x00465456 ? 0 : qFromLittleEndian<uint16_t>(data + 24);
}

uint32_t UploadVtf(Texture &texture, const QString &path, const QByteArray &bytes, bool srgb, int frame)
{
    const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
    const auto u32 = [&](int offset) { return qFromLittleEndian<uint32_t>(data + offset); };
    const auto u16 = [&](int offset) { return qFromLittleEndian<uint16_t>(data + offset); };
    if (bytes.size() < 64 || u32(0) != 0x00465456 || u32(4) != 7 || u32(8) > 5)
        ThrowAssetError(path, QStringLiteral("expected a PC VTF 7.0–7.5 header"));
    const auto version = u32(8);
    const auto headerSize = u32(12);
    const int width = u16(16), height = u16(18), mipCount = data[56];
    const auto flags = u32(20);
    // TEXTUREFLAGS_ENVMAP: six faces, plus a seventh sphere map before VTF 7.5 unless startFrame is 0xffff (VTFLib's rule).
    const bool cube = flags & 0x4000;
    const int faces = !cube ? 1 : version < 5 && u16(26) != 0xffff ? 7 : 6;
    if (cube != (texture.target() == Texture::TargetCubeMap))
        ThrowAssetError(path, cube ? QStringLiteral("cube map where a 2D texture is expected") : QStringLiteral("2D texture where a cube map is expected"));
    // Volume textures are rejected; `frame` selects the animation frame (proxies and $frame choose it).
    const int frames = u16(24);
    if (headerSize < (version >= 3 ? 80u : version >= 2 ? 65u : 64u) || headerSize > bytes.size() ||
        !width || !height || !mipCount || mipCount > std::bit_width(unsigned(std::max(width, height))) ||
        !frames || (version >= 2 && u16(63) != 1))
        ThrowAssetError(path, QStringLiteral("invalid header or unsupported volume texture"));

    qsizetype offset = headerSize;
    if (version >= 3)
    {
        const auto count = u32(68);
        if (count > (headerSize - 80) / 8)
            ThrowAssetError(path, QStringLiteral("truncated VTF resource directory"));
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
        end += qsizetype(frames) * faces * format.size(std::max(1, width >> mip), std::max(1, height >> mip));
    if (offset < headerSize || end > bytes.size())
        ThrowAssetError(path, QStringLiteral("VTF image data outside file bounds"));
    if (frame < 0 || frame >= frames)
        ThrowAssetError(path, QStringLiteral("VTF frame %1 out of range").arg(frame));

    texture.setFormat(format.internal);
    texture.setSize(width, height);
    texture.setMipLevels(mipCount);
    texture.setAutoMipMapGenerationEnabled(false);
    texture.allocateStorage(format.source, Texture::UInt8);
    if (!texture.isStorageAllocated())
        ThrowAssetError(path, QStringLiteral("OpenGL texture allocation failed"));
    QOpenGLPixelTransferOptions unpack;
    unpack.setAlignment(1);
    // VTF order per mip, smallest first: frames, then faces in D3D order (+X -X +Y -Y +Z -Z), which GL shares.
    for (int mip = mipCount - 1; mip >= 0; --mip)
    {
        const auto length = format.size(std::max(1, width >> mip), std::max(1, height >> mip));
        offset += qsizetype(frame) * faces * length; // frames come first within a mip
        for (int face = 0; face < faces; ++face, offset += length)
        {
            if (face == 6)
                continue; // pre-7.5 sphere map
            const auto glFace = cube ? Texture::CubeMapFace(Texture::CubeMapPositiveX + face) : Texture::CubeMapPositiveX;
            if (format.compressed)
                texture.setCompressedData(mip, 0, glFace, int(length), data + offset);
            else
                texture.setData(mip, 0, glFace, format.source, Texture::UInt8, data + offset, &unpack);
        }
        offset += qsizetype(frames - 1 - frame) * faces * length; // skip the remaining frames
    }
    texture.setWrapMode(Texture::DirectionS, flags & 4 ? Texture::ClampToEdge : Texture::Repeat);
    texture.setWrapMode(Texture::DirectionT, flags & 8 ? Texture::ClampToEdge : Texture::Repeat);
    const bool nearest = flags & 1;
    const auto filter = nearest ? Texture::Nearest : Texture::Linear;
    texture.setMinMagFilters(!nearest && mipCount > 1 && !(flags & 0x100) ? Texture::LinearMipMapLinear : filter, filter);
    texture.setMipLevelRange(0, mipCount - 1);
    return flags;
}

void UploadSolidColor(Texture &texture, QColor color, bool srgb)
{
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(color);
    texture.setFormat(srgb ? Texture::SRGB8_Alpha8 : Texture::RGBA8_UNorm);
    texture.setData(image, Texture::DontGenerateMipMaps);
    texture.setMinMagFilters(Texture::Nearest, Texture::Nearest);
}
