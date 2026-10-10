// SPDX-License-Identifier: GPL-3.0-or-later
// Structuur naar Dolphin GameListModel.cpp (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08).
#include "core/GameListModel.h"

#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QPainter>

namespace rg {

GameListModel::GameListModel(GameLibrary* library, GameData* data, QObject* parent)
    : QAbstractTableModel(parent), library_(library), data_(data) {
    connect(library_, &GameLibrary::gamesChanged, this, &GameListModel::reload);
    reload();
}

void GameListModel::reload() {
    beginResetModel();
    games_ = library_->visibleGames();
    endResetModel();
}

int GameListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(games_.size()); }
int GameListModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(Column::Count); }

const GameEntry* GameListModel::game(int row) const { return row >= 0 && row < games_.size() ? &games_[row] : nullptr; }

int GameListModel::rowOf(const QString& id) const {
    for (int i = 0; i < games_.size(); ++i)
        if (games_[i].id == id) return i;
    return -1;
}

QHash<int, QByteArray> GameListModel::roleNames() const {
    auto r = QAbstractTableModel::roleNames();
    r.insert(ID_ROLE, "gameId");
    r.insert(LARGE_ICON_ROLE, "largeIcon");
    return r;
}

QString GameListModel::statusText(const GameEntry& g) const {
    if (!running_.isEmpty() && g.profile == running_) return QStringLiteral("Running");
    switch (g.kind) {
        case GameKind::TeknoParrot:
        case GameKind::Unity:
            return g.exeExists ? QStringLiteral("Ready") : QStringLiteral("Game files missing");
        case GameKind::Detected: return QStringLiteral("Not set up");
        case GameKind::Unconfigured: return QStringLiteral("Unconfigured");
    }
    return {};
}

QPixmap GameListModel::placeholderIcon(const QString& name, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const uint h = qHash(name);
    const QColor bg = QColor::fromHsv(static_cast<int>(h % 360), 120, 150);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), size / 8.0, size / 8.0);
    QFont f;
    f.setBold(true);
    f.setPixelSize(size / 2);
    p.setFont(f);
    p.setPen(Qt::white);
    const QString letter = name.trimmed().isEmpty() ? QStringLiteral("?") : name.trimmed().left(1).toUpper();
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, letter);
    return pm;
}

QPixmap GameListModel::icon(const GameEntry& g, int size) const {
    const QString key = g.id + QLatin1Char('@') + QString::number(size);
    auto it = iconCache_.find(key);
    if (it != iconCache_.end()) return *it;
    QPixmap pm;
    if (!g.iconPath.isEmpty() && QFileInfo(g.iconPath).isFile()) {
        QPixmap src(g.iconPath);
        if (!src.isNull()) pm = src.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (pm.isNull()) pm = placeholderIcon(g.displayName(), size);
    iconCache_.insert(key, pm);
    return pm;
}

QVariant GameListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const GameEntry& g = games_[index.row()];
    if (role == ID_ROLE) return g.id;
    if (role == LARGE_ICON_ROLE) return icon(g, static_cast<int>(128 * scale_));
    const bool isDefault = g.id == library_->data().defaultGame;
    if (role == Qt::FontRole && (isDefault || (!running_.isEmpty() && g.profile == running_))) {
        QFont f;
        f.setBold(true);
        return f;
    }
    const QString profile = g.profile.isEmpty() ? g.templateProfile : g.profile;
    switch (static_cast<Column>(index.column())) {
        case Column::Icon:
            if (role == Qt::DecorationRole) return icon(g, 32);
            if (role == SORT_ROLE) return g.displayName().toLower();
            break;
        case Column::Title:
            if (role == Qt::DisplayRole || role == SORT_ROLE)
                return isDefault && role == Qt::DisplayRole ? g.displayName() + QStringLiteral("  (default)") : g.displayName();
            if (role == Qt::ToolTipRole) return g.folder;
            break;
        case Column::Platform:
            if (role == Qt::DisplayRole || role == SORT_ROLE) {
                const QString p = g.platform == QStringLiteral("TeknoParrot") ? platformLabel(g.platform) : g.platform;  // oude cache
                return p.isEmpty() ? kindLabel(g.kind) : p;
            }
            break;
        case Column::Status:
            if (role == Qt::DisplayRole || role == SORT_ROLE) return statusText(g);
            if (role == Qt::ForegroundRole) {
                const QString s = statusText(g);
                if (s == QStringLiteral("Running")) return QColor(0x2e, 0x9e, 0x44);
                if (s != QStringLiteral("Ready")) return QColor(0xb0, 0x6a, 0x00);
            }
            break;
        case Column::Route:
            if (role == Qt::DisplayRole || role == SORT_ROLE) {
                if (g.kind == GameKind::Unconfigured || profile.isEmpty()) return QString();
                return GameData::routeLabel(data_->route(profile).route);
            }
            break;
        case Column::Notes:
            if (role == Qt::DisplayRole || role == SORT_ROLE || role == Qt::ToolTipRole) {
                if (!g.userNote.isEmpty()) return g.userNote;
                return data_->note(profile);
            }
            break;
        case Column::Profile:
            if (role == Qt::DisplayRole || role == SORT_ROLE) return profile;
            break;
        case Column::Path:
            if (role == Qt::DisplayRole || role == SORT_ROLE) return g.exePath.isEmpty() ? g.folder : g.exePath;
            break;
        case Column::Count: break;
    }
    return {};
}

QVariant GameListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Vertical || role != Qt::DisplayRole) return {};
    switch (static_cast<Column>(section)) {
        case Column::Icon: return QStringLiteral("Icon");
        case Column::Title: return QStringLiteral("Title");
        case Column::Platform: return QStringLiteral("Platform");
        case Column::Status: return QStringLiteral("Status");
        case Column::Route: return QStringLiteral("Input Route");
        case Column::Notes: return QStringLiteral("Notes");
        case Column::Profile: return QStringLiteral("Profile");
        case Column::Path: return QStringLiteral("Path");
        case Column::Count: break;
    }
    return {};
}

bool GameListModel::ShouldDisplayGameListItem(int row) const {
    if (term_.isEmpty()) return true;
    const GameEntry& g = games_[row];
    return g.displayName().contains(term_, Qt::CaseInsensitive) || g.profile.contains(term_, Qt::CaseInsensitive) ||
           g.platform.contains(term_, Qt::CaseInsensitive);
}

void GameListModel::SetSearchTerm(const QString& term) { term_ = term; }

void GameListModel::setRunningProfile(const QString& profile) {
    if (profile == running_) return;
    running_ = profile;
    if (!games_.isEmpty()) emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

void GameListModel::setScale(double s) {
    scale_ = s;
    if (!games_.isEmpty()) emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

}  // namespace rg
