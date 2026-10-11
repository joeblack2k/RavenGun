# De HASP-aanroepen van de Linux/ELF-games onderscheppen

*RavenGun, ronde 12 (2026-10-11). Dit is het antwoord op de vraag **welk mechanisme** de
HASP-aanroepen van de Linux-games gebruiken, en wat er nodig is om ze te onderscheppen. De
documentatie van de emulator zelf staat in `docs/hasp-layer.md`; dit document gaat over de koppeling
tussen het spel en die emulator.*

---

## 0. De uitkomst in één alinea

De aanname in `docs/hasp-layer.md` §4 en §9 — "nodig is de PLT/GOT-koppeling" — **klopt niet**.
De HASP-cliënt van deze games is **statisch in het `game`-bestand gelinkt**: de functies zijn
*gedefinieerd* in het spel en staan op een vast adres in zijn eigen beeld. Er is dus geen PLT-slot en
geen GOT-invoer om te vullen, en GOT-herschrijving (de techniek van `tools/rt-glfix`) of
`LD_PRELOAD`-symboolinterpositie kan hier niet werken. De aanroepen gaan rechtstreeks naar de code in
het eigen beeld. Wat wél werkt is de functies **op hun ingangspunt** aanhaken: de eerste bytes
vervangen door een sprong naar onze eigen functie. Dat is precies wat TeknoParrot zelf doet in
`Halo Fireteam/g7/halo/halo.so`. Die aanpak is gebouwd (`tools/rt-hasphook/`) en **bewezen**: een
statisch gelinkte `hasp_login` wordt onderschept en door onze emulator beantwoord, met de echte
donglegegevens van Halo Fireteam (greep `0xe6f`, grootte 112, eerste bytes `2c 03 ab 63`), terwijl de
statisch gelinkte cliënt **nul keer** wordt aangeroepen.

De drie mogelijkheden uit de opdracht, met het antwoord:

| # | mogelijkheid | uitkomst |
|---|---|---|
| 1 | de cliënt is statisch gelinkt en praat met echte hardware | **dit is het** (voor alle vier de games) |
| 2 | de aanroepen gaan via `dlsym` | nee: er is geen `dlopen`/`dlsym` van de hasp-cliënt; de cliënt zit in het beeld |
| 3 | de game roept via de PLT, maar de verplaatsingsindexen zijn gewist | **nee, en dat is een nieuw feit**: de dynsym van deze games is *compleet* (geen gewiste indexen) en bevat de hasp-symbolen als *gedefinieerde* symbolen |

Punt 3 is de moeite waard: de HASP-agent concludeerde uit "0 PLT-verplaatsingen" dat de indexen
gewist waren (zoals bij Jurassic Park voor de GL-functies). Dat is voor hasp **niet** het geval. De
dynsym van Jurassic Park heeft 18811 symbolen, waarvan 30 met `hasp_`, alle met `st_shndx = 11` (een
echte sectie) en `st_value != 0`; Galaga Assault heeft 4719 symbolen waarvan 28 met `hasp_`. Er is
niets gewist — er is simpelweg geen import om te verplaatsen.

---

## 1. De meting

### 1.1 De vier games

| game | klasse | type | ongedefinieerde hasp-symbolen | PLT-verplaatsingen met hasp | hasp-symbolen *gedefinieerd* in de game |
|---|---|---|---|---|---|
| Halo Fireteam (`g7/halo/game`) | 64-bits | EXEC | 0 | 0 | 3 (`patchInit`, `patchDtInitHook`, `closeThread`) |
| Terminator Salvation (`game`) | 32-bits | EXEC | 0 | 0 | **0** (zie §5) |
| Jurassic Park (`game`) | 32-bits | EXEC | 0 | 0 | **30** |
| Galaga Assault (`game`) | 32-bits | EXEC | 0 | 0 | **28** |

