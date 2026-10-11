# Loskoppelen van TeknoParrot: wat er nodig is (stand 2026-10-11)

Feitelijk overzicht voor het team. Per onderdeel: wat TeknoParrot nu levert, wat RavenGun daarvan al zelf doet, en wat
nodig is om het te vervangen. Bronnen: de RavenGun-code (`scripts/launch-direct.py`, `scripts/play-halo.sh`,
`scripts/play-unity.sh`, `packaging/io-routes.json`), de antwoorden van Johan (`findings/antwoorden*.md`) en
`docs/research/zonder-teknoparrot.md`. Vragen 17 t/m 22 aan het TeknoParrot-team zijn beantwoord in `findings/antwoorden_ronde4.md`: de soorten aanpassingen van de kern (11 categorieën, geen lijst per game; oogsten door te starten), Terminator, Transformers: Shadows Rising en Raccoon Rampage hebben alleen invoer en aanpassingen nodig, Go Go Strike is een abonneetitel, de openbare OpenParrot-bron (GPL-3.0) is gelijk aan de lader in de runtime, `pipehelper.exe` zit niet in de bron, het draadprotocol van de Raw Thrills-gunborden is niet gedocumenteerd (vraagt een USB-opname).

Projectafspraak die hier geldt: dongle-, keychip-, regio-, integriteits- en abonnementsafhandeling bouwt RavenGun niet.
Die onderdelen staan hieronder wel genoemd (zodat het overzicht compleet is), maar zonder uitwerking.

## 1. Wat RavenGun nu nog van TeknoParrot gebruikt

| # | Onderdeel van TeknoParrot | Waarvoor | Al vervangen? |
|---|---|---|---|
| 1 | TeknoParrotUi (frontend) | bibliotheek, opties, invoer, starten | Ja, door de RavenGun-app. Alleen nog vangnet als de directe start niet kan, en standaard voor games zonder `"launch": "direct"` |
| 2 | `GameProfiles/`, `UserProfiles/`, `Metadata/`, `Icons/`, `AndroidLaunchRecipes/` | gamegegevens: exe, 32/64-bit, emulatortype, opties, invoerindeling | Deels: `packaging/game-options.json` (564 opties), `games-notes.json`, `io-routes.json` |
| 3 | Laders: `OpenParrotLoader(64).exe`, `OpenParrotKonamiLoader.exe` | game bevroren starten, kern injecteren, doorstarten | Nee |
| 4 | Laders: `ElfLdr2/BudgieLoader(_x64).exe`, `TeknoParrot/BudgieLoader.exe`, `N2/BudgieLoader.exe` | Linux-ELF (Raw Thrills) of Lindbergh/N2 onder Windows/Wine draaien | Nee |
| 5 | Open kern: `OpenParrot(64)` (GPL-3.0, github.com/teknogods/OpenParrot) | I/O en patches voor o.a. Taito Type X2 | Nee |
| 6 | Gesloten kern: `TeknoParrot/TeknoParrot(64).dll` | I/O, compatibiliteitspatches per game, en de beschermingsafhandeling | Alleen de I/O-kant: RavenGun schrijft de invoer |
| 7 | `pipehelper.exe` | named pipe `\\.\pipe\TeknoParrot_JVS` in Wine koppelen aan een TCP-poort | Nee (wij gebruiken de exe uit de runtime) |
| 8 | `~/.local/share/TeknoParrotUI/prefixes/shared/wine` | Wine-prefix waarin de activering van TeknoParrot staat | Nee, zolang 6 nodig is |
| 9 | `~/.local/share/TeknoParrotUI/proton/GE-Proton11-7` | Proton voor Tomb Raider en Rabbids | Geen TeknoParrot-code; staat alleen in die map |
| 10 | Formaten: `TeknoParrot_JvsState` (64 bytes), `teknoparrot.ini`, profiel-XML | afspraken tussen UI, lader en kern | Nagebouwd in RavenGun (geen bestanden van TP) |
| 11 | `FFBBlaster` (force feedback) | recoil/rumble | Ja, eigen rumble per schot |

Niet (meer) van TeknoParrot: Wine (Kron4ek wine-proton 11.0-2 in `~/.local/share/ravengun/wine/`), de invoer (de
RavenGun-engine), en Tomb Raider/Rabbids (eigen BepInEx-plugin, geen TP-onderdelen).

