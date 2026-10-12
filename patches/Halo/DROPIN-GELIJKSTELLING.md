# De drop-in-kopieën gelijkstellen, en wat de rekenwijze-reparatie per game doet

*RavenGun, ronde "loader-specialist Halo", 2026-10-12. Bench-vrij: er is geen spel gestart, de NAS is
alleen gelezen, `packaging/native-games.json` is niet aangeraakt, en de gewijzigde bestanden van
andere partijen in de lader-repo (`aagames-setup.py`, `launch-direct.py`, `tp-add-game.py`,
`hid_board.*`, `rg_elfloader.c`) zijn met rust gelaten. Geen commit.*

**Eerst een rechtzetting, want die verandert de conclusie.** In mijn vorige stukken stond dat
Jurassic Park's `game` "ook nul hasp-namen in de dynsym" heeft. **Dat is fout.** Ik had
`readelf --dyn-syms` gebruikt, en dat geeft op deze bestanden **niets** terug omdat `game` **nul
sectiekoppen** heeft; de symbolen staan alleen in het dynamische segment. Gemeten via `PT_DYNAMIC`
(`probe/dynsym_probe.py`): Jurassic Park heeft **29 namen met `hasp_`** in zijn dynsym, allemaal
gedefinieerd met een absolute `st_value` rond `0x0852xxxx`. De drop-in van die game zegt het al sinds
eerder: "30 hasp_-symbolen in de dynsym, alle met shndx 11 en st_value != 0". Mijn eerdere claim is
in `NATIVE-RECIPE-PATCH.md` en `HASP-INJECTIE.md` gecorrigeerd.

Daarmee is de kernvraag anders dan verwacht: **Jurassic Park is juist de game waar de reparatie iets
doet**, en het is de enige van de vier.

---

## 1. De stand per game, met oude en nieuwe sha256

De nieuwe bibliotheken komen uit de huidige bron (`tools/rt-hasphook/hasp_inline_hook.c`), gebouwd
zoals `scripts/build-hasphook.sh` doet (beide breedtes, `la_version` aanwezig in allebei).

| game | bestand | oud | nieuw | actie |
|---|---|---|---|---|
| **JurassicPark** | `libravengun-hasphook.so` | 35.024 B `6ea7b2cec0da53e9` | **39.576 B `a2ab74d490e0118d`** | **nu bijgewerkt** |
| **JurassicPark** | `libravengun-hasphook-x64.so` | 40.912 B `9e87a1ec5f8e52b8` | **41.536 B `4264bf75abc320ab`** | **nu bijgewerkt** |
| Halo | `libravengun-hasphook.so` | 35.024 B (vóór mijn eerdere wijziging) | 39.576 B `a2ab74d490e0118d` | al gedaan |
| Halo | `libravengun-hasphook-x64.so` | 36.816 B (idem) | 41.536 B `4264bf75abc320ab` | al gedaan |
| GalagaAssault | beide | 35.024 B `eef750665287eb28` / 36.816 B `856c1a659177dcee` | **onveranderd** | niets — zie §4 |
| Terminator | beide | ontbreekt | **ontbreekt** | niets — zie §3 |

`JurassicPark/dropin/dropin.ini` is **niet** aangeraakt (3.793 B, ongewijzigd); die bevat al
`Audit = false` voor de hook en `Preload = true`.

Twee opmerkingen bij de tabel:

* Voor Halo is de **oude sha256 niet meer uit git te halen**: die bestanden zijn in dezelfde commit
  toegevoegd (`git show HEAD~1:patches/Halo/dropin/…` geeft niets). Ik heb daar de maten van vóór
  mijn wijziging (35.024 en 36.816 B, opgemeten op 2026-10-11 21:08).
* De twee Halo-bestanden en de twee JurassicPark-bestanden zijn nu **identiek**: de hook is generiek
  en er staat geen game-naam of game-adres in. Dat is precies de bedoeling.

Controle met het gereedschap van de bouwer (`scripts/build-hasphook.sh --check`): Halo en
JurassicPark **actueel**; GalagaAssault **afwijkend** (bewust, §4); Terminator **ontbreekt** (bewust,
§3).

---

## 2. Wat de reparatie per game doet — het bewijs zonder de bench

De reparatie is één regel in de naam-route:

```diff
-            adres = hh_base + (uintptr_t)hh_symtab[i].st_value;
+            adres = (uintptr_t)(hh_base - (uintptr_t)hh_laag) + (uintptr_t)hh_symtab[i].st_value;
```

