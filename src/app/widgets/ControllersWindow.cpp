// SPDX-License-Identifier: GPL-3.0-or-later
// Rijopbouw naar Dolphin GamecubeControllersWidget.cpp (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08).
#include "widgets/ControllersWindow.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/AppPaths.h"
#include "widgets/MoveConfigDialog.h"
#include "widgets/PairWizardDialog.h"
#include "widgets/QtUtils.h"
#include "widgets/Resources.h"

namespace rg::ui {

namespace {

QSettings& settings() {
    static QSettings s(rg::AppPaths::appSettingsFile(), QSettings::IniFormat);
    return s;
}

QString batteryText(const QJsonObject& c) {
    if (!c.value(QStringLiteral("connected")).toBool()) return QObject::tr("not connected");
    const QJsonObject b = c.value(QStringLiteral("battery")).toObject();
    if (!b.value(QStringLiteral("known")).toBool()) return QObject::tr("battery unknown");
    QString s = b.value(QStringLiteral("percent")).isNull() ? b.value(QStringLiteral("code")).toString()
                                                            : QStringLiteral("%1%").arg(b.value(QStringLiteral("percent")).toInt());
    if (b.value(QStringLiteral("charging")).toBool()) s += QObject::tr(", charging");
    return s;
}

QString typeText(const QJsonObject& c) {
    const QString t = c.value(QStringLiteral("type")).toString();
    if (t == QStringLiteral("move")) return QStringLiteral("PS Move") + (c.value(QStringLiteral("model")).toString().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(c.value(QStringLiteral("model")).toString()));
    if (t == QStringLiteral("navigation")) return QStringLiteral("Navigation Controller");
    return t;
}

}  // namespace

ControllersWindow::ControllersWindow(rg::AppCore* core, QWidget* parent) : QDialog(parent), core_(core) {
    setWindowTitle(tr("Controller Settings"));
    setWindowIcon(Resources::GetThemeIcon("classic"));
    CreateLayout();
    connect(core_->engine(), &rg::EngineClient::controllersUpdated, this, &ControllersWindow::Refresh);
    connect(core_->engine(), &rg::EngineClient::connectedChanged, this, [this] {
        Refresh();
        RefreshMice();
    });
    auto* t = new QTimer(this);
    t->setInterval(3000);
    connect(t, &QTimer::timeout, this, &ControllersWindow::RefreshMice);
    t->start();
    Refresh();
    RefreshMice();
    resize(760, 620);
}

void ControllersWindow::CreateLayout() {
    auto* layout = new QVBoxLayout(this);

    m_players_box = new QGroupBox(tr("Lightguns"));
    m_players_layout = new QGridLayout(m_players_box);
    m_players_layout->setVerticalSpacing(7);
    m_players_layout->setColumnStretch(1, 1);
    for (int i = 0; i < kPlayers; i++) {
        auto* label = new QLabel(tr("Player %1").arg(i + 1));
        auto* box = m_combos[i] = new QComboBox();
        auto* button = m_configure[i] = new NonDefaultQPushButton(tr("Configure"));
        auto* state = m_state[i] = new QLabel;
        state->setMinimumWidth(150);
        const int row = m_players_layout->rowCount();
        m_players_layout->addWidget(label, row, 0);
        m_players_layout->addWidget(box, row, 1);
        m_players_layout->addWidget(state, row, 2);
        m_players_layout->addWidget(button, row, 3);
        connect(box, &QComboBox::currentIndexChanged, this, [this, i] { OnTypeChanged(i + 1); });
        connect(button, &QPushButton::clicked, this, [this, i] { OnConfigure(i + 1); });
    }
    m_mouse_note = new QLabel;
    m_mouse_note->setWordWrap(true);
    m_players_layout->addWidget(m_mouse_note, m_players_layout->rowCount(), 0, 1, 4);
    layout->addWidget(m_players_box);

    auto* known = new QGroupBox(tr("Controllers"));
    auto* kl = new QVBoxLayout(known);
    m_list = new QTreeWidget;
    m_list->setHeaderLabels({tr("Name"), tr("Type"), tr("Player"), tr("Connection"), tr("Battery"), tr("Last seen")});
    m_list->setRootIsDecorated(false);
    m_list->setAlternatingRowColors(true);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    kl->addWidget(m_list);
    auto* buttons = new QHBoxLayout;
    auto* pair = new NonDefaultQPushButton(tr("Pair New Controller..."));
    m_rename = new NonDefaultQPushButton(tr("Rename..."));
    m_player = new NonDefaultQPushButton(tr("Player..."));
    m_blink = new NonDefaultQPushButton(tr("Blink"));
    m_remove = new NonDefaultQPushButton(tr("Remove..."));
    buttons->addWidget(pair);
    buttons->addStretch(1);
    buttons->addWidget(m_rename);
    buttons->addWidget(m_player);
    buttons->addWidget(m_blink);
    buttons->addWidget(m_remove);
    kl->addLayout(buttons);
    layout->addWidget(known, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(bb);

    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        const QJsonObject c = ControllerByKey(SelectedControllerKey());
        const bool any = !c.isEmpty();
        m_rename->setEnabled(any);
        m_player->setEnabled(any);
        m_remove->setEnabled(any);
        m_blink->setEnabled(any && c.value(QStringLiteral("type")).toString() == QStringLiteral("move") &&
                            c.value(QStringLiteral("connected")).toBool());
    });
    m_rename->setEnabled(false);
    m_player->setEnabled(false);
    m_blink->setEnabled(false);
    m_remove->setEnabled(false);
    connect(pair, &QPushButton::clicked, this, [this] {
        PairWizardDialog d(core_, this);
        d.exec();
        core_->engine()->refreshNow();
    });
    connect(m_rename, &QPushButton::clicked, this, [this] {
        const QJsonObject c = ControllerByKey(SelectedControllerKey());
        if (c.isEmpty()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename Controller"), tr("Name (1 to 32 characters):"), QLineEdit::Normal,
                                                   c.value(QStringLiteral("name")).toString(), &ok)
                                 .trimmed();
        if (!ok || name.isEmpty()) return;
        core_->engine()->request(QStringLiteral("set_controller"),
                                 {{QStringLiteral("controller"), c.value(QStringLiteral("key")).toString()}, {QStringLiteral("name"), name.left(32)}},
                                 [this](const rg::EngineReply& r) {
                                     if (!r.ok) QMessageBox::warning(this, tr("Rename Controller"), r.errorMessage);
                                     core_->engine()->refreshNow();
                                 });
    });
    // Speler kiezen, ook voor een Navigation Controller (die vult de Move van die speler aan). Bezet = ruilen (engine).
    connect(m_player, &QPushButton::clicked, this, [this] {
        const QJsonObject c = ControllerByKey(SelectedControllerKey());
        if (c.isEmpty()) return;
        const QStringList players{tr("Player 1"), tr("Player 2"), tr("Player 3"), tr("Player 4")};
        bool ok = false;
        const int cur = std::clamp(c.value(QStringLiteral("player")).toInt(1), 1, 4) - 1;
        const QString pick = QInputDialog::getItem(this, tr("Player"), tr("Player for \"%1\":").arg(c.value(QStringLiteral("name")).toString()),
                                                   players, cur, false, &ok);
        if (!ok) return;
        core_->engine()->request(QStringLiteral("set_controller"),
                                 {{QStringLiteral("controller"), c.value(QStringLiteral("key")).toString()},
                                  {QStringLiteral("new_player"), static_cast<int>(players.indexOf(pick)) + 1}},
                                 [this](const rg::EngineReply& r) {
                                     if (!r.ok) QMessageBox::warning(this, tr("Player"), r.errorMessage);
                                     core_->engine()->refreshNow();
                                 });
    });
    connect(m_blink, &QPushButton::clicked, this, [this] {
        core_->engine()->request(QStringLiteral("blink"), {{QStringLiteral("controller"), SelectedControllerKey()}}, [this](const rg::EngineReply& r) {
            if (!r.ok) QMessageBox::warning(this, tr("Blink"), r.errorMessage);
        });
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const QJsonObject c = ControllerByKey(SelectedControllerKey());
        if (c.isEmpty()) return;
        QMessageBox box(QMessageBox::Question, tr("Remove Controller"),
                        tr("Remove \"%1\"? It is unpaired from this PC.").arg(c.value(QStringLiteral("name")).toString()),
                        QMessageBox::NoButton, this);
        QPushButton* keep = box.addButton(tr("Remove, Keep Settings"), QMessageBox::AcceptRole);
        QPushButton* wipe = box.addButton(tr("Remove and Delete Settings"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != keep && box.clickedButton() != wipe) return;
        core_->engine()->request(QStringLiteral("unpair"),
                                 {{QStringLiteral("controller"), c.value(QStringLiteral("key")).toString()},
                                  {QStringLiteral("keep_settings"), box.clickedButton() == keep}},
                                 [this](const rg::EngineReply& r) {
                                     if (!r.ok) QMessageBox::warning(this, tr("Remove Controller"), r.errorMessage);
                                     core_->engine()->refreshNow();
                                 });
    });
}

QJsonObject ControllersWindow::ControllerByKey(const QString& key) const {
    for (const auto& v : m_controllers)
        if (v.toObject().value(QStringLiteral("key")).toString() == key) return v.toObject();
    return {};
}

QString ControllersWindow::SelectedControllerKey() const {
    const auto sel = m_list->selectedItems();
    return sel.isEmpty() ? QString() : sel.first()->data(0, Qt::UserRole).toString();
}

void ControllersWindow::Refresh() {
    m_controllers = core_->engine()->controllers().value(QStringLiteral("controllers")).toArray();
    const QString selected = SelectedControllerKey();
    m_list->clear();
    for (const auto& v : m_controllers) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("removed")).toBool()) continue;
        auto* it = new QTreeWidgetItem(m_list);
        it->setText(0, c.value(QStringLiteral("name")).toString());
        it->setText(1, typeText(c));
        const int p = c.value(QStringLiteral("player")).toInt();
        it->setText(2, p > 0 ? QString::number(p) : tr("none"));
        it->setText(3, c.value(QStringLiteral("connected")).toBool()
                           ? (c.value(QStringLiteral("connection")).toString() == QStringLiteral("usb") ? tr("USB") : tr("Bluetooth"))
                           : (c.value(QStringLiteral("paired")).toBool() ? tr("Paired, off") : tr("Not paired")));
        it->setText(4, batteryText(c));
        const qint64 seen = c.value(QStringLiteral("last_seen_unix")).toInteger();
        it->setText(5, seen > 0 ? QDateTime::fromSecsSinceEpoch(seen).toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString());
        it->setData(0, Qt::UserRole, c.value(QStringLiteral("key")).toString());
        if (c.value(QStringLiteral("key")).toString() == selected) it->setSelected(true);
    }
    for (int c = 1; c < 6; ++c) m_list->resizeColumnToContents(c);
    if (!core_->engine()->isConnected()) {
        auto* it = new QTreeWidgetItem(m_list);
        it->setText(0, tr("The engine is not running."));
        it->setFlags(Qt::NoItemFlags);
    }
    Populate();
}

