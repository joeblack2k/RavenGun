# Silent Scope: Bone Eater zonder TeknoParrot (TeknoMacaw64.exe vervangen)

Stand 2026-10-17. Alles is statisch gemeten (objdump, strings, het logboek van de laatste dubbelklik-run); er is niets gestart.
Status: **kandidaat, klaar voor een eerste proef op de tv**. De huidige `game.ini` blijft op TeknoMacaw.

## 1. Wat TeknoParrot hier doet

`build/rg-direct --plan SilentScopeBoneEater` toont: lader `TeknoParrot/TeknoMacaw64.exe` (gepakt, sectie `.teknoGo`, importeert alleen
KERNEL32/ADVAPI32), **geen** kern (`core_dll` leeg). De gesloten `TeknoParrot64.dll` speelt hier dus geen rol. TeknoMacaw is een
Konami-AVS2-lader (de tegenhanger van Konami's `launcher.exe`); zijn logboek (`views/SilentScopeBoneEater/tp_log.txt`) en stdout geven
de stappen:

| Stap in TeknoMacaw | Bewijs | Wat wij doen |
|---|---|---|
| werkmap = map boven `modules\`, DLL-zoekpad = `modules\` | stdout "Working directory set to", "DLL search path includes" | `rg_macaw.c`: SetCurrentDirectory + SetDllDirectory |
| netwerkadapter zoeken ("Usable network adapter found") | tp_log | niet nodig: wij zetten sntp/ea_on = 0 in de AVS-config; ea3 start asynchroon |
| `AVS booted successfully` | tp_log | `avs_boot` uit `libavs-win64.dll` (2.16.3, r7106) met `prop/avs-config.xml` |
| `ea3boot: Configuration loaded from prop files`, `Set up custom values`, `Loaded game dll`, `EA3 boot successful` | tp_log | `prop/ea3-config.xml`, softid toegevoegd als hij ontbreekt, `dll_entry_init("NDD:A:A:A:2016122700", /config van ark-config.xml)`, `ea3_boot(/ea3)`, `dll_entry_main()` |
| Patreon-abonnementscontrole ("[SUBSCRIPTION MODE] Activation key ok!") | stdout | vervalt (`Patreon = false` in de kandidaat) |
| libacio: de kast-I/O (invoer uit `TeknoParrot_JvsState`, "our libacio code" in `AcioPipe.cs`) | AcioPipe.cs ("For use with our libacio code"); de dump heeft een eigen `modules\libacio.dll` die een seriele poort wil | `libacio.dll` als vervanger (hieronder) |
| USB-sleutel (`ess_usbkey_*`, `/config/nousbkey`) | `ark-config.xml` heeft `nousbkey = 0` ("niet 0 = geen sleutel") | de lader zet in een tijdelijke kopie `nousbkey = 1` |

Het spel laadt `libacio.dll` gewoon uit `modules\` (import van `arkndd.dll`); de andere borden van het origineel (secplug, kfca, hbhi,
...) importeert `arkndd.dll` niet: er is **geen beveiligingsstekker** om na te bootsen.

## 2. De vervanging (`tools/rg-macaw/`)

* `rg_macaw_logic.h`: zuivere logica (pad, XML-waarden, softwarecode, tekstvervanging, AVS-naamtabel, bmpu-blok). Native getest.
* `rg_macaw.c` -> `rg-macaw.exe`: de lader. AVS-functies bij NAAM uit de dll (`XCnbrep7000129` = boot, enz.). Elke stap logt naar
  `rg-macaw.log` in de hoofdmap van de view; een ontbrekende naam of falende stap stopt met een duidelijke regel.
* `rg_libacio.c` -> `libacio.dll`: exporteert exact de 35 functies die `arkndd.dll` importeert (`libacio.exports`, getest tegen de
  bron). Levert knoppen/munt/geweerpositie uit `TeknoParrot_JvsState` (indeling KonamiAcio).
* `build.sh`: MinGW-w64 in de gepinde podman-image van de OpenParrot-build, zonder netwerk. Uitvoer in
  `patches/SilentScopeBoneEater/dropin/` (sha256 in het rapport; twee keer bouwen geeft dezelfde bestanden).

### Wat gemeten is
* **AVS-versie 2.16.3** (`FileVersion` van `libavs-win64.dll` en `-ea3.dll`). Namen volgens de spice2x-tabel voor 2.16.3-2.16.8;
  kruiscontrole tegen de dll zelf: `libacio.dll` importeert op ordinaal 298 (= boot, index 0x129 + 1), en `arkndd.dll` importeert
  precies de ordinalen van property_search (162), property_node_create (163), property_create (145), avs_fs_open/read/close (79/82/86).
* **Statusblok bmpu** (uit `arkndd.dll`, functie 0x18000b6e0 en 0x18000ad3c..0x18000b028, blok L):
  `L[4]` bit7/6/5 -> knopmasker bit 8/9/10, `L[5]` bit0..4 -> bit 3..7 (start, menu op/neer), `L[7]` bit4/1/0 -> bit 0/1/2
  (test, service, munt), `X = L[8]<<4 | L[9]>>4`, `Y = L[10]<<4 | L[11]>>4` (12 bit, links/boven = 0). Het masker-bit-nummer
  is gelijk aan het bitnummer in onze tp-shm-indeling voor 0..10, wat de keuze steunt. Munten: `ac_io_bmpu_current_coinstock`
  levert een oplopende teller; het spel neemt het verschil (0x18000af12).
* Het nddb-statusblok (`L+0x38`) wordt na het lezen in de ringbuffer gekopieerd en daar door niemand uit gelezen (alleen +0x98, +0x9c,
  +0xa8, +0xac). Wij leveren daar nullen.
* Het spel heeft drie velden voor trekker, scope links, scope rechts (object +0x33c/+0x340/+0x344, vtable 0x1800df168).

### Wat NIET gemeten is (de eerste proef moet dit beslissen)
1. **De scopeknoppen.** Welke bits de scope links/rechts voeden is niet uit de code te halen zonder het hele object na te lopen.
   Standaard zet de scope (onze granaat) beide bits L[4]&0xC0; zijknop = links. Past dat niet, dan zonder herbouwen:
   `RG_ACIO_SCOPE_L4=0x80|0x40|0x20` (welke L[4]-bits de scope zet) of `RG_ACIO_NDDB_SCOPE=<offset>:<masker>,...` in `dropin.ini` `Env`.
   `rg-libacio.log` toont de eerste 20 statusblokken met invoer.
2. **Richting en bereik van het vizier.** X/Y gaan 1:1 van de 16-bit tp-shm-as naar 12 bit. Het spel heeft eigen gunkalibratie
   (`guncontrolleCheck`); staat het vizier gespiegeld, dan is dat de eerste aanpassing (`rgm_acio_pack_bmpu`).
3. **De handtekeningen van de AVS-functies** (boot met `(config, heap, size, NULL, log_cb, handle)`, `property_*`) komen uit de
   spice2x-bron, niet uit een eigen meting; de lader roept de log-callback als NULL aan. Faalt een stap, dan zegt `rg-macaw.log` welke.
4. De propertyvlaggen (`0xF` = lezen|schrijven|aanmaken|toevoegen) en of `/config` van `ark-config.xml` het knooppunt is dat
   `dll_entry_init` verwacht. Het spel zoekt zijn eigen paden absoluut (`/config/...`), dus elk knooppunt van die property volstaat.
5. De E:\-schijf (`E:\NDD-001\update\...`): de vorige runs kwamen zonder verder; bij een fout daarover `Drives = E=TeknoParrot/e` in `[Launch]`.

## 3. Launcher-wijziging (niet in `src/` gezet)
`SilentScopeBoneEater.launcher.patch` (diff tegen `src/ravengun/launch/direct.cpp`, controle: `g++ -fsyntax-only` slaagt): nieuwe
sleutel `[Loader] Exe = <naam>` = een eigen lader die als drop-in meekomt. De check "lader ontbreekt" vervalt dan; na `dropin_plan` komt
de plek in de view in elke argv op de plaats van de runtime-lader. Een naam zonder sectie in `dropin.ini` weigert de start (exit 5).
Zonder die patch blijft de game gewoon op TeknoMacaw (de binaire bestanden in `dropin/` zijn zonder `dropin.ini` inert).

## 4. De proef op de tv (jij)
1. Patch toepassen op `src/ravengun/launch/direct.cpp`, bouwen. `docs/research/losse/SilentScopeBoneEater.dropin.ini` naar
   `patches/SilentScopeBoneEater/dropin/dropin.ini`, `SilentScopeBoneEater.game.ini` als `game.ini` (alleen voor de proef).
2. `build/rg-direct --plan SilentScopeBoneEater`: argv begint met `wine .../views/SilentScopeBoneEater/rg-macaw.exe Z:\...\modules\arkndd.dll`
   (geen `TeknoMacaw64.exe`), en `dropin_files` heeft `rg-macaw.exe` (dest = view-hoofdmap) en `libacio.dll` (dest = `modules/`).
3. Dubbelklik. Verwacht in `views/SilentScopeBoneEater/rg-macaw.log` achtereenvolgens: versie 2.16.3, de `ok:`-regels, "AVS gebooted",
   softwarecode `NDD:A:A:A:2016122700`, "dll_entry_init", "EA3 gebooted", "dll_entry_main"; en in `rg-libacio.log` "rg-libacio geladen",
   "tp-shm gekoppeld", `ac_io_begin(poort=...)`. Beeld: WARNING-scherm, attract met FREE PLAY (zoals nu).
4. Invoer (muismodus): munt (5), START, trekker, scope. Kijk bij niets in `rg-libacio.log` "bmpu-blok: ctl=... L4=..." mee.
5. Geen tp_log.txt/"SUBSCRIPTION MODE" in de run en `TeknoMacaw64.exe` niet in `ps`: dat is het bewijs dat TeknoParrot weg is.
