# Tussenrapport — het dongle-bewijs per game

**Herkomst:** een bestandsscan van de NAS-spelmappen op 2026-10-11, plus de uitlezing van de profielen en de
gegenereerde `teknoparrot.ini`. Dit is de basis onder kolom *Dongle* van
[loskoppelen-per-game.md](../loskoppelen-per-game.md).

## Het commando

```bash
NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot
for d in "$NAS"/*/; do
  n=$(basename "$d")
  echo "################ $n"
  find "$d" -maxdepth 4 -iname '*hasp*' -o -maxdepth 4 -iname '*matrix*' -o -maxdepth 4 -iname '*dongle*' \
       -o -maxdepth 4 -iname '*keychip*' -o -maxdepth 4 -iname '*sbk*' -o -maxdepth 4 -iname '*amdaemon*' \
       -o -maxdepth 4 -iname '*fff4*' -o -maxdepth 4 -iname '*sentinel*' -o -maxdepth 4 -iname '*bngrw*' \
       -o -maxdepth 4 -iname '*idmac*' -o -maxdepth 4 -iname '*segaapi*' 2>/dev/null | sed "s|$NAS/||"
  echo "-- top level:"
  ls -1 "$d" 2>/dev/null
done
```

De volledige uitvoer (924 regels) staat in `re/scratch/_lp6.out` in de Windows-werkmap en op de Deck in
`/home/deck/_lp6.out`.

---

## De drie dongle-soorten en hun API

Uit `findings/MATRIX-dongle-API.md` §5:

| Soort | API | Spellen |
|---|---|---|
| **HASP** | tien `hasp_*`-functies (`hasp_login`, `hasp_read`, `hasp_write`, `hasp_get_info`, …) | Halo, Jurassic Park, Terminator, TC5, NERF |
| **Matrix** | **27 `Dongle_*`-functies**; `Dongle_Count(short)` beslist | Rabbids, Rampage, en mogelijk meer |
| **regio** | geen API — vaste waarden | de Let's Go-games, GaiaAttack4, HauntedMuseum, en ~30 Sega-titels |

En `dongles/README.md` §2 noemt daarnaast het type **`sega-scan`** (de getrapte scan van de Sega-shell) en het
type **`region-pcb`**.

---

## De vondsten per game (letterlijke paden)

### HASP

| Game | Gevonden op de NAS | Grootte / aantal |
|---|---|---|
| **Halo** | `Halo Fireteam/g7/halo/fff4` | 112 bytes (ook `dongles/halo-fireteam/memory/fff4` en `…1111111111110100` in deze werkmap) |
| **JurassicPark** | `Jurassic Park/utils/pmrtdata/libhasp_linux_{5176,93341,93898,96728,98111,100624,112983,114576}.so` | 8 versies |
| **NERF** | `NERF Arcade/Plugins/x86_64/libhasp_linux_23714.so` en `NERF Arcade/Nerf_Data/Plugins/x86_64_ori/libhasp_linux_23714.so` | 2 kopieën |
| **TC5** | `Time Crisis 5/TC5/Binaries/Win64/hasp_windows_x64_105293.dll`, `…/hasp_windows_x64_106993.dll`, `Time Crisis 5/hasp_windows_x64_105293.dll` | 3 bestanden |
| **Terminator** | `Terminator Salvation /hasp/` (5413 bestanden, namen van 32 en 64 hextekens) + `ServiceDongleApp/ServiceDongleApp` | **5413**, niet 10.826 |
| **GhostBusters** | **niets** — alleen `GhostBusters/utils/splash/dongle-1024x768.cfg` en `…jpg` (een splash-afbeelding) | — |

**De HASP-API** (`findings/17_hasp_dongle.md` §1, uit `cores/halo/halo.so`): `hasp_login` schrijft de handle
`0xE6F` en geeft `0` terug; `hasp_get_info` levert een vaste XML; `hasp_read`/`hasp_write` openen een bestand
waarvan de **naam de 32-bits binaire fileid** is (`65524` = `0x0000FFF4` → `00000000000000001111111111110100`);
`hasp_get_size` geeft altijd `0`; `hasp_encrypt` laat de buffer ongemoeid. De dongle-image wordt **niet
ontsleuteld**.

