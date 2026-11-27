#pragma once

// OpenGL texture uploads shared by the material system and shader ports.

#include <QColor>
#include <QOpenGLTexture>
#include <QString>

// A PC VTF 7.0-7.5 file: 2D, or a cube map (TEXTUREFLAGS_ENVMAP) when `texture` targets cube maps. Throws
// std::runtime_error with the path for unsupported formats or a mismatched target.
void UploadVtf(QOpenGLTexture &texture, const QString &path, bool srgb);
// A 1x1 texture of `color`: Source's default and stand-in textures (white, black, flat normal, ...).
void UploadSolidColor(QOpenGLTexture &texture, QColor color, bool srgb = false);
