# Eigen runtime: inventaris van wat RavenGun nog van TeknoParrot gebruikt (stand 2026-10-11)

Doel van de eigenaar: zo min mogelijk TeknoParrot (TP) gebruiken en alles wat geen beveiliging is zelf maken. Dit document
zet per game op een rij welke TP-onderdelen nog gebruikt worden, waarvoor, en wat RavenGun daarvan zelf kan of al doet.

Methode: alleen lezen. Bronnen: `patches/*/game.ini` en `patches/_defaults/*.ini`, `scripts/launch-direct.py`,
`scripts/tp-add-game.py`, `scripts/play-halo.sh`, `scripts/play-unity.sh`, `docs/research/*.md`, `docs/decisions.md`,
`packaging/*.json`, de bestandsnamen en `GameProfiles/*.xml` van de runtime
(`~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/`) en de NAS-antwoorden zoals die in onze docs zijn
verwerkt. Er is niets gestart, gemeten of gewijzigd; waar een feit niet in onze docs of metingen staat, staat er
"onbekend, te meten".

Afspraak die hier geldt (decisions.md): dongle-, keychip-, regio-, integriteits-, activerings- en abonnementsafhandeling
bouwt RavenGun niet. Voor games waar onze docs dat als de reden noemen, staat hieronder alleen dat de gesloten kern
nodig blijft voor de beveiligingsafhandeling; er staat geen uitwerking bij.

Afkortingen in de tabel:

- `L-OP32` = `OpenParrotWin32/OpenParrotLoader.exe`, `L-OP64` = `OpenParrotx64/OpenParrotLoader64.exe` (open bron, GPL-3.0).
- `K-TP32` = `TeknoParrot/TeknoParrot.dll`, `K-TP64` = `TeknoParrot/TeknoParrot64.dll` (gesloten kern, door de OpenParrot-lader als eerste argument geladen).
- `K-OP32` = `OpenParrotWin32/OpenParrot.dll` (open kern, GPL-3.0).
- `B32` = `ElfLdr2/BudgieLoader.exe` met `ElfLdr2/TeknoParrot.dll`; `B64` = `ElfLdr2/x64/BudgieLoader_x64.exe` met `ElfLdr2/x64/TeknoParrot64.dll` (gesloten; Linux-ELF onder Wine). Hoort bij de map `ElfLdr2/` met `libs/`, `env/`, `hints.dat` en msys-dll's.
- shm = `/dev/shm/TeknoParrot_JvsState` (64 bytes), door `pipehelper.exe` in de Wine-prefix zichtbaar gemaakt.

## 1. Tabel per game

De 35 mappen in `patches/` (zonder `_defaults`). "Soort" is `Type` uit `game.ini`; staat die er niet, dan is het soort
uit `EmulatorType` van het sjabloon `GameProfiles/<P>.xml` afgeleid en gemarkeerd met (profiel). "Patreon" is het veld uit
het meegeleverde sjabloon (leeg = het veld bestaat niet in het sjabloon). "TPUI nodig" volgt `[Launch] Mode`: `direct` =
nee (scripts/launch-direct.py), leeg of `tpui` = ja (TeknoParrotUi is dan het hoofdproces).

### 1a. Lader, kern, route en wie het bord nabootst

