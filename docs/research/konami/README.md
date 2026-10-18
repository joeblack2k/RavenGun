# Lethal Enforcers 3 en Gashaaaan zonder TeknoParrot

*Stand 2026-10-17. Statische analyse (objdump, eigen PE-lezer, geen game gestart). Vervolg op de rijen LethalEnforcers3 en Gashaaaan in
`docs/research/wartran/README.md` ("geblokkeerd: structuren niet gelezen"): de structuren zijn nu gelezen, de vervangers gebouwd.*

| Game | Stand | Wat er nog open is |
|---|---|---|
| **LethalEnforcers3** | **loopt zonder TeknoParrot (invoer komt aan), beeld nog zwart of soms een hang in `PeekMessageA`** (proef 17) | beeld: A/B zonder gamescope en de nieuwe waakhond (meetpunten "Proef 17"); daarna kalibratie, Option/Reload |
| **Gashaaaan** | **werkt zonder TeknoParrot tot het titelscherm** (proef 13, 2026-10-17) | niet gemeten: munt, START, worpen/inslagen per kwart, 5 minuten spelen |

Beide: `[Loader] Core = none` (geen lader, geen kern), eigen DLL's als drop-in, invoer uit het bordbeeld `TeknoParrot_JvsState` van onze
`rg-jvsbridge.exe`. `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan <Game> --patches docs/research/konami/patches` geeft voor beide
een plan zonder lader, zonder kern en met `tp_files []`; `rg-direct --tp-refs --expect-free LethalEnforcers3,Gashaaaan` meldt 0
verwijzingen.

## Wat de kern deed

De kern is ingepakt; wat hij doet is alleen te zien aan wat de game verwacht. Beide games praten niet zelf met hardware maar via Konami's
eigen bibliotheken, en die praten over een seriele poort met het bord (imports van het origineel: `CreateFileA`, `SetCommState`,
`SetCommTimeouts`, `EscapeCommFunction`, `ReadFile`/`WriteFile`):

| Game | Bibliotheek | Wie importeert hem | Waarvoor |
|---|---|---|---|
| LE3 | `iob.dll` (44 exports, `d:\projects\police\iob\Release\iob.pdb`) | `game.exe`, 37 namen, IAT `0x6272c8..0x62735c` | I/O-bord: test, service, munten, per speler start/trekker/option, lampen, schudmotoren |
| LE3 | `gunio.dll` (18 exports) | `game.exe`, 14 namen, IAT `0x62728c..0x6272c0` (thunks `0x561a0c..0x561a5a`) | geweerbord: ruwe X/Y per geweer, "geen signaal" |
| Gashaaaan | `bin/iob.dll` (30 exports, F24-familie) | `system.dll`, `testmode.dll`, `gcoin.dll`, `gutil.dll`, `player.dll`, `gamemode.dll`, `demo.dll`, `result.dll`, `howto.dll`, `soft_config.dll`, `gmain.dll` en `ballio.dll` | knoppen (START P1..P4, TEST, SERVICE), twee muntproevers, lampen, blacklight, twee balautomaten |
| Gashaaaan | `bin/ballio.dll` (63 exports) | `system.dll` (opstart), `gamemode.dll`, `player.dll`, `testmode.dll`, `demo.dll`, `gmain.dll`, 15 `stageN.dll` | balsensor (BS2): waar een geworpen bal het scherm raakt |

Of de kern deze exports onderschepte of de seriele poort nabootste is niet te zien (en maakt voor de vervanger niet uit). Wel bewezen:
de geweerkalibratie die LE3 onder de kern opsloeg (`SAVE/io.sav` op de NAS, 10 okt 09:33) heeft voor beide geweren **middelpunt (0,0)
en schalen 192/176**: zo komen ze uit `game.exe 0x51d430` als alle gemeten ruwe punten 0 waren (verschil 0 wordt 1, 192/1). Met die
kalibratie geeft elke ruwe waarde een sprong van 192 pixels; de kern leverde het richtpunt dus **niet** als lineaire ruwe waarde via
`gunio_read_data` (hij schreef waarschijnlijk zelf in de game). Daarom levert onze route een eigen `io.sav` mee (zie onder).

Verder deed de kern (profiel/`teknoparrot.ini`): `UseLocaleEmulator`, `Windowed`/`HideCursor`, en bij Gashaaaan `ScreenFix`. Die
hebben zonder kern geen vervanger (zie "niet bewezen"); de eerste kandidaat zette de locale met `[Env] LANG/LC_ALL = ja_JP.UTF-8`, sinds
proef 8 niet meer (een variabele minder; alle bestandsnamen van beide games zijn ASCII).

## Wat we vervangen: `tools/rg-konami/`

Vier DLL's, 32-bit PE, zonder C-runtime (alleen `kernel32` en `user32`, zoals `tools/rg-pqmt`), gebouwd met MinGW in de podman-container
(`tools/rg-konami/build.sh`). De exports komen uit `.def`-bestanden die uit de exporttabel van het origineel gemaakt zijn: **dezelfde
C++-namen en dezelfde ordinalen**. `tools/rg-konami/test.sh` controleert dat tegen het origineel op de NAS (alleen lezen) en controleert
ook dat elke naam die een module van de game uit die DLL importeert bij ons bestaat (anders laadt Wine de game niet). Uitkomst nu:
44/18/30/63 exports, alles gelijk, alleen systeem-imports.

De logica staat los van Windows in `konami_logic.h` en heeft een eenheidstest (`test_konami.c`, 1968 controles). Elke DLL schrijft met
`RG_KONAMI_LOG=rg-konami.log` een logboek naast zichzelf in de view: een relatief pad is relatief aan de map van **de DLL** (niet aan de
werkmap; hier zijn ze gelijk: LE3 de view-wortel, Gashaaaan `bin/`), lukt dat niet dan `%TEMP%\rg-konami.log` van de prefix. Sinds proef
8 schrijft elke DLL al in `DllMain` een **laadregel** (DLL-pad, exe van het proces, werkmap) en bij het stoppen `DllMain: proces stopt
normaal`; alleen kernel32, veilig onder de loader-lock.

Geladen worden ze **statisch**: `game.exe` (LE3) en `system.dll` (Gashaaaan, statisch geimporteerd door `ballshooting.exe`, die alleen
`SystemMain` aanroept) hebben `iob.dll`/`gunio.dll`/`ballio.dll` in hun importtabel; geen `LoadLibrary` met een pad. Wine zoekt ze eerst
in de map van de exe, en daar zet de drop-in ze (de `dest` in het plan). `DllOverride = native` (`iob=n;gunio=n` / `iob=n;ballio=n`) is
niet nodig (Wine heeft geen eigen versie) maar schaadt niet. Laadproef van de DLL's zelf (geen game): een eigen `laadtest.exe` in een
lege wegwerp-prefix met de Proton-11-wine van RavenGun, zonder beeldscherm: `LoadLibrary` lukt, `iob_get_status()` = 3, en het logboek
naast de DLL bevat de laadregel, de eerste aanroep en `DllMain: proces stopt normaal`.

### Lethal Enforcers 3: `iob.dll`