`hh_base` is het adres waar het **laagste** `PT_LOAD`-segment is afgebeeld, dus `hh_base - hh_laag` is
het laadadres (de bias). Bij een PIE is `hh_laag` nul en verandert er niets; bij een **ET_EXEC** telt
de oude vorm het segmentadres dubbel.

Bewijs: `probe/arithmetic-ab.sh` bouwt de hook **twee keer uit dezelfde bron** — één keer met de
nieuwe regel, één keer met de oude — en laat beide los op een **echt gemapt beeld** van de game
(`probe/hook-name-route-test.c`: de `PT_LOAD`-segmenten op hun eigen `p_vaddr`, gevuld met de
bestandsbytes, vanuit een PIE-gastheer zodat er niets botst). Daarna wordt op elk doeladres
teruggelezen of er een sprong staat.

| game | wat de naam-route vindt | oude rekenwijze | nieuwe rekenwijze |
|---|---|---|---|
| **Jurassic Park** (ELF32, `hh_laag = 0x08048000`) | 26 namen met `hasp_` (gedefinieerd) | **0 van 11 aangehaakt** — `hasp_login op 0x10571650 is (nog) niet leesbaar; volgende ronde` | **11 van 11 aangehaakt** — `hasp_login op 0x08529650 -> 0xf7ee6220 (trampoline …, 5 bytes)` |
| Halo (ELF64, `hh_laag = 0x400000`) | **0** namen | 0 van 11 | 0 van 11 |
| Terminator (ELF32, `hh_laag = 0x08048000`) | **0** namen | 0 van 11 | 0 van 11 |

De rekenkundige kern, met de gemeten getallen van Jurassic Park:

```
hasp_login: st_value = 0x08529650
  oud:  hh_base + st_value = 0x08048000 + 0x08529650 = 0x10571650   -> buiten elk segment
        (segment 1 loopt van 0x08048000 tot 0x08adc000)             -> "niet leesbaar", nooit gepatcht
  nieuw: (hh_base - hh_laag) + st_value = 0 + 0x08529650 = 0x08529650 -> in segment 1, R+X -> gepatcht
```

En de teruggelezen bytes op Jurassic Park (32-bits, dus een 5-byte `e9 rel32`):

```
0x08529650  voor [53 83 ec 08]  na [e9 cb cb 9b ef ...]  AANGEHAAKT
0x08528090  voor [55 57 56 53]  na [e9 2b e2 9b ef ...]  AANGEHAAKT
   … 11 van 11, en de hook meldt zelf: "totaal aangehaakt: 11 van 11; niet aangehaakt: 0"
```

**Dit is het sterkste bewijs dat de reparatie echt iets oplost:** op Jurassic Park vond de hook de
namen al (dat was nooit het probleem), maar elke berekende aanroep viel buiten het beeld, dus er werd
nooit iets aangehaakt. Nu haakt hij alle elf aan. Op Halo en Terminator verandert er in **beide**
varianten niets, omdat daar geen enkele `hasp_`-naam in de dynsym staat.

### De twee andere controles die erbij horen

* **De bibliotheek bouwt** (32 én 64 bits, `la_version` in beide): zie §1; de bouw is onderdeel van
  `arithmetic-ab.sh` en `dropin_equalise.sh`, beide met exitcode 0.
* **De zelftest van de naam-route slaagt.** `probe/nopie-selftest.sh`: de bestaande 32-bits zelftest
  haakt 10 van 11 aan, met de emulator die antwoordt (`hasp_login -> 0, greep = 0xe6f`,
  `hasp_read(65524, 0, 16) -> 0, eerste bytes: 2c 03 ab 63`, `echte_aanroepen = 0`). En met een
  **`-no-pie`** gebouwde zelftest haakt hij op `0x080491c6` in plaats van op een bias-adres — dat is
  dezelfde reparatie, op een tweede ET_EXEC.

---

## 3. Terminator: het kanaal is er, maar er is niets om aan te haken — en het dossier is van iemand anders

**Het kanaal, in twee delen.**

1. **Zoals Terminator vandaag start:** het profiel heeft `loader: None`, `preload: ""`,
   `exe: /pm/game`. De native route gebruikt dus de **kernel-`ld.so`**, en dan werkt een gewone
   `LD_PRELOAD` in het spelproces — precies zoals bij Halo. Kanaal: **ja**.
