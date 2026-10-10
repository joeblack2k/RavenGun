// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/MoveConfigDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "widgets/QtUtils.h"

namespace rg::ui {

// --- LiveSlider -------------------------------------------------------------------------------------------------------

class LiveSlider : public QWidget {
public:
    using Fmt = std::function<QString(double)>;
    using Sender = std::function<void(double, std::function<void()>)>;  // stuurt; roept done() aan na het antwoord
    LiveSlider(double min, double max, double step, bool log, Fmt fmt, Sender send, QWidget* parent = nullptr)
        : QWidget(parent), min_(min), max_(max), step_(step), log_(log), fmt_(std::move(fmt)), send_(std::move(send)) {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        slider_ = new QSlider(Qt::Horizontal);
        slider_->setRange(0, positions());
        label_ = new QLabel;
        label_->setMinimumWidth(80);
        h->addWidget(slider_, 1);
        h->addWidget(label_);
        timer_.setSingleShot(true);
        timer_.setInterval(150);
        connect(slider_, &QSlider::valueChanged, this, [this](int pos) {
            const double v = valueOf(pos);
            label_->setText(fmt_(v));
            if (updating_) return;
            pending_ = v;
            hasPending_ = true;
            if (!timer_.isActive()) timer_.start();
        });
        connect(slider_, &QSlider::sliderReleased, this, [this] { flush(); });
        connect(&timer_, &QTimer::timeout, this, [this] { flush(); });
    }
    void setValue(double v) {
        if (slider_->isSliderDown() || hasPending_ || inflight_) return;
        updating_ = true;
        slider_->setValue(positionOf(v));
        label_->setText(fmt_(v));
        updating_ = false;
    }
    double value() const { return valueOf(slider_->value()); }

private:
    int positions() const { return log_ ? 1000 : static_cast<int>(std::lround((max_ - min_) / step_)); }
    double valueOf(int pos) const {
        if (log_) return min_ * std::pow(max_ / min_, pos / 1000.0);
        return min_ + pos * step_;
    }
    int positionOf(double v) const {
        v = std::clamp(v, min_, max_);
        if (log_) return static_cast<int>(std::lround(1000.0 * std::log(v / min_) / std::log(max_ / min_)));
        return static_cast<int>(std::lround((v - min_) / step_));
    }
    void flush() {
        if (!hasPending_) return;
        if (inflight_) {
            timer_.start();
            return;
        }
        hasPending_ = false;
        inflight_ = true;
        send_(pending_, [this] { inflight_ = false; });
    }
    double min_, max_, step_;
    bool log_;
    Fmt fmt_;
    Sender send_;
    QSlider* slider_;
    QLabel* label_;
    QTimer timer_;
    double pending_ = 0;
    bool hasPending_ = false, inflight_ = false, updating_ = false;
};

namespace {

QLabel* hint(const QString& t) {
    auto* l = new QLabel(t);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setItalic(true);
    l->setFont(f);
    return l;
}

QJsonObject sub(const QJsonObject& o, std::initializer_list<const char*> path) {
    QJsonObject cur = o;
    for (const char* p : path) cur = cur.value(QLatin1String(p)).toObject();
    return cur;
}

struct ButtonDef {
    const char* key;
    const char* label;
};
constexpr ButtonDef kMoveButtons[] = {{"triangle", "Triangle"}, {"circle", "Circle"}, {"cross", "Cross"}, {"square", "Square"},
                                      {"select", "Select"},     {"start", "Start"},   {"move", "Move (big button)"}};
constexpr ButtonDef kSharpShooter[] = {{"pump", "Pump"},         {"ss_rl", "RL button"},  {"ss_fire", "Fire button"},
                                       {"sel1", "Fire mode 1"},   {"sel2", "Fire mode 2"}, {"sel3", "Fire mode 3"}};
constexpr ButtonDef kNavigation[] = {{"nav_cross", "Cross"},        {"nav_circle", "Circle"},        {"nav_l1", "L1"},
                                     {"nav_l2", "L2"},              {"nav_l3", "L3 (stick press)"},  {"nav_ps", "PS"},
                                     {"nav_up", "D-pad up"},        {"nav_down", "D-pad down"},      {"nav_left", "D-pad left"},
                                     {"nav_right", "D-pad right"},  {"nav_stick_up", "Stick up"},    {"nav_stick_down", "Stick down"},
                                     {"nav_stick_left", "Stick left"}, {"nav_stick_right", "Stick right"}};

const QList<QPair<QString, QString>>& buttonActions() {
    static const QList<QPair<QString, QString>> a{
        {QStringLiteral("none"), QStringLiteral("Nothing")},          {QStringLiteral("reload"), QStringLiteral("Reload")},
        {QStringLiteral("grenade"), QStringLiteral("Grenade")},       {QStringLiteral("aux1"), QStringLiteral("Extra button 1")},
        {QStringLiteral("aux2"), QStringLiteral("Extra button 2")},   {QStringLiteral("recenter"), QStringLiteral("Recenter aim")},
        {QStringLiteral("key:5"), QStringLiteral("Coin (key 5)")},    {QStringLiteral("key:1"), QStringLiteral("Start player 1 (key 1)")},
        {QStringLiteral("key:2"), QStringLiteral("Start player 2 (key 2)")}, {QStringLiteral("key:3"), QStringLiteral("Start player 3 (key 3)")},
        {QStringLiteral("key:4"), QStringLiteral("Start player 4 (key 4)")}, {QStringLiteral("key:f2"), QStringLiteral("Test (F2)")},
        {QStringLiteral("key:f3"), QStringLiteral("Service (F3)")},   {QStringLiteral("key:esc"), QStringLiteral("Escape")},
        {QStringLiteral("key:enter"), QStringLiteral("Enter")},       {QStringLiteral("key:up"), QStringLiteral("Arrow up")},
        {QStringLiteral("key:down"), QStringLiteral("Arrow down")},   {QStringLiteral("key:left"), QStringLiteral("Arrow left")},
        {QStringLiteral("key:right"), QStringLiteral("Arrow right")}, {QStringLiteral("key:space"), QStringLiteral("Space")},
    };
    return a;
}

const QList<QPair<QString, QString>>& ledPresets() {
    // Namen zoals de engine ze kent (Nederlands), met Engelse labels.
    static const QList<QPair<QString, QString>> p{{QStringLiteral("cyaan"), QStringLiteral("Cyan")},   {QStringLiteral("magenta"), QStringLiteral("Magenta")},
                                                  {QStringLiteral("rood"), QStringLiteral("Red")},     {QStringLiteral("groen"), QStringLiteral("Green")},
                                                  {QStringLiteral("blauw"), QStringLiteral("Blue")},   {QStringLiteral("wit"), QStringLiteral("White")}};
    return p;
}

}  // namespace

// --- MoveConfigDialog -------------------------------------------------------------------------------------------------

MoveConfigDialog::MoveConfigDialog(rg::AppCore* core, int player, QWidget* parent) : QDialog(parent), core_(core), player_(player) {
    setWindowTitle(tr("PS Move: Player %1").arg(player));
    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    m_tabs->addTab(MakeGeneralTab(), tr("General"));
    m_tabs->addTab(MakeAimingTab(), tr("Aiming"));
    m_tabs->addTab(MakeRumbleTab(), tr("Rumble"));
    m_tabs->addTab(MakeButtonsTab(), tr("Buttons"));
    m_tabs->addTab(MakeCalibrationTab(), tr("Calibration"));
    layout->addWidget(m_tabs, 1);
    m_error = new QLabel;
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color:#c0392b"));
    layout->addWidget(m_error);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(bb);
    m_refresh = new QTimer(this);
    m_refresh->setInterval(2000);
    connect(m_refresh, &QTimer::timeout, this, &MoveConfigDialog::Load);
    m_refresh->start();
    core_->engine()->request(QStringLiteral("settings"), {}, [this](const rg::EngineReply& r) {
        if (r.ok) m_defaults = r.object().value(QStringLiteral("defaults")).toObject();
    });
    Load();
    resize(700, 640);
}

void MoveConfigDialog::ShowError(const QString& text) {
    m_error->setText(text);
    if (!text.isEmpty()) QTimer::singleShot(6000, m_error, [this, text] {
            if (m_error->text() == text) m_error->clear();
        });
}

void MoveConfigDialog::Send(const QString& cmd, QJsonObject fields, std::function<void(const rg::EngineReply&)> after) {
    if (cmd != QStringLiteral("set_clock") && cmd != QStringLiteral("calibrate_ui")) fields.insert(QStringLiteral("player"), player_);
    core_->engine()->request(cmd, fields, [this, after](const rg::EngineReply& r) {
        if (!r.ok) ShowError(r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
        if (after) after(r);
    });
}

void MoveConfigDialog::Load() {
    core_->engine()->request(QStringLiteral("status"), {{QStringLiteral("player"), player_}}, [this](const rg::EngineReply& r) {
        if (r.ok) Apply(r.object());
        else ShowError(r.errorMessage);
    });
}

QWidget* MoveConfigDialog::MakeGeneralTab() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(tr("Controller"));
    auto* form = new QFormLayout(box);
    auto* nameRow = new QHBoxLayout;
    m_name = new QLineEdit;
    m_name->setMaxLength(32);
    auto* save = new NonDefaultQPushButton(tr("Save Name"));
    nameRow->addWidget(m_name, 1);
    nameRow->addWidget(save);
    form->addRow(tr("Name:"), nameRow);
    m_info = new QLabel;
    m_battery = new QLabel;
    form->addRow(tr("Controller:"), m_info);
    form->addRow(tr("Battery:"), m_battery);
    v->addWidget(box);
    connect(save, &QPushButton::clicked, this, [this] {
        const QString n = m_name->text().trimmed();
        if (n.isEmpty() || m_key.isEmpty()) return;
        core_->engine()->request(QStringLiteral("set_controller"), {{QStringLiteral("controller"), m_key}, {QStringLiteral("name"), n}},
                                 [this](const rg::EngineReply& r) {
                                     if (!r.ok) ShowError(r.errorMessage);
                                     core_->engine()->refreshNow();
                                 });
    });

    auto* led = new QGroupBox(tr("LED color"));
    auto* ll = new QVBoxLayout(led);
    auto* grid = new QHBoxLayout;
    for (const auto& [id, label] : ledPresets()) {
        auto* b = new NonDefaultQPushButton(label);
        grid->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, id] { Send(QStringLiteral("set_led"), {{QStringLiteral("preset"), id}}, [this](auto&) { Load(); }); });
    }
    auto* custom = new NonDefaultQPushButton(tr("Custom..."));
    grid->addWidget(custom);
    ll->addLayout(grid);
    m_led = new QLabel;
    ll->addWidget(m_led);
    ll->addWidget(hint(tr("The camera finds the ball by its color. White and custom colors may not be tracked.")));
    connect(custom, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(Qt::cyan, this, tr("LED color"));
        if (!c.isValid()) return;
        Send(QStringLiteral("set_led"), {{QStringLiteral("r"), c.red()}, {QStringLiteral("g"), c.green()}, {QStringLiteral("b"), c.blue()}},
             [this](auto&) { Load(); });
    });
    v->addWidget(led);

    auto* actions = new QGroupBox(tr("Actions"));
    auto* al = new QHBoxLayout(actions);
    auto* blink = new NonDefaultQPushButton(tr("Blink and Vibrate"));
    auto* recenter = new NonDefaultQPushButton(tr("Recenter Aim"));
    al->addWidget(blink);
    al->addWidget(recenter);
    al->addStretch(1);
    connect(blink, &QPushButton::clicked, this, [this] { Send(QStringLiteral("blink"), {{QStringLiteral("ms"), 3000}}); });
    connect(recenter, &QPushButton::clicked, this, [this] { Send(QStringLiteral("recenter"), {}); });
    v->addWidget(actions);
    v->addStretch(1);
    return w;
}