Halo Fireteam exporteert de `hasp_*`-namen niet in zijn dynsym (alleen de drie hulpjes van de
envelope); de rest is er wel, maar zonder naam. De 32-bits games exporteren ze wél, en dat is wat de
koppeling hieronder mogelijk maakt.

### 1.2 De handtekening van de pakker

In Jurassic Park en Galaga Assault staat deze versiegebonden export:

```
09048d30 T comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1     (Jurassic Park)
08659d30 T comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1     (Galaga Assault)
```

`comaladdin` + `hl` + `x86` + `env` + `spoony` met versie `HASPE_LINUX_LOADER_1.1` is de
handtekening van de **Sentinel LDK HASP Envelope** (Aladdin/SafeNet/Thales): de pakker heeft de
runtime van de fabrikant in het programma opgenomen en de `hasp_*`-namen erbij gezet. Dat verklaart
alles in één keer: de cliënt is statisch, de namen staan in de dynsym, en er is geen import.

### 1.3 Waarom de oude conclusie niet volgde

`docs/hasp-layer.md` §4 zegt: "De game roept `hasp_login`/`hasp_read` dus binnen zijn eigen code aan;
er is geen PLT-slot en geen GOT-entry om te vullen. (Dat past bij wat eerder in het project is
gevonden: de pakker van deze games heeft de verplaatsingsindexen gewist.)"

De eerste helft is juist en dat is de kern. De tweede helft is een verklaring die niet uit de meting
volgt: als de indexen gewist waren, zouden de `hasp_*`-symbolen als **ongedefinieerd** in de dynsym
staan (met een verplaatsing die naar `0xdeadbeef` wijst). Ze staan er als **gedefinieerd**. En de
daarop volgende conclusie — "nodig is de PLT/GOT-koppeling" — is daarmee onjuist: er is geen
PLT/GOT-koppeling mogelijk voor een symbool dat het spel zelf bezit.

### 1.4 Hoe je het narekent

```bash
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park/game"

# 1. geen ongedefinieerde hasp-symbolen
nm -D --undefined-only "$G" | grep -i hasp        # niets

# 2. geen PLT-verplaatsingen voor hasp
readelf -r "$G" | grep -i hasp                    # niets
readelf -d "$G" | grep -E 'JMPREL|PLTRELSZ'       # er ZIJN wel PLT-verplaatsingen (101 stuks)

# 3. maar wél gedefinieerde hasp-symbolen in de dynsym van de game
nm -D "$G" | grep -i ' hasp_'
#   08529650 T hasp_login
#   08528520 T hasp_read
#   ...

# 4. de handtekening van de envelope
nm -D "$G" | grep -i spoony
#   09048d30 T comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1
```

Omdat de game geen sectiekoppen heeft, werkt `readelf` niet overal; het eigen leesprogramma
`tools/rt-hasphook/hasp_inline_hook.c` en `docs/research/hasp-elf-dynsym.md` lezen de dynsym via
`PT_DYNAMIC`, en dat is de weg die de koppeling zelf ook gebruikt.

---

## 2. Wat TeknoParrot zelf doet (`halo.so`)

`Halo Fireteam/g7/halo/halo.so` is de 64-bits preload van TeknoParrot voor Halo Fireteam. Het is het
beste bewijs van hoe het spel het zelf doet, en het is ontleed:

* de bibliotheek exporteert `hasp_login`, `hasp_read`, `hasp_write`, `hasp_encrypt`,
  `hasp_get_size`, `hasp_get_info`, `hasp_get_sessioninfo`, `hasp_logout`, `hasp_login_scope` en
  `hasp_free` — plus `real_hasp_*`-trampolines en de dongle-beschrijving in XML;
* `GOT_FIX_SYM` (0x4e79) en `GOT_FIX_VSYM` (0x4dda) zijn **geen GOT-herschrijvers**, ondanks hun
  naam. Ze doen `dlopen(NULL, RTLD_NOW)` en dan `dlsym` respectievelijk `dlvsym`, en schrijven het
  resultaat in een meegegeven pointer. Het zijn **oplossers**;
