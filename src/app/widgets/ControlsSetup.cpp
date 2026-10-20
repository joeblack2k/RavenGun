// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/ControlsSetup.h"

#include <algorithm>

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPalette>
#include <QVBoxLayout>

#include "core/AppPaths.h"
#include "core/ControlsLegend.h"
#include "ravengun/gamesettings/controls_settings.hpp"

namespace gs = ravengun::gamesettings;

namespace rg::ui {

namespace {

QLabel* bigLabel(int px, bool bold, const QString& color = {}) {
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignCenter);
    QFont f = l->font();
    f.setPixelSize(px);
    f.setBold(bold);
    l->setFont(f);
    if (!color.isEmpty()) l->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    return l;
}

QPushButton* bigButton(const QString& text) {
    auto* b = new QPushButton(text);
    QFont f = b->font();
    f.setPixelSize(22);
    b->setFont(f);
    b->setMinimumHeight(54);
    b->setMinimumWidth(150);
    b->setFocusPolicy(Qt::NoFocus);   // de Move kiest, niet Tab
    return b;
}

gs::ControlsInfo infoFor(const QString& profile, const gs::Dirs& dirs, bool* needs = nullptr) {
    const gs::Game g = gs::load_game(dirs, profile.toStdString());
    if (needs) *needs = gs::controls_need_setup(g.settings);
    return gs::controls_info(g.settings);
}

}  // namespace