QWidget* MoveConfigDialog::MakeAimingTab() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* drift = new QGroupBox(tr("Drift correction"));
    auto* dl = new QVBoxLayout(drift);
    auto* group = new QButtonGroup(this);
    const QList<QPair<QString, QString>> modes{{QStringLiteral("vqf_mag"), tr("Compass (recommended)")},
                                               {QStringLiteral("vqf"), tr("Without compass")},
                                               {QStringLiteral("mahony_mag"), tr("Compass (legacy method)")},
                                               {QStringLiteral("mahony"), tr("Gyro only")}};
    for (const auto& [id, label] : modes) {
        auto* r = new QRadioButton(label);
        group->addButton(r);
        m_fusion.insert(id, r);
        dl->addWidget(r);
        connect(r, &QRadioButton::toggled, this, [this, id](bool on) {
            if (on && !m_loading) Send(QStringLiteral("set_fusion"), {{QStringLiteral("mode"), id}});
        });
    }
    m_learning = new QCheckBox(tr("Learn from recentering (corrects slow drift each time you press PS)"));
    dl->addWidget(m_learning);
    connect(m_learning, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) Send(QStringLiteral("set_fusion"), {{QStringLiteral("recenter_learning"), on}});
    });
    v->addWidget(drift);

    auto* feel = new QGroupBox(tr("Feel"));
    auto* form = new QFormLayout(feel);
    m_predict = new LiveSlider(0, 100, 1, false, [](double x) { return QStringLiteral("%1 ms").arg(x, 0, 'f', 0); },
                               [this](double x, std::function<void()> done) {
                                   Send(QStringLiteral("set_aim"), {{QStringLiteral("predict_ms"), x}}, [done](auto&) { done(); });
                               });
    form->addRow(tr("Prediction:"), m_predict);
    m_filter = new QCheckBox(tr("Smooth the aim (1€ filter)"));
    connect(m_filter, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) Send(QStringLiteral("set_filter"), {{QStringLiteral("enabled"), on}});
    });
    form->addRow(QString(), m_filter);
    m_min_cutoff = new LiveSlider(0.05, 5, 0, true, [](double x) { return QString::number(x, 'f', 2); },
                                  [this](double x, std::function<void()> done) {
                                      Send(QStringLiteral("set_filter"), {{QStringLiteral("min_cutoff_hz"), x}}, [done](auto&) { done(); });
                                  });
    form->addRow(tr("Steadiness when still:"), m_min_cutoff);
    m_beta = new LiveSlider(0, 100, 1, false, [](double x) { return QString::number(x, 'f', 0); },
                            [this](double x, std::function<void()> done) {
                                Send(QStringLiteral("set_filter"), {{QStringLiteral("beta"), x}}, [done](auto&) { done(); });
                            });
    form->addRow(tr("Responsiveness:"), m_beta);
    auto* edges = new QHBoxLayout;
    m_h_edge = new QSpinBox;
    m_v_edge = new QSpinBox;
    for (QSpinBox* s : {m_h_edge, m_v_edge}) {
        s->setRange(1, 89);
        s->setSuffix(tr(" degrees"));
        s->setKeyboardTracking(false);
    }
    edges->addWidget(new QLabel(tr("left/right")));
    edges->addWidget(m_h_edge);
    edges->addWidget(new QLabel(tr("up/down")));
    edges->addWidget(m_v_edge);
    edges->addStretch(1);
    form->addRow(tr("Turn to screen edge:"), edges);
    connect(m_h_edge, &QSpinBox::valueChanged, this, [this](int x) {
        if (!m_loading) Send(QStringLiteral("set_aim"), {{QStringLiteral("h_edge_deg"), x}});
    });
    connect(m_v_edge, &QSpinBox::valueChanged, this, [this](int x) {
        if (!m_loading) Send(QStringLiteral("set_aim"), {{QStringLiteral("v_edge_deg"), x}});
    });
    m_clock = new QComboBox;
    m_clock->addItem(tr("Off (position per report)"), 0);
    m_clock->addItem(tr("125 Hz"), 125);
    m_clock->addItem(tr("250 Hz (default)"), 250);
    m_clock->addItem(tr("500 Hz"), 500);
    connect(m_clock, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_loading) Send(QStringLiteral("set_clock"), {{QStringLiteral("clock_hz"), m_clock->currentData().toInt()}});
    });
    form->addRow(tr("Output clock (all players):"), m_clock);
    v->addWidget(feel);

    auto* cal = new QGroupBox(tr("Calibration screen"));
    auto* cl = new QHBoxLayout(cal);
    auto* open = new NonDefaultQPushButton(tr("Open Calibration Screen on TV..."));
    auto* defaults = new NonDefaultQPushButton(tr("Restore Defaults"));
    cl->addWidget(open);
    cl->addStretch(1);
    cl->addWidget(defaults);
    connect(open, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Calibration"), tr("Open the calibration screen full screen on the TV?")) != QMessageBox::Yes) return;
        Send(QStringLiteral("calibrate_ui"), {{QStringLiteral("action"), QStringLiteral("open")}});
    });
    connect(defaults, &QPushButton::clicked, this, [this] {
        const QJsonObject f = m_defaults.value(QStringLiteral("filter")).toObject();
        const QJsonObject a = m_defaults.value(QStringLiteral("aim")).toObject();
        if (f.isEmpty() || a.isEmpty()) return;
        Send(QStringLiteral("set_filter"), {{QStringLiteral("enabled"), f.value(QStringLiteral("enabled"))},
                                            {QStringLiteral("min_cutoff_hz"), f.value(QStringLiteral("min_cutoff_hz"))},
                                            {QStringLiteral("beta"), f.value(QStringLiteral("beta"))},
                                            {QStringLiteral("d_cutoff_hz"), f.value(QStringLiteral("d_cutoff_hz"))}});
        Send(QStringLiteral("set_aim"), {{QStringLiteral("h_edge_deg"), a.value(QStringLiteral("h_edge_deg"))},
                                         {QStringLiteral("v_edge_deg"), a.value(QStringLiteral("v_edge_deg"))},
                                         {QStringLiteral("predict_ms"), a.value(QStringLiteral("predict_ms"))}},
             [this](auto&) { Load(); });
    });
    v->addWidget(cal);
    v->addStretch(1);
    return w;
}

