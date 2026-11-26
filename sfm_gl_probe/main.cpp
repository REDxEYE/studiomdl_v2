#include "viewport.h"

#include <exception>

#include <QDebug>
#include <QGuiApplication>

int main(int argc, char **argv)
{
    ConfigureSfmViewport();
    QGuiApplication app(argc, argv);
    auto args = app.arguments();
    const bool check = args.removeOne(QStringLiteral("--check"));
    const bool checker = args.removeOne(QStringLiteral("--checker"));
    const auto count = args.size();
    if ((count != 3 && count != 4) || (checker && count != 4))
    {
        qCritical() << "usage: sfm_gl_probe <vertex GLSL> <fragment GLSL> [model.mdl [--checker]] [--check]";
        return 2;
    }

    const QString path = count == 4 ? args[3] : QString();
    try
    {
        auto window = CreateSfmViewport(args[1], args[2], path, path.isEmpty() ? MdlMesh{} : LoadMdlMesh(path), check, checker);
        window->show();
        return app.exec();
    }
    catch (const std::exception &error)
    {
        qCritical().noquote() << error.what();
        return 1;
    }
}
