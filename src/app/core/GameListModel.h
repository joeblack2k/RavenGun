// SPDX-License-Identifier: GPL-3.0-or-later
// Structuur naar Dolphin, Source/Core/DolphinQt/GameList/GameListModel.h (GPL-2.0-or-later, commit
// e6f3ae17627e4344da95b13424af5baf4c892b08): kolommen als enum, SORT_ROLE, ShouldDisplayGameListItem/SetSearchTerm en
// een schaal voor de rasterweergave. De gegevens komen uit rg::GameLibrary en rg::GameData.
#pragma once
#include <QAbstractTableModel>
#include <QHash>
#include <QPixmap>

#include "core/GameData.h"
#include "core/GameLibrary.h"

namespace rg {

class GameListModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit GameListModel(GameLibrary* library, GameData* data, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QHash<int, QByteArray> roleNames() const override;  // voor een latere QML-weergave

    static constexpr int SORT_ROLE = Qt::UserRole;
    static constexpr int ID_ROLE = Qt::UserRole + 1;
    static constexpr int LARGE_ICON_ROLE = Qt::UserRole + 2;

    enum class Column { Icon = 0, Title, Platform, Status, Route, Notes, Profile, Path, Count };

    const GameEntry* game(int row) const;
    int rowOf(const QString& id) const;
    bool ShouldDisplayGameListItem(int row) const;
    void SetSearchTerm(const QString& term);
    void setRunningProfile(const QString& profile);
    void setScale(double s);
    double scale() const { return scale_; }
    void reload();

    QString statusText(const GameEntry& g) const;
    static QPixmap placeholderIcon(const QString& name, int size);

private:
    QPixmap icon(const GameEntry& g, int size) const;
    GameLibrary* library_;
    GameData* data_;
    QList<GameEntry> games_;
    QString term_, running_;
    double scale_ = 1.0;
    mutable QHash<QString, QPixmap> iconCache_;
};

}  // namespace rg
