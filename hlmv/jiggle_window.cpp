// Jiggle Bones tab: lists the model's bones, edits their $jigglebone rules on the viewer's copy of the model (new rules
// on plain bones included) and saves them by rebuilding the .mdl (mdl_jiggle.h).

#include "controlpanel.h"

#include <numbers>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>

#include "game_filesystem.h"
#include "studio_anim.h"
#include "viewport.h"

namespace
{
constexpr double degrees = 180.0 / std::numbers::pi;

QString Error(const QString &text) { return QStringLiteral("<span style=\"color:#e05252\">%1</span>").arg(text.toHtmlEscaped()); }
}

void CJiggleWindow::Init()
{
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Filter bones"));
    m_filter->setClearButtonEnabled(true);
    m_list = new QListWidget(this);
    m_enabled = new QCheckBox(QStringLiteral("Jiggle bone"), this);
    m_enabled->setToolTip(QStringLiteral("Give the selected bone a jiggle rule, or remove its rule"));
    m_highlight = new QCheckBox(QStringLiteral("Highlight"), this);
    m_highlight->setToolTip(QStringLiteral("Draw the selected bone in the viewport"));
    m_revert = new QPushButton(QStringLiteral("Revert"), this);
    m_revert->setToolTip(QStringLiteral("Restore the selected bone's rule from the file"));
    m_save = new QPushButton(QStringLiteral("Save MDL"), this);
    m_save->setToolTip(QStringLiteral("Rebuild the .mdl with every edited rule and update the .vvd/.vtx/.phy checksums"));
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Every edit goes through the selected bone's current rule.
    const auto edit = [this](auto change) {
        const int bone = Selected();
        if (bone < 0 || !m_current[bone])
            return;
        auto rule = *m_current[bone];
        change(rule);
        Apply(bone, rule);
    };
    auto *tabs = new QTabWidget(this);
    m_editor = tabs;
    const auto checkGroup = [&](QGroupBox *box, uint32_t flag) {
        box->setCheckable(true);
        m_groups.emplace_back(box, flag);
        QObject::connect(box, &QGroupBox::toggled, this, [this, edit, flag](bool on) {
            edit([&](JiggleParams &rule) {
                constexpr uint32_t tip = JiggleParams::Flexible | JiggleParams::Rigid;
                if (flag & tip)
                {
                    rule.flags &= ~(flag | JiggleParams::LengthConstraint);
                    if (on)
                        rule.flags = (rule.flags & ~tip) | flag | JiggleParams::LengthConstraint;
                }
                else
                    rule.flags = on ? rule.flags | flag : rule.flags & ~flag;
            });
            ShowRule();
        });
    };
    const auto page = [&](const QString &title, const QString &label, uint32_t flag) {
        auto *box = new QGroupBox(label, tabs);
        checkGroup(box, flag);
        tabs->addTab(box, title);
        return new QGridLayout(box);
    };
    const auto group = [&](QGridLayout *grid, int row, int column, const QString &title, uint32_t flag = 0, int rows = 1) {
        auto *box = new QGroupBox(title, grid->parentWidget());
        if (flag)
            checkGroup(box, flag);
        grid->addWidget(box, row, column, rows, 1, Qt::AlignTop);
        return new QFormLayout(box);
    };
    enum Range { Positive, Signed, Angle };
    const auto field = [&](QFormLayout *form, const QString &label, float JiggleParams::*member, Range range, double step) {
        auto *spin = new QDoubleSpinBox(form->parentWidget());
        spin->setRange(range == Angle ? -360 : range == Signed ? -100000 : 0, range == Angle ? 360 : 100000);
        spin->setDecimals(3);
        spin->setSingleStep(step);
        form->addRow(label, spin);
        m_fields.push_back({spin, member, range == Angle});
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, this, [edit, member, range](double value) {
            edit([&](JiggleParams &rule) { rule.*member = float(range == Angle ? value / degrees : value); });
        });
    };

    const auto tipFields = [&](QGridLayout *grid) {
        auto *tip = group(grid, 0, 0, QStringLiteral("Tip"));
        field(tip, QStringLiteral("Length:"), &JiggleParams::length, Positive, 1);
        field(tip, QStringLiteral("Tip mass:"), &JiggleParams::tipMass, Positive, 1);

        auto *angle = group(grid, 1, 0, QStringLiteral("Angle constraint"), JiggleParams::AngleConstraint);
        field(angle, QStringLiteral("Limit:"), &JiggleParams::angleLimit, Angle, 1);
        auto *yaw = group(grid, 0, 1, QStringLiteral("Yaw constraint"), JiggleParams::YawConstraint);
        field(yaw, QStringLiteral("Min:"), &JiggleParams::minYaw, Angle, 1);
        field(yaw, QStringLiteral("Max:"), &JiggleParams::maxYaw, Angle, 1);
        field(yaw, QStringLiteral("Friction:"), &JiggleParams::yawFriction, Positive, 0.1);
        field(yaw, QStringLiteral("Bounce:"), &JiggleParams::yawBounce, Positive, 0.1);
        auto *pitch = group(grid, 0, 2, QStringLiteral("Pitch constraint"), JiggleParams::PitchConstraint);
        field(pitch, QStringLiteral("Min:"), &JiggleParams::minPitch, Angle, 1);
        field(pitch, QStringLiteral("Max:"), &JiggleParams::maxPitch, Angle, 1);
        field(pitch, QStringLiteral("Friction:"), &JiggleParams::pitchFriction, Positive, 0.1);
        field(pitch, QStringLiteral("Bounce:"), &JiggleParams::pitchBounce, Positive, 0.1);
        grid->setRowStretch(2, 1);
    };

    auto *flexible = page(QStringLiteral("Flexible"), QStringLiteral("Enable flexible tip"), JiggleParams::Flexible);
    tipFields(flexible);
    auto *flexibleForm = group(flexible, 0, 3, QStringLiteral("Tip springs"), 0, 2);
    field(flexibleForm, QStringLiteral("Yaw stiffness:"), &JiggleParams::yawStiffness, Positive, 1);
    field(flexibleForm, QStringLiteral("Yaw damping:"), &JiggleParams::yawDamping, Positive, 0.1);
    field(flexibleForm, QStringLiteral("Pitch stiffness:"), &JiggleParams::pitchStiffness, Positive, 1);
    field(flexibleForm, QStringLiteral("Pitch damping:"), &JiggleParams::pitchDamping, Positive, 0.1);
    m_lengthFlex = new QCheckBox(QStringLiteral("Allow length flex"), flexibleForm->parentWidget());
    QObject::connect(m_lengthFlex, &QCheckBox::clicked, this, [edit](bool flex) {
        edit([&](JiggleParams &rule) {
            rule.flags = flex ? rule.flags & ~JiggleParams::LengthConstraint : rule.flags | JiggleParams::LengthConstraint;
        });
    });
    flexibleForm->addRow(m_lengthFlex);
    field(flexibleForm, QStringLiteral("Along stiffness:"), &JiggleParams::alongStiffness, Positive, 1);
    field(flexibleForm, QStringLiteral("Along damping:"), &JiggleParams::alongDamping, Positive, 0.1);

    tipFields(page(QStringLiteral("Rigid"), QStringLiteral("Enable rigid tip"), JiggleParams::Rigid));

    auto *base = page(QStringLiteral("Base Spring"), QStringLiteral("Enable base spring"), JiggleParams::BaseSpring);
    auto *baseForm = group(base, 0, 0, QStringLiteral("Spring"));
    field(baseForm, QStringLiteral("Mass:"), &JiggleParams::baseMass, Positive, 1);
    field(baseForm, QStringLiteral("Stiffness:"), &JiggleParams::baseStiffness, Positive, 1);
    field(baseForm, QStringLiteral("Damping:"), &JiggleParams::baseDamping, Positive, 0.1);
    auto *left = group(base, 0, 1, QStringLiteral("Left"));
    field(left, QStringLiteral("Min:"), &JiggleParams::baseMinLeft, Signed, 0.5);
    field(left, QStringLiteral("Max:"), &JiggleParams::baseMaxLeft, Signed, 0.5);
    field(left, QStringLiteral("Friction:"), &JiggleParams::baseLeftFriction, Positive, 0.1);
    auto *up = group(base, 0, 2, QStringLiteral("Up"));
    field(up, QStringLiteral("Min:"), &JiggleParams::baseMinUp, Signed, 0.5);
    field(up, QStringLiteral("Max:"), &JiggleParams::baseMaxUp, Signed, 0.5);
    field(up, QStringLiteral("Friction:"), &JiggleParams::baseUpFriction, Positive, 0.1);
    auto *forward = group(base, 0, 3, QStringLiteral("Forward"));
    field(forward, QStringLiteral("Min:"), &JiggleParams::baseMinForward, Signed, 0.5);
    field(forward, QStringLiteral("Max:"), &JiggleParams::baseMaxForward, Signed, 0.5);
    field(forward, QStringLiteral("Friction:"), &JiggleParams::baseForwardFriction, Positive, 0.1);
    base->setRowStretch(1, 1);
    QObject::connect(tabs, &QTabWidget::currentChanged, this, [this] { ShowRule(); });

    QObject::connect(m_filter, &QLineEdit::textChanged, this, [this] { UpdateRows(); });
    QObject::connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        if (m_highlight->isChecked())
        {
            m_settings.highlightBone = Selected();
            m_panel->redraw();
        }
        ShowRule();
    });
    QObject::connect(m_highlight, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.highlightBone = on ? Selected() : -1;
        m_panel->redraw();
    });
    QObject::connect(m_enabled, &QCheckBox::clicked, this, [this](bool on) {
        const int bone = Selected();
        if (bone < 0)
            return;
        if (on) // the rule this bone had last, else the file's, else studiomdl's defaults
            Apply(bone, m_stash[bone] ? m_stash[bone] : m_saved[bone] ? m_saved[bone] : JiggleParams{});
        else
        {
            m_stash[bone] = m_current[bone];
            Apply(bone, std::nullopt);
        }
        ShowRule();
    });
    QObject::connect(m_revert, &QPushButton::clicked, this, [this] {
        if (const int bone = Selected(); bone >= 0)
        {
            Apply(bone, m_saved[bone]);
            ShowRule();
        }
    });
    QObject::connect(m_save, &QPushButton::clicked, this, [this] { Save(); });

    auto *layout = new QGridLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Bones:"), this), 0, 0);
    layout->addWidget(m_filter, 1, 0);
    layout->addWidget(m_list, 2, 0, 2, 1);
    auto *head = new QHBoxLayout;
    head->addWidget(m_enabled);
    head->addWidget(m_highlight);
    head->addStretch();
    head->addWidget(m_revert);
    head->addWidget(m_save);
    layout->addLayout(head, 0, 1, 2, 1);
    layout->addWidget(m_editor, 2, 1);
    layout->addWidget(m_status, 3, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 4);
    layout->setRowStretch(2, 1);
    ShowRule();
}

