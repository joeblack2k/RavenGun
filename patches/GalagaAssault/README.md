# Galaga Assault

Instellingen: `game.ini`. Dit bestand gaat over de **HASP-dongle** van deze game.
Bevindingen en meetgeschiedenis: `findings/GalagaAssault.md` (op de NAS naast de games, niet in deze repo).

## Vraagt deze game een dongle? — JA

Bewijs, allemaal gemeten op `Galaga Assault/game` (ELF 32-bits, 7.278.592 bytes, geen sectiekoppen;
de dynsym is via `PT_DYNAMIC` gelezen — `DT_HASH` 0x86351c8, `DT_SYMTAB` 0x8051a0c, `DT_STRTAB`
0x806511c, 4.718 dynamische symbolen):

* **28 `hasp_`-symbolen staan als GEDEFINIEERD in de dynsym**, met 0 ongedefinieerde
  `hasp_`-symbolen en 0 PLT-verplaatsingen. De HASP-client zit statisch in het beeld.
* De pakker-handtekening `comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1` staat op VA 0x08659d30.
  Ook de losse tekst `HASPE_LINUX_LOADER_1.1` en `PATCHED_WITH_HASPE_LINUX_LOADER_1.1` staat in het
  bestand.
* `hasp_login` (VA 0x08227970) wordt **vier keer** aangeroepen, alle vier met
  **`vendor_code = 0xffff0000`**: VA 0x809e2fb, 0x809e48c, 0x809e536, 0x809e580. De `scope` die
  meegaat is de string op VA 0x83154e0:
  `2jIY6Dv02LQbOVVV9P2xjLr01j5R2LHiQM3Qsohu5ksZZkQUQByfsbNcIJwBC5B5`
  (dat is een waarde uit de game-ELF zelf, geen gegeven van de eigenaar).
* `hasp_read` (VA 0x08226150) wordt **één keer** aangeroepen: VA 0x809e333 met **fileid 65520 =
  0xFFF0**, offset **0x28**, lengte 4, buffer het globale adres 0x8315b80.
* `hasp_write` (VA 0x08226070) wordt **één keer** aangeroepen: VA 0x809e4c0, dezelfde fileid, offset
  en lengte. Het spel **schrijft** die 4 bytes dus terug — dit is een lees/schrijf-gebied.
* `hasp_get_info` en `hasp_get_size` hebben **0 aanroepplekken**.

## Wat er in de drop-in staat

```
dropin/
  dropin.ini                                   een sectie per bestand hieronder
  librt-glfix.so                               de GL-fix (bestond al)
  libravengun-hasphook.so                      de inline-hook voor de statisch gelinkte hasp_-aanroepen (bestond al)
  libravengun-hasphook-x64.so                  overblijfsel van build-hasphook.sh; sectie zonder Preload
  ravengun-hasp.so                             de HASP-emulator, 32-bits (nieuw)
  ravengun-hasp/GalagaAssault/dongle.json      de donglebeschrijving (nieuw)
  ravengun-hasp/GalagaAssault/memory/00000000000000001111111111110000   de image, 112 bytes (nieuw)
```

De emulator krijgt **geen `Preload`** (glibc eist `la_version` van een `LD_AUDIT`-bibliotheek en de
emulator heeft die niet; alleen de inline-hook staat op `Preload = true` en laadt de emulator zelf).
Er is **geen `[Dongle]`-sectie** in `game.ini`: de loader weet niets van dongles.

## De dongle

| veld | waarde | herkomst |
|---|---|---|
| `kind` | `hasp` | het soort dongle |
| `fileid` | **65520** (`0xFFF0`) | de `hasp_read`-aanroepplek VA 0x809e333 en de `hasp_write`-aanroepplek VA 0x809e4c0 |
| `size` | **112** | de HASP-HL-hoofdgeheugengrootte zoals in dit project gemeten (Halo Fireteam) |
| `name` | `read/write` | het spel leest én schrijft dit gebied |
| `haspid` | **weggelaten** | niet gemeten. Geen gemis: het spel roept `hasp_get_info` nooit aan |

### De image is GECONSTRUEERD, niet gedumpt

