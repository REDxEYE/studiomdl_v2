#include "viewport.h"

#include <exception>
#include <utility>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

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

    ViewportSettings settings;
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Half-Life Model Viewer"));
    window.resize(820, 720);
    auto *central = new QWidget(&window);
    window.setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // HLMV's Size handler places the viewport above a 240 px control panel.
    QWidget *container = new QLabel(QStringLiteral("Load a model from File → Load Model…"), central);
    static_cast<QLabel *>(container)->setAlignment(Qt::AlignCenter);
    layout->addWidget(container, 1);
    QOpenGLWindow *viewport = nullptr;
    auto *tabs = new QTabWidget(central);
    tabs->setFixedHeight(240);
    layout->addWidget(tabs);

    auto *render = new QWidget(tabs);
    tabs->addTab(render, QStringLiteral("Render"));
    auto *renderLayout = new QVBoxLayout(render);
    renderLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto *mode = new QComboBox(render);
    mode->addItems({QStringLiteral("Wireframe"), QStringLiteral("Textured")});
    mode->setCurrentIndex(1);
    mode->setAccessibleName(QStringLiteral("Render mode"));
    renderLayout->addWidget(mode);
    auto *specular = new QCheckBox(QStringLiteral("Specular"), render);
    specular->setChecked(true);
    renderLayout->addStretch();
    renderLayout->addWidget(specular);

    auto *model = new QWidget(tabs);
    tabs->addTab(model, QStringLiteral("Model"));
    auto *modelLayout = new QVBoxLayout(model);
    auto *summary = new QLabel(model);
    modelLayout->addWidget(summary);
    auto *materials = new QListWidget(model);
    materials->setAccessibleName(QStringLiteral("Model materials"));
    modelLayout->addWidget(materials);

    const auto redraw = [&] { if (viewport) viewport->update(); };
    QObject::connect(mode, &QComboBox::currentIndexChanged, &window, [&](int index) {
        settings.wireframe = index == 0;
        redraw();
    });
    QObject::connect(specular, &QCheckBox::toggled, &window, [&](bool enabled) {
        settings.specular = enabled;
        redraw();
    });
    settings.reportError = [&](const QString &error) {
        window.statusBar()->showMessage(error);
        if (check)
            QCoreApplication::exit(1);
    };

    QString currentPath;
    auto *fileMenu = window.menuBar()->addMenu(QStringLiteral("&File"));
    auto *open = fileMenu->addAction(QStringLiteral("&Load Model…"), QKeySequence::Open);
    auto *refresh = fileMenu->addAction(QStringLiteral("&Refresh"), QKeySequence(Qt::Key_F5));
    refresh->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), QKeySequence::Quit, &window, &QWidget::close);
    const auto load = [&](const QString &path) {
        if (path.isEmpty())
            return;
        try
        {
            auto mesh = LoadMdlMesh(path);
            const auto stats = QStringLiteral("LOD 0 — %1 vertices, %2 triangles").arg(mesh.vertices.size()).arg(mesh.indices.size() / 3);
            auto materialNames = mesh.textures;
            auto next = CreateSfmVcsViewport(args[1], path, std::move(mesh), check, settings);
            layout->removeWidget(container);
            delete container;
            viewport = next.get();
            container = QWidget::createWindowContainer(next.release(), central);
            container->setFocusPolicy(Qt::StrongFocus);
            layout->insertWidget(0, container, 1);
            currentPath = path;
            summary->setText(stats);
            materials->clear();
            materials->addItems(materialNames);
            refresh->setEnabled(true);
            window.setWindowTitle(QStringLiteral("Half-Life Model Viewer — ") + QFileInfo(path).fileName());
            window.statusBar()->showMessage(QStringLiteral("Left drag: orbit  |  Ctrl+left drag: light  |  Middle/right drag: pan  |  Wheel: zoom"));
        }
        catch (const std::exception &error)
        {
            if (check)
            {
                qCritical("%s", error.what());
                QCoreApplication::exit(1);
                return;
            }
            QMessageBox::warning(&window, QStringLiteral("Load Model"), QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(open, &QAction::triggered, &window, [&] {
        load(QFileDialog::getOpenFileName(&window, QStringLiteral("Load Model"), QFileInfo(currentPath).absolutePath(),
                                         QStringLiteral("Source models (*.mdl)")));
    });
    QObject::connect(refresh, &QAction::triggered, &window, [&] { load(currentPath); });

    auto *options = window.menuBar()->addMenu(QStringLiteral("&Options"));
    options->addAction(QStringLiteral("&Background Color…"), &window, [&] {
        const auto color = QColorDialog::getColor(settings.background, &window);
        if (color.isValid())
        {
            settings.background = color;
            redraw();
        }
    });
    options->addAction(QStringLiteral("&Center View"), &window, [&] {
        settings.centerView = true;
        redraw();
    });
    auto *view = window.menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(QStringLiteral("&Render"), tabs, [=] { tabs->setCurrentWidget(render); });
    view->addAction(QStringLiteral("&Model"), tabs, [=] { tabs->setCurrentWidget(model); });
    window.menuBar()->addMenu(QStringLiteral("&Help"))->addAction(QStringLiteral("&About…"), &window, [&] {
        QMessageBox::about(&window, QStringLiteral("Half-Life Model Viewer"),
                           QStringLiteral("Native Qt port with the SFM OpenGL model renderer."));
    });

    if (args.size() == 3)
        load(args[2]);
    if (check && !viewport)
        return 1;
    window.show();
    return app.exec();
}