De opstartmachine van de game (`game.exe 0x51ea80`, toestand `[obj+0xe42c]`/`[obj+0xe430]`, het I/O-object staat op `[0x694150]`):
`iob_init` + `iob_start` → `iob_boot_wait() == 1` → `iob_check_need_firm_update() == 0` → `iob_check_initial_security_checked() == 0`
→ `iob_check_error() == 0` → geweerbord. Daarna elk beeld `iob_get_digital_in(&obj[0xe438])` (vorige kopie op `+0xe464`).

`IOB_DIGITAL_IN_W` (0x2a bytes, gepakt) — wat het origineel schrijft (`iob.dll 0x10006400`) en hoe de game het leest:

| Offset | Inhoud | Bewijs in `game.exe` |
|---|---|---|
| `+0x00` int | geldig (≠ 0) | `0x51db80`: munten tellen alleen als nu en vorige keer geldig |
| `+0x04` u8 | TEST | flank `0x51d860` (overal in het spel: naar het testmenu), niveau `0x51d8a0` (testmenu), I/O-test systeem item 0 |
| `+0x07` u8 | SERVICE | flank `0x51d8b0`; `0x5238a1` telt er één krediet bij |
| `+0x05` u8 | munt (alleen in de I/O-test, 10 beelden zichtbaar) | `0x547781` — **gegokt** als niveau van de muntschakelaar |
| `+0x08` u8[2] | START per speler (niveau) | `0x51d840`; I/O-test spelersrij item 0 |
| `+0x0a` u8[2] | TREKKER (niveau) | `0x51d710`; richtlus `0x51ef3c`/`0x51efbd` ("trekker vast", vlag 2) |
| `+0x0c` u8[2] | OPTION (niveau) | `0x51d760`; I/O-test item 2 |
| `+0x0e`/`+0x10`/`+0x12` u8[2] | tellers van START/TREKKER/OPTION | de game kijkt alleen "veranderd" (`0x51d810`, `0x51d6e0` = schot in de richtlus `0x51ef9a`, `0x51d730`); wij tellen bij elke druk |
| `+0x26` u8 | munten, 6 bits | `0x51db80` telt het verschil modulo 0x40 (een waarde ≥ 0x40 laat die lus nooit stoppen: wij blijven op 0..63) |
| rest | 0 | `+0x1e..+0x21` toont de I/O-test als vier getallen |

Bordbeeld → velden: test bit 0, service bit 1, munt 1/2 bit 2/3 (beide op de ene teller), per speler p: start `6+5p`, trekker `7+5p`,
Option = "reload" `8+5p` (TeknoParrot Button2 = "Player N Option"). De overige functies (lampen, schudmotoren, terugslag, firmware)
doen niets en geven "in orde".

### Lethal Enforcers 3: `gunio.dll` en `SAVE/io.sav`

`gunio_read_data(int geweer, float *x, float *y, int *buiten, int *ruw_x, int *ruw_y)` (`?gunio_read_data@@YAXHPAM0PAH11@Z`). Het
origineel (`gunio.dll 0x10002dd5`) geeft per as één byte ruw (0..255) en in `*buiten` bit `7-geweer` van een statusbyte. De game
(`0x51d540`, aanroep `0x51d56d`, geweer 0/1 uit de tabel `0x646770`) gooit `*x`/`*y` weg en rekent met de kalibratie uit `io.sav`:

    ruw > midden:  X = 320 - (ruw - midden) * schaal_x+      anders: X = 320 + (midden - ruw) * schaal_x-     (Y idem rond 240)

met `*buiten ≠ 0` of X/Y buiten `[0,640) x [0,480)` → -1 (buiten beeld; `0x51d67f`). De standaardschalen van de game (`0x51e9c0`: 2,5
en 1,875 = 320/128 en 240/128) laten zien dat één byte het hele scherm beslaat, hoog = links/boven. Wij geven `ruw = 255 - as` en
`*buiten` = as ≤ 1 of ≥ 254 (de offscreen-regel van de indeling WartranTroopers) **of** de knop "Reload" van het profiel (Button3 =
granaat-bit `9+5p`: herladen = buiten beeld schieten — **gegokt** dat dat in dit spel de bedoeling is).

`io.sav` (80 bytes; `game.exe 0x51dc10` schrijft, `0x51dc80` leest, bestandsroutines `0x520020`/`0x5202d0`): kop `"I/OD"`, 0,
controle2, controle1, lengte 0x3c; gegevens: frequentie-id, gekalibreerd P1/P2, per speler midden X/Y (int) en schalen X+, Y+, X-, Y-
(float). De controles zijn een CRC-32 met polynoom **0xEDB88340** (niet de standaard 0xEDB88320; tabel `0x5315b0`, `0x531650`,
`0x5316a0`): controle1 over de gegevens, controle2 over de vier bytes van controle1. **Nagerekend**: met deze CRC kloppen de controles
van de io.sav en de coin_options.sav op de NAS. Onze io.sav: midden 127, schalen 2,5 en 1,875 → scherm-X = 2,5 × as, scherm-Y =
1,875 × as (de eenheidstest rekent alle 256 waarden door). Een kalibratie in het testmenu werkt met onze ruwe waarden ook, maar de
drop-in zet elke start onze io.sav terug.

### Gashaaaan: `bin/iob.dll`

Opstart in `system.dll` (`0x10005400`): `iob_step` + `iob_get_status()` — **3** = bord draait (4 = fout, 5 = firmware); daarna sensor,
`iob_check_plug() == 1` (2 = fout). Elk beeld `iob_step`; `iob_f24_button_trg(i)` = nu wel, vorige stap niet (`iob.dll 0x1000ef90`).

| `iob_f24_button(i)` | Knop | Bewijs |
|---|---|---|
| 0..3 | START P1..P4 | `testmode.dll` INPUT CHECK `0x100045b0..0x10004775` (labels "P1 START".."P4 START") |
| 4 | TEST | label "TEST" `0x10004b05` → `button(4)`; ook de opstart (`system.dll`: firmware/fout bevestigen) |
| 5 | SERVICE | label "SERVICE" `0x10004b93` → `button(5)`; `gcoin.dll 0x1000166c`: service = krediet |
| 6..9 | 0 | komen in INPUT CHECK niet voor |

Bordbeeld: start `6+5p` (bij de indeling WartranTroopersKonami is bit 16 ook P1 aux1: dat blijft START P3, zoals nu), test bit 0,
service bit 1. `iob_f24_nr_coin(mech, &modulo)`: 6-bits teller per muntproever (munt 1 → mech 0, munt 2 "Other SW" → mech 1), modulo
0x3f (`iob.dll 0x1000f0b0`; `gcoin.dll 0x1000160f` rekent het verschil). Balautomaten (`ballout_*`): de aanvraag lukt meteen, status
"klaar, geen fout" (`system.dll 0x100050cc`/`0x1000511f`: tweede uitvoerbyte ≠ 0 = foutmelding).

### Gashaaaan: `bin/ballio.dll`

- Opstart (`system.dll` toestand 8/9): `bs2_get_status() == 2` (sensor klaar; ≤ 0 = fout), `bs2_calibration_get_state() ≠ 0`.
- `ball_io_point_get(T_BALL_IO_POINT *buf, int bytes)`: hoogstens `bytes/4` punten `{short x; short y;}` in **schermpixels**
  (`ballio.dll 0x10008840`). `player.dll 0x10001206` bepaalt de speler als `x / (sfgl_get_screen_width() / 4)`: elke speler heeft een
  kwart van het scherm. De stage-DLL en `player.dll` vragen de punten in hetzelfde beeld allebei op: `ball_io_step` bouwt de lijst,
  `point_get` verbruikt niet (zoals het origineel, dat elke stap opnieuw vult).
