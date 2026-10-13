# Schrijfpaden op de NAS: inventaris van alle views, in twee klassen

Gemeten op 2026-10-12 (alleen gelezen; er is niets gewijzigd, niets van de NAS verwijderd en geen game gestart).
Opnieuw te draaien: `python3 -I tools/nas-schrijfpaden-scan.py` (leesbaar), `--md` (de tabellen hieronder), `--json`.
Test: `python3 -B tests/test_nas_schrijfpaden_views.py` (laag A: de controle op een namaak-NAS; laag B: de echte views).

## 1. Besluit van de eigenaar en indeling (2026-10-12)

De games WONEN op de NAS en de lader start ze daarvandaan: dat is het ontwerp en dat blijft. Daaruit volgt:

* **Klasse (a), ontwerp, GEEN bevinding:** wat de game of de kern zelf in zijn eigen spelmap op de NAS schrijft: `Ram/`, `Log/`,
  eigen `*.ini/*.json/*.conf`, NVRAM, `<exe>.dxvk-cache` naast de exe, en het `teknoparrot.ini` dat de kern in de werkmap schrijft.
  Status: **bekend en geaccepteerd** (besluit eigenaar, 2026-10-12). Er wordt hier niets aan gedaan.
* **Klasse (b), onze artefacten, de echte bevindingen:** wat WIJ neerzetten mag nooit stilzwijgend in een spelmap op de NAS landen:
  drop-ins, patchbestanden (die horen in `~/.local/share/ravengun/patched`), onze plugin-DLL's met hun config/cache/log (de BepInEx-boom
  met onze plugin), hulpbestanden. Status per bevinding: `open` (zonder bescherming), `gedekt` (launch-direct maakt de map bij het plaatsen
  lokaal: `dropin_materialiseer`; `place_binary` weigert de NAS) of `restant` (staat er al; opruimen is aan de eigenaar).
  De afsluitcode van het script en laag B van de test hangen alleen aan klasse (b) met status `open`.
* De eigen BepInEx van een game uit de TeknoParrot-dump (bijv. Rainbow Bombergirl met `BBGTPPlugin.dll`, zonder plugin van ons) is van de game:
  klasse (a), rol `bepinex-van-de-game`. Een BepInEx-boom is van ons als het een aagames-view is of als er een artefact van ons in staat
  (`RavenGun.*.dll`, `nl.studiodamon.ravengun.*.cfg`).

## 2. Uitkomst

* **Klasse (b): geen enkele view heeft vandaag een plek waar een artefact van ons ZONDER bescherming op de NAS zou landen** (open: 0, gedekt: 0).
  Alle 28 doelbestanden van drop-ins in de TeknoParrot-views staan al lokaal in de view (bestaande map is lokaal, bestand geen symlink), de
  twee aagames-views hebben `BepInEx/` en `Game_Data/Plugins` lokaal, en de map voor gepatchte binaries (`~/.local/share/ravengun/patched`) is lokaal.
* **Klasse (b), restanten:** in de spelmap van Rabbids Hollywood op de NAS staat onze BepInEx-boom van 2026-10-11 (plugin-DLL 18:26,
  config en cache 19:33, `LogOutput.log` 19:34; zie par. 5). Gelezen door beide Rabbids-views (TeknoParrot en aagames); niets is
  verwijderd of aangepast. Dit is een restant van 2026-10-11, geen nieuwe schrijfactie: de mtimes zijn van 2026-10-11 en er is sindsdien niets bijgekomen.
* **Klasse (a):** 44 plekken in 15 views (zie par. 6): informatief.

## 3. Waar leest en schrijft de kern teknoparrot.ini (klasse a, bekend en geaccepteerd)

Bron: OpenParrot @ `fc8ba632e1f67c5c340ff88f30efca743e8d38f5` (gepind; build-cache `~/.cache/ravengun/build/openparrot/<commit>/src`,
onze patches in `third_party/openparrot/patches/`).

