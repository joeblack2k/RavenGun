// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Stap "Components" van de wizard (ook los via Tools > Components): toont wat RavenGun nodig heeft (Wine, DXVK, BepInEx, mods, DLL's
// uit Steam, eigen onderdelen, TeknoParrot-runtime) met de stand per onderdeel, en haalt het op of zet het klaar met voortgang.
// De logica staat in core/Components en core/ComponentInstaller; hier alleen het venster.
#include <QWizardPage>

#include "core/ComponentInstaller.h"
#include "core/Components.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace rg { class AppCore; }

namespace rg::ui {

class ComponentsPage final : public QWizardPage {
    Q_OBJECT
public:
    explicit ComponentsPage(rg::AppCore* core, QWidget* parent = nullptr);
    void initializePage() override;
    // Tools > Components: de pagina in een gewoon dialoogvenster.
    static void ShowDialog(rg::AppCore* core, QWidget* parent);

    void Refresh();   // catalogus en standen opnieuw lezen

private:
    void InstallIds(const QStringList& ids);
    void UpdateButtons();
    QTreeWidgetItem* ItemFor(const QString& id) const;

    rg::AppCore* core_;
    rg::ComponentEnv env_;
    QList<rg::Component> all_;
    rg::ComponentInstaller* installer_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* install_all_ = nullptr;
    QPushButton* install_sel_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* refresh_ = nullptr;
};

}  // namespace rg::ui
