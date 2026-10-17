# Silent Scope: Bone Eater zonder TeknoParrot (TeknoMacaw64.exe vervangen)

Stand 2026-10-17. Alles is statisch gemeten (objdump, strings, het logboek van de laatste dubbelklik-run); er is niets gestart.
Status: **kandidaat; `ea3_boot` viel stil door een ontbrekende `accountid` (sectie 5, opgelost en zonder spel getest), volgende proef op de tv**.
De huidige `game.ini` blijft op TeknoMacaw.

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
3. **De handtekeningen van de AVS-functies** (`property_*`, `ea3_boot`) komen uit de spice2x-bron, niet uit een eigen meting. `avs_boot` is wel
   gemeten (sectie 5: 6 argumenten, 5e = log-schrijver) en krijgt nu een schrijver. Faalt een stap, dan zegt `rg-macaw.log` welke.
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

## 5. Waarom `ea3_boot` het proces stil liet verdwijnen (2026-10-17, avond)

**Oorzaak (gemeten, reproduceerbaar zonder het spel):** ea3 (`libavs-win64-ea3.dll`, functie 0x180009050, controle op 0x1800098a3) stopt
met een fatale melding `F:ea3: boot: bad account ID(NIL/NIL)` als `/ea3/id/accountid` leeg is en de AVS-omgeving geen
`/env/profile/accountid` heeft. De dump van `prop/ea3-config.xml` heeft het veld `accountid` niet (alleen `pcbid` en `hardid`). `log_fatal`
(`XCnbrep700017a`) schrijft de regel en stopt met een breekpunt (`int3`); omdat AVS standaard een lege schrijver gebruikt (0x18009ed40 is een
kale `ret`), zag niemand de melding, en onder Wine met `WINEDEBUG=-all` bleef alleen "exitcode 0" over.

**Wat de lader nu doet** (`tools/rg-macaw/rg_macaw.c`, logica in `rg_macaw_logic.h`):
* `avs_boot` krijgt een log-schrijver. De handtekening is gemeten (disassemblage van 0x180030f90): `(config, std_heap, grootte, intern blok,
  log_schrijver, schrijver_context)`; de schrijver is `void (*)(const char *chars, uint32_t nchars, void *ctx)` in de gewone x64-conventie
  (MinGW-standaard). De regels komen als `AVS: ...` in `rg-macaw.log` (het niveau `ALL` staat al in `avs-config.xml`).
* stdout/stderr van wat daarna geladen wordt gaat naar `rg-macaw-stdio.log` (SetStdHandle voor het laden van de dll's; uitzetten met
  `RG_MACAW_STDIO=0`).
* een uitgangswacht: haken op `ExitProcess`, `RtlExitUserProcess` en `TerminateProcess` (alleen op zichzelf) loggen de aanroepketen
  (`module+offset`), een vectored exception handler logt toegangsfouten e.d., en `atexit` meldt een gewone exit. Zo staat in het log wie het proces stopt.
* `/ea3/id/accountid` wordt in de tijdelijke kopie toegevoegd (`0000000000000001`, **gegokt**: elke niet-lege waarde volstaat voor de controle;
  hij komt alleen terug als `/env/profile/account_id`). Overschrijven met `RG_MACAW_ACCOUNTID=<waarde>`; `none` laat hem weg (reproduceert de fout).
* `softid` blijft toegevoegd (`012199999999`, bemanitools/spice2x-standaard; ea3 zet er `license_id`/`software_id` mee).
* de teruggave van `ea3_boot` wordt alleen gelogd (void of bool, onbekend; de oude `< 0`-controle kon een goede start afkeuren).
* `RG_MACAW_NOGAME=1`: alleen AVS + `ea3_boot`, zonder `arkndd.dll` te laden (`RG_MACAW_NOGAME_SECS`, standaard 15 s wachten). Daarmee test
  `tools/rg-macaw/nogame-test.sh` het hele pad onder Wine, zonder scherm, in een wegwerpmap.
* overige schakelaars (omgeving): `RG_MACAW_SERVICES=<url>|offline`, `RG_MACAW_PCBID`, `RG_MACAW_HARDID`, `RG_MACAW_HEAP_MB`.

**Wat `ea3_boot` minimaal nodig heeft om offline te booten** (gemeten met `nogame-test.sh`, AVS 2.16.3, Wine; plus disassemblage):
1. `/ea3/id/pcbid`: even aantal hexcijfers in hoofdletters (`00010203040506070809` is goed; ea3 meldt anders `bad pcbid`);
   `/ea3/id/hardid` zonder controle; `soft/model` (3 tekens), `dest`, `spec`, `rev` (1 hoofdletter) en `ext` (8 t/m 11 cijfers) volstaan voor "bad soft id code".
