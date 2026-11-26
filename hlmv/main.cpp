#include <QApplication>

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

    MDLViewer viewer(args[1], check);
    if (args.size() == 3 && !viewer.LoadModelFile(args[2]) && check)
        return 1;
    viewer.show();
    return app.exec();
}
