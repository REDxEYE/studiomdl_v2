#include "game_filesystem.h"

#include <algorithm>
#include <stdexcept>

#include <QFile>
#include <QFileInfo>

#include "tier1/keyvalues.h"

namespace
{
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
} // namespace

void ThrowAssetError(const QString &path, const QString &reason)
{
    throw std::runtime_error((path + QStringLiteral(": ") + reason).toStdString());
}

QByteArray ReadAssetFile(const QString &path, qint64 limit)
{
    QFile file(path);
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

// Game directories in Source search order. Loose files only: VPKs are skipped.
GameFileSystem::GameFileSystem(const QDir &modDir)
{
    QDir root = modDir;
    root.cdUp();
    const auto usermod = root.filePath(QStringLiteral("usermod/gameinfo.txt"));
    const auto info = QFileInfo::exists(usermod) ? usermod : modDir.filePath(QStringLiteral("gameinfo.txt"));
    const auto add = [&](const QString &path) {
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty() && QFileInfo(canonical).isDir() && !m_dirs.contains(canonical))
            m_dirs.append(canonical);
    };
    if (QFileInfo::exists(info))
    {
        KeyValues kv("GameInfo");
        const auto text = ReadAssetFile(info, 1024 * 1024);
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
}

QString GameFileSystem::FindMaterial(const QString &relative) const
{
    for (const auto &dir : m_dirs)
        if (auto path = FindAsset(QDir(dir).filePath(QStringLiteral("materials")), relative); !path.isEmpty())
            return path;
    return {};
}
