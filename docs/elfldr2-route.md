# De ElfLdr2-route (BudgieLoader): wat er werkelijk misgaat

*RavenGun, 2026-10-11. Gemeten op de Deck met `scripts/launch-direct.py` en
`/home/deck/starttest-batch.sh`. Dit document gaat over de **zeven** games met
`Type = ElfLdr2` in `patches/<Game>/game.ini`: GalagaAssault, GhostBusters, GoGoStrike, Halo,
JurassicPark, PacmanChomp, Terminator. Zie `docs/dropins.md` voor het drop-in-contract,
`docs/rt-glfix.md` voor de 32-bits grafische brug en `patches/Halo/README.md` voor de volledige
meting van één game.*

## 1. Samenvatting

Er zijn hier **drie** dingen aan de hand, en ze zijn niet hetzelfde:

1. **De starttest kan geen enkele ElfLdr2-game als gestart herkennen.** De twee succesregels
   (`Have fun`, `Loading game...`) bestaan in deze route niet. Vijf van de zeven games lezen daarom
   `start-niet` terwijl hun lader op het einde van het venster nog leefde. Dat is een fout in de
   maatstaf, niet in de games.
2. **`Preload = true` op een inline-hook is fataal.** De launcher zet zo'n bibliotheek in
   `LD_AUDIT` van de Wine-start, en de ElfLdr2-lader is een Cygwin-programma dat de game-ELF in zijn
   eigen proces laadt. Voor `librt-glfix` is dat het werkende kanaal; voor `libravengun-hasphook`
   betekent het de dood van de lader (Halo: `exitcode 1`; JurassicPark: `exitcode -11`, SIGSEGV).
   Dit is op te lossen met de nieuwe sleutel `Audit = false` (§3).
3. **`Warning SHT_DYNSYM not found in section headers` is een rode haring.** De games missen hun
   sectiekoppen, de lader waarschuwt, en gaat door. De symbolen staan in `DT_HASH`/`DT_SYMTAB`.

## 2. De waarschuwing, en waarom hij niet de oorzaak is

De Raw Thrills-dumps hebben **nul sectiekoppen**:

```
$ readelf -h .../halo-g7-tp/halo/game
  Start of section headers:          0 (bytes into file)
  Number of section headers:         0
$ readelf -S .../halo-g7-tp/halo/game
There are no sections in this file.
```

De symboolontleder van ElfLdr2 zoekt `SHT_DYNSYM` in de sectietabel, vindt hem niet, waarschuwt
twee keer en gaat verder. Bewijs dat de waarschuwing onschadelijk is:

* **PacmanChomp geeft dezelfde twee regels en loopt door** tot
  `Starting Game` / `[SDL]: SDL initialized.` / `[SDL]: SetVideoMode initialized: width=1920
  height=1080` (`/tmp/rg-starttest-PacmanChomp.log`);
* **GalagaAssault** geeft dezelfde waarschuwing en loopt door tot `Detected Linux game: Galaga
  (1.38 US)`, `Graphics API: SDL`, `Starting Game`;
* **in de periode dat Halo volledig speelbaar was** stonden dezelfde twee regels in
  `teknoparrot_log.txt` (10 oktober 18:07), gevolgd door de eigen regels van de game
  (`cmgr_init`, `cmgr_thread_main: thread begun`, `mo_mode_loop_thread: begun`);
* de dynamische symbolen zijn er wel, langs het dynamische segment:
  `readelf -d` geeft `DT_HASH 0x1f48d28`, `DT_SYMTAB 0x401990`, `DT_STRTAB 0x405968`, en onze eigen
  hook leest daar 220 symbolen uit (`[ravengun-hasphook] aantal symbolen uit DT_HASH: 220`).

Er hoeft dus **geen patch op de ELF**: de tabel die de lader zoekt is optioneel, en de informatie
die hij nodig heeft staat in het programma-segment.

## 3. `Preload = true` → `LD_AUDIT` → de Cygwin-lader sterft