void CJiggleWindow::OnLoadModel(const QString &path, const QStringList &bones)
{
    m_path = path;
    m_bones = bones;
    m_states.clear();
    m_saved.clear();
    QString error;
    try
    {
        const auto mdl = ReadAssetFile(path, 512 * 1024 * 1024);
        m_states = ReadJiggleStates(mdl);
        m_saved = ReadJiggleBones(mdl);
        if (m_states.size() != size_t(bones.size()))
            throw std::runtime_error("bone count does not match the loaded model");
    }
    catch (const std::exception &failure)
    {
        error = QString::fromUtf8(failure.what());
        m_states.clear();
        m_saved.clear();
    }
    m_current = m_saved;
    m_stash.assign(m_saved.size(), std::nullopt);
    {
        const QSignalBlocker quiet(m_list);
        m_list->clear();
        for (qsizetype i = 0; i < qsizetype(m_states.size()); ++i)
            m_list->addItem(bones[i]);
        const auto first = std::ranges::find_if(m_saved, [](const auto &rule) { return rule.has_value(); });
        m_list->setCurrentRow(m_saved.empty() ? -1 : first != m_saved.end() ? int(first - m_saved.begin()) : 0);
    }
    ShowRule();
    if (!error.isEmpty())
        m_status->setText(Error(error));
}

