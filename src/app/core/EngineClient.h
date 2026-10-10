// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Verbinding met de engine (ravengund) via de IPC-socket $XDG_RUNTIME_DIR/ravengun/ravengund.sock: één JSON-object per regel,
// antwoorden gekoppeld aan het `id` van het verzoek (docs/architecture.md, "IPC"). Asynchroon, zonder blokkeren van de GUI.
//
// De client houdt zelf de verbinding open (opnieuw verbinden elke 2 s) en ververst periodiek de toestand die het
// hoofdvenster nodig heeft: `status` (speler 1), `controllers` en `games`. Andere onderdelen sturen losse verzoeken met
// request(). Geen widgets: ook bruikbaar voor een latere fullscreen-modus.
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <functional>

namespace rg {

struct EngineReply {
    bool ok = false;
    QJsonValue result;
    QString errorCode;     // parse_error, invalid_argument, unavailable, unknown_command, ..., of "not_connected"/"timeout"
    QString errorMessage;   // Engels, voor de gebruiker (afgeleid van de foutcode)
    QString engineMessage;  // de oorspronkelijke melding van de engine (Nederlands), voor logboek en tooltip
    static QString englishFor(const QString& code);
    QJsonObject object() const { return result.toObject(); }
};

class EngineClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const EngineReply&)>;
    explicit EngineClient(QString socketPath = {}, QObject* parent = nullptr);
    ~EngineClient() override;

    QString socketPath() const { return path_; }
    bool isConnected() const { return sock_.state() == QLocalSocket::ConnectedState; }
    void start(int pollMs = 1500);  // verbinden en periodiek verversen
    void stop();
    void refreshNow();

    // cmd plus velden; `v` en `id` vult de client in. cb krijgt altijd precies één antwoord (ook bij time-out of geen verbinding).
    void request(const QString& cmd, const QJsonObject& fields = {}, Callback cb = {}, int timeoutMs = 4000);

    // Laatste bekende toestand (leeg zolang er niets is).
    const QJsonObject& status() const { return status_; }
    const QJsonObject& controllers() const { return controllers_; }
    const QJsonObject& games() const { return games_; }

signals:
    void connectedChanged(bool connected);
    void statusUpdated(const QJsonObject& status);
    void controllersUpdated(const QJsonObject& controllers);
    void gamesUpdated(const QJsonObject& games);

private:
    void tryConnect();
    void onReadyRead();
    void onDisconnected();
    void failAll(const QString& code, const QString& msg);
    void poll();

    struct Pending {
        Callback cb;
        QTimer* timer = nullptr;
    };
    QString path_;
    QLocalSocket sock_;
    QByteArray buf_;
    qint64 nextId_ = 1;
    QHash<qint64, Pending> pending_;
    QTimer reconnect_, poll_;
    bool wasConnected_ = false;
    bool polling_ = false;
    int pollInFlight_ = 0;
    QJsonObject status_, controllers_, games_;
};

}  // namespace rg
