// Qt port of upstream/mdlviewer.cpp. Menu construction follows MDLViewer::MDLViewer line for line;
// handleEvent keeps the original cases, with features the port lacks left as no-ops.

#include "mdlviewer.h"

#include <exception>
#include <utility>

#include <QColorDialog>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QShortcut>
#include <QUrl>

#include "controlpanel.h"
#include "hlmv_ids.h"

namespace
{
constexpr const char *appTitle = "Half-Life Model Viewer";
constexpr int panelHeight = 240; // upstream: #define HEIGHT 240 (Win32)
constexpr int recentCount = 8;

// Hosts the OpenGL viewport container, like upstream's MatSysWindow child of the main window.
class MatSysWindow : public mxWindow
{
public:
    explicit MatSysWindow(mxWindow *parent) : mxWindow(parent, 0, 0, 100, 100, "", Normal)
    {
        setAutoFillBackground(true);
    }

    int handleEvent(mxEvent *event) override
    {
        if (event->event != mxEvent::Size)
            return 0;
        for (auto *child : findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            child->setGeometry(0, 0, event->width, event->height);
        return 1;
    }
};

struct Accel
{
    QKeySequence key;
    int command;
};

QSettings Settings()
{
    return QSettings(QStringLiteral("studiomdl_v2"), QStringLiteral("hlmv"));
}
} // namespace

MDLViewer::MDLViewer(QString shaderDirectory, bool check)
    : mxWindow(nullptr, 0, 0, 0, 0, appTitle, Normal), m_shaderDirectory(std::move(shaderDirectory)), m_check(check)
{
    // create menu stuff
    mb = new mxMenuBar(this);
    mxMenu *menuFile = new mxMenu();
    menuOptions = new mxMenu();
    menuView = new mxMenu();
    mxMenu *menuHelp = new mxMenu();

    mb->addMenu("File", menuFile);
    mb->addMenu("Options", menuOptions);
    mb->addMenu("View", menuView);
    mb->addMenu("Help", menuHelp);

    mxMenu *menuRecentModels = new mxMenu();
    for (int i = 0; i < recentCount; ++i)
        menuRecentModels->add("(empty)", IDC_FILE_RECENTMODELS1 + i);

    menuFile->add("Load Model...", IDC_FILE_LOADMODEL_STEAM);
    menuFile->add("Refresh (F5)", IDC_FILE_REFRESH);
    menuFile->addSeparator();
    menuFile->add("Recompile", IDC_FILE_RECOMPILE);
    menuFile->addSeparator();
    menuFile->add("Run .mvscript...", IDC_OPTIONS_RUNMVSCRIPT);
    menuFile->add("Save session as .mvscript...", IDC_OPTIONS_SAVEMVSCRIPT);
    menuFile->addSeparator();
    menuFile->add("Load Background Texture...", IDC_FILE_LOADBACKGROUNDTEX);
    menuFile->add("Load Ground Texture...", IDC_FILE_LOADGROUNDTEX);
    menuFile->addSeparator();
    menuFile->add("Unload Ground Texture", IDC_FILE_UNLOADGROUNDTEX);
    menuFile->addSeparator();
    menuFile->addMenu("Recent Models", menuRecentModels);
    menuFile->addSeparator();
    menuFile->add("Exit", IDC_FILE_EXIT);

    menuFile->setEnabled(IDC_FILE_LOADBACKGROUNDTEX, false);
    menuFile->setEnabled(IDC_FILE_LOADGROUNDTEX, false);
    menuFile->setEnabled(IDC_FILE_UNLOADGROUNDTEX, false);

    menuOptions->add("Background Color...", IDC_OPTIONS_COLORBACKGROUND);
    menuOptions->add("Ground Color...", IDC_OPTIONS_COLORGROUND);
    menuOptions->add("Light Color...", IDC_OPTIONS_COLORLIGHT);
    menuOptions->add("Ambient Color...", IDC_OPTIONS_COLORAMBIENT);
    menuOptions->add("Secondary Lights", IDC_OPTIONS_SECONDARYLIGHTS);
    menuOptions->addSeparator();
    menuOptions->add("Center View", IDC_OPTIONS_CENTERVIEW);
    menuOptions->add("Center Verts", IDC_OPTIONS_CENTERVERTS);
    menuOptions->add("Viewmodel Mode", IDC_OPTIONS_VIEWMODEL);
    menuOptions->addSeparator();
    menuOptions->add("Make Screenshot...", IDC_OPTIONS_MAKESCREENSHOT);

    menuView->add("File Associations...", IDC_VIEW_FILEASSOCIATIONS);
    menuView->setEnabled(IDC_VIEW_FILEASSOCIATIONS, false);
    menuView->addSeparator();
    menuView->add("Show Activities", IDC_VIEW_ACTIVITIES);
    menuView->add("Show hidden", IDC_VIEW_HIDDEN);
    menuView->add("Show sequence numbers", IDC_VIEW_SEQUENCE_INDICES);
    menuView->add("Sort sequences", IDC_VIEW_SORT_SEQUENCES);
    menuView->add("Show orbit circle", IDC_VIEW_ORBIT_CIRCLE);
    menuView->setChecked(IDC_VIEW_ORBIT_CIRCLE, false);
    menuView->add("Enable orbit yaw", IDC_VIEW_ORBIT_YAW);
    menuView->setChecked(IDC_VIEW_ORBIT_YAW, false);
    menuView->addSeparator();
    menuView->add("DotA View Mode", IDC_VIEW_DOTA);

    menuHelp->add("Goto Homepage...", IDC_HELP_GOTOHOMEPAGE);
    menuHelp->addSeparator();
    menuHelp->add("About...", IDC_HELP_ABOUT);

    for (int id : {IDC_VIEW_ACTIVITIES, IDC_VIEW_HIDDEN, IDC_VIEW_SEQUENCE_INDICES, IDC_VIEW_SORT_SEQUENCES,
                   IDC_VIEW_DOTA, IDC_OPTIONS_SECONDARYLIGHTS})
        mb->setChecked(id, false);

    d_MatSysWindow = new MatSysWindow(this);
    d_cpl = new ControlPanel(this, m_settings);

    auto settings = Settings();
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();
    m_settings.background = settings.value(QStringLiteral("bgColor"), QColor(64, 64, 64)).value<QColor>();
    m_groundColor = settings.value(QStringLiteral("gColor"), m_groundColor).value<QColor>();
    m_settings.light = settings.value(QStringLiteral("lColor"), m_settings.light).value<QColor>();
    m_settings.ambient = settings.value(QStringLiteral("aColor"), m_settings.ambient).value<QColor>();
    initRecentFiles();

    QPalette palette = d_MatSysWindow->palette();
    palette.setColor(QPalette::Window, m_settings.background);
    d_MatSysWindow->setPalette(palette);

    // upstream clamps the restored window to at least 640x700
    const QRect bounds = settings.value(QStringLiteral("bounds"), QRect(20, 20, 640, 700)).toRect();
    setGeometry(bounds.x(), std::max(0, bounds.y()), std::max(640, bounds.width()), std::max(700, bounds.height()));

    m_settings.reportError = [this](const QString &error) {
        qWarning().noquote() << error;
        if (m_check)
            QCoreApplication::exit(1);
    };

    // upstream accelTable; both letter cases map to the same Ctrl shortcut in Qt
    const Accel accelerators[] = {
        {QKeySequence(Qt::Key_F5), IDC_FILE_REFRESH},
        {QKeySequence(Qt::CTRL | Qt::Key_Up), IDC_ACCEL_TESSELLATION_INC},
        {QKeySequence(Qt::CTRL | Qt::Key_Down), IDC_ACCEL_TESSELLATION_DEC},
        {QKeySequence(Qt::CTRL | Qt::Key_W), IDC_ACCEL_WIREFRAME},
        {QKeySequence(Qt::CTRL | Qt::Key_A), IDC_ACCEL_ATTACHMENTS},
        {QKeySequence(Qt::CTRL | Qt::Key_G), IDC_ACCEL_GROUND},
        {QKeySequence(Qt::CTRL | Qt::Key_H), IDC_ACCEL_HITBOXES},
        {QKeySequence(Qt::CTRL | Qt::Key_O), IDC_ACCEL_BONES},
        {QKeySequence(Qt::CTRL | Qt::Key_B), IDC_ACCEL_BACKGROUND},
        {QKeySequence(Qt::CTRL | Qt::Key_M), IDC_ACCEL_MOVEMENT},
        {QKeySequence(Qt::CTRL | Qt::Key_N), IDC_ACCEL_NORMALS},
        {QKeySequence(Qt::CTRL | Qt::Key_D), IDC_ACCEL_DISPLACEMENT},
        {QKeySequence(Qt::CTRL | Qt::Key_T), IDC_ACCEL_TANGENTS},
        {QKeySequence(Qt::CTRL | Qt::Key_S), IDC_ACCEL_SHADOW},
    };
    for (const auto &accel : accelerators)
    {
        // Application-wide so shortcuts also fire while the embedded GL window has focus.
        auto *shortcut = new QShortcut(accel.key, this, nullptr, nullptr, Qt::ApplicationShortcut);
        QObject::connect(shortcut, &QShortcut::activated, this, [this, command = accel.command] {
            mxEvent event;
            event.event = mxEvent::Action;
            event.action = command;
            handleEvent(&event);
        });
    }
}

MDLViewer::~MDLViewer()
{
    auto settings = Settings();
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    settings.setValue(QStringLiteral("bounds"), geometry());
    settings.setValue(QStringLiteral("bgColor"), m_settings.background);
    settings.setValue(QStringLiteral("gColor"), m_groundColor);
    settings.setValue(QStringLiteral("lColor"), m_settings.light);
    settings.setValue(QStringLiteral("aColor"), m_settings.ambient);
}

void MDLViewer::initRecentFiles()
{
    for (int i = 0; i < recentCount; i++)
    {
        const bool used = i < m_recentFiles.size();
        mb->modify(IDC_FILE_RECENTMODELS1 + i, IDC_FILE_RECENTMODELS1 + i,
                   used ? m_recentFiles[i].toUtf8().constData() : "(empty)");
        mb->setEnabled(IDC_FILE_RECENTMODELS1 + i, used);
    }
}

bool MDLViewer::LoadModelFile(const QString &path)
{
    try
    {
        auto mesh = LoadMdlMesh(path);
        d_cpl->OnLoadModel(mesh);
        auto next = CreateSfmVcsViewport(m_shaderDirectory, path, std::move(mesh), m_check, m_settings);
        qDeleteAll(d_MatSysWindow->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
        m_viewport = next.get();
        auto *container = QWidget::createWindowContainer(next.release(), d_MatSysWindow);
        container->setFocusPolicy(Qt::StrongFocus);
        container->setGeometry(d_MatSysWindow->rect());
        container->show();
        d_cpl->setViewport(m_viewport);
    }
    catch (const std::exception &error)
    {
        qCritical("%s", error.what());
        if (m_check)
            QCoreApplication::exit(1);
        else
            mxMessageBox(this, ("Error loading model.\n" + std::string(error.what())).c_str(), appTitle, MX_MBERROR | MX_MBOK);
        return false;
    }

    // MRU: move an existing entry (case-insensitive, like mx_strcasecmp) or push a new one to the front
    m_modelFile = path;
    for (qsizetype i = 0; i < m_recentFiles.size(); ++i)
        if (m_recentFiles[i].compare(path, Qt::CaseInsensitive) == 0)
            m_recentFiles.removeAt(i--);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > recentCount)
        m_recentFiles.removeLast();
    initRecentFiles();
    setLabel("%s", path.toUtf8().constData());
    return true;
}

void MDLViewer::Refresh()
{
    if (!m_modelFile.isEmpty())
        LoadModelFile(m_modelFile);
}

void MDLViewer::chooseColor(QColor &color)
{
    const QColor chosen = QColorDialog::getColor(color, this);
    if (chosen.isValid())
        color = chosen;
}

int MDLViewer::handleEvent(mxEvent *event)
{
    if (event->event == mxEvent::Size)
    {
        const int y = mb->getHeight();
        const int w = event->width;
        const int h = event->height;
        d_MatSysWindow->setBounds(0, y, w, std::max(0, h - y - panelHeight));
        d_cpl->setBounds(0, h - panelHeight, w, panelHeight);
        return 1;
    }
    if (event->event != mxEvent::Action)
        return 0;

    const auto redraw = [this] {
        if (m_viewport)
            m_viewport->update();
    };

    switch (event->action)
    {
    case IDC_FILE_LOADMODEL:
    case IDC_FILE_LOADMODEL_STEAM:
    {
        const QString start = m_modelFile.isEmpty() ? QString() : QFileInfo(m_modelFile).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load Model"), start,
                                                          QStringLiteral("*.mdl"));
        if (!path.isEmpty())
            LoadModelFile(path);
        break;
    }

    case IDC_FILE_REFRESH:
        Refresh();
        break;

    case IDC_FILE_RECENTMODELS1:
    case IDC_FILE_RECENTMODELS2:
    case IDC_FILE_RECENTMODELS3:
    case IDC_FILE_RECENTMODELS4:
    case IDC_FILE_RECENTMODELS5:
    case IDC_FILE_RECENTMODELS6:
    case IDC_FILE_RECENTMODELS7:
    case IDC_FILE_RECENTMODELS8:
    {
        const int i = event->action - IDC_FILE_RECENTMODELS1;
        if (i < m_recentFiles.size())
            LoadModelFile(QString(m_recentFiles[i])); // copy: LoadModelFile reorders the list
        break;
    }

    case IDC_FILE_EXIT:
        close();
        break;

    case IDC_OPTIONS_COLORBACKGROUND:
    {
        chooseColor(m_settings.background);
        QPalette palette = d_MatSysWindow->palette();
        palette.setColor(QPalette::Window, m_settings.background);
        d_MatSysWindow->setPalette(palette);
        redraw();
        break;
    }

    case IDC_OPTIONS_COLORGROUND:
        chooseColor(m_groundColor); // no ground plane yet; kept for the session and settings
        break;

    case IDC_OPTIONS_COLORLIGHT:
        chooseColor(m_settings.light);
        redraw();
        break;

    case IDC_OPTIONS_COLORAMBIENT:
        chooseColor(m_settings.ambient);
        redraw();
        break;

    case IDC_OPTIONS_SECONDARYLIGHTS:
        m_secondaryLights = !m_secondaryLights;
        menuOptions->setChecked(IDC_OPTIONS_SECONDARYLIGHTS, m_secondaryLights);
        break;

    case IDC_OPTIONS_CENTERVIEW:
        if (menuView->isChecked(IDC_VIEW_DOTA))
            break;
        m_settings.centerView = true;
        redraw();
        break;

    case IDC_OPTIONS_CENTERVERTS:
        if (m_viewport && !menuView->isChecked(IDC_VIEW_DOTA))
        {
            m_settings.centerVerts = true;
            redraw();
        }
        break;

    case IDC_OPTIONS_MAKESCREENSHOT:
    {
        if (!m_viewport)
            break;
        QString path = QFileDialog::getSaveFileName(this, QString(), QString(), QStringLiteral("*.bmp"));
        if (path.isEmpty())
            break;
        if (!path.contains(QStringLiteral(".bmp")))
            path += QStringLiteral(".bmp");
        if (!m_viewport->grabFramebuffer().save(path))
            mxMessageBox(this, "Error saving screenshot.", appTitle, MX_MBERROR | MX_MBOK);
        break;
    }

    case IDC_VIEW_ACTIVITIES:
    case IDC_VIEW_HIDDEN:
    case IDC_VIEW_SEQUENCE_INDICES:
    case IDC_VIEW_SORT_SEQUENCES:
    case IDC_VIEW_ORBIT_CIRCLE:
    case IDC_VIEW_ORBIT_YAW:
    case IDC_VIEW_DOTA:
        // Qt already flipped the checkable item; the option itself is not implemented yet.
        break;

    case IDC_HELP_GOTOHOMEPAGE:
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://www.swissquake.ch/chumbalum-soft/index.html")));
        break;

    case IDC_HELP_ABOUT:
        mxMessageBox(this,
                     "Half-Life Model Viewer v2.0 (c) 2004 Valve Corp.\n"
                     "Portions (c) 1999 by Mete Ciragan\n\n"
                     "Native Qt port controls:\n"
                     "Left-drag to rotate.\n"
                     "Middle- or right-drag to pan.\n"
                     "Wheel to zoom.\n"
                     "Ctrl-left-drag to move light.",
                     "About Half-Life Model Viewer", MX_MBOK);
        break;

    case IDC_ACCEL_WIREFRAME:
        d_cpl->toggle(IDC_OVERLAY_WIREFRAME);
        break;
    case IDC_ACCEL_ATTACHMENTS:
        d_cpl->toggle(IDC_ATTACHMENTS);
        break;
    case IDC_ACCEL_GROUND:
        d_cpl->toggle(IDC_GROUND);
        break;
    case IDC_ACCEL_HITBOXES:
        d_cpl->toggle(IDC_HITBOXES);
        break;
    case IDC_ACCEL_BONES:
        d_cpl->toggle(IDC_BONES);
        break;
    case IDC_ACCEL_BACKGROUND:
        d_cpl->toggle(IDC_BACKGROUND);
        break;
    case IDC_ACCEL_MOVEMENT:
        d_cpl->toggle(IDC_MOVEMENT);
        break;
    case IDC_ACCEL_NORMALS:
        d_cpl->toggle(IDC_NORMALS);
        break;
    case IDC_ACCEL_DISPLACEMENT:
        d_cpl->toggle(IDC_DISPLACEMENTMAP);
        break;
    case IDC_ACCEL_TANGENTS:
        d_cpl->toggle(IDC_TANGENTFRAME);
        break;
    case IDC_ACCEL_SHADOW:
        d_cpl->toggle(IDC_SHADOW);
        break;

    default:
        // Recompile, .mvscript, textures, Viewmodel Mode, tessellation: not ported yet.
        break;
    }
    return 1;
}