* de echte koppeling zit in `initialize()` (0x569f). Die roept acht keer `flh_inline_hook` aan:

  ```asm
  569f <initialize>:
    ...
    mov  edi, 0xb2d630          ; het adres van hasp_login IN DE GAME
    mov  rsi, <de hookfunctie>
    call flh_inline_hook
    mov  [real_hasp_login], rax ; de trampoline
    ...
    mov  edi, 0xb2fd63          ; hasp_read
    ...
  ```

* `flh_inline_hook`, `flh_inline_unhook`, `flh_patch_memory`, `flh_set_memory_permission` en een
  complete Capstone-decoder (`X86_getInstruction`, `MCInst_*`) zitten in dezelfde bibliotheek;
* de adressen staan **hardgecodeerd**: 0xb2e1aa (encrypt), 0xb2d630 (login), 0xb2dd48 (logout),
  0xb2fd63 (read), 0xb2fe60 (write), 0xb2e391 (free), 0xb2f5ac (get_sessioninfo). Die adressen
  liggen in de 32 MB grote `game` van Halo Fireteam.

Dat is het patroon: **aanhaken op het ingangspunt van de statisch gelinkte functie**, niet op een
PLT of GOT. Deze implementatie doet hetzelfde, maar haalt de adressen tijdens het draaien uit de
dynsym van de game in plaats van ze hard te coderen — daardoor werkt dezelfde bibliotheek voor de
hele familie en blijft hij kloppen als de pakker andere adressen kiest.

---

## 3. Wat er gebouwd is

```
tools/rt-hasphook/
  hasp_inline_hook.c   de koppeling: leest de dynsym van de game, haakt de hasp-functies aan op hun
                       ingangspunt, laadt de emulator en leidt de aanroepen daarheen
  hasp_zelftest.c      de zelftest: een 32-bits ELF die zijn eigen hasp-functies statisch in het
                       beeld heeft en ze via een zelf uitgerekend adres aanroept (net als de game)
  dynsym.py            leest de dynsym van een gepakte game (geen sectiekoppen) via PT_DYNAMIC
scripts/build-hasphook.sh   bouwt beide breedtes en legt ze in patches/<Game>/dropin/
patches/<Game>/dropin/libravengun-hasphook{,-x64}.so
```

De drop-in staat als **sectie in `dropin.ini`** (niet in een eigen `.ini`: de launcher leest alleen
`dropin.ini`, zie `docs/dropins.md`):

```ini
[libravengun-hasphook.so]
To = .
Preload = true
Env = RAVENGUN_HASP_ELF_LOG=hasphook.log;RAVENGUN_HASP_VERBOSE=1
```

Die sectie staat in `patches/JurassicPark/dropin/dropin.ini` en
`patches/GalagaAssault/dropin/dropin.ini`, **naast** de sectie van `librt-glfix.so`. Voor
`patches/Halo/dropin/` staat de bibliotheek klaar, maar de sectie nog niet: bij Halo vindt de
koppeling geen namen om aan te haken (§6, punt 2). Terminator heeft geen `dropin/` en krijgt die ook
niet: er is geen cliënt om aan te haken (§5).

### 3.1 Hoe de koppeling werkt

1. **De game-ELF van schijf lezen.** Het proces waarin de bibliotheek komt is onder ElfLdr2 de
   `wine-preloader`, niet de game. De game wordt gevonden via `RAVENGUN_HASP_ELF_GAME`, anders via
   de commandoregel (`Z:\...\game`), anders via `/proc/self/exe`, anders via `./game` in de werkmap.
   De dynsym wordt via `PT_DYNAMIC` gelezen (`DT_SYMTAB`, `DT_STRTAB`, `DT_HASH` of `DT_GNU_HASH`),
   want deze games hebben geen sectiekoppen.
