// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GameLauncher.h"

#include <QJsonArray>

#include "core/EngineClient.h"
#include "core/PreLaunch.h"

namespace rg {

GameLauncher::GameLauncher(EngineClient* engine, GameLibrary* library, GameData* data, QObject* parent)
    : QObject(parent), engine_(engine), library_(library), data_(data) {
    setBackend(library_->data().launcher);
    connect(engine_, &EngineClient::gamesUpdated, this, &GameLauncher::onGames);
    connect(engine_, &EngineClient::connectedChanged, this, [this](bool c) {
        if (!c) set(State::EngineOffline, QStringLiteral("Engine stopped"));
    });
    text_ = QStringLiteral("Engine stopped");
}

GameLauncher::~GameLauncher() = default;

void GameLauncher::setBackend(const QString& id) { backend_ = makeBackend(id, engine_, library_, data_); }

QString GameLauncher::stateText(const QString& s, const QString& name, const QString& message) {
    const QString game = name.isEmpty() ? QStringLiteral("The game") : name;
    if (s == QStringLiteral("waiting_move")) return prelaunch::waitingText({}, true);
    if (s == QStringLiteral("starting")) return QStringLiteral("Starting %1...").arg(game);
    if (s == QStringLiteral("running")) return QStringLiteral("%1 is running").arg(game);
    if (s == QStringLiteral("stopping")) return QStringLiteral("Stopping...");
    if (s == QStringLiteral("failed"))
        return message.isEmpty() ? QStringLiteral("Failed to start") : QStringLiteral("Failed: %1").arg(message);
    return QStringLiteral("No game running");
}

void GameLauncher::onGames(const QJsonObject& games) {
    const QJsonObject st = games.value(QStringLiteral("status")).toObject();
    const QString s = st.value(QStringLiteral("state")).toString();
    profile_ = st.value(QStringLiteral("profile")).toString();
    QString name = profile_;
    for (const auto& v : games.value(QStringLiteral("games")).toArray()) {
        const QJsonObject g = v.toObject();
        if (g.value(QStringLiteral("profile")).toString() == profile_) name = g.value(QStringLiteral("name")).toString();
    }
    // waiting_move: op wiens Move de engine wacht (waiting_players), als "Please turn on your PS Move controller".
    const QString text = s == QStringLiteral("waiting_move") ? prelaunch::waitingTextFromStatus(st)
                                                             : stateText(s, name, st.value(QStringLiteral("message")).toString());
    State ns = State::Idle;
    if (s == QStringLiteral("waiting_move")) ns = State::WaitingMove;
    else if (s == QStringLiteral("starting")) ns = State::Starting;
    else if (s == QStringLiteral("running")) ns = State::Running;
    else if (s == QStringLiteral("stopping")) ns = State::Stopping;
    else if (s == QStringLiteral("failed")) ns = State::Failed;
    set(ns, text);
}

void GameLauncher::set(State s, const QString& text) {
    if (s == state_ && text == text_) return;
    state_ = s;
    text_ = text;
    emit stateChanged(s, text);
}

QString GameLauncher::whyNotPlayable(const GameEntry& g) {
    switch (g.kind) {
        case GameKind::TeknoParrot:
        case GameKind::Unity:
            if (g.profile.isEmpty()) return QStringLiteral("This game is not set up yet.");
            return {};
        case GameKind::Detected:
            return QStringLiteral("This game is not set up yet. Right-click it and choose \"Set Up Game...\".");
        case GameKind::Unconfigured:
            return QStringLiteral("This folder is not linked to a known game. Open Properties to link it.");
    }
    return {};
}

void GameLauncher::play(const GameEntry& g) {
    const QString why = whyNotPlayable(g);
    if (!why.isEmpty()) {
        emit message(why, true);
        return;
    }
    if (!engine_->isConnected()) {
        emit message(QStringLiteral("The engine is not running. Start it with Engine > Start Engine."), true);
        return;
    }
    if (canStop()) {
        emit message(QStringLiteral("A game is already running. Stop it first."), true);
        return;
    }
    QString reason;
    if (!backend_->available(&reason) || !backend_->prepare(g, &reason)) {
        emit message(reason, true);
        return;
    }
    set(State::Starting, stateText(QStringLiteral("starting"), g.displayName(), {}));
    backend_->start(g, [this, g](bool ok, const QString& err) {
        if (!ok) {
            set(State::Failed, QStringLiteral("Failed: %1").arg(err));
            emit message(QStringLiteral("Could not start %1: %2").arg(g.displayName(), err), true);
        } else {
            emit message(QStringLiteral("Starting %1...").arg(g.displayName()), false);
        }
        engine_->refreshNow();
    });
}

void GameLauncher::stop() {
    backend_->stop([this](bool ok, const QString& err) {
        if (!ok) emit message(QStringLiteral("Could not stop the game: %1").arg(err), true);
        else emit message(QStringLiteral("Stopping the game..."), false);
        engine_->refreshNow();
    });
}

}  // namespace rg
