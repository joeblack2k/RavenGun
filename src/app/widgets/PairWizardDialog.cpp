// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/PairWizardDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "widgets/QtUtils.h"

namespace rg::ui {

QString PairWizardDialog::StepText(const QString& s, const QString& device) {
    const QString d = device.isEmpty() ? tr("the controller") : device;
    if (s == QStringLiteral("wait_usb"))
        return tr("Connect the PS Move or Navigation Controller to this PC with a USB cable. Use a cable that carries data, not a "
                  "charge-only cable.");
    if (s == QStringLiteral("already_paired"))
        return tr("This controller is already paired with this PC. Pair it again? Otherwise unplug the cable and press the PS "
                  "button.");
    if (s == QStringLiteral("set_master")) return tr("Pairing %1 with this PC. Keep the cable connected.").arg(d);
    if (s == QStringLiteral("need_helper"))
        return tr("The last step needs a small helper with administrator rights, installed once. Ask someone with the "
                  "administrator password to run this command in a terminal, then press Try Again:");
    if (s == QStringLiteral("register"))
        return tr("Setting up Bluetooth. Bluetooth restarts briefly; other controllers reconnect by themselves.");
    if (s == QStringLiteral("wait_unplug")) return tr("Unplug the USB cable.");
    if (s == QStringLiteral("wait_bluetooth")) return tr("Now press the PS button on %1.").arg(d);
    if (s == QStringLiteral("done")) return tr("Done: %1 is paired and connected.").arg(d);
    if (s == QStringLiteral("failed")) return tr("Pairing failed.");
    if (s == QStringLiteral("cancelled")) return tr("Pairing was cancelled.");
    return tr("Press Start to pair a new controller.");
}

int PairWizardDialog::StepNumber(const QString& s) {
    if (s == QStringLiteral("wait_usb") || s == QStringLiteral("already_paired")) return 1;
    if (s == QStringLiteral("set_master") || s == QStringLiteral("need_helper") || s == QStringLiteral("register")) return 2;
    if (s == QStringLiteral("wait_unplug")) return 3;
    if (s == QStringLiteral("wait_bluetooth") || s == QStringLiteral("done")) return 4;
    return 0;
}

PairWizardDialog::PairWizardDialog(rg::AppCore* core, QWidget* parent) : QDialog(parent), core_(core) {
    setWindowTitle(tr("Pair New Controller"));
    auto* v = new QVBoxLayout(this);
    m_steps = new QLabel;
    m_steps->setTextFormat(Qt::RichText);
    v->addWidget(m_steps);
    m_text = new QLabel;
    m_text->setWordWrap(true);
    QFont f = m_text->font();
    f.setPointSizeF(f.pointSizeF() * 1.3);
    m_text->setFont(f);
    m_text->setMinimumHeight(80);
    v->addWidget(m_text);
    m_command = new QLineEdit;
    m_command->setReadOnly(true);
    m_command->hide();
    v->addWidget(m_command);
    m_detail = new QLabel;
    m_detail->setWordWrap(true);
    m_detail->setStyleSheet(QStringLiteral("color: palette(mid)"));
    v->addWidget(m_detail);
    v->addStretch(1);
    auto* h = new QHBoxLayout;
    m_start = new NonDefaultQPushButton(tr("Start"));
    m_confirm = new NonDefaultQPushButton(tr("Pair Again"));
    m_cancel = new NonDefaultQPushButton(tr("Cancel Pairing"));
    auto* close = new NonDefaultQPushButton(tr("Close"));
    h->addWidget(m_start);
    h->addWidget(m_confirm);
    h->addWidget(m_cancel);
    h->addStretch(1);
    h->addWidget(close);
    v->addLayout(h);
    connect(m_start, &QPushButton::clicked, this, &PairWizardDialog::Start);
    connect(m_confirm, &QPushButton::clicked, this, [this] {
        core_->engine()->request(QStringLiteral("pair"), {{QStringLiteral("action"), QStringLiteral("confirm")}},
                                 [this](const rg::EngineReply& r) {
                                     if (r.ok) Show(r.object());
                                 });
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        core_->engine()->request(QStringLiteral("pair"), {{QStringLiteral("action"), QStringLiteral("cancel")}},
                                 [this](const rg::EngineReply& r) {
                                     if (r.ok) Show(r.object());
                                 });
    });
    connect(close, &QPushButton::clicked, this, &PairWizardDialog::reject);
    m_poll = new QTimer(this);
    m_poll->setInterval(700);
    connect(m_poll, &QTimer::timeout, this, &PairWizardDialog::Poll);
    m_poll->start();
    Show(core_->engine()->controllers().value(QStringLiteral("pairing")).toObject());
    Poll();
    resize(620, 360);
}

void PairWizardDialog::Start() {
    core_->engine()->request(QStringLiteral("pair"), {{QStringLiteral("action"), QStringLiteral("start")}}, [this](const rg::EngineReply& r) {
        if (r.ok) Show(r.object());
        else m_detail->setText(tr("Could not start pairing: %1").arg(r.errorMessage));
    });
}

void PairWizardDialog::Poll() {
    if (!core_->engine()->isConnected()) {
        m_text->setText(tr("The engine is not running. Start it first (Engine > Start Engine)."));
        m_start->setEnabled(false);
        return;
    }
    core_->engine()->request(QStringLiteral("pair"), {{QStringLiteral("action"), QStringLiteral("status")}}, [this](const rg::EngineReply& r) {
        if (r.ok) Show(r.object());
    });
}

void PairWizardDialog::Show(const QJsonObject& in) {
    // `pair` antwoordt met de toestand van de wizard, `controllers` heeft hem onder "pairing".
    const QJsonObject p = in.contains(QStringLiteral("pairing")) ? in.value(QStringLiteral("pairing")).toObject() : in;
    m_state = p.value(QStringLiteral("state")).toString(QStringLiteral("idle"));
    const QJsonObject target = p.value(QStringLiteral("target")).toObject();
    QString device;
    if (target.value(QStringLiteral("type")).toString() == QStringLiteral("move")) device = tr("the PS Move");
    else if (target.value(QStringLiteral("type")).toString() == QStringLiteral("navigation")) device = tr("the Navigation Controller");
    const int step = StepNumber(m_state);
    const QStringList names{tr("Cable in"), tr("Pair"), tr("Cable out"), tr("PS button")};
    QStringList parts;
    for (int i = 0; i < names.size(); ++i) {
        const QString n = QStringLiteral("%1. %2").arg(i + 1).arg(names[i]);
        parts << (i + 1 == step ? QStringLiteral("<b>%1</b>").arg(n) : (i + 1 < step ? QStringLiteral("<span style='color:#2e9e44'>%1</span>").arg(n) : n));
    }
    m_steps->setText(parts.join(QStringLiteral(" &rarr; ")));
    m_text->setText(StepText(m_state, device));
    const bool helper = m_state == QStringLiteral("need_helper");
    m_command->setVisible(helper);
    if (helper) m_command->setText(p.value(QStringLiteral("helper")).toObject().value(QStringLiteral("install_command")).toString());
    const QString msg = p.value(QStringLiteral("message")).toString();
    m_detail->setText(m_state == QStringLiteral("failed") && !msg.isEmpty() ? tr("Details: %1").arg(msg) : QString());
    const bool active = p.value(QStringLiteral("active")).toBool();
    m_start->setEnabled(!active);
    m_start->setText(m_state == QStringLiteral("done") || m_state == QStringLiteral("failed") || m_state == QStringLiteral("cancelled")
                         ? tr("Pair Another")
                         : tr("Start"));
    m_confirm->setVisible(m_state == QStringLiteral("already_paired") || helper);
    m_confirm->setText(helper ? tr("Try Again") : tr("Pair Again"));
    m_cancel->setEnabled(active);
}

void PairWizardDialog::reject() {
    if (StepNumber(m_state) > 0 && m_state != QStringLiteral("done"))
        core_->engine()->request(QStringLiteral("pair"), {{QStringLiteral("action"), QStringLiteral("cancel")}}, {});
    QDialog::reject();
}

}  // namespace rg::ui