- `ball_io_mode_set(0)` → geen punten (het origineel roept dan een terugroepfunctie aan die niemand zet), `1` → punten
  (`gamemode.dll 0x10001417`). `ball_io_disable/enable`.
- Het origineel heeft zelf al een muisroute: in de lopende toestand wordt een klik (`check_kbd_trg(1)` + `mouse_util_get_point` uit
  `util.dll`, in vensterpixels) een inslag (`ballio.dll 0x100084f7`). Onze vertaling: **de trekker van speler p gaat in terwijl hij in
  beeld mikt → één inslag op (as_x × (B-1)/255, as_y × (H-1)/255)**, B/H uit `util.dll` `sfgl_get_screen_width/height` (dezelfde maat
  die `player.dll` gebruikt), anders de clientrechthoek van het venster uit `ball_io_boot`.
- Structuren die het testmenu leest en wij vullen: `t_bs_version` 9 bytes (`0x10002220`), `t_bs_calibout` 0x24 bytes (`0x10002410`),
  `bs2_environment_check` (twee floats, 0 = "O K" in `demo.dll 0x100027ab`). Functies die geen module importeert raken geen structuur
  aan.

## Proef 8 (tv 2026-10-17 18:14-18:19, TP-map weg)

| Game | Gezien | Uitkomst van het onderzoek | Veranderd |
|---|---|---|---|
| LE3 | `wine game.exe` zonder gamescope, 90 s zwart, geen `rg-konami.log` | De drop-ins stonden goed (`views/LethalEnforcers3/{iob,gunio}.dll` = onze sha256, `SAVE/io.sav` lokaal). `game.exe` importeert ze statisch: zonder onze DLL was het proces meteen gestopt, het draaide 90 s, dus ze waren geladen. Geen logregel omdat de toenmalige DLL pas schreef bij de eerste aanroep, en de game kwam in 90 s niet aan zijn I/O-update (`0x51ea80`) toe. Zelfde beeld als Rampage op deze route: zonder lader krijgt het exclusieve D3D-venster de voorgrond niet. De kandidaat had geen `[Display] Gamescope` (de route met de kern had die ook niet nodig). | `[Display] Gamescope = true`; laadregel in `DllMain` |
| Gashaaaan | gamescope + `ballshooting.exe`, "lader gestopt (exitcode 0)" binnen 1 s, geen `rg-konami.log` | `ballshooting.exe` (40 kB) is geen starter van een tweede exe: hij roept alleen `system.dll!SystemMain` aan. Exitcode 0 is die van gamescope (ook bij AliensExtermination en Silent Scope vandaag: "Broken pipe" + 0 na een snel einde), dus over Wine zegt hij niets. In `SystemMain` (`0x10001170`) is de eerste stap `OpenMutexA("JONG4APPLICATION")` (`0x100014f0`): bestaat die al, dan `return 0` zonder melding, nog voor er iets van iob/ballio wordt aangeroepen. Dat is de enige stille uitgang met 0 zo vroeg; of het die was is niet te zien (geen laadregel toen). Ook een DLL die niet laadt geeft hier een stille stop (WINEDEBUG=-all). | laadregel in `DllMain`; `iob.dll` meldt in `DllMain` of `JONG4APPLICATION` al bestaat |

Beide: de Japanse locale is uit de kandidaat (een variabele minder).

## Proef 11 (tv 2026-10-17 19:59, f12f0fe, TP-map weg)

**Lethal Enforcers 3** liep 150 s met onze DLL's (laadregels, `iob_init`, `gunio_boot_status`, bordbeeld, richtpunten) en kwam tot
de opstartcontrole: `I/O BOARD OK, I/O BOARD BIOS OK, SECURITY OK, GUN I/O OK, SYSTEM DATA1 CHECK`, dan rood `SYSTEM DATA1 CHECK
ERROR 2-1005-0001`. Gelezen in `game.exe`:

- Foutcode: tabel `0x691b14` (index 4 = `2-1005-0001`); de stap `0x533830(x, n)` start in een draad `0x536a90(n)` en toont bij een
  fout index `n + 4`. `n = 0` is SYSTEM DATA1, `n = 1` SYSTEM DATA2.
- `0x536a90(0)` controleert zes paren (tabel `0x649eec`): `D:\D22\DATA\check.cc` tegen `D:\D22\DATA\check.a`, en
  `DATA\check\{game,iob,gunio,pmds,msvcr71}.cc` (uit het archief `check.a`) tegen `D:\D22\{game.exe,iob.dll,gunio.dll,pmds.dll,
  msvcr71.dll}`. Een `.cc` is 8 bytes: CRC van de CRC en de CRC van het bestand (de CRC-32 met polynoom 0xEDB88340, dezelfde als van de
  .sav-bestanden; `0x5318d0` controleert het eerste woord). `0x536a90(1)`: `DATA\check\testmode.cc` tegen `D:\D22\DATA\testmode.a`.
  **Nagerekend op de NAS**: `check.cc` klopt met `check.a`, en de CRC's van de vijf originele bestanden en van `testmode.a` staan als
  `.cc` in `check.a`. Het is een bestandscontrole met een CRC, geen dongle en geen sleutel.
- Waarom het misging: de prefix heeft geen `D:` (de kern leidde `D:\D22` om naar de map van de game), en met de map van de game als
  `D:\D22` zou de CRC van ONZE `iob.dll`/`gunio.dll` niet kloppen.
