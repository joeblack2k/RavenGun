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
