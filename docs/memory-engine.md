# De geheugen-motor

*`memory/src/` + `scripts/build-memory.sh`, ronde 6. Dit is het TWEEDE deel van de patchmotor. Het
eerste deel (`scripts/patchfile.py` + `scripts/addrmap.py`) schrijft in een **bestand**; zie
`docs/patchmotor.md`. Dit deel schrijft in het **geheugen van een lopend proces**, en wel om de
gevallen waarin een bestandspatch niet kan. Zie `docs/community-model.md` paragraaf 3 voor het
waarom en `docs/dropins.md` voor de mapindeling.*

## 1. Waarvoor

`docs/community-model.md` paragraaf 3 noemt vier dingen die een bestandspatch niet dekt. Dit is wat
de geheugen-motor ervan doet — met het bewijs erbij, niet als belofte:

| Geval | Kan de geheugen-motor het? | Bewijs |
|---|---|---|
| Een patch in een **DLL die pas later geladen wordt** | **ja** | §6 proef 3: `libravengun-proeflaat.so` wordt na `dlopen` gepatcht, zowel meteen (onderschepping) als door de achtergrondthread |
| Een **gepakte** exe (Pigs Might Fly, Luigi's Mansion) | **ja, in het geheugen** — met twee echte grenzen | §6 proef 4: een nagebootste pakker; een patch op de bestandsbytes wordt geweigerd, een patch op de geheugenbytes lukt |
| Iets dat pas **tijdens het draaien** aangepast moet worden | **ja, met de hand** | de motor past de lijst toe bij de start en blijft kijken naar modules; hij heeft geen *trigger* ("pas aan zodra X gebeurt") — zie §8 |
| Aanpassingen die geen bytes zijn maar **gedrag** | **nee** | een nieuwe API onderscheppen of een nieuw nagebootst apparaat is engine-werk, geen patch |

De motor is **één component voor elke game**: hij leest een lijst, verder weet hij niets van games.

## 2. Het formaat van de patchlijst

Een tekstbestand, één regel per patch, velden met `|` gescheiden:

```
# module|offset|verwacht|nieuw
game.exe|0x113E80||90909090
UnityPlayer.dll|0x1234|48895c24|deadbeef
```

| Veld | Betekenis |
|---|---|
| `module` | de **bestandsnaam** van de geladen module, **case-ongevoelig**. `game.exe`, `UnityPlayer.dll`, `libravengun-proeflaat.so`. Een pad mag ook: alleen het laatste stuk telt. |
| `offset` | een **RVA**: het adres ten opzichte van de **basis** van die module. Decimaal of `0x…`. |
| `verwacht` | hex-bytes die er **moeten** staan. **Leeg = niet controleren.** Klopt het niet, dan wordt er **niets** geschreven en komt er een melding. |
| `nieuw` | de hex-bytes die er moeten komen. Mag niet leeg zijn. |

Regels die met `#` beginnen en lege regels worden overgeslagen.

Over de bytes:

* hex mag aan elkaar (`48895c24`), met spaties (`48 89 5c 24`), met `0x` (`0x48,0x89`) of met
  dubbele punten (`48:89`); hoofdletters mogen;
* `offset` met een leidende nul is **decimaal**, niet octaal: `010` is tien, niet acht;
* **`verwacht` en `nieuw` mogen even lang zijn, maar het hoeft niet.** `verwacht` is alleen een
  controle; er wordt pas geschreven als die klopt. `game.exe|0x1000|48895c24|90` controleert dus
  vier bytes en vervangt er één.

Wat de motor met een **verkeerde regel** doet: hij meldt hem, telt hem mee als vormfout, en gaat
door met de rest. Een lijst met één fout kost je nooit de andere patches.

## 3. Omgevingsvariabelen

| Variabele | Nodig? | Wat hij doet |
|---|---|---|
| `RAVENGUN_PATCHMEM_FILE` | **ja** | het pad van de patchlijst. **Zonder deze variabele gebeurt er niets.** Dat is met opzet: een drop-in die in elke game geladen wordt mag niet uit zichzelf gaan schrijven. |
| `RAVENGUN_PATCHMEM_VERBOSE` | nee | gezet: **één regel per patch op stderr**, met module, offset en voor/na; plus de lijst van geladen modules |
| `RAVENGUN_PATCHMEM_WACHT` | nee | seconden dat de achtergrondthread blijft kijken (standaard **30**; `0` = geen thread) |
| `RAVENGUN_PATCHMEM_POLL` | nee | milliseconden tussen twee rondes (standaard **250**, minimaal 20) |
| `RAVENGUN_PATCHMEM_GEEN_DLOPEN` | nee | gezet: `dlopen` **niet** onderscheppen. Alleen voor de proef: dan is te zien dat de achtergrondthread het werk doet. |

Voorbeeld van de verbose uitvoer (dit is echte uitvoer, zie §6):

```
patchmem: toegepast   probe_linux+0x4080  [112233445566778899aabbccddeeff00 -> deadbeefdeadbeefdeadbeefdeadbeef]  op probe_linux+0x4080
patchmem: /pad/lijst.txt: 1 patch(es) gelezen -- 1 toegepast, 0 stond er al, 0 wacht op een module, 0 geweigerd (verwacht klopte niet), 0 mislukt
```

En de melding die je nooit mag missen — `verwacht` klopte niet, dus er is **niets** geschreven:

```
patchmem: GEWEIGERD   probe_linux+0x4080  [112233445566778899aabbccddeeff00 -> ffffffffffffffffffffffffffffffff]  de bytes op probe_linux+0x4080 zijn 11223344, verwacht zegt 99999999 (verkeerde spelversie?); er is NIETS geschreven
```

## 4. Hoe het werkt

### De kern (`memory/src/patchmem.c`)

Platform-onafhankelijk. Leest de lijst, zoekt per module de basis op, controleert `verwacht`,
schrijft `nieuw`, houdt een verslag bij. De volgorde per patch is streng:

1. **is de module geladen?** Zo nee → `wacht`, en de patch blijft op de lijst voor de volgende ronde;
2. **klopt `verwacht`?** Zo nee → `GEWEIGERD`, **niets** geschreven, en deze patch wordt niet meer
   geprobeerd;
3. **staan de bytes er al?** Zo ja → `stond er al`, niets geschreven (zo blijft een tweede ronde van
   de achtergrondthread onschadelijk);
4. **schrijven**, en daarna **teruglezen** om te controleren dat het echt gelukt is.

### De twee haken

Het opzoeken van modules is het enige stuk dat per platform verschilt, en het gaat via precies twee
haken:

```c
void *patchmem_module_base(const char *naam);              /* de snelle weg */
int   patchmem_foreach_module(int (*cb)(const char *, void *, void *), void *user);
```

`patchmem_module_base` is de snelle weg, maar hij kan "gevonden met basis 0" niet van "niet
gevonden" onderscheiden — en dat is precies wat een **niet-PIE** programma op Linux oplevert. Daarom
is `patchmem_foreach_module` de **doorslaggevende** hulp: die meldt apart of de module gevonden is.
De motor gebruikt eerst de snelle weg en valt anders terug op de volledige lijst. Proef 5 in §6
bewijst dat dat nodig was: zonder die terugval zou een niet-PIE programma nooit gepatcht worden.

### De Linux-kant (`memory/src/entry_linux.c`)

* een `__attribute__((constructor))` past de lijst toe **voordat `main` begint**;
* een **achtergrondthread** (`pthread`, losgekoppeld) kijkt elke `RAVENGUN_PATCHMEM_POLL` ms of een
  module uit de lijst inmiddels geladen is;
* **`dlopen` wordt onderschept**, zodat een net geladen module *onmiddellijk* gepatcht wordt in
  plaats van bij de volgende ronde. Dat is nodig als de patch klaar moet zijn voordat de
  constructors van die module iets doen;
* modulebasis via **`dl_iterate_phdr`**; voor de hoofdmodule, waar `dlpi_name` leeg is, via
  `/proc/self/exe`.

### De Windows-kant (`memory/src/entry_win.c`)

* `DllMain` (`DLL_PROCESS_ATTACH`) doet **niets anders dan een draad starten**;
* die draad past de lijst toe en blijft kijken. Modulebasis via
  **`CreateToolhelp32Snapshot(TH32CS_SNAPMODULE)`**;
* alleen **kernel32**: geen Detours, geen injectie, geen extra afhankelijkheden.

**Waarom niet in `DllMain` zelf?** Tijdens `DLL_PROCESS_ATTACH` houdt de thread de loader-lock vast,
en `CreateToolhelp32Snapshot` vraagt diezelfde lock. Werk in `DllMain` doen zou onszelf daar laten
wachten. `DllMain` wacht nooit op de draad, dus er is geen impasse. **De prijs is wel echt**: op
Windows is de eerste ronde een paar milliseconden **na** de start van het programma in plaats van
ervoor. Gemeten: **2 ms** (§6 proef 6). Zie §8.

### Schrijven: de rechten

Schrijven in code vraagt dat het geheugen even schrijfbaar wordt. Dat gebeurt zo precies mogelijk:

* **Linux**: de rechten van het gebied worden opgezocht in `/proc/self/maps`, er wordt alleen
  `PROT_WRITE` bijgezet, en daarna worden **de oorspronkelijke rechten exact teruggezet**. Zonder dat
  zou een `.data`-gebied (rw-) na het patchen als r-x achterblijven, en dat breekt de game.
* **Windows**: `VirtualProtect` met `PAGE_EXECUTE_READWRITE`, en daarna terug naar de oude waarde die
  `VirtualProtect` zelf teruggeeft; plus `FlushInstructionCache`.

### Schrijven: eerst kijken of er wel geheugen ligt

Vóór er iets gelezen of geschreven wordt, controleert de motor dat het hele bereik
`[basis+offset, basis+offset+lengte)` in **toegewezen, leesbaar** geheugen ligt — op Linux via
`/proc/self/maps`, op Windows via `VirtualQuery`. Klopt de module of de offset niet, dan volgt
`MISLUKT` met een melding en wordt er **niets** gelezen of geschreven.

Dat is geen luxe: zonder deze controle zou één verkeerde offset in de lijst het spel laten omvallen,
en een patchmotor die de game laat crashen is erger dan een patchmotor die weigert. De proeven
`test_offset_buiten_de_module_valt_niet_om` en `test_offset_net_over_het_einde_van_het_bestand`
controleren dat het proefprogramma overeind blijft en de weigering meldt.

## 5. Bouwen

```bash
scripts/build-memory.sh                # alles bouwen, installeren en zelftesten
scripts/build-memory.sh --no-windows   # alleen de Linux-kant (geen podman nodig)
scripts/build-memory.sh --no-install   # alleen in memory/out/ bouwen
scripts/build-memory.sh --no-test      # niet zelftesten
```

Resultaat, in `${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/memory/`:

| Bestand | Voor |
|---|---|
| `ravengun-patchmem.so` | 64-bits ELF-games, via `LD_PRELOAD` |
| `ravengun-patchmem32.so` | 32-bits ELF-games, via `LD_PRELOAD` |
| `ravengun-patchmem.dll` | x86_64-games onder Proton/Wine |
| `ravengun-patchmem32.dll` | i686-games onder Proton/Wine |

De PE-DLL's worden gebouwd met mingw-w64 in de rootless podman-image `localhost/ravengun-mingw`
(dezelfde als `scripts/build-hasp.sh` en `scripts/build-matrix.sh`). De proefprogramma's komen in
`memory/out/` en worden **niet** geïnstalleerd.

Er wordt nooit iets buiten de bibliotheekmap en `memory/out/` gezet. De motor schrijft **geen enkel
bestand** — dat is niet een belofte maar een test (§6 proef 7).

## 6. Het bewijs

Alles hieronder is op de Deck gemeten. De geautomatiseerde versie staat in
`tests/test_memory_engine.py` (**38 proeven**); dezelfde proeven met de hand staan in
`re/proef_laat.sh`, `re/proef_wine.sh`, `re/proef_extra.sh` en `re/proef_appinit.sh`.

> De **offsets** in de uitvoer hieronder hangen af van de build van het proefprogramma: verandert
> `memory/test/probe_linux.c`, dan verschuift `probe_linux+0x…`. Wat telt is de **vorm** van de
> uitvoer en het **resultaat**; de tests controleren dat, niet de getallen.

```bash
python3 tests/test_memory_engine.py        # 38 proeven, ~55 s (inclusief Wine)
```

### Proef 1 — een patch in het programma zelf (Linux, 64-bits)

```bash
bash scripts/build-memory.sh
```

```
zonder de motor: probe_linux+0x4090 = 112233445566778899aabbccddeeff00
patchmem: toegepast   probe_linux+0x4090  [112233445566778899aabbccddeeff00 -> deadbeefdeadbeefdeadbeefdeadbeef]  op probe_linux+0x4090
patchmem: .../zelftest.txt: 1 patch(es) gelezen -- 1 toegepast, 0 stond er al, 0 wacht op een module, 0 geweigerd (verwacht klopte niet), 0 mislukt
  OK: de bytes in het lopende proces zijn veranderd
```

En een patch op **code** in plaats van data: `probe_linux` op offset `0x1440` had de bytes
`31c0c3669066` (`xor eax,eax; ret`). Na de patch met `b801000000c3` (`mov eax,1; ret`) geeft de
functie **1** terug in plaats van 0. Dat is dezelfde woordenschat als `Action = jmp-returns-true` uit
`docs/patchmotor.md`, nu in een lopend proces in plaats van in een bestand.

### Proef 2 — `verwacht` klopt niet

```
patchmem: GEWEIGERD   probe_linux+0x4090  [112233445566778899aabbccddeeff00 -> ffffffffffffffffffffffffffffffff]  de bytes op probe_linux+0x4090 zijn 11223344, verwacht zegt 99999999 (verkeerde spelversie?); er is NIETS geschreven
  OK: een verwacht dat niet klopt laat de bytes met rust
```

De motor meldt **wat er stond** en **wat de lijst zei**, en raakt de bytes niet aan.

### Proef 3 — een bibliotheek die pas later geladen wordt

`memory/test/probe_linux.c` doet na de start `dlopen("libravengun-proeflaat.so")`. De module is bij
de start niet geladen, dus de constructor laat de patch `wachten`:

```
patchmem: wacht       libravengun-proeflaat.so+0x4010  [(leeg) -> deadbeefdeadbeefdeadbeefdeadbeef]  module libravengun-proeflaat.so is niet geladen; de patch wacht
patchmem: /tmp/proef-laat.txt: 1 patch(es) gelezen -- 0 toegepast, 0 stond er al, 1 wacht op een module, 0 geweigerd (verwacht klopte niet), 0 mislukt
```

Daarna, met de dlopen-onderschepping aan:

```
patchmem: toegepast   libravengun-proeflaat.so+0x4010  [aabbccddeeff0102030405060708090a -> deadbeefdeadbeefdeadbeefdeadbeef]  op libravengun-proeflaat.so+0x4010
laat_direct=deadbeefdeadbeefdeadbeefdeadbeef      <- meteen na dlopen al gepatcht
laat_na=deadbeefdeadbeefdeadbeefdeadbeef
```

En met `RAVENGUN_PATCHMEM_GEEN_DLOPEN=1`, zodat **alleen de achtergrondthread** het kan doen:

```
laat_direct=aabbccddeeff0102030405060708090a      <- meteen na dlopen nog niets
patchmem: toegepast   libravengun-proeflaat.so+0x4010  [aabbccddeeff0102030405060708090a -> deadbeefdeadbeefdeadbeefdeadbeef]  op libravengun-proeflaat.so+0x4010
patchmem: klaar -- 1 toegepast, 0 stond er al, 0 geweigerd, 0 mislukt
laat_na=deadbeefdeadbeefdeadbeefdeadbeef          <- de thread heeft het gedaan
```

Twee losse mechanismen, twee losse bewijzen. Dat is wat een **pas later geladen DLL** dekt.

### Proef 4 — een GEPAKTE module

Een echte pakker zit in de game; het wezenlijke verschil is klein en wel na te bootsen.
`memory/test/packed_module.c` heeft de bytes **versleuteld in het bestand** en pakt ze bij het laden
uit in een constructor:

```
bestand:  f0 e1 96 87 b4 a5 5b 58 59 5e 5f 5c 5d 52 53 50   (XOR 0x5a)
geheugen: aa bb cc dd ee ff 01 02 03 04 05 06 07 08 09 0a
```

De proeven (`TestGepakteModule`) controleren alle vier de kanten:

| Proef | Uitkomst |
|---|---|
| de versleutelde bytes staan in het bestand | **ja** |
| de uitgepakte bytes staan **niet** in het bestand | **ja** |
| een patch met `verwacht` = de **geheugen**bytes | **toegepast** |
| een patch met `verwacht` = de **bestands**bytes | **GEWEIGERD**, niets geschreven |

Dat is precies het verschil: de bestandspatch-motor (`docs/patchmotor.md`) kan een gepakte exe niet
aan, deze motor wel — hij leest en schrijft het geheugenbeeld, niet het bestand. **De twee echte
grenzen staan in §8.**

### Proef 5 — een niet-PIE programma (modulebasis 0)

`probe_linux_nopie` (gebouwd met `-no-pie`) meldt `doel_offset=0x404090` en
`functie_offset=0x401440` — dat zijn absolute adressen, want de basis is 0. Beide patches worden
toegepast, en de functie geeft daarna 1 terug. Zonder de terugval via `patchmem_foreach_module` zou
dit mislukken: `patchmem_module_base` geeft namelijk óók NULL als de module gevonden is met basis 0.

### Proef 6 — de PE-DLL onder Wine

`memory/test/probe_win.c` **importeert** `ravengun-patchmem.dll`. Dat is nodig, want
`WINEDLLOVERRIDES=<naam>=n` doet alleen iets voor een module die het programma werkelijk importeert.
Zo laden we de DLL op dezelfde manier als een game dat zou doen.

```bash
WINEDLLOVERRIDES="ravengun-patchmem=n" RAVENGUN_PATCHMEM_FILE='Z:\...\lijst.txt' wine probe_win.exe
```

x86_64 (`proton-11.0-2`):

```
patchmem: toegepast   probe_win.exe+0x4020  [112233445566778899aabbccddeeff00 -> deadbeefdeadbeefdeadbeefdeadbeef]  op probe_win.exe+0x4020
patchmem: toegepast   probe_win.exe+0x1580  [31c0c3669066 -> b801000000c3]  op probe_win.exe+0x1580
patchmem: wacht       ravengun-laat.dll+0x3020  [(leeg) -> deadbeefdeadbeefdeadbeefdeadbeef]  module ravengun-laat.dll is niet geladen; de patch wacht
patchmem: toegepast   ravengun-laat.dll+0x3020  [aabbccddeeff0102030405060708090a -> deadbeefdeadbeefdeadbeefdeadbeef]  op ravengun-laat.dll+0x3020
patchmem: klaar -- 3 toegepast, 0 stond er al, 0 geweigerd, 0 mislukt
```

```
doel_na_ms=2          <- de eerste ronde van de draad, 2 ms na de start van het programma
doel_na=deadbeefdeadbeefdeadbeefdeadbeef
functie_na=1          <- mov eax,1 ; ret
laat_na_ms=250        <- de pas later geladen DLL, één poll-ronde later (250 ms is de standaard)
laat_na=deadbeefdeadbeefdeadbeefdeadbeef
```

i686 (`probe_win32.exe`, `ravengun-patchmem32.dll`, `ravengun-laat32.dll`): dezelfde uitkomsten,
`doel_na_ms=2`, `functie_na=1`, `laat_na_ms=251`. De 32-bits kant is dus niet alleen gebouwd maar
ook gedraaid.

### Proef 7 — het bestand blijft onaangeroerd

`TestLinuxMotor.test_het_bestand_blijft_onaangeroerd` neemt de sha256 van `probe_linux`, patcht het
lopende proces, en controleert dat de sha256 **gelijk** is gebleven. De motor schrijft dus geen
enkel bestand. Dat is ook waarom "nooit naar de NAS schrijven" hier geen aparte regel nodig heeft:
er is geen bestandspad in het spel.

### Proef 8 — de 32-bits Linux-bibliotheek

`probe_linux32` (een echt 32-bits ELF-bestand) met `LD_PRELOAD=ravengun-patchmem32.so`: data patch,
functiepatch, `verwacht`-weigering en de later geladen bibliotheek — alle vier goed.

## 7. In `patches/<Game>/dropin/`

De motor is **niet game-specifiek**: dezelfde vier bibliotheken bedienen elke game. Wat een game
nodig heeft staat in de map van die game. Het contract staat in **`docs/dropins.md`** — hier staat
alleen hoe deze motor erin past.

In `patches/<Game>/dropin/` komen het bestand en de lijst:

```
patches/<Game>/dropin/
  ravengun-patchmem.so        (of .dll, of de 32-bits variant)
  patchmem.txt                de patchlijst van deze game
  dropin.ini                  één sectie die het bestand en de variabele aan elkaar knoopt
```

Voor een **Linux-game** (ELF, via `LD_PRELOAD`):

```ini
[ravengun-patchmem.so]
To = .
Preload = true
Env = RAVENGUN_PATCHMEM_FILE=<pad naar patchmem.txt in de view van de game>
```

Voor een **Windows-game onder Proton**:

```ini
[ravengun-patchmem.dll]
To = .
DllOverride = native
Env = RAVENGUN_PATCHMEM_FILE=<pad naar patchmem.txt in de view van de game>
```

`To = .` zet het bestand in de hoofdmap van de view; de launcher leest `dropin.ini` en doet de rest.
De sleutels `To`, `DllOverride`, `Env` en `Preload` staan beschreven in `docs/dropins.md`; dat
contract is de baas, niet dit document.

**Eén ding om te weten bij het pad.** `RAVENGUN_PATCHMEM_FILE` wordt door het **spelproces** gelezen,
niet door de launcher. Onder Wine is een `/home/deck/...`-pad daarom onbruikbaar: het moet
`Z:\home\deck\...` zijn. Zet de lijst naast de DLL (`To = .`) en gebruik een pad dat het spelproces
begrijpt; de launcher weet de view en kan het invullen.

**En één ding over het laden onder Windows — dit is belangrijk.** `DllOverride = native` zet
`WINEDLLOVERRIDES=<naam>=n`, en dat werkt **alleen voor een module die de game ook werkelijk
importeert**. Een game-exe importeert `ravengun-patchmem.dll` niet. Gemeten (§8): `AppInit_DLLs` in
het Wine-prefix werkt hier **niet**. Wat overblijft is de weg die de andere drop-ins al gebruiken:
**de DLL onder de naam van een DLL zetten die de game wél importeert** (een proxy-DLL, precies zoals
`ravengun-hasp.dll` en `matrix64.dll` dat doen). Dat is geen tekortkoming van deze motor; het is hoe
het laden van een DLL in een bestaand spelproces werkt.

## 8. Wat niet werkt, en waar het op stukloopt

Eerlijk, met het bewijs erbij waar we het gemeten hebben.

**1. `WINEDLLOVERRIDES` alleen is niet genoeg, en `AppInit_DLLs` werkt hier niet.**
`WINEDLLOVERRIDES=<naam>=n` laadt alleen een native DLL voor een module die het programma importeert
(§6 proef 6 laat dat zien: de proef importeert de DLL). Voor een echte game is er dus een laadweg
nodig. Gemeten met `re/proef_appinit.sh`, met een proefprogramma dat de DLL **niet** importeert:

```
=== 2. dezelfde proef, nu met RAVENGUN_PATCHMEM_FILE ===
    motor=(niet geimporteerd)
    doel_na=112233445566778899aabbccddeeff00
    doel_na_ms=geen
  LET OP: AppInit_DLLs werkt hier niet (doel_na=...)
```

Het prefix stond goed (`reg query` toont `AppInit_DLLs = Z:\...\ravengun-patchmem.dll` en
`LoadAppInit_DLLs = 0x1`), en de DLL werd niet geladen. **Proton 11.0-2 laadt `AppInit_DLLs` dus
niet.** Dezelfde proef met de tweede Wine op de Deck (`wine/11.19`) is **niet afgerond**: het
aanmaken van een vers prefix daar liep vast en is afgebroken, dus daar zeggen we niets over. De
uitweg is de proxy-DLL uit §7 — dezelfde weg die de HASP- en Matrix-drop-ins al nemen.
Wat we **niet** hebben getest: een echte game-exe met een echte proxy-DLL. Dat vraagt een game van
de NAS en dat is werk voor de launcher, niet voor deze motor.

**2. Een gepakte exe: wat wel en niet.**
* **Wel**: patchen in het geheugen, ook als de bytes in het bestand anders zijn (§6 proef 4).
* **Niet, grens A — de RVA in de lijst is de RUNTIME-RVA.** Bij een gepakte exe is de pakker de
  eerste die draait; de code komt op een andere plaats dan in het bestand. Wie de lijst maakt moet de
  RVA in het **geheugen** nemen, niet de bestands-RVA. Onze nagebootste pakker laat precies dat zien:
  met de bestandsbytes als `verwacht` wordt de patch **geweigerd**. Dat is de goede afloop (er wordt
  niets kapot geschreven), maar het betekent wel dat een verkeerd gemaakte lijst stil niets doet —
  daarom meldt de motor het hardop.
* **Niet, grens B — een pakker die pas láát uitpakt.** Pakt de pakker de code pas uit op het moment
  dat die voor het eerst nodig is, dan schrijven wij in nog-niet-uitgepakte bytes. Onze `verwacht`
  vangt dat af zodra die bytes afwijken, maar als `verwacht` leeg is schrijven we in het duister. En
  pakt de pakker **na** ons opnieuw uit, dan overschrijft hij onze patch. **Dit hebben we niet
  gemeten**: onze nagebootste pakker pakt in een constructor uit, dus vóór ons. Een echte pakker
  (Pigs Might Fly, Luigi's Mansion) staat op de NAS en is niet getest.
* **Niet, grens C — integriteitscontrole.** Een pakker die zijn eigen bytes nakijkt, ziet onze patch.
  Daar is niets tegen te doen met een bytepatch; dat vraagt de pakker zelf aanpakken.

**3. Code patchen die een andere thread op dat moment uitvoert.** De motor schrijft in code die een
andere thread kan uitvoeren. Op x86 is dat in de praktijk goed te doen (de syscall van `mprotect`
respectievelijk `VirtualProtect` werkt als serialisatiepunt, en we lezen na het schrijven terug),
maar het is **niet gegarandeerd**: er is geen moment waarop we weten dat niemand op dat adres staat.
Voor een data-patch maakt het niets uit; voor een code-patch is het een echt risico. Een volledig
veilige motor zou de threads moeten stilzetten — dat is injectiewerk en valt buiten dit stuk.

**4. De eerste ronde op Windows is ná de start, niet ervoor.** Gemeten: **2 ms** (§6 proef 6). Dat
komt door de loader-lock (§4). Voor een game die seconden nodig heeft om bij de gepatchte code te
komen is dat niets; voor een patch die op de **eerste instructie** van de exe moet zitten is het te
laat. De Linux-kant heeft dat probleem niet: daar draait de constructor vóór `main`.

**5. De dlopen-onderschepping komt ná de constructors van de geladen module.** `dlopen` is klaar
met laden als wij eraan te pas komen, dus de constructors van die module hebben dan al gedraaid. Een
patch die *tijdens* die constructors nodig is, is te laat. Iets eerder kan alleen door `dlopen` zelf
af te vangen vóór het laden — en dan is de module nog niet in het geheugen, dus er valt niets te
patchen. Dit is een echte grens, geen implementatiedetail.

**6. `dlmopen` wordt niet onderschept.** Alleen `dlopen`. Een module die met `dlmopen` in een eigen
naamruimte geladen wordt, wordt door de achtergrondthread opgepikt (`dl_iterate_phdr` ziet hem) maar
niet onmiddellijk. In de praktijk gebruikt vrijwel niets `dlmopen`.

**7. Er is geen trigger.** De motor past de lijst toe bij de start en blijft kijken naar **modules**.
Hij heeft geen voorwaarde als "pas aan zodra de speler het level in gaat" of "pas aan als deze
functie voor het eerst aangeroepen wordt". Alles wat van de uitvoeringstoestand afhangt vraagt een
haak in de code zelf — en dat is de volgende `Action` in de woordenschat, niet iets wat deze lijst
kan uitdrukken.

**8. Geen ondersteuning voor `Action`-namen.** De lijst kent geen `Action = jmp-returns-true`; de
bytes staan er letterlijk. De woordenschat van `docs/patchmotor.md` (`nop`, `jmp-returns-true`,
`replace-bytes`) zou hier bovenop te leggen zijn, maar dat is nog niet gedaan: wie een lijst maakt
schrijft de bytes zelf. `b801000000c3` is `jmp-returns-true` op 32 én 64 bits.

**9. Geen integriteitscontrole op de spelversie.** `verwacht` is de enige controle, en die is zo
goed als de lijst. Zet er altijd een `verwacht` bij, ook als je hem met de hand uit een dump haalt:
zonder `verwacht` schrijft de motor blind.

## 9. Wat er nog open is

1. **De launcher laten aanhaken.** `scripts/launch-direct.py` is van een andere agent; die moet de
   drop-in uit `patches/<Game>/dropin/` opzetten (inclusief `RAVENGUN_PATCHMEM_FILE`) en de motor
   als `Preload` respectievelijk `DllOverride` meegeven. Dit document beschrijft wat er dan moet
   staan; de koppeling zelf is niet gemaakt.
2. **`Action`-namen boven op de bytes.** Zie §8 punt 8.
3. **Een echte proxy-DLL-proef.** Zie §8 punt 1: de laadweg voor een echte game-exe is beredeneerd,
   niet gemeten.
4. **Een echte gepakte game.** Zie §8 punt 2: grens B en C zijn beredeneerd, niet gemeten.
5. **Een trigger-mechanisme.** Zie §8 punt 7.
