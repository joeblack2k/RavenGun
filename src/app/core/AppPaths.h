// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vaste paden van de app (XDG wordt gevolgd) en het vinden van de repo (scripts/ en packaging/).
#include <QString>
#include <QStringList>

namespace rg {

class AppPaths {
public:
    // $XDG_CONFIG_HOME/ravengun (standaard ~/.config/ravengun)
    static QString configDir();
    static QString libraryFile();        // configDir()/library.json
    static QString engineConfigFile();   // configDir()/config.json (van de engine)
    static QString appSettingsFile();    // configDir()/ravengun-app.conf (venster, weergave)
    // $XDG_STATE_HOME/ravengun (standaard ~/.local/state/ravengun)
    static QString stateDir();
    // $XDG_DATA_HOME/ravengun (standaard ~/.local/share/ravengun)
    static QString dataDir();
    static QString defaultTpDir();       // dataDir()/runtime/TeknoParrotUi-2.0.0.130-linux-x64
    static QString viewsDir();           // dataDir()/runtime/views
    // De datamap met scripts/, packaging/ en docs/. Vaste volgorde (gelijk aan de engine, src/ravengun/paths.hpp):
    // 1. $RAVENGUN_DATA_DIR, 2. <prefix>/share/ravengun (prefix = de map boven die van de binary), 3. de repo bij
    // ontwikkeling: $RAVENGUN_ROOT, de map boven de binary (build/ of build-app/), ~/projects/ravengun. Leeg als niets past.
    static QString dataRoot();
    static QString repoRoot() { return dataRoot(); }  // oude naam
    // De map van de binary (bin/ van de installatie of build-app/).
    static QString binDir();
    // Voor tests: dezelfde zoektocht met opgegeven invoer.
    static QString findDataRoot(const QString& dataDirEnv, const QString& rootEnv, const QString& appDir, const QString& home);
    static QStringList dataRootCandidates(const QString& dataDirEnv, const QString& rootEnv, const QString& appDir, const QString& home);
    static bool looksLikeDataRoot(const QString& dir);
    // $XDG_RUNTIME_DIR/ravengun/ravengund.sock (leeg als XDG_RUNTIME_DIR ontbreekt); $RAVENGUN_SOCKET gaat voor.
    static QString socketPath();
    // packaging/<naam> uit de datamap, anders de ingebouwde kopie (":/packaging/<naam>"), anders leeg.
    static QString packagingFile(const QString& name);
    // "~" en "~/..." naar de thuismap.
    static QString expandHome(const QString& path);
    static QString homeDir();
};

}  // namespace rg
