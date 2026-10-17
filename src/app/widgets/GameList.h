// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/GameList/GameList.{h,cpp}, ListProxyModel.{h,cpp} en GridProxyModel.{h,cpp}
// (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08). Copyright 2015/2017 Dolphin Emulator Project.
// Overgenomen: QStackedWidget met tabel- en rasterweergave en een lege weergave, de twee proxymodellen met
// titelvolgorde bij gelijke sorteerwaarden, zoomen (Ctrl +/-), Enter = spelen, Alt+Enter = eigenschappen, het rechtermuismenu
// en het verdelen van de kolombreedte (OnHeaderViewChanged). Aangepast: onze kolommen en ons menu.
#pragma once
#include <QSortFilterProxyModel>
#include <QStackedWidget>

#include "core/GameListModel.h"

class QLabel;
class QListView;
class QTableView;
class QAbstractItemView;

namespace rg::ui {

class ListProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ListProxyModel(QObject* parent = nullptr);
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};

class GridProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit GridProxyModel(QObject* parent = nullptr);
    QVariant data(const QModelIndex& i, int role) const override;
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};

class GameList final : public QStackedWidget {
    Q_OBJECT
public:
    explicit GameList(rg::GameListModel* model, QWidget* parent = nullptr);
    ~GameList() override;

    QString GetSelectedGameId() const;
    void SelectGame(const QString& id);
    void SetListView() { SetPreferredView(true); }
    void SetGridView() { SetPreferredView(false); }
    bool IsListView() const { return m_prefer_list; }
    void SetSearchTerm(const QString& term);
    void SetViewColumn(int col, bool view);
    bool IsColumnVisible(int col) const;
    void ZoomIn();
    void ZoomOut();
    void SetRefreshing(bool refreshing);
    void resizeEvent(QResizeEvent* event) override;

signals:
    void GameSelected();                // dubbelklik of Enter: spelen
    void SelectionChanged(const QString& id);
    void OpenProperties();
    void OpenContainingFolder();
    void SetDefaultGame();
    void RemoveFromList();
    void CreateProfile();
    void AddToSteam();                  // de game als eigen niet-Steam-game (ravengun --play PROFIEL)
    void AddFolderRequested();
    void GameCountUpdated(int total, int visible);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void ShowContextMenu(const QPoint&);
    void MakeListView();
    void MakeGridView();
    void MakeEmptyView();
    void SetPreferredView(bool list);
    QAbstractItemView* GetActiveView() const;
    QSortFilterProxyModel* GetActiveProxyModel() const;
    void ConsiderViewChange();
    void OnHeaderViewChanged();
    void UpdateGameCount();
    void UpdateFont();

    rg::GameListModel* m_model;
    QSortFilterProxyModel* m_list_proxy;
    QSortFilterProxyModel* m_grid_proxy;
    QListView* m_grid;
    QTableView* m_list;
    QLabel* m_empty;
    bool m_prefer_list = true;
};

}  // namespace rg::ui
