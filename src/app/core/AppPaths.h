// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vaste paden van de app (XDG wordt gevolgd) en het vinden van de repo (scripts/ en packaging/).
#include <QString>

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
    // De repo: $RAVENGUN_ROOT, anders de map boven de binary (build/ of build-app/), anders ~/projects/ravengun.
    static QString repoRoot();
    // $XDG_RUNTIME_DIR/ravengun/ravengund.sock (leeg als XDG_RUNTIME_DIR ontbreekt); $RAVENGUN_SOCKET gaat voor.
    static QString socketPath();
    // packaging/<naam> uit de repo, anders de ingebouwde kopie (":/packaging/<naam>"), anders leeg.
    static QString packagingFile(const QString& name);
    // "~" en "~/..." naar de thuismap.
    static QString expandHome(const QString& path);
    static QString homeDir();
};

}  // namespace rg
