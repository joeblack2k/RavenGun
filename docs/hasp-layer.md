# De HASP-donglelaag — de emulator, het formaat en de injectie

*RavenGun, ronde 6 (2026-10-11). Dit is de documentatie van `hasp/` (de emulator), `scripts/hasp.py` (de
controle), `scripts/build-hasp.sh` (de bouw) en het stuk van `scripts/launch-direct.py` dat de dongle en de
drop-ins in het spel zet.*

De HASP is één van de donglesoorten uit `docs/dropins.md`. De **gegevens** van een game staan in
`patches/<Game>/dongle.json` + `patches/<Game>/dongle/`; de **emulator** is niet game-specifiek en staat in
`hasp/src/` (bron) en in `~/.local/share/ravengun/hasp/` (gebouwd). De launcher zet hem met een **drop-in** in
de view van de game.

---

## 0. Stand van zaken — lees dit eerst

**De Windows-kant werkt.** Voor Time Crisis 5 is de PE-DLL een volledige vervanging van de runtime van de
fabrikant, en dat is end-to-end gemeten onder Wine met `tests/hasp_win_probe.c` (dat dezelfde vier ordinalen
aanroept als het spel):

```
[ok ] ordinaal 2 (hasp_decrypt) / 13 (hasp_login) / 14 (hasp_logout) / 15 (hasp_read)
[ok ] alle 25 ordinalen bestaan
[ravengun-hasp] dongle 'Halo Fireteam' geladen uit Z:/home/deck/projects/ravengun/patches/Halo
[ok ] hasp_login geeft HASP_STATUS_OK (0)      greep: 0xe6f
[ok ] hasp_read(fileid 65524, 112 bytes) geeft HASP_STATUS_OK
     eerste 8 bytes: 2c 03 ab 63 2a 91 92 3f
```

**De Linux/ELF-kant is NIET af.** De emulator komt wel in het ELF-proces van de game (gemeten: de constructor
draait en `LD_TRACE_LOADED_OBJECTS` toont de bibliotheek), maar **hij wordt niet aangesproken**. De
HASP-aanroepen van deze games zijn **statisch** in de game gelinkt:

| Gemeten op `game` van | ongedefinieerde hasp-symbolen | PLT-verplaatsingen voor hasp |
|---|---|---|
| Halo Fireteam (64-bits) | 0 | 0 |
| Terminator Salvation (32-bits) | 0 | 0 |
| Jurassic Park (32-bits) | 0 | 0 |
| Galaga Assault (32-bits) | 0 | 0 |

`LD_PRELOAD` zet de emulator dus in het proces en in de globale naamruimte, maar de aanroepen gaan er niet
langs. Wat daarvoor nodig is, staat in §4 en §9: de PLT/GOT-koppeling (de techniek die `halo.so` van
TeknoParrot gebruikt). Tot die er is, is de Linux-kant van deze laag **geladen maar werkloos**.

---

## 1. De API — tien functies die het spel aanroept

De HASP-HL-runtime van de fabrikant (Sentinel LDK) levert 25 namen. De games gebruiken daar een deel van.
Onze emulator (`hasp/src/dongle.c`) levert **alle 25**, zodat hij een volledige vervanging is: een ontbrekende
naam laat het laden van een spel mislukken.

De signaturen zijn **nagemeten op twee onafhankelijke exemplaren** en niet gegokt:

* de Linux-koppeling van TeknoParrot (`Halo Fireteam/g7/halo/halo.so`, x86-64 SysV), en
* de Windows-runtime van Time Crisis 5 (`hasp_windows_x64_105293.dll`, Win64).

