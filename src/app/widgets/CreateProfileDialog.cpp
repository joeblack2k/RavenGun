// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/CreateProfileDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/AppPaths.h"
#include "core/FileSafety.h"
#include "core/SystemControl.h"
#include "widgets/QtUtils.h"

namespace rg::ui {

QStringList CreateProfileDialog::BuildArguments(const QString& profile, const QString& exe, bool view, bool fullscreen, bool gamescope,
                                                bool force) {
    QStringList a{profile, exe};
    if (view) a << QStringLiteral("--view");
    if (fullscreen) a << QStringLiteral("--fullscreen");
    if (gamescope) a << QStringLiteral("--gamescope");
    if (force) a << QStringLiteral("--force");
    return a;
}

CreateProfileDialog::CreateProfileDialog(rg::AppCore* core, const rg::GameEntry& game, QWidget* parent)
    : QDialog(parent), core_(core), game_(game) {
    setWindowTitle(tr("Set Up Game"));
    auto* v = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->addRow(tr("Game folder:"), new QLabel(game_.folder));
    m_profile = new QComboBox;
    const QString tp = core_->library()->tpDir();
    QStringList profiles;
    const QString chosen = core_->library()->override_(game_.id).templateProfile.isEmpty() ? game_.templateProfile
                                                                                            : core_->library()->override_(game_.id).templateProfile;
    if (!chosen.isEmpty()) profiles << chosen;
    for (const QString& c : game_.candidates)
        if (!profiles.contains(c)) profiles << c;
    for (const QString& p : profiles) {
        const rg::TemplateInfo t = rg::LibraryScanner::templateInfo(tp, p);
        m_profile->addItem(QStringLiteral("%1 (%2)").arg(t.name, p), p);
    }
    form->addRow(tr("Game profile:"), m_profile);
    auto* exeRow = new QHBoxLayout;
    m_exe = new QLineEdit(game_.exePath);
    auto* browse = new NonDefaultQPushButton(QStringLiteral("..."));
    exeRow->addWidget(m_exe, 1);
    exeRow->addWidget(browse);
    form->addRow(tr("Executable:"), exeRow);
    v->addLayout(form);

    const bool onNas = rg::isOnNetwork(game_.folder);
    m_view = new QCheckBox(tr("Use a local view of the game folder (links only; recommended for games on the NAS)"));
    m_view->setChecked(onNas);
    m_fullscreen = new QCheckBox(tr("Full screen at 1920x1080 (where the profile supports it)"));
    m_fullscreen->setChecked(true);
    m_gamescope = new QCheckBox(tr("Run in gamescope (for games that only run windowed)"));
    m_force = new QCheckBox(tr("Replace the existing profile (a backup copy is kept)"));
    m_exists = new QLabel;
    m_exists->setWordWrap(true);
    for (QWidget* w : {static_cast<QWidget*>(m_view), static_cast<QWidget*>(m_fullscreen), static_cast<QWidget*>(m_gamescope),
                       static_cast<QWidget*>(m_force), static_cast<QWidget*>(m_exists)})
        v->addWidget(w);
    auto* note = new QLabel(tr("RavenGun writes the launch profile for this game, with the RavenGun lightgun and keyboard "
                               "bindings, to the runtime folder on this PC. Nothing on the NAS is changed."));
    note->setWordWrap(true);
    v->addWidget(note);
    if (QDir::cleanPath(tp) != QDir::cleanPath(rg::AppPaths::defaultTpDir())) {
        auto* warn = new QLabel(tr("Note: the setup script always uses the default runtime folder (%1), not %2.").arg(rg::AppPaths::defaultTpDir(), tp));
        warn->setWordWrap(true);
        warn->setStyleSheet(QStringLiteral("color:#b06a00"));
        v->addWidget(warn);
    }
    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setPlaceholderText(tr("Output of the setup script appears here."));
    v->addWidget(m_output, 1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    m_create = bb->addButton(tr("Set Up"), QDialogButtonBox::ActionRole);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, &CreateProfileDialog::Run);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Select the Game Executable"), game_.folder);
        if (!f.isEmpty()) m_exe->setText(f);
    });
    connect(m_profile, &QComboBox::currentIndexChanged, this, &CreateProfileDialog::UpdateState);
    connect(m_exe, &QLineEdit::textChanged, this, &CreateProfileDialog::UpdateState);
    UpdateState();
    resize(720, 520);
}

void CreateProfileDialog::UpdateState() {
    const QString p = m_profile->currentData().toString();
    const bool exists = !p.isEmpty() && QFileInfo::exists(rg::AppPaths::defaultTpDir() + QStringLiteral("/UserProfiles/") + p + QStringLiteral(".xml"));
    m_force->setVisible(exists);
    m_exists->setText(exists ? tr("A launch profile \"%1\" already exists.").arg(p) : QString());
    const bool scriptOk = QFileInfo::exists(rg::AppPaths::repoRoot() + QStringLiteral("/scripts/tp-add-game.py"));
    m_create->setEnabled(!p.isEmpty() && QFileInfo(m_exe->text()).isFile() && scriptOk);
    if (!scriptOk) m_exists->setText(tr("scripts/tp-add-game.py was not found (RavenGun folder: %1).").arg(rg::AppPaths::repoRoot()));
}

void CreateProfileDialog::Run() {
    const QString script = rg::AppPaths::repoRoot() + QStringLiteral("/scripts/tp-add-game.py");
    const QStringList args = QStringList{QStringLiteral("-I"), script} +
                             BuildArguments(m_profile->currentData().toString(), m_exe->text(), m_view->isChecked(), m_fullscreen->isChecked(),
                                            m_gamescope->isChecked(), m_force->isVisible() && m_force->isChecked());
    m_output->setPlainText(QStringLiteral("$ python3 %1\n").arg(args.join(QLatin1Char(' '))));
    m_create->setEnabled(false);
    rg::SystemControl::run(QStringLiteral("python3"), args, this, [this](int code, const QString& out) {
        m_output->appendPlainText(out);
        m_output->appendPlainText(code == 0 ? tr("\nThe game is set up. The game list is refreshed.") : tr("\nFailed (exit code %1).").arg(code));
        UpdateState();
        if (code == 0) core_->library()->rescan();
    });
}

}  // namespace rg::ui