**Twee haspid's** zijn in omloop: `744811821` (korte XML-vorm) en `748675309` (uitgebreide vorm).

### Matrix/SBK

| Game | Gevonden op de NAS |
|---|---|
| **InfinityBladeFX** | `Infinity Blade FX/Binaries/Win32/matrix32.dll` **en** `matrix32.TEKNOPARROT.dll` (plus `readme.txt` over de matrix32/TPFIX-oplossing en een `TPFIX.ini`) |
| **RabbidsHollywood** | `Rabbids Hollywood /matrix64.dll`, `matrix32.dll`, `Game_Data/Plugins/matrix64.dll`, `Game_Data/Plugins/matrix32.dll` |
| **Rampage** | `Rampage/matrix64.dll`, `matrix32.dll`, `Game_Data/Plugins/matrix64.dll`, `Game_Data/Plugins/matrix32.dll` |
| **TombRaider** | `Tomb Raider/matrix64.dll`, `matrix32.dll`, `Game_Data/Plugins/matrix64.dll`, `Game_Data/Plugins/matrix32.dll` |

**De beslissende aanroep** (`findings/MATRIX-dongle-API.md` §1, uit de IL van `Assembly-CSharp.dll`):

```il
IL_0022: ldc.i4.s 40
IL_0024: call  MatrixDLLCSharp.MatrixWrapper::Dongle_Count   ; <-- DE AANROEP
IL_0029: stfld short GBJODEKFBGD                             ; het aantal dongles
IL_0034: ldc.i4.1                                            ; vergelijk met 1
```

Bij 0 volgt `DONGLE NOT FOUND` en `UnityEngine.Application.Quit`. `MatrixDLLCSharp.MatrixWrapper.DLL_PATH =
"matrix64.dll"`. `LPT_Nr`/`LPT_Adr` in de `DNGINFO`-structuur wijzen op een **parallelle-poortdongle**.

**Let op — geen dongle maar gereedschap:** `Tomb Raider/Sixences/Matrix_Files/*.mtx` en
`Sixences/Firmware/*.zip` zijn de kalibratie en firmware van het **Sixence**-apparaat (een IMU/gun-tracker met
`ftdi.dll`, `usb.dll`, `sxCore.dll`). Dat is **geen** Matrix-dongle; de Matrix-dongle is `matrix64.dll`.

### regio/PCB

Bewijs uit `patches/<Game>/game.ini`, de door `--plan` gegenereerde `teknoparrot.ini` en de profielvelden:

| Game | Bewijs |
|---|---|
| **LGI** | `game.ini`: `DongleRegion = JAPAN`, `PcbRegion = JAPAN`; `teknoparrot.ini` `[General] DongleRegion=JAPAN` / `PcbRegion=JAPAN`; `GameProfiles/LGI.xml` heeft `<FieldName>DongleRegion</FieldName>` en `<FieldName>PcbRegion</FieldName>` |
| **LGI3D** | idem met `USA`/`USA` |
| **LGS** | idem met `USA`/`USA` |
| **GaiaAttack4** | `teknoparrot.ini` `[General] Region=E`; `MINIGUN.INI` `REGION\t\tE`; `GameProfiles/GaiaAttack4.xml` regel 52 `<FieldName>Region</FieldName>` |
| **HauntedMuseum** | `teknoparrot.ini` `Region=E`; `MUSEUM.INI` `REGION\t\tE`; profielveld `Region` |
| **HauntedMuseumII** | `teknoparrot.ini` `Region=E`; `HAUNTED2.INI` `REGION\t\tE`; profielveld `Region` |
| **RainbowBombergirl** | `teknoparrot.ini` `[General] Region=EXPORT`; `GameProfiles/RainbowBombergirl.xml` regel 68 `<FieldName>Region</FieldName>` |

`findings/12_aanpassingen_en_koppelingen.md` §7 zegt het expliciet: *"Dit zijn software-dongle/PIC-emulaties:
de game leest een regio-/PCB-identifier en TeknoParrot geeft de gekozen waarde terug via het I/O-board
(JVS-identifier of geheugenoffset). Er is geen fysieke dongle nodig."*

En §7 noemt ook: `DongleRegion` komt in **14** profielen voor, `PcbRegion` in **14**, `Region` in **7**,
`PCB ID` in **1**.

### twee-exe (amdaemon)

