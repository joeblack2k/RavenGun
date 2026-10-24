# Halo: Fireteam Raven — voorstel voor `packaging/native-games.json`

*RavenGun, ronde "loader-specialist Halo", 2026-10-12 (tweede versie). Dit is een **voorstel**, geen
wijziging: `packaging/native-games.json` staat op zijn gecommitte basis (`ccd52fe`) en mijn eerdere
schrijfactie is teruggedraaid (`probe/revert_recipe.sh`, werkboom schoon). Eén schrijver per moment.*

Bench-vrij gemeten: er is geen spel gestart, de NAS is alleen gelezen, `halo.so` is niet aangeraakt.

**Wat er sinds de eerste versie van dit document is veranderd.** De eerste versie zette onze hook
alleen in `libs`, met de meting dat hij in Halo **nul** hasp-namen in de dynsym vindt en dus nooit
iets kan aanhaken. Dat gat is nu gedicht: de hook heeft een **generieke adres-route** gekregen
(mechanisme in de lader, de adreslijst per game in `patches/Halo/hasp-haken.ini`) en die is
bench-vrij bewezen — **7 van 7 aangehaakt op een echt gemapt beeld waar de naam-route nul vond**.
Daarom is dit voorstel nu wél de verwisseling: de hook in `preload` in plaats van `halo.so`.

---

## 1. De twee vragen die eerst beantwoord moeten zijn

### (a) Kan onze hasphook in het native recept werken, gegeven de ElfLdr2-route?

**Het kanaal en de vondst zijn twee verschillende dingen, en ze zijn nu allebei in orde.**

* **Het kanaal past.** In de ElfLdr2-route is er geen bruikbaar kanaal: `LD_PRELOAD` laat
  `BudgieLoader` (Cygwin) sterven met `error while loading shared libraries`, en `LD_AUDIT` laat de
  lader sterven zodra de hook het spelbeeld meldt (zeven proeven P0–P7 in `patches/Halo/README.md`
  §3). De native route heeft dat probleem niet: daar is er **één gewoon ELF-proces**
  (`/pm/g7/halo/game`), en `LD_PRELOAD` komt in dat proces. Dit voorstel gaat daarom alleen over
  `scripts/play-native.sh`.
* **De vondst paste niet, en past nu wel.** Onze hook zocht zijn doelen **uitsluitend op naam in de
  dynsym**. Gemeten: Halo's `game` heeft 220 dynsym-symbolen, 182 ongedefinieerd en **allemaal**
  libc/pthread/socket/dl, en **nul** met `hasp_`; de byte-sequentie `hasp_login` komt **nul keer**
  in het hele bestand voor.
  **Let op: dat is niet voor de hele familie zo.** Jurassic Park's `game` heeft **29 namen met
  `hasp_`** in zijn dynsym (de drop-in van die game zegt 30), allemaal gedefinieerd met een
  absolute `st_value` rond `0x0852xxxx` — gemeten met `probe/dynsym_probe.py`, dat de
  symbolentabel via `PT_DYNAMIC` leest. Een eerdere versie van dit document beweerde het
  omgekeerde; die fout kwam van `readelf --dyn-syms`, dat op deze bestanden **niets** teruggeeft
  omdat `game` nul sectiekoppen heeft. Zie `DROPIN-GELIJKSTELLING.md` §2.
