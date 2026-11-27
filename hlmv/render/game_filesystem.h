#pragma once

// Qt-side glue over the SDK filesystem (libs/filesystem, g_pFullFileSystem) mounted from gameinfo.txt by
// filesystem_init: SFM's usermod/gameinfo.txt when present (SFM runs -game usermod, which mounts every mod), else the
// model's own mod. Lookups and reads match case-insensitively, like the SDK's CStdFile.

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QStringList>

class GameFileSystem
{
public:
    // Mounts the mod owning `modelPath` (the directory above its models/ folder). Throws std::runtime_error when
    // the model is not under a models/ folder.
    static GameFileSystem ForModel(const QString &modelPath);
    explicit GameFileSystem(const QDir &modDir);

    QStringList dirs() const; // GAME search paths, in order
    // `relative` (e.g. "materials/x.vmt") through the GAME search paths; empty when absent.
    QString Find(const QString &relative) const;
    QString FindMaterial(const QString &relative) const; // Find("materials/" + relative)
};

[[noreturn]] void ThrowAssetError(const QString &path, const QString &reason);
// The whole file, matching `path` case-insensitively; throws std::runtime_error when unreadable or larger than
// `limit` bytes.
QByteArray ReadAssetFile(const QString &path, qint64 limit);
