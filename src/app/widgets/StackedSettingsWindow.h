// SPDX-License-Identifier: GPL-2.0-or-later
// Overgenomen uit Dolphin (GPL-2.0-or-later), commit e6f3ae17627e4344da95b13424af5baf4c892b08:
//   Source/Core/DolphinQt/Config/SettingsWindow.{h,cpp} (class StackedSettingsWindow)
// Copyright 2015/2016 Dolphin Emulator Project. Aanpassingen voor RavenGun: geen Dolphin-Settings, geen macOS-tak,
// paneelnamen opvraagbaar (voor schermafdrukken), extra knoppen naast Close mogelijk.
#pragma once
#include <QDialog>

class QDialogButtonBox;
class QListWidget;
class QStackedWidget;

namespace rg::ui {

// A settings window with a QListWidget to switch between panes of a QStackedWidget.
class StackedSettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit StackedSettingsWindow(QWidget* parent = nullptr);
    void ActivatePane(int index);
    int PaneCount() const;
    QString PaneName(int index) const;

protected:
    void AddPane(QWidget*, const QString& name);
    // Adds a scrollable Pane.
    void AddWrappedPane(QWidget*, const QString& name);
    // For derived classes to call after they create their settings panes.
    void OnDoneCreatingPanes();
    void changeEvent(QEvent* event) override;
    QDialogButtonBox* ButtonBox() const { return m_button_box; }

private:
    void UpdateNavigationListStyle();
    QStackedWidget* m_stacked_panes = nullptr;
    QListWidget* m_navigation_list = nullptr;
    QDialogButtonBox* m_button_box = nullptr;
};

}  // namespace rg::ui
