# Jurassic Park Arcade

> **STAND 2026-10-13, 23:25 — lees eerst `ATTRACT-RUN.md`.** De game draait zijn attract-lus op onze eigen
> lader (12 minuten, op de tv, onafhankelijk meegekeken), met de rode melding "RIO Disconnected" op elk beeld
> en zonder invoer: hij demonstreert, hij is niet speelbaar. Hoe de 577 importsloten hun naam terugkregen
> staat in `EERSTE-PLT-SLOTS.md`. Wat hieronder over de dongle staat is van 2026-10-11/12 en op vier punten
> achterhaald (de game SCHRIJFT wel naar de dongle; de lezingen zijn 1 en 4 bytes; `hasp_decrypt` en
> `hasp_encrypt` worden wel gebruikt; de donglecontrole wordt op onze lader wel bereikt en gehaald) — zie
> `ATTRACT-RUN.md` §3 en §6. De image is nog steeds geconstrueerd, niet gedumpt.

Instellingen: `game.ini`. Dit bestand gaat over de **HASP-dongle** van deze game.
Bevindingen en meetgeschiedenis: `findings/JurassicPark.md` (op de NAS naast de games, niet in deze repo).

## Vraagt deze game een dongle? — JA

Bewijs, allemaal gemeten op `Jurassic Park/game` (ELF 32-bits, 17.694.720 bytes, geen sectiekoppen;
de dynsym is daarom via `PT_DYNAMIC` gelezen — `DT_HASH` 0x8fc90c0, `DT_SYMTAB` 0x806e680,
`DT_STRTAB` 0x80b9bb0, 18.810 dynamische symbolen):

* **30 `hasp_`-symbolen staan als GEDEFINIEERD in de dynsym** (alle `FUNC`, `st_shndx` 11,
  `st_value != 0`), met 0 ongedefinieerde `hasp_`-symbolen en 0 PLT-verplaatsingen. De HASP-client
  zit dus statisch in het beeld; er is geen PLT-slot of GOT-invoer om te vullen.
  Zie `docs/hasp-elf-hook.md`.
* De pakker-handtekening `comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1` staat op VA 0x09048d30
  (Sentinel LDK HASP Envelope).
* `hasp_login` (de functie die het spel zelf bezit, VA 0x08529650) wordt **drie keer** aangeroepen:
  * VA 0x837c420 met `vendor_code = 0xffff0000`
  * VA 0x837c4f9 met `vendor_code = 0`
  * VA 0x837c87a met een `vendor_code` uit een variabele
* `hasp_read` (VA 0x08528520) wordt **vier keer** aangeroepen, alle vier met **fileid 65522 =
  0xFFF2** en lengte 4: VA 0x837c948, 0x837ca85, 0x837cb75, 0x837cc2d.
* `hasp_get_info` en `hasp_get_size` hebben **0 aanroepplekken**: het spel vraagt de XML nooit op.
* De controle zelf staat in de functie op VA 0x837c9e0: die leest 4 bytes op offset `index*4-4` en
  keurt twee patronen goed — `00 00 FF FF` (resultaat 1) en `FF FF FF FF` (resultaat 0). De functie
  op VA 0x837cb90 leest 4 bytes op offset **0x3c** en keurt `FF FF FF FF` goed.

## Wat er in de drop-in staat

```
dropin/
  dropin.ini                                   een sectie per bestand hieronder
  librt-glfix.so                               de GL-fix (bestond al)
  libravengun-hasphook.so                      de inline-hook voor de statisch gelinkte hasp_-aanroepen (bestond al)
  libravengun-hasphook-x64.so                  overblijfsel van build-hasphook.sh; sectie zonder Preload
  ravengun-hasp.so                             de HASP-emulator, 32-bits (nieuw)
  ravengun-hasp/JurassicPark/dongle.json       de donglebeschrijving (nieuw)
  ravengun-hasp/JurassicPark/memory/00000000000000001111111111110010   de image, 112 bytes (nieuw)
```

