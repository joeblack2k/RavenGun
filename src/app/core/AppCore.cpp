// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AppCore.h"

#include <QJsonArray>

namespace rg {

AppCore::AppCore(QObject* parent, const QString& socketPath, const QString& libraryFile)
    : QObject(parent), engine_(socketPath), library_(libraryFile), launcher_(&engine_, &library_, &data_) {
    data_.load();
    library_.setKnownGames(data_.knownGames());
    library_.setOwnGames(data_.ownGames());
    library_.setUnityGames(data_.unityGames());
    model_ = new GameListModel(&library_, &data_, this);
    connect(&engine_, &EngineClient::gamesUpdated, this, [this](const QJsonObject& g) {
        const QJsonObject st = g.value(QStringLiteral("status")).toObject();
        const bool active = st.value(QStringLiteral("running")).toBool() || st.value(QStringLiteral("busy")).toBool();
        model_->setRunningProfile(active ? st.value(QStringLiteral("profile")).toString() : QString());
    });
    connect(&engine_, &EngineClient::connectedChanged, this, [this](bool c) {
        if (!c) model_->setRunningProfile({});
    });
}

void AppCore::start(bool scan) {
    library_.load();
    launcher_.setBackend(library_.data().launcher);
    engine_.start();
    if (scan) library_.rescan();
}

void AppCore::reloadGameData() {
    data_.load();
    library_.setKnownGames(data_.knownGames());
    library_.setOwnGames(data_.ownGames());
    library_.setUnityGames(data_.unityGames());
    model_->reload();
}

}  // namespace rg