| Game | Bewijs |
|---|---|
| **HOTDSD** | `game.ini`: `Executable2 = amdaemon.exe`, `Is64Bit = true`, `HasTwoExecutables = true`, `LaunchSecondExecutableFirst = true`; `GameProfiles/HOTDSD.xml`: `<ExecutableName2>amdaemon.exe</ExecutableName2>` en `<HasTwoExecutables>true</HasTwoExecutables>`; op de NAS: `House of the Dead Scarlet Dawn/App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe` (+ `amdaemon.exe.original`, `config.json`, `aime_firm`, `am_resources`, `hwvalue.exe`) |

`findings/WAT-ELK-SPEL-NODIG-HEEFT.md` §2: *"er staat geen enkel HASP-bestand in de spelmap. De hindernis is
dat `amdaemon.exe` — Sega's AM Daemon — moet draaien voordat het spel start. En daar is `minime` de
open-source vervanging voor."*

### Sega-donglescan (`sega-scan`)

| Game | Bewijs |
|---|---|
| **TransformersShadowsRising** | `Sega/shell/dk2win64.dll` (10.203.232 B), `Sega/shell/dk2win32.dll` (7.315.552 B), `Sega/shell/dk2inst.dll` (91.744 B) — de Dinkey Pro-runtime; en `Sega/shell/Dongle - 261009.log`, `Dongle - 261010.log`, `Dongle - 261011.log` met letterlijk:<br>`[ 15:01:40:884 ] DONGLE : MainThreadDongleScan: failed stage 1` |

