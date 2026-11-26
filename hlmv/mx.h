#pragma once

// Qt implementation of the subset of mxtk (public/mxtk) that HLMV uses, so the original control
// construction code ports nearly verbatim. Widgets are owned by their Qt parent, like mxtk's HWNDs.
// Actions go to the nearest mxWindow ancestor's handleEvent(); a 0 return passes them further up.

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QTabWidget>

class mxWindow;
class mxWidget;

class mxEvent
{
public:
    enum { Action, Size, Timer, Idle, Show, Hide, MouseUp, MouseDown, MouseMove, MouseDrag, KeyUp, KeyDown,
           MouseWheeled, Focus, Activate, NCMouseUp, NCMouseDown, NCMouseMove, NCMouseDrag, Close, PosChanged,
           Char, ParentNotify, DropFile };
    int event = 0;
    mxWidget *widget = nullptr;
    int action = 0;
    int width = 0, height = 0;
};

// Mixin for mxtk's id/label/bounds API; the Qt base class supplies visibility, enabling and position.
class mxWidget
{
public:
    mxWidget(QWidget *self, int id) : m_self(self), m_id(id) {}
    virtual ~mxWidget() = default;
    void setBounds(int x, int y, int w, int h) { m_self->setGeometry(x, y, w, h); }
    int getId() const { return m_id; }
    void setId(int id) { m_id = id; }
    mxWindow *getParent() const;
    virtual void setLabel(const char *format, ...);
    virtual const char *getLabel() const;

protected:
    void emitAction(); // user changes only; programmatic ones are suppressed with m_quiet
    bool m_quiet = false;
    mutable QByteArray m_text;

private:
    QWidget *m_self;
    int m_id;
};

class mxMenuBar;

class mxWindow : public QWidget, public mxWidget
{
public:
    enum { Normal, Popup, Dialog, ModalDialog };
    mxWindow(mxWindow *parent, int x, int y, int w, int h, const char *label = nullptr, int style = Normal);
    virtual int handleEvent(mxEvent *) { return 0; }
    virtual void redraw() { update(); }
    void setMenuBar(mxMenuBar *menuBar) { m_menuBar = menuBar; }
    void setLabel(const char *format, ...) override;
    const char *getLabel() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    mxMenuBar *m_menuBar = nullptr;
};

class mxLabel : public QLabel, public mxWidget
{
public:
    mxLabel(mxWindow *parent, int x, int y, int w, int h, const char *label = nullptr);
    void setLabel(const char *format, ...) override;
    const char *getLabel() const override;
};

class mxButton : public QPushButton, public mxWidget
{
public:
    mxButton(mxWindow *parent, int x, int y, int w, int h, const char *label = nullptr, int id = 0);
    void setLabel(const char *format, ...) override;
    const char *getLabel() const override;
};

class mxCheckBox : public QCheckBox, public mxWidget
{
public:
    mxCheckBox(mxWindow *parent, int x, int y, int w, int h, const char *label = nullptr, int id = 0);
};

class mxRadioButton : public QRadioButton, public mxWidget
{
public:
    // Qt keeps radio buttons exclusive per parent, which matches every HLMV group.
    mxRadioButton(mxWindow *parent, int x, int y, int w, int h, const char *label = nullptr, int id = 0, bool newGroup = false);
};

class mxChoice : public QComboBox, public mxWidget
{
public:
    mxChoice(mxWindow *parent, int x, int y, int w, int h, int id = 0);
    void add(const char *item);
    void select(int index);
    void remove(int index) { removeItem(index); }
    void removeAll();
    int getItemCount() const { return count(); }
    int getSelectedIndex() const { return currentIndex(); }
    const char *getItemText(int index) const;
    const char *getLabel() const override { return getItemText(currentIndex()); }
};

class mxListBox : public QListWidget, public mxWidget
{
public:
    enum { Normal, MultiSelection };
    mxListBox(mxWindow *parent, int x, int y, int w, int h, int id = 0, int style = Normal);
    void add(const char *item);
    void select(int index);
    void deselect(int index);
    void remove(int index) { delete takeItem(index); }
    void removeAll();
    void setItemText(int index, const char *text);
    void setItemData(int index, void *data);
    int getItemCount() const { return count(); }
    int getSelectedIndex() const { return currentRow(); }
    bool isSelected(int index) const;
    const char *getItemText(int index) const;
    void *getItemData(int index) const;
};

class mxLineEdit : public QLineEdit, public mxWidget
{
public:
    enum { Normal, ReadOnly, Password };
    mxLineEdit(mxWindow *parent, int x, int y, int w, int h, const char *label = nullptr, int id = 0, int style = Normal);
    void getText(char *buf, size_t size) const;
    void setText(const char *text);
    void setLabel(const char *format, ...) override;
    const char *getLabel() const override;
};
using mxLineEdit2 = mxLineEdit; // upstream's mxLineEdit2 only adds Win32 key handling

class mxSlider : public QSlider, public mxWidget
{
public:
    enum { Horizontal, Vertical };
    mxSlider(mxWindow *parent, int x, int y, int w, int h, int id = 0, int style = Horizontal);
    // Hide QSlider's integer API: mxtk sliders map a float range onto `ticks` steps.
    void setRange(float min, float max, int ticks = 100);
    void setValue(float value);
    void setSteps(int line, int page);
    float getValue() const;
    float getMinValue() const { return m_min; }
    float getMaxValue() const { return m_max; }

private:
    float m_min = 0, m_max = 1;
    int m_ticks = 100;
};

class mxTab : public QTabWidget, public mxWidget
{
public:
    mxTab(mxWindow *parent, int x, int y, int w, int h, int id = 0);
    void add(QWidget *page, const char *text);
    void select(int index);
    int getSelectedIndex() const { return currentIndex(); }
};

class mxMenu : public QMenu
{
public:
    mxMenu() = default;
    void add(const char *item, int id);
    void addMenu(const char *item, mxMenu *menu);
    void addSeparator() { QMenu::addSeparator(); }
    void setEnabled(int id, bool b);
    void setChecked(int id, bool b);
    bool isEnabled(int id) const;
    bool isChecked(int id) const;

private:
    friend class mxMenuBar;
    QAction *find(int id) const;
    void setOwner(mxWindow *owner);
    mxWindow *m_owner = nullptr;
    QHash<int, QAction *> m_items;
    QList<mxMenu *> m_submenus;
};

class mxMenuBar : public QMenuBar
{
public:
    explicit mxMenuBar(mxWindow *parent);
    void addMenu(const char *item, mxMenu *menu);
    void setEnabled(int id, bool b);
    void setChecked(int id, bool b);
    void modify(int id, int newId, const char *newItem);
    bool isEnabled(int id) const;
    bool isChecked(int id) const;
    int getHeight() const { return sizeHint().height(); }

private:
    QAction *find(int id) const;
    QList<mxMenu *> m_menus;
};

namespace mxToolTip
{
void add(QWidget *widget, const char *text);
}

enum { MX_MBOK = 1, MX_MBYESNO = 4, MX_MBYESNOCANCEL = 8, MX_MBWARNING = 16, MX_MBERROR = 32, MX_MBQUESTION = 64 };
int mxMessageBox(mxWindow *parent, const char *msg, const char *title, int style = MX_MBOK);