QWidget* MoveConfigDialog::MakeRumbleTab() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(tr("Rumble when shooting"));
    auto* form = new QFormLayout(box);
    m_rumble_on = new QCheckBox(tr("Enabled"));
    connect(m_rumble_on, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) Send(QStringLiteral("set_rumble"), {{QStringLiteral("enabled"), on}});
    });
    form->addRow(QString(), m_rumble_on);
    auto rumbleSender = [this](const char* field) {
        return [this, field](double x, std::function<void()> done) {
            Send(QStringLiteral("set_rumble"), {{QLatin1String(field), static_cast<int>(std::lround(x))}}, [done](auto&) { done(); });
        };
    };
    m_strength = new LiveSlider(0, 255, 1, false, [](double x) { return QStringLiteral("%1%").arg(std::lround(x * 100 / 255)); },
                                rumbleSender("strength"));
    m_pulse = new LiveSlider(10, 500, 5, false, [](double x) { return QStringLiteral("%1 ms").arg(std::lround(x)); }, rumbleSender("pulse_ms"));
    m_repeat = new LiveSlider(0, 30, 1, false,
                              [](double x) { return x < 0.5 ? QObject::tr("single pulse") : QStringLiteral("%1 Hz").arg(std::lround(x)); },
                              rumbleSender("repeat_hz"));
    m_pump = new LiveSlider(0, 255, 1, false, [](double x) { return QStringLiteral("%1%").arg(std::lround(x * 100 / 255)); },
                            rumbleSender("pump_strength"));
    form->addRow(tr("Strength:"), m_strength);
    form->addRow(tr("Pulse length:"), m_pulse);
    form->addRow(tr("Repeat while held:"), m_repeat);
    form->addRow(tr("Pump strength:"), m_pump);
    v->addWidget(box);
    auto* h = new QHBoxLayout;
    auto* test = new NonDefaultQPushButton(tr("Test"));
    h->addWidget(test);
    h->addStretch(1);
    v->addLayout(h);
    connect(test, &QPushButton::clicked, this, [this] { Send(QStringLiteral("rumble_test"), {{QStringLiteral("ms"), 300}}); });
    v->addStretch(1);
    return w;
}