| wat | waar | pad | relatief aan |
|---|---|---|---|
| kern leest zijn instellingen | `OpenParrot/src/dllmain.cpp:190` en `:215` | `config.load_file("teknoparrot.ini")` | **werkmap van het proces** (cwd) |
| kern leest hotkeys | `OpenParrot/src/Functions/Global.cpp:545-546` | `".\\teknoparrot.ini"` | cwd |
| kern leest render-hook | `OpenParrot/src/Functions/D3D9Misc.cpp:788`, `:801` | `".\\teknoparrot.ini"` | cwd |
| kern SCHRIJFT de [Error]-regel | `OpenParrot/src/Utility/GameDetect.cpp:923` (onze patch `0003-unknown-game-generic-route.patch`, regel 51 van de patch) | `WritePrivateProfileStringA("Error","Unsupported Executable",..., ".\\teknoparrot.ini")` | cwd |
| kern schrijft score-ini | `OpenParrot/src/Functions/Global.cpp:582`, `:584` | `".\\ScoreSubmission.ini"` | cwd |
| kern: schijfletter-herleiding (onze patch 0004) | `third_party/openparrot/patches/0004-drive-letter-redirect.patch:19`, `:51` | `.\TeknoParrot\<letter>\<rest>` (ook NVRAM van o.a. Gashaaaan) | cwd |
| spelspecifiek | `Games/ES3X/StarWars.cpp:236,272`, `Games/Other/FNFSC.cpp:137-148` | `".\\teknoparrot.ini"` | cwd |
| lader leest | `OpenParrotLoader/src/main.cpp:925` | `<map van de exe>\\teknoparrot.ini` | **exe-map** (`PathRemoveFileSpecW(gamePathW)`) |

23 plaatsen in 7 bestanden, vast ingebakken: **geen omgevingsvariabele, `RG_*`-variabele of argument** kiest het pad van `teknoparrot.ini`.
De werkmap zelf is wel instelbaar: `OpenParrotLoader/src/main.cpp:674-693` start de game (`CreateProcess(..., gameFolderW, ...)`, regels 749 en 770) met
de map van de exe of de map in `TP_GAME_WORKING_DIRECTORY`; wij zetten die uit het profiel (`scripts/gamesettings.py:369`). Bij Luigi's Mansion is dat
`...\views\LuigisMansion\exe\x64`, en `views/LuigisMansion/exe` is een symlink naar de NAS: de game draait dus in zijn eigen spelmap op de NAS en de kern schrijft
daar `teknoparrot.ini`. Het plan toont voor Luigi `core_source: runtime` (gesloten `TeknoParrot64.dll`); de [Error]-regel van 2026-10-12 10:54 kan van die kern
komen of van onze open kern (`[Loader] Build = ravengun`, dezelfde `default:`-tak, patch 0003).

**Besluit (2026-10-12): dit grensgeval is bekend en geaccepteerd.** Er wordt geen fix gebouwd. (Voor de volledigheid, niet gevraagd en niet uitgevoerd: de
kleinste kernwijziging zou een `IniPad()`-hulpfunctie zijn die `RG_TEKNOPARROT_INI` leest, op de 23 plaatsen en in `main.cpp:925`; `DXVK_STATE_CACHE_PATH` zou de
dxvk-cache kunnen verplaatsen.)

## 4. Wordt de regel "wij schrijven nooit naar de NAS" in launch-direct.py gehandhaafd?

* De regel staat in de kop (`scripts/launch-direct.py:16`). Hij gaat over wat launch-direct ZELF schrijft (het gegenereerde `teknoparrot.ini` uit het profiel,
  drop-ins, gepatchte kopieen), dus over klasse (b), niet over wat de game doet.
* Handhaving is **realpath, niet een tekstvoorvoegsel**: `on_nas()` (`:221-236`) doet `os.path.realpath(path)` en kijkt of het resultaat onder `~/mnt/` valt of op een
  cifs/smb/nfs/sshfs-mount ligt (`/proc/self/mounts`). Gebruikt in `place_file` (`:301`), `place_binary` (`:324`), patch-doel (`:812`) en de map voor gepatchte kopieen (`:845`).
