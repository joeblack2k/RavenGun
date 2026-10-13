# Volledigheid van House of the Dead: Scarlet Dawn (HOTDSD)

**Gemeten op 2026-10-12. Alleen gelezen op de NAS; niets geschreven, geen spel gestart, geen bench gebruikt.
Methode: `re/volledigheidscontrole.md` — dezelfde vier controles als de andere vijf games, aangepast aan
een Windows/UE4-titel in plaats van een ElfLdr2-ELF. Het is een STRUCTURELE controle en GEEN hash-controle;
dat onderscheid hoort bij elke uitspraak hieronder (zie §1).**

## Samenvatting in één oogopslag

| # | Controle | Uitkomst |
|---|---|---|
| 1 | Bestaat de exe die het profiel noemt? | **Ja** — `Hodzero-Win64-Shipping.exe` (54.128.128 B, precies de `Identify/Size` uit `game.ini`) én `amdaemon.exe` (8.272.384 B) |
| 2 | Lege bestanden (0 bytes)? | **0 van de 2.081** |
| 3a | Is het beeld heel (niet afgekapt)? | **Ja** — 32 PE-bestanden, **0 afgekapt**; het einde van de secties is exact het einde van elk bestand |
| 3b | Zijn alle imports vindbaar? (het `DT_NEEDED`-equivalent) | **Ja** — spel 50/50, daemon 23/23, `hwvalue.exe` 7/7, `pxGetHwinfo.exe` 5/5; **0 onopgelost**, en alle 44 namen staan in de gedeelde Wine-prefix (`system32` én `syswow64`) |
| 4 | Logisch van omvang en structuur voor dit soort game? | **Ja** — exact de verwachte UE4-cooked structuur; de 7,31 GB pak heeft een gave 44-byte footer en de index sluit **byte-exact** op het einde aan |
| — | Verwijzingen in de eigen scripts die ontbreken | 9 stuks, **alle 9 met een gemeten onschuldige verklaring** (§6) |
| — | **Sterkste bewijs: een echte speelsessie** | **Ja** — de UE4-logs in de prefix laten **zes sessies** op 2026-10-11 zien, waarvan één van **±7 minuten** die `Stage0` laadde en boekhouding bijhield (§12, [`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md)). Niet alleen "start", maar gespeeld — **met de eerlijke toevoeging dat één van de zes sessies in een crash eindigde**, 74 s na de start, en dat de andere vijf gestopt zijn |
| — | **Eindoordeel** | **Structureel compleet.** Geen enkel ontbrekend bestand, geen afkapping, geen onopgeloste afhankelijkheid. Van de vier onbewijsbare punten in §7 is er inmiddels één beantwoord (§12); drie blijven open |

**Correctie op een bestaande regel.** `patches/LOADERS-ZES-GAMES.md` regel 25 zegt bij Scarlet Dawn
*"de volledigheid is nagerekend"*. Dat was **niet** gebeurd — er was geen meting, geen methode en geen
uitkomst. Deze notitie doet die controle alsnog, en komt op dezelfde uitkomst uit die de kaart al
claimde. De regel is dus nu pas waar; wie hem leest moet deze notitie als onderbouwing kunnen vinden.

**Onverwachte bijvangst die de opdracht raakt.** In `Game.ini` staat het sleutelwoord
`DebugWithoutAMDaemon=false` — het is een **echte configuratiesleutel van het spel zelf**, geen losse
string. Daarmee is de open vraag uit ronde 12 ("is dat een modus waarin het spel zonder daemon werkt?")
beantwoord, en is er een **niet-invasieve patchweg** voor deze game (§9 en §10). Zie ook
`REPORT.md` §5/§10 voor de rest van het dossier.

---

## 1. De methode, en wat hij niet dekt

Overgenomen uit `re/volledigheidscontrole.md`, met dezelfde orakelbronnen (allemaal lokaal, dus zonder de
NAS aan te raken): `GameProfiles/HOTDSD.xml` (welke exe, welke route, `EmulationProfile`),
`GameSetup/HOTDSD.xml` (de twee paden), `Metadata/HOTDSD.json` (jaar/platform), en de NAS zelf
(bestanden, omvang, lege bestanden, structuur).

**Wat deze controle niet dekt.** De verwachte bestandshashes die TeknoParrot bij zijn integriteitstest
gebruikt zitten niet in de profielen (het profiel noemt de test wel: `BypassFilesystemIntegrityTest`,
`TPCheckAndFixBadDump`). Een game kan hier dus "compleet" uitkomen terwijl een databestand toch andere
inhoud heeft dan verwacht. Voor HOTDSD is er één plek waar dat concreet zou kunnen bijten: de pak van
7,31 GB (§5). Alles wat daarover te meten is zonder het spel te starten staat erbij.

**De inventaris is aan beide kanten nagerekend.** Op Windows (via de SMB-share) en op de Deck zelf:
**2.081 bestanden, 13.095.511.155 B, 0 leeg**, en vijf sha256-waarden identiek
(`Hodzero-Win64-Shipping.exe` `0fdb745d…5ac0`, `…_org.exe` `38fde9f4…cdf`, `amdaemon.exe`
`b4b0484e…3415`, `amdaemon.exe.original` `a483facc…4cf1`, `hwvalue.exe` `ceaa2c27…7c83`). De share die
op Windows `Z:` is, is dezelfde CIFS-mount die de Deck gebruikt
(`//192.168.2.3/emulation` → `/home/deck/mnt/spidernas_emulation`), dus dit is geen verouderde kopie.

## 2. Controle 1 — de exe's die het profiel noemt bestaan

| Bron | Wat hij noemt | Op de NAS |
|---|---|---|
| `GameProfiles/HOTDSD.xml` | `ExecutableName = Hodzero-Win64-Shipping.exe`, `ExecutableName2 = amdaemon.exe` | beide aanwezig |
| `GameSetup/HOTDSD.xml` | `App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Shipping.exe`, `App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe` | beide aanwezig, exact die paden |
| `patches/HOTDSD/game.ini` `[Identify]` | `Path`, **`Size = 54128128`**, `Path2` | `Hodzero-Win64-Shipping.exe` is **54.128.128 B — gelijk aan de vastgelegde maat** [gemeten] |

`game.ini` legt alleen een maat vast voor de **eerste** exe; voor `amdaemon.exe` staat er geen `Size2`.
Dat is een klein gat: de maat van de daemon (8.272.384 B) is nu nergens in de repo vastgelegd, terwijl
juist die daemon de tweede schakel is. Aanbeveling: `Size2 = 8272384` toevoegen.

De drie terugval-exe's die `App/Hodzero/StartApp.bat` zou proberen als Shipping ontbreekt —
`Hodzero-Win64-Test.exe`, `Hodzero.exe` (Development), `Hodzero-Win64-Debug.exe` — staan **niet** in de
map. Dat is geen gemis: de keten kiest Shipping als eerste en die is er. `firewall.cfg` noemt
`Hodzero.exe`, de Development-naam; dat bestand is een allow-lijst voor de kast-image en geen startvereiste.
**Onschuldig** [afgeleid uit de leesorde in `StartApp.bat`, en uit het feit dat de route via
TeknoParrotUi zonder die bestanden gemeten speelbaar is].

## 3. Controle 2 — lege bestanden

**0 van de 2.081** [gemeten]. Geen enkel bestand van 0 bytes, dus geen afgebroken kopie in de zin van
`re/volledigheidscontrole.md`.

## 4. Controle 3 — is het beeld heel en zijn de afhankelijkheden vindbaar

**3a — niet afgekapt (het `PT_LOAD`-equivalent).** Van alle **32** PE-bestanden in de map is het einde van
de laatste sectie (`rawptr + rawsize`) vergeleken met de bestandsgrootte: **0 afgekapt**, en voor de twee
hoofdexes is het verschil exact 0 (`Hodzero-Win64-Shipping.exe` 54.128.128 = 54.128.128;
`amdaemon.exe` 8.272.384 = 8.272.384) [gemeten]. Drie kleine bestanden hebben een overlay
(`error.common.*.dll` +512 B, `WinPixEventRuntime.dll` +16.080 B); dat is een resource-/handtekeningstaart
en geen afkapping.

**3b — imports vindbaar (het `DT_NEEDED`-equivalent).** Voor de vier eigen uitvoerbare bestanden:

| Bestand | imports | systeem | lokaal meegeleverd | **onopgelost** |
|---|---|---|---|---|
| `Hodzero-Win64-Shipping.exe` | 50 | 50 | 0 | **0** |
| `amdaemon.exe` | 23 | 23 | 0 | **0** |
| `hwvalue.exe` | 7 | 7 | 0 | **0** |
| `pxGetHwinfo.exe` | 5 | 5 | 0 | **0** |

En dat is niet alleen "het zijn systeemnamen": van alle **44** unieke namen is op de Deck gecontroleerd of
ze in de **gedeelde Wine-prefix** staan (`~/.local/share/ravengun/prefixes/shared/wine`): **alle 44 in
`system32` én in `syswow64`** (598 respectievelijk 631 DLL's in die prefix). Daar zitten juist de
kandidaten bij die vaak ontbreken: `vcruntime140.dll`, `msvcp140.dll`, `ucrtbase.dll`, `mfplat.dll`,
`d3d9/d3d11/dxgi`, `xinput1_3.dll`, `X3DAudio1_7.dll`, `XAPOFX1_5.dll`, `hid.dll`, `wlanapi.dll`.
**Geen enkele onopgeloste afhankelijkheid** [gemeten, Deck].

De 15 DLL's die de map **zelf** meelevert (PhysX/APEX, Ogg, Vorbis, NVaftermath, WinPixEventRuntime) worden
door de game **dynamisch** geladen en staan niet in de importtabel; ze zijn alle 15 aanwezig en PE-heel.
Daarmee is ook de "waar verwijst het spel naar"-vraag gedekt: geen van de 15 ontbreekt.

De drie adapters die de daemon **bij naam** laadt — `aime_rw_adapterMD.dll`, `thincatcapclient.dll`,
`indicdll.dll` — staan **niet** in de map. Dat is geen onvolledigheid van deze kopie maar de
uitgeleverde toestand: de kaartlezer is optioneel en de game is zonder kaartlezer gemeten speelbaar
(`findings/HOTDScarletDawn.md`). Zie `REPORT.md` §9.5.

## 5. Controle 4 — omvang en structuur passen bij dit soort game

De map is een **UE4-cooked Windows-build** en ziet er precies zo uit [gemeten]:

```
App/Startup/                     5 .bat  (05_SetVideoPort, 10_SetResolution, 20_SetAudioVolume, 99_StartApp, AppStartup)
App/Tools/                       3 .exe  (SetResolution, SetAudioVolume, CleanupLogDir)
App/Hodzero/StartApp.bat
App/Hodzero/WindowsNoEditor/
   AMDaemon/                     5 bestanden + aime_firm/ (6) + am_resources/ (9) + TeknoParrot/{AMFS,D,Y}
   Engine/Binaries/ThirdParty/   PhysX (11), Vorbis (2), Ogg (1), NVaftermath (1), Windows/DirectX (1)
   Hodzero/Binaries/Win64/       2 exe (Shipping + _org), 2 .png, teknoparrot.ini
   Hodzero/Content/Paks/         1 pak,  7.847.593.660 B
   Hodzero/Content/Hodzero_V1/ADX/  2.014 bestanden: 1.890 .uasset + 123 .usm + 1 .acf  (5.024.069.116 B)
   Hodzero/Saved/Config/WindowsNoEditor/Game.ini  (274 B)
game.bat, firewall.cfg, pxGetHwinfo.exe, pxGetHwInfo.ini
```

Verdeling: pak **7,31 GB**, ADX-content **4,68 GB**, binaries **101 MB**, PhysX **90 MB**, rest klein.
Dat is een logische verhouding voor deze titel: alle gameplay zit in de pak, het CRIWARE-beeld en -geluid
staat los in `Hodzero_V1/ADX` [gemeten].

**Correctie op deze regel (bijgewerkt na het lezen van de pak-index).** Ik schreef hier eerder dat
`NewProject.acf` "de index daarvan" is. Dat is onjuist: het is de **CRI Atom-configuratietabel**
(`VoiceLimitGroupTable`, `DspSettingTable`, `BusTable`, `AisacControlNameTable`, `CategoryTable`,
`CueLimitWorkTable`, `GameVariableTable`, `SnapshotBackupWork`, `Md5Hash`, `VersionString`, …) en er
staat **geen enkel bestandspad** in — 0 treffers op `usm`, `ADX` en `Hodzero`. De pak-index vertelt het
echte verhaal, en dat is scherper dan ik dacht:

| Verzameling | Aantal | Detail |
|---|---|---|
| pak, onder `HodZero_v1/ADX/` | **2.874** | 1.437 `.uasset` + 1.437 `.uexp` (UE4 bewaart een asset als dat paar) |
| los op schijf, `Content/Hodzero_V1/ADX/` | **2.014** | 1.890 `.uasset` + 123 `.usm` + 1 `.acf` |
| **los, en nergens in de pak** | **577** | 453 `.uasset` + 123 `.usm` + 1 `.acf` |

Die **577 bestanden bestaan dus nergens anders** — de losse ADX-map is geen overbodige kopie. De 123
`.usm`-films zijn samen circa 4,3 GB. **Risico voor later, met de bron:** de pak spelt die map
`HodZero_v1` (27.311 indexregels, en **0** met `Hodzero_V1`) terwijl de schijf `Hodzero_V1` spelt; op
deze CIFS-mount bestaat dat onderscheid niet, maar **op een hoofdlettergevoelig bestandssysteem kan een
opzoeking van die 577 losse bestanden grijpen**. Zie [`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md) §4.
Het is nu geen probleem: de game heeft zes sessies gespeeld.

**De pak is niet afgekapt — en dat is te bewijzen zonder het spel te starten.** De laatste 44 bytes zijn
een gave UE4-footer:

```
Magic = 0x5A6F12E1   Version = 4
IndexOffset = 7.839.093.987   IndexSize = 8.499.628   IndexHash = 8c720ef38769d2c5e927f13b415d1b66642a4c4b
index_offset + index_size = 7.847.593.615   footer begint op 7.847.593.616   bestand = 7.847.593.660
```

De index eindigt dus **exact** waar de footer begint, en de footer eindigt exact op het einde van het
bestand. Er is geen ruimte voor ontbrekende inhoud en geen bungelende offset [gemeten].
`Content/` heeft precies twee kinderen (`Paks`, `Hodzero_V1`), en `Hodzero_V1` heeft precies één kind
(`ADX`) — er ontbreekt dus geen contentmap.

Wat **ontbreekt maar hoort te ontbreken** in een cooked build: `Engine/Content`, `Engine/Plugins`,
`Hodzero/Plugins`, `Hodzero/Config` en `Hodzero/Saved/Logs`. Bij een cooked Shipping-build zit de
configuratie en de plug-incode in de pak. **En dat is nu niet meer afgeleid maar gemeten:** de
pak-index bevat **3.697** regels met `Engine/Content`, en in een echte sessie laadt de engine
`../../../Engine/Content/EngineFonts/Faces/RobotoBold.ufont` met succes terwijl `Engine/Content` niet
op schijf staat — **de pak levert die content dus**. Hetzelfde geldt voor de fonts
(`../../../Hodzero/Content/HodZero_v1/Fonts/*.ufont`, 18 `.ufont`-regels in de index).

## 6. Verwijzingen in de eigen scripts en configs die niet bestaan

Alle 23 padverwijzingen in `game.bat`, `AppStartup.bat`, `99_StartApp.bat`, `StartApp.bat`,
`05/10/20_*.bat`, `firewall.cfg`, `pxGetHwInfo.ini`, `config.json` en `teknoparrot.ini` zijn nagelopen.
23 van de 23 bestaan, op deze na — met per stuk de verklaring [gemeten]:

| Ontbreekt | Waar het genoemd wordt | Verdict |
|---|---|---|
| `Hodzero-Win64-Test.exe`, `Hodzero.exe`, `Hodzero-Win64-Debug.exe` | `StartApp.bat` (terugvalorde Shipping → Test → Development → Debug) | **onschuldig** — Shipping bestaat en staat vooraan |
| `..\..\Dev\AsMiniCabinet`, `..\..\Dev\AsTarget` | `StartApp.bat` (ontwikkelaarsmarkeringen) | **onschuldig, en betekenisvol** — hun afwezigheid kiest juist `DETAIL_LAUNCH_MODE=cabinet` en dus `config.json` |
| `..\..\Dev\Tools\CheckUseAMDaemon.bat` | `StartApp.bat` | **onschuldig, en betekenisvol** — zijn afwezigheid betekent dat de daemon **altijd** gestart wordt |
| `config_target.json`, `config_cabinetMini.json` | `StartApp.bat` (alternatieve daemonconfigs) | **onschuldig** — die tak wordt niet gekozen; de bestanden staan ook niet in de map |
| `app.exe` | `StartApp.bat`, in een Japanse **commentaarregel** | **onschuldig** — het is een opmerking over een denkbeeldige `app.exe`, geen bestandsverwijzing |
| `Hodzero.exe` | `firewall.cfg` | **onschuldig** — firewall-allow-lijst van de kast-image, geen startvereiste |
| `aime_rw_adapterMD.dll`, `thincatcapclient.dll`, `indicdll.dll` | dynamisch geladen door `amdaemon.exe` | **onschuldig** — uitgeleverd zonder kaartlezer; game is zo speelbaar |

Er is dus **geen enkele ontbrekende verwijzing zonder verklaring**.

## 7. Wat ik zonder het spel te starten NIET kan vaststellen

Dit staat er expliciet, en telt **niet** als compleet:

1. **Of de pak-inhoud klopt.** De structuur is gaaf en de index sluit, maar of elk bestand *in* de pak de
   juiste bytes heeft is met deze methode niet te zien. Dit is precies het gat dat
   `re/volledigheidscontrole.md` voor elke game benoemt.
2. **Of er een pak-handtekening vereist is.** Er is **geen `.sig`** naast de pak (0 stuks in de hele map),
   en de footer is de 44-byte vorm **zonder** `bEncryptedIndex` en **zonder** `EncryptionKeyGuid`, wat
   erop wijst dat er geen sleutel in het spel is. In de exe komen `PakSigning`, `SigningPublicKey` en
   `bEnablePakSigning` **0 keer** voor; `.sig` en `SecureHash` komen alleen in generieke UE-code voor.
   Dat is **structureel bewijs dat ondertekening hier niet speelt**, maar het is geen harde uitspraak:
   alleen een start laat zien of de pak mount. **Restrisico, klein.**
3. **Welke mappen het spel tijdens het spelen opent** — *inmiddels beantwoord door een echte run, zie §12.*
   Voor een cooked build zit de configuratie die dat bepaalt in de pak, dus statisch was het niet te lezen.
   `Hodzero/Saved/Logs` en `Hodzero/Config` ontbreken en dat is voor dit bouwtype verwacht.
4. **Of het gedrag van de 15 dynamisch geladen ThirdParty-DLL's klopt.** Ze zijn aanwezig en PE-heel; of
   de game ze ook daadwerkelijk kan laden (afhankelijkheden van die DLL's zelf) is niet nagelopen.

## 8. Wat er al is aan onze kant — de inhoud van `patches/HOTDSD/`

De map is de juiste map: `re/_zes-games-nieuw.md` zegt met zoveel woorden dat **de mapnaam de
TeknoParrot-profielnaam moet zijn** en dat een map met een andere naam door geen enkel script gevonden
wordt. Dat is inmiddels ook gebeurd (commit `47a2d8e` verhuisde de dossiers uit
`HouseOfTheDeadScarletDawn/` en `TerminatorSalvation/` naar `HOTDSD/` en `Terminator/`).

| In `patches/HOTDSD/` | Wat het is |
|---|---|
| `game.ini` (4.138 B) | twee exe's, `HasTwoExecutables`, `LaunchSecondExecutableFirst`, `[Launch] Mode = tpui`, `[Env] RG_UNSET_REMOTETHREAD = 1`, `[Wine] Prefix = shared`, `[Status] playable` |
| `README.md` (5.702 B) | het per-game dossier; noemt de stand van de start, de donglevraag en wat ontbreekt |
| `REPORT.md` (73 KB) | het verdiepingsrapport: PE-vergelijking, de daemon als IPC-server, de keychip-ABI, het `sega-keychip`-ontwerp |
| `dongle.sega-keychip.draft.json` | concept-donglebeschrijving (alle niet-gemeten waarden als `TODO`) |
| `sega-keychip.schema-fragment.json` | de aanvulling op `dongles/schema.json` voor het nieuwe type `sega-keychip` |
| `probes/`, `measurements/` | de 9 meetscripts met hun ruwe uitvoer |
| **`VOLLEDIGHEID.md`** | dit bestand |

**Wat er nog ontbreekt aan loader- of patchwerk** (uit `STAND.md`, `STARTTEST.md`,
`PATCHES-PER-GAME.md`, `ZONDER-LADER.md` en deze meting):

| Gat | Bron die het telt | Wat er nodig is |
|---|---|---|
| **Twee exe's starten kan de lader niet** | `STARTTEST.md` r. 50/131: `niet-gestart`, rc=4; `STAND.md` r. 54 | `launch-direct.py` regel 966 weigert `HasTwoExecutables`. Nodig: tweede exe eerst, `DaemonWait` (5 s uit `DAEMON_WAIT_SECONDS`), beide door dezelfde lader met dezelfde kern, daemon doden als de game klaar is |
| **Geen directe startmeting** | `STARTTEST.md` (HOTDSD staat op `niet-gestart`), `README.md` punt 3 | volgt automatisch zodra het vorige punt staat; het harnas is er al (`starttest3.sh` + `starttest-gen.py`) |
| **Geen `dongle.json` / `dongle/`** | `STAND.md` r. 54 en r. 118; `PATCHES-PER-GAME.md` r. 281 (4 games) | de keychip-/donglekaart voor dit spel. De ABI en de waarden zijn in kaart (`REPORT.md` §9, §10); het gaat om **per-game gegevens**, en `PATCHES-PER-GAME.md` zegt inmiddels zelf dat die in `patches/<Game>/` horen en niet in de lader-repo |
| **`[Launch] Mode = tpui`** | `game.ini` | pas om te zetten naar `direct` als de lader het kan; dat is dezelfde ingreep als het eerste gat |
| **De mountletters (AMFS / appdata-drijf / `Y:`) zijn nog niemands werk** | deze meting + `REPORT.md` §2.3 | de daemon leest `HKLM\SYSTEM\SEGA\SystemProperty\mount` en krijgt zijn letters als **data**; TeknoParrot vult ze met een `CreateFile`-omleiding naar `.\TeknoParrot\<LETTER>\`. Dat is een klein, generiek stuk laderwerk dat nog niet bestaat |
| **Het invoerkanaal voor start/test/service/munt is onbekend** | `findings/HOTDScarletDawn.md`; `PATCHES-PER-GAME.md` r. 235 | `HOTDSDPipe` (offset 8 = trekkerbit, 12/16/20/24 = as) draagt die vier niet. Dit is de echte onbekende |
| **`Size2` voor de daemon** | deze meting | één regel in `game.ini` |
| **De `Findings`-sleutel** | `README.md` punt 4 | het bevindingenbestand heet `HOTDScarletDawn.md`, niet `HOTDSD.md` |
| **Twee verouderde regels in `README.md`** | deze meting + `REPORT.md` | r. 54-55 en r. 84-86 noemen `minime` "de open vervanging van `amdaemon`". Dat is weerlegd: `minime` is een ALL.Net/AiMe-**server**, kent het spel-id `SDET` niet en is niet nodig om te starten. `patches/PATCHES-PER-GAME.md` r. 235 herhaalt het. Tekstcorrectie, geen code |

## 9. Wat de game zelf nodig heeft om te starten — met de bron

| Nodig | Bron | Status |
|---|---|---|
| **Twee programma's, daemon eerst** | `GameProfiles/HOTDSD.xml` (`ExecutableName2`, `HasTwoExecutables`, `LaunchSecondExecutableFirst`); `GameSetup/HOTDSD.xml` (de twee paden) | [gemeten] |
| **5 seconden wachten tussen de twee** | `App/Hodzero/StartApp.bat`: `set DAEMON_WAIT_SECONDS=5` + `timeout %DAEMON_WAIT_SECONDS%` | [gemeten] |
| **De daemon met `-c <config.json> -l <log>`** | `StartApp.bat`; en `amdaemon.exe` zelf: `Usage: amdaemon.exe [-c <config file path ...>] [-l <log file path>]` | [gemeten] |
| **De daemon doden als de game klaar is** | `StartApp.bat`: `taskkill /im amdaemon.exe` na `start /wait` + `timeout /t 1` | [gemeten] |
| **Werkmap `App/Hodzero`** | `App/Startup/99_StartApp.bat`: `pushd ..\Hodzero` en `StartApp.bat` gebruikt daarna relatieve paden | [gemeten] |
| **De mountletters** (AMFS + appdata + `Y:`) | de daemon leest `SystemProperty\mount` (waarden `AMFS` en appdata) — `amdaemon.exe`-strings; de map `AMDaemon/TeknoParrot/{AMFS,D,Y}` met `D/SDET/appfile.dat` (8.388.608 B) bewijst dat de letters naar mappen naast de daemon wijzen | [gemeten] |
| **Kastconfiguratie** (`max_player 2`, 1920×1080, `credit`, `usb_io` node 0 met `coin_input`/`switch_input`/`general_output`/`unique_io`) | `AMDaemon/config.json` (3.171 B) | [gemeten] |
| **Hardware-eis** `platform-id ACA0`, 16.384 MB, 17.000 MB appdata, 16 lanes | `pxGetHwInfo.ini` (990 B) | **inert** — niets in de keten start `pxGetHwinfo.exe` [gemeten] |
| **De eigen spellconfig** — inclusief `DebugWithoutAMDaemon`, `DebugAMCredit`, `DebugAMCreditFreePlay` | `Hodzero/Saved/Config/WindowsNoEditor/Game.ini`, sectie `[/Script/Hodzero.AppGameInstance]` | [gemeten] |
| **De `-launch=`/`-ABSLOG=`/`-UserDir=`-argumenten** | `StartApp.bat` | **niet nodig** — TeknoParrot geeft géén argumenten mee (`ExtraParameters` is leeg) en de game is zo gemeten speelbaar |
| **De keychip/dongle** | `amdaemon.exe`: `amDongleSetupKeychip`, `\??\FddDriver`, `Keychip Not Found`, `Retry Keychip Access.` | **niet nodig om te starten** — gemeten speelbaar zonder keychip; de laag is wel in kaart (`REPORT.md` §9) |
| **Een ALL.Net-server** | `amdaemon.exe`: `amMaster*`, `alpb*`, `keychipid=…&functype=…` | **niet nodig** — de daemon bereikt `Process state: Advertise` zonder netwerk |

## 10. Voorstel voor de eerste meting

De bestanden zijn compleet, dus de vraag verschuift van "is het bestand er" naar "wat houdt het tegen".
De grootste onbekende is de **twee-programma-start met de daemon**, en die is in twee stappen te ontleden.
Hieronder per stap: welke run, **wat ik vooraf verwacht**, en wat elke uitkomst zou betekenen. Ik doe
geen van beide — de bench is niet van mij.

### Meting 1 (aanbevolen eerste run) — de daemon alleen, zonder enige TeknoParrot-component

**Wat:** `amdaemon.exe` alleen, vanuit een **view-kopie** (nooit de NAS), met een harde tijdslimiet, met
`WINEDEBUG=+file,+relay`. Geen game, geen lader, geen kern. Ongeveer 90 seconden.

```sh
V="$HOME/.local/share/ravengun/runtime/views/HOTDSD"; W="$HOME/.local/share/ravengun/prefixes/shared/wine"
mkdir -p "$V"
rsync -a --delete "/home/deck/mnt/spidernas_emulation/Teknoparrot/House of the Dead Scarlet Dawn/App/Hodzero/WindowsNoEditor/AMDaemon/" "$V/AMDaemon/"
cd "$V/AMDaemon"
timeout 90 env WINEDEBUG=+file,+relay WINEPREFIX="$W" \
  wine amdaemon.exe -c config.json -l "$V/amdaemon.log" > "$V/amdaemon-ftrace.log" 2>&1
grep -oE '[A-Za-z]:\\\\[^"]*' "$V/amdaemon-ftrace.log" | sort -u | head -60
grep -a 'DeviceIoControl\|FddDriver' "$V/amdaemon-ftrace.log" | head -40
grep -aiE 'amdaemonExe|state|Advertise|Keychip|Error|Fail' "$V/amdaemon.log" | tail -40
```

**Waarom deze eerst:** hij vraagt geen enkele codewijziging, hij kan vandaag, hij raakt het scherm niet
(`amdaemon.exe` is een console-programma), en hij beslist de grootste splitsing in het plan: is de daemon
zelfstandig te draaien (dan is alleen de mountletter-omleiding nieuw werk) of eist hij de gesloten kern
(dan verandert de vorm van de hele route).

**Vooraf verwacht:**
* de log begint met `amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37`;
* hij komt tot een toestand met **`Advertise`**;
* `Keychip Not Found` en/of `Retry Keychip Access.` verschijnen **en hij loopt door**;
* de filetrace noemt een handvol `X:`/`Y:`/`D:`-paden en de registry `System\SEGA\SystemProperty\mount`;
* de `DeviceIoControl`-regels naar `\\.\FddDriver` zijn er en verklappen de ioctl-codes.

**Wat elke uitkomst betekent:**

| Uitkomst | Betekenis |
|---|---|
| Banner + `Advertise`, keychip wordt gelogd en getolereerd | **De daemon is zelfstandig.** De blokkade is dan alleen: mountletter-omleiding + twee-exe-start. De keychip blijft completeness-werk. Dit is wat ik verwacht |
| Banner, maar hij stopt op de mountletters of op `get amfs drive letter failed` | De mountletter-omleiding is **niet optioneel** en moet er eerst zijn; hij is wel precies af te leiden uit de filetrace |
| Banner, maar hij stopt op de keychip (`Cannot setup the keychip module.` / geen `Advertise`) | De keychip wordt een **startblokkade** in plaats van completeness-werk; dan is het `sega-keychip`-ontwerp (`REPORT.md` §10) de eerstvolgende klus, en dat verandert de volgorde van het hele plan |
| Geen banner, wine-fout | Het is een omgevingsprobleem (prefix, DLL-override), geen gameprobleem — en dan is dat de volgende vraag |
| `+relay` geeft geen `DeviceIoControl` | De daemon loopt via `NtDeviceIoControlFile`; dan op die naam zoeken, en anders de log-hook in onze eigen kern bouwen (dan zijn meting en feature dezelfde code) |

### Meting 2 (daarna) — de twee-exe-start via onze eigen lader

**Wat:** `launch-direct.py` met de twee-exe-ondersteuning erin, via het bestaande harnas
`bash /home/deck/starttest3.sh 22 HOTDSD` en daarna `python3 patches/starttest-gen.py`. Dat levert de
ontbrekende rij in `patches/STARTTEST.md`.

**Vooraf verwacht:** `START` (spelproces gezien, leeft aan het eind) en in het log de volgorde daemon →
5 s → game, plus `amdaemonExe Ver.2437` in het daemonlog. Dat is wat de teruggedraaide proef al liet zien.

| Uitkomst | Betekenis |
|---|---|
| `START` | De startvorm is opgelost; het gat is dan alleen nog het invoerkanaal (§8, start/test/service/munt) |
| `niet-gestart` met een fout vóór de eerste start | De lader zelf; de logregel wijst de plek aan |
| Spelproces gezien maar daarna weg (`viel-uit`) | Iets in de tweede schakel: de daemon te laat/vroeg, de werkmap, of de mountletters — te scheiden met de daemonlog en de filetrace van dezelfde run |
| De game start maar zonder invoer | Precies de bekende toestand: de startvorm werkt, het invoerkanaal is het enige gat |

## 11. Herhalen

```powershell
# op Windows, via de SMB-share:
& "$env:USERPROFILE\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe" probes\completeness.py
& "…\python.exe" probes\paksig_probe.py
```
```sh
# op de Deck, read-only:
bash /home/deck/_hotdsd-imports.sh          # de 44 imports tegen de gedeelde Wine-prefix
bash /home/deck/_hotdsd-verify.sh           # totalen + sha256 tegen de SMB-lezing
```

Ruwe uitvoer: `measurements/completeness.txt` en `measurements/pak-signing.txt`.
De vorige ronde staat in `REPORT.md` (dat is het Engelse verdiepingsrapport) en `measurements/01..07`.

**Eindoordeel: de bestanden van House of the Dead: Scarlet Dawn zijn structureel compleet.** Er ontbreekt
geen bestand dat de game, de daemon of het profiel noemt; niets is afgekapt; geen enkele afhankelijkheid
is onopgelost; en de omvang en de mappenstructuur passen bij een UE4-cooked Windows-build. De twee dingen
die een hash-controle zou toevoegen staan in §7 en zijn zonder start niet te meten — de pak-handtekening
is daarvan het enige met een restrisico, en alles wijst erop dat die hier niet speelt.

---

## 12. Bijvangst uit de meting: de UE4-logs bestaan wél, en het spel heeft gespeeld

*Toegevoegd na de daemonmeting van 2026-10-13. De volledige meting staat in
[`METING-1-DAEMON.md`](METING-1-DAEMON.md).*

`findings/HOTDScarletDawn.md` zegt: *"er is geen UE4-log: `Hodzero/Saved/Logs` wordt niet aangemaakt"*.
Dat is **fout**. De game schrijft zijn UE4-logs wel, maar niet in de speltmap — in **de Wine-prefix**, op
`%LOCALAPPDATA%\Hodzero\Saved\Logs\`:

```
~/.local/share/ravengun/prefixes/shared/wine/drive_c/users/steamuser/AppData/Local/Hodzero/
  Saved/Logs/Hodzero.log                              117.045 B   (08:55:41 → 09:02:44, ±7 minuten)
  Saved/Logs/Hodzero-backup-2026.10.11-08.44.21.log    22.252 B   (08:41:39 → 08:42:12)
  Saved/Logs/Hodzero-backup-2026.10.11-08.46.26.log    22.249 B   (08:44:21 → 08:44:47)
  Saved/Logs/Hodzero-backup-2026.10.11-08.47.52.log    22.248 B   (08:46:27 → 08:46:54)
  Saved/Logs/Hodzero-backup-2026.10.11-08.52.40.log    56.577 B   (08:47:52 → 08:51:56)
  Saved/Logs/Hodzero-backup-2026.10.11-08.55.41.log    49.042 B   (08:52:40 → 08:54:54)
  Saved/Crashes/UE4CC-Windows-249AC8AF…_0000/
      CrashContext.runtime-xml                          3.309 B
      UE4Minidump.dmp                                 297.524 B
      Hodzero.log                                      47.491 B
```

**Zes sessies op één dag, waarvan één van ±7 minuten — dat is het sterkste volledigheidsbewijs dat er is:
het spel heeft op deze machine gespeeld, niet alleen gestart.** Dit is nog steeds een **structurele**
meting: het bewijs dat de bestanden toereikend zijn, niet dat elke byte in de pak de verwachte is.

**De eerlijke toevoeging, en die hoort in het eindoordeel.** Van die zes sessies **eindigde er één in een
crash** (sessie 5, 08:52:40 → 08:54:54) en werden **vijf gestopt**. De crash valt **74 seconden** na de
start — precies toen `PL_Stage0` klaar was en de match naar `InProgress` ging — met een **lege
foutmelding**, géén callstack, en **22,7 GB RAM vrij**, dus geen geheugenprobleem. Er is een minidump van
297.524 B bewaard. **Het is niet reproduceerbaar op dat punt:** sessie 6 laadde dezelfde `PL_Stage0` en
liep daarna nog **105 seconden** door. Analyse en tijdlijn: [`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md).

Wat de logs vastleggen [gemeten]:

* `EngineVersion 4.18.1-0+++UE4+Release-4.18` — bevestigt de UE4-versie uit het dossier;
* `BaseDir Z:/home/deck/mnt/spidernas_emulation/…/Binaries/Win64/` — **direct van de NAS gedraaid**;
* `CommandLine` **leeg** — de game speelt zonder argumenten (TeknoParrot's `ExtraParameters` is leeg);
* de game bereikt `Match State Changed from WaitingToStart to InProgress`, laadt
  `…/Maps/Stage0/PL_Stage0` (1,98 s en 5,20 s in twee sessies) en activeert alle `SL_Stage0*`-sublevels;
* `LogPluginManager: Mounting plugin AMDaemon`, `CriWare`, `ABaaSGS`, `EmitMan`, `StateMan`, `TaskMan`,
  `WidgetMan`, `ApexDestruction`, `Paper2D`, `LowEntryExtendedStandardLibrary`;
* `LogApp: P2_CenterY : 622 / P2_RightX : 890 / P2_BottomY : 979` — kalibratiewaarden van speler 2;
* `LogAppData: UBookkeeping:…` met `P1 GameTime`, `AddDamageLogCount`, `AddFirstDeathLogCount`,
  `AddSecondDeathLogCount`, `Ranking[0][0][2]` — het spel houdt zijn eigen boekhouding bij;
* **`LogAMDaemon: Error: Exception caused. (Category:Logic Msg:'aimeId' is invalid.)`** — **één keer per
  binnenkomst in de `[Login]`-stap** (niet twee per sessie: in de zevenminutensessie staat hij tweemaal
  omdat de speler twee keer door Entry/Login ging), deterministisch op **regel 416** in elke sessie, en
  **het spel gaat er direct daarna gewoon door** (`UPlayLog::StartPlayLog PlayCount=1`, dan
  `Browse … PL_StageSelect`). Dit is de AMDaemon-plugin van de game die de AiMe-kaartidentiteit
  valideert; de fout heeft een eigen code in de game, `EAMDaemonAimeErrorId::AimeIdInvalid`. Uitgewerkt in
  [`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md) §3;
* `CrashContext.runtime-xml`: `SecondsSinceStart 74`, `CrashType Crash`, `IsEnsure`/`IsAssert` false,
  `CallStack` leeg, `ErrorMessage` leeg, `bIsOOM 0` — een harde crash met een **lege melding**, 74 s na de
  start, in de sessie die ook `LogWindows: Could not start CrashReportClient.exe` logt (dat bestand wordt
  niet meegeleverd). **Let op:** de `MemoryStats.*`-velden in dat bestand staan allemaal op 0 en zijn
  onbruikbaar; gebruik de `LogMemory`-regels uit het sessielog.

**Hiermee is §7 punt 3 beantwoord.** De game zoekt zijn content via de `[Core.System] Paths=` uit
`Saved/Config/WindowsNoEditor/Engine.ini` (`../../../Engine/Content`, `%GAMEDIR%Content`, en
plugin-Content-mappen), en die plugin-mappen (`Hodzero/Plugins/…`, `Engine/Plugins/2D/Paper2D`) **staan
niet op schijf** — ze zitten in de pak. Dat was afgeleid en is nu in een echte run gezien. De pak-index
bevestigt het cijfermatig: **3.697** regels met `Engine/Content`, en de engine laadt
`../../../Engine/Content/EngineFonts/Faces/RobotoBold.ufont` met succes terwijl die map niet bestaat.

**Eén portabiliteitsdetail dat eruit volgt, met de bron.** De log vraagt letterlijk om
`../../../Hodzero/Content/HodZero_v1/Fonts/…` (`Z`, `v1`), terwijl de map op schijf
`Content/Hodzero_V1/ADX` heet (`z`, `V1`); de **pak spelt het ook met een hoofdletter Z** (27.311
indexregels met `HodZero_v1`, **0** met `Hodzero_V1`). Op de CIFS-mount bestaat dat onderscheid niet —
CIFS is hoofdletterongevoelig — dus daar werkt het. **Op een hoofdlettergevoelig bestandssysteem (ext4)
kan dat breken**, en dan specifiek voor de **577 losse bestanden die nergens in de pak zitten** (453
`.uasset` + 123 `.usm` + 1 `.acf`): de pak bedient zijn eigen spelling, het bestandssysteem niet. Zie
[`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md) §4 voor de cijfers en voor wat er nog niet aan gemeten is
(met welke spelling CRIWARE de `.usm`-films opent).