* **Wat er nu is.** Een tweede weg, naast de naam-route:
  * **Het mechanisme is generiek** en staat in de lader (`tools/rt-hasphook/hasp_inline_hook.c`):
    de hook leest een adreslijst, keurt elk adres, en haakt aan. Geen enkele game-naam of game-adres
    staat in de C-code.
  * **De gegevens staan per game in de patches-repo**: `patches/Halo/hasp-haken.ini`. Dat bestand
    is de schakelaar: zonder het bestand (en zonder `RAVENGUN_HASP_ELF_ADRESLIJST`) verandert er
    niets en blijft de naam-route de enige weg — dat is de standaard.
  * **De adressen zijn gemeten, niet gegokt.** `halo.so`'s `initialize()` op `0x569f` roept zeven
    keer `flh_inline_hook` aan en schrijft de retourwaarde meteen in de bijbehorende
    `real_hasp_*`-gleuf; de disassembly geeft dus de naam bij elk adres:

    | adres in `game` | naam (uit de `real_hasp_*`-gleuf) |
    |---|---|
    | `0xB2D630` | `hasp_login` |
    | `0xB2DD48` | `hasp_logout` |
    | `0xB2FD63` | `hasp_read` |
    | `0xB2FE60` | `hasp_write` |
    | `0xB2E1AA` | `hasp_encrypt` |
    | `0xB2E391` | `hasp_free` |
    | `0xB2F5AC` | `hasp_get_sessioninfo` |

    `hasp_login_scope`, `hasp_decrypt`, `hasp_get_size` en `hasp_get_info` staan er **niet** in:
    `halo.so` haakt die vier niet aan, dus er is geen gemeten adres. De hook meldt ze na het
    aanhaken apart (`niet aangehaakt: hasp_get_info (niet in de dynsym en niet in de adreslijst)`),
    zodat dat gat zichtbaar blijft in plaats van stil. Staat een naam **wel** in de adreslijst maar
    is zijn adres afgekeurd, dan zegt diezelfde regel dat met de reden erbij
    (`… (in de adreslijst maar afgekeurd: eerste bytes e9 ab 22 00 zijn geen prologue …)`) — de
    samenvatting onderscheidt dus drie gevallen: niet in de dynsym én niet in de lijst, wel in de
    lijst maar afgekeurd (met reden), of aangehaakt. Zie `DROPIN-GELIJKSTELLING.md` §2b.
  * **Er wordt nooit blind gepatcht.** Elk adres moet in een uitvoerbaar `PT_LOAD`-segment vallen,
    binnen `p_filesz`, en de bytes moeten een herkenbare functieprologue zijn. Daarnaast zijn er
    twee versiecontroles (`grootte`, `ingang`): kloppen die niet, dan keurt de hook de **hele**
    lijst af en patcht hij niets. Zie §4 voor de drie gemeten weigeringen.
* **De naam-route blijft de standaard en blijft werken.** Gemeten met de bestaande zelftest, met de
  hook en de Halo-dongle erbij: 10 van 11 aangehaakt, `hasp_login -> 0, greep = 0xe6f`,
  `hasp_read(65524, 0, 16) -> 0, eerste bytes: 2c 03 ab 63`, `echte_aanroepen = 0` — dus alle
  aanroepen zijn door de **emulator** beantwoord en de statisch gelinkte cliënt is niet aangeroepen.