| Functie | Signatuur (C) | Bewijs |
|---|---|---|
| `hasp_login` | `(unsigned feature, const char *scope, void **handle)` | `halo.so` 0x5341: `edi`, `rsi`, `rdx`; schrijft `0xE6F` |
| `hasp_login_scope` | `(unsigned feature, const char *scope, const char *format, void **handle)` | `halo.so` 0x5316: vier argumenten, greep in `rcx` |
| `hasp_logout` | `(void *handle)` | `halo.so` 0x5368 |
| `hasp_read` | `(void *handle, unsigned fileid, unsigned offset, unsigned length, void *buffer)` | `halo.so` 0x5376: `edi`,`esi`,`edx`,`ecx`,`r8` |
| `hasp_write` | `(void *handle, unsigned fileid, unsigned offset, unsigned length, const void *buffer)` | `halo.so` 0x5488: idem |
| `hasp_encrypt` | `(void *handle, void *buffer, unsigned length)` | `halo.so` 0x5289 en de Windows-DLL 0x17250: **in de buffer zelf** |
| `hasp_decrypt` | `(void *handle, void *buffer, unsigned length)` | Windows-DLL 0x17330: idem |
| `hasp_get_size` | `(void *handle, unsigned fileid, unsigned *size)` | `halo.so` 0x52f7; Windows-DLL 0x18060 |
| `hasp_get_info` | `(void *handle, const char *format, const char *scope, char **info)` | `halo.so` 0x563d: uitvoer in het **vierde** argument |
| `hasp_get_sessioninfo` | `(void *handle, const char *format, char **info)` | `halo.so` 0x55d3: uitvoer in het derde |
| `hasp_free` | `(void *p)` | `halo.so` 0x55b8: `free()` |
| 14 overige namen | zie `hasp/src/dongle.h` | ze doen niets en melden succes; geen enkele gemeten game roept ze aan |

**Let op `hasp_read`/`hasp_write`: vijf argumenten, niet vier.** De oude versie van `hasp/dongle.h` had er vier
(`handle, fileid, buf, len`), afgeleid uit een samenvatting van de disassemblage. Wie dat aanhoudt, leest bij
Time Crisis 5 de *offset* als bufferpointer. De offset hoort erbij: `hasp_read(h, 65524, 0, 112, buf)`.

Twee keuzes die van de bestaande ondersteuning afwijken, met reden:

* **`hasp_get_size` geeft de werkelijke grootte** uit `dongle.json` in plaats van altijd 0. Een echte dongle
  geeft de grootte van dat fileid; een spel dat op "grootte != 0" controleert, herkent een laag die 0 meldt
  niet. Een onbekende fileid blijft 0.
* **`hasp_encrypt` en `hasp_decrypt` laten de buffer ongemoeid.** De image wordt niet ontsleuteld maar
  één-op-één doorgegeven, net als bij de bestaande ondersteuning.

### De greep

`hasp_login` schrijft `0xE6F` in de handle-pointer — dezelfde waarde als `halo.so`. Op 64-bits Windows is de
greep 8 bytes breed, dus er wordt een **pointerwaardige** waarde geschreven en niet 4 bytes: anders blijft de
bovenste helft staan wat de aanroeper erin had gezet.

### De XML die `hasp_get_info` teruggeeft

* `hasp_get_info` → de korte vorm: `<hasp id="<haspid>" type="HASP-HL">` met de kenmerken eronder.
* `hasp_get_sessioninfo` → de uitgebreide descriptor: `<keyspec>` met de keycaps, het haspid, een
  `<memoryinfo>` per geheugenbestand en het poortadres.

Beide komen uit `dongle.json`; de bron van de vorm is `hasp/src/dongle.c` (`dongle_get_info_xml`).

### Sporen (bewijs dat de laag geladen is)

Staat `RAVENGUN_HASP_VERBOSE` in de omgeving (niet leeg, niet `0`), dan schrijft de emulator één regel per
aangeroepen functie naar stderr, met een plafond van 64 regels:

```
[ravengun-hasp] dongle 'Halo Fireteam' geladen uit /home/deck/projects/ravengun/patches/Halo (2 geheugenbestand(en), haspid 748675309)
[ravengun-hasp] hasp_login (vendor_code=0)
[ravengun-hasp] hasp_read (fileid=65524 offset=0 lengte=112)
```

Dat is het bewijs dat de emulator in het proces zit **en** dat het spel hem aanspreekt. Zonder die variabele
is de laag stil. `scripts/hasp.py` zet hem standaard aan bij het injecteren; uitzetten kan met
`RAVENGUN_HASP_VERBOSE=0` (via `[Env]` in `game.ini` of `--env`).