QWidget* MoveConfigDialog::MakeButtonsTab() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto addGroup = [this, v](const QString& title, const ButtonDef* defs, size_t n, bool fixed) {
        auto* box = new QGroupBox(title);
        auto* grid = new QGridLayout(box);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        int i = 0;
        if (fixed) {
            grid->addWidget(new QLabel(tr("PS")), 0, 0);
            grid->addWidget(new QLabel(tr("<i>Recenter aim (fixed)</i>")), 0, 1);
            grid->addWidget(new QLabel(tr("T (trigger)")), 0, 2);
            grid->addWidget(new QLabel(tr("<i>Shoot (fixed)</i>")), 0, 3);
            i = 2;
        }
        for (size_t k = 0; k < n; ++k, ++i) {
            auto* combo = new QComboBox;
            for (const auto& [id, label] : buttonActions()) combo->addItem(label, id);
            m_buttons.insert(QLatin1String(defs[k].key), combo);
            grid->addWidget(new QLabel(tr(defs[k].label)), i / 2, (i % 2) * 2);
            grid->addWidget(combo, i / 2, (i % 2) * 2 + 1);
            connect(combo, &QComboBox::currentIndexChanged, this, [this, combo] {
                if (m_loading) return;
                combo->setStyleSheet(QStringLiteral("QComboBox { border: 2px solid #e67e22; }"));
                m_buttons_state->setText(tr("Unsaved changes."));
            });
        }
        v->addWidget(box);
    };
    addGroup(tr("PS Move"), kMoveButtons, std::size(kMoveButtons), true);
    addGroup(tr("Sharp Shooter"), kSharpShooter, std::size(kSharpShooter), false);
    addGroup(tr("Navigation Controller"), kNavigation, std::size(kNavigation), false);
    auto* h = new QHBoxLayout;
    m_buttons_state = new QLabel;
    auto* defaults = new NonDefaultQPushButton(tr("Default Layout"));
    auto* save = new NonDefaultQPushButton(tr("Save"));
    h->addWidget(m_buttons_state, 1);
    h->addWidget(defaults);
    h->addWidget(save);
    v->addLayout(h);
    connect(defaults, &QPushButton::clicked, this, [this] {
        const QJsonObject map = sub(m_defaults, {"gun_buttons", "map"});
        for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
            const int idx = (*it)->findData(map.value(it.key()).toString(QStringLiteral("none")));
            if (idx >= 0 && idx != (*it)->currentIndex()) (*it)->setCurrentIndex(idx);
        }
    });
    connect(save, &QPushButton::clicked, this, [this] {
        QJsonObject map;
        const QJsonObject cur = sub(m_status, {"gun_buttons", "map"});
        for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
            const QString val = (*it)->currentData().toString();
            if (val != cur.value(it.key()).toString()) map.insert(it.key(), val);
        }
        if (map.isEmpty()) {
            m_buttons_state->setText(tr("Nothing changed."));
            return;
        }
        Send(QStringLiteral("set_gun_buttons"), {{QStringLiteral("map"), map}}, [this](const rg::EngineReply& r) {
            m_buttons_state->setText(r.ok ? tr("Saved.") : tr("Not saved."));
            if (r.ok)
                for (QComboBox* c : m_buttons) c->setStyleSheet(QString());
            Load();
        });
    });
    v->addStretch(1);
    return rg::ui::GetWrappedWidget(w);
}