**De originele bytes van deze dongle zijn in geen enkele bron gevonden.** Ik heb de hele NAS-boom
doorzocht op de bestandsnaamregel van dit formaat (32 tekens 0/1), op hex-fileid-namen
(`fff0`..`ffff`), op bestanden van 112 bytes, op mappen `*hasp*`/`*dongle*`, op
`*.dmp`/`*.dgl`/`*.rgd`/`*aladdin*` en op `hasp.bin`. De **enige** dongle-images die daar staan zijn
die van Halo Fireteam. Deze spelmap heeft zelfs geen `hasp`-map en geen `libhasp_linux_*.so`.

De image is dus **112 bytes van 0xFF**, en dat is geen vrije keuze:

1. `0xFF` is het patroon van **gewist HASP-HL-geheugen**, zoals de **echte** dump
   `Terminator Salvation/hasp/hhl_mem.dmp` het laat zien (bytes 0x28..0x6b zijn daar alle 0xFF).
2. Voor deze game is het bovendien onschadelijk: het gebied op offset 0x28 wordt door het spel zelf
   overschreven (`hasp_write`, VA 0x809e4c0).

Alle andere getallen komen uit de code (zie de tabel en de aanroepplekken hierboven).

## Verifiëren

```sh
cd ~/projects/ravengun
python3 scripts/hasp.py --game GalagaAssault --json     # ok: true, errors: []
python3 scripts/launch-direct.py GalagaAssault --plan
```

De keten end-to-end, gemeten met een 32-bits probe die zijn eigen `hasp_*`-functies in het beeld
heeft (net als `game`):

```
hasp_login(0xffff0000) -> 0, greep = 0xe6f
hasp_get_size(65520) -> 0, grootte = 112
hasp_read(65520, 0, 16) -> 0, eerste bytes: ff ff ff ff ff ff ff ff
echte_aanroepen = 0
RESULTAAT: alle aanroepen zijn door de EMULATOR beantwoord; de statisch gelinkte client is NIET aangeroepen
```

## Wat NIET is vastgesteld

* **De echte dongle-bytes.** Zie hierboven: niet in een bron te vinden. De image is gereconstrueerd.
* **`haspid`, `features`, `keycaps`.** Niet gemeten, daarom weggelaten; de XML wordt nooit opgevraagd.
* **Of het spel voorbij zijn donglecontrole komt.** Dat kan in deze omgeving niet: het spel stopt nu al
  eerder op ontbrekende lettertypen (`images/fonts/zoinks*.tga`, `AlSeana*.tga`; zie
  `findings/GalagaAssault.md`) en daarnaast op dezelfde laderbeperking als Jurassic Park.
* **Wat de dongle op fileid 0xFFF0 offset 0x28 moet antwoorden.** Het spel schrijft er zelf naartoe,
  dus vermoedelijk een teller, maar de beginterm staat in geen enkele bron.

## Herhalen: de keten end-to-end meten

De metingen hierboven komen van een 32-bits probe die zijn eigen `hasp_*`-functies **statisch in het
beeld** heeft — precies de situatie van `game`. Die probe is een geparametriseerde kopie van
`tools/rt-hasphook/hasp_zelftest.c` uit de loader-repo; het enige verschil is dat de fileid uit
`RAVENGUN_PROBE_FILEID` komt, zodat dezelfde test voor 65520, 65522 en 65524 kan draaien.

```sh
# 1. de probe bouwen (de -I is nodig: de dev-header gnu/stubs-32.h ontbreekt op SteamOS en
#    scripts/build-hasp.sh maakt hem lokaal in hasp/src/inc/gnu/ aan)
gcc -m32 -I ~/projects/ravengun/hasp/src/inc -O1 -rdynamic -o hasp_probe <de kopie>

# 2. draaien met de hook en de dongle van deze game
LD_PRELOAD=<dropin>/libravengun-hasphook.so \
RAVENGUN_HASP_VERBOSE=1 \
RAVENGUN_HASP_GAME=GalagaAssault \
RAVENGUN_HASP_DIR=<dropin>/ravengun-hasp \
RAVENGUN_PROBE_FILEID=65520 \
HASP_ZELFTEST_WACHT=2000 ./hasp_probe
```

De probe wacht standaard 1500 ms (`HASP_ZELFTEST_WACHT`) omdat de hook vanuit een kijker-thread
werkt en het spelbeeld eerst moet vinden. Het logboek van de hook staat in `RAVENGUN_HASP_ELF_LOG`.