2. **Het spelbeeld in het geheugen vinden.** Het laagste `PT_LOAD`-adres is het adres waarop de game
   hoort te staan (0x08048000 voor de 32-bits familie). De kop in het geheugen wordt met die van het
   bestand vergeleken (klasse, codering, machine, type, ingangspunt, `e_phoff`, `e_phnum`), zodat een
   andere 32-bits ELF in hetzelfde proces geen valse treffer geeft. Lukt dat niet meteen, dan zoekt
   de kijker-thread verder in `/proc/self/maps`.
3. **Aanhaken.** Voor elke `hasp_*`-naam uit de doelenlijst wordt het adres `beeld + st_value`
   berekend en worden de eerste bytes vervangen:
   * 32-bits: `e9 <rel32>` (5 bytes);
   * 64-bits: `48 b8 <abs64> ff e0` (12 bytes, een absolute sprong).
   Van de originele bytes wordt een **trampoline** gemaakt (originele bytes + sprong terug), zodat
   een vervanger de echte functie alsnog kan aanroepen. De eerste `PT_LOAD` van de gepakte 32-bits
   games is RWX, dus het beeld is al schrijfbaar; waar dat niet zo is doet de bibliotheek een
   `mprotect`.
4. **De emulator laden.** `dlopen(..., RTLD_NOW | RTLD_LOCAL)` op
   `~/.local/share/ravengun/hasp/ravengun-hasp{,-x64}.so` (of `RAVENGUN_HASP_ELF_LIB`), en de
   `hasp_*`-functies eruit halen met `dlsym` op die greep. `RTLD_LOCAL` is hier het punt: de
   exporten van de emulator mogen **niet** in de globale naamruimte komen, want dan zou het spel ze
   via de gewone symboolopzoeking kunnen vinden in plaats van via onze haak.
   (`dlmopen` met een eigen naamruimte is er als optie — `RAVENGUN_HASP_ELF_DLMOPEN=1` — maar bleek
   onder de Wine-preloader onbetrouwbaar: het proces sterft.)
5. **De aanroepen.** Elke vervanger meldt de aanroep (bij `RAVENGUN_HASP_VERBOSE=1`, één regel per
   aanroep — het meetpunt) en gaat naar de emulator. Is de emulator er niet, dan valt hij terug op de
   trampoline, zodat het gedrag van het spel precies blijft zoals het was.

### 3.2 De omgeving

| variabele | betekenis |
|---|---|
| `RAVENGUN_HASP_GAME` | de gamenaam; de emulator zoekt hiermee de donglegegevens |
| `RAVENGUN_HASP_DIR` | de wortel van de donglegegevens (standaard `/hasp`) |
| `RAVENGUN_HASP_VERBOSE` | niet leeg en niet `0`: één regel per aangeroepen functie |
| `RAVENGUN_HASP_ELF_LOG` | pad voor het logboek van de koppeling |
| `RAVENGUN_HASP_ELF_GAME` | pad naar de game-ELF (anders de commandoregel/`/proc/self/exe`/`./game`) |
| `RAVENGUN_HASP_ELF_LIB` | pad naar de emulator |
| `RAVENGUN_HASP_ELF_WACHT` | hoe lang op het spelbeeld wachten, in seconden (standaard 90) |
| `RAVENGUN_HASP_ELF_MS` | poll-interval in milliseconden (standaard 5) |
| `RAVENGUN_HASP_ELF_UIT` | `1`: alleen meten, niets aanhaken |
| `RAVENGUN_HASP_ELF_DLMOPEN` | `1`: de emulator in een eigen naamruimte proberen |

### 3.3 Bouwen

```bash
scripts/build-hasphook.sh                  # bouwen en naar alle games in GAMES leggen
scripts/build-hasphook.sh --game <Naam>    # alleen die game
scripts/build-hasphook.sh --no-place       # alleen bouwen
scripts/build-hasphook.sh --check          # zijn de geplaatste kopieën actueel?
scripts/build-hasphook.sh --selftest       # ook de zelftest draaien
```

