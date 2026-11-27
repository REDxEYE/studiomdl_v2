#pragma once

// OpenGL texture uploads shared by the material system and shader ports.

#include <QByteArray>
#include <QColor>
#include <QOpenGLTexture>
#include <QString>

// A PC VTF 7.0-7.5 file: 2D, or a cube map (TEXTUREFLAGS_ENVMAP) when `texture` targets cube maps. Returns the VTF
// flags. Throws std::runtime_error with the path for unsupported formats or a mismatched target.
uint32_t UploadVtf(QOpenGLTexture &texture, const QString &path, bool srgb);
// The same for the already-read file `bytes`, showing animation frame `frame` (0 .. VtfFrameCount-1).
uint32_t UploadVtf(QOpenGLTexture &texture, const QString &path, const QByteArray &bytes, bool srgb, int frame = 0);
// Animation frames in a VTF's header; 0 when it is not a VTF.
int VtfFrameCount(const QByteArray &bytes);
// ITexture::IsTranslucent: TEXTUREFLAGS_ONEBITALPHA or TEXTUREFLAGS_EIGHTBITALPHA.
constexpr bool VtfHasAlpha(uint32_t flags) { return flags & (0x1000 | 0x2000); }
// A 1x1 texture of `color`: Source's default and stand-in textures (white, black, flat normal, ...).
void UploadSolidColor(QOpenGLTexture &texture, QColor color, bool srgb = false);
