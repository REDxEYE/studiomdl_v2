#include "mx.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

#include <QMessageBox>
#include <QResizeEvent>

namespace
{
QString Format(const char *format, va_list args)
{
    char buffer[4096];
    std::vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
    return QString::fromUtf8(buffer);
}

// Runs a programmatic change without reporting it as a user action, like Win32 control messages.
template<class F>
void Quietly(bool &quiet, F &&change)
{
    const bool was = std::exchange(quiet, true);
    change();
    quiet = was;
}
} // namespace

#define MX_FORMAT_LABEL(target)            \
    va_list args;                          \
    va_start(args, format);                \
    target(Format(format, args));          \
    va_end(args)

mxWindow *mxWidget::getParent() const
{
    for (auto *parent = m_self->parentWidget(); parent; parent = parent->parentWidget())
        if (auto *window = dynamic_cast<mxWindow *>(parent))
            return window;
    return nullptr;
}

void mxWidget::setLabel(const char *format, ...)
{
    MX_FORMAT_LABEL(m_self->setWindowTitle);
}

const char *mxWidget::getLabel() const
{
    m_text = m_self->windowTitle().toUtf8();
    return m_text.constData();
}

void mxWidget::emitAction()
{
    if (m_quiet)
        return;
    mxEvent event;
    event.event = mxEvent::Action;
    event.widget = this;
    event.action = m_id;
    for (auto *window = getParent(); window; window = window->getParent())
        if (window->handleEvent(&event))
            return;
}

mxWindow::mxWindow(mxWindow *parent, int x, int y, int w, int h, const char *label, int)
    : QWidget(parent), mxWidget(this, 0)
{
    setGeometry(x, y, w, h);
    if (label)
        setWindowTitle(QString::fromUtf8(label));
}

void mxWindow::setLabel(const char *format, ...)
{
    MX_FORMAT_LABEL(setWindowTitle);
}

const char *mxWindow::getLabel() const
{
    return mxWidget::getLabel();
}

void mxWindow::resizeEvent(QResizeEvent *resize)
{
    QWidget::resizeEvent(resize);
    if (m_menuBar)
        m_menuBar->setGeometry(0, 0, width(), m_menuBar->getHeight());
    mxEvent event;
    event.event = mxEvent::Size;
    event.width = width();
    event.height = height();
    handleEvent(&event);
}

mxLabel::mxLabel(mxWindow *parent, int x, int y, int w, int h, const char *label)
    : QLabel(QString::fromUtf8(label ? label : ""), parent), mxWidget(this, 0)
{
    setGeometry(x, y, w, h);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
}

void mxLabel::setLabel(const char *format, ...)
{
    MX_FORMAT_LABEL(setText);
}

const char *mxLabel::getLabel() const
{
    m_text = text().toUtf8();
    return m_text.constData();
}

mxButton::mxButton(mxWindow *parent, int x, int y, int w, int h, const char *label, int id)
    : QPushButton(QString::fromUtf8(label ? label : ""), parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    connect(this, &QPushButton::clicked, this, [this] { emitAction(); });
}

void mxButton::setLabel(const char *format, ...)
{
    MX_FORMAT_LABEL(setText);
}

const char *mxButton::getLabel() const
{
    m_text = text().toUtf8();
    return m_text.constData();
}

mxCheckBox::mxCheckBox(mxWindow *parent, int x, int y, int w, int h, const char *label, int id)
    : QCheckBox(QString::fromUtf8(label ? label : ""), parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    connect(this, &QCheckBox::clicked, this, [this] { emitAction(); });
}

mxRadioButton::mxRadioButton(mxWindow *parent, int x, int y, int w, int h, const char *label, int id, bool)
    : QRadioButton(QString::fromUtf8(label ? label : ""), parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    connect(this, &QRadioButton::clicked, this, [this] { emitAction(); });
}

mxChoice::mxChoice(mxWindow *parent, int x, int y, int w, int h, int id)
    : QComboBox(parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    setMaxVisibleItems(30);
    connect(this, &QComboBox::activated, this, [this] { emitAction(); });
}

void mxChoice::add(const char *item)
{
    Quietly(m_quiet, [&] { addItem(QString::fromUtf8(item)); });
}

void mxChoice::select(int index)
{
    Quietly(m_quiet, [&] { setCurrentIndex(index); });
}

void mxChoice::removeAll()
{
    Quietly(m_quiet, [&] { clear(); });
}