---

## 4. Het bewijs

### 4.1 De zelftest (herhaalbaar, geen game nodig)

`tools/rt-hasphook/hasp_zelftest.c` is een 32-bits ELF die zijn eigen `hasp_*`-functies **statisch in
het beeld** heeft — precies de situatie van `game` van Jurassic Park, Galaga Assault en Halo
Fireteam. Hij roept ze aan via een adres dat hij zelf opzoekt, zodat symboolinterpositie geen rol
speelt. De "echte" implementaties doen alsof ze met hardware praten: ze schrijven `ECHT: ...` en
geven `HASP_STATUS_NO_HARDWARE` terug.

```bash
cd ~/projects/ravengun
gcc -m32 -I hasp/src/inc -O1 -rdynamic -o /tmp/zelftest tools/rt-hasphook/hasp_zelftest.c
gcc -m32 -I hasp/src/inc -shared -fPIC -O2 -o /tmp/hook.so tools/rt-hasphook/hasp_inline_hook.c -ldl -lpthread
```

**Zonder de koppeling** (de statisch gelinkte cliënt antwoordt):

```
ECHT: hasp_login(feature=0) -> HASP_STATUS_NO_HARDWARE
hasp_login -> 3, greep = (nil)
ECHT: hasp_get_size(fileid=65524)
hasp_get_size(65524) -> 3, grootte = 0
ECHT: hasp_read(fileid=65524, offset=0, lengte=16)
echte_aanroepen = 4
RESULTAAT: de ECHTE cliënt is aangeroepen (4 keer) — de hook werkt NIET
```

**Met de koppeling en de echte dongle van Halo Fireteam:**

```
LD_PRELOAD=/tmp/hook.so RAVENGUN_HASP_VERBOSE=1 RAVENGUN_HASP_GAME=Halo \
RAVENGUN_HASP_DIR=~/projects/ravengun/patches ./hasp_zelftest
```

```
[ravengun-hasp] dongle 'Halo Fireteam' geladen uit .../patches/Halo (2 geheugenbestand(en), haspid 748675309)
[ravengun-hasp] hasp_login (vendor_code=0)
[ravengun-hasp] hasp_get_size (fileid=65524)
[ravengun-hasp] hasp_read (fileid=65524 offset=0 lengte=16)
[ravengun-hasp] hasp_get_info
hasp_login -> 0, greep = 0xe6f
hasp_get_size(65524) -> 0, grootte = 112
hasp_read(65524, 0, 16) -> 0, eerste bytes: 2c 03 ab 63
hasp_get_info -> 0, info = <?xml version="1.0" ... <hasp id="748675309" type="HASP-HL"> ...
echte_aanroepen = 0
RESULTAAT: alle aanroepen zijn door de EMULATOR beantwoord; de statisch gelinkte cliënt is NIET aangeroepen
```

Dat is de hele keten in één meting: **statisch gelinkte aanroep → inline-hook → onze emulator →
dongle-image**, en de cliënt van de fabrikant wordt nul keer aangeroepen. De greep `0xe6f`, de
grootte 112 en de eerste bytes `2c 03 ab 63` zijn de waarden uit `patches/Halo/dongle.json` en
`patches/Halo/dongle/memory/00000000000000001111111111110100`.

`scripts/build-hasphook.sh --selftest` draait dit als onderdeel van de bouw.

### 4.2 Het kanaal naar het spelproces (gemeten)

De koppeling moet in het ELF-proces van de game komen. Gemeten met een meetbibliotheek:

| kanaal | uitkomst |
|---|---|
| `LD_PRELOAD` van het Wine-proces | **sterft**: BudgieLoader is Cygwin en leest `LD_PRELOAD` zelf |
| `LD_AUDIT` van het Wine-proces | **werkt**: de constructor draait in het 32-bits `wine-preloader`-proces |
| `ElfLdr2/libs/` op het zoekpad | de map staat op het zoekpad van de game (zie `docs/research/elfldr2-injectie.md`) |