Twee dingen die met opzet zo zijn:

* **De emulator krijgt GEEN `Preload`.** glibc eist van een bibliotheek in `LD_AUDIT` dat hij
  `la_version` exporteert; `ravengun-hasp.so` doet dat niet en wordt geweigerd
  (`undefined symbol: la_version; ignored`). Alleen de inline-hook staat op `Preload = true`; die
  laadt de emulator zelf met `dlopen`. De kopie in `dropin/` staat er omdat het drop-in-contract
  zegt dat de emulatorbibliotheek in de drop-in hoort.
* **`libravengun-hasphook-x64.so` heeft een sectie ZONDER `Preload`.** Deze game is 32-bits en de
  lezer is 32-bits; een 64-bits object wordt geweigerd met `wrong ELF class: ELFCLASS64`. Het
  bestand komt uit `scripts/build-hasphook.sh`, dat beide breedtes in elke game legt. Een bestand
  zonder sectie wordt niet uitgerold (`docs/dropins.md`); weglaten mag ook.

Er is **geen `[Dongle]`-sectie** in `game.ini`: de loader weet niets van dongles. Een software-dongle
is een gewone drop-in; de launcher zet de bestanden in de view en zet de omgevingsvariabelen uit
`dropin.ini`.

## De dongle

| veld | waarde | herkomst |
|---|---|---|
| `kind` | `hasp` | het soort dongle |
| `fileid` | **65522** (`0xFFF2`) | de vier `hasp_read`-aanroepplekken in `game` |
| `size` | **112** | de HASP-HL-hoofdgeheugengrootte zoals in dit project gemeten (Halo Fireteam) |
| `name` | `read-only` | het spel leest dit gebied alleen; het schrijft er niet in |
| `haspid` | **weggelaten** | niet gemeten. Geen gemis: het spel roept `hasp_get_info` nooit aan |

### De image is GECONSTRUEERD, niet gedumpt

**De originele bytes van deze dongle zijn in geen enkele bron gevonden.** Ik heb de hele NAS-boom
doorzocht op de bestandsnaamregel van dit formaat (32 tekens 0/1), op hex-fileid-namen
(`fff0`..`ffff`), op bestanden van 112 bytes, op mappen `*hasp*`/`*dongle*`, op
`*.dmp`/`*.dgl`/`*.rgd`/`*aladdin*` en op `hasp.bin`. De **enige** dongle-images die daar staan zijn
die van Halo Fireteam (`Halo Fireteam/g7/halo/00000000000000001111111111110100`, `…01`, `fff4`).
`utils/pmrtdata/` bevat acht `libhasp_linux_<vendorid>.so` en twee `.tga`'s — geen image. De notitie
in `docs/hasp-elf-hook.md` §6.4 dat de images daar staan, klopt niet; dat is nagemeten.

De image is dus **112 bytes van 0xFF**, en dat is geen vrije keuze:

1. `0xFF` is het patroon van **gewist HASP-HL-geheugen**, zoals de **echte** dump
   `Terminator Salvation/hasp/hhl_mem.dmp` het laat zien: daar zijn de bytes 0x28..0x6b alle 0xFF.
2. `FF FF FF FF` is een van de **twee patronen die de controle van deze game zelf goedkeurt**
   (VA 0x837c9e0, en op offset 0x3c VA 0x837cb90).

Alle andere getallen komen uit de code (zie de tabel en de aanroepplekken hierboven).

## Verifiëren

```sh
cd ~/projects/ravengun
python3 scripts/hasp.py --game JurassicPark --json     # ok: true, errors: []
python3 scripts/launch-direct.py JurassicPark --plan   # kijk naar de dropins-regel
```

`dest` moet eindigen op `views/JurassicPark/ravengun-hasp/JurassicPark/dongle.json` — het pad staat
niet dubbel in.