* Een symlinkmap van de view naar de NAS (Luigi: `exe` -> NAS) **omzeilt deze controle dus niet**: `place_file(.../exe/x64/teknoparrot.ini)` ziet via realpath dat het op
  de NAS ligt en schrijft niet (status `nas`). Het gevolg: de game gebruikt het `teknoparrot.ini` dat op de NAS staat.
* Drop-ins: `_binnen()` (`:408-411`) en de controle op `:490` zijn lexicaal; `dropin_materialiseer` (`:414-457`) maakt een symlinkmap daarom eerst lokaal en controleert achteraf met
  realpath (`:455`). Daarom status `gedekt` voor drop-indoelen op de TeknoParrot-route.
* Geen controle dat de werkmap van het plan lokaal is (`:996`, `:1623`): bedoeld, want de game start in zijn spelmap (klasse a).

## 5. Klasse (b): onze artefacten

Wat de scan controleert: de BepInEx-boom van ons (map, `plugins/config/cache/patchers`, `LogOutput.log`), elke doelmap van een drop-in (uit het startplan van
`launch-direct.py --plan --json`, dus zoals de drop-in-logica ze oplost; voor aagames-views dezelfde relatieve paden in de view-wortel), `Game_Data/Plugins` van een aagames-view,
de map voor gepatchte binaries, en restanten op de NAS (naam `RavenGun*`/`nl.studiodamon.*`/`*.ravengun-tmp`/`rg-*`, en drop-inbestanden die per sha256 identiek zijn aan het origineel in `patches/`).

| view | plek | wijst naar | rol | status | wie/wat | bewijs op de NAS (datum) |
|---|---|---|---|---|---|---|
| RabbidsHollywood (tp) | `BepInEx/config/nl.studiodamon.ravengun.rabbidshollywood.cfg` (via `.`) | `NAS/Teknoparrot/Rabbids Hollywood /BepInEx/config/nl.studiodamon.ravengun.rabbidshollywood.cfg` | aanwezig-op-nas | **restant** | naam wijst op een artefact van ons (plugin, drop-in of hulpbestand) | `nl.studiodamon.ravengun.rabbidshollywood.cfg` (2026-10-11 19:33) |
| RabbidsHollywood (tp) | `BepInEx/plugins/RavenGun.RabbidsHollywood.dll` (via `.`) | `NAS/Teknoparrot/Rabbids Hollywood /BepInEx/plugins/RavenGun.RabbidsHollywood.dll` | aanwezig-op-nas | **restant** | naam wijst op een artefact van ons (plugin, drop-in of hulpbestand) | `RavenGun.RabbidsHollywood.dll` (2026-10-11 18:26) |
| RabbidsHollywood (tp) | `BepInEx/LogOutput.log` (via `.`) | `NAS/Teknoparrot/Rabbids Hollywood /BepInEx/LogOutput.log` | aanwezig-op-nas | **restant** | geschreven door BepInEx tijdens een run met onze plugin | `LogOutput.log` (2026-10-11 19:34) |
| RabbidsHollywood (tp) | `BepInEx/config/BepInEx.cfg` (via `.`) | `NAS/Teknoparrot/Rabbids Hollywood /BepInEx/config/BepInEx.cfg` | aanwezig-op-nas | **restant** | geschreven door BepInEx tijdens een run met onze plugin | `BepInEx.cfg` (2026-10-11 19:33) |
| RabbidsHollywood (tp) | `BepInEx/cache/chainloader_typeloader.dat` (via `.`) | `NAS/Teknoparrot/Rabbids Hollywood /BepInEx/cache/chainloader_typeloader.dat` | aanwezig-op-nas | **restant** | geschreven door BepInEx tijdens een run met onze plugin | `chainloader_typeloader.dat` (2026-10-11 19:33) |
| RabbidsHollywood (tp) | `BepInEx/cache/harmony_interop_cache.dat` (via `.`) | `NAS/Teknoparrot/Rabbids Hollywood /BepInEx/cache/harmony_interop_cache.dat` | aanwezig-op-nas | **restant** | geschreven door BepInEx tijdens een run met onze plugin | `harmony_interop_cache.dat` (2026-10-11 19:34) |