* **Eén latente fout in de naam-route is meteen gerepareerd.** Die rekende `hh_base + st_value`,
  waarbij `hh_base` het adres van het laagste segment is. Voor een PIE klopt dat (`hh_laag` is 0),
  maar voor een **ET_EXEC** telt het het segmentadres dubbel: Halo's `game` heeft `hh_laag =
  0x400000` en Jurassic Park's `game` `0x08048000`. De juiste uitdrukking is `hh_base - hh_laag +
  st_value` (het laadadres plus de symboolwaarde). Dat is nu zo, en het is gemeten op beide
  soorten gastheer: een PIE-zelftest haakt op `0x5662e1dd`, een **`-no-pie`**-zelftest op
  `0x080491c6` — absoluut en binnen zijn eigen segment, wat met de oude rekenwijze onmogelijk was.

### (b) De emulator wordt met `dlopen(RTLD_LOCAL)` geladen, niet met `LD_PRELOAD`

Geen smaak maar een gemeten eigenschap, en het detail dat een latere lezer het makkelijkst sloopt:

* `ravengun-hasp-x64.so` exporteert **25** `hasp_*`-namen. De hook laadt hem met
  `dlopen(pad, RTLD_NOW | RTLD_LOCAL)` (`hh_open_geisoleerd()`) en lost de **elf** namen die hij
  nodig heeft via **die handle** op (`dlsym(hh_emulator, …)`).
* Gemeten met `probe/hook-emulator-coupling.c`, dat precies `hh_laad_emulator()` +
  `hh_koppel_emulator()` nadoet:
  * via `dlopen(RTLD_LOCAL)`: `[ok ] hasp_login staat niet in de globale naamruimte` ✔
  * met dezelfde emulator in `LD_PRELOAD`: `[let] hasp_login staat WEL in de globale naamruimte` ✘
* En `halo.so` exporteert zelf **10** `hasp_*`-namen. Zet je de emulator in `LD_PRELOAD`, dan zijn er
  in één proces **twee leveranciers** van `hasp_login`, en wie wint hangt van de laadorde af.

Daarom is de emulator in dit voorstel een **bestand in de sandbox dat de hook opzoekt**
(`RAVENGUN_HASP_ELF_LIB=/rg/hasp/ravengun-hasp-x64.so`) en **geen** regel in `preload`.

---

## 2. De diff, per blok

Uitgangspunt: het **gecommitte** bestand, commit `ccd52fe`. De Halo-tak staat op regel 8 en heeft nu
`libdirs` (1 bron), `preload`, `exe`, `cwd`, `home`, `args`, `route`, `libs` (1 regel) en
`extra_ro`; er is geen `env`-blok. Vijf blokken, waarvan er één een bestaande regel **wijzigt** en
vier toevoegen.

### Blok 1 — `libdirs`: één bron toevoegen, achter de bestaande

```diff
       "libdirs": [
         {
           "src": "@data/runtime/lib-fmodstub",
           "dst": "/rg/libfmod",
           "note": "FMOD 1.10 ontbreekt (propriëtair); de stub geeft geen geluid"
+        },
+        {
+          "src": "@data/hasp",
+          "dst": "/rg/hasp",
+          "note": "de HASP-emulator + de donglegegevens (geïnstalleerd); de inline-hook laadt de emulator hiervandaan met dlopen(RTLD_LOCAL), NIET met LD_PRELOAD"
         }
       ],
```

### Blok 2 — `preload`: **dit is de verwisseling**

```diff
-      "preload": "/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so",
+      "preload": "/rg/lib/librg_rt_input.so:/rg/lib/libravengun-hasphook-x64.so",
```

`halo.so` blijft als bestand staan (het zit in de overlay op `/pm/g7/halo/halo.so`); terugrollen is
deze ene regel terugzetten. Zie §6 voor waarom nooit beide tegelijk in `preload` mogen.

### Blok 3 — `libs`: één regel toevoegen, achter de bestaande

```diff
       "libs": [
-        "@build/librg_rt_input.so"
+        "@build/librg_rt_input.so",
+        "/home/deck/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook-x64.so"
       ],
```

### Blok 4 — `env`: een nieuw blok, tussen `libs` en `extra_ro`

```diff
+      "env": {
+        "RAVENGUN_HASP_DIR": "/rg/hasp",
+        "RAVENGUN_HASP_GAME": "Halo Fireteam",
+        "RAVENGUN_HASP_ELF_LIB": "/rg/hasp/ravengun-hasp-x64.so",
+        "RAVENGUN_HASP_ELF_GAME": "/pm/g7/halo/game",
+        "RAVENGUN_HASP_ELF_ADRESLIJST": "/home/deck/projects/ravengun-patches/patches/Halo/hasp-haken.ini",
+        "RAVENGUN_HASP_ELF_LOG": "/pm/g7/halo/hasphook.log",
+        "RAVENGUN_HASP_VERBOSE": "1"
+      },
```

`play-native.sh` leest `env` als `GENV` en past die toe **ná** `LD_PRELOAD`; dit blok overschrijft
`LD_PRELOAD` dus niet. **[gemeten in de droogloop, §4]** `RAVENGUN_HASP_VERBOSE` mag na de eerste
geslaagde run weg (het is één regel per aangeroepen functie).

### Blok 5 — `extra_ro`: de adreslijst de sandbox in

```diff
       "extra_ro": [
-        "/dev/shm/TeknoParrot_JvsState"
+        "/dev/shm/TeknoParrot_JvsState",
+        "/home/deck/projects/ravengun-patches/patches/Halo/hasp-haken.ini"
       ]
