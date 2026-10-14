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
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/AppPaths.h"
#include "core/GamepadText.h"
#include "ravengun/output/gamepad.hpp"
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

// De standaardindeling als tabel (twee paren per regel), uit dezelfde code als de engine.
QString defaultMappingTable() {
    const auto rows = ravengun::output::describe_gamepad_map(ravengun::output::default_gamepad_map());
    QString t = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"2\">");
    const int half = static_cast<int>((rows.size() + 1) / 2);
    for (int i = 0; i < half; ++i) {
        t += QStringLiteral("<tr>");
        for (int col = 0; col < 2; ++col) {
            const size_t k = static_cast<size_t>(i + col * half);
            if (k >= rows.size()) { t += QStringLiteral("<td></td><td></td>"); continue; }
            t += QStringLiteral("<td><b>%1</b></td><td style=\"padding-right:18px\">%2</td>")
                     .arg(QString::fromStdString(rows[k].first).toHtmlEscaped(), rg::GamepadText::target(QString::fromStdString(rows[k].second)).toHtmlEscaped());
        }
        t += QStringLiteral("</tr>");
    }
    return t + QStringLiteral("</table>");
}

}  // namespace

// --- PadLiveView -----------------------------------------------------------------------------------------------------

PadLiveView::PadLiveView(QWidget* parent) : QWidget(parent) { setMinimumSize(sizeHint()); }

QSize PadLiveView::sizeHint() const { return {300, 54}; }

void PadLiveView::setLive(const QJsonObject& live) {
    if (live == live_) return;
    live_ = live;
    update();
}

void PadLiveView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette& pal = palette();
    const bool on = !live_.isEmpty();
    const QColor fg = on ? pal.color(QPalette::Highlight) : pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor rim = pal.color(QPalette::Mid);
    const QJsonObject ax = live_.value(QStringLiteral("axes")).toObject();
    auto val = [&](const char* k, double def) { return ax.value(QLatin1String(k)).toDouble(def); };
    const int r = 22, cy = height() / 2;
    // Sticks: 0..1 met het midden op 0,5 (links/boven = 0).
    auto stick = [&](int cx, double x, double y) {
        p.setPen(rim);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(cx, cy), r, r);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawEllipse(QPointF(cx + (x - 0.5) * 2 * (r - 4), cy + (y - 0.5) * 2 * (r - 4)), 4, 4);
    };
    stick(r + 2, val("LeftX", 0.5), val("LeftY", 0.5));
    stick(3 * r + 10, val("RightX", 0.5), val("RightY", 0.5));
    // Triggers: balkjes van onder naar boven.
    auto bar = [&](int x, double v, const QString& label) {
        const int h = 2 * r - 12, top = cy - r;
        p.setPen(rim);
        p.setBrush(Qt::NoBrush);
        p.drawRect(x, top, 10, h);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        const int fill = static_cast<int>(std::clamp(v, 0.0, 1.0) * (h - 2));
        p.drawRect(x + 1, top + h - 1 - fill, 8, fill);
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(QRect(x - 6, top + h, 22, 14), Qt::AlignCenter, label);
    };
    bar(4 * r + 22, val("LT", 0), QStringLiteral("LT"));
    bar(4 * r + 44, val("RT", 0), QStringLiteral("RT"));
    QStringList pressed;
    for (const auto& b : live_.value(QStringLiteral("buttons")).toArray()) pressed << b.toString();
    p.setPen(pal.color(on ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
    const QRect text(4 * r + 66, 0, width() - (4 * r + 66), height());
    p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap,
               !on ? tr("not connected") : pressed.isEmpty() ? tr("no buttons pressed") : pressed.join(QStringLiteral(" ")));
}

// --- ControllersWindow -----------------------------------------------------------------------------------------------