QWidget* MoveConfigDialog::MakeCalibrationTab() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* mag = new QGroupBox(tr("Magnetometer (compass)"));
    auto* ml = new QVBoxLayout(mag);
    m_mag = new QLabel;
    m_mag->setWordWrap(true);
    ml->addWidget(m_mag);
    auto* mh = new QHBoxLayout;
    auto* start = new NonDefaultQPushButton(tr("Start Calibration"));
    auto* stop = new NonDefaultQPushButton(tr("Finish and Save"));
    auto* abort = new NonDefaultQPushButton(tr("Abort"));
    mh->addWidget(start);
    mh->addWidget(stop);
    mh->addWidget(abort);
    mh->addStretch(1);
    ml->addLayout(mh);
    ml->addWidget(hint(tr("Start, then slowly turn the Move in all directions for about a minute, then press Finish and Save.")));
    connect(start, &QPushButton::clicked, this, [this] { Send(QStringLiteral("calib_mag"), {{QStringLiteral("action"), QStringLiteral("start")}}, [this](auto&) { Load(); }); });
    connect(stop, &QPushButton::clicked, this, [this] { Send(QStringLiteral("calib_mag"), {{QStringLiteral("action"), QStringLiteral("stop")}}, [this](auto&) { Load(); }); });
    connect(abort, &QPushButton::clicked, this, [this] { Send(QStringLiteral("calib_mag"), {{QStringLiteral("action"), QStringLiteral("abort")}}, [this](auto&) { Load(); }); });
    v->addWidget(mag);

    auto* gyro = new QGroupBox(tr("Gyroscope"));
    auto* gl = new QVBoxLayout(gyro);
    m_bias = new QLabel;
    m_bias->setWordWrap(true);
    gl->addWidget(m_bias);
    m_trim = new QLabel;
    m_trim->setWordWrap(true);
    gl->addWidget(m_trim);
    auto* reset = new NonDefaultQPushButton(tr("Reset Learned Drift Trim"));
    auto* gh = new QHBoxLayout;
    gh->addWidget(reset);
    gh->addStretch(1);
    gl->addLayout(gh);
    connect(reset, &QPushButton::clicked, this, [this] { Send(QStringLiteral("clear_trim"), {}, [this](auto&) { Load(); }); });
    gl->addWidget(hint(tr("Put the Move down on a table for a few seconds after switching it on: the gyro bias is then measured.")));
    v->addWidget(gyro);
    v->addStretch(1);
    return w;
}