int CJiggleWindow::Selected() const
{
    const int row = m_list->currentRow();
    return row >= 0 && size_t(row) < m_states.size() ? row : -1;
}

void CJiggleWindow::Apply(int bone, const std::optional<JiggleParams> &rule)
{
    try
    {
        if (const auto &animation = m_settings.animation; animation && !animation->SetJiggle(bone, rule))
        {
            m_status->setText(Error(m_bones[bone] + QStringLiteral(" already has another procedural rule")));
            return;
        }
    }
    catch (const std::exception &failure)
    {
        m_status->setText(Error(QString::fromUtf8(failure.what())));
        return;
    }
    m_current[bone] = rule;
    UpdateRows();
    m_panel->redraw();
}

void CJiggleWindow::UpdateRows()
{
    const auto filter = m_filter->text().trimmed();
    int unsaved = 0;
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        const bool edited = m_current[i] != m_saved[i];
        unsaved += edited;
        auto *item = m_list->item(int(i));
        item->setText((edited ? QStringLiteral("* ") : QString()) + m_bones[int(i)] +
                      (m_current[i] ? QStringLiteral("  [jiggle]")
                                    : m_states[i] == JiggleState::OtherProcedural ? QStringLiteral("  [procedural]") : QString()));
        item->setHidden(!filter.isEmpty() && !m_bones[int(i)].contains(filter, Qt::CaseInsensitive));
    }
    const int bone = Selected();
    m_revert->setEnabled(bone >= 0 && m_current[bone] != m_saved[bone]);
    m_save->setEnabled(unsaved);
    m_save->setText(unsaved ? QStringLiteral("Save MDL (%1)").arg(unsaved) : QStringLiteral("Save MDL"));
    auto status = m_path.toHtmlEscaped();
    if (!m_settings.animation)
        status += QStringLiteral("<br>No sequences loaded: edits are saved but not previewed");
    if (unsaved)
        status += QStringLiteral("<br>%1 unsaved bone(s)").arg(unsaved);
    m_status->setText(status);
}

