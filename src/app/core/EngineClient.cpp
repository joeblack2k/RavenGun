// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/EngineClient.h"

#include <QJsonDocument>

#include "core/AppPaths.h"

namespace rg {

EngineClient::EngineClient(QString socketPath, QObject* parent)
    : QObject(parent), path_(socketPath.isEmpty() ? AppPaths::socketPath() : std::move(socketPath)) {
    connect(&sock_, &QLocalSocket::readyRead, this, &EngineClient::onReadyRead);
    connect(&sock_, &QLocalSocket::disconnected, this, &EngineClient::onDisconnected);
    connect(&sock_, &QLocalSocket::connected, this, [this] {
        wasConnected_ = true;
        emit connectedChanged(true);
        poll();
    });
    connect(&sock_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (sock_.state() != QLocalSocket::ConnectedState) onDisconnected();
    });
    reconnect_.setInterval(2000);
    connect(&reconnect_, &QTimer::timeout, this, &EngineClient::tryConnect);
    connect(&poll_, &QTimer::timeout, this, &EngineClient::poll);
}

EngineClient::~EngineClient() {
    // Eerst de socket loskoppelen: zijn destructor meldt "disconnected" terwijl pending_ al weg zou zijn.
    QObject::disconnect(&sock_, nullptr, this, nullptr);
    sock_.abort();
    for (const Pending& p : pending_) delete p.timer;
    pending_.clear();
}

void EngineClient::start(int pollMs) {
    polling_ = true;
    poll_.setInterval(pollMs);
    poll_.start();
    reconnect_.start();
    tryConnect();
}

void EngineClient::stop() {
    polling_ = false;
    poll_.stop();
    reconnect_.stop();
    sock_.abort();
}

void EngineClient::refreshNow() { poll(); }

void EngineClient::tryConnect() {
    if (path_.isEmpty() || sock_.state() != QLocalSocket::UnconnectedState) return;
    sock_.connectToServer(path_, QIODevice::ReadWrite);
}

void EngineClient::onDisconnected() {
    buf_.clear();
    failAll(QStringLiteral("not_connected"), QStringLiteral("The engine is not running."));
    pollInFlight_ = 0;
    if (wasConnected_) {
        wasConnected_ = false;
        status_ = {};
        controllers_ = {};
        games_ = {};
        emit connectedChanged(false);
    }
    if (sock_.state() != QLocalSocket::UnconnectedState) sock_.abort();
}

void EngineClient::failAll(const QString& code, const QString& msg) {
    const auto pend = pending_;
    pending_.clear();
    for (const Pending& p : pend) {
        if (p.timer) p.timer->deleteLater();
        if (p.cb) {
            EngineReply r;
            r.errorCode = code;
            r.errorMessage = msg;
            p.cb(r);
        }
    }
}

void EngineClient::request(const QString& cmd, const QJsonObject& fields, Callback cb, int timeoutMs) {
    if (!isConnected()) {
        if (cb) {
            EngineReply r;
            r.errorCode = QStringLiteral("not_connected");
            r.errorMessage = QStringLiteral("The engine is not running.");
            // Asynchroon, zodat de aanroeper altijd hetzelfde gedrag ziet.
            QMetaObject::invokeMethod(this, [cb, r] { cb(r); }, Qt::QueuedConnection);
        }
        tryConnect();
        return;
    }
    const qint64 id = nextId_++;
    QJsonObject o = fields;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("cmd"), cmd);
    o.insert(QStringLiteral("id"), id);
    Pending p;
    p.cb = std::move(cb);
    p.timer = new QTimer(this);
    p.timer->setSingleShot(true);
    connect(p.timer, &QTimer::timeout, this, [this, id] {
        auto it = pending_.find(id);
        if (it == pending_.end()) return;
        Pending q = *it;
        pending_.erase(it);
        q.timer->deleteLater();
        if (q.cb) {
            EngineReply r;
            r.errorCode = QStringLiteral("timeout");
            r.errorMessage = QStringLiteral("The engine did not answer in time.");
            q.cb(r);
        }
    });
    p.timer->start(timeoutMs);
    pending_.insert(id, p);
    sock_.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
}

void EngineClient::onReadyRead() {
    buf_ += sock_.readAll();
    if (buf_.size() > 8 * 1024 * 1024) {  // beschadigde stroom: opnieuw beginnen
        sock_.abort();
        return;
    }
    int nl;
    while ((nl = static_cast<int>(buf_.indexOf('\n'))) >= 0) {
        const QByteArray line = buf_.left(nl);
        buf_.remove(0, nl + 1);
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.contains(QStringLiteral("event"))) continue;  // watch-events gebruikt de app (nog) niet
        const qint64 id = o.value(QStringLiteral("id")).toInteger(-1);
        auto it = pending_.find(id);
        if (it == pending_.end()) continue;
        Pending p = *it;
        pending_.erase(it);
        if (p.timer) p.timer->deleteLater();
        EngineReply r;
        r.ok = o.value(QStringLiteral("ok")).toBool();
        r.result = o.value(QStringLiteral("result"));
        const QJsonObject e = o.value(QStringLiteral("error")).toObject();
        r.errorCode = e.value(QStringLiteral("code")).toString();
        r.errorMessage = e.value(QStringLiteral("message")).toString();
        if (p.cb) p.cb(r);
    }
}

void EngineClient::poll() {
    if (!isConnected() || pollInFlight_ > 0) return;
    pollInFlight_ = 3;
    request(QStringLiteral("status"), {}, [this](const EngineReply& r) {
        --pollInFlight_;
        if (!r.ok) return;
        status_ = r.object();
        emit statusUpdated(status_);
    });
    request(QStringLiteral("controllers"), {}, [this](const EngineReply& r) {
        --pollInFlight_;
        if (!r.ok) return;
        controllers_ = r.object();
        emit controllersUpdated(controllers_);
    });
    request(QStringLiteral("games"), {}, [this](const EngineReply& r) {
        --pollInFlight_;
        if (!r.ok) return;
        games_ = r.object();
        emit gamesUpdated(games_);
    });
}

}  // namespace rg