De keten end-to-end (statisch gelinkte aanroep -> inline-hook -> emulator -> deze dongle), gemeten
met een 32-bits probe die zijn eigen `hasp_*`-functies in het beeld heeft, net als `game`:

```sh
LD_PRELOAD=<dropin>/libravengun-hasphook.so RAVENGUN_HASP_VERBOSE=1 \
RAVENGUN_HASP_GAME=JurassicPark RAVENGUN_HASP_DIR=<dropin>/ravengun-hasp \
RAVENGUN_PROBE_FILEID=65522 ./hasp_probe
```

Uitkomst (2026-10-11):

```
hasp_login(0xffff0000) -> 0, greep = 0xe6f
hasp_get_size(65522) -> 0, grootte = 112
hasp_read(65522, 0, 16) -> 0, eerste bytes: ff ff ff ff ff ff ff ff
hasp_read(65522, 0x3c, 4) -> 0, bytes: ff ff ff ff
echte_aanroepen = 0
RESULTAAT: alle aanroepen zijn door de EMULATOR beantwoord; de statisch gelinkte client is NIET aangeroepen
```

## Wat NIET is vastgesteld

* **De echte dongle-bytes.** Zie hierboven: niet in een bron te vinden. De image is gereconstrueerd.
* **`haspid`, `features`, `keycaps`.** Niet gemeten, daarom weggelaten. Voor deze game maakt dat niet
  uit: `hasp_get_info` wordt nooit aangeroepen, dus de XML wordt nooit opgevraagd.
* **Of het spel voorbij zijn donglecontrole komt.** Dat kan in deze omgeving niet gemeten worden: het
  spel sterft al eerder aan de OpenGL-blokkade (`GL_MAX_TEXTURE_SIZE > 2047`, zie `game.ini`,
  `[Status] Note`, en `docs/hasp-elf-hook.md` §4.3: 3 van 3 starts sterven bij `Starting Game`,
  met en zonder de hook). De donglecontrole wordt dus nooit bereikt. De keten zelf is wel bewezen
  met de probe hierboven.
* **De vendor_code.** `0xffff0000` is wat het spel doorgeeft (gemeten), maar wat een echte dongle
  daarop antwoordt is niet vastgesteld; de emulator negeert de vendor_code en geeft de vaste greep
  0xE6F terug.

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
RAVENGUN_HASP_GAME=JurassicPark \
RAVENGUN_HASP_DIR=<dropin>/ravengun-hasp \
RAVENGUN_PROBE_FILEID=65522 \
HASP_ZELFTEST_WACHT=2000 ./hasp_probe
```

De probe wacht standaard 1500 ms (`HASP_ZELFTEST_WACHT`) omdat de hook vanuit een kijker-thread
werkt en het spelbeeld eerst moet vinden. Het logboek van de hook staat in `RAVENGUN_HASP_ELF_LOG`.

## Zonder ElfLdr2 (gemeten)

Deze game is een **gewone Linux-ELF** (`ELF32 i386`), geen Windows-programma: hij heeft
`/lib/ld-linux.so.2` als interpreter en vraagt dezelfde bibliotheken als elk Linux-spel. Toch
start hij niet als je hem los start — en dat is gemeten, niet aangenomen:

```
/home/deck/.local/share/ravengun/runtime/views/JurassicPark/game: error while loading shared libraries: libtheora.so.0: cannot open shared object file: No such file or directory
```

Zonder ElfLdr2 stopt hij dus al bij de dynamic linker: **`libtheora.so.0`** staat niet op deze
machine. In totaal mist deze machine er 5: `libGLU.so.1`, `libPhysXLoader.so.1`, `libSDL-1.2.so.0`, `libglut.so.3`, `libtheora.so.0`.
Dat zijn precies de bibliotheken die `ElfLdr2` in zijn eigen proces emuleert.

De volledige meting over de hele familie staat in `patches/ELF-NATIVE.md`; de generator is
`patches/elf-native.py`.