Dat is exact het voorbeeld in `dongles/README.md` §2, type 3 (`sega-scan`):
*"De Sega-titels doen een getrapte scan naar een dongle. Uit het log van de shell: `DONGLE :
MainThreadDongleScan: failed stage 1`. De scan kent dus meerdere fasen, en de eerste faalt standaard."*
Het type is een **raamwerk**: alleen fase 1 is bekend.

**Dit staat niet in `findings/INVENTARIS-dongle-per-game.md`** — die zet bij TransformersShadowsRising een
streepje. Zie open vraag 1 in het hoofd document.

### Geen aanwijzing (16 games)

Voor deze games is in de spelmap, in `game.ini`, in de gebruikerslaag en in het profiel **geen** donglebestand,
geen dongle-API-string en geen `DongleRegion`/`PcbRegion`/`Region`-veld gevonden:

3DCosplayMahjong\*, GalagaAssault, Gashaaaan, Gashaaaan2, GoGoStrike, LethalEnforcers3, LuigisMansion,
MedaruNoGunman, MusicGunGun2, OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui, Otomedius, PacmanChomp,
PigsMightFly, RaccoonRampage, TankTankTank\*

\* 3DCosplayMahjong en TankTankTank staan niet op de NAS; voor die twee is "geen aanwijzing" dus eerder
"niet gemeten" dan "bewezen geen".

**Wat er in die mappen wél staat en géén dongle is:**

| Game | Bestanden | Wat het wel is |
|---|---|---|
| MedaruNoGunman | `keychip/SBRZ_Table.dat`, `keychip/SBTF_Table.dat` (elk 1.190.000 B) | tabeldata; de blokkade van deze game is de SRAM/RAM-fout |
| Otomedius | `libacio.dll`, `HidGet.exe`, `HidBoot.id`, `testmode.bin` | Konami's ACIO-I/O op COM2, geen dongle |
| LethalEnforcers3 | `gunio.dll`, `iob.dll`, `pmds.dll` | I/O-modules van de game |
| MusicGunGun2 | `SECLIB.dll`, `ewfapi.dll`, `ewfdll.dll` | game-eigen bibliotheken (de blokkade is D3D9) |
| Luigi's Mansion | `LMAIO.bin`, `SendFlash.exe`, `oxGetHwInfo.exe` | het I/O-bord op COM1 (`game.bat` start `sendFlash COM1 LMAIO.bin`) |
| GoGoStrike | `alleg40.dll`, `bowling_usa`, `w040101sram` | Allegro + SRAM |
| LGI/LGI3D/LGS | `mxGetHwInfo.exe`, `mxGetHwInfo.ini`, `firewall.cfg` | Sega's hardwarecontrole (RingWide/Ring) |

---

## Waar de dongle-verwijzing zit: op schijf, dynamisch of statisch in de ELF

**Statisch gemeten op 2026-10-12, read-only, zonder één game te starten.** Aanleiding: de HASP-tabel
hierboven zet bij **GhostBusters** "niets" en bij **Terminator** een map `hasp/`, terwijl
`loskoppelen-takenlijst.md` §3b (stand 2026-10-12) zegt dat GhostBusters **en** Terminator "beide
blokkeren op een statisch ingelinkte dongle (`DongleInitialize() FAILED`, HASP/FTDI)". Dat is geen
tegenspraak maar een verschil in **wat** er gemeten is: "niets in de spelmap" is niet hetzelfde als
"geen dongle in de game". Deze sectie meet per game de drie mogelijke plekken: **(a)** een bestand op
schijf, **(b)** een dynamisch gelinkte bibliotheek, **(c)** een **statisch in de ELF gelinkte**
dongle-runtime.

Alle tellingen hieronder zijn **hoofdlettergevoelige, letterlijke** treffers van
`strings -a <ELF> | grep -c -F -- <marker>`. `matrix32`, `matrix64`, `Sixence` en `Aksusb` zijn in alle
vier de bestanden **0** en staan daarom niet in de tabel.

| Bestand | `file` | `hasp_` | `HASP` | `DongleInitialize` | `ftdi` | `FTDI` | `Sentinel` | `DONGLE NOT FOUND` | dongle-symbool in `--dyn-syms` | dongle-lib in `DT_NEEDED` |
|---|---|---|---|---|---|---|---|---|---|---|
| `GhostBusters/game` (11.057.832 B) | ELF **32-bits** i386, stripped | **58** | 13 | **1** | **32** | **16** | 0 (wel 1× lowercase `sentinel`) | 0 | nee | nee (14 `NEEDED`) |
| `Terminator Salvation /game` (7.764.284 B) | ELF **32-bits** i386, stripped | **13** | 3 | **1** | 0 | 0 | 0 (wel 1× lowercase) | 0 | nee | nee (13 `NEEDED`) |
| `Halo Fireteam/g7/halo/game` (33.554.432 B) | ELF **64-bits** x86-64, geen sectiekoppen | **45** | 31 | 0 | **32** | **16** | **48** | 0 | nee | nee (13 `NEEDED`) |
| `Halo Fireteam/g7/halo/game_original` (31.270.111 B) | ELF **64-bits** x86-64, stripped | **21** | 16 | 0 | 0 | 0 | **25** | 0 | nee | nee (13 `NEEDED`) |

De "dongle-achtige naam" die `--dyn-syms` oplevert is in alle vier de gevallen een **valse positief van
het zoekpatroon `matrix`**: het zijn OpenGL-functies (`glMatrixMode`, `glMultMatrixf`,
`glUniformMatrix4fv`, `glLoadMatrixf`, …). Er staat dus in **geen** van de vier een dongle-achtig
**dynamisch** symbool, en geen van de vier `DT_NEEDED`-lijsten bevat een `hasp`/`dongle`/`ftdi`-
bibliotheek — een `libhasp_linux_*.so` zoals Jurassic Park en NERF die hebben, zit hier dus niet in de
linklijst. Omdat alle vier de bestanden **gestript** zijn, is `readelf -sW | grep -iE 'hasp|dongle|ftdi'`
in alle vier leeg: "statisch gelinkt" is hier dus gemeten aan de **teksten in het bestand**, niet aan een
symbooltabel.

Wat de markers letterlijk zijn (geciteerd uit `strings`, per bestand ontdubbeld):

| Bestand | Letterlijke regels |
|---|---|
| GhostBusters (**82** unieke regels met `hasp`) | `DongleInitialize() FAILED!`, `Dongle noise error, missing dongle suspected`, `Dongle Not Open, ReInit Handles`, `ReInitAladdinDongles: resetting hasp daemon`, `USB Bus Reset: ReInit Dongle Handles`, `Did not find exactly one FTDI chip (found: %d)`, `FTDI read fail NODEV`, `cmgr_connection_add(): Unable to set baud rate: %d (%s). Resetting FTDI chip...`, `hasp_classic_login: search local: status: %u`, `hasp_legacy_decrypt: LowAPI status %u`, `<hasp4/>`, `<haspid>%u</haspid>`, `PCM_IS_HASP`, `R_GET_HASP_CODE` |
| Terminator (**35** unieke regels met `hasp`) | `DongleInitialize() FAILED!`, `ReInitAladdinDongles called: %d times`, `<hasp type="HASP-HL" />`, `<hasp type="HASP-SL" />`, `<hasphl/>`, `<haspscope>`, `HASP_ALI_TIMEOUT`, `nethasp.ini` |
| Halo (`game`) | `Sentinel HASP`, `<hasp configuration="sentinelhl,driverless" />`, `/dev/aks/sentinelhid`, `Failed to retrieve list of connected Sentinel HL keys, (error 0x%x/0x%x)`, `Sentinel HL %llu failed to execute LOGIN command (error 0x%x)`, `Did not find exactly one FTDI chip (found: %d)`, `FTDI force reset`, `cmgr_connection_add(): Unable to set baud rate: %d (%s). Resetting FTDI chip...`, `Dongle Error! Code: %i, Count: %i` |
| Halo (`game_original`) | dezelfde Sentinel-HASP-HL-regels (`Sentinel HASP`, `/dev/aks/sentinelhid`, `Sentinel HL %llu failed to execute …`) maar **zonder** `ftdi`/`FTDI` en **zonder** enige `Dongle`-regel |

**De uitkomst per game:**

| Game | Waar de verwijzing zit | Beslissende gemeten marker | Aantal |
|---|---|---|---|
| **GhostBusters** | **(c) statisch in de ELF** — en géén donglebestand in de spelmap | `DongleInitialize() FAILED!` plus de Aladdin/`hasp_`-API-teksten plus libftdi-teksten, alle in `game` | 1 + 58 `hasp_` + 32 `ftdi` |
| **Terminator** | **(a) een bestand op schijf** (`hasp/`) **én (c) statisch in de ELF** | de map `Terminator Salvation /hasp/` in de spelmap; en in `game` de regel `DongleInitialize() FAILED!` + 13 `hasp_` | 5413 bestanden + 1 + 13 |
| **Halo** | **(a) een bestand op schijf** (`g7/halo/fff4`) **én (c) statisch in de ELF** (Sentinel HASP HL, driverloos) | 48 × `Sentinel`, 45 × `hasp_`, 32 × `ftdi` | 48 / 45 / 32 |
| **Halo** (`game_original`) | **(c) statisch in de ELF**, maar zonder het FTDI-deel | 25 × `Sentinel`, 21 × `hasp_` | 25 / 21 |
| alle vier | **(b) dynamisch gelinkt: nee** | 0 dongle-symbolen in `--dyn-syms`, 0 dongle-libs in `DT_NEEDED` | 0 |

**Wat er in `Terminator Salvation /hasp/` staat (gemeten).** Let op: die mapnaam eindigt op een
**spatie** — het pad is letterlijk `…/Teknoparrot/Terminator Salvation /hasp`. Er staan **5413**
bestanden in, geen submappen, samen **174.016 B**. De naamvorm is 3736 namen van 32 hextekens, 1676 van
64 hextekens en 1 naam van 11 tekens: `hhl_mem.dmp` (128 B). De groottes zijn 5410 × 32 B, 1 × 128 B,
1 × 256 B en 1 × 512 B. Een `ServiceDongleApp` zit **niet** in `hasp/` maar ernaast:
`Terminator Salvation /ServiceDongleApp/ServiceDongleApp` (8.994.378 B, ELF 32-bits i386, **niet**
gestript, met `debug_info`) plus een `data/`-map.

**Wat dit rechtzet:**

* De HASP-tabel hierboven zegt bij **GhostBusters** "niets", en dat betekent **"geen donglebestand in de
  spelmap"**, niet "geen dongle in de game". In `GhostBusters/` staan inderdaad alleen
  `utils/splash/dongle-1024x768.cfg` en `…jpg` (een splash-afbeelding), maar de **ELF zelf** draagt
  58 `hasp_`-strings, 32 `ftdi`-strings en de letterlijke regel `DongleInitialize() FAILED!`. De
  dongle-verwijzing is dus niet afwezig; ze zat alleen niet in de spelmap.
* **Terminator** staat volgens `patches/Terminator/game.ini` op `Status = playable` en draait via
  BudgieLoader. Zijn `hasp/`-map is een **bestand op schijf** (5413 bestanden) en de ELF draagt daarnaast
  zijn eigen HASP-verwijzing. Deze meting zegt dus **niet** dat die map de start blokkeert: volgens
  hetzelfde `game.ini` is de game juist speelbaar.
* **GhostBusters** staat in `patches/GhostBusters/game.ini` op `Status = parked`, met als note dat de
  lader tot "Starting Game" komt en dat *"wat er dan blokkeert (dongle, OpenGL-context) **niet gemeten**"*
  is. Die bewering neem ik hier **niet** over en versterk ik niet: gemeten is dat de dongle-runtime **in
  de ELF aanwezig is**; dat die runtime ook **draait en de start tegenhoudt** is daarmee niet aangetoond.
  Daarvoor is een start met log nodig, en dat is hier niet gedaan.
* De dongle-runtime van **Halo** is een andere dan die van GhostBusters/Terminator: Sentinel HASP **HL**
  met de driverloze configuratie `sentinelhl,driverless` en `/dev/aks/sentinelhid`, tegenover de
  HASP-**classic**-API bij GhostBusters/Terminator (`hasp_classic_login`, `HASP_ALI_TIMEOUT`) met
  Aladdin/FTDI-teksten. Dat volgt uit de markers, niet uit een versieclaim van de fabrikant.
* De lader levert **geen** dongle-bibliotheek mee: `ElfLdr2/libs/` heeft 77 entries en
  `ElfLdr2/x64/libs/` 19, en `grep -iE 'hasp|dongle|ftdi'` is in beide **leeg**. Of `BudgieLoader.exe` de
  dongle dan in zijn eigen runtime nabootst (zoals §3b van de takenlijst stelt), is hier **niet**
  gemeten — dat vergt het uitvoeren of openbreken van die PE.

**Wat ik niet heb kunnen vaststellen:**

* of de statisch aanwezige HASP/FTDI-code daadwerkelijk **wordt aangeroepen** en of die aanroep de start
  blokkeert (geen start, geen log, geen `strace`; dat mag op deze bench niet);
* of de HASP/FTDI-runtime in GhostBusters en Halo door de **ontwikkelaar** is meegelinkt of door een
  **pakker/wrapper** is toegevoegd — de bestanden zijn gestript, dus er is geen symbooltabel om op af te
  gaan;
* waarom `Halo/game_original` **geen** `ftdi`/`FTDI`-strings heeft terwijl `Halo/game` ze wel heeft
  (21/16 tegenover 45/31 voor `hasp_`/`HASP`): de twee bestanden verschillen dus ook in de dongle-runtime,
  niet alleen in grootte;
* of de 5413 bestanden in `Terminator Salvation /hasp/` door de game worden gelezen (de ELF noemt alleen
  `nethasp.ini` en `hasp_`-API-teksten; een pad naar die map is niet gemeten);
* wat `ServiceDongleApp` doet en of de game die start (het is een niet-gestripte 32-bits ELF van
  8.994.378 B, maar de aanroep is niet gemeten);
* of de `Sentinel`-treffers in Halo bij de **game** zelf horen of bij een meegeleverde HASP-runtime: de
  strings staan in het bestand, de herkomst is zonder symbolen niet vast te stellen.

## Wat er aan dongle-lagen al gebouwd is

Op de Deck:

```
$ ls -1 ~/.local/share/ravengun/hasp/
Halo Fireteam
ravengun-hasp-x64.so
ravengun-hasp.so

