#pragma once

// Qt port of upstream/mdlviewer.h: main window with the original menus, accelerators, recent files
// and the viewport-over-control-panel layout.

#include <QColor>
#include <QElapsedTimer>
#include <QStringList>

#include "mx.h"
#include "viewport.h"

class ControlPanel;
class QOpenGLWindow;

class MDLViewer : public mxWindow
{
public:
    MDLViewer(QString shaderDirectory, bool check);
    ~MDLViewer() override;
    int handleEvent(mxEvent *event) override;
    bool LoadModelFile(const QString &path);
    bool hasModel() const { return m_viewport; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void Refresh();
    void initRecentFiles();
    void chooseColor(QColor &color);

    mxMenuBar *mb = nullptr;
    mxMenu *menuOptions = nullptr;
    mxMenu *menuView = nullptr;
    mxWindow *d_MatSysWindow = nullptr;
    ControlPanel *d_cpl = nullptr;
    QOpenGLWindow *m_viewport = nullptr;
    ViewportSettings m_settings;
    QColor m_groundColor{217, 217, 176}; // HLMV default gColor 0.85 0.85 0.69
    QString m_shaderDirectory;
    QString m_modelFile;
    QStringList m_recentFiles;
    bool m_check;
    bool m_secondaryLights = false;
    QElapsedTimer m_clockTime; // animation clock (upstream MatSysWindow idle dt)
};