`dropin_plan()` in `scripts/launch-direct.py` zet elk bestand met `Preload = true` voor een
Linux-ELF onder Wine in `LD_AUDIT` van de Wine-start. Dat is één proces: de ElfLdr2-lader laadt de
game-ELF **in zijn eigen adresruimte** (dat is de hele werking van ElfLdr2), dus een
auditbibliotheek komt in het proces van de Cygwin-lader. Voor `librt-glfix` gaat dat goed; voor
`libravengun-hasphook` niet.

Meting (Halo, `probe2.sh`; elke proef start de lader met precies de omgeving van de launcher, met
één variabele erbij):

| proef | omgeving | exitcode | game loopt? |
|---|---|---|---|
| P0 | niets extra | 124 (nog levend na 20 s) | ja |
| P1 | `LD_AUDIT` = emulator + hook | **1** | **nee** |
| P2 | alleen `RG_DROPIN_PRELOAD` | 124 | ja |
| P3 | alleen `RAVENGUN_HASP_*` | 124 | ja |
| P4 | alleen `TP_LAUNCH_SESSION_ID` | 124 | ja |
| P5 | alles | **1** | **nee** |
| P6 | `LD_AUDIT` = alleen de hook | **1** | **nee** |
| P7 | `LD_AUDIT` = alleen de emulator | 124 | ja |

Het laatste wat de hook schrijft is `[ravengun-hasphook] spelbeeld op 0x00400000`; daarna is de
lader weg en komt de game nooit aan zijn eigen code toe.

Voor JurassicPark is hetzelfde patroon eerder al gezien (`hf-proef/met.txt`):

```
ERROR: ld.so: object '.../views/JurassicPark/libravengun-hasphook.so' cannot be loaded as audit
       interface: wrong ELF class: ELFCLASS32; ignored.
timeout: the monitored command dumped core
```

en de starttest van 11 oktober geeft voor JurassicPark `exitcode -11` (SIGSEGV).

### De reparatie: `Audit = false`

`scripts/launch-direct.py` kent nu een derde sleutel per drop-in-sectie:

| Sleutel | Betekenis |
|---|---|
| `Preload` | dit bestand moet in het proces van de game geladen worden (het pad komt in `RG_DROPIN_PRELOAD`) |
| `Audit` | **mag dit bestand in `LD_AUDIT` van de Wine-start?** standaard `true`; `false` houdt het eruit |

Met `Audit = false` blijft het pad in `RG_DROPIN_PRELOAD` staan — dat is het kanaal voor de native
route (`scripts/play-native.sh`) en voor een eigen lader — maar het bestand wordt niet meer in het
proces van de Cygwin-lader geladen. `--plan` laat de keuze zien:

```
'audit': [], 'geen_audit': ['.../ravengun-hasp-x64.so', '.../libravengun-hasphook-x64.so']
niet in LD_AUDIT (Audit = false): ... -- deze bibliotheek hoort in het spelproces en niet in het
Cygwin-proces van de lader
```

De standaard is bewust `true`: niets verandert vanzelf, en de drop-in van een game die het kanaal
wél nodig heeft (PacmanChomp, GalagaAssault en JurassicPark gebruiken het voor `librt-glfix`)
blijft werken. **Voor de HASP-hooks van JurassicPark, GalagaAssault en Terminator moet de eigenaar
van die map nog `Audit = false` zetten**; dat is één regel per sectie.

## 4. Waarom de starttest geen ElfLdr2-game `START` kan noemen

`/home/deck/starttest-batch.sh` (regel 59-68) noemt een game pas gestart bij:

```bash
grep -q 'Have fun' "$LOG" && GESTART=1
grep -q 'Loading game\.\.\.' "$LOG" && GESTART=1
```

Die twee regels komen uit de OpenParrot/TeknoParrot-kernen, niet uit ElfLdr2. Gemeten:

| waar | `Have fun` | `Loading game` |
|---|---|---|
| `game` van Halo (33 MB) | 0 | 0 |
| `ElfLdr2/x64/BudgieLoader_x64.exe` | 0 | 0 |
| `ElfLdr2/BudgieLoader.exe` | 0 | 0 |
| `TeknoParrot/BudgieLoader.exe` | 0 | 0 |
| `N2/BudgieLoader.exe` | 0 | 0 |
| `ElfLdr2/TeknoParrot.dll` | 0 | 0 |
| de logboeken van alle zeven ElfLdr2-games | 0 | 0 |