- Dezelfde vaste paden elders: films `D:\D22\DATA\movies\*.m1v`, `D:\D22\DATA\DATA.a`, en de .sav-bestanden in twee kopieen
  `F:\SAVE\` en `E:\SAVE\` (`0x51fd80`/`0x51fe20`; schrijven `0x520020` naar beide, lezen `0x5202d0` eerst F: dan E:). Zonder F:/E:
  las de game in proef 11 dus ook onze `SAVE/io.sav` niet.

Nu in de kandidaat: `[Launch] Drives = D=.; E=.; F=.` (alle drie de view, voor de duur van de run) en `[Launch] ViewLinks` voor
`D22/{game.exe,iob.dll,gunio.dll,pmds.dll,msvcr71.dll,DATA}` naar de **originelen op de NAS**. Daarvoor kreeg ViewLinks in rg-direct
het doel `origineel:<pad>` (het bestand of de map van de game zelf, ook als de view daar een drop-in heeft) en mag een doel een map
zijn (`direct_files.cpp` `parse_view_links`, `direct_apply.cpp` `place_view_links`; test in `tests/test_direct_launch.cpp`; uitleg in
`docs/per-game-patches.md`). De controle leest zo Konami's bestanden in `D:\D22`, de game laadt de onze uit zijn eigen map. Er wordt
niets nagemaakt of omzeild.

**Gashaaaan**: onze DLL's laden (`mutex JONG4APPLICATION bestaat nog niet`), daarna meteen `DllMain: proces stopt normaal` zonder
`iob_boot`. `SystemMain` (`system.dll 0x10001170`) doet voor `iob_boot` (moduleboot stap 2, `0x10002460` → `0x100052e0`):
mutex → `SetCurrentDirectoryA` (map van de exe) → `system_error_init`, `ime_util_init` → AVS-opstart `0x10001f60` (geeft altijd 0;
`log_body_fatal` bij te weinig heap) → `load_inifile("BALLS.INI")` → `0x10001c70`: `NVRAM_DIR` als `/dev/nvram` en `RAW_DIR` als
`/dev/raw` mounten (`gcreate_directory`) → `soft_config.dll` `security_code_init` (leest `ROMFILE` = `D:\TYPE.BIN`) en
`soft_id_code_init` → moduleboot stap 0 en 1 (log, muis, COM, geheugen, draden, bestanden); een stap < 0 stopt. `BALLS.INI` zegt
`F:\`, `E:\` en `D:\TYPE.BIN`; geen van die schijven bestaat zonder de kern. Onder de kern stond de NVRAM in `bin\NVRAM`
(`info.bin` + mirrors `0..3`, in de schrijflaag), en de game maakte `bin\RAW` en `bin\LOG`: de kern leidde de schijven om. **Niet
bewezen** dat dit de stop is (er komt geen eigen melding); het is de enige plek tussen het laden en `iob_boot` die van de omgeving
afhangt. Nu in de kandidaat: een eigen `bin/BALLS.INI` (drop-in, tekst) met `NVRAM_DIR .\NVRAM\`, `RAW_DIR .\RAW\` en
`ROMFILE ..\TYPE.BIN` (werkmap = `bin`), zodat de NVRAM van de route met de kern blijft gelden. **Gegokt**: dat AVS een relatief pad
als bron van `/dev/nvram` aanneemt. Een schijfletter naar `bin/NVRAM` kan niet: die map is een koppeling naar de NAS en rg-direct zet
geen schijf op de NAS.

Meetvoorstel als Gashaaaan weer meteen stopt (een korte run, dus een klein logboek): `WINEDEBUG=+seh,+debugstr,warn+file,err+all`.
Te zoeken: de laatste `CreateFile`/`GetFileAttributes` voor de exit (welk pad, welke fout), een uitzondering (`+seh`), en de
AVS-logregels (`log_body_*` via OutputDebugString). Staat er niets van AVS: dezelfde run met `+relay` beperkt tot
`WINEDEBUG=+relay,+seh` en `RelayInclude=kernel32.ExitProcess;kernel32.CreateFileA` (register `HKCU\Software\Wine\Debug`) geeft de
plek van de `ExitProcess`.

## Proef 13 (tv 2026-10-17 23:48, 1aac61f)

**Gashaaaan werkt zonder TeknoParrot**: titelscherm ("投球王国ガシャーン", "コインを入れてください", CREDIT 0) na 45/90 s. De eigen
`BALLS.INI` was het: de stop in proef 11 kwam door de schijfletters.

**Lethal Enforcers 3**: schijven D:/E:/F: en de zes links in `D22/` stonden goed, maar 150 s zwart, en in `rg-konami.log` alleen de
twee laadregels (geen `iob_init`, geen bordbeeld). De game blijft dus hangen voor zijn I/O-update, en die draait in de hoofdlus.
Gelezen in `game.exe`, wat D: verandert:

- **De films.** `0x523e60` maakt van elke film `D:\D22\DATA\movies\%s` (`0x523ea2`; alleen een pad met een schijfletter blijft
  zoals het is). Zonder D: (proef 11) ging het openen mis, sloeg de game de films over en kwam hij tot de opstartcontrole. Met
  `D22/DATA` als link naar de hele DATA-map bestaan de films nu. Onder de kern speelde de eerste film (bench 2026-10-15, met Gst32,
  zonder gamescope). Nu draait hij in gamescope. Dit is het enige verschil met proef 11 dat voor de hoofdlus valt.
- **Niet de CRC-controles**: SYSTEM DATA1/2 (`0x536a90`) en ROM CHECK (`0x541ec0`, tabel `0x64d938` met `DATA.a` van 349 MB en alle
  films; alleen in het testmenu `CWtTmRomCheck`) lopen pas in de hoofdlus, in een eigen draad.
- **Waarschijnlijk niet E:/F:**: `F:\SAVE` = `view/SAVE` (met onze `io.sav`); `io.sav` wordt pas na `gunio_boot_status` gelezen.
  De volumevergrendeling `\\.\F:` (`0x51fcc0`, alleen bij een opslag) faalt hooguit en de game gaat dan door.
- Wine noemt de werkmap nu `D:\` (laadregel: `werkmap D:\`): D:, E: en F: wijzen alle drie naar de view. Relatieve paden
  blijven dezelfde bestanden. Waarschijnlijk geen oorzaak, maar wel een verschil.

Nu in de kandidaat: `D22/` krijgt alleen wat SYSTEM DATA1/2 leest (`game.exe`, `iob.dll`, `gunio.dll`, `pmds.dll`, `msvcr71.dll`,
`DATA/check.a`, `DATA/check.cc`, `DATA/testmode.a`) en **geen films**. Dat is voor de films dezelfde toestand als proef 11, plus een
geslaagde DATA1. **Voor de run eerst `rm -r ~/.local/share/ravengun/runtime/views/LethalEnforcers3/D22`** (alleen links van proef
13). Anders maakt het materialiseren van de oude link `D22/DATA` een map met alle inhoud van DATA, ook `movies`.

Meetvoorstel voor de films (een aparte run, met `D22/DATA -> origineel:DATA` zoals in proef 13):
`WINEDEBUG=+timestamp,+seh,+quartz,+strmbase,+mfplat,err+all` en `GST_DEBUG=2`. Te zoeken: blijft het hangen in
`IGraphBuilder_RenderFile`/`IMediaControl_Run` of een `WaitForCompletion` (quartz), mist er een GStreamer-element (MPEG-1-demuxer
`mpegpsdemux`, `mpeg2dec`), of loopt de film en blijft alleen het beeld zwart (dan de renderer in gamescope). Als vergelijking dezelfde
run zonder gamescope (`[Display] Gamescope = false`): onder de kern speelde de film zonder gamescope.

## Proef 14 (tv 2026-10-18 07:37, 4b63ae7, D22 zonder films)

70 s een geluidsstroom van `game.exe`, het beeld zwart, ook na munt, START en schot. Uit de logboeken (alleen gelezen):

- `rg-konami.log` van deze run (laatst geschreven 07:37:26,56, een halve seconde na de start) bevat alleen de twee laadregels. Er is
  geen `iob_get_digital_in`, geen `iob_init` en geen richtpunt. De game kwam in 70 s **niet in zijn hoofdlus** (die roept elk beeld
  `iob_get_digital_in` aan). Munt, START en schot konden dus niets doen.
- Er was nog een run met precies dezelfde kandidaat (`direct-LethalEnforcers3-20261018-000505`: D:/E:/F:, acht links, gamescope).
  Die kwam **wel** in de hoofdlus: `iob_init`, `iob_boot_wait: 1`, `gunio_boot_status(0): 1`, het eerste richtpunt per geweer. Om
  00:05:10 (4 s na de start) schreef hij `bookkeeping`, `bookkeeping_playdata` en `ranking_data` (in de schrijflaag) en 60 nieuwe
  bestanden `SAVE/_CPUP{0,1}ST{0..5}NO{000..004}.sav` (elk 129.688 bytes, kop `CDSV`, CRC's kloppen; `CWtCpuDataManager`). Daarna
  stopte hij zelf, normaal (`DllMain: proces stopt normaal (ExitProcess)`, 00:05:17). Onder de kern stonden die `_CPUP`-bestanden
  nooit in SAVE.
- Dus: eerste start met een lege SAVE → data aanmaken en afsluiten. Op de kast start `boot.exe` daarna `D:\D22\game.exe` opnieuw
  (`boot.exe` heeft `CreateProcessA` en de string `D:\D22\game.exe`). Tweede start, mét de 60 `_CPUP`-bestanden → zwart met geluid,
  geen hoofdlus. Het verschil tussen de twee runs zit in SAVE, niet in de opstelling.
- Wat er sinds proef 11 nog meer veranderde, behalve D:/E:/F:: de game vindt via F:/E: nu ook zijn SAVE. In proef 11 las en schreef
  hij die niet; daar dus geen `_CPUP`-bestanden en geen `io.sav`.

**Meetvoorstellen** (geen code: zonder een meting is niet te zien waar de tweede start hangt):

1. **A/B op SAVE:** zet de 60 `_CPUP*.sav` in `views/LethalEnforcers3/SAVE/` opzij (het zijn lokale bestanden, niet op de NAS).
   Verwacht: zoals de run van 00:05 (hoofdlus, data aanmaken, na ~10 s afsluiten). Daarna nog een run zonder iets te verplaatsen.
   Komt de zwarte toestand dan terug, dan is het de tweede start met CPU-data.
2. **Waar hangt hij:** tijdens het zwarte beeld de draden van het proces dumpen. Met gdb: `sudo gdb -p <pid van game.exe> -batch
   -ex 'thread apply all bt'` (alleen lezen, mag volgens de eigenaar). Of met Wine zelf: `WINEPREFIX=<prefix>
   ~/.local/share/ravengun/wine/proton-11.0-2/bin/wine winedbg --command "info thread"` en per draad `bt`. Te zoeken: hangt de
   hoofddraad in d3d8/dxgi (`Present`, `Reset`, `CreateDevice`), in quartz of strmbase (een film), in `ReadFile` of `CreateFileA`
   (SAVE), of in een eigen wachtlus van `game.exe` (adres in `0x401000..0x626000`; dat adres doorgeven, dan lees ik het na).
3. **Kort spoor van dezelfde run:** `WINEDEBUG=+timestamp,+seh,warn+file,+quartz,+d3d8,err+all`. Kijk naar de laatste
   bestandsopen/fout voor het beeld zwart blijft, en of `d3d8` een apparaat maakt en `Present` aanroept (dan tekent hij wel, maar ziet
   gamescope het niet). Of het een venster- of schermkwestie is, valt af te lezen aan `Executable name: game.exe` in de gamescope-regels
   (die is er) en aan of `Present` loopt.

## Proef 15 (tv 2026-10-18 08:12 en 08:13, `_CPUP*.sav` opzij)

Uit `rg-konami.log` en de SAVE-map (alleen gelezen; het logboek had nog geen tijden, de blokken zijn met de bestandstijden en de
logboeken van rg-direct uit elkaar gehaald):

| Run | Hoofdlus? | SAVE | Einde |
|---|---|---|---|
| 23:48 (proef 13) | nee (alleen laadregels) | - | na 150 s gestopt |
| 00:05 | **ja** (`iob_init` … richtpunt) | maakt 60 `_CPUP*.sav` + bookkeeping om +4 s | stopt zelf, +11 s |
| 07:37 (proef 14, met `_CPUP`) | nee | - | na 70 s gestopt |
| 08:12 (zonder `_CPUP`, bureaublad op de tv) | nee | maakt niets | na 70 s gestopt |
| 08:13 (zonder `_CPUP`) | **ja** | maakt 60 `_CPUP*.sav` + bookkeeping om +4 s | stopt zelf, +12 s |

1. **Run 1 (08:12) kwam niet in de hoofdlus** en maakte niets aan. Run 2 (08:13) wel: hoofdlus, `_CPUP` en bookkeeping om +4 s,
   en daarna stopte hij zelf (`DllMain: proces stopt normaal`). De `_CPUP`-bestanden beslissen het dus niet: met én zonder die
   bestanden komt hij soms wel en soms niet in de hoofdlus. Er zijn **twee losse verschijnselen**: (a) soms geen hoofdlus (en dan zwart
   of het bureaublad), (b) als hij wel loopt, stopt hij na ~12 s zelf.
2. **Venster**: gamescope staat in plan en argv (`/usr/bin/gamescope -W 1920 -H 1080 -S fit -f --backend wayland -- ...`), en
   gamescope zag `explorer.exe` en `game.exe`. Het bureaublad op de tv betekent dat gamescope geen beeld van de game liet zien. Dat
   past bij (a): de game hangt voor zijn hoofdlus. De game maakt zelf een volledig scherm met d3d8 (`CreateWindowExA`, geen
   `DestroyWindow`). Waar hij hangt, zegt het nieuwe meetinstrument hieronder.
3. **Waarom hij stopt (b)**: `game.exe` stopt op drie manieren. WM_DESTROY (venster dicht) → `PostQuitMessage` + vlag `0x694100` →
   einde van WinMain (`0x40a3b0`, `0x404cb5`). `exit(1)` uit de foutroutine van libjpeg (`0x44f584`, een kapotte JPEG). `exit()` als
   een draad niet start (`0x5325a8`). Welke het was, is zonder stapel niet te zien.
4. **`boot.exe` starten kan niet zomaar.** Hij praat zelf met het I/O-bord (geen `iob.dll`: "io_ready", "io_check_error"),
   controleert een MAC-adres (`iphlpapi`, "mac addr error"), zoekt updates op `F:\UP`/`E:\UP` en start dan `D:\D22\game.exe` met
   `CreateProcessA`. Opnieuw starten na een eerste start zou een optie van rg-direct worden ("start de exe opnieuw als hij met code X
   stopt"). Dat bouw ik pas als de afsluitregel laat zien dat het een bedoelde herstart is.

**Gewijzigd: een meetinstrument in onze iob.dll** (alleen kernel32 en user32, geen gedrag van de game veranderd):

- elke regel in `rg-konami.log` krijgt de tijd sinds de start: `[+12.345 s]`;
- **waakhond**: komt `iob_get_digital_in` (de hoofdlus) niet binnen 15 s, dan elke 15 s (vier keer) `waakhond: hoofddraad, geen
  hoofdlus na 15 s: eip=<adres> terugkeer in de exe: <adres> <adres> ...`, plus per venster van de hoofddraad `waakhond: venster
  <hwnd> klasse '...' titel '...' zichtbaar <0/1> voorgrond <0/1> rect ... stijl ...`. De hoofddraad wordt daarvoor heel even
  stilgezet (SuspendThread/GetThreadContext);
- **afsluitregel**: bij het stoppen `DllMain: wie stopt het proces terugkeer in de exe: <adres> ...`. Staat daarin `44f59e`, dan was
  het libjpeg (`exit(1)`). Een adres rond `54a4e1`/`54a4e6` is het einde van WinMain (venster dicht).

Getest in een wegwerp-prefix met een eigen proef-exe (geen game): de waakhond meldde na 15,0 s het juiste terugkeeradres in de
wachtfunctie, en de afsluitregel na `exit(3)` het adres in de stopfunctie.

**Testrecept proef 16** (LE3; eerst `tools/rg-konami/build.sh --le3 patches/LethalEnforcers3/dropin`, de oude
`views/LethalEnforcers3/rg-konami.log` opzij zetten zodat het logboek alleen deze runs bevat; `_CPUP*.sav` laten staan zoals ze zijn):

1. Twee of drie starts achter elkaar, elk tot hij stopt of 90 s.
2. Per run uit `rg-konami.log` doorgeven: de `waakhond:`-regels (eip, terugkeeradressen, vensters) of, als hij in de hoofdlus kwam,
   de `wie stopt het proces`-regel met zijn tijd.
3. Ik lees die adressen in `game.exe` na en lever dan de wijziging: een omleiding van wat hem tegenhoudt (a), of een herstart in
   rg-direct als (b) een bedoelde herstart is.

## Proef 16 (tv 2026-10-18 08:40, 08:41, 08:43, c27ee29)

Alle drie de starts kwamen in de hoofdlus (`iob_init` op +1,0 s) en stopten zelf op +9,4 tot +10,2 s, met dezelfde afsluitregel
(`54a9cc 5314fc 565b4d 5279f2 521eb7 54a95d 54a962 41b60c 54a962 54f405 54f40b 54b70c 54b84b 54b882 54a513 54a36a`). Geen
waakhond-regel. Nagelezen in `game.exe` (de buitenste ramen staan achteraan):

- `54a36a`: de CRT-opstart (`__tmainCRTStartup`, `0x54a33c`).
- **`54a513`**: na `call 0x54b875` op `0x54a50e`. Dat is het **`__except`-blok** van de CRT-opstart: `0x54a501` zet `esp` terug,
  `edi = uitzonderingscode`, en `_exit(edi)`. Het normale einde van WinMain zou `54a4e6` zijn (na `exit` op `0x54a4e1`), en dat
  adres staat er niet in.
- `54b882` → `54b84b` → `54b70c`: `_exit` → `doexit` → `__crtExitProcess` → `ExitProcess`. De lagere adressen (`54f405`, `41b60c`,
  `5279f2`, `565b4d`, `5314fc`, `54a9cc`) zijn opruimwerk tijdens `doexit` of oude waarden op de stapel.

**Het is dus een crash in de hoofddraad** (een niet-afgevangen uitzondering, exitcode = de uitzonderingscode). Geen venster dat dichtgaat,
geen JPEG-fout (`44f59e` staat er niet in), geen draad die niet start. Het gebeurt ~10 s na de start: na SYSTEM DATA1 en het
aanmaken van de SAVE-gegevens, waar de game naar de attract met de openingsfilm gaat. Sinds proef 14 staan de films niet meer in
`D22/DATA` (`D:\D22\DATA\movies\*.m1v`, `0x523ea2`). Onder de kern waren ze er altijd. Zonder D: (proef 11) kwam de game
nooit voorbij SYSTEM DATA1, dus hij kwam ook nooit bij de films. **Herziening van proef 13**: het zwart daar was de wisselende hang
voor de hoofdlus (zie proef 14/15: 07:37 en 08:12, ook zonder films), niet de films.

**Gewijzigd:**

1. **De films en `data.a` terug** in de kandidaat: `D22/DATA/movies -> origineel:DATA/movies` en `D22/DATA/data.a ->
   origineel:DATA/data.a`, als links in de bestaande lokale map `D22/DATA`. Geen `rm` nodig: de drie links van proef 14 blijven.
2. **Uitzonderingen in het logboek** (onze iob.dll, een vectored handler; laat elke uitzondering door): `uitzondering: code <code> op
   <adres> (lezen|schrijven|uitvoeren <adres>) draad <id> (hoofddraad): eip=... terugkeer in de exe: ...`, de eerste 12. Een
   C++-throw, een draadnaam en debugtekst worden overgeslagen. Getest met een eigen proef-exe (schrijven naar 0x10): code `c0000005`,
   het juiste adres en het terugkeeradres in `main`.

**Testrecept proef 17** (LE3): `tools/rg-konami/build.sh --le3 patches/LethalEnforcers3/dropin`, de nieuwe `game.ini` kopiëren, de oude
`rg-konami.log` opzij. Twee starts.

- rg-direct-log: tien `viewlink:`-regels, waaronder `D22/DATA/movies -> …/Lethal Enforcers 3/DATA/movies`.
- Doorloop verwacht: SYSTEM DATA1/2 OK, de openingsfilm, de attract. Geen `uitzondering:`-regel, en na 30 s nog richtpunten.
- Crasht hij weer: de `uitzondering:`-regels (code, adres, terugkeeradressen) doorgeven. Dan lees ik de plek in `game.exe` na.
- Hangt hij voor de hoofdlus (geen `iob_init`): de `waakhond:`-regels doorgeven.

## Proef 17 (tv 2026-10-18 08:53, 08:55, 09:08, dd5bbcc, films terug)

Per run uit `rg-konami.log` (de tijden staan sinds c27ee29 in het logboek):

| Run | Hoofdlus? | Wat het logboek zegt |
|---|---|---|
| 08:53 | nee | `[+15.003 s] waakhond: hoofddraad … eip=7abe1d38 terugkeer in de exe: 401cbf 401e5b 40a5f3 5599a7 54a4d6 54a36a`, daarna niets meer |
| 08:55 | **ja**, 75 s | `iob_init` +1,0 s, `CHECKING GUN…OK`, richtpunten elke 10 s, `MUNT 1` +60,8 s, `START P1` +65,4 s, `TREKKER P1` +66,7 s … +74,6 s; **geen crash meer** (de films waren de oorzaak van de crash in proef 16) |
| 09:08 | nee | als 08:53 (`[+15.000 s]`, zelfde adressen) |

Nagelezen in `game.exe`: `54a36a`/`54a4d6` zijn de CRT-opstart en de terugkeer uit WinMain (`0x40a460`), `5599a7` hoort bij de
opstart, `40a5f3` is WinMain (`call 0x401e40`), `401e5b` is `0x401e40` (eerst `0x4012c0`, daarna `0x401c70`). **`0x401c70` is de
hoofdlus van de game**, een takenplanner op fibers: `PeekMessageA` (`0x401cb9`) → bericht verwerken, anders elke klare taak met
`SwitchToFiber` (`0x401d1e`) laten lopen. De I/O-update (en al het andere van de game) zit in die taken. **`401cbf` is de terugkeer
uit `PeekMessageA`**: de hoofddraad komt daar niet meer uit, met eip in een Wine-DLL. Dat de oude waakhond na de eerste regel stil
bleef, past daarbij. Hij bleef hangen in `GetWindowTextA`, dat een bericht naar de hoofddraad stuurt, en dat bericht werd nooit
verwerkt. De hoofddraad zit dus vast in de venster- of beeldlaag van Wine, niet in de game, niet in de I/O en niet in een bestand.

Samen met de run van 08:55 (game loopt, invoer komt aan, maar het beeld is zwart): **beide verschijnselen zitten in de weg van het
beeld** (een d3d8-spel met een volledig scherm van 640x480 in gamescope met Xwayland). De route met de kern liep zonder gamescope.

**Gewijzigd: de waakhond** (onze iob.dll) meldt nu bij een hang elke 15 s (vier keer) **alle draden** van het proces: eip als
`<module>+<offset>` en de terugkeeradressen op de stapel in elke module, ook Wine's eigen DLL's (`win32u.dll`, `user32.dll`,
`winex11.drv`, `d3d8.dll`, `quartz.dll` …), plus de vensters van de hoofddraad. Daarbij geen aanroep die een bericht stuurt (alleen
`GetClassNameA`, `GetWindowRect`, `IsWindowVisible`). Getest in een wegwerp-prefix: `draad 36 (hoofddraad) eip=ntdll.dll+d410
terugkeer: kernelbase.dll+78611 waaktest.exe+1580 …` (Sleep in de proefexe).

**Meetpunten proef 18** (LE3; `tools/rg-konami/build.sh --le3 patches/LethalEnforcers3/dropin`, `rg-konami.log` opzij):

1. **A/B zonder gamescope**: dezelfde kandidaat met `[Display] Gamescope = false` (zo liep de route met de kern), twee starts, met
   een schermfoto. Komt er beeld, dan is de oplossing de venstermodus en niet de I/O.
2. **Met gamescope** (zoals nu), twee starts. Bij een hang de `waakhond: draad …`-regels doorgeven. Daaruit volgt in welke Wine-module
   en welke draad hij wacht (bv. de hoofddraad in `win32u`/`winex11.drv`, en een andere draad in `d3d8`/`dxgi`/`winevulkan`). Bij
   zwart zonder hang: een schermfoto.
3. Alleen als 2 niet genoeg zegt: tijdens de hang `sudo gdb -p <pid van game.exe> -batch -ex 'thread apply all bt'` (alleen lezen).
   Dat geeft de Linux-kant (xcb, futex, vulkan) van dezelfde draden.

## Niet bewezen / gegokt

1. **LE3, de kalibratie in de praktijk**: de formule is uit de code gelezen en doorgerekend, niet op het scherm gezien. Klopt de hoek
   niet, dan is dat een aanpassing van `io.sav` (gegevens), geen code.
2. **LE3, Option en Reload**: Option = TeknoParrot Button2 is een gewone knop van het bord (gelezen); dat Button3 "Reload" bedoeld is
   als "buiten beeld" is een aanname. Zonder die knop werkt herladen ook door buiten het scherm te richten.
3. **LE3 `+0x05`** (munt in de I/O-test) en de tellers (`+0x0e..+0x13`): de game kijkt alleen of ze veranderen; of het echte bord ook
   bij het loslaten verandert is niet te zien. Wij veranderen alleen bij indrukken (dus nooit een dubbel schot).
4. **Schermmodus zonder kern**: `Windowed`, `HideCursor` en (Gashaaaan) `ScreenFix` waren functies van de kern. LE3 draaide met
   `Windowed = 0` (de game zelf schermvullend), dus waarschijnlijk geen verschil. Gashaaaan draaide met `Windowed = 1` in gamescope;
   met de kern gaf `Windowed = 0` in gamescope zwart beeld. Zonder kern kiest de game zelf. **Meten.**
5. **Japanse locale**: `UseLocaleEmulator = 1` van de kern heeft geen vervanger meer; niet gemeten of de games hem nodig hebben
   (verminkte tekst of een vroege stop: `[Env] LANG/LC_ALL = ja_JP.UTF-8` terugzetten en vergelijken).
6. **Gashaaaan, schijfletters**: de kern leidde `D:`, `E:` en `F:` om (zie "Proef 11"); de eigen `BALLS.INI` vervangt dat met
   relatieve paden. Niet gemeten.
7. **LE3, bestandscontrole** (`0x536a90`): eerder dacht ik dat die alleen in het testmenu liep; het is de opstartstap SYSTEM DATA1
   (zie "Proef 11"). Opgelost met de originelen in `D22/`, niet gemeten.

## Testrecept voor de tv

**Uitrollen** (in de hoofdcheckout, `patches/` = de patchlaag die de app gebruikt; eerst controleren dat `patches/<Game>/game.ini` nog
het bestand is van `docs/research/konami/patches/<Game>/game.ini.basis.sha256`):

    tools/rg-konami/build.sh --le3 patches/LethalEnforcers3/dropin --gsh patches/Gashaaaan/dropin   # DLL's + SAVE/io.sav
    cp docs/research/konami/patches/LethalEnforcers3/game.ini          patches/LethalEnforcers3/game.ini
    cp docs/research/konami/patches/LethalEnforcers3/dropin/dropin.ini patches/LethalEnforcers3/dropin/
    cp docs/research/konami/patches/Gashaaaan/game.ini                 patches/Gashaaaan/game.ini
    cp docs/research/konami/patches/Gashaaaan/dropin/dropin.ini        patches/Gashaaaan/dropin/
    cp docs/research/konami/patches/Gashaaaan/dropin/BALLS.INI         patches/Gashaaaan/dropin/
    tools/rg-konami/test.sh                                             # 1968 controles, exports "in orde"
    RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan LethalEnforcers3   # en Gashaaaan: "verdict":"ok", "tp_files":[], "loader":"", "core_dll":""

Een gebruikerslaag (`~/.local/share/ravengun/patches/<Game>/game.ini`) mag geen eigen `[Loader]` hebben. Voor Gashaaaan eerst controleren
dat er geen oude `ballshooting.exe` meer in de gedeelde prefix draait (`ps -eo pid,args | grep -i '[b]allshooting'`). Voor de eerste run
kan `WINEDEBUG=err+module` erbij (een DLL die niet laadt, staat dan in het logboek van rg-direct). Starten: dubbelklik in de app.

Sinds proef 11 vraagt de LE3-kandidaat ook de nieuwe rg-direct (ViewLinks `origineel:` en mappen): eerst `cmake --build build`, dan
`install-user.sh`, anders weigert het plan met "ViewLinks ... ('origineel:...')".

**Proef 12, waar op te letten.** LE3, logboek van rg-direct: `schijfletter D: -> .../views/LethalEnforcers3` (en E:, F:),
`viewlink: .../views/LethalEnforcers3/D22/iob.dll -> /home/deck/mnt/spidernas_emulation/Teknoparrot/Lethal Enforcers 3/iob.dll
(alleen in de view)` (acht regels sinds proef 13: de laatste drie voor `D22/DATA/check.a`, `check.cc` en `testmode.a`; geen `movies`). Op het scherm: `SYSTEM DATA1 CHECK OK` en daarna DATA2, dan de attract
met film. In `rg-konami.log` geen nieuwe regels nodig; wel moet het richtpunt nu bij de as passen (io.sav wordt gelezen: zonder F:/E:
las de game hem in proef 11 niet). Faalt DATA1 weer: `ls -la views/LethalEnforcers3/D22` en de foutcode noteren (2-1005-0001 = DATA1,
-0002 = DATA2). Gashaaaan, logboek van rg-direct: `drop-in: .../views/Gashaaaan/bin/BALLS.INI geschreven`; in `bin/rg-konami.log` na de
laadregels nu `iob_boot(...)`, `iob_get_status: 3`, `bs2_get_status: 2`; in de schrijflaag krijgt `bin/NVRAM/info.bin` een nieuwe
tijd. Stopt hij weer meteen: het meetvoorstel uit "Proef 11".

**Bewijs per run** (beide games):

- logboek van rg-direct: `[Loader] Core = none: Wine start de exe zelf, zonder lader en kern`, `drop-in: ... iob.dll geschreven`,
  `WINEDLLOVERRIDES += iob=n, gunio=n` (LE3) / `iob=n, ballio=n` (Gashaaaan);
- `/proc/<pid>/maps` van het spelproces: `views/LethalEnforcers3/iob.dll` en `gunio.dll` (resp. `views/Gashaaaan/bin/iob.dll` en
  `ballio.dll`) geladen, **geen** `TeknoParrot.dll`, `TeknoParrot64.dll`, `OpenParrot` en niets uit `TeknoParrotUi-2.0.0.130-linux-x64`.

**Lethal Enforcers 3** — `views/LethalEnforcers3/rg-konami.log`. De **laadregels** (bewijs dat Wine onze DLL's in het spelproces laadde)
komen altijd, ook als de game daarna niets doet:

    iob.dll(LE3): DllMain: geladen Z:\...\views\LethalEnforcers3\iob.dll in proces Z:\...\views\LethalEnforcers3\game.exe, werkmap Z:\...\views\LethalEnforcers3 (RavenGun rg-konami, zonder TeknoParrot)
    gunio.dll(LE3): DllMain: geladen Z:\...\views\LethalEnforcers3\gunio.dll in proces ...\game.exe, werkmap ... (RavenGun rg-konami, zonder TeknoParrot)

Dan, als de game zijn I/O-code haalt (ongeveer in deze volgorde; de volgorde van de eerste regels hangt af van welke functie de game het
eerst aanroept):

    iob.dll(LE3): geladen: Z:\...\views\LethalEnforcers3\iob.dll (RavenGun, zonder TeknoParrot); eerste aanroep: iob_get_digital_in   (of iob_init)
    iob.dll(LE3): bordbeeld TeknoParrot_JvsState geopend
    iob.dll(LE3): iob_init(model=..., 1, 2): bord van RavenGun
    iob.dll(LE3): iob_start
    iob.dll(LE3): iob_boot_wait: 1 (bord klaar)
    gunio.dll(LE3): geladen: ...gunio.dll ...; eerste aanroep: gunio_init
    gunio.dll(LE3): gunio_init(1, 1): geweerbord van RavenGun
    gunio.dll(LE3): gunio_boot_status(0): 1 (CHECKING GUN...OK)
    gunio.dll(LE3): bordbeeld TeknoParrot_JvsState geopend
    iob.dll(LE3): invoer: MUNT 1 ingedrukt / START P1 ingedrukt / TREKKER P1 ingedrukt
    gunio.dll(LE3): richtpunt geweer 1: as X,Y -> ruw 255-X,255-Y (scherm ~2.5X,1.875Y van 640x480)

Op het scherm: geen "I/O BOARD ERROR" of "GUN BOARD ERROR"; `CHECKING GUN...OK`; munt geeft krediet, START begint, het vizier/de
inslagen volgen de Move in alle vier de hoeken (meting 1: mik op de vier hoeken en het midden, foto + de regel `richtpunt` erbij);
buiten beeld richten + trekker herlaadt. Vijf minuten spelen (`Spel gehaald = 5 minuten`). Valt de kalibratie scheef of gespiegeld: de
`richtpunt`-regel en de plaats op het scherm noteren; dat wordt een nieuwe `io.sav`, geen code.

**Gashaaaan** — `views/Gashaaaan/bin/rg-konami.log`. Laadregels:

    iob.dll(Gashaaaan): DllMain: geladen Z:\...\views\Gashaaaan\bin\iob.dll in proces Z:\...\views\Gashaaaan\bin\ballshooting.exe, werkmap Z:\...\views\Gashaaaan\bin (RavenGun rg-konami, zonder TeknoParrot)
    iob.dll(Gashaaaan): mutex JONG4APPLICATION bestaat nog niet (goed: system.dll maakt hem zelf)
    ballio.dll(Gashaaaan): DllMain: geladen ...\bin\ballio.dll in proces ...\ballshooting.exe, werkmap ...

Staat er `LET OP: de mutex JONG4APPLICATION bestaat al`, dan stopt de game om die reden (oude processen in de prefix stoppen). Komen
de laadregels en daarna meteen `DllMain: proces stopt normaal (ExitProcess)` zonder `iob_boot`, dan stopte `SystemMain` vroeg om een
andere reden (de AVS-opstart `0x10001f60`): dan `WINEDEBUG=+seh,err+module` meten. Komt er geen enkele regel, dan laadde onze DLL niet
(of een van de andere DLL's in `bin/` niet): `WINEDEBUG=err+module`. Daarna, als de game doorloopt:

    iob.dll(Gashaaaan): geladen: ...\views\Gashaaaan\bin\iob.dll ...; eerste aanroep: iob_boot   (of iob_get_status)
    iob.dll(Gashaaaan): iob_boot(...): bord van RavenGun
    ballio.dll(Gashaaaan): geladen: ...; eerste aanroep: ball_io_boot
    iob.dll(Gashaaaan): iob_get_status: 3 (bord draait)
    ballio.dll(Gashaaaan): bs2_get_status: 2 (sensor klaar)
    iob.dll(Gashaaaan): iob_check_plug: 1 (in orde)
    iob.dll(Gashaaaan): bordbeeld TeknoParrot_JvsState geopend
    iob.dll(Gashaaaan): invoer: munt 1 (teller 1) / START P1 ingedrukt
    ballio.dll(Gashaaaan): ball_io_mode_set(1)
    ballio.dll(Gashaaaan): schermmaat: util.dll sfgl_get_screen_width/height
    ballio.dll(Gashaaaan): worp: inslag X,Y op een scherm van BxH (modus 1)

Op het scherm: de opstart komt voorbij de I/O- en sensorcontrole (geen I/O-fout, geen "ball out"-fout), munt (toets 5) + START (toets
1) beginnen, een worp (trekker) laat een bal inslaan waar de Move mikt, in het kwart van de juiste speler (meting 2: gooi in elk kwart,
noteer `worp: inslag`-regel en de plek op het beeld). Meting 3: is het beeld schermvullend en zonder taakbalk? Zo niet: eerst
`[Display] Gamescope` aan/uit proberen en het resultaat noteren (de kern deed hier `Windowed`/`ScreenFix`).

**Terug naar de kern van TeknoParrot** (in ravengun-patches): `git checkout -- LethalEnforcers3/game.ini Gashaaaan/game.ini` en
`rm -r LethalEnforcers3/dropin Gashaaaan/dropin`. De view houdt onze DLL's als echte bestanden (de drop-in verving de symlinks): zet ze
terug met `ln -sfn "$HOME/mnt/spidernas_emulation/Teknoparrot/Lethal Enforcers 3/iob.dll" ~/.local/share/ravengun/runtime/views/LethalEnforcers3/iob.dll`
(idem `gunio.dll`, en `.../views/Gashaaaan/bin/{iob,ballio}.dll` naar `Gashaaaan!!!!/bin/`), en voor LE3 `views/LethalEnforcers3/SAVE`:
eerst kijken of er nieuwe lokale bestanden in staan (`find .../SAVE -type f`), dan de map weg en weer als symlink naar `.../Lethal
Enforcers 3/SAVE`. Of de view opnieuw maken met `scripts/tp-add-game.py --view`.