| Game | Soort | Lader | Kern | Pipehelper | Invoerroute (indeling / bord) | Wie bootst het I/O-bord na |
|---|---|---|---|---|---|---|
| 3DCosplayMahjong | TeknoParrot (profiel; EmulationProfile TaitoTypeXGeneric) | L-OP32 | K-TP32 | ja (via TPUI) | niet ingesteld | onbekend, te meten (de kern, zodra een route is gekozen) |
| TankTankTank | ElfLdr2 (profiel; DeadHeat) | B32 | ElfLdr2/TeknoParrot.dll | ja (via TPUI) | niet ingesteld | onbekend, te meten |
| GaiaAttack4 | OpenParrot | L-OP32 (lokale gepatchte kopie, MXCSR) | K-OP32 | ja | tp-shm (MusicGunGun2Pipe); de game spreekt echt JVS over de pipe | RavenGun: `JvsPipeServer` in launch-direct.py (standaard JVS-bord, `JvsPipeBoard = true`); de open kern doet zelf een deel van de patches (`AppliedBy = core`) |
| HauntedMuseum | OpenParrot | L-OP32 (gepatcht) | K-OP32 | ja | tp-shm (MusicGunGun2Pipe), JVS over de pipe | idem |
| HauntedMuseumII | OpenParrot | L-OP32 (gepatcht) | K-OP32 | ja | tp-shm (MusicGunGun2Pipe), JVS over de pipe | idem |
| MusicGunGun2 | OpenParrot | L-OP32 (gepatcht) | K-OP32 | ja | tp-shm (MusicGunGun2Pipe); de game opent de JVS-pipe niet | RavenGun/open kern (idem); game komt niet verder dan een D3D-foutvenster |
| GalagaAssault | ElfLdr2 | B32 | ElfLdr2/TeknoParrot.dll | ja | niet ingesteld (indeling RawThrillsGUN) | de kern/ElfLdr2 (leest shm); RavenGun schrijft alleen shm. Draadprotocol van het Raw Thrills-gunbord: onbekend (USB-opname nodig) |
| GhostBusters | ElfLdr2 | B32 | ElfLdr2/TeknoParrot.dll | ja | niet ingesteld (RawThrillsGUN) | de kern/ElfLdr2 |
| JurassicPark | ElfLdr2 | B32 | ElfLdr2/TeknoParrot.dll | ja | evdev (RawThrillsGUN), dus via TeknoParrotUi | TeknoParrotUi leest evdev en vult shm; de kern vertaalt |
| PacmanChomp | ElfLdr2 | B32 | ElfLdr2/TeknoParrot.dll | ja | niet ingesteld (RawThrillsGUN) | de kern/ElfLdr2 |
| Terminator | ElfLdr2 | B32 | ElfLdr2/TeknoParrot.dll | ja | tp-shm (RawThrillsGUN) | de kern/ElfLdr2 (RavenGun schrijft shm) |
| GoGoStrike | ElfLdr2 | B32 | ElfLdr2/TeknoParrot.dll | ja | tp-shm (eigen indeling GoGoStrike) | de kern/ElfLdr2 |
| Halo | ElfLdr2 | B64 | ElfLdr2/x64/TeknoParrot64.dll | ja | tp-shm (WartranTroopers) | de kern/ElfLdr2 |
| Gashaaaan | TeknoParrot | L-OP32 | K-TP32 | ja | tp-shm (WartranTroopersKonami) | de kern (RavenGun schrijft shm) |
| Gashaaaan2 | TeknoParrot | L-OP32 | K-TP32 | ja | tp-shm (WartranTroopersKonami) | de kern |
| HOTDSD | TeknoParrot (twee exe's: amdaemon.exe, dan de game) | L-OP64 | K-TP64 | ja (via TPUI) | evdev; knoppen als naambindingen in UserProfiles | TeknoParrotUi leest evdev; de kern vertaalt. Bitindeling van het stuurwoord: onbekend (vraag 77) |
| InfinityBladeFX | TeknoParrot | L-OP32 | K-TP32 | ja (via TPUI) | tp-shm (TimeCrisis5) | de kern |
| LethalEnforcers3 | TeknoParrot | L-OP32 | K-TP32 | ja (via TPUI) | tp-shm (WartranTroopers) | de kern |
| LGI | TeknoParrot | L-OP32 | K-TP32 | ja | jvs over pipe (bord LGI, COM4) | RavenGun: `JvsPipeServer` met bord uit `packaging/jvs-boards.json` |
| LGI3D | TeknoParrot | L-OP32 | K-TP32 | ja | jvs over pipe (bord LGI3D, COM4) | RavenGun (idem) |
| LGS | TeknoParrot | L-OP32 | K-TP32 | ja (via TPUI) | jvs via pty als COM4 (bord LGS) | RavenGun: de daemon is het JVS-bord achter een pty (dialect jvs); wielwaarde en knoppen: zie 2 |
| LuigisMansion | TeknoParrot | L-OP64 | K-TP64 | ja | tp-shm (LuigisMansion) | de kern (volgens onze docs neemt de 64-bit kern ook de COM-poort naar het bord over) |
| MedaruNoGunman | TeknoParrot (EmulationProfile FarCry) | L-OP32 | K-TP32 | ja | tp-shm (FarCryPipe) | de kern |
| NERF | TeknoParrot | L-OP64 | K-TP64 | ja | tp-shm (WartranTroopers) | de kern en de TP-BepInEx-plugin (`NERFTPPlugin.dll`, Harmony-postfix op `UpdateInput_Unity`); waar de plugin zijn waarden vandaan haalt: onbekend |
| OmatsuriYasanGansoTakoyaki | TeknoParrot | L-OP32 | K-TP32 | ja (via TPUI) | tp-shm (WartranTroopers) | de kern |
| OmatsuriYasanKingyoSukui | TeknoParrot | L-OP32 | K-TP32 | ja (via TPUI) | tp-shm (WartranTroopers) | de kern |
| Otomedius | TeknoParrot | L-OP32 | K-TP32 | ja (via TPUI) | tp-shm (WartranTroopers); de game wacht op een ACIO-micom op COM2 | niemand nu. De ACIO-responder is gespecificeerd (research, 3.2) maar niet gebouwd; of de 32-bit kern ACIO doet: onbekend, te meten (Johan zegt ja voor de 64-bit kern, onze meting zegt nee voor de 32-bit) |
| PigsMightFly | TeknoParrot | L-OP32 | K-TP32 | ja | tp-shm (WartranTroopers) | de kern |
| RabbidsHollywood | Unity | geen (Proton/Wine met BepInEx, `play-unity.sh`) | geen | nee | unity-tcp | RavenGun (eigen BepInEx-plugin `plugins/unity/`, daemon is de TCP-klant) |
| TombRaider | Unity | geen | geen | nee | unity-tcp | RavenGun (idem) |
| RaccoonRampage | TeknoParrot | L-OP64 | K-TP64 | ja | tp-shm (WartranTroopers) | de kern |
| RainbowBombergirl | TeknoParrot | L-OP64 | K-TP64 | ja | tp-shm (WartranTroopers); de game start met een aanraking van het scherm | de kern en de TP-BepInEx-plugin (`BBGTPPlugin`) |
| Rampage | TeknoParrot | L-OP64 | K-TP64 | ja | tp-shm (WartranTroopers, drie spelers) | de kern |
| TC5 | TeknoParrot | L-OP64 | K-TP64 | ja | tp-shm (TimeCrisis5; munt via toetsenbord Shift+T) | de kern |
| TransformersShadowsRising | TeknoParrot | L-OP32 | K-TP32 | ja | hid-board (bord SaeGunBoard) | RavenGun: virtueel HID-gunbord uit `packaging/hid-boards.json` (alleen de DirectInput-gun). De kern is wel geladen; of hij nodig is: onbekend, te meten |

Opmerking pipehelper: `launch-direct.py` start `pipehelper.exe` bij elke TP-game (ook als de pipe leeg blijft); die maakt
het gedeelde geheugen in de prefix en de named pipe `TeknoParrot_JVS`. Zonder TPUI en zonder pipehelper zou de kern geen
shm zien. Alleen de twee Unity-games gebruiken hem niet.

### 1b. Andere TP-bestanden, TeknoParrotUi, Patreon, status

| Game | Andere TP-bestanden die nu gebruikt worden | TPUI nodig | Patreon (sjabloon) | Status |
|---|---|---|---|---|
| 3DCosplayMahjong | GameProfiles/UserProfiles-XML; `ParrotData.xml` (algemeen) | ja (Mode leeg) | leeg | geen status in game.ini (onbekend, te meten) |
| TankTankTank | idem; `TP_NUSOUND=1` uit het profiel | ja (Mode leeg) | leeg | geen status in game.ini |
| GaiaAttack4 | UserProfiles-XML; `teknoparrot.ini` en `MINIGUN.INI` (schrijft launch-direct.py in de gamemap); lokale lader-kopie | ja (Mode leeg) | leeg | parked (zwart beeld na de JVS-functiecheck) |
| HauntedMuseum | idem met `MUSEUM.INI` | ja | leeg | parked (idem) |
| HauntedMuseumII | idem met `HAUNTED2.INI` | ja | leeg | parked (idem) |
| MusicGunGun2 | idem, geen gun-INI | ja | leeg | parked (D3D-foutvenster) |
| GalagaAssault | `ElfLdr2/libs` (o.a. PhysX, FMOD, libav*), `ElfLdr2`-link in de view, `hints.dat` (volgens Johan geen rol voor ons) | ja | false | parked (andere blokkade: ontbrekende texturen; Johan rekent hem bij JP) |
| GhostBusters | idem | ja | false | parked ("GAME PATH CHECK FAILED") |
| JurassicPark | idem | ja | false | parked (ElfLdr2 leest alleen sectiekoppen; geen eigen oplossing gepland) |
| PacmanChomp | idem | ja | true | parked (zelfde klasse als JP) |
| Terminator | idem | nee (Mode direct) | leeg | playable (met gamescope) |
| GoGoStrike | idem | nee | true | playable (gamescope; 4:3) |
| Halo | idem; `ElfLdr2/x64` | nee* | true | playable |
| Gashaaaan | UserProfiles-XML; `teknoparrot.ini` | nee | false | playable |
| Gashaaaan2 | idem | nee* | true | playable |
| HOTDSD | UserProfiles-XML en `.env`; `amdaemon.exe` en game (TP-versies in de gamemap); `WineRunnerPath` in het profiel | ja (Mode tpui; directe start met twee exe's niet gebouwd) | leeg | playable via TPUI |
| InfinityBladeFX | `TPFIX.ini` (zou vooraf te plaatsen zijn, nu eerste-start-dialoog); wine-mono | ja | leeg | parked (Kaboom `InitLib` failed) |
| LethalEnforcers3 | UserProfiles-XML | ja | false | parked (geen 32-bit GStreamer voor `.m1v`) |
| LGI | UserProfiles-XML; kern schrijft `SBUH_sram.bin` in de prefix (onze meting); eigen prefix-kopie | nee | leeg | playable |
| LGI3D | idem | nee | leeg | playable |
| LGS | UserProfiles-XML met de toetsbindingen (`[Bindings]` in game.ini is de bron); eigen prefix | ja (Mode leeg; speelbaar "via TeknoParrotUi") | leeg | playable |
| LuigisMansion | UserProfiles-XML; herstartmarkers `.\ForceRestart` e.d. (door de kern opgemerkt) | nee | leeg | playable |
| MedaruNoGunman | kern maakt `SBTF_sram.bin` (2 MB) en `SBTF_e2prom.bin` in de prefix (onze meting); `d3dx9_40` | nee | leeg | parked (3001/3003 RAM-fout bij verse start) |
| NERF | `NERFTPPlugin.dll` (BepInEx, in de gamemap); DXVK-prefix | nee* | true | playable |
| OmatsuriYasanGansoTakoyaki | UserProfiles-XML | ja | true | parked (wit venster) |
| OmatsuriYasanKingyoSukui | idem; schijfletter-herleiding `.\TeknoParrot\<L>\` (volgens Johan, 64-bit kern) | ja | true | parked (exitcode 8) |
| Otomedius | UserProfiles-XML | ja | true | parked (ACIO op COM2) |
| PigsMightFly | UserProfiles-XML; eigen prefix met `C:\Sega` | nee* | true | playable |
| RabbidsHollywood | geen TP-code; Proton staat in `~/.local/share/TeknoParrotUI/proton/` (alleen opslagmap) | nee | leeg | playable |
| TombRaider | idem | nee | leeg | playable |
| RaccoonRampage | UserProfiles-XML; DXVK-prefix | nee | false | playable |
| RainbowBombergirl | `BBGTPPlugin` (BepInEx); GStreamer-plugins zijn niet van TP | nee* | true | parked (na munt en start geen verloop) |
| Rampage | UserProfiles-XML; DXVK-prefix | nee | leeg | playable (geen lightgun-game) |
| TC5 | UserProfiles-XML; `OPENSSL_ia32cap` (zet launch-direct.py zoals TPUI) | nee | leeg | playable |
| TransformersShadowsRising | UserProfiles-XML; eigen prefix | nee | leeg | playable |

\* Mode = direct, maar bij Patreon = true start `launch-direct.py` alleen als TeknoParrotUi de activering eenmalig in de
prefix heeft gezet (`check_activation`; de waarde lezen we niet). Ook voor alle andere direct-games geldt: het
gebruikersprofiel `UserProfiles/<P>.xml` bestaat (gemaakt door `tp-add-game.py` uit `GameProfiles/<P>.xml`) en heeft
dezelfde `GameProfileRevision` als het sjabloon, anders vraagt het script om eenmaal TeknoParrotUi te starten.
Of de kern bij een niet-Patreon-game iets van de activering wil: onze docs zeggen "nog niet op de tv gemeten".

## 2. Per onderdeel

Gedrag alleen zoals beschreven in onze docs (loskoppelen-teknoparrot.md, antwoorden-tp-ronde5-22.md,
antwoorden-tp-ronde23-en-jp.md, teknoparrot-io.md, decisions.md) en Johans antwoorden daarin.

| Onderdeel | Wat het nu voor ons doet | Kan RavenGun het zelf? Wat vraagt dat? | Wat er al is |
|---|---|---|---|
| **OpenParrot-lader** (`OpenParrotLoader(64).exe`) | Start de game bevroren, laadt de kern, springt door op het startpunt (de entry-point-route die voor Wine bedoeld is). Instelbaar met `TP_REMOTETHREAD`, `TP_POSTSTART_REMOTETHREAD_MS`, `TP_ENTRYPOINT_REMOTETHREAD_MS`, `TP_GAME_WORKING_DIRECTORY`. Voor de Type X2-games schrijft hij `SetThreadContext` met `0x1001F` (Wine zet dan MXCSR op 0), waardoor we een binaire lader-patch op een lokale kopie hebben. | Ja, de bron is open (GPL-3.0, verenigbaar met onze GPL-3.0-or-later; Johan: gelijk aan de lader in de runtime, niet vastgesteld of exact dezelfde build). Vraagt een mingw-toolchain (in een container; niet aanwezig op deze machine, `x86_64-w64-mingw32-gcc` ontbreekt), 32- en 64-bit bouwen. Fix voor MXCSR kan dan in de bron (0,5 tot 1 dag volgens research 2.14). | Per-game lader-patch als gegevens (`_defaults/OpenParrot.patches.ini`, `Kind = file`); `RemoteThread`-instellingen in `game.ini`; startplan in `launch-direct.py`. Geen eigen lader. |
| **Open kern** `OpenParrot(64).dll` | I/O en patches voor Taito Type X2 (4 games). Acht Type X2-adressen komen uit de open bron (`TypeX2Generic.cpp`, GPL-3.0) en staan als documentatie in `patches/*/patches.ini` (`AppliedBy = core`). | Ja, bron open (GPL-3.0); bouwen met dezelfde toolchain. Wat nog ontbreekt is geen kern maar het JVS-bord: de JVS-node `TaitoTypeX*` staat in de open TeknoParrotUi-bron en is nog niet gelezen/overgenomen. | Generiek Sega-bord `SegaGun2P` in `jvs-boards.json`; `JvsPipeServer` beantwoordt reset en adres; sense-lijn-hypothese (research 2.8) nog niet gemeten. |
| **Gesloten kern** `TeknoParrot(64).dll` (ook die in `ElfLdr2/`) | Leest shm en/of de JVS-pipe en maakt er I/O voor de game van; compatibiliteitspatches per game (11 categorieen, geen lijst per game; "oogsten door te starten"); in-proces nabootsing van platformdiensten bij Sega Ring-games (poort 40100 is geen TCP-dienst); schijfletter-herleiding naar `.\TeknoParrot\<L>\`; onvoorwaardelijke `CreateWindowExW`-hook (daarom het virtuele bureaublad bij LGI); SRAM/EEPROM-bestanden (`SBTF_*`, `SBUH_*`); herstartmarkers; ACIO-handler in de 64-bit kern. Volgens onze docs zit hier ook de beveiligings-/licentieafhandeling in (dongle, regio, integriteit, abonnement). | Alleen het niet-beveiligende deel, en alleen met een eigen hook-DLL per game (of per indelingsfamilie) die de invoer levert waar de kern dat doet, plus de compatibiliteitspatches. De lijst van die patches per game is onbekend, te meten (starten zonder kern en kijken wat breekt). Het beveiligingsdeel bouwt RavenGun niet. | Invoerkant volledig: `src/ravengun/iobridge/` (tp_shm, jvs, hid_board, unity_tcp), indelingen in `packaging/tp-shm-profiles.json` (11 stuks). Patchmachine in `launch-direct.py` + `gamesettings.py` (benoemd, met verwachte bytes, alleen op lokale kopieen). Geen hook-DLL, geen Windows-compiler. |
| **ElfLdr2 / BudgieLoader** (`B32`, `B64`, `N2/`, `TeknoParrot/BudgieLoader.exe`) | Draait Linux-ELF-games (Raw Thrills) onder Wine; levert de bibliotheken (`ElfLdr2/libs`: PhysX, FMOD 7, libav*) en de gun-/dongle-kant. Leest alleen `SHT_DYNSYM` uit sectiekoppen (ook in 1451), geen terugval op `PT_DYNAMIC`. | Gesloten en niet in onze bron. Native draaien zonder ElfLdr2 (`play-native.sh`, bubblewrap) is geprobeerd en geparkeerd (Halo komt het verst; gunbordprotocol onbekend; Terminator wil de dongle). Een eigen ELF-lader met `PT_DYNAMIC` is "dagen werk, niet gepland"; voor JP/Pac-Man/Galaga besloten we geen import-reconstructie te bouwen (decisions.md, 2026-10-11). | `play-native.sh`, `packaging/native-games.json` (alles `geparkeerd`), `tools/rt-input-shim`, `tools/fmod-stub`, symlink-views. |
| **pipehelper.exe** (`pipehelper32.exe` bestaat ook) | Windows-hulp in de prefix: maakt `\\.\pipe\TeknoParrot_JVS` en de named file mapping `TeknoParrot_JvsState`, spiegelt shm met `/dev/shm` en de pipe via TCP naar de host. Niet afgeschermd; zit niet in de open bron. | Ja, klein. Vraagt een Windows-binary (mingw-toolchain) die named pipe + file mapping + TCP doet. Protocol volledig bekend (TPB1: 58+N bytes kop, antwoord `OKAY`, daarna kale stroom; research 3.1). De geauthenticeerde vorm is niet nodig: de positionele vorm werkt (onze server stuurt nooit `OKAY`). Meten: of de helper na een sluiting opnieuw verbindt. | Host-kant: `JvsPipeServer` (Python, positionele vorm) en shm-spiegeling in `launch-direct.py`; TPB1-spec in research. Eigen helper-exe: nee. |
| **ParrotData.xml** | Algemene TP-instellingen. Wij lezen er alleen een paar uit: `Elfldr2LogToFile`, `Elfldr2NetworkAdapterName` (wordt `TP_ETH`). De rest (UI-kleur, taal, update-check, Discord, hotkeys) is UI. | Ja, triviaal: twee instellingen naar `game.ini`/globale RavenGun-instellingen. | `parrot_data()` in `launch-direct.py` leest het alleen-lezen. |
| **GameProfiles/*.xml** (sjablonen) | Gamegegevens: `EmulatorType`, `EmulationProfile`, `Is64Bit`, `ExecutableName`, `HasTwoExecutables`, `Patreon`, `GameProfileRevision`, velden (opties) en invoerindeling. Bepaalt lader en kern (`resolve_loader`) en of een game onder het abonnement valt (het sjabloon, niet het gebruikersprofiel, beslist). | Gedeeltelijk al. Een eigen gamedatabase (exe, bitness, emulatortype, opties, indeling) is "deels" gedaan. Wat blijft is het `Patreon`-veld uit het sjabloon (abonnementsvoorwaarde, geen RavenGun-gegeven) en de gebruiksvoorwaarden van de sjablonen (vraag 21, niet beantwoord). Ontbreken zonder sjabloon: opties met keuzelijsten per game. | `packaging/game-options.json` (564 opties), `games-notes.json`, `patches/<Game>/game.ini` (`Type`, `EmulationProfile`, `Layout`, `Board`); `gamesettings.py`. |
| **UserProfiles/<P>.xml** (+ `.env`) | Gebruikersprofiel: pad naar de game, Input API, toetsbindingen, opties. `launch-direct.py` leest het, vraagt gelijke revisie met het sjabloon, en `tp-add-game.py` maakt het. `.env` is verouderd (nog een versie gelezen als laagste laag). Bij LGS en HOTDSD zit het bij de start (TPUI) nodig. | Ja: de inhoud kan in `patches/<Game>/game.ini` (de keuzes) en in een eigen gamepad-/invoerlaag. Vraagt: de revisiecontrole en `HasTwoExecutables`-logica nabouwen. | `game.ini` met gebruikerslaag (Dolphin-model); `[Bindings]` in game.ini; `tp-add-game.py` zet nog UserProfiles aan; `migrate-game-settings.py`. |
| **TeknoParrotUi** | Frontend: bibliotheek, opties, starten; leest evdev en vult shm/JVS-pipe (alleen bij route evdev en `Mode = tpui`); zet de activering (abonnement) in de prefix; start `pipehelper`. Zonder UI leest niemand evdev. | Frontend: al vervangen door de RavenGun-app. Starten: `launch-direct.py` doet het voor de games met `Mode = direct`. Nog niet direct: HOTDSD (twee exe's: amdaemon eerst, 5 s wachten; als gegevens in `game.ini` voorzien maar niet gebouwd), LGS (route jvs via pty nog via TPUI genoteerd), en alle geparkeerde games (nooit in directe modus beoordeeld). De activering voor Patreon-games blijft bij TPUI. | `scripts/launch-direct.py` (1285 regels), `play-halo.sh --direct`, de RavenGun-app; terugval op TPUI bij exitcode 3/4. |
| **Wine-prefix `shared`** (`~/.local/share/TeknoParrotUI/prefixes/shared/wine`) | Basis-prefix waarin TPUI de activering zet; eigen prefix-kopieen (`ravengun-test-*`) zijn daarvan afgeleid. | Ja: eigen prefix/Proton in de RavenGun-map is "eenvoudig"; blijft gebonden aan de activering zolang Patreon-games of de kern die in de prefix vragen. | `wine-prefix.sh`, `--vdesktop`, `--dxvk` in `tp-add-game.py`. Wine zelf is niet van TP (Kron4ek wine-proton 11.0-2 in `~/.local/share/ravengun/wine/`). |
| **Proton GE** (`~/.local/share/TeknoParrotUI/proton/GE-Proton11-7-x86_64`) | Alleen een opslagplek; geen TP-code. Gebruikt door `play-unity.sh` (Tomb Raider, Rabbids) en voor DXVK-bibliotheken. | Ja, triviaal: verplaatsen naar `~/.local/share/ravengun/`. | `play-unity.sh` verwijst er nu naar. |
| **Bestanden naast de game** (`teknoparrot.ini`, `MINIGUN.INI`/`MUSEUM.INI`/`HAUNTED2.INI`, `TPFIX.ini`) | Door TPUI geschreven spelinstellingen. | Ja, al gedaan voor de eerste twee; `TPFIX.ini` vooraf plaatsen staat open (IBFX). | `build_ini()` en `taito_gun_ini()` in `launch-direct.py`. |
| **FFBBlaster, ScoreSubmission, TeknoDraw, SegaApi, SDL2, cg/glut/regal enz.** (in `TeknoParrot/`) | Niet in onze docs beschreven, behalve FFBBlaster (recoil). | FFB: ja, al eigen rumble per schot. De rest: onbekend, te meten. | Eigen rumble per schot. |
| **TP-BepInEx-plugins** (`NERFTPPlugin.dll`, `BBGTPPlugin`) | Plugin in de Unity-game voor invoer; bij NERF volgens Johan ook dongle- en muntboekhouding. | Invoerdeel: ja, een eigen plugin kan dezelfde functie patchen (`AnalogGameInput`, `m_DigitalInputData`, `m_CurrentGunState`); de eigen Tomb Raider/Rabbids-plugin bewijst de route. Het dongle-/muntdeel bouwen we niet. | `plugins/unity/`, `unity_tcp`. |
| **JVS-bord (afspraken)** | Plat JVS (sync E0, escape D0), opdrachten 0x10 t/m 0x14, 0x20 t/m 0x22, 0xF1; adres-vlag in shm byte 0. | Ja, volledig eigen. | `iobridge/jvs.*`, `jvs-boards.json` (LGS, SegaGun2P, LGI, LGI3D), JVS over pty en over pipe; getest via de kern. |
| **ACIO (Konami, Otomedius)** | Micom op COM2, 57600 8N1. | Ja, eigen responder op een pty; protocol beschreven, twee aannames te meten (somregel met of zonder `0xAA`; welke productcode de game verwacht). | Alleen de spec (research 3.2); geen code. De Otomedius-game valt onder Patreon. |
| **HID-bord** (Transformers) | De game zoekt een HID-IO-bord (DirectInput). | Ja, al gedaan voor de gun. | `hid_board.*`, `hid-boards.json`; het bord is bewust niet volledig nagebouwd. |

## 3. Indeling in groepen

Voor groep (c) staat alleen de reden zoals onze docs die geven (profielveld, abonnement of beveiligingsafhandeling in de
kern). Geen uitwerking.

### (a) Kan nu al zonder TeknoParrot-binaries: 2 games

- **TombRaider**, **RabbidsHollywood** (Unity, eigen BepInEx-plugin, unity-tcp). Enige TP-afhankelijkheid: de map van
  Proton GE (geen TP-code).

### (b) Kan zonder, als RavenGun zelf onderdelen maakt: 8 games

Voorwaarde in alle gevallen: eigen lader (uit de OpenParrot-bron), eigen pipehelper en een eigen hook-DLL of open kern.
Grond voor de indeling: onze docs noemen geen beschermingsveld voor deze games (Johan, ronde 4) of de kern is open.

| Game | Wat RavenGun zelf moet maken | Open punt |
|---|---|---|
| GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2 (Type X2, open kern OpenParrot) | open kern en lader uit de OpenParrot-bron bouwen (mingw), `TaitoTypeX2`-bord in `jvs-boards.json` (node uit de open TPUI-bron), eigen pipehelper | Het zwarte beeld na de JVS-functiecheck is onopgelost; oorzaak onbekend, te meten (race op de sense-lijn is hypothese; blijkt het een beschermingscontrole dan stoppen). De docs noemen "regiokeuze" in het profiel; dat is een gewone instelling (correctie eigenaar). MGG2 heeft daarnaast een D3D-fout. |
| RaccoonRampage | eigen lader, hook-DLL voor shm-invoer (indeling WartranTroopers), compatibiliteitspatches | Lijst van wat de kern patcht: onbekend, te meten. Johan: "alleen invoer en aanpassingen" nodig. Kalibratieruimte 0..65535 te meten. |
| Gashaaaan | idem | Zelfde onbekende lijst. De 32-bit kern vraagt de standaardroute van de lader. |
| Rampage | idem (drie spelers, bitvolgorde gevraagd, vraag 77) | Beschermingsveld in onze docs niet vastgelegd: onbekend, te meten. |
| LethalEnforcers3 | idem | Geparkeerd om een niet-TP-reden (32-bit GStreamer voor `.m1v`); beschermingsveld niet vastgelegd: onbekend, te meten. |

### (c) Houdt de gesloten kern nodig: 20 games

| Game | Reden volgens onze docs |
|---|---|
| Halo, Pigs Might Fly (Patreon + packer), GoGoStrike, NERF, Gashaaaan2, Pac-Man Chomp-Mania, Rainbow Bombergirl, Otomedius, Omatsuri Ganso Takoyaki, Omatsuri Kingyo Sukui | `Patreon = true` in het sjabloon: abonnementscontrole, valt onder beveiligings-/licentieafhandeling; `launch-direct.py` eist daarom de activering van TPUI. (Halo heeft daarnaast volgens de docs een beveiligingsveld.) |
| JurassicPark, GalagaAssault | beveiligingsveld volgens loskoppelen-teknoparrot.md (pakker en integriteitscontrole); ElfLdr2 leest alleen sectiekoppen. Galaga heeft daarnaast een eigen blokkade (ontbrekende bestanden). |
| GhostBusters | ElfLdr2; de loader meldt "GAME PATH CHECK FAILED" (beveiligingsveld volgens onze eigen log-interpretatie: onbekend, te meten). |
| Terminator | ElfLdr2 (gesloten). Onze docs spreken elkaar tegen: loskoppelen-teknoparrot.md noemt geen beschermingsveld, findings-digest.md en de native-meting (exitcode 255) wel een beveiligingslaag: onbekend, te meten. |
| TC5 | beschermingsveld volgens loskoppelen-teknoparrot.md; een eigen invoer-DLL (`wajvio`-vervanger) is wel mogelijk maar haalt de gesloten kern niet weg. |
| LGS, LGI, LGI3D | beschermingsveld (dongle-/PCB-regio) volgens de docs; daarnaast in-proces platformdiensten in de kern. |
| LuigisMansion | beschermingsveld (packer op de exe) volgens de docs; de 64-bit kern neemt ook het COM-bord over. |
| HOTDSD | keychip/AM-diensten volgens de docs. |

### Onbekend, te meten: 5 games

| Game | Waarom onbekend |
|---|---|
| TransformersShadowsRising | Johan (ronde 4): geen beschermingsveld, "alleen invoer en aanpassingen". Findings-digest noemt een dongle-bibliotheek in de Sega-shell. RavenGun maakt het gunbord al na; of een start zonder kern kan: te meten. |
| MedaruNoGunman | `DongleRegion`/`PcbRegion` zijn volgens de digest een kernonderdeel voor Sega Ring-games; loskoppelen-teknoparrot.md noemt geen veld. De SRAM-bestanden (`SBTF_*`) maakt de kern. |
| InfinityBladeFX | Kaboom `InitLib` is nog niet beoordeeld; de activeringsaanvraag bij Omatsuri is niet hetzelfde spel maar de docs zetten dit onder "mogelijk beschermingsgerelateerd, nog niet beoordeeld". |
| 3DCosplayMahjong | geen route, geen status, niets gemeten. |
| TankTankTank | idem (ElfLdr2, profiel DeadHeat). |

Totaal: 2 + 8 + 20 + 5 = 35.

## 4. Voorgestelde volgorde van werk (grootste winst eerst)

Winst = aantal games dat er een TP-bestand of -proces minder door nodig heeft, gewogen met hoe weinig het kost. Alles
hieronder blijft buiten beveiligingsafhandeling.

1. **Eigen gamedatabase, geen UserProfiles/GameProfiles/ParrotData meer, en alles direct starten.** Het sjabloonveld
   `Patreon` blijft de enige afhankelijkheid. HOTDSD (twee exe's, volgorde en wachttijd staan al als besluit),
   LGS (route jvs via pty) en de geparkeerde games die nog op `Mode = tpui` (leeg) staan een directe proef geven. Betrokken: alle 33
   TP-games (TPUI uit voor HOTDSD, LGS, JurassicPark, GalagaAssault, GhostBusters, PacmanChomp, Gaia, HM, HM2, MGG2, LE3,
   IBFX, Omatsuri x2, Otomedius, Mahjong, TankTankTank). Kost: laag tot middel, geen toolchain nodig.
2. **mingw-container en eigen `pipehelper.exe`** (TPB1 is gespecificeerd; positionele vorm volstaat; meten of herverbinding
   na sluiten nodig is). Betrokken: alle 33 TP-games. Dit is ook de voorwaarde voor 3 en 4.
3. **Eigen OpenParrot-lader uit de bron, met de MXCSR-fix in de bron** (vervangt de binaire lader-patch). Betrokken: de 25
   games op `OpenParrotLoader(64)` (alle TeknoParrot- en OpenParrot-soorten, waaronder de vier Type X2-games); de 8 ElfLdr2-games niet.
4. **Open kern OpenParrot zelf bouwen en `TaitoTypeX2`-bord toevoegen** (JVS-node uit de open TPUI-bron lezen, sense-lijn-
   proef). Betrokken: GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2. Groep (b) wordt hiermee los van de TP-distributie, mits
   het zwarte beeld niet bij een beschermingscontrole blijkt te liggen (dan stoppen en melden).
5. **Eigen hook-DLL voor de shm-invoer plus meetronde "start zonder kern".** Eerst meten wat de kern per game doet (lijst
   compatibiliteitspatches, schijfletters, SRAM-bestanden, vensterhooks), dan de DLL maken voor de indelingsfamilie
   WartranTroopers. Betrokken: RaccoonRampage, Rampage, Gashaaaan, LethalEnforcers3, plus de "onbekend"-games
   TransformersShadowsRising (bord is al eigen), MedaruNoGunman, InfinityBladeFX.

Daarna (kleiner of afhankelijk van beslissingen van de eigenaar):

6. Proton GE en prefix verplaatsen naar `~/.local/share/ravengun/` (Tomb Raider, Rabbids; alle prefixen).
7. Eigen ACIO-responder op een pty (Otomedius; blijft Patreon, dus de kern blijft toch nodig) en eigen invoer-DLL voor TC5 (`wajvio`-vervanger):
   verkleint wat de kern doet maar haalt hem bij (c) niet weg.
8. Eigen BepInEx-plugin voor NERF en Rainbow Bombergirl (invoerdeel): verkleint het gebruik, maar beide zijn `Patreon = true`.
9. Raw Thrills (Halo, Terminator, GoGoStrike, GhostBusters, JP, Pac-Man, Galaga): geblokkeerd op het gunbordprotocol (USB-opname) en
   op ElfLdr2; geen werk gepland.

## 5. Wat te meten voordat groep (b) en "onbekend" vaststaan

- Per game uit (b) en "onbekend": starten zonder `TeknoParrot(64).dll` (eigen lader + eigen shm-hook), noteren wat breekt (research 4, punten 47/64).
- Type X2: aantal F1-antwoorden per run en tijdstip van `shm[0] host->wine 0->1` (research 2.8 stap 1).
- Medaru: wie de grootte van `SBTF_sram.bin` zet en welke beginwaarde de 3001/3003-fout laat verdwijnen (2.13).
- Of de gesloten kern bij een niet-Patreon-game nog iets van de activering in de prefix wil (decisions.md: "nog niet op de tv gemeten").
- Of `pipehelper` na het sluiten van de pipe opnieuw verbindt en een nieuwe TPB1-handdruk doet.
