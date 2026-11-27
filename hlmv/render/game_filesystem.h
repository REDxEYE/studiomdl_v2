#pragma once

// Loose-file view of a Source game's search paths, resolved like the engine's filesystem: SFM's
// usermod/gameinfo.txt when present (SFM runs -game usermod, which mounts every mod), else the model's own mod.

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QStringList>

class GameFileSystem
{
public:
    // The mod owning `modelPath` (the directory above its models/ folder). Throws std::runtime_error when the
    // model is not under a models/ folder.
    static GameFileSystem ForModel(const QString &modelPath);
    explicit GameFileSystem(const QDir &modDir);

    const QStringList &dirs() const { return m_dirs; }
    // materials/<relative> in search order, matched case-insensitively; empty when absent.
    QString FindMaterial(const QString &relative) const;

private:
    QStringList m_dirs;
};

[[noreturn]] void ThrowAssetError(const QString &path, const QString &reason);
// The whole file; throws std::runtime_error when unreadable or larger than `limit` bytes.
QByteArray ReadAssetFile(const QString &path, qint64 limit);
