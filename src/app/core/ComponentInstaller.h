// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Installeert onderdelen uit de catalogus (core/Components.h) in de gebruikersmap, zonder shellscripts:
//  - downloaden met Qt Network naar <data>/components/dl/<naam>.part (hervatten met Range), sha256 controleren, dan hernoemen;
//  - uitpakken met libarchive naar <data>/components/.staging/<id>, controleren (boom-sha256 bij mods) en met een rename op zijn plek;
//  - Steam-onderdelen (DirectX, Visual C++ 2010, GStreamer 32 bits, sniper-bibliotheken) lezen uit de eigen Steam-installatie;
//  - Wine-onderdelen (prefix, PhysX) starten de Wine van RavenGun rechtstreeks.
// De synchrone kern (installFromFiles) is los te testen; de klasse zelf regelt de wachtrij, de voortgang en annuleren.
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>

#include "core/Components.h"

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QThread;

namespace rg {

class ComponentInstaller : public QObject {
    Q_OBJECT
public:
    using Progress = std::function<void(const QString& phase, qint64 done, qint64 total)>;

    explicit ComponentInstaller(const ComponentEnv& env, QObject* parent = nullptr);
    ~ComponentInstaller() override;

    // Zet de onderdelen (en wat ze nodig hebben) in de wachtrij en begint. Onderdelen die al kloppen worden overgeslagen.
    void install(const QList<Component>& all, const QStringList& ids);
    void cancel();
    bool busy() const { return busy_; }
    const ComponentEnv& env() const { return env_; }

    // Downloadmap en bestandspad van een download.
    QString downloadPath(const ComponentDownload& d) const;

    // Synchroon: de stappen na het downloaden (uitpakken, kopiëren, Steam, Wine, links, stempel).
    static bool installFromFiles(const Component& c, const QStringList& files, const ComponentEnv& env, const Progress& progress,
                                 const std::atomic_bool* cancel, QString* err);
    // Verplaatst/zet de inhoud van `staging` in `dest`: vervangt de map (merge=false) of voegt bestanden toe (merge=true; overwrite
    // bepaalt of bestaande bestanden vervangen worden). De staging-map is daarna weg.
    static bool placeStaging(const QString& staging, const QString& dest, bool merge, bool overwrite, QString* err);
    static bool copyTree(const QString& src, const QString& dst, const QStringList& exclude, const std::atomic_bool* cancel, QString* err);

signals:
    void started(const QString& id);
    void progress(const QString& id, const QString& phase, qint64 done, qint64 total);
    void finished(const QString& id, bool ok, const QString& message);
    void allFinished(bool ok);

private:
    void next();
    void downloadNext();
    void startRequest(bool allowResume);
    void onReadyRead();
    void onReplyFinished();
    void runWorker();
    void finishCurrent(bool ok, const QString& msg);

    ComponentEnv env_;
    QNetworkAccessManager* nam_ = nullptr;
    QList<Component> queue_;
    QList<Component> all_;
    Component cur_;
    int dlIndex_ = 0;
    QStringList files_;
    QNetworkReply* reply_ = nullptr;
    QFile* part_ = nullptr;
    qint64 resumeFrom_ = 0;
    bool resumeRequested_ = false;
    bool retriedNoRange_ = false;
    QThread* worker_ = nullptr;
    std::atomic_bool cancel_{false};
    bool busy_ = false;
    bool allOk_ = true;
};

}  // namespace rg
