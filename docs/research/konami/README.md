# Lethal Enforcers 3 en Gashaaaan zonder TeknoParrot

*Stand 2026-10-17. Statische analyse (objdump, eigen PE-lezer, geen game gestart). Vervolg op de rijen LethalEnforcers3 en Gashaaaan in
`docs/research/wartran/README.md` ("geblokkeerd: structuren niet gelezen"): de structuren zijn nu gelezen, de vervangers gebouwd.*

| Game | Stand | Wat er nog open is |
|---|---|---|
| **LethalEnforcers3** | **kandidaat, klaar om op de tv te testen** | richtpunt en knoppen zijn uit de game gelezen; niet gemeten: of de kalibratie in de hoeken klopt, de betekenis van Option/Reload in het spel, of de game zonder de kern dezelfde schermmodus krijgt |
| **Gashaaaan** | **kandidaat, klaar om op de tv te testen** | knoppen en inslagen zijn uit de game gelezen; niet gemeten: venster/schermvullend zonder de kern (Windowed/ScreenFix waren kernfuncties), of de inslagcoordinaten op het beeld vallen |

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

Verder deed de kern (profiel/`teknoparrot.ini`): `UseLocaleEmulator` (nu `[Env] LANG/LC_ALL = ja_JP.UTF-8`; `ja_JP.utf8` staat op de
machine), `Windowed`/`HideCursor`, en bij Gashaaaan `ScreenFix`. Die laatste drie hebben zonder kern geen vervanger (zie "niet bewezen").

## Wat we vervangen: `tools/rg-konami/`

Vier DLL's, 32-bit PE, zonder C-runtime (alleen `kernel32` en `user32`, zoals `tools/rg-pqmt`), gebouwd met MinGW in de podman-container
(`tools/rg-konami/build.sh`). De exports komen uit `.def`-bestanden die uit de exporttabel van het origineel gemaakt zijn: **dezelfde
C++-namen en dezelfde ordinalen**. `tools/rg-konami/test.sh` controleert dat tegen het origineel op de NAS (alleen lezen) en controleert
ook dat elke naam die een module van de game uit die DLL importeert bij ons bestaat (anders laadt Wine de game niet). Uitkomst nu:
44/18/30/63 exports, alles gelijk, alleen systeem-imports.

De logica staat los van Windows in `konami_logic.h` en heeft een eenheidstest (`test_konami.c`, 1968 controles). Elke DLL schrijft met
`RG_KONAMI_LOG=rg-konami.log` een logboek naast zichzelf in de view.

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
5. **Japanse locale**: overgenomen van `UseLocaleEmulator = 1`; niet gemeten of de games hem nodig hebben.
6. **Gashaaaan, schijfletters**: `bin/BALLS.INI` noemt `D:\TYPE.BIN`, `E:\` en `F:\`; de prefix heeft alleen `c:` en `z:`, ook op de
   huidige route (kern), en die werkt. `system.dll` heeft standaardpaden (`.\CONF\NVRAM`). Geen verandering verwacht.
7. **LE3, bestandscontrole in het testmenu** (`0x536a90`: `DATA\check\iob.cc` tegen `D:\D22\iob.dll`): alleen in het testmenu, met
   absolute `D:\D22`-paden die hier niet bestaan; faalde dus al. Onze iob.dll verandert daar niets aan.

## Testrecept voor de tv

**Uitrollen** (in de hoofdcheckout, `patches/` = de patchlaag die de app gebruikt; eerst controleren dat `patches/<Game>/game.ini` nog
het bestand is van `docs/research/konami/patches/<Game>/game.ini.basis.sha256`):

    tools/rg-konami/build.sh --le3 patches/LethalEnforcers3/dropin --gsh patches/Gashaaaan/dropin   # DLL's + SAVE/io.sav
    cp docs/research/konami/patches/LethalEnforcers3/game.ini          patches/LethalEnforcers3/game.ini
    cp docs/research/konami/patches/LethalEnforcers3/dropin/dropin.ini patches/LethalEnforcers3/dropin/
    cp docs/research/konami/patches/Gashaaaan/game.ini                 patches/Gashaaaan/game.ini
    cp docs/research/konami/patches/Gashaaaan/dropin/dropin.ini        patches/Gashaaaan/dropin/
    tools/rg-konami/test.sh                                             # 1968 controles, exports "in orde"
    RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan LethalEnforcers3   # en Gashaaaan: "verdict":"ok", "tp_files":[], "loader":"", "core_dll":""

Een gebruikerslaag (`~/.local/share/ravengun/patches/<Game>/game.ini`) mag geen eigen `[Loader]` hebben. Starten: dubbelklik in de app.

**Bewijs per run** (beide games):

- logboek van rg-direct: `[Loader] Core = none: Wine start de exe zelf, zonder lader en kern`, `drop-in: ... iob.dll geschreven`,
  `WINEDLLOVERRIDES += iob=n, gunio=n` (LE3) / `iob=n, ballio=n` (Gashaaaan);
- `/proc/<pid>/maps` van het spelproces: `views/LethalEnforcers3/iob.dll` en `gunio.dll` (resp. `views/Gashaaaan/bin/iob.dll` en
  `ballio.dll`) geladen, **geen** `TeknoParrot.dll`, `TeknoParrot64.dll`, `OpenParrot` en niets uit `TeknoParrotUi-2.0.0.130-linux-x64`.

**Lethal Enforcers 3** — `views/LethalEnforcers3/rg-konami.log`, ongeveer in deze volgorde (de volgorde van de eerste regels hangt af
van welke functie de game het eerst aanroept; de inhoud niet):

    iob.dll(LE3): geladen: Z:\...\views\LethalEnforcers3\iob.dll (RavenGun, zonder TeknoParrot); eerste aanroep: iob_get_digital_in   (of iob_init)
    iob.dll(LE3): bordbeeld TeknoParrot_JvsState geopend
    iob.dll(LE3): iob_init(model=..., 1, 2): bord van RavenGun
    iob.dll(LE3): iob_start
    iob.dll(LE3): iob_boot_wait: 1 (bord klaar)
    gunio.dll(LE3): geladen: ...gunio.dll ...; eerste aanroep: gunio_init
    gunio.dll(LE3): gunio_init(1, 1): geweerbord van RavenGun
    gunio.dll(LE3): gunio_boot_status(0): 1 (CHECKING GUN...OK)
    gunio.dll(LE3): gunio_run
    gunio.dll(LE3): bordbeeld TeknoParrot_JvsState geopend
    iob.dll(LE3): invoer: MUNT 1 ingedrukt / START P1 ingedrukt / TREKKER P1 ingedrukt
    gunio.dll(LE3): richtpunt geweer 1: as X,Y -> ruw 255-X,255-Y (scherm ~2.5X,1.875Y van 640x480)

Op het scherm: geen "I/O BOARD ERROR" of "GUN BOARD ERROR"; `CHECKING GUN...OK`; munt geeft krediet, START begint, het vizier/de
inslagen volgen de Move in alle vier de hoeken (meting 1: mik op de vier hoeken en het midden, foto + de regel `richtpunt` erbij);
buiten beeld richten + trekker herlaadt. Vijf minuten spelen (`Spel gehaald = 5 minuten`). Valt de kalibratie scheef of gespiegeld: de
`richtpunt`-regel en de plaats op het scherm noteren; dat wordt een nieuwe `io.sav`, geen code.

**Gashaaaan** — `views/Gashaaaan/bin/rg-konami.log` (zelfde kanttekening over de volgorde):

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
