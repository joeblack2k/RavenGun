// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Spelen en stoppen. Het starten zelf doet een verwisselbare backend (core/Launcher.h: nu "teknoparrot-ui", via IPC
// game_start van de engine; later "direct"). De voortgang komt uit de toestand van `games` in de engine (idle,
// waiting_move, starting, running, stopping, failed) en wordt hier naar Engelse meldingen vertaald.
#include <QJsonObject>
#include <QObject>
#include <memory>

#include "core/GameLibrary.h"
#include "core/Launcher.h"

namespace rg {

class EngineClient;
class GameData;

class GameLauncher : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, WaitingMove, Starting, Running, Stopping, Failed, EngineOffline };
    GameLauncher(EngineClient* engine, GameLibrary* library, GameData* data, QObject* parent = nullptr);
    ~GameLauncher() override;

    void setBackend(const QString& id);
    LaunchBackend* backend() const { return backend_.get(); }

    State state() const { return state_; }
    QString text() const { return text_; }
    QString runningProfile() const { return profile_; }
    bool canStop() const { return state_ == State::WaitingMove || state_ == State::Starting || state_ == State::Running; }

    // Waarom deze game (nog) niet kan starten; leeg = kan. Engels.
    static QString whyNotPlayable(const GameEntry& g);
    void play(const GameEntry& g);
    void stop();

    static QString stateText(const QString& engineState, const QString& profileName, const QString& message);

signals:
    void stateChanged(rg::GameLauncher::State state, const QString& text);
    void message(const QString& text, bool error);  // voor de statusbalk of een melding

private:
    void onGames(const QJsonObject& games);
    void set(State s, const QString& text);
    EngineClient* engine_;
    GameLibrary* library_;
    GameData* data_;
    std::unique_ptr<LaunchBackend> backend_;
    State state_ = State::EngineOffline;
    QString text_, profile_;
};

}  // namespace rg