```

`extra_ro` bindt een pad op **dezelfde** plek read-only (`--ro-bind-try "$x" "$x"`), dus de
`RAVENGUN_HASP_ELF_ADRESLIJST` hierboven wijst naar hetzelfde pad binnen en buiten de sandbox. Zonder
deze regel is het bestand onleesbaar en meldt de hook dat luid (§6, geval 6).

---

## 3. Reden per regel

| regel | waarom precies dit |
|---|---|
| `"src": "@data/hasp"` | De emulator én de Halo-donglegegevens staan al geïnstalleerd in `~/.local/share/ravengun/hasp/`; `@data` is een bestaande placeholder, dus geen nieuw aanknopingspunt en geen absoluut hostpad. |
| `"dst": "/rg/hasp"` | Korte, vaste naam in de sandbox waar de hook en `dongle.c` naartoe kunnen wijzen. |
| de `note` erbij | Waarschuwt de volgende lezer dat de emulator hier **niet** voor `LD_PRELOAD` ligt — dat is de fout uit §1(b). |
| `preload` → de hook | Dit is de verwisseling: onze eigen hook neemt de HASP-aanroepen over in plaats van de onbekende `halo.so`. Nu gemeten dat hij zeven functies vindt (§4). |
| `libs` + de hook | `libs` bindt elk bestand op `/rg/lib/<naam>` en zet `/rg/lib` in `LD_LIBRARY_PATH`; de `preload`-regel verwijst naar `/rg/lib/…`, dus zonder deze regel laadt de hook niet. |
| `RAVENGUN_HASP_DIR=/rg/hasp` | `dongle.c` zoekt `<DIR>/<naam>/dongle.json` (met een terugval op `<DIR>/<naam>/dropin/ravengun-hasp/<naam>`). |
| `RAVENGUN_HASP_GAME="Halo Fireteam"` | De **mapnaam** en het veld `game` in `dongle.json` — met spatie. De ElfLdr2-drop-in gebruikte `Halo` (dat werkt via de terugval op de patches-repo-indeling); met de geïnstalleerde indeling moet het `Halo Fireteam` zijn. |
| `RAVENGUN_HASP_ELF_LIB=/rg/hasp/ravengun-hasp-x64.so` | De standaard van de hook is een **hostpad** dat in de sandbox niet bestaat; expliciet is hier beter dan leunen op `LD_LIBRARY_PATH`. |
| `RAVENGUN_HASP_ELF_GAME=/pm/g7/halo/game` | De hook leest het spelbeeld van schijf voor zijn dynsym én voor het keuren van de adressen; zonder deze regel valt hij terug op `/proc/self/exe`. |
| `RAVENGUN_HASP_ELF_ADRESLIJST=…/hasp-haken.ini` | **De schakelaar van de adres-route.** Zonder deze variabele (of een `hasp-haken.ini` naast de hook) blijft de naam-route de enige weg — dat is de standaard en dat is precies wat je wilt voor de andere games. |
| `RAVENGUN_HASP_ELF_LOG=/pm/g7/halo/hasphook.log` | `/pm/g7/halo` is een **overlay**, dus dit logboek landt onder `~/.local/share/ravengun/native/Halo/<n>/upper/` en niet op de NAS. |
| `RAVENGUN_HASP_VERBOSE=1` | Eén regel per aangeroepen hasp-functie plus het volledige keuringsoordeel per adres; precies wat je bij een eerste run wilt zien. |
| `extra_ro` + de adreslijst | Bindt het bestand read-only op zijn eigen pad, zodat de variabele hierboven binnen de sandbox hetzelfde pad kan aanwijzen. |

---

## 4. Het droogloop-bewijs (geen spelstart)

Reproduceerbaar met:
```bash
bash ~/projects/ravengun-patches/patches/Halo/probe/proposal_dryrun.sh    # het recept
bash ~/projects/ravengun-patches/patches/Halo/probe/address-route-test.sh # de adres-route
bash ~/projects/ravengun-patches/patches/Halo/probe/address-route-neg.sh  # de weigeringen
bash ~/projects/ravengun-patches/patches/Halo/probe/nopie-selftest.sh     # de naam-route
```
`proposal_dryrun.sh` bouwt een **kladboom** in `/tmp/rg-proposed` met het gecommitte bestand + dit
voorstel, en raakt `packaging/` in de repo niet aan.

**1. `play-native.sh --dry-run` tegen het voorstel: exitcode 0**, met:
```
--ro-bind … /rg/libfmod
--ro-bind … /rg/hasp
--ro-bind …/librg_rt_input.so  /rg/lib/librg_rt_input.so
--ro-bind …/libravengun-hasphook-x64.so  /rg/lib/libravengun-hasphook-x64.so
--ro-bind-try …/hasp-haken.ini           …/hasp-haken.ini
--setenv LD_LIBRARY_PATH /rg/libfmod:/rg/hasp:/rg/lib
--setenv LD_PRELOAD      /rg/lib/librg_rt_input.so:/rg/lib/libravengun-hasphook-x64.so
--setenv RAVENGUN_HASP_DIR        /rg/hasp
--setenv RAVENGUN_HASP_GAME       Halo Fireteam
--setenv RAVENGUN_HASP_ELF_LIB    /rg/hasp/ravengun-hasp-x64.so
--setenv RAVENGUN_HASP_ELF_GAME   /pm/g7/halo/game
--setenv RAVENGUN_HASP_ELF_ADRESLIJST …/hasp-haken.ini
--setenv RAVENGUN_HASP_ELF_LOG    /pm/g7/halo/hasphook.log
--setenv RAVENGUN_HASP_VERBOSE    1
… /pm/g7/halo/game -f1920x1080 -n
```
`--dry-run` weigert met een `die` als een `mounts`-, `files`-, `libdirs`- of `libs`-bron ontbreekt,
dus dit bewijst ook dat alle paden bestaan.

**2. De adres-route op een echt gemapt beeld.** `probe/hook-address-route-test.c` zet de twee
`PT_LOAD`-segmenten van de echte `game` vast op hun eigen virtuele adressen (vanuit een PIE-gastheer,
dus zonder botsing), vult ze met de bestandsbytes, en laadt dan de hook met `dlopen` — dezelfde weg
door de code als in het spelproces. Uitkomst:

```
[ravengun-hasphook] dynsym: 220 symbolen
[ravengun-hasphook] 0 hasp-symbolen in de dynsym van de game (gedefinieerd in de game zelf)
[ravengun-hasphook] adreslijst: 7 regel(s) uit …/hasp-haken.ini (basis: absoluut)
[ravengun-hasphook]   hasp_login             0x00b2d630  GELDIG: segment 0x400000 (R+X), bestand 0x72d630, prologue 55 41 57 41
   … (zeven keer GELDIG)