2. `/ea3/id/accountid` niet leeg (zie boven) - **dit was het ontbrekende stuk**.
3. `/dev/nvram/coin.xml` moet bestaan (`conf/nvram/coin.xml`; de dump heeft hem). Zonder: `F:ea3-pos: ea3_posev_init: reading opt file failed.`
   en hetzelfde stille einde.
4. Een e-amusement-server is **niet** nodig: met de URL uit de dump (`https://eamuse.konami.fun/...`) of met `RG_MACAW_SERVICES=offline`
   (`http://127.0.0.1:1/`) bootte `ea3_boot` beide keren door (`M:ea3_boot: complete`); de services-aanroep faalt later asynchroon
   (`bad services URL` / `connection fail`, `boot_status: 1,2002,915`) en dat is niet fataal. ea3 heeft geen aparte offline-vlag nodig;
   `ea3_boot` geeft 0 terug. Standaard laat de lader de URL uit de dump staan (zoals onder TeknoParrot; met netwerk probeert ea3 dus die host);
   `RG_MACAW_SERVICES=offline` voorkomt dat.
5. De `/ea3/pos/*`-instellingen die OpenParrot's Konami-lader (`OpenParrotKonamiLoader/src/libavs-win32.cpp`, `eaBootExt`) met de hand
   aanmaakt zijn hier overbodig: de dump heeft de volledige `<pos><coin>...` en `<eacoin>` (OpenParrot's spel, HGT, had ze niet). Dezelfde
   volgorde geldt wel: `avs_boot` -> `ea3_boot` met knooppunt `/ea3`. OpenParrot roept de 32-bit `avs_boot` met 7 argumenten (aparte
   AVS-heap met grootte, schrijver `ReturnsFalse`); de 64-bit 2.16.3 heeft er 6 (gemeten), met de schrijver als 5e.

**Let op (NAS):** AVS schrijft tijdens het draaien in `conf/nvram` en `conf/raw` (klok, bookkeeping, sntp-reservekopie, logboek). In de view is
`conf` een symlink naar de NAS. `nogame-test.sh` werkt op een kopie; de echte run schrijft op de NAS tot de schrijflaag er is.

**Bewijs op de tv** (in `views/SilentScopeBoneEater/rg-macaw.log`, in deze volgorde):
```
uitgangswacht: ExitProcess aan, RtlExitUserProcess aan, TerminateProcess aan
avs_boot (heap 64 MB, log-schrijver 0000...)
AVS: [...] I:boot: AVS2 2.16.3 r7106,win64            <- bewijst dat de schrijver aankomt
AVS gebooted
ea3-config.xml: accountid toegevoegd
dll_entry_init(NDD:A:A:A:2016122700)
dll_entry_init gelukt; ea3_boot
AVS: [...] I:ea3: setenv : /env/profile/account_id='0000000000000001'
AVS: [...] M:ea3_boot: complete
EA3 gebooted (ea3_boot gaf 0, 0x0); dll_entry_main
```
Stopt het toch weer, dan staat de reden erin: de laatste `AVS: ... F:...`-regel, en `ExitProcess(...)`/`RtlExitUserProcess(...)` met
de aanroepketen, of `uitzondering 0x...`/`ONAFGEHANDELDE uitzondering ...` (het breekpunt van `log_fatal` is `0x80000003` op
`libavs-win64.dll+0x9b94`). `rg-macaw-stdio.log` heeft wat het spel zelf naar stdout/stderr schreef.

**Niet getest:** de volledige start met `arkndd.dll` (dat is de tv-proef). Onbekend is of het spel na `dll_entry_init` zelf nog iets aan
`/env/profile/*` of `/ea3` verwacht dat in de NOGAME-proef ontbreekt.

Test zonder spel (geen tv): `tools/rg-macaw/build.sh --out <map>` en dan
`tools/rg-macaw/nogame-test.sh "<NAS>/Silent Scope Bone Eater" <map>/rg-macaw.exe [RG_MACAW_SERVICES=offline]`; exit 0 = "ea3_boot: complete".
Met `RG_MACAW_ACCOUNTID=none` komt de oorspronkelijke fout terug (`F:ea3: boot: bad account ID(NIL/NIL)`).

## 6. Wit venster na een geslaagde ea3-boot (tv-meting 2026-10-17 18:52)

**Stand van de meting:** ea3 bootte ("EA3 gebooted ... dll_entry_main"), het proces liep 4 minuten, maar het beeld bleef een wit staand venster.
`xrpc: apply: cannot get url: "services"`, `boot_status: 1,2002,0` en `W:pkglist: ... no list` elke 20 s zijn de normale offline-meldingen (ook in de proef
zonder spel); ze houden het spel niet tegen. Wat wel de oorzaak is, staat in hetzelfde log:

```
M:MiCmd: COM Port 1 Open Error
M:MiCmd: Port 1 : Error 3 Node 1, step 1
M:MiCmd: MiCmd ErrorOut 3 Node 1
```
`MiCmd` komt uit **Konami's eigen libacio.dll** (strings: alleen daar, niet in arkndd.dll). Het spel laadde dus het ORIGINELE `libacio.dll` van de NAS, geen
`rg-libacio.log`, en `modules/libacio.dll` in de view was nog de symlink naar de NAS.

**Oorzaak (gemeten):** de gebruikte `dropin.ini` had `[libacio.dll] To = modules`. `To` is relatief aan de map van het spel (`modules/`, `dirname(pl.game)` in
`dropin_plan`), dus het bestand kwam in `modules/modules/libacio.dll` (zie `drop-in: libacio.dll -> .../modules/modules/libacio.dll` in het direct-log). Met `To = .`
komt het op de plek van het origineel. (De kandidaat `SilentScopeBoneEater.dropin.ini` in deze map is gecorrigeerd.)

**Waarom het venster dan wit blijft:** het spel heeft een IO-toestandsmachine (arkndd.dll 0x18000b240, toestand in 0x18018bcf0). Die moet via
`ac_io_begin` -> `ac_io_is_active2`/`is_active_device` -> `*_create_get_status_thread` -> `ac_io_nddb_req_solenoide_control` -> COM2 -> ... toestand 9 komen
voor de hoofdlus verder gaat. Met het origineel meldt `is_active2` dat er niets is (geen seriele poort 1), de machine blijft hangen en er wordt niets getekend.
Onze `libacio.dll` beantwoordt alles met "aanwezig" (zie rg_libacio.c).

**Wat daarna de volgende blokkade is (gemeten aan arkndd.dll, voorspeld):** in toestand 1 opent het spel zelf **"COM2"** (`CreateFileA`, 0x18000b32f), zet
9600 8N1, stuurt elke frame 6 bytes (`03 8a 00 00 00 8d`) en leest 0x68 bytes antwoord (0x18000b910). Faalt het openen, dan roept het `DebugBreak` aan (0x18000b351).
In Wine is `com2` -> `/dev/ttyS1` (root:uucp, niet te openen als gebruiker); onder TeknoParrot deed TeknoMacaw hier iets wat wij niet kennen. Uit de code
volgt: het antwoord heeft geen kop en geen controlesom, alleen vier bytes (offset 0x63..0x66) gaan naar een statusdword. Daarom haakt `rg-macaw.exe` nu de
Kernel32-imports van arkndd.dll (`install_fakecom`: CreateFileA, ReadFile, WriteFile, ClearCommError, SetupComm, PurgeComm, Get/SetCommState,
Get/SetCommTimeouts, alleen de IAT van die ene module) en geeft een nep-handle als het echte openen van een COM-poort mislukt: schrijven lukt, lezen geeft een
volle buffer nullen. **Gegokt:** dat een antwoord van nullen ("niets") door het spel als gezond apparaat wordt gezien; het echte apparaat (waarschijnlijk het
geweer-/lampenbord) is niet nagebootst. Uitzetten: `RG_MACAW_FAKECOM=0`.

**Nieuw in het log van de lader:**
```
libacio.dll: ONZE vervanger geladen (...\modules\libacio.dll)     <- of: WAARSCHUWING: libacio.dll is NIET onze vervanger ... To = .
virtuele COM: 10 van 10 imports van het spel gehaakt (...)
virtuele COM: CreateFileA(COM2) faalde (5); nep-handle ...         <- zodra het spel COM2 opent
virtuele COM: eerste WriteFile (6 bytes: 03 8a 00 00 00 8d)
virtuele COM: eerste ReadFile (104 bytes, nullen terug)
```
en `rg-libacio.log` in de hoofdmap van de view ("rg-libacio geladen", "ac_io_begin(poort=...)", "tp-shm gekoppeld"). Blijft het beeld dan toch wit, dan zeggen deze regels
hoe ver de IO-machine kwam: ontbreekt "eerste WriteFile", dan bleef hij voor COM2 hangen (kijk in rg-libacio.log welke `eerste aanroep:`-regels er wel zijn).

**Niet beoordeeld:** `Render As Landscape`, ScreenFix en de volledigscherminstelling van TeknoMacaw. Het witte venster past bij een swapchain die nooit gevuld wordt omdat de
hoofdlus nog op de IO wacht; dat de venstergrootte (staand) er is, laat zien dat het spel zijn video-init wel deed. Pas als de IO-machine door is en het beeld
dan nog wit/verkeerd staat, zijn dat de volgende verdachten.

**Test zonder spel:** `tools/rg-macaw/fakecom-test.sh "<NAS>/Silent Scope Bone Eater" <map>/rg-macaw.exe` bouwt een nep-`arkndd.dll` (`fakecom_game.c`) die dezelfde
seriele aanroepen doet, draait de hele route onder Wine in een wegwerpmap en controleert dat COM2 geopend, beschreven en gelezen kan worden (exit 0).