void ControllersWindow::RefreshMice() {
    if (!core_->engine()->isConnected()) return;
    core_->engine()->request(QStringLiteral("mice"), {}, [this](const rg::EngineReply& r) {
        if (!r.ok) {
            if (r.errorCode == QStringLiteral("unknown_command")) m_mice_supported = false;
            return;
        }
        m_mice_supported = true;
        m_mice = r.object().value(QStringLiteral("mice")).toArray();
        m_assignments = r.object().value(QStringLiteral("assignments")).toArray();
        Populate();
    });
}

void ControllersWindow::Populate() {
    m_updating = true;
    const bool connected = core_->engine()->isConnected();
    for (int i = 0; i < kPlayers; ++i) {
        const int player = i + 1;
        QComboBox* box = m_combos[i];
        box->clear();
        box->addItem(tr("None"), QStringLiteral("none"));
        QString current = QStringLiteral("none");
        QString state;
        for (const auto& v : m_controllers) {
            const QJsonObject c = v.toObject();
            if (c.value(QStringLiteral("removed")).toBool() || c.value(QStringLiteral("type")).toString() != QStringLiteral("move")) continue;
            const QString data = QStringLiteral("move:") + c.value(QStringLiteral("key")).toString();
            box->addItem(Resources::GetThemeIcon("classic"), tr("PS Move: %1").arg(c.value(QStringLiteral("name")).toString()), data);
            if (c.value(QStringLiteral("player")).toInt() == player) {
                current = data;
                state = batteryText(c);
            }
        }
        if (m_mice_supported) {
            for (const auto& v : m_mice) {
                const QJsonObject m = v.toObject();
                box->addItem(tr("Mouse: %1").arg(m.value(QStringLiteral("name")).toString()), QStringLiteral("mouse:") + m.value(QStringLiteral("id")).toString());
                box->setItemData(box->count() - 1, m.value(QStringLiteral("id")).toString(), Qt::ToolTipRole);
            }
            for (const auto& v : m_assignments) {
                const QJsonObject a = v.toObject();
                if (a.value(QStringLiteral("player")).toInt() != player || a.value(QStringLiteral("mouse")).isNull()) continue;
                if (current == QStringLiteral("none")) {
                    current = QStringLiteral("mouse:") + a.value(QStringLiteral("mouse")).toString();
                    if (box->findData(current) < 0) box->addItem(tr("Mouse: %1 (not found)").arg(a.value(QStringLiteral("mouse")).toString()), current);
                    state = a.value(QStringLiteral("last_error")).isNull()
                                ? (a.value(QStringLiteral("connected")).toBool() ? tr("mouse active") : tr("mouse not connected"))
                                : a.value(QStringLiteral("last_error")).toString();
                }
            }
        }
        box->setCurrentIndex(std::max(0, box->findData(current)));
        box->setEnabled(connected);
        m_state[i]->setText(state);
        m_configure[i]->setEnabled(connected && current != QStringLiteral("none"));
    }
    m_mouse_note->setText(m_mice_supported
                              ? tr("<i>Mouse: the system mouse moves the lightgun of that player (the desktop cursor moves too). "
                                   "A PS Move always belongs to a player; to free a player, give the Move another player or "
                                   "remove it.</i>")
                              : tr("<i>Mouse as lightgun needs a newer engine (IPC command \"mice\"). Update and restart the engine.</i>"));
    m_updating = false;
}