Two diagnostics of the Windows DLL, only when set (`[Env]` in the user layer's `game.ini`), each logged on every
`hasp_logout`: `RAVENGUN_HASP_PEEK=<rva>:<len>` (hex RVA in the main module, at most 64 bytes, e.g. a global where the game
keeps its dongle verdict) and `RAVENGUN_HASP_PEEK_MAP=<name>:<len>` (a named file mapping the game shares with another
process, at most 4096 bytes). Lost Land Adventure (2026-10-21) was measured this way.

The dongle dir (`RAVENGUN_HASP_DIR`/`RAVENGUN_HASP_GAME`) is made absolute when the DLL loads: the memory files are opened
at `hasp_read` time, and a game may change its working directory in between (Lost Land's UE3 game does).

---

## 2. Het dongle-formaat — `patches/<Game>/dongle.json`

De gegevens staan in de map van de game (docs/dropins.md), niet in een globale boom:

```
patches/<Game>/
  dongle.json            WELKE dongle deze game vraagt + de gegevens (kind = hasp)
  dongle/memory/<fileid> de geheugenimages; het pad in dongle.json is relatief aan patches/<Game>/
```

```json
{
  "kind": "hasp",
  "game": "Halo Fireteam",
  "haspid": 748675309,
  "features": [0, 60],
  "keycaps": ["hasphl", "newintf", "hasp4", "aes", "sentinelhl"],
  "memory": [
    {"name": "read/write", "fileid": 65524, "size": 112, "file": "dongle/memory/00000000000000001111111111110100"}
  ],
  "port": {"type": "USB", "address": 196608}
}
```

* **De bestandsnaam van een geheugenbestand is de 32-bits binaire fileid**, hoogste bit eerst:
  `fileid 65524` = `0x0000FFF4` → `00000000000000001111111111110100`. Dat is de regel die de runtime zelf
  gebruikt; `hasp/src/dongle.c` (`dongle_fileid_name`) schrijft hem voor. De **hexvorm** (`fff4`) wordt ook
  geaccepteerd, want dongles in omloop gebruiken die. Een hex-alias van dezelfde fileid is geen fout.
* `kind` is de machineleesbare betekenis ("deze game vraagt een dongle"). `type` is de oudere naam en blijft
  werken (`hasp-hl` = `hasp`).
* De gebruikerslaag `~/.local/share/ravengun/patches/<Game>/` gaat voor op de meegeleverde map.
* `scripts/hasp.py` weigert een pad dat absoluut is of buiten de map van de game wijst, en controleert de
  grootte tegen `size`.

`scripts/hasp.py` zoekt een game op **de mapnaam** (`patches/Halo`) of op **het veld `game`**
(`"Halo Fireteam"`), zodat beide namen werken.

---

## 3. De emulator en de bouw

```
hasp/src/
  dongle.h              de API en het formaat
  dongle.c              de JSON-lezer, de geheugenbestanden, de 25 hasp_-functies, dongle_install_from_env()
  hasp_preload.c        de Linux-constructor (__attribute__((constructor)))
  hasp_preload_win.c    de Windows DllMain
  hasp_windows.def      de uitvoertabel: 25 namen met de ordinalen van de fabrikant
  selftest.c            de zelftest tegen een echte dongle
  inc/gnu/stubs-32.h    lokaal gemaakt (de 32-bits dev-header ontbreekt in lib32-glibc op SteamOS)
```

`scripts/build-hasp.sh` bouwt en installeert:

```
~/.local/share/ravengun/hasp/
  ravengun-hasp.so            32-bits ELF, de 32-bits Linux-games
  ravengun-hasp-x64.so        64-bits ELF, de 64-bits Linux-games
  windows/hasp_windows_x64.dll           de PE-DLL voor de Windows-games
  windows/hasp_windows_x64_105293.dll    dezelfde inhoud, de naam die Time Crisis 5 importeert
  windows/hasp_windows_x64_106993.dll
  windows/hasp_windows_32.dll
  windows/hasp_probe.exe                 het meetprogramma (tests/hasp_win_probe.c)
```

* De bibliotheekmap is `RAVENGUN_HASP_LIB_DIR`, anders `RAVENGUN_USER_HASP_DIR`, anders
  `${XDG_DATA_HOME:-~/.local/share}/ravengun/hasp`. **`RAVENGUN_HASP_DIR` is iets anders:** dat is voor de
  emulator de wortel van de donglegegevens (standaard `/hasp`). Dezelfde variabele werkt voor de Linux- en de
  Windows-laag.
* De PE-DLL's komen uit de rootless podman-image `localhost/ravengun-mingw` (mingw-w64). Zonder podman of
  zonder die image slaat het script de Windows-kant over met een melding; `--no-windows` doet dat stil.
* Na het bouwen zet het script elk gebouwd bestand in de `dropin/`-map van elke game die het in `dropin.ini`
  noemt. Dat is datagestuurd: **een drop-in toevoegen is een bestand in `dropin/` zetten plus één sectie in
  `dropin.ini`, zonder codewijziging.**
* De zelftest draait tegen de eerste game met een `dongle.json` van het soort hasp.

### De ordinalen van de Windows-DLL

`TimeCrisisGame-Win64-Shipping.exe` en `RSLauncher.exe` importeren `hasp_windows_x64_105293.dll` **op
ordinalen, zonder namen**: 2, 13, 14 en 15 — `hasp_decrypt`, `hasp_login`, `hasp_logout`, `hasp_read`. De
runtime van de fabrikant heeft ordinal-basis 1 en 25 uitvoeren; `hasp/src/hasp_windows.def` zet dezelfde namen
op dezelfde ordinalen (nagemeten met een eigen PE-lezer). Zonder die ordinalen weigert Windows het laden.

---

## 4. De injectie per spelsoort

### Windows-games onder Proton (Time Crisis 5, en elke game met een `hasp_windows_*.dll`)

Een **PE-DLL als drop-in**:

```
patches/TC5/dropin/dropin.ini
  [hasp_windows_x64_105293.dll]
  To = .
  DllOverride = native
```

De launcher kopieert het bestand naar de map van de game (de view) en zet
`WINEDLLOVERRIDES="hasp_windows_x64_105293=n,b"`. Wine laadt dan onze DLL in plaats van de runtime van de
fabrikant. De donglegegevens komen uit `RAVENGUN_HASP_DIR` + `RAVENGUN_HASP_GAME`, die de dongle-dispatch zet.

Onder Wine is de Unix-wortel het `Z:`-station. Dezelfde `RAVENGUN_HASP_DIR` (een Unix-pad) werkt daarom ook
voor de Windows-laag: `dongle.c` zet een pad dat met `/` begint om naar `Z:` + pad
(`RAVENGUN_HASP_WINPREFIX` kiest een ander station). Gemeten: `dongle 'Halo Fireteam' geladen uit
Z:/home/deck/projects/ravengun/patches/Halo`.

### Linux/ELF-games (ElfLdr2 en de native route: Halo, Terminator, Jurassic Park, Pacman Chomp, Galaga)

De **ELF-bibliotheek** van de juiste breedte (`ravengun-hasp.so` of `ravengun-hasp-x64.so`) hoort in het
ELF-proces van de game. De breedte komt uit `[Game] Is64Bit` in `game.ini` (via `plan["is64"]`).

```
patches/Halo/dropin/dropin.ini
  [ravengun-hasp-x64.so]
  To = .
  Preload = true
```

#### Waarom LD_PRELOAD hier niet in de omgeving van Wine gaat (gemeten)

Deze launcher start altijd Wine, en Wine geeft zijn omgeving door aan alles wat het start — ook aan
`BudgieLoader_x64.exe`, dat een **Cygwin**-programma is. Cygwin leest `LD_PRELOAD` en sterft eraan:

```
ERROR: ld.so: object '.../ravengun-hasp-x64.so' from LD_PRELOAD cannot be preloaded (wrong ELF class: ELFCLASS64): ignored.
Cygwin WARNING: Couldn't compute FAST_CWD pointer...
BudgieLoader_x64 1017 ...: *** fatal error - error while loading shared libraries: .../ravengun-hasp-x64.so
```

Gemeten op 2026-10-11 met een echte start van Halo: met `LD_PRELOAD` in de omgeving start de game niet; zonder
die variabele draait hij door (150 s, 0 keer "fatal error"). Daarom zet `launch-direct.py` **nooit**
`LD_PRELOAD` in de omgeving van de start.

`Preload = true` levert het pad daarom als **`RG_DROPIN_PRELOAD`** (onze eigen variabele, zoals
`RG_LD_PRELOAD`). De **native route** (`scripts/play-native.sh`) en een **eigen lader** zetten dat in
`LD_PRELOAD` van het ELF-proces zelf. Dat is de plek waar het wel werkt:

```
LD_PRELOAD=<onze .so> ./game
[ravengun-hasp] dongle 'Halo Fireteam' geladen uit /home/deck/projects/ravengun/patches/Halo (2 geheugenbestand(en), haspid 748675309)
```

`LD_TRACE_LOADED_OBJECTS=1 LD_PRELOAD=<onze .so> ./game` toont de bibliotheek in de objectlijst van `game`.

Alleen bij een start **zonder** Wine zet de drop-in-motor ook `LD_PRELOAD` (regel: `plan["wine"]` leeg).

#### Wat er nog niet is: de aanroepen onderscheppen (de eerstvolgende stap)

De HASP-aanroepen van de Linux-games zijn **statisch** in de game gelinkt. Gemeten op `game` van Halo
Fireteam, Terminator Salvation, Jurassic Park en Galaga Assault: **0 ongedefinieerde hasp-symbolen** in de
dynamische symbolentabel (`readelf --dyn-syms`, `nm -D --undefined-only`) en **0 verplaatsingen** voor hasp
(`readelf -r`). De game roept `hasp_login`/`hasp_read` dus binnen zijn eigen code aan; er is geen PLT-slot en
geen GOT-entry om te vullen. (Dat past bij wat eerder in het project is gevonden: de pakker van deze games
heeft de verplaatsingsindexen gewist.)

`LD_PRELOAD` zet de emulator dus wel in het proces — de constructor draait, `LD_TRACE_LOADED_OBJECTS` toont de
bibliotheek, en `RAVENGUN_HASP_VERBOSE` laat de geladen dongle zien — maar de statische aanroepen gaan er niet
langs. **Voor deze games is de laag daarmee nog niet werkend.**

Wat er nodig is (en wat TeknoParrot zelf doet): een koppeling die de **aanroepen in de code van de game
herschrijft**. `halo.so` van TeknoParrot doet dat: het bevat `GOT_FIX_SYM`, `GOT_FIX_VSYM` en een
x86-decoder (`MCInst_*`, `X86_getInstruction`) waarmee het de aanroepplekken opzoekt en omzet naar zijn eigen
functies. In `docs/loskoppelen-per-game.md` staat dit als aparte stap ("eigen lader/preload"): voor de 64-bits
familie met plthook + Capstone, voor de 32-bits familie door de vaste GOT te herschrijven en `dlsym` te
onderscheppen. Onze `ravengun-hasp.so` levert de functies al; die koppeling moet er nog omheen.

Zolang dat ontbreekt, is het nut van de Linux-kant van deze laag: de dongle wordt gelezen en gevalideerd
(`scripts/hasp.py`), de bibliotheek staat klaar in de view, en elke aanroep die *wel* dynamisch gaat (een
`dlopen` van de vendor-runtime, of een lader die het zelf doorgeeft) wordt beantwoord.

---

## 5. De dongle-dispatch in `launch-direct.py`

`[Dongle]` in `game.ini` heeft twee bronnen, met een **verschillend doel**:

| `Source` | Wat de launcher doet |
|---|---|
| `usb-passthrough` | **Controleren.** `scripts/dongle.py`: is de fysieke dongle van de eigenaar aangesloten en draait de officiële runtime? Zo niet: weigeren met **exitcode 6**. Dit gedrag is ongewijzigd. |
| `drop-in` | **Activeren.** Wat in `patches/<Game>/dongle.json` staat wordt in het spel geladen. Er wordt **niet** gekeken of er een fysieke dongle is. |

De **soort** staat in `dongle.json` (`kind`), niet in `game.ini` — één bron van waarheid. De dispatch is
expliciet en heeft geen vangnet dat iets anders doet dan gevraagd:

| `kind` | script |
|---|---|
| `hasp` | `scripts/hasp.py` (deze launcher zet de emulator zelf in het spel) |
| `matrix` | `scripts/matrix.py` |
| `region` | `scripts/region.py` |
| `amdaemon` | `scripts/amdaemon.py` |

* Een **onbekende `Source`** → weigering met de bekende waarden (`bekend: usb-passthrough, drop-in`).
* Een **onbekende `kind`** → weigering met de bekende soorten.
* Een **ontbrekend script** → weigering die zegt welk script het bouwt
  (`... maar scripts/matrix.py bestaat nog niet`).
* Geen `dongle.json` terwijl de bron `drop-in` is → weigering die het pad noemt.
* Elke andere fout uit `scripts/gamesettings.py` wordt geweigerd. Eén melding wordt genegeerd: die waarin de
  lezer een `Source` als onbekend aanmerkt (`[Dongle] Source = ... (known: ...)`). De dispatch hier kent de
  twee bronnen immers zelf, en zo blijft een nieuwere `game.ini` werken met een oudere lezer. `DONGLE_SOURCES`
  in `scripts/gamesettings.py` staat inmiddels op `("usb-passthrough", "drop-in")`, dus in de praktijk komt die
  melding niet meer voor.

`scripts/hasp.py` is de tegenhanger van `scripts/dongle.py`:

```
scripts/hasp.py --list                     alle games met een dongle.json
scripts/hasp.py --game <Game> [--json]     de dongle van die game controleren
scripts/hasp.py --game <Game> --bits 32    de 32-bits bibliotheek kiezen
scripts/hasp.py --game <Game> --windows    de PE-DLL in plaats van de ELF-bibliotheek
```

Exit 0 = in orde, 1 = niet in orde. Het `--json`-antwoord bevat `game_dir`, `dongle`, `emulator`,
`injection` (`env`, `preload`, `dll`, `files`, `via`) en `errors`.

### Het contract voor `scripts/<kind>.py`

De andere lagen worden met `--game <Game> --json` aangeroepen en moeten op hun uitvoer JSON zetten:

```json
{"ok": true, "message": "...", "env": {"RAVENGUN_MATRIX_GAME": "RabbidsHollywood"}}
```

Exitcode 0 = in orde; `env` wordt samengevoegd met de omgeving van de start. Bij een andere exitcode weigert de
launcher met `message` (of `errors`). Een laag die geen JSON teruggeeft, levert zijn uitvoer als melding.

---

## 6. De drop-in-motor

`patches/<Game>/dropin/dropin.ini`, met per bestand een sectie (docs/dropins.md):

| Sleutel | Betekenis |
|---|---|
| `To` | doelmap, relatief aan de map van de game (`. ` = de map van de exe) |
| `DllOverride` | `native` → `<naam>=n`; `builtin,native` → `<naam>=n,b`; toegevoegd aan `WINEDLLOVERRIDES` |
| `Env` | extra omgevingsvariabelen, puntkomma-gescheiden |
| `Preload` | `true` → dit bestand moet in het ELF-proces van de game (zie §4) |

* De **gebruikerslaag** `~/.local/share/ravengun/patches/<Game>/dropin/dropin.ini` gaat voor op de
  meegeleverde map; een sectie met dezelfde naam in de meegeleverde map wordt dan overgeslagen.
* Er wordt **nooit** naar de NAS geschreven. Ligt de map van de game op de NAS (geen lokale view), dan weigert
  de motor dat bestand met de opdracht erbij (`scripts/tp-add-game.py <Game> <pad> --view`). Een symlink in de
  view wordt eerst weggehaald, zodat het origineel onaangeroerd blijft.
* Ontbreekt `dropin/` of `dropin.ini`, dan gebeurt er niets.
* Een onbekende `DllOverride`, een `Env`-regel zonder `=`, of een ontbrekend bestand komt als `LET OP`-regel
  in het logboek en in `--plan` (het blokkeert de start niet).
* De drop-in-motor draait bij **elke** start, ook zonder dongle. `--plan` laat zien wat er zou gebeuren;
  `--run` kopieert en zet de omgeving.

---

## 7. Stand per game

| Game | Soort | Breedte | Dongle in `patches/` | Drop-in | Stand |
|---|---|---|---|---|---|
| **Halo** | ElfLdr2 | 64 | `patches/Halo/dongle.json` + 2 images (112 bytes) | `ravengun-hasp-x64.so`, `Preload` | **gegevens compleet**; de laag wordt in de view gezet en het pad gaat mee als `RG_DROPIN_PRELOAD`. De aanroepen zijn nog statisch (§4) |
| **TC5** | TeknoParrot | 64 | `patches/TC5/dongle.json` — **geen image** | 3 PE-DLL's, `DllOverride = native` | **drop-in klaar en gemeten** (ordinalen 2/13/14/15, image-lezing bewezen met `hasp_probe.exe`); de **dongle-image van TC5 is nog niet vastgelegd**, dus `hasp.py` meldt die dongle als onvolledig |
| Terminator | ElfLdr2 | 32 | nog geen `dongle.json` | — | de game heeft een `hasp/`-map in de spelmap; de image moet nog worden vastgelegd |
| Jurassic Park | ElfLdr2 | 32 | nog geen `dongle.json` | — | idem; de vendor-runtime (`utils/pmrtdata/libhasp_linux_*.so`) staat wel in de spelmap |
| Pacman Chomp | ElfLdr2 | 32 | nog geen `dongle.json` | — | geen hasp-bestand in de spelmap gevonden |
| Galaga Assault | ElfLdr2 | 32 | nog geen `dongle.json` | — | idem |

Een dongle toevoegen voor een van deze games is: `patches/<Game>/dongle.json` schrijven (met `kind: hasp`), de
images in `patches/<Game>/dongle/memory/` zetten, `scripts/hasp.py --game <Game>` groen krijgen, en
`scripts/build-hasp.sh --dropin <Game>` draaien.

---

## 8. Bewijs (herhaalbaar)

```bash
# de emulator bouwen en de drop-ins installeren
scripts/build-hasp.sh

# de dongle van een game
scripts/hasp.py --game "Halo Fireteam" --json

# het plan (drop-ins + dongle) zonder bijwerkingen
python3 -I scripts/launch-direct.py Halo --plan

# de Windows-kant: ordinalen, greep en de image, onder Wine
# (een eigen prefix, zodat een lopende game niet geraakt wordt; de eerste keer kost het een wineboot)
WINE=$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine
PFX=$HOME/.local/share/ravengun/runtime/hasp-test-prefix
T=$HOME/.local/share/ravengun/runtime/hasp-test
mkdir -p "$T" && cp patches/TC5/dropin/hasp_windows_x64_105293.dll \
  "$HOME/.local/share/ravengun/hasp/windows/hasp_probe.exe" "$T/"
WINEPREFIX=$PFX "$WINE" wineboot --init            # alleen de eerste keer
cd "$T" && WINEPREFIX=$PFX WINEDEBUG=-all WINEDLLOVERRIDES="hasp_windows_x64_105293=n,b" \
  RAVENGUN_HASP_VERBOSE=1 RAVENGUN_HASP_DIR=~/projects/ravengun/patches RAVENGUN_HASP_GAME=Halo \
  "$WINE" hasp_probe.exe hasp_windows_x64_105293.dll

# de Linux-kant: de emulator in het ELF-proces van de game
cd ~/.local/share/ravengun/runtime/halo-g7-tp/halo
LD_PRELOAD=$PWD/ravengun-hasp-x64.so RAVENGUN_HASP_VERBOSE=1 \
  RAVENGUN_HASP_DIR=~/projects/ravengun/patches RAVENGUN_HASP_GAME=Halo ./game -f1920x1080 -n

# de tests
python3 -I tests/test_hasp.py
python3 -m unittest discover -s tests -q
```

---

## 9. Wat nog open is

1. **De statische hasp-aanroepen van de Linux-games onderscheppen — dit is de eerstvolgende stap en zonder
   die is de Linux-kant niet werkend.** `LD_PRELOAD` zet de emulator in het proces, maar de aanroepen gaan er
   niet langs: 0 ongedefinieerde hasp-symbolen en 0 PLT-verplaatsingen in `game` van Halo, Terminator,
   Jurassic Park en Galaga Assault. Nodig is de PLT/GOT-koppeling uit `docs/loskoppelen-per-game.md`
   (`plthook` + Capstone voor 64-bits, de vaste GOT herschrijven voor 32-bits), zoals `halo.so` van TeknoParrot
   doet met `GOT_FIX_SYM` en zijn eigen x86-decoder. De Windows-kant heeft dit niet nodig: daar is de DLL een
   gewone import en werkt de vervanging al (zie §0).
2. **`RG_DROPIN_PRELOAD` laten lezen door de native route en de eigen lader.** Zonder dat wordt de
   Linux-bibliotheek wel in de view gezet maar nooit geladen.
3. **De dongle-images van TC5, Terminator en Jurassic Park vastleggen.** TC5's image zit niet in de spelmap;
   Terminator en Jurassic Park hebben er wel een (`hasp/`, `utils/pmrtdata/`), maar de fileids en de
   beschrijving moeten nog worden uitgezocht.
4. **Geen serienummers.** De laag leest en logt geen unieke identificaties: alleen de dongle-beschrijving en de
   geheugenimages. `scripts/dongle.py` (de fysieke kant) heeft daar een eigen test voor.