## 2. Wat een game van TeknoParrot krijgt, per soort taak

Uit `findings/09_how_teknoparrot_runs_lightgun_games.md`, `12_aanpassingen_en_koppelingen.md`,
`19_loader_analysis.md` en `antwoorden_ronde2.md` (vraag 10):

1. **Starten (lader).** Game bevroren starten, kern laden op het startpunt van de game ("parked entry point", de
   Wine-route), dan doorlopen. Instelbaar met `TP_GAME_WORKING_DIRECTORY`, `TP_REMOTETHREAD`,
   `TP_POSTSTART_REMOTETHREAD_MS`, `TP_ENTRYPOINT_REMOTETHREAD_MS`, `TP_LOADER_MANAGED_INIT`. De bron van
   OpenParrotLoader is open (GPL-3).
2. **Invoer (I/O).** Twee transporten: het gedeelde geheugen `TeknoParrot_JvsState` (111 van 172 profielen hebben
   alleen dit nodig) en JVS over een nagebootste COM-poort of pipe (55 profielen, o.a. de Sega JVS-familie).
3. **Compatibiliteitspatches per game.** Voorbeelden uit de open bron: "Disable Gun Writes" (Type X2), volume-write
   uitzetten (Analog0-conflict), toetsenbord uitzetten, JVS-handle open houden, crosshairs tekenen in D3D9, drive `Y:`
   omleiden, assen in de gamestructuur schrijven (Friction). Voor de gesloten kern zijn deze patches per game niet
   gedocumenteerd.
4. **Diensten die een game op de pc verwacht.** Bijvoorbeeld 127.0.0.1:40100 bij de Sega Ring-games
   (`antwoorden_poort40100.md`).
5. **Bescherming en licentie.** Dongle (regio/PCB, HASP), keychip, regiokeuze, integriteitscontrole, kaartsystemen en
   de abonnementscontrole (`Patreon=true` in het profiel). Volgens Johan: "gebruik de starter voor alles in die
   tabel". Buiten de scope van RavenGun (zie boven).

## 3. Wat nodig is per onderdeel

| Onderdeel | Nodig om te vervangen | Bekende bron | Status RavenGun |
|---|---|---|---|
| Frontend | niets meer | — | klaar |
| Gamegegevens | eigen gamedatabase met exe, architectuur, emulatortype, opties, invoerindeling per game; eigen iconen | `GameProfiles`/`Metadata` als referentie (gebruiksvoorwaarden: vraag 21) | deels |
| Lader (Windows-games) | eigen lader met entry-point-injectie, gebouwd uit de OpenParrot-bron | OpenParrot (GPL-3, verenigbaar met onze GPL-3.0-or-later) | nee; mingw-toolchain in een container nodig |
| `pipehelper.exe` | eigen helper (Windows-exe) die de named pipe aan een socket koppelt | protocol bekend uit ons eigen gebruik | nee; klein |
| I/O (gedeeld geheugen) | een eigen hook-DLL per game die de invoer levert waar de kern dat nu doet | indelingen in `packaging/tp-shm-profiles.json`, `findings/06`, `07` | invoer zelf klaar; levering in de game nog via de kern |
| I/O (JVS) | JVS-responder op een pty/COM-poort | `src/ravengun/iobridge/jvs.*` | klaar, getest via de kern |
| I/O (HID-bord) | virtueel bord | `hid_board.*` | klaar (Transformers) |
| Compatibiliteitspatches | per game een lijst van wat de kern patcht en waarom, voor de exacte exe-versie | open bron voor Type X2 (4 games); voor de rest: vraag 17 | nee |
| Diensten (bv. poort 40100) | eigen dienst die antwoordt zoals de game verwacht | `antwoorden_poort40100.md` | nee |
| Raw Thrills (Linux-ELF) | native draaien zonder ElfLdr2: eigen `LD_PRELOAD`-bibliotheek voor de I/O, de bibliotheken van de game (PhysX, FMOD) uit de eigen installatie van de gebruiker, de paden die de game verwacht | `antwoorden_ronde2.md` vraag 9; protocol van het gunbord onbekend (vraag 20) | eerder geprobeerd, geparkeerd |
| Unity-games | eigen BepInEx-plugin per game | `plugins/unity/` | klaar voor Tomb Raider en Rabbids; NERF zou dezelfde route kunnen volgen |
| Wine-prefix en Proton | eigen prefix en Proton in de RavenGun-map | — | eenvoudig; prefix met TP-activering blijft nodig zolang de gesloten kern gebruikt wordt |
| Bescherming en licentie | — | — | buiten scope; per game blijft dit van TeknoParrot (of de rechthebbende) afhangen |

