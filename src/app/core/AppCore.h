// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// De kern van de app zonder widgets: engineverbinding, bibliotheek, gamegegevens en het starten van games. Het hoofdvenster
// (Qt Widgets) en een latere fullscreen-modus ("big picture", A7) gebruiken dezelfde AppCore.
#include <QObject>

#include "core/EngineClient.h"
#include "core/GameData.h"
#include "core/GameLauncher.h"
#include "core/GameLibrary.h"
#include "core/GameListModel.h"

namespace rg {

class AppCore : public QObject {
    Q_OBJECT
public:
    explicit AppCore(QObject* parent = nullptr, const QString& socketPath = {}, const QString& libraryFile = {});

    EngineClient* engine() { return &engine_; }
    GameLibrary* library() { return &library_; }
    GameData* gameData() { return &data_; }
    GameLauncher* launcher() { return &launcher_; }
    GameListModel* gameListModel() { return model_; }

    // Laadt de bibliotheek en packaging-gegevens, verbindt met de engine; scant bij de eerste start (of als er nog niets is).
    void start(bool scan = true);
    void reloadGameData();

private:
    EngineClient engine_;
    GameLibrary library_;
    GameData data_;
    GameLauncher launcher_;  // na library_ en data_ (gebruikt ze in de constructor)
    GameListModel* model_ = nullptr;
};

}  // namespace rg
