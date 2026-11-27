#include <QApplication>
#include <QDebug>
#include <QFileInfo>

#include "mdlviewer.h"

int main(int argc, char **argv)
{
    ConfigureSfmViewport();
    QApplication app(argc, argv);
    auto args = app.arguments();
    const bool check = args.removeOne(QStringLiteral("--check"));
    if ((args.size() != 2 && args.size() != 3) || (check && args.size() != 3))
    {
        qCritical("usage: hlmv <SFM shaders/fxc directory> [model.mdl] [--check]");
        return 2;
    }

    QString shaderPath = args[1];
#ifdef Q_OS_WIN
    // Legacy PowerShell argument passing can turn a trailing backslash into a literal quote.
    if (shaderPath.endsWith(u'"'))
        shaderPath.chop(1);
#endif
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
