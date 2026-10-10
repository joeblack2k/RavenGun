# RavenGun — takenlijst

Leidraad van de eigenaar (2026-10-10): alle games in de TeknoParrot-map werken zo goed mogelijk met onze lightgun, en er is één app (Qt, Engels, in de stijl van Dolphin) die alles regelt, ook in Steam Game Mode. Deze lijst wordt bijgewerkt na elke afgeronde stap. Status: [ ] open, [~] bezig, [x] klaar.

## Besluiten (2026-10-10)

- De engine (ravengund) blijft een achtergrondservice; de Qt-app start, stopt en bedient hem. De app is het enige wat de gebruiker ziet.
- De webpagina blijft alleen als afstandsbediening voor de telefoon; aan/uit in de app.
- Licentie: GPL-3.0-or-later (LICENSE). Daarmee mag code uit Dolphin (GPL-2.0-or-later) en OpenParrot (GPL-3.0) worden overgenomen, met bronvermelding. De DemulShooter-plugin (geen licentie) wordt niet meegeleverd; vervangen door een eigen plugin.
- App-teksten in het Engels; code-commentaar en interne docs blijven Nederlands.
- Eerste gebruik: inrichten in Desktop Mode met een wizard (rechten, Moves koppelen, kalibratie, gamemap toevoegen, toevoegen aan Steam); daarna werkt alles na een herstart ook in Game Mode via de fullscreen-modus van de app.

## Spoor A — Qt-app

- [x] A1 Architectuur en skelet: Qt 6 Widgets (gelijk aan de systeem-Qt 6.11 van SteamOS), hoofdvenster naar het voorbeeld van Dolphin (gamelijst/raster, werkbalk Play/Stop/Config/Controllers/Add Folder/Fullscreen), praat met de engine via de bestaande IPC-socket. Gebouwd in een container (scripts/build-app.sh, docs/build.md), kern zonder widgets voor A7, launcher-backends (teknoparrot-ui nu, direct later).
- [x] A2 Gamebibliotheek: "Add Folder" scant een map en herkent alle games (TeknoParrot-profielen, sjablonen op bestandsnaam, game-options.json, Unity-route, overige als Unconfigured), met iconen en status per game; "Set Up Game" maakt op verzoek het profiel (tp-add-game.py).
- [~] A3 Per game "Properties" (rechtsklik > Edit): opties uit packaging/game-options.json (bibliotheek = bron van waarheid, ProfileSync naar het profiel), Display, invoerroute (alleen tonen; wijzigen nog in io-routes.json), "memory card" (bekijken en lokaal wissen). Open: het echte game-options.json van de andere stap (de parser volgt het concept-schema; getest met tests/app/data/game-options.json); niet-INI-formaten (rtdt_adjval, tab_ini, ...) en base wine_prefix zijn nog alleen-lezen.
- [x] A4 Controllers: Player 1 t/m 4 met None / Mouse / PS Move en Configure (General, Aiming, Rumble, Buttons incl. Sharp Shooter en Navigation Controller, Calibration), koppelwizard, hernoemen, speler kiezen (ook Navigation Controller), knipperen, verwijderen; muis als lightgun in de engine (IPC mice/set_mouse_gun). Nog niet met echte Moves of een echte muis getest (alleen test-engine).
- [x] A5 Instellingen: engine starten/stoppen/herstarten en autostart, telefoon-webpagina aan/uit (web.lan) met adres, OLED-bescherming met wachttijd, paden (gamemappen, TeknoParrot-runtime met controle, launcher), logboek.
- [ ] A6 Eerste-gebruik-wizard (Desktop Mode): rechten installeren (udev/Bluetooth-hulp via één beheerdersprompt), Moves koppelen, kalibratie, gamemap, toevoegen aan Steam.
- [ ] A7 Fullscreen-modus voor Steam Game Mode: zwarte achtergrond, game-iconen, kiezen en starten met de lightgun; terug na afsluiten van de game.
- [ ] A8 Steam-integratie: app als niet-Steam-game toevoegen (shortcuts.vdf) met fullscreen-argument; controleren in Game Mode.
- [ ] A9 Verpakking voor GitHub: bouwen, installeren, AppImage of vergelijkbaar; README voor nieuwe gebruikers ("downloaden, starten, werkt").

## Spoor B — games

Bronnen: docs/research/findings-digest.md (per game platform, board, route, moeite) en packaging/games-status.md.

- [x] Halo, Terminator, Tomb Raider, Rabbids, Time Crisis 5, Transformers: starten en zijn speelbaar (deels nog niet met de echte Move bevestigd).
- [~] B1 JVS-board in de IO-brug + Let's Go Safari (COM4 als pty).
- [~] B2 Eigen Unity-plugin (Tomb Raider, Rabbids) ter vervanging van de DemulShooter-plugin.
- [ ] B3 Overige games uit de backups toevoegen en per route afwerken (lijst volgt uit de digest).
- [ ] B4 Jurassic Park: zwart beeld/crash (geparkeerd; nieuwe aanknopingspunten uit antwoorden.md).
- [ ] B5 Luigi's Mansion: COM1 + LMAIO-flashstap + board; de Capcom-afscherming zelf wordt niet omzeild.
- [ ] B6 Speler 2..4 per game (bindingen, tweede JVS-node voor Halo).

## Spoor C — met de eigenaar

- [ ] C1 TC5, Rabbids, Transformers met de echte Move spelen.
- [ ] C2 Tweede Move koppelen (via de app).
- [ ] C3 Kompasverstoring van 2026-10-09 (drift bij Tomb Raider) uitzoeken.