[ravengun-hasphook]   aangehaakt: hasp_login  op 0x00b2d630 -> 0x7f1abef0f240 (trampoline …, 12 bytes)
   … (zeven keer aangehaakt)
[ravengun-hasphook]   niet aangehaakt: hasp_login_scope / hasp_decrypt / hasp_get_size / hasp_get_info
[ravengun-hasphook] totaal aangehaakt: 7 van 11; niet aangehaakt: 4

0x00b2d630  voor [55 41 57 41]  na [48 b8 40 f2 f0 be 1a 7f 00 00 ff e0]  AANGEHAAKT
   … (zeven keer)
4. GESLAAGD: 7 van 7 aangehaakt
```

Dat is de kern: **dezelfde run waarin de naam-route nul vindt, haakt de adres-route zeven functies
aan**, en het teruggelezen geheugen toont op elk adres de absolute sprong `48 b8 … ff e0`.

**3. De drie weigeringen** (`address-route-neg.sh`), alle drie luid en zonder te patchen:

| geval | lijst | oordeel van de hook | aangehaakt |
|---|---|---|---|
| adres zonder functie | `hasp_login = 0x400000` | `AFGEWEZEN: wijst naar het begin van het bestand (ELF-kop), geen functie` | 6 van 7 |
| verkeerde build | `grootte = 1234` | `adreslijst AFGEKEURD: grootte 1234 in de lijst, maar het beeld is 33554432 bytes` → hele lijst | **0 van 7** |
| midden in een functie | `hasp_read = 0xb2d800` | `AFGEWEZEN: eerste bytes 00 00 3c 15 zijn geen prologue (mogelijk al door een andere hook gepatcht)` | 6 van 7 |

Het derde geval is precies de detector voor "iemand anders heeft hier al gepatcht": als `halo.so` er
als eerste was, staan er geen prologue-bytes meer en weigert onze hook in plaats van een ketting van
twee vervangingen te bouwen.

Dezelfde weigering is ook in de **samenvatting** terug te lezen, met de reden erbij — dat is de regel
die iemand leest die een benchrun uitzoekt, dus daar staat geen "niet in de adreslijst" meer bij een
adres dat er wel in staat. Gemeten op de Terminator-lijst (die één bewust afgewezen regel heeft,
`hasp_free` is daar een alias-keten):

```
voor:  niet aangehaakt: hasp_free  (niet in de dynsym en niet in de adreslijst)          <- misleidend
na:    niet aangehaakt: hasp_free  (in de adreslijst maar afgekeurd: eerste bytes e9 ab 22 00
                                    zijn geen prologue (mogelijk al door een andere hook gepatcht))