const char *mxChoice::getItemText(int index) const
{
    m_text = itemText(index).toUtf8();
    return m_text.constData();
}

mxListBox::mxListBox(mxWindow *parent, int x, int y, int w, int h, int id, int style)
    : QListWidget(parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    if (style == MultiSelection)
        setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(this, &QListWidget::itemSelectionChanged, this, [this] { emitAction(); });
}

void mxListBox::add(const char *item)
{
    Quietly(m_quiet, [&] { addItem(QString::fromUtf8(item)); });
}

void mxListBox::select(int index)
{
    Quietly(m_quiet, [&] {
        if (index >= 0 && index < count())
            setCurrentRow(index);
    });
}

void mxListBox::deselect(int index)
{
    Quietly(m_quiet, [&] {
        if (auto *entry = item(index))
            entry->setSelected(false);
    });
}

void mxListBox::removeAll()
{
    Quietly(m_quiet, [&] { clear(); });
}

void mxListBox::setItemText(int index, const char *text)
{
    if (auto *entry = item(index))
        entry->setText(QString::fromUtf8(text));
}

void mxListBox::setItemData(int index, void *data)
{
    if (auto *entry = item(index))
        entry->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(data)));
}

bool mxListBox::isSelected(int index) const
{
    const auto *entry = item(index);
    return entry && entry->isSelected();
}

const char *mxListBox::getItemText(int index) const
{
    const auto *entry = item(index);
    m_text = entry ? entry->text().toUtf8() : QByteArray();
    return m_text.constData();
}

void *mxListBox::getItemData(int index) const
{
    const auto *entry = item(index);
    return entry ? reinterpret_cast<void *>(entry->data(Qt::UserRole).value<quintptr>()) : nullptr;
}

mxLineEdit::mxLineEdit(mxWindow *parent, int x, int y, int w, int h, const char *label, int id, int style)
    : QLineEdit(QString::fromUtf8(label ? label : ""), parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    setReadOnly(style == ReadOnly);
    if (style == Password)
        setEchoMode(QLineEdit::Password);
    connect(this, &QLineEdit::textEdited, this, [this] { emitAction(); });
}

void mxLineEdit::getText(char *buf, size_t size) const
{
    if (!size)
        return;
    const auto utf8 = text().toUtf8();
    const auto length = std::min(size - 1, size_t(utf8.size()));
    std::memcpy(buf, utf8.constData(), length);
    buf[length] = 0;
}

void mxLineEdit::setText(const char *text)
{
    Quietly(m_quiet, [&] { QLineEdit::setText(QString::fromUtf8(text ? text : "")); });
}

void mxLineEdit::setLabel(const char *format, ...)
{
    MX_FORMAT_LABEL(QLineEdit::setText);
}

const char *mxLineEdit::getLabel() const
{
    m_text = text().toUtf8();
    return m_text.constData();
}

mxSlider::mxSlider(mxWindow *parent, int x, int y, int w, int h, int id, int style)
    : QSlider(style == Vertical ? Qt::Vertical : Qt::Horizontal, parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    QSlider::setRange(0, m_ticks);
    connect(this, &QSlider::valueChanged, this, [this] { emitAction(); });
}

void mxSlider::setRange(float min, float max, int ticks)
{
    const float value = getValue();
    m_min = min;
    m_max = max;
    m_ticks = std::max(1, ticks);
    Quietly(m_quiet, [&] { QSlider::setRange(0, m_ticks); });
    setValue(value);
}

void mxSlider::setValue(float value)
{
    const float t = m_max == m_min ? 0.f : (value - m_min) / (m_max - m_min);
    Quietly(m_quiet, [&] { QSlider::setValue(int(std::lround(std::clamp(t, 0.f, 1.f) * m_ticks))); });
}

void mxSlider::setSteps(int line, int page)
{
    setSingleStep(line);
    setPageStep(page);
}

float mxSlider::getValue() const
{
    return m_min + (m_max - m_min) * float(value()) / float(m_ticks);
}

mxTab::mxTab(mxWindow *parent, int x, int y, int w, int h, int id)
    : QTabWidget(parent), mxWidget(this, id)
{
    setGeometry(x, y, w, h);
    connect(this, &QTabWidget::currentChanged, this, [this] { emitAction(); });
}

void mxTab::add(QWidget *page, const char *text)
{
    Quietly(m_quiet, [&] { addTab(page, QString::fromUtf8(text)); });
}

void mxTab::select(int index)
{
    Quietly(m_quiet, [&] { setCurrentIndex(index); });
}