ControllersWindow::ControllersWindow(rg::AppCore* core, QWidget* parent) : QDialog(parent), core_(core) {
    setWindowTitle(tr("Controller Settings"));
    setWindowIcon(Resources::GetThemeIcon("classic"));
    CreateLayout();
    connect(core_->engine(), &rg::EngineClient::controllersUpdated, this, &ControllersWindow::Refresh);
    connect(core_->engine(), &rg::EngineClient::connectedChanged, this, [this] {
        Refresh();
        RefreshMice();
        RefreshPads();
    });
    auto* t = new QTimer(this);
    t->setInterval(3000);
    connect(t, &QTimer::timeout, this, &ControllersWindow::RefreshMice);
    t->start();
    // Gamepads: vaak genoeg voor de live weergave, alleen zolang het venster open is.
    m_pad_timer = new QTimer(this);
    m_pad_timer->setInterval(150);
    connect(m_pad_timer, &QTimer::timeout, this, [this] {
        if (isVisible()) RefreshPads();
    });
    m_pad_timer->start();
    Refresh();
    RefreshMice();
    RefreshPads();
    resize(800, 900);
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
    layout->addWidget(CreateGamepadsBox());

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

QWidget* ControllersWindow::CreateGamepadsBox() {
    auto* box = new QGroupBox(tr("Gamepads"));
    auto* v = new QVBoxLayout(box);
    auto* help = new QLabel(
        tr("<p>A gamepad (Xbox controller, Steam Controller or any other gamepad) adds the controls a lightgun lacks: a steering "
           "wheel, gas and brake, and extra cabinet buttons (camera, wipers, horn, gear shift). It works next to the lightgun of the "
           "same player: the buttons of both work, and the desktop keeps the gamepad too. Without a gamepad, the steering wheel "
           "follows where the lightgun aims.</p>"
           "<p>If player 1 has no gamepad and exactly one gamepad is connected, it is used for player 1 automatically. The engine "
           "keeps your choice when it restarts.</p>"
           "<p><b>Default mapping</b> (for every game without its own):</p>%1"
           "<p>Each game can change this in the <b>[Gamepad]</b> section of its game.ini, for example <tt>RightX = wheel</tt>, "
           "<tt>LB = btn3</tt> or <tt>LT = none</tt>. Properties &gt; General shows the mapping a game uses and where each line "
           "comes from. Format and all controls: docs/gamepad.md.</p>")
            .arg(defaultMappingTable()));
    help->setWordWrap(true);
    help->setTextFormat(Qt::RichText);
    v->addWidget(help);
    auto* grid = new QGridLayout;
    grid->setVerticalSpacing(7);
    grid->setColumnStretch(1, 1);
    for (int i = 0; i < kPlayers; i++) {
        auto* box2 = m_pad_combos[static_cast<size_t>(i)] = new QComboBox;
        auto* live = m_pad_live[static_cast<size_t>(i)] = new PadLiveView;
        auto* state = m_pad_state[static_cast<size_t>(i)] = new QLabel;
        state->setMinimumWidth(150);
        state->setWordWrap(true);
        grid->addWidget(new QLabel(tr("Player %1").arg(i + 1)), i, 0);
        grid->addWidget(box2, i, 1);
        grid->addWidget(live, i, 2);
        grid->addWidget(state, i, 3);
        connect(box2, &QComboBox::currentIndexChanged, this, [this, i] { OnPadChanged(i + 1); });
    }
    v->addLayout(grid);
    m_pad_active = new QLabel;
    m_pad_active->setWordWrap(true);
    v->addWidget(m_pad_active);
    return box;
}

void ControllersWindow::RefreshPads() {
    if (!core_->engine()->isConnected() || !m_pads_supported || m_pads_pending) {
        if (!core_->engine()->isConnected()) PopulatePads(true);
        return;
    }
    m_pads_pending = true;
    core_->engine()->request(QStringLiteral("pads"), {}, [this](const rg::EngineReply& r) {
        m_pads_pending = false;
        if (!r.ok) {
            if (r.errorCode == QStringLiteral("unknown_command")) {
                m_pads_supported = false;
                PopulatePads(true);
            }
            return;
        }
        m_pads = r.object();
        // Pads en toewijzingen zonder de live stand: verandert dat, dan de keuzelijsten opnieuw opbouwen.
        QJsonArray pads;
        for (const auto& v : m_pads.value(QStringLiteral("pads")).toArray()) {
            QJsonObject o = v.toObject();
            o.remove(QStringLiteral("live"));
            pads.append(o);
        }
        const QByteArray sig = QJsonDocument(QJsonObject{{QStringLiteral("p"), pads}, {QStringLiteral("a"), m_pads.value(QStringLiteral("assignments"))},
                                                         {QStringLiteral("g"), m_pads.value(QStringLiteral("game"))}})
                                   .toJson(QJsonDocument::Compact);
        const bool rebuild = sig != m_pads_sig;
        m_pads_sig = sig;
        PopulatePads(rebuild);
    });
}

void ControllersWindow::PopulatePads(bool rebuild) {
    const bool connected = core_->engine()->isConnected();
    const QJsonArray pads = m_pads.value(QStringLiteral("pads")).toArray();
    const QJsonArray as = m_pads.value(QStringLiteral("assignments")).toArray();
    for (int i = 0; i < kPlayers; ++i) {
        const int player = i + 1;
        QJsonObject a;
        for (const auto& v : as)
            if (v.toObject().value(QStringLiteral("player")).toInt() == player) a = v.toObject();
        const QString id = a.value(QStringLiteral("pad")).toString();
        const bool isAuto = a.value(QStringLiteral("auto")).toBool();
        QJsonObject pad;
        for (const auto& v : pads)
            if (v.toObject().value(QStringLiteral("id")).toString() == id && !id.isEmpty()) pad = v.toObject();
        if (rebuild) {
            m_updating = true;
            QComboBox* box = m_pad_combos[static_cast<size_t>(i)];
            box->clear();
            box->addItem(player == 1 ? tr("None (or automatic: the only connected gamepad)") : tr("None"), QString());
            for (const auto& v : pads) {
                const QJsonObject o = v.toObject();
                box->addItem(QStringLiteral("%1 (%2)").arg(o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("event")).toString()),
                             o.value(QStringLiteral("id")).toString());
                box->setItemData(box->count() - 1, o.value(QStringLiteral("id")).toString(), Qt::ToolTipRole);
            }
            const QString want = isAuto ? QString() : id;
            if (!want.isEmpty() && box->findData(want) < 0) box->addItem(tr("%1 (not connected)").arg(want), want);
            box->setCurrentIndex(std::max(0, box->findData(want)));
            box->setEnabled(connected && m_pads_supported);
            m_updating = false;
        }
        QString state;
        if (!m_pads_supported) state = tr("needs a newer engine");
        else if (id.isEmpty()) state = tr("no gamepad");
        else if (!a.value(QStringLiteral("last_error")).isNull()) state = a.value(QStringLiteral("last_error")).toString();
        else if (a.value(QStringLiteral("connected")).toBool())
            state = isAuto ? tr("automatic: %1").arg(pad.value(QStringLiteral("name")).toString()) : tr("connected");
        else state = tr("not connected");
        m_pad_state[static_cast<size_t>(i)]->setText(state);
        m_pad_live[static_cast<size_t>(i)]->setLive(pad.value(QStringLiteral("live")).toObject());
    }
    if (!connected) {
        m_pad_active->setText(tr("<i>The engine is not running.</i>"));
    } else if (!m_pads_supported) {
        m_pad_active->setText(tr("<i>Gamepads need a newer engine (IPC command \"pads\"). Update and restart the engine.</i>"));
    } else {
        const QJsonValue g = m_pads.value(QStringLiteral("game"));
        m_pad_active->setText(g.isString() ? tr("<i>Mapping in use: %1 (from the last started game).</i>").arg(g.toString().toHtmlEscaped())
                                           : tr("<i>Mapping in use: the default (no game started yet).</i>"));
    }
}

void ControllersWindow::OnPadChanged(int player) {
    if (m_updating) return;
    const QString id = m_pad_combos[static_cast<size_t>(player - 1)]->currentData().toString();
    core_->engine()->request(QStringLiteral("set_pad"), {{QStringLiteral("player"), player}, {QStringLiteral("pad"), id}},
                             [this](const rg::EngineReply& r) {
                                 if (!r.ok) QMessageBox::warning(this, tr("Gamepads"), r.errorMessage);
                                 m_pads_sig.clear();   // de keuzelijsten opnieuw opbouwen (ook na een fout: terug naar wat geldt)
                                 RefreshPads();
                             });
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