void ControllersWindow::OnTypeChanged(int player) {
    if (m_updating) return;
    QComboBox* box = m_combos[player - 1];
    const QString data = box->currentData().toString();
    QString moveKey, mouseNow;
    for (const auto& v : m_controllers) {
        const QJsonObject c = v.toObject();
        if (!c.value(QStringLiteral("removed")).toBool() && c.value(QStringLiteral("type")).toString() == QStringLiteral("move") &&
            c.value(QStringLiteral("player")).toInt() == player)
            moveKey = c.value(QStringLiteral("key")).toString();
    }
    for (const auto& v : m_assignments)
        if (v.toObject().value(QStringLiteral("player")).toInt() == player && !v.toObject().value(QStringLiteral("mouse")).isNull())
            mouseNow = v.toObject().value(QStringLiteral("mouse")).toString();
    auto done = [this](const rg::EngineReply& r) {
        if (!r.ok) QMessageBox::warning(this, tr("Controller Settings"), r.errorMessage);
        core_->engine()->refreshNow();
        RefreshMice();
    };
    auto setMouse = [this, player, done](const QString& id) {
        settings().setValue(QStringLiteral("mice/player%1").arg(player), id);
        QJsonObject f{{QStringLiteral("player"), player}, {QStringLiteral("mouse"), id}};
        const int counts = settings().value(QStringLiteral("mice/counts%1").arg(player), 0).toInt();
        if (!id.isEmpty() && counts > 0) f.insert(QStringLiteral("counts_per_screen"), counts);
        core_->engine()->request(QStringLiteral("set_mouse_gun"), f, done);
    };
    if (data.startsWith(QStringLiteral("move:"))) {
        if (!mouseNow.isEmpty()) setMouse(QString());
        core_->engine()->request(QStringLiteral("set_controller"),
                                 {{QStringLiteral("controller"), data.mid(5)}, {QStringLiteral("new_player"), player}}, done);
        return;
    }
    if (!moveKey.isEmpty()) {
        QMessageBox::information(this, tr("Controller Settings"),
                                 tr("Player %1 has a PS Move. A PS Move always belongs to a player: give the Move to another "
                                    "player first, or remove it.")
                                     .arg(player));
        Populate();
        return;
    }
    if (data.startsWith(QStringLiteral("mouse:"))) setMouse(data.mid(6));
    else if (!mouseNow.isEmpty()) setMouse(QString());
}