void mxMenu::add(const char *item, int id)
{
    auto *action = addAction(QString::fromUtf8(item));
    m_items.insert(id, action);
    connect(action, &QAction::triggered, this, [this, action] {
        if (!m_owner)
            return;
        mxEvent event;
        event.event = mxEvent::Action;
        event.action = m_items.key(action);
        m_owner->handleEvent(&event);
    });
}

void mxMenu::addMenu(const char *item, mxMenu *menu)
{
    menu->setTitle(QString::fromUtf8(item));
    menu->setParent(this, menu->windowFlags());
    menu->setOwner(m_owner);
    m_submenus.append(menu);
    QMenu::addMenu(menu);
}

QAction *mxMenu::find(int id) const
{
    if (auto *action = m_items.value(id))
        return action;
    for (const auto *menu : m_submenus)
        if (auto *action = menu->find(id))
            return action;
    return nullptr;
}

void mxMenu::setOwner(mxWindow *owner)
{
    m_owner = owner;
    for (auto *menu : m_submenus)
        menu->setOwner(owner);
}

void mxMenu::setEnabled(int id, bool b)
{
    if (auto *action = find(id))
        action->setEnabled(b);
}

void mxMenu::setChecked(int id, bool b)
{
    if (auto *action = find(id))
    {
        action->setCheckable(true);
        action->setChecked(b);
    }
}

bool mxMenu::isEnabled(int id) const
{
    const auto *action = find(id);
    return action && action->isEnabled();
}

bool mxMenu::isChecked(int id) const
{
    const auto *action = find(id);
    return action && action->isChecked();
}

mxMenuBar::mxMenuBar(mxWindow *parent) : QMenuBar(parent)
{
    parent->setMenuBar(this);
    setGeometry(0, 0, parent->width(), getHeight());
}

void mxMenuBar::addMenu(const char *item, mxMenu *menu)
{
    menu->setTitle(QString::fromUtf8(item));
    menu->setParent(this, menu->windowFlags());
    menu->setOwner(dynamic_cast<mxWindow *>(parentWidget()));
    m_menus.append(menu);
    QMenuBar::addMenu(menu);
}

QAction *mxMenuBar::find(int id) const
{
    for (const auto *menu : m_menus)
        if (auto *action = menu->find(id))
            return action;
    return nullptr;
}

void mxMenuBar::setEnabled(int id, bool b)
{
    if (auto *action = find(id))
        action->setEnabled(b);
}

void mxMenuBar::setChecked(int id, bool b)
{
    if (auto *action = find(id))
    {
        action->setCheckable(true);
        action->setChecked(b);
    }
}

void mxMenuBar::modify(int id, int newId, const char *newItem)
{
    const auto rename = [&](auto &self, mxMenu *menu) -> bool {
        if (auto *action = menu->m_items.take(id))
        {
            action->setText(QString::fromUtf8(newItem));
            action->setEnabled(true);
            menu->m_items.insert(newId, action);
            return true;
        }
        return std::ranges::any_of(menu->m_submenus, [&](mxMenu *submenu) { return self(self, submenu); });
    };
    std::ranges::any_of(m_menus, [&](mxMenu *menu) { return rename(rename, menu); });
}

bool mxMenuBar::isEnabled(int id) const
{
    const auto *action = find(id);
    return action && action->isEnabled();
}

bool mxMenuBar::isChecked(int id) const
{
    const auto *action = find(id);
    return action && action->isChecked();
}

void mxToolTip::add(QWidget *widget, const char *text)
{
    widget->setToolTip(QString::fromUtf8(text));
}

int mxMessageBox(mxWindow *parent, const char *msg, const char *title, int style)
{
    const auto text = QString::fromUtf8(msg), caption = QString::fromUtf8(title);
    if (style & (MX_MBYESNO | MX_MBYESNOCANCEL))
    {
        auto buttons = QMessageBox::Yes | QMessageBox::No;
        if (style & MX_MBYESNOCANCEL)
            buttons |= QMessageBox::Cancel;
        const auto answer = QMessageBox::question(parent, caption, text, buttons);
        return answer == QMessageBox::Yes ? 0 : answer == QMessageBox::No ? 1 : 2;
    }
    if (style & MX_MBERROR)
        QMessageBox::critical(parent, caption, text);
    else if (style & MX_MBWARNING)
        QMessageBox::warning(parent, caption, text);
    else
        QMessageBox::information(parent, caption, text);
    return 0;
}