Toelichting restanten Rabbids: `BepInEx/LogOutput.log` op de NAS meldt BepInEx 5.4.23.2 (die van de dump) en "RHA Mouse Plugin" naast onze plugin: de game draaide op
2026-10-11 met de NAS-map als werkmap (TeknoParrot-route met `BepInEx/` als symlink), en onze plugin-DLL (18:26) stond toen in de BepInEx van de NAS. Alle bestanden zijn van
2026-10-11. De huidige views hebben `BepInEx/` lokaal. Opruimen is aan de eigenaar.

## 6. Klasse (a): ontwerp, de game schrijft in zijn eigen spelmap op de NAS (informatief)

Alle regels hieronder: **bekend en geaccepteerd** (besluit eigenaar, 2026-10-12). "Bewijs" zijn bestanden met een mtime van na 2026-10-09 en na de exe (+1 uur); dumps op de NAS dragen
1996-12-24, dus een nieuwere datum komt van een run (van ons, van TeknoParrotUi of van de game). Wie schreef is niet uit de mtime te lezen.

| view | map | wijst naar | wie kan er schrijven | wat | bewijs op de NAS (datum) | status |
|---|---|---|---|---|---|---|
| HOTDSD | `App/Hodzero/WindowsNoEditor/AMDaemon` (rol: exe-map; via `App`) | `NAS/Teknoparrot/House of the Dead Scarlet Dawn/App/Hodzero/WindowsNoEditor/AMDaemon` | teknoparrot.ini door de kern (launch-direct weigert op de NAS), DXVK state cache (<exe>.dxvk-cache), game | - | `teknoparrot.ini` (2026-10-11 08:55) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| HOTDSD | `App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64` (rol: exe-map; via `App`) | `NAS/Teknoparrot/House of the Dead Scarlet Dawn/App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64` | teknoparrot.ini door de kern (launch-direct weigert op de NAS), DXVK state cache (<exe>.dxvk-cache), game | - | `teknoparrot.ini` (2026-10-11 08:55) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| HOTDSD | `firewall.cfg` (rol: schrijfbestand; via `firewall.cfg`) | `NAS/Teknoparrot/House of the Dead Scarlet Dawn/firewall.cfg` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| HOTDSD | `pxGetHwInfo.ini` (rol: schrijfbestand; via `pxGetHwInfo.ini`) | `NAS/Teknoparrot/House of the Dead Scarlet Dawn/pxGetHwInfo.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| InfinityBladeFX | `Binaries/Win32/TPFIX.ini` (rol: schrijfbestand; via `Binaries/Win32/TPFIX.ini`) | `NAS/Teknoparrot/Infinity Blade FX/Binaries/Win32/TPFIX.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `TPFIX.ini` (2026-10-11 00:19) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| InfinityBladeFX | `Binaries/Win32/teknoparrot.ini` (rol: schrijfbestand; via `Binaries/Win32/teknoparrot.ini`) | `NAS/Teknoparrot/Infinity Blade FX/Binaries/Win32/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-12 11:00) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| JurassicPark | `teknoparrot.ini` (rol: schrijfbestand; via `teknoparrot.ini`) | `NAS/Teknoparrot/Jurassic Park/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-09 20:31) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGI | `app.ini` (rol: schrijfbestand; via `app.ini`) | `NAS/Teknoparrot/Let's Go Island Lost on the Island of Tropics/app.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGI | `teknoparrot.ini` (rol: schrijfbestand; via `teknoparrot.ini`) | `NAS/Teknoparrot/Let's Go Island Lost on the Island of Tropics/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-12 07:03) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGI3D | `OutputBlaster.ini` (rol: schrijfbestand; via `OutputBlaster.ini`) | `NAS/Teknoparrot/Let's Go Island 3D Lost on the Island of Tropics/OutputBlaster.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGI3D | `app.ini` (rol: schrijfbestand; via `app.ini`) | `NAS/Teknoparrot/Let's Go Island 3D Lost on the Island of Tropics/app.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGI3D | `config.ini` (rol: schrijfbestand; via `config.ini`) | `NAS/Teknoparrot/Let's Go Island 3D Lost on the Island of Tropics/config.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGI3D | `teknoparrot.ini` (rol: schrijfbestand; via `teknoparrot.ini`) | `NAS/Teknoparrot/Let's Go Island 3D Lost on the Island of Tropics/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-11 03:35) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGS | `GameConfig.ini` (rol: schrijfbestand; via `GameConfig.ini`) | `NAS/Teknoparrot/Let's Go Safari/GameConfig.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGS | `firewall.cfg` (rol: schrijfbestand; via `firewall.cfg`) | `NAS/Teknoparrot/Let's Go Safari/firewall.cfg` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGS | `mxGetHwInfo.ini` (rol: schrijfbestand; via `mxGetHwInfo.ini`) | `NAS/Teknoparrot/Let's Go Safari/mxGetHwInfo.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LGS | `teknoparrot.ini` (rol: schrijfbestand; via `teknoparrot.ini`) | `NAS/Teknoparrot/Let's Go Safari/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-11 02:22) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LuigisMansion | `Log` (rol: schrijfmap; via `Log`) | `NAS/Teknoparrot/Luigi's Mansion Arcade/Log` | logbestanden van de game of loader | - | `event_PN2.csv` (2026-10-10 22:38)<br>`event_PN3.csv` (2026-10-10 22:47)<br>`play_log.csv` (2026-10-10 22:47)<br>`play_number.dat` (2026-10-10 22:47) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LuigisMansion | `Ram` (rol: schrijfmap; via `Ram`) | `NAS/Teknoparrot/Luigi's Mansion Arcade/Ram` | NVRAM/RAM-bestanden van de game | - | `SRAM_0x00004300.bin` (2026-10-10 22:47)<br>`SRAM_0x00004400.bin` (2026-10-10 22:47)<br>`SRAM_0x00004c00.bin` (2026-10-10 22:47)<br>`SRAM_0x00004100.bin` (2026-10-11 17:09)<br>`EEPROM_0x00001100.bin` (2026-10-12 22:22)<br>`SRAM_0x00004200.bin` (2026-10-12 22:22)<br>`CreditConfig.bin` (2026-10-12 22:38) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LuigisMansion | `exe/x64` (rol: exe-map, werkmap-game; via `exe`) | `NAS/Teknoparrot/Luigi's Mansion Arcade/exe/x64` | OpenParrot-kern (teknoparrot.ini [Error], ScoreSubmission.ini, .\TeknoParrot\<letter>\), game (logs/NVRAM relatief aan de werkmap) | teknoparrot.ini door de kern (launch-direct weigert op de NAS), DXVK state cache (<exe>.dxvk-cache), game | `VACUUM.dxvk-cache` (2026-10-10 22:14)<br>`teknoparrot.ini` (2026-10-12 10:54) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LuigisMansion | `FFBPlugin.ini` (rol: schrijfbestand; via `FFBPlugin.ini`) | `NAS/Teknoparrot/Luigi's Mansion Arcade/FFBPlugin.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| LuigisMansion | `oxGetHwInfo.ini` (rol: schrijfbestand; via `oxGetHwInfo.ini`) | `NAS/Teknoparrot/Luigi's Mansion Arcade/oxGetHwInfo.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| MedaruNoGunman | `firewall.cfg` (rol: schrijfbestand; via `firewall.cfg`) | `NAS/Teknoparrot/Medaru No Gunman/firewall.cfg` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| MedaruNoGunman | `mxGetHwInfo.ini` (rol: schrijfbestand; via `mxGetHwInfo.ini`) | `NAS/Teknoparrot/Medaru No Gunman/mxGetHwInfo.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| MedaruNoGunman | `exe/teknoparrot.ini` (rol: schrijfbestand; via `exe/teknoparrot.ini`) | `NAS/Teknoparrot/Medaru No Gunman/exe/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-12 11:00) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| OG | `OutputBlaster.ini` (rol: schrijfbestand; via `OutputBlaster.ini`) | `NAS/Teknoparrot/OAV6DU~3/OutputBlaster.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| OG | `gs2.ini` (rol: schrijfbestand; via `gs2.ini`) | `NAS/Teknoparrot/OAV6DU~3/gs2.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| OG | `mxRingwide.ini` (rol: schrijfbestand; via `mxRingwide.ini`) | `NAS/Teknoparrot/OAV6DU~3/mxRingwide.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RabbidsHollywood | `ControllerBaseConfig.json` (rol: schrijfbestand; via `ControllerBaseConfig.json`) | `NAS/Teknoparrot/Rabbids Hollywood /ControllerBaseConfig.json` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `ControllerBaseConfig.json` (2026-10-12 01:46) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RabbidsHollywood | `ControllerBaseConfig_Mini.json` (rol: schrijfbestand; via `ControllerBaseConfig_Mini.json`) | `NAS/Teknoparrot/Rabbids Hollywood /ControllerBaseConfig_Mini.json` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RabbidsHollywood | `RabbidsHollywood_Operator.conf` (rol: schrijfbestand; via `RabbidsHollywood_Operator.conf`) | `NAS/Teknoparrot/Rabbids Hollywood /RabbidsHollywood_Operator.conf` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RabbidsHollywood | `doorstop_config.ini` (rol: schrijfbestand; via `doorstop_config.ini`) | `NAS/Teknoparrot/Rabbids Hollywood /doorstop_config.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RabbidsHollywood | `teknoparrot.ini` (rol: schrijfbestand; via `teknoparrot.ini`) | `NAS/Teknoparrot/Rabbids Hollywood /teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-09 17:46) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RaccoonRampage | `RSGame/Binaries/Win64` (rol: exe-map; via `RSGame/Binaries`) | `NAS/Teknoparrot/Raccoon Rampage/RSGame/Binaries/Win64` | teknoparrot.ini door de kern (launch-direct weigert op de NAS), DXVK state cache (<exe>.dxvk-cache), game | - | `RSGame-Win64-Shipping.dxvk-cache` (2026-10-11 01:57)<br>`Setting_System.ini` (2026-10-11 22:59)<br>`Setting_System_Backups.ini` (2026-10-11 22:59)<br>`Setting_DailyReport.ini` (2026-10-12 01:47)<br>`Setting_DailyReport_Backups.ini` (2026-10-12 01:47)<br>`Setting_Hardware.ini` (2026-10-12 01:47)<br>`Setting_Hardware_Backups.ini` (2026-10-12 01:47)<br>`Setting_Water.ini` (2026-10-12 01:47)<br>`Setting_Water_Backups.ini` (2026-10-12 01:47)<br>`teknoparrot.ini` (2026-10-12 10:55) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RainbowBombergirl | `BepInEx/plugins` (rol: bepinex-van-de-game; via `BepInEx/plugins`) | `NAS/Teknoparrot/Rainbow Bombergirl/BepInEx/plugins` | de eigen BepInEx van de game (TeknoParrot-dump, zonder plugin van ons): config, cache, LogOutput.log | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| RainbowBombergirl | `doorstop_config.ini` (rol: schrijfbestand; via `doorstop_config.ini`) | `NAS/Teknoparrot/Rainbow Bombergirl/doorstop_config.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| Rampage | `Game.dxvk-cache` (rol: schrijfbestand; via `Game.dxvk-cache`) | `NAS/Teknoparrot/Rampage/Game.dxvk-cache` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `Game.dxvk-cache` (2026-10-11 09:12) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| TC5 | `TeknoParrot` (rol: schrijfmap; via `TeknoParrot`) | `NAS/Teknoparrot/Time Crisis 5/TeknoParrot` | kern: schijfletter-herleiding naar .\TeknoParrot\<letter>\ (patch 0004), opslag | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| TC5 | `teknoparrot.ini` (rol: schrijfbestand; via `teknoparrot.ini`) | `NAS/Teknoparrot/Time Crisis 5/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-09 16:43) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| TC5 | `TC5/Binaries/Win64/teknoparrot.ini` (rol: schrijfbestand; via `TC5/Binaries/Win64/teknoparrot.ini`) | `NAS/Teknoparrot/Time Crisis 5/TC5/Binaries/Win64/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-10 17:54) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| TombRaider | `ControllerBaseConfig.json` (rol: schrijfbestand; via `ControllerBaseConfig.json`) | `NAS/Teknoparrot/Tomb Raider/ControllerBaseConfig.json` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `ControllerBaseConfig.json` (2026-10-12 01:49) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| TombRaider | `ControllerBaseConfig_Mini.json` (rol: schrijfbestand; via `ControllerBaseConfig_Mini.json`) | `NAS/Teknoparrot/Tomb Raider/ControllerBaseConfig_Mini.json` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| TombRaider | `doorstop_config.ini` (rol: schrijfbestand; via `doorstop_config.ini`) | `NAS/Teknoparrot/Tomb Raider/doorstop_config.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | geen nieuwe bestanden gevonden | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |
| TombRaider | `teknoparrot.ini` (rol: schrijfbestand; via `teknoparrot.ini`) | `NAS/Teknoparrot/Tomb Raider/teknoparrot.ini` | de game/kern schrijft in dit eigen bestand van de game, op de NAS | - | `teknoparrot.ini` (2026-10-09 17:39) | bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS) |

