#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>

#include "mdlviewer.h"

namespace
{
QString FindSfmShaders(const QString &folder)
{
    if (folder.isEmpty())
        return {};
    const QDir root(folder);
    for (const auto *relative : {"game/platform/shaders/fxc", "platform/shaders/fxc", "."})
    {
        const QDir shaders(root.filePath(QString::fromLatin1(relative)));
        if (QFileInfo(shaders.filePath(QStringLiteral("vertexlit_and_unlit_generic_vs30.vcs"))).isFile() &&
            QFileInfo(shaders.filePath(QStringLiteral("vertexlit_and_unlit_generic_ps30.vcs"))).isFile())
            return shaders.absolutePath();
    }
    return {};
}

QString ChooseSfmShaders()
{
    QSettings settings(QStringLiteral("studiomdl_v2"), QStringLiteral("hlmv"));
    const auto key = QStringLiteral("sfmFolder");
    auto folder = settings.value(key).toString();
    if (const auto shaders = FindSfmShaders(folder); !shaders.isEmpty())
        return shaders;

    while (true)
    {
        folder = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Select the Source Filmmaker folder"),
                                                   QDir(folder).exists() ? folder : QString());
        if (folder.isEmpty())
            return {};
        if (const auto shaders = FindSfmShaders(folder); !shaders.isEmpty())
        {
            settings.setValue(key, folder);
            return shaders;
        }
        QMessageBox::warning(nullptr, QStringLiteral("SFM shaders not found"),
                             QStringLiteral("Select the Source Filmmaker installation, its game folder, "
                                            "or its platform/shaders/fxc folder."));
    }
}
} // namespace

int main(int argc, char **argv)
{
    ConfigureSfmViewport();
    QApplication app(argc, argv);
    auto args = app.arguments();
    const bool check = args.removeOne(QStringLiteral("--check"));
    if (args.size() > 3 || (check && args.size() != 3))
    {
        qCritical("usage: hlmv [<SFM shaders/fxc directory> [model.mdl] [--check]]");
        return 2;
    }

    QString shaderPath;
    if (args.size() == 1)
    {
        shaderPath = ChooseSfmShaders();
        if (shaderPath.isEmpty())
            return 0;
    }
    else
    {
        shaderPath = args[1];
#ifdef Q_OS_WIN
        // Legacy PowerShell argument passing can turn a trailing backslash into a literal quote.
        if (shaderPath.endsWith(u'"'))
            shaderPath.chop(1);
#endif
    }
    const QFileInfo shaderDirectory(shaderPath);
    if (!shaderDirectory.isDir())
    {
        qCritical().noquote() << "Shader directory does not exist:" << shaderPath;
        return 2;
    }

    MDLViewer viewer(shaderDirectory.absoluteFilePath(), check);
    if (args.size() == 3 && !viewer.LoadModelFile(args[2]) && check)
        return 1;
    viewer.show();
    return app.exec();
}
