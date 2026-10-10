# RavenGun — takenlijst

Leidraad van de eigenaar (2026-10-10): alle games in de TeknoParrot-map werken zo goed mogelijk met onze lightgun, en er is één app (Qt, Engels, in de stijl van Dolphin) die alles regelt, ook in Steam Game Mode. Deze lijst wordt bijgewerkt na elke afgeronde stap. Status: [ ] open, [~] bezig, [x] klaar.

## Besluiten (2026-10-10)

- De engine (ravengund) blijft een achtergrondservice; de Qt-app start, stopt en bedient hem. De app is het enige wat de gebruiker ziet.
- De webpagina blijft alleen als afstandsbediening voor de telefoon; aan/uit in de app.
- Licentie: GPL-3.0-or-later (LICENSE). Daarmee mag code uit Dolphin (GPL-2.0-or-later) en OpenParrot (GPL-3.0) worden overgenomen, met bronvermelding. De DemulShooter-plugin (geen licentie) wordt niet meegeleverd; vervangen door een eigen plugin.
- RavenGun vervangt de TeknoParrot-UI voor de gebruiker (bibliotheek, opties, invoer, starten). De TeknoParrot-runtime (loaders en cores uit het abonnement van de eigenaar) blijft een afhankelijkheid, zoals een frontend een emulatorcore nodig heeft; de gebruiker opent TeknoParrotUi niet meer.
- App-teksten in het Engels; code-commentaar en interne docs blijven Nederlands.
- Eerste gebruik: inrichten in Desktop Mode met een wizard (rechten, Moves koppelen, kalibratie, gamemap toevoegen, toevoegen aan Steam); daarna werkt alles na een herstart ook in Game Mode via de fullscreen-modus van de app.

## Spoor A — Qt-app

- [ ] A1 Architectuur en skelet: Qt 6 Widgets (gelijk aan de systeem-Qt 6.11 van SteamOS), hoofdvenster naar het voorbeeld van Dolphin (gamelijst/raster, werkbalk Play/Stop/Config/Controllers/Add Folder/Fullscreen), praat met de engine via de bestaande IPC-socket.
- [ ] A2 Gamebibliotheek: "Add Folder" scant een map en herkent alle games (TeknoParrot-profielen, Unity-route, overige), met iconen en status per game.
- [ ] A3 Per game "Properties" (rechtsklik > Edit): alle opties uit packaging/game-options.json (resolutie, venster/volledig scherm, free play, debug, enz.), invoerroute, en "memory card" (spelstand/NVRAM bekijken en wissen).
- [ ] A4 Controllers: Player 1 t/m 4 met keuze None / Mouse / PS Move; per Move alle instellingen (koppelen, LED, trillen, richten, knoppen, kalibratie, batterij); Navigation Controller.
- [ ] A5 Instellingen: engine starten/stoppen, telefoon-webpagina aan/uit, OLED-bescherming, paden.
- [ ] A6 Eerste-gebruik-wizard (Desktop Mode): rechten installeren (udev/Bluetooth-hulp via één beheerdersprompt), Moves koppelen, kalibratie, gamemap, toevoegen aan Steam.
- [ ] A7 Fullscreen-modus voor Steam Game Mode: zwarte achtergrond, game-iconen, kiezen en starten met de lightgun; terug na afsluiten van de game.
- [ ] A8 Steam-integratie: app als niet-Steam-game toevoegen (shortcuts.vdf) met fullscreen-argument; controleren in Game Mode.
- [ ] A10 Zonder TeknoParrot waar dat kan: per game de eigen route (Unity-plugin; OpenParrot-code (GPL-3) voor Taito Type X2; Raw Thrills native op Linux met eigen I/O), en de TeknoParrot-runtime alleen nog waar de core dongle/keychip/integriteit afhandelt (dat bouwen we niet zelf na). Vragen 8-10 aan het TeknoParrot-team staan in findings/vragen.txt.
- [ ] A9 Verpakking voor GitHub: bouwen, installeren, AppImage of vergelijkbaar; README voor nieuwe gebruikers ("downloaden, starten, werkt").

## Spoor B — games

Bronnen: docs/research/findings-digest.md (per game platform, board, route, moeite) en packaging/games-status.md.

- [x] Halo, Terminator, Tomb Raider, Rabbids, Time Crisis 5, Transformers: starten en zijn speelbaar (deels nog niet met de echte Move bevestigd).
- [x] B1 JVS-board in de IO-brug (kern, borden LGS en SegaGun2P, pty als COM-poort, 119 tests). Let's Go Safari zelf geparkeerd: de game wacht op een antwoord op 127.0.0.1:40100 (Sega PCP) vóór hij COM4 opent; vraag 11 aan het TeknoParrot-team.
- [~] B2 Eigen Unity-plugin (Tomb Raider, Rabbids) ter vervanging van de DemulShooter-plugin.
- [ ] B3 Overige games uit de backups (docs/research/findings-digest.md; 36 titels, 6 al speelbaar):
  - [x] B3a tp-shm-indelingen MusicGunGun2Pipe, HOTDSDPipe en FarCryPipe toegevoegd (bytes exact getest); offscreen-regel: binnen het scherm blijven 1-byte-assen op 2..253 (`offscreen_margin`), 0/255 alleen als de speler buiten het scherm mikt (getest).
  - [ ] B3b Taito Type X2 via OpenParrot: Gaia Attack 4, Haunted Museum, Haunted Museum II, Music GunGun 2 (JVS-pipe op COM2 voor munt/test/service).
  - [ ] B3c WartranTroopers/TC5-indeling: Gashaaaan, Gashaaaan Refill, Lethal Enforcers 3, Otomedius, Omatsuri Ganso Takoyaki, Omatsuri Kingyo Sukui, Infinity Blade FX, Raccoon Rampage, NERF Arcade, Pigs Might Fly.
  - [ ] B3d Raw Thrills Linux: GhostBusters, Go Go Strike (evdev); Galaga Assault en Pacman Chomp-Mania (herschreven ELF, zelfde klasse als Jurassic Park; DirectInput).
  - [ ] B3e Sega Ring/JVS (na B1): Let's Go Island, Let's Go Island 3D, Operation G.H.O.S.T., Medaru No Gunman.
  - [ ] B3f House of the Dead: Scarlet Dawn (ALLS, AM Daemon, geheugeneisen), Rainbow Bombergirl (touch via muis), Rampage (Unity zonder BepInEx).
  - [ ] B3g Geen lightgun: Mario Kart Arcade GP DX (2 versies), Mighty Moto — laagste prioriteit.
- [ ] B4 Jurassic Park: zwart beeld/crash (geparkeerd; nieuwe aanknopingspunten uit antwoorden.md).
- [ ] B5 Luigi's Mansion: COM1 + LMAIO-flashstap + board; de Capcom-afscherming zelf wordt niet omzeild.
- [ ] B6 Speler 2..4 per game (bindingen, tweede JVS-node voor Halo).

## Spoor C — met de eigenaar

- [ ] C1 TC5, Rabbids, Transformers met de echte Move spelen.
- [ ] C2 Tweede Move koppelen (via de app).
- [ ] C3 Kompasverstoring van 2026-10-09 (drift bij Tomb Raider) uitzoeken.