## 7. Samenvatting per view

| view | route | (a) ontwerp: plekken op de NAS | (b) artefacten: open / gedekt / restant |
|---|---|---|---|
| GhostBusters | TeknoParrot | 0 | 0 / 0 / 0 |
| HOTDSD | TeknoParrot | 4 | 0 / 0 / 0 |
| InfinityBladeFX | TeknoParrot | 2 | 0 / 0 / 0 |
| JurassicPark | TeknoParrot | 1 | 0 / 0 / 0 |
| LGI | TeknoParrot | 2 | 0 / 0 / 0 |
| LGI3D | TeknoParrot | 4 | 0 / 0 / 0 |
| LGS | TeknoParrot | 4 | 0 / 0 / 0 |
| LuigisMansion | TeknoParrot | 5 | 0 / 0 / 0 |
| MedaruNoGunman | TeknoParrot | 3 | 0 / 0 / 0 |
| OG | TeknoParrot | 3 | 0 / 0 / 0 |
| RabbidsHollywood | TeknoParrot | 5 | 0 / 0 / 6 |
| RaccoonRampage | TeknoParrot | 1 | 0 / 0 / 0 |
| RainbowBombergirl | TeknoParrot | 2 | 0 / 0 / 0 |
| Rampage | TeknoParrot | 1 | 0 / 0 / 0 |
| T4User | TeknoParrot | 0 | 0 / 0 / 0 |
| TC5 | TeknoParrot | 3 | 0 / 0 / 0 |
| Terminator | TeknoParrot | 0 | 0 / 0 / 0 |
| TombRaider | TeknoParrot | 4 | 0 / 0 / 0 |
| RabbidsHollywood | aagames | 0 | 0 / 0 / 6 |
| TombRaider | aagames | 0 | 0 / 0 / 0 |
| (globaal) | globaal | 0 | 0 / 0 / 0 |

## 8. Wat deze inventaris niet doet

* De views zijn niet gewijzigd en er is niets op de NAS aangeraakt of verwijderd.
* Niet gemeten: of Wine een symlink naar `Game.exe` oplost naar de NAS-map; daarvoor moet een game gestart worden.
* Een BepInEx-plugin van ons in de eigen BepInEx van een TeknoParrot-view (zoals Rainbow Bombergirl) bestaat nu niet; plaatst iemand er een, dan landt die via de symlinkmap `plugins`
  op de NAS. De scan ziet dat vanaf het moment dat de plugin er staat (de boom is dan "van ons").
