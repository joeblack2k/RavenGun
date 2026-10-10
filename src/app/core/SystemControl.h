// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Systeemkant van de app zonder root: systemd-gebruikersunits (systemctl --user), het instellingenbestand van de engine
// (alleen web.lan en web.require_token, die geen IPC-commando hebben) en hulpscripts uit de repo. Alles asynchroon via
// QProcess. $RAVENGUN_SYSTEMCTL vervangt systemctl (voor tests en schermafdrukken).
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>

namespace rg {

inline constexpr const char* kEngineUnit = "ravengund.service";
inline constexpr const char* kOledUnit = "ravengun-oled-saver.service";
inline constexpr const char* kGameUnit = "ravengun-tpui.service";

class SystemControl {
public:
    using Done = std::function<void(int exitCode, const QString& output)>;
    // Start een proces; Done komt in de GUI-thread (via `context`). Time-out na timeoutMs (exitcode -1).
    static void run(const QString& program, const QStringList& args, QObject* context, Done done, int timeoutMs = 20000);
    static void systemctl(const QStringList& args, QObject* context, Done done);
    // is-active en is-enabled in één keer: active ("active", "inactive", "failed", ...) en enabled ("enabled", "disabled", ...).
    static void unitState(const QString& unit, QObject* context, std::function<void(const QString& active, const QString& enabled)> cb);

    // Engine-instellingen (config.json): lezen en web.lan/web.require_token zetten (atomisch, overige sleutels blijven).
    static QJsonObject engineConfig();
    static bool setEngineWeb(bool lan, bool requireToken, QString* err);
    static int engineWebPort();  // web.port, standaard 8765
    static QString webToken();   // inhoud van ~/.config/ravengun/web-token (leeg als het ontbreekt)
    static QStringList lanAddresses();  // IPv4-adressen van het thuisnetwerk (geen loopback)

    // OLED-bescherming: wachttijd uit de drop-in (RG_SAVER_IDLE_S), standaard 600 s.
    static int oledIdleSeconds();
    static bool setOledIdleSeconds(int seconds, QString* err);  // schrijft de drop-in; daarna daemon-reload + herstart
    static QString oledDropInPath();

    // Laatste regels van een logbestand.
    static QString tail(const QString& path, int maxLines = 400);
};

}  // namespace rg