2. **Zoals het plan is** (`--loader` / `"loader": "rg-elfloader"`, zie `LOADERS-ZES-GAMES.md`):
   `rg-elfloader` is een gewoon 32-bits PIE-glibc-programma dat de `PT_LOAD`-segmenten van de game op
   hun `p_vaddr` mapt (`MAP_FIXED|MAP_ANONYMOUS`) en dan **in hetzelfde proces** naar `e_entry`
   springt; het doet geen `exec` en het wist de omgeving niet. Een `LD_PRELOAD`-hook wordt dus
   geërfd en zijn constructor loopt. Op dat moment is het beeld nog niet gemapt, dus het synchrone pad
   faalt en neemt de **kijker-thread** het over (standaard 90 s, elke 5 ms); die vindt het beeld zodra
   de lader het gemapt heeft. Kanaal: **ja, met een race** — de lader past de relocaties toe en
   springt meteen, dus in theorie kan de eerste hasp-aanroep vóór de patch vallen. Dat is een
   `RAVENGUN_HASP_ELF_MS`/`WACHT`-afweging en hoort in het draaiboek van die game, niet hier.
   Het pad naar de game vindt de hook in deze route ook zonder omgevingsvariabele: `rg-elfloader
   <game>` zet het game-pad in de commandoregel, en de hook scant die.

**Waarom er dan toch niets bij komt.** Twee redenen, en de tweede is de belangrijkste:

* **Er is niets om aan te haken.** Gemeten via `PT_DYNAMIC`: Terminator's `game` heeft **1.019
  dynamische symbolen** (via `DT_GNU_HASH`; het bestand heeft wél sectiekoppen) en daar zit **geen
  enkele `hasp_`-naam** bij. De Sentinel-runtime zit er wel in (48 strings met `hasp`/`sentinel`),
  maar statisch en zonder namen. En er is **geen adreslijst** voor Terminator. Een hook die geladen
  wordt en niets vindt, is ballast — precies wat je niet wilt.
* **Het Terminator-dossier is op dit moment van een andere partij.** In
  `patches/Terminator/` staan `DRAAIBOEK-BENCH.md`, `LADER-README.md`, `README.md`,
  `classify-elf.py`, `find-dongle-adjacency.py`, `find-dongle-symbols.py` en `next-measurement.sh`,
  alle met tijdstempel **22:24** (enkele minuten voor dit schrijven), plus een eigen
  `dropin/{dropin.ini, ravengun-hasp.so}`. Daar bestanden bij zetten zou een tweede schrijver in
  hetzelfde dossier zijn. Ik laat het met rust en draag mijn kanaalanalyse hierboven aan.

**Wat er nodig is om die drop-in zinvol te maken:** een **adreslijst voor Terminator**, zoals
`patches/Halo/hasp-haken.ini` er een is. Anders dan bij Halo is er **geen `halo.so`-equivalent** om de
adressen uit te lezen: in Terminator's gamemap staan **geen `.so`-bestanden** (wel `hasp/` en
`ServiceDongleApp`). De adressen moeten dus uit de disassembly van Terminator's eigen Sentinel-code
komen — en dat is precies waar `find-dongle-symbols.py` in dat andere dossier op lijkt te mikken. Mijn
mechanisme is er klaar voor: zodra die lijst er is, is het één bestand plus één `RAVENGUN_HASP_ELF_ADRESLIJST`.

---

## 4. GalagaAssault: de game bestaat niet meer, dus geen drop-in

Bewijs, alle drie gemeten:

* **Op de NAS is er geen map** die op `*alaga*` matcht (hoofdletterongevoelig gezocht) — de game is
  van de NAS verdwenen.
* **Het profiel is weg**: `packaging/native-games.json` bevat **0** keer `GalagaAssault`. Dat is
  commit `ccd52fe` — *"app: vier verwijderde games eruit (Galaga, Pacman Chomp, Go Go Strike,
  Gashaaaan Refill)"*.
* **De repo heeft de map nog wel**: `patches/GalagaAssault/{README.md, game.ini,
  dropin/{dropin.ini, libravengun-hasphook.so, libravengun-hasphook-x64.so, librt-glfix.so,
  ravengun-hasp.so}}`.

Een bijgewerkte hook daar kan dus **nooit geladen worden**: er is geen game en geen profiel. Ik heb
hem daarom **niet** bijgewerkt (dezelfde afweging als bij Terminator: een drop-in die niet geladen kan
worden is ballast). Het gevolg is wel dat `build-hasphook.sh --check` die twee bestanden blijvend als
"afwijkend" blijft melden.