$ ls -1R ~/.local/share/ravengun/hasp/Halo Fireteam
dongle.json
memory/00000000000000001111111111110100
memory/00000000000000001111111111110101
memory/fff4
```

In deze werkmap: `dongles/README.md`, `dongles/schema.json`, `dongles/_template/dongle.json`,
`dongles/_voorbeelden/{region-pcb,sega-scan}.json`, `dongles/halo-fireteam/{dongle.json,memory/*}`,
`dongles/lets-go-safari/dongle.json`, `dongles/src/{dongle.c,dongle.h,hasp_preload.c,selftest.c,rgd1.py}`.

`dongles/src/dongle.c` is 15 KB portable C99 zonder afhankelijkheden; de zelftest tegen de **echte**
Halo-dongle geeft volgens `findings/OPDRACHT-voor-de-loader-bouwer.md` §3 `GESLAAGD — 0 fouten`.

**De dongle-laag is dus gebouwd voor HASP.** Voor Matrix (27 functies) en `sega-scan` is het een raamwerk.

---

## De projectafspraak

`docs/research/loskoppelen-teknoparrot.md` regel 8:

> Projectafspraak die hier geldt: dongle-, keychip-, regio-, integriteits- en abonnementsafhandeling bouwt
> RavenGun niet. Die onderdelen staan hieronder wel genoemd (zodat het overzicht compleet is), maar zonder
> uitwerking.

Dit tussenrapport beschrijft daarom alleen **welke** laag een game nodig heeft en **met welk bewijs** — niet
hoe je hem bouwt of omzeilt.

## Hoe je dit narekent

Alles zonder een game te starten en zonder iets naar de NAS te schrijven. Let op: die NAS is een
CIFS-mount met **`rw`** in de opties (gemeten met `mount` op 2026-10-12), dus "read-only" is hier een
**afspraak van ons**, geen eigenschap van de mount. Er is bij deze meting niets naar de NAS geschreven.

```bash
NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot
R=~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2

# let op: de mapnaam van Terminator eindigt op een SPATIE
F_GB="$NAS/GhostBusters/game"
F_T="$NAS/Terminator Salvation /game"
F_H="$NAS/Halo Fireteam/g7/halo/game"
F_HO="$NAS/Halo Fireteam/g7/halo/game_original"

# 1. 32- of 64-bits
file "$F_GB" "$F_T" "$F_H" "$F_HO"

# 2. staat er een hasp/dongle-achtige bibliotheek in de DT_NEEDED-lijst?
readelf -d "$F_GB" | grep -E '\(NEEDED\)'

# 3. dynamische symbolen (let op: 'matrix' levert hier alleen OpenGL op)
readelf --dyn-syms -W "$F_GB" | grep -iE 'hasp|dongle|ftdi|matrix|sentinel|aksusb|aladdin'

# 4. de markers tellen en de treffers zelf tonen
for m in hasp_ HASP DongleInitialize 'DONGLE NOT FOUND' ftdi FTDI Aksusb Sentinel matrix32 matrix64 Sixence; do
  printf '%-18s %s\n' "$m" "$(strings -a "$F_GB" | grep -c -F -- "$m")"
done
strings -a "$F_GB" | grep -iE 'hasp|dongle|ftdi|aksusb|sentinel|matrix[0-9]|sixence' | sort -u | head -12
strings -a "$F_GB" | grep -E 'DongleInitialize|DONGLE NOT FOUND|Dongle' | sort -u

# 5. levert de lader zelf een dongle-bibliotheek mee?
ls -1 "$R/libs/" | grep -iE 'hasp|dongle|ftdi'        # leeg
ls -1 "$R/x64/libs/" | grep -iE 'hasp|dongle|ftdi'    # leeg
ls -1 "$R/libs/" | wc -l                              # 77
ls -1 "$R/x64/libs/" | wc -l                          # 19

# 6. de hasp-map van Terminator (nogmaals: spatie in de mapnaam)
H="$NAS/Terminator Salvation /hasp"
find "$H" -maxdepth 1 -type f | wc -l                                              # 5413
find "$H" -maxdepth 1 -type f -printf '%f\n' | awk '{print length($0)}' | sort -n | uniq -c
find "$H" -maxdepth 1 -type f -printf '%s\n' | sort -n | uniq -c
find "$H" -maxdepth 1 -type f -printf '%s\t%f\n' | sort -n | awk '$1 > 32'
```

De kant-en-klare scripts staan in de Windows-werkmap als `re/dongle-statisch-meting.sh`,
`re/dongle-statisch-meting2.sh` … `re/dongle-statisch-meting6.sh`; op de Deck als
`/home/deck/dongle-statisch-meting*.sh`, met de uitvoer in `/home/deck/_dongle_statisch{,2,3,4,5,6}.out`
en in deze werkmap in `re/scratch/dongle_statisch{,2,3,4,5,6}.out`. Ze lezen alleen de
NAS-mount en `ElfLdr2/`, starten geen game en schrijven alleen hun eigen uitvoerbestand in `/home/deck/`.