void CJiggleWindow::ShowRule()
{
    const int bone = Selected();
    const bool other = bone >= 0 && m_states[bone] == JiggleState::OtherProcedural;
    const auto &current = bone >= 0 ? m_current[bone] : std::nullopt;
    const auto rule = current.value_or(JiggleParams{});
    m_enabled->setEnabled(bone >= 0 && !other);
    m_enabled->setToolTip(other ? QStringLiteral("This bone already has another procedural rule")
                                : QStringLiteral("Give the selected bone a jiggle rule, or remove its rule"));
    m_enabled->setChecked(current.has_value());
    m_editor->setEnabled(current.has_value());
    m_lengthFlex->setChecked(!(rule.flags & JiggleParams::LengthConstraint));
    for (const auto &[box, flag] : m_groups)
    {
        const QSignalBlocker quiet(box);
        box->setChecked(rule.flags & flag);
    }
    for (const auto &field : m_fields)
    {
        const QSignalBlocker quiet(field.spin);
        field.spin->setValue(field.angle ? rule.*field.member * degrees : rule.*field.member);
    }
    if (bone >= 0)
        UpdateRows();
}

void CJiggleWindow::Save()
{
    JiggleEdits edits;
    for (size_t i = 0; i < m_states.size(); ++i)
        if (m_current[i] != m_saved[i])
            edits[int(i)] = m_current[i];
    if (edits.empty())
        return;
    try
    {
        const auto notes = SaveMdlJiggles(m_path, edits);
        m_saved = m_current;
        UpdateRows();
        m_status->setText(m_status->text() + QStringLiteral("<br>") + notes.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    catch (const std::exception &failure)
    {
        m_status->setText(Error(QString::fromUtf8(failure.what())));
        mxMessageBox(this, (std::string("The model was not saved:\n") + failure.what()).c_str(), "Save MDL", MX_MBERROR | MX_MBOK);
    }
}