**Aanbeveling (voor de coördinator, niet iets wat ik stil doe):** haal de hele map
`patches/GalagaAssault/` weg in één commit. Dan is de `--check` weer stil en staat er geen dossier
voor een game die niet meer bestaat. Wil je hem toch consistent houden, dan is
`scripts/build-hasphook.sh --game GalagaAssault` één commando — maar dat is onderhoud aan iets
onbruikbaars.

---

## 5. Kan de reparatie een game veranderen die geen adreslijst gebruikt? **Ja — en dat is Jurassic Park**

Dit is geen aanname maar de meting uit §2. De volledige lijst, per game:

| game | route die de hook daar heeft | verandert de reparatie iets? | benchrun nodig? |
|---|---|---|---|
| **Jurassic Park** | **naam-route** (29 `hasp_`-namen in de dynsym), geen adreslijst | **ja: 0 → 11 aangehaakt** | **ja** — zie de kanttekening hieronder |
| Halo | naam-route vindt 0 namen; de **adreslijst** doet het werk (7 van 11) | nee (in beide varianten 0) | nee voor deze reparatie; Halo's run gaat over de dongle + FTDI |
| Terminator | naam-route vindt 0 namen; geen adreslijst | nee (in beide varianten 0) | nee voor deze reparatie |
| GalagaAssault | game bestaat niet meer | n.v.t. | nee |

**Dus de benchlijst die uit deze reparatie volgt is één game: Jurassic Park.**

Twee kanttekeningen die de waarde van die run bepalen, allebei al gemeten en opgeschreven:

1. **Het recept laadt de hook voor Jurassic Park nog niet.** Het profiel heeft `preload: ""`. De
   reparatie verandert dus pas iets als de hook daar ook werkelijk vooraf geladen wordt. Dat is een
   receptwijziging en die is van de coördinator; het voorstel staat hieronder.
2. **Jurassic Park heeft een eigen, losse blokkade.** Het profiel zegt: *"sprong naar een vast adres
   uit de dump (SIGSEGV op 0xb7227dc0 voor main); de beveiligingslaag die BudgieLoader op dat adres
   zet, is er native niet."* Een run op Jurassic Park test dus **de hook**, niet een speelbare game.
   De hook kan zijn werk doen en de game kan alsnog vóór `main` sterven.

**Voorstel voor de JurassicPark-regel** (32-bits! dus de 32-bits hook), in dezelfde vorm als het
Halo-voorstel in `NATIVE-RECIPE-PATCH.md` §2:

```diff
-      "preload": "",
+      "preload": "/rg/lib/libravengun-hasphook.so",
```
```diff
       "libs": [
         "@tpui/ElfLdr2/libs/libPhysXLoader.so.1",
         …
+        ,"/home/deck/projects/ravengun-patches/patches/JurassicPark/dropin/libravengun-hasphook.so"
       ],
+      "env": {
+        "RAVENGUN_HASP_DIR": "/rg/hasp",
+        "RAVENGUN_HASP_GAME": "JurassicPark",
+        "RAVENGUN_HASP_ELF_LIB": "/rg/hasp/ravengun-hasp.so",
+        "RAVENGUN_HASP_ELF_GAME": "/pm/game",
+        "RAVENGUN_HASP_ELF_LOG": "/pm/hasphook.log",
+        "RAVENGUN_HASP_VERBOSE": "1"
+      },
```
`@data/hasp` → `/rg/hasp` moet dan ook in `libdirs` (die bron staat al in het Halo-voorstel), en let
op de breedte: `ravengun-hasp.so` (32-bits) en `libravengun-hasphook.so` (32-bits), niet de `-x64`.
De donglegegevens van Jurassic Park staan in
`patches/JurassicPark/dropin/ravengun-hasp/JurassicPark/`; `docs/hasp-layer.md` §9 noemt de fileids
daarvan nog niet uitgezocht, dus dat is een tweede open punt voor die run.

---

## 6. Hoe je dit herhaalt

```bash
P=~/projects/ravengun-patches/patches/Halo/probe

# per game: wat vindt de naam-route in de dynsym?  (via PT_DYNAMIC, niet via readelf)
python3 $P/dynsym_probe.py "/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park/game" hasp_

# de A/B: oude rekenwijze tegen nieuwe, op een echt gemapt beeld, voor alle vier games
bash $P/arithmetic-ab.sh

# de drop-in-kopieën: stand vóór/na met sha256, en de status per game
bash $P/dropin_equalise.sh

# de naam-route als regressie (PIE en -no-pie), en de adres-route van Halo
bash $P/nopie-selftest.sh
bash $P/address-route-test.sh
```