ControlsSetup::ControlsSetup(rg::AppCore* core, const QString& profile, QWidget* parent)
    : QWidget(parent),
      core_(core),
      profile_(profile),
      dirs_(core ? core->gameData()->dirs() : gs::default_dirs(rg::AppPaths::dataRoot().toStdString())),
      walk_(infoFor(profile, dirs_), baseLayout(core)) {
    setObjectName(QStringLiteral("ControlsSetup"));
    setAutoFillBackground(true);
    // Een eigen donker palet: op het startscherm (PreLaunchWindow, zwarte achtergrond) erfden de titel en de uitleg anders zwarte
    // tekst en bleven ze onzichtbaar (gemeten op de tv, 2026-10-17). Zo is het scherm overal hetzelfde: tv-leesbaar, licht op donker.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(QStringLiteral("#0b0d10")));
    pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#f2f4f7")));
    pal.setColor(QPalette::Text, QColor(QStringLiteral("#f2f4f7")));
    setPalette(pal);
    setFocusPolicy(Qt::StrongFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(48, 32, 48, 32);
    header_ = bigLabel(22, false, QStringLiteral("#9aa4b2"));
    root->addWidget(header_);
    pages_ = new QStackedWidget;
    root->addWidget(pages_, 1);

    // Pagina 0: een stap.
    auto* step = new QWidget;
    auto* sv = new QVBoxLayout(step);
    sv->addStretch(1);
    label_ = bigLabel(64, true);
    explanation_ = bigLabel(28, false);
    prompt_ = bigLabel(36, true, QStringLiteral("#ffd166"));
    hint_ = bigLabel(22, false, QStringLiteral("#9aa4b2"));
    sv->addWidget(label_);
    sv->addWidget(explanation_);
    sv->addSpacing(24);
    sv->addWidget(prompt_);
    sv->addWidget(hint_);
    clickRow_ = new QWidget;
    auto* cr = new QHBoxLayout(clickRow_);
    for (const auto& b : gs::control_buttons()) {
        auto* btn = bigButton(QString::fromStdString(rg::ControlsWalk::buttonName(b)));
        btn->setMinimumWidth(110);
        connect(btn, &QPushButton::clicked, this, [this, b] { pressButton(QString::fromStdString(b)); });
        cr->addWidget(btn);
    }
    sv->addWidget(clickRow_);
    sv->addStretch(1);
    pages_->addWidget(step);

    // Pagina 1: bevestigen.
    auto* conf = new QWidget;
    auto* cv = new QVBoxLayout(conf);
    cv->addStretch(1);
    auto* ct = bigLabel(48, true);
    ct->setText(tr("Your controls"));
    summary_ = bigLabel(28, false);
    summary_->setTextFormat(Qt::RichText);
    all_ = new QCheckBox(tr("Use the same buttons for every player"));
    all_->setChecked(true);
    {
        QFont f = all_->font();
        f.setPixelSize(22);
        all_->setFont(f);
    }
    all_->setFocusPolicy(Qt::NoFocus);
    connect(all_, &QCheckBox::toggled, this, [this](bool on) { allPlayers_ = on; });
    auto* confirmHint = bigLabel(26, true, QStringLiteral("#ffd166"));
    confirmHint->setText(tr("Pull the trigger to save. Press PS to start over."));
    cv->addWidget(ct);
    cv->addWidget(summary_);
    cv->addSpacing(16);
    cv->addWidget(all_, 0, Qt::AlignHCenter);
    cv->addWidget(confirmHint);
    cv->addStretch(1);
    pages_->addWidget(conf);

    // Onderaan: knoppen voor muis en toetsenbord.
    auto* bottom = new QHBoxLayout;
    cancel_ = bigButton(tr("Cancel"));
    back_ = bigButton(tr("Back"));
    skip_ = bigButton(tr("Keep default"));
    restart_ = bigButton(tr("Start over"));
    save_ = bigButton(tr("Save"));
    bottom->addWidget(cancel_);
    bottom->addStretch(1);
    bottom->addWidget(back_);
    bottom->addWidget(skip_);
    bottom->addWidget(restart_);
    bottom->addWidget(save_);
    root->addLayout(bottom);
    status_ = bigLabel(18, false, QStringLiteral("#9aa4b2"));
    root->addWidget(status_);

    connect(cancel_, &QPushButton::clicked, this, [this] { emit finished(false); });
    connect(back_, &QPushButton::clicked, this, [this] { walk_.back(); rebuild(); });
    connect(skip_, &QPushButton::clicked, this, [this] { walk_.keepDefault(); rebuild(); });
    connect(restart_, &QPushButton::clicked, this, [this] { walk_.restart(); rebuild(); });
    connect(save_, &QPushButton::clicked, this, [this] { if (save()) emit finished(true); });

    stream_ = std::make_unique<rg::AimStream>(QString(), this);
    connect(stream_.get(), &rg::AimStream::aimChanged, this, &ControlsSetup::onAim);
    connect(stream_.get(), &rg::AimStream::connectedChanged, this, [this](bool) { rebuild(); });
    rebuild();

    // Niets te kiezen (geen toewijsbaar signaal): klaar zonder iets te vragen.
    const auto lines = walk_.summary();
    if (std::none_of(lines.begin(), lines.end(), [](const rg::ControlsWalk::Line& l) { return l.assignable; })) {
        QMetaObject::invokeMethod(this, [this] { emit finished(true); }, Qt::QueuedConnection);
    }
}

ControlsSetup::~ControlsSetup() = default;

bool ControlsSetup::needsSetup(const QString& profile) {
    return needsSetup(profile, gs::default_dirs(rg::AppPaths::dataRoot().toStdString()));
}

bool ControlsSetup::needsSetup(const QString& profile, const gs::Dirs& dirs) {
    if (profile.isEmpty()) return false;
    bool needs = false;
    infoFor(profile, dirs, &needs);
    return needs;
}

bool ControlsSetup::hasControls(const QString& profile, const gs::Dirs& dirs) {
    if (profile.isEmpty()) return false;
    for (const auto& s : infoFor(profile, dirs).entries) if (s.assignable) return true;
    return false;
}

void ControlsSetup::setGameTitle(const QString& title) {
    title_ = title;
    rebuild();
}

// The global layout the walk starts from: the engine's status, else its config.json, else the engine's built-in default (so the
// Sharp Shooter inputs pump and ss_rl are always part of it). The same source as the legend on the start screen.
std::map<std::string, std::string> ControlsSetup::baseLayout(rg::AppCore* core) {
    return rg::controls::baseLayout(core ? core->engine()->status() : QJsonObject());
}

QString ControlsSetup::defaultText(const rg::ControlsWalk::Line& l) const {
    if (l.buttons.empty()) return tr("no button");
    QStringList names;
    for (const auto& b : l.buttons) names << QString::fromStdString(rg::ControlsWalk::buttonName(b));
    return names.join(QStringLiteral(" + "));
}

void ControlsSetup::rebuild() {
    const bool confirm = walk_.done();
    pages_->setCurrentIndex(confirm ? 1 : 0);
    const QString game = title_.isEmpty() ? profile_ : title_;
    if (confirm) {
        header_->setText(tr("%1: controls").arg(game));
        QString html;
        for (const auto& l : walk_.summary()) {
            if (!l.assignable) continue;
            html += QStringLiteral("<p><b>%1</b> &nbsp;&rarr;&nbsp; %2%3</p>")
                        .arg(l.label.empty() ? QString() : QString::fromStdString(l.label).toHtmlEscaped(), defaultText(l).toHtmlEscaped(),
                             l.picked ? QString() : tr(" (default)").toHtmlEscaped());
        }
        const auto unb = walk_.unbound();
        if (!unb.empty()) {
            QStringList names;
            for (const auto& u : unb) names << QString::fromStdString(u);
            html += QStringLiteral("<p style='color:#ff8080'>%1</p>")
                        .arg(tr("No button for: %1. Press Start over and give it one.").arg(names.join(QStringLiteral(", "))).toHtmlEscaped());
        }
        summary_->setText(html);
    } else {
        const rg::ControlsWalk& w = walk_;
        const auto& s = w.step();
        header_->setText(tr("%1: controls (%2 of %3)").arg(game).arg(w.index() + 1).arg(w.count()));
        label_->setText(QString::fromStdString(s.label));
        explanation_->setText(QString::fromStdString(s.explanation));
        explanation_->setVisible(!s.explanation.empty());
        clickRow_->setVisible(s.assignable);
        if (s.assignable) {
            prompt_->setText(tr("Press the button for: %1").arg(QString::fromStdString(s.label)));
            rg::ControlsWalk::Line cur;
            cur.buttons = w.defaultButtons(s.signal);
            hint_->setText(tr("Now: %1. Press PS to keep it.").arg(defaultText(cur)));
            skip_->setText(tr("Keep default"));
        } else {
            prompt_->setText(s.signal == "trigger" ? tr("Pull the trigger to continue") : tr("Press any button to continue"));
            hint_->clear();
            skip_->setText(tr("Continue"));
        }
    }
    back_->setVisible(!confirm && walk_.index() > 0);
    skip_->setVisible(!confirm);
    restart_->setVisible(confirm);
    save_->setVisible(confirm);
    const bool linked = stream_ && stream_->isActive();
    status_->setText(linked ? (error_.isEmpty() ? tr("Using the PS Move of player 1. You can also use the mouse.") : error_)
                            : tr("Waiting for the RavenGun engine. You can use the mouse in the meantime."));
}

void ControlsSetup::onAim(const rg::bigpicture::AimState& s) {
    for (const QString& b : s.pressed) pressButton(b);
}

void ControlsSetup::pressButton(const QString& moveButton) {
    // Debounce: a quick double press (or two buttons in one frame) counts as one.
    if (sinceAccepted_.isValid() && sinceAccepted_.elapsed() < 350) return;
    const std::string b = moveButton.toStdString();
    if (walk_.done()) {   // confirm page: the trigger saves, PS starts over
        if (b == "t" || b == "ss_fire") {   // with a Sharp Shooter fitted the trigger is its fire bit
            sinceAccepted_.restart();
            if (save()) emit finished(true);
            else rebuild();
        } else if (b == "ps") {
            sinceAccepted_.restart();
            walk_.restart();
            rebuild();
        }
        return;
    }
    const auto r = walk_.press(b);
    if (r == rg::ControlsWalk::Press::Ignored) {
        if (b == "t" && walk_.step().assignable) status_->setText(tr("The trigger cannot be changed. Press another button."));
        return;
    }
    sinceAccepted_.restart();
    if (r == rg::ControlsWalk::Press::Duplicate) {
        status_->setText(tr("%1 is already used for another action. Press a different button.").arg(QString::fromStdString(rg::ControlsWalk::buttonName(b))));
        return;
    }
    error_.clear();
    rebuild();
}

void ControlsSetup::setAllPlayers(bool on) {
    allPlayers_ = on;
    if (all_) all_->setChecked(on);
}

bool ControlsSetup::save() {
    error_.clear();
    std::vector<std::vector<std::pair<std::string, std::string>>> per;
    const auto c = walk_.choices();
    per.push_back(c);
    if (allPlayers_) for (int i = 1; i < 4; ++i) per.push_back(c);
    std::string err;
    if (!gs::save_user_controls(dirs_, profile_.toStdString(), per, &err)) {
        error_ = tr("Could not save the controls: %1").arg(QString::fromStdString(err));
        return false;
    }
    return true;
}

void ControlsSetup::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { emit finished(false); return; }
    if (e->key() == Qt::Key_Backspace && !walk_.done()) { walk_.back(); rebuild(); return; }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && walk_.done()) {
        if (save()) emit finished(true);
        else rebuild();
        return;
    }
    QWidget::keyPressEvent(e);
}

void ControlsSetup::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (stream_ && !stream_->isActive()) stream_->start();
    setFocus();
}

void ControlsSetup::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    if (stream_) stream_->stop();
}

}  // namespace rg::ui