void ControllersWindow::OnConfigure(int player) {
    const QString data = m_combos[player - 1]->currentData().toString();
    if (data.startsWith(QStringLiteral("move:"))) {
        auto* d = new MoveConfigDialog(core_, player, this);
        d->setAttribute(Qt::WA_DeleteOnClose, true);
        d->setWindowModality(Qt::WindowModal);
        d->show();
        return;
    }
    if (data.startsWith(QStringLiteral("mouse:"))) {
        QDialog d(this);
        d.setWindowTitle(tr("Mouse for Player %1").arg(player));
        auto* form = new QFormLayout(&d);
        auto* counts = new QSpinBox;
        counts->setRange(100, 20000);
        counts->setSingleStep(100);
        int cur = 1920;
        for (const auto& v : m_assignments)
            if (v.toObject().value(QStringLiteral("player")).toInt() == player) cur = v.toObject().value(QStringLiteral("counts_per_screen")).toInt(1920);
        counts->setValue(cur);
        form->addRow(tr("Mouse:"), new QLabel(data.mid(6)));
        form->addRow(tr("Mouse movement per screen width:"), counts);
        auto* hint = new QLabel(tr("Lower is faster. The left button is the trigger, right is reload, middle is grenade."));
        hint->setWordWrap(true);
        form->addRow(hint);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted) return;
        settings().setValue(QStringLiteral("mice/counts%1").arg(player), counts->value());
        core_->engine()->request(QStringLiteral("set_mouse_gun"),
                                 {{QStringLiteral("player"), player}, {QStringLiteral("mouse"), data.mid(6)}, {QStringLiteral("counts_per_screen"), counts->value()}},
                                 [this](const rg::EngineReply& r) {
                                     if (!r.ok) QMessageBox::warning(this, tr("Mouse"), r.errorMessage);
                                     RefreshMice();
                                 });
    }
}

void ControllersWindow::ReapplyMouseAssignments(rg::AppCore* core) {
    for (int p = 1; p <= kPlayers; ++p) {
        const QString id = settings().value(QStringLiteral("mice/player%1").arg(p)).toString();
        if (id.isEmpty()) continue;
        QJsonObject f{{QStringLiteral("player"), p}, {QStringLiteral("mouse"), id}};
        const int counts = settings().value(QStringLiteral("mice/counts%1").arg(p), 0).toInt();
        if (counts > 0) f.insert(QStringLiteral("counts_per_screen"), counts);
        core->engine()->request(QStringLiteral("set_mouse_gun"), f, {});
    }
}

}  // namespace rg::ui
