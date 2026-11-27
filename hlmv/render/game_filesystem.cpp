#include "game_filesystem.h"

#include <stdexcept>

#include <QFile>
#include <QFileInfo>

#include "filesystem.h"
#include "filesystem/stdfile.h"
#include "filesystem_init.h"
#include "interfaces/interfaces.h"

namespace
{
// `path` as given, or its case-insensitive match (FS_ResolvePathCaseInsensitive), as the SDK's CStdFile opens it.
QString ResolveCase(const QString &path)
{
    if (QFileInfo::exists(path))
        return path;
    std::string resolved;
    return FS_ResolvePathCaseInsensitive(path.toUtf8().constData(), resolved) ? QString::fromStdString(resolved) : path;
}
} // namespace

void ThrowAssetError(const QString &path, const QString &reason)
{
    throw std::runtime_error((path + QStringLiteral(": ") + reason).toStdString());
}

QByteArray ReadAssetFile(const QString &path, qint64 limit)
{
    QFile file(ResolveCase(path));
    if (!file.open(QIODevice::ReadOnly))
        ThrowAssetError(path, file.errorString());
    const auto size = file.size();
    if (size > limit)
        ThrowAssetError(path, QStringLiteral("file exceeds preview size limit"));
    auto bytes = file.read(size);
    if (bytes.size() != size)
        ThrowAssetError(path, QStringLiteral("incomplete read"));
    return bytes;
}

GameFileSystem GameFileSystem::ForModel(const QString &modelPath)
{
    QDir root = QFileInfo(modelPath).absoluteDir();
    while (root.dirName().compare(QStringLiteral("models"), Qt::CaseInsensitive) != 0)
        if (!root.cdUp())
            ThrowAssetError(modelPath, QStringLiteral("expected the model under a game directory's models/ folder"));
    root.cdUp();
    return GameFileSystem(root);
}

// Mounts the SDK filesystem's GAME paths like the engine: SFM runs -game usermod, whose gameinfo.txt mounts every mod,
// so its usermod/gameinfo.txt wins over the model's own mod. Remounts only when the mod changes.
// shortcut: a gameinfo.txt without FileSystem/SearchPaths ends the process (filesystem_init's Error); make its
// errors recoverable if broken mods turn up.
GameFileSystem::GameFileSystem(const QDir &modDir)
{
    static QString mounted;
    const auto mod = modDir.absolutePath();
    if (mod == mounted)
        return;
    QDir root = modDir;
    root.cdUp();
    const auto usermod = root.filePath(QStringLiteral("usermod"));
    const auto gameInfoDir = QFileInfo::exists(usermod + QStringLiteral("/gameinfo.txt")) ? usermod : mod;
    g_pFullFileSystem->RemoveAllSearchPaths();
    if (QFileInfo::exists(gameInfoDir + QStringLiteral("/gameinfo.txt")))
        FileSystem_LoadSearchPaths(g_pFullFileSystem, gameInfoDir.toStdString(), root.absolutePath().toStdString());
    // The model's own mod even when the gameinfo does not list it.
    g_pFullFileSystem->AddSearchPath(mod.toStdString(), IBaseFileSystem::PathID::Game, PATH_ADD_TO_TAIL);
    mounted = mod;
}

QStringList GameFileSystem::dirs() const
{
    const int length = g_pFullFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, nullptr, 0);
    std::string paths(length, '\0');
    g_pFullFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, paths.data(), length);
    return QString::fromStdString(paths).remove(QChar(0)).split(';', Qt::SkipEmptyParts);
}

QString GameFileSystem::Find(const QString &relative) const
{
    // The engine filesystem collapses "dir//../name" (MDL texture names may climb out of $cdmaterials).
    const auto clean = QDir::cleanPath(QString(relative).replace('\\', '/'));
    if (clean.isEmpty() || clean.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(clean))
        return {};
    char path[MAX_PATH];
    if (!g_pFullFileSystem->RelativePathToFullPath(clean.toUtf8().constData(), IBaseFileSystem::PathID::Game, path, sizeof(path)))
        return {};
    return ResolveCase(QString::fromUtf8(path));
}

QString GameFileSystem::FindMaterial(const QString &relative) const { return Find(QStringLiteral("materials/") + relative); }