void MoveConfigDialog::Apply(const QJsonObject& st) {
    m_status = st;
    m_loading = true;
    // General
    const QJsonObject asg = sub(st, {"assignment", "controller"});
    m_key = asg.value(QStringLiteral("key")).toString();
    if (!m_name->hasFocus()) m_name->setText(asg.value(QStringLiteral("name")).toString());
    const QJsonObject ctl = st.value(QStringLiteral("controller")).toObject();
    if (m_key.isEmpty()) m_info->setText(tr("No PS Move assigned to player %1").arg(player_));
    else
        m_info->setText(tr("%1, %2%3")
                            .arg(asg.value(QStringLiteral("address")).toString(),
                                 ctl.value(QStringLiteral("connected")).toBool() ? tr("connected") : tr("not connected"),
                                 ctl.value(QStringLiteral("model")).toString().isEmpty() ? QString()
                                                                                         : QStringLiteral(" (%1)").arg(ctl.value(QStringLiteral("model")).toString())));
    // De controllerlijst kent ook batterij en LED als de pijplijn (nog) geen rapporten heeft.
    QJsonObject listed;
    for (const auto& v : core_->engine()->controllers().value(QStringLiteral("controllers")).toArray())
        if (v.toObject().value(QStringLiteral("key")).toString() == m_key) listed = v.toObject();
    QJsonObject bat = ctl.value(QStringLiteral("battery")).toObject();
    if (bat.value(QStringLiteral("code")).toString(QStringLiteral("unknown")) == QStringLiteral("unknown") &&
        listed.value(QStringLiteral("battery")).toObject().value(QStringLiteral("known")).toBool())
        bat = listed.value(QStringLiteral("battery")).toObject();
    QString battery = bat.value(QStringLiteral("code")).toString();
    if (!bat.value(QStringLiteral("percent")).isNull() && bat.contains(QStringLiteral("percent")))
        battery = QStringLiteral("%1%").arg(bat.value(QStringLiteral("percent")).toInt());
    if (battery == QStringLiteral("unknown")) battery = tr("unknown (the Move is off)");
    else if (battery == QStringLiteral("max")) battery = tr("full");
    else if (battery == QStringLiteral("min")) battery = tr("low (less than 20%)");
    if (bat.value(QStringLiteral("charging")).toBool()) battery += tr(", charging");
    m_battery->setText(battery);
    QJsonArray rgb = sub(st, {"controller", "led"}).value(QStringLiteral("rgb")).toArray();
    if (!sub(st, {"controller", "led"}).value(QStringLiteral("enabled")).toBool() &&
        listed.value(QStringLiteral("led")).toObject().value(QStringLiteral("rgb")).toArray().size() == 3)
        rgb = listed.value(QStringLiteral("led")).toObject().value(QStringLiteral("rgb")).toArray();
    if (rgb.size() == 3)
        m_led->setText(tr("Current: <span style='background:rgb(%1,%2,%3)'>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</span> (%1, %2, %3)")
                           .arg(rgb[0].toInt())
                           .arg(rgb[1].toInt())
                           .arg(rgb[2].toInt()));
    else m_led->setText(tr("Current: not set"));
    // Aiming
    const QJsonObject cfg = st.value(QStringLiteral("config")).toObject();
    const QString mode = sub(cfg, {"fusion"}).value(QStringLiteral("mode")).toString();
    if (m_fusion.contains(mode)) m_fusion.value(mode)->setChecked(true);
    m_learning->setChecked(sub(st, {"yaw_trim"}).value(QStringLiteral("learning")).toBool());
    const QJsonObject filter = cfg.value(QStringLiteral("filter")).toObject();
    m_filter->setChecked(filter.value(QStringLiteral("enabled")).toBool());
    m_min_cutoff->setValue(filter.value(QStringLiteral("min_cutoff_hz")).toDouble(0.3));
    m_beta->setValue(filter.value(QStringLiteral("beta")).toDouble(10));
    const QJsonObject aim = cfg.value(QStringLiteral("aim")).toObject();
    m_predict->setValue(aim.value(QStringLiteral("predict_ms")).toDouble(40));
    if (!m_h_edge->hasFocus()) m_h_edge->setValue(static_cast<int>(std::lround(aim.value(QStringLiteral("h_edge_deg")).toDouble(20))));
    if (!m_v_edge->hasFocus()) m_v_edge->setValue(static_cast<int>(std::lround(aim.value(QStringLiteral("v_edge_deg")).toDouble(11.25))));
    const int hz = sub(st, {"output", "live", "clock"}).value(QStringLiteral("hz")).toInt(250);
    const bool clockOn = sub(st, {"output", "live", "clock"}).value(QStringLiteral("enabled")).toBool(true);
    int ci = m_clock->findData(clockOn ? hz : 0);
    if (ci < 0) {
        m_clock->addItem(tr("%1 Hz").arg(hz), hz);
        ci = m_clock->count() - 1;
    }
    m_clock->setCurrentIndex(ci);
    // Rumble
    const QJsonObject rum = sub(st, {"controller", "rumble"});
    m_rumble_on->setChecked(rum.value(QStringLiteral("enabled")).toBool());
    m_strength->setValue(rum.value(QStringLiteral("strength")).toDouble());
    m_pulse->setValue(rum.value(QStringLiteral("pulse_ms")).toDouble());
    m_repeat->setValue(rum.value(QStringLiteral("repeat_hz")).toDouble());
    m_pump->setValue(rum.value(QStringLiteral("pump_strength")).toDouble());
    // Buttons (niet overschrijven zolang er niet-bewaarde wijzigingen zijn)
    if (!m_buttons_state->text().startsWith(tr("Unsaved"))) {
        const QJsonObject map = sub(st, {"gun_buttons", "map"});
        for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
            const QString val = map.value(it.key()).toString(QStringLiteral("none"));
            int idx = (*it)->findData(val);
            if (idx < 0) {
                (*it)->addItem(val, val);
                idx = (*it)->count() - 1;
            }
            (*it)->setCurrentIndex(idx);
        }
    }
    // Calibration
    const QJsonObject mag = st.value(QStringLiteral("magnetometer")).toObject();
    const bool magActive = sub(mag, {"session", "mag_calibration"}).value(QStringLiteral("active")).toBool();
    QString magText = mag.value(QStringLiteral("calibrated")).toBool() ? tr("Calibrated.") : tr("Not calibrated.");
    if (!mag.value(QStringLiteral("present")).toBool()) magText = tr("No magnetometer data (is the Move connected?).");
    if (magActive) magText = tr("Calibration running: turn the Move slowly in all directions.");
    magText += tr(" Used for drift correction: %1.").arg(mag.value(QStringLiteral("used_in_fusion")).toBool() ? tr("yes") : tr("no"));
    m_mag->setText(magText);
    const QJsonObject gb = st.value(QStringLiteral("gyro_bias")).toObject();
    const QString state = gb.value(QStringLiteral("state")).toString();
    QString biasText;
    if (state == QStringLiteral("estimated")) biasText = tr("Gyro bias: measured");
    else if (state == QStringLiteral("estimating")) biasText = tr("Gyro bias: measuring...");
    else if (state == QStringLiteral("restored")) biasText = tr("Gyro bias: restored from the last session (not yet confirmed)");
    else biasText = tr("Gyro bias: not measured yet");
    const QJsonArray b = gb.value(QStringLiteral("bias_deg_s")).toArray();
    if (b.size() == 3)
        biasText += tr(" (%1, %2, %3 degrees/s)").arg(b[0].toDouble(), 0, 'f', 2).arg(b[1].toDouble(), 0, 'f', 2).arg(b[2].toDouble(), 0, 'f', 2);
    biasText += tr(". Time still: %1 s.").arg(gb.value(QStringLiteral("still_s")).toDouble(), 0, 'f', 0);
    m_bias->setText(biasText);
    const QJsonObject yt = st.value(QStringLiteral("yaw_trim")).toObject();
    m_trim->setText(tr("Learned drift trim: %1 degrees/s from %2 recenter presses (%3 used).")
                        .arg(yt.value(QStringLiteral("trim_deg_s")).toDouble(), 0, 'f', 3)
                        .arg(yt.value(QStringLiteral("presses")).toInt())
                        .arg(yt.value(QStringLiteral("learned")).toInt()));
    m_loading = false;
}

}  // namespace rg::ui