```

Het aantal aangehaakt blijft in beide gevallen 10 van 11, en de aangehaakte adressen zijn identiek —
dit is een rapportagewijziging, geen gedragswijziging. Zie `DROPIN-GELIJKSTELLING.md` §2b.

**4. De naam-route, als regressie.** Beide breedtes bouwen (`scripts/build-hasphook.sh --no-place`,
`la_version` aanwezig in 32 én 64 bits) en de bestaande 32-bits zelftest haakt 10 van 11 aan met de
emulator die antwoordt (`greep = 0xe6f`, `2c 03 ab 63`). Met een `-no-pie` gebouwde zelftest haakt
hij op `0x080491c6` in plaats van op een bias-adres — het bewijs dat de rekenwijze-fix uit §1(a)
noodzakelijk is voor ET_EXEC.

**5. De koppeling hook ↔ emulator:** alle elf namen oplosbaar via de `RTLD_LOCAL`-handle, en
`hasp_login` **niet** in de globale naamruimte.

**6. `ldd`:** `LD_LIBRARY_PATH=…/lib-fmodstub ldd '…/game' | grep -c 'not found'` → **0**.

---

## 5. Wat er in de huidige versie staat dat moet blijven

Alles behalve de ene `preload`-regel. Expliciet, zodat een samenvoeging ze niet per ongeluk opruimt:

| bestaand element | blijft |
|---|---|
| `preload` → `librg_rt_input.so` (het eerste deel) | **ja**; alleen het tweede deel (`halo.so`) wordt vervangen |
| `libdirs` → `@data/runtime/lib-fmodstub` → `/rg/libfmod` | ja, en blijft de **eerste** van de twee zijn |
| `libs` → `"@build/librg_rt_input.so"` | ja, en blijft de **eerste** van de twee zijn |
| `mounts` (overlay `/pm/g7/halo`, ro `/pm/g7/dump`) | ja |
| `files` (`rtinit_original`, `run_game.sh_original`) | ja |
| `exe`/`cwd`/`home` (`/pm/g7/halo`), `args` (`-f{res}`, `-n`), `route` (`tp-shm`) | ja |
| `extra_ro` → `/dev/shm/TeknoParrot_JvsState` | ja, en blijft de **eerste** van de twee zijn |
| `enabled: false`, `status: "geparkeerd"` | ja — dit voorstel verandert de status niet |
| `halo.so` als bestand | ja, onaangeroerd; het blijft de terugrolweg |

---

## 6. Wat er misgaat als de volgorde of de kanalen fout zijn

1. **De emulator in `LD_PRELOAD` zetten** (in plaats van hem door de hook te laten `dlopen`-en):
   `hasp_login` komt in de **globale** naamruimte — gemeten, §1(b). Met `halo.so` erbij zijn er dan
   twee leveranciers van dezelfde naam en wie wint hangt van de laadorde af.
2. **Beide hooks actief** (`halo.so` én de onze, elk met hun eigen trampoline naar "het origineel"):
   de laatstgepatchte wint en de verliezer zijn trampoline wijst naar de winnaar — een ketting door
   twee emulators. Daarom staat er precies één van de twee in `preload`. Onze eigen prologue-controle
   vangt dit trouwens ook af als de andere hook er als eerste was (§4, geval 3).
3. **De hook in `preload` zonder adreslijst**: hij vindt nul namen in de dynsym en haakt niets aan.
   Het logboek zegt dan `0 hasp-symbolen in de dynsym` en `geen adreslijst; alleen de naam-route`.
   Geen crash, maar de dongle blijft onbeantwoord.
4. **`RAVENGUN_HASP_ELF_LIB` weglaten**: de hook valt terug op
   `/home/deck/.local/share/ravengun/hasp/ravengun-hasp-x64.so`, een **hostpad** dat in de sandbox
   niet bestaat. Het logboek zegt `GEEN emulator kunnen laden` en elke vervanging valt terug op de
   originele code — stil, zonder crash.
5. **`RAVENGUN_HASP_GAME` op `Halo` in plaats van `Halo Fireteam`**: `dongle.c` zoekt dan
   `<DIR>/Halo/dongle.json` en `<DIR>/Halo/dropin/ravengun-hasp/Halo`; met `/rg/hasp` als wortel
   bestaat geen van beide en er wordt **geen** dongle geladen. Dit is de waarschijnlijkste
   vergissing, want in de ElfLdr2-drop-in is `Halo` juist wél goed.
6. **De adreslijst niet in de sandbox binden** (blok 5 vergeten): de hook meldt
   `adreslijst … is niet te lezen; alleen de naam-route`, en er wordt niets aangehaakt. Ook dat is
   luid, maar het is één regel vergeten.
7. **De adreslijst van de verkeerde game-versie gebruiken**: de hook keurt de hele lijst af op
   `grootte`/`ingang` en patcht niets (§4, geval 2). Dat is de bedoeling — een adres uit een andere
   build is een crash die niemand terugvindt.
8. **`env` vóór `libs` zetten**: onschadelijk voor de uitkomst, maar het breekt de leesorde van het
   bestand (`route` → `libs` → `env` → `extra_ro`), en dat maakt diffs onnodig groot.

---

## 7. Wat hier niet in zit

* **Geen wijziging aan `scripts/play-native.sh`.** Twee dingen zouden het bestand netter maken en
  zijn **niet** voorgesteld omdat ze de launcher raken: een placeholder `@patches` (dan hoeven blok 3
  en blok 5 geen absoluut `/home/deck`-pad te bevatten), en het honoreren van `RG_DROPIN_PRELOAD`
  (`docs/hasp-layer.md` §9 punt 2).
* **Geen tweede adreslijst voor de andere games.** Terminator, Jurassic Park en Galaga hebben nog
  geen `hasp-haken.ini`; voor hen blijft de naam-route de enige weg (en die vindt daar ook nul namen,
  dus daar is de dongle nog open). Dit voorstel raakt alleen Halo.
* **De drop-in-kopieën van de andere games staan achter op de bron.** Na het bouwen van de nieuwe
  hook is `patches/Halo/dropin/` bijgewerkt; `patches/JurassicPark/dropin/` en
  `patches/GalagaAssault/dropin/` wijken af en `patches/Terminator/dropin/` heeft ze niet. Wie alles
  gelijk wil, draait `scripts/build-hasphook.sh` zonder argumenten — dat raakt vier games, dus dat is
  een afweging voor de coördinator, niet iets wat ik stil doe.
* **Geen bewijs dat de game de zeven aangehaakte functies ook aanroept.** Dat vraagt de bench; zie
  `RUN-1-DRAAIBOEK.md` (FTDI) en de eerste native run met deze verwisseling.