## 4. Per game in onze bibliotheek

Platform en route uit `packaging/io-routes.json` en `packaging/games-status.md`; afhankelijkheid van bescherming uit
`antwoorden_ronde2.md` vraag 10 en `findings/11_hardware_patches_dongles.md` (alleen of het veld er is, niet hoe).

| Game | Nu | TP-onderdelen nu | Beschermingsveld in profiel |
|---|---|---|---|
| Halo: Fireteam Raven | speelbaar, direct | ElfLdr2-lader, gesloten kern, prefix | ja (HASP-bestand, integriteitscontrole, Patreon) |
| Terminator Salvation | speelbaar, direct | ElfLdr2-lader, gesloten kern, prefix | geen (antwoorden_ronde4 vraag 18) |
| Time Crisis 5 | speelbaar, direct | OpenParrotLoader, gesloten kern, pipehelper, prefix | ja (HASP) |
| Transformers: Shadows Rising | speelbaar, direct | OpenParrotLoader, gesloten kern, prefix | geen (antwoorden_ronde4 vraag 18) |
| Go Go Strike | speelbaar, direct | lader, gesloten kern, prefix | Patreon=true (antwoorden_ronde4 vraag 18) |
| Raccoon Rampage | speelbaar, direct (2026-10-11; standaardroute van de lader, kalibratie 5x5 in testmenu) | lader, gesloten kern, prefix | geen, Patreon=false (antwoorden_ronde4 vraag 18) |
| Tomb Raider Arcade | speelbaar | geen (alleen Proton uit de TP-map) | — |
| Rabbids Hollywood | speelbaar | geen (alleen Proton uit de TP-map) | — |
| NERF Arcade | speelbaar, direct (2026-10-11; standaardroute van de lader) | lader, gesloten kern | Patreon=true |
| Gashaaaan, Gashaaaan Refill | speelbaar, direct (2026-10-11, standaardroute van de lader) | lader, gesloten kern | Gashaaaan Refill: Patreon=true |
| Otomedius, Lethal Enforcers 3, Omatsuri ×2, Infinity Blade FX | geparkeerd (32-bit kern start nu wel; per game een eigen oorzaak: films, ACIO op COM2, activering, ontbrekende bestanden, Kaboom-bibliotheek) | lader, gesloten kern | Otomedius: Patreon=true |
| Haunted Museum 1/2, Gaia Attack 4, Music GunGun 2 (Type X2) | geparkeerd (crash opgelost met een lokale ladercopie; zwart beeld na de JVS-featurecheck) | OpenParrot (open kern) | regiokeuze |
| Jurassic Park | geparkeerd | ElfLdr2, gesloten kern | ja (integriteitscontrole) |
| Let's Go Safari, Sega Ring-games | geparkeerd (poort 40100) | lader, gesloten kern | deels (dongle regio/PCB) |
| Luigi's Mansion | speelbaar, direct (2026-10-10) | lader, gesloten kern | Capcom-packer op de exe |

## 5. Volgorde van werk als het team besluit los te koppelen (feitelijk: afhankelijkheden)

1. Gamedatabase en eigen prefix/Proton: geen afhankelijkheid van anderen.
2. Eigen `pipehelper` en eigen lader uit de OpenParrot-bron: nodig voor alles daarna; vraagt een mingw-build.
3. Open kern (OpenParrot) zelf bouwen: daarmee zijn de Type X2-games los van de TeknoParrot-distributie.
4. Per game zonder beschermingsveld: eigen hook-DLL voor I/O plus de compatibiliteitspatches. Vraagt per game de lijst
   uit vraag 17.
5. Raw Thrills native: vraagt het gunbordprotocol (vraag 20).
6. Games met een beschermingsveld: blijven van TeknoParrot afhangen, tenzij de rechthebbenden iets anders aanbieden
   (vraag 22).
