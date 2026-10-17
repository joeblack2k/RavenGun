// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/ComponentsPage.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/AppCore.h"
#include "core/AppPaths.h"

namespace rg::ui {

namespace {
enum Col { ColName, ColVersion, ColSize, ColState, ColProgress, ColCount };
constexpr int kIdRole = Qt::UserRole;

QString stateText(const rg::ComponentStatus& s) {
    switch (s.state) {
        case rg::ComponentState::Installed: return QObject::tr("Installed");
        case rg::ComponentState::Outdated: return QObject::tr("Outdated");
        case rg::ComponentState::Unavailable: return QObject::tr("Not available");
        case rg::ComponentState::Missing: return QObject::tr("Missing");
    }
    return {};
}
}  // namespace

ComponentsPage::ComponentsPage(rg::AppCore* core, QWidget* parent) : QWizardPage(parent), core_(core) {
    setTitle(tr("Components"));
    setSubTitle(tr("RavenGun needs a few programs and libraries. They are downloaded (and checked) or taken from your Steam and game folders; "
                   "nothing is installed system-wide."));
    auto* v = new QVBoxLayout(this);
    tree_ = new QTreeWidget;
    tree_->setColumnCount(ColCount);
    tree_->setHeaderLabels({tr("Component"), tr("Version"), tr("Download"), tr("State"), tr("Progress")});
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(ColProgress, QHeaderView::Interactive);
    tree_->setColumnWidth(ColProgress, 150);
    v->addWidget(tree_, 1);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    v->addWidget(summary_);
    auto* h = new QHBoxLayout;
    install_all_ = new QPushButton(tr("Install All"));
    install_sel_ = new QPushButton(tr("Install Selected"));
    cancel_ = new QPushButton(tr("Cancel"));
    refresh_ = new QPushButton(tr("Check Again"));
    for (auto* b : {install_all_, install_sel_, cancel_, refresh_}) h->addWidget(b);
    h->addStretch(1);
    v->addLayout(h);

    connect(install_all_, &QPushButton::clicked, this, [this] {
        QStringList ids;
        for (const auto& c : all_) {
            const auto st = rg::Components::status(c, env_);
            if (st.state == rg::ComponentState::Missing || st.state == rg::ComponentState::Outdated) ids << c.id;
        }
        InstallIds(ids);
    });
    connect(install_sel_, &QPushButton::clicked, this, [this] {
        QStringList ids;
        for (QTreeWidgetItem* it : tree_->selectedItems())
            if (it->parent()) ids << it->data(0, kIdRole).toString();
        InstallIds(ids);
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { if (installer_) installer_->cancel(); });
    connect(refresh_, &QPushButton::clicked, this, &ComponentsPage::Refresh);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &ComponentsPage::UpdateButtons);
}

void ComponentsPage::initializePage() { Refresh(); }

QTreeWidgetItem* ComponentsPage::ItemFor(const QString& id) const {
    QTreeWidgetItemIterator it(tree_);
    while (*it) {
        if ((*it)->data(0, kIdRole).toString() == id) return *it;
        ++it;
    }
    return nullptr;
}

void ComponentsPage::Refresh() {
    if (installer_ && installer_->busy()) return;
    env_ = rg::ComponentEnv::standard();
    env_.sourceRoots.clear();
    if (core_) {
        for (const auto& f : core_->library()->data().folders) env_.sourceRoots << f.path;
        if (!core_->library()->data().tpDir.isEmpty()) env_.sourceRoots << core_->library()->data().tpDir;
    }
    QString err;
    all_ = rg::Components::catalogue(&err);
    all_ += rg::Components::modComponents();
    tree_->clear();
    QHash<QString, QTreeWidgetItem*> groups;
    int missing = 0, ready = 0, unavailable = 0;
    qint64 bytes = 0;
    for (const auto& c : all_) {
        QTreeWidgetItem*& g = groups[c.group];
        if (!g) {
            g = new QTreeWidgetItem(tree_, {c.group});
            QFont f = g->font(0);
            f.setBold(true);
            g->setFont(0, f);
            g->setFirstColumnSpanned(true);
        }
        const auto st = rg::Components::status(c, env_);
        auto* it = new QTreeWidgetItem(g, {c.name, c.version, rg::Components::formatSize(c.totalSize()), stateText(st), QString()});
        it->setData(0, kIdRole, c.id);
        it->setToolTip(0, c.description + (st.detail.isEmpty() ? QString() : QStringLiteral("\n") + st.detail));
        it->setToolTip(ColState, st.detail);
        auto* bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setTextVisible(true);
        bar->setFormat(st.state == rg::ComponentState::Installed ? tr("done") : QString());
        bar->setValue(st.state == rg::ComponentState::Installed ? 100 : 0);
        tree_->setItemWidget(it, ColProgress, bar);
        if (st.state == rg::ComponentState::Installed) ++ready;
        else if (st.state == rg::ComponentState::Unavailable) ++unavailable;
        else { ++missing; bytes += c.totalSize(); }
    }
    tree_->expandAll();
    QString t = err.isEmpty() ? QString() : tr("Catalogue problem: %1\n").arg(err);
    t += tr("%1 installed, %2 to install (about %3 to download), %4 not available on this machine.")
             .arg(ready).arg(missing).arg(rg::Components::formatSize(bytes)).arg(unavailable);
    summary_->setText(t);
    UpdateButtons();
}

void ComponentsPage::UpdateButtons() {
    const bool busy = installer_ && installer_->busy();
    install_all_->setEnabled(!busy);
    install_sel_->setEnabled(!busy && !tree_->selectedItems().isEmpty());
    cancel_->setEnabled(busy);
    refresh_->setEnabled(!busy);
}

void ComponentsPage::InstallIds(const QStringList& ids) {
    if (ids.isEmpty() || (installer_ && installer_->busy())) return;
    if (!installer_) {
        installer_ = new rg::ComponentInstaller(env_, this);
        connect(installer_, &rg::ComponentInstaller::started, this, [this](const QString& id) {
            if (auto* it = ItemFor(id)) it->setText(ColState, tr("Working..."));
        });
        connect(installer_, &rg::ComponentInstaller::progress, this, [this](const QString& id, const QString& phase, qint64 done, qint64 total) {
            auto* it = ItemFor(id);
            if (!it) return;
            auto* bar = qobject_cast<QProgressBar*>(tree_->itemWidget(it, ColProgress));
            if (!bar) return;
            if (total > 0) { bar->setRange(0, 1000); bar->setValue(static_cast<int>(done * 1000 / total)); }
            else bar->setRange(0, 0);
            bar->setFormat(phase);
            it->setText(ColState, phase);
        });
        connect(installer_, &rg::ComponentInstaller::finished, this, [this](const QString& id, bool ok, const QString& msg) {
            auto* it = ItemFor(id);
            if (!it) return;
            auto* bar = qobject_cast<QProgressBar*>(tree_->itemWidget(it, ColProgress));
            if (bar) { bar->setRange(0, 100); bar->setValue(ok ? 100 : 0); bar->setFormat(ok ? tr("done") : tr("failed")); }
            it->setText(ColState, ok ? tr("Installed") : tr("Failed"));
            it->setToolTip(ColState, msg);
            if (!ok) summary_->setText(tr("%1: %2").arg(it->text(ColName), msg));
        });
        connect(installer_, &rg::ComponentInstaller::allFinished, this, [this](bool ok) {
            const QString last = summary_->text();
            Refresh();
            if (!ok) summary_->setText(last + QStringLiteral("\n") + summary_->text());
            UpdateButtons();
        });
    }
    summary_->setText(tr("Working..."));
    installer_->install(all_, ids);
    UpdateButtons();
}

void ComponentsPage::ShowDialog(rg::AppCore* core, QWidget* parent) {
    QDialog d(parent);
    d.setWindowTitle(tr("Components"));
    d.resize(900, 640);
    auto* v = new QVBoxLayout(&d);
    auto* page = new ComponentsPage(core, &d);
    v->addWidget(page);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, &d, &QDialog::accept);
    v->addWidget(box);
    page->Refresh();
    d.exec();
}

}  // namespace rg::ui