De constructor van de koppeling draait in het 32-bits proces `wine-preloader`, en het spelbeeld staat
daar al in het geheugen:

```
08048000-09128000 rwxp 00000000 00:00 0        <- Jurassic Park, 17 MB, vanaf het begin van de start
```

De koppeling vindt de game-ELF, leest zijn dynsym (18811 symbolen, 26 met `hasp_`) en vindt het beeld
op 0x08048000. Dat is precies wat de GL-fix-agent in zijn logboek zag
(`spelbeeld gevonden op 0x08048000 (11091968 bytes)`).

### 4.3 De live-game-test: wat wel en niet kon

**Wat wel lukte**: de koppeling komt in het proces, vindt het spelbeeld en haakt de functies aan.
Uit het logboek van een echte start van Jurassic Park:

```
[ravengun-hasphook] spel-ELF gelezen uit .../views/JurassicPark/game (cmdline Z:-pad): 17694720 bytes
[ravengun-hasphook] aantal symbolen uit DT_HASH: 18811
[ravengun-hasphook] 26 hasp-symbolen in de dynsym van de game (gedefinieerd in de game zelf)
[ravengun-hasphook] spelbeeld op 0x08048000
```

**Wat niet lukte**: een `hasp_login` van een echte game opvangen. De reden is niet de koppeling maar
het spel: Jurassic Park **start niet** in deze omgeving. Het spel komt tot `Starting Game` en sterft
dan aan zijn eigen begin — de bekende OpenGL-blokkade (`GL_MAX_TEXTURE_SIZE > 2047`, zie
`patches/JurassicPark/game.ini`, `[Status] Note`). Het bereikt zijn HASP-controle nooit, dus er valt
niets op te vangen. Dat is met en zonder koppeling hetzelfde:

```
zonder LD_AUDIT:  3 van 3 keer  -> "Starting Game", exit 255
met de koppeling: 3 van 3 keer  -> "Starting Game", exit 255
```

Er is nog een tweede effect gemeten dat het project moet weten: een `LD_AUDIT`-bibliotheek met een
constructor die **veel** doet (de meetprobe las het spelbeeld en liep de GOT na) laat het
Wine-proces sterven met een `SIGSEGV` in `wine-preloader`. De koppeling zelf is voorzichtiger
geworden (signaal-veilige lezing van het beeld, aanhaken in de constructor, `RTLD_LOCAL`) en laat
het spel gewoon doorlopen; maar het blijft een omgeving waarin elke toevoeging aan het proces de
lader kan laten vallen. Zie §6.

---

## 5. Terminator Salvation: geen HASP-cliënt in het spel

Terminator Salvation is de uitzondering. `game` is 32-bits, 7,7 MB, en heeft:

* **0** ongedefinieerde hasp-symbolen;
* **0** verplaatsingen met hasp;
* **0** gedefinieerde hasp-symbolen in de dynsym (1019 dynamische symbolen, geen enkele met `hasp`);
* geen versiegebonden `@@HASPE`-export;
* `strings` geeft **0** treffers voor `hasp_login`, en alleen `ReInitAladdinDongles called: %d times`
  en `hasp_vtc_d` als losse sporen.

Er zit dus **geen** HASP-cliënt in dit spel. Wat er wel is: een `hasp/`-map in de spelmap met
honderden bestanden van 32 bytes (hashes) — dat zijn gegevens, geen code. Terminator heeft
kennelijk een andere (of geen) dongle-route, of de cliënt is er bij het packen uitgehaald.

**Gevolg voor het project**: voor Terminator valt er aan de ELF-kant niets aan te haken. Dat is een
aparte bevinding, geen fout van de koppeling.

---

## 6. Wat nog open is