Gevolg: elke ElfLdr2-game leest `start-niet`, ook als de lader nog leeft. De kolom "seconden" in de
starttest is `levend` (de launcher leefde nog op het einde), en de laatste regel `klaar (exitcode
gestopt)` betekent hetzelfde: de lader liep nog toen de proef hem afbrak. In de ronde van de
coördinator (35 games, 11 oktober) hadden GalagaAssault, GhostBusters, PacmanChomp en Terminator
precies die laatste regel.

Wat de route wél als teken van leven geeft, per game gemeten:

| game | teken dat de game zelf loopt |
|---|---|
| PacmanChomp | `Detected Linux game: Pac-Man Chomp`, `Graphics API: SDL`, `Starting Game`, `[SDL]: SDL initialized.`, `[SDL]: SetVideoMode initialized: width=1920 height=1080 bpp=24` |
| GalagaAssault | `Detected Linux game: Galaga (1.38 US)`, `Graphics API: SDL`, `Starting Game` |
| Terminator | `Detected Linux game: Terminator (v1.25 US)`, `Starting Game` |
| Halo | `Detected Linux game:` (leeg), `Graphics API: Unknown`, daarna eigen regels van de game (`cmgr_init`, `cmgr_thread_main: thread begun`, `mo_mode_loop_thread: begun`, `Cpmgui start`, `Gun 0 Cal`) en een venster `TeknoParrot (ElfLoader64) - HALO Fireteam Raven` van 1920×1080 |
| GhostBusters | `Detected Linux game: Ghostbusters (1.17 US)`, `Starting Game` (en de padcontrole eist de map `pm`; zie `patches/GhostBusters/README.md`) |

## 5. Wat de metingen van 11 oktober per game laten zien

`bash /home/deck/starttest-batch.sh 30 Halo GoGoStrike PacmanChomp`:

| game | resultaat | launcher nog levend | laatste regel | wat er werkelijk gebeurde |
|---|---|---|---|---|
| Halo | start-niet | 1 (ja) | `klaar (exitcode gestopt)` | game liep: eigen regels + venster 1920×1080 |
| GoGoStrike | start-niet | 0 (nee) | `klaar (exitcode 1)` | **gamescope**: `Failed to create backend.` — de game is nooit aan de beurt gekomen |
| PacmanChomp | start-niet | 0 (nee) | `klaar (exitcode 0)` | liep tot `SetVideoMode initialized`, daarna een **Cygwin-uitzondering**: `cygwin_exception::open_stackdumpfile: Dumping stack trace to BudgieLoader.exe.stackdump` |

Dus: Halo start en blijft draaien, GoGoStrike sneuvelt op gamescope (geen backend — dat is
displaybezetting, niet de game) en PacmanChomp komt tot het zetten van de videomodus en valt dan om
in de Cygwin-lader. Die laatste twee zijn echte, aparte storingen; ze delen geen oorzaak met Halo.

## 6. Wat er nog moet

1. **Beeld.** Voor Halo (en volgens `docs/rt-glfix.md` ook voor de 32-bits games) loopt de game maar
   is er geen getekend beeld gemeten: `Graphics API: Unknown`, en een schermopname van de lopende
   start is volledig zwart. ElfLdr2 herkent Halo niet als spel (`Detected Linux game:` is leeg) en
   installeert dan zijn grafische brug niet. Dat is de eerstvolgende te meten stap.
2. **`Audit = false` bij de andere drie HASP-hooks** (JurassicPark, GalagaAssault, Terminator). Dat
   is één regel per sectie, en het haalt de SIGSEGV-route eruit. De mappen zijn van een andere
   agent, dus hier alleen gemeld.
3. **De starttest een ElfLdr2-maatstaf geven.** Zonder dat blijft de hele familie `start-niet`
   lezen, ook als er niets mis is.
4. **De JVS-pijp en de opruiming.** `\\.\pipe\TeknoParrot_JVS` is procesbreed
   (`CreateNamedPipe failed: 231` bij een tweede start), en `pkill -f "$BASIS"` in
   `starttest-batch.sh` doodt elke game met dezelfde bestandsnaam — zes games in deze verzameling
   hebben `game`. Gelijktijdige proeven meten dus elkaar.