1. **Een live-game-bewijs.** Dat kon niet, omdat Jurassic Park in deze omgeving niet voorbij zijn
   OpenGL-start komt. Zodra dat wel lukt (de GL-fix van `tools/rt-glfix`), is de verwachting dat
   `RAVENGUN_HASP_VERBOSE=1` de `hasp_login`-regel laat zien. De zelftest bewijst de keten al; de
   live-test zou hem in de echte game bevestigen.
2. **Halo Fireteam (64-bits).** Daar staat de cliënt ook statisch in, maar de `hasp_*`-namen zijn
   **niet** geëxporteerd; alleen `patchInit`, `patchDtInitHook` en `closeThread`. De koppeling vindt
   daar dus geen aanknopingspunten. Nodig is ofwel de hardgecodeerde adressen uit TeknoParrot's
   `halo.so` (0xb2d630 enzovoort, gemeten op de 32 MB grote `game`), ofwel een patroonzoeker op de
   code. Dat is de eerstvolgende stap voor Halo.
3. **Terminator.** Geen cliënt in het spel (§5). Uitzoeken hoe die game zijn dongle dan wel
   controleert, is een aparte vraag.
4. **De dongle-images van Jurassic Park en Galaga Assault.** De koppeling werkt, maar zonder
   `patches/<Game>/dongle.json` antwoordt de emulator met `HASP_STATUS_INVALID_PARAMETER` (503). De
   images staan in de spelmap (`Jurassic Park/utils/pmrtdata/`, en de hasp-map van Terminator), maar
   de fileids en de beschrijving moeten nog worden vastgelegd.
5. **Het kanaal netjes maken.** `LD_AUDIT` werkt, maar het is geen afgesproken route: de launcher zet
   het pad als `RG_DROPIN_PRELOAD` en niemand leest dat. De afspraak hoort te zijn: de launcher zet
   `RG_DROPIN_PRELOAD` in `LD_AUDIT` van het Wine-proces (zoals `patches/JurassicPark/dropin/dropin.ini`
   het al beschrijft voor `librt-glfix.so`), en de drop-in van deze koppeling volgt dezelfde weg.
6. **De stabiliteit van het Wine-proces.** Een `LD_AUDIT`-bibliotheek die veel doet, kan de
   Wine-preloader laten vallen (§4.3). Wie hier meer in het proces wil zetten, moet dat in kleine
   stappen doen en elke stap meten.

---

## 7. Herhalen

```bash
cd ~/projects/ravengun

# 1. bouwen (en de zelftest)
scripts/build-hasphook.sh --no-place --selftest

# 2. de drop-in plaatsen
scripts/build-hasphook.sh --game JurassicPark

# 3. de zelftest met de echte dongle van Halo Fireteam
cd ~/.local/share/ravengun/runtime/hf-proef
gcc -m32 -I ~/projects/ravengun/hasp/src/inc -O1 -rdynamic \
  -o hasp_zelftest ~/projects/ravengun/tools/rt-hasphook/hasp_zelftest.c
gcc -m32 -I ~/projects/ravengun/hasp/src/inc -shared -fPIC -O2 -Wno-format-truncation \
  -o libravengun-hasphook.so ~/projects/ravengun/tools/rt-hasphook/hasp_inline_hook.c -ldl -lpthread
LD_PRELOAD=$PWD/libravengun-hasphook.so RAVENGUN_HASP_VERBOSE=1 RAVENGUN_HASP_GAME=Halo \
RAVENGUN_HASP_DIR=~/projects/ravengun/patches ./hasp_zelftest

# 4. de statische meting narekenen
python3 tools/rt-hasphook/dynsym.py --hasp "…/Jurassic Park/game"

# 5. de tests
python3 -I tests/test_hasp_elf_hook.py
```

De tests staan in `tests/test_hasp_elf_hook.py` (12 tests: de bestanden, het dynsym-hulpmiddel op de
echte game-bestanden, de bouw, en de zelftest met de echte dongle).
