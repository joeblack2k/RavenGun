# Halo: Fireteam Raven — voorstel voor `packaging/native-games.json`

*RavenGun, ronde "loader-specialist Halo", 2026-10-12. Dit is een **voorstel**, geen wijziging:
het bestand `packaging/native-games.json` staat weer op zijn gecommitte basis (`ccd52fe`) en mijn
eigen schrijfactie is teruggedraaid (`git checkout -- packaging/native-games.json`, werkboom schoon).
Eén schrijver per moment — deze regel is voor de coördinator.*

Bench-vrij gemeten: er is geen spel gestart, de NAS is alleen gelezen, `halo.so` is niet aangeraakt.

---

## 1. De twee vragen die eerst beantwoord moeten zijn

### (a) Kan onze hasphook in het native recept werken, gegeven de ElfLdr2-route?

**Het kanaal past, de vondst niet — en dat zijn twee verschillende dingen.**

* **Het kanaal past.** In de ElfLdr2-route is er geen bruikbaar kanaal: `LD_PRELOAD` laat
  `BudgieLoader` (Cygwin) sterven met `error while loading shared libraries`, en `LD_AUDIT` laat de
  lader sterven op het moment dat de hook het spelbeeld meldt. Dat is gemeten en staat in
  `patches/Halo/README.md` §3 (zeven proeven P0–P7). De native route heeft dat probleem niet: daar
  is er **één gewoon ELF-proces** (`/pm/g7/halo/game`), en `LD_PRELOAD` komt in dat proces. Onze
  hook is dus in de native route op de juiste plek; dat is precies waarom dit voorstel alleen over
  `scripts/play-native.sh` gaat.
* **De vondst past niet.** Onze hook vindt zijn doelen **uitsluitend op naam in de dynsym** van het
  spel (`hh_verzamel()` + `hh_haak_alles()`, `tools/rt-hasphook/hasp_inline_hook.c`). Voor Halo is
  die lijst leeg. Gemeten, met de hook in meet-stand en het echte spelbeeld:

  ```
  $ RAVENGUN_HASP_ELF_GAME='…/Halo Fireteam/g7/halo/game' RAVENGUN_HASP_ELF_UIT=1 \
      RAVENGUN_HASP_ELF_LOG=/tmp/hh.log \
      LD_PRELOAD=librg_rt_input.so:libravengun-hasphook-x64.so /bin/ls
  [ravengun-hasphook] spel-ELF gelezen uit …/game (RAVENGUN_HASP_ELF_GAME): 33554432 bytes, 9 program headers
  [ravengun-hasphook] aantal symbolen uit DT_HASH: 220
  [ravengun-hasphook] dynsym: 220 symbolen
  [ravengun-hasphook] 0 hasp-symbolen in de dynsym van de game (gedefinieerd in de game zelf)
  [ravengun-hasphook] RAVENGUN_HASP_ELF_UIT gezet: alleen gemeten, niets aangehaakt
  ```

  Dat klopt met de statische meting: 220 dynsym-symbolen, 182 ongedefinieerd en **allemaal**
  libc/pthread/socket/dl; `hasp_login` komt **nul keer** als byte-sequentie in `game` voor.
  De kop van `hasp_inline_hook.c` ("`nm -D game` → wél `T hasp_login`") geldt voor de **32-bits**
  zusjes, niet voor Halo.

  **Gevolg: de hook hoort vandaag NIET in `preload`.** Hij zou laden, niets vinden en niets doen —
  en als hij in de plaats van `halo.so` kwam, zou de dongle onbeantwoord blijven. Daarom staat hij
  in het voorstel alleen in `libs` (gebonden, niet geladen): dat is de voorwaarde voor de
  verwisseling, niet de verwisseling.

  **De verwisseling is één regel** zodra de vondst er is. De invoer daarvoor is al gemeten:
  `halo.so`'s `initialize()` op `0x569f` patcht **zeven** functies in `game` — `0xB2D630`,
  `0xB2DD48`, `0xB2E1AA`, `0xB2E391`, `0xB2F5AC`, `0xB2FD63`, `0xB2FE60`, alle zeven met een normale
  x86-64-prologue en alle zeven in segment 1. Dat is de adreslijst die `hh_haak_alles()` nodig heeft
  (een terugval naast de naamopzoeking). Zie `HASP-INJECTIE.md` §3.

### (b) De emulator wordt met `dlopen(RTLD_LOCAL)` geladen, niet met `LD_PRELOAD`

Dit is geen smaak maar een gemeten eigenschap, en het is het detail dat een latere lezer het
makkelijkst sloopt:

* `ravengun-hasp-x64.so` exporteert **25** `hasp_*`-namen. Onze hook laadt hem met
  `dlopen(pad, RTLD_NOW | RTLD_LOCAL)` (`hh_open_geisoleerd()`), lost de **elf** namen die hij nodig
  heeft via **die handle** op (`dlsym(hh_emulator, …)`), en laat de exporten verder met rust.
* Gemeten met `probe/hook-emulator-coupling.c`, dat precies `hh_laad_emulator()` +
  `hh_koppel_emulator()` nadoet:
  * via `dlopen(RTLD_LOCAL)`: `[ok ] hasp_login staat niet in de globale naamruimte` ✔
  * met dezelfde emulator in `LD_PRELOAD`: `[let] hasp_login staat WEL in de globale naamruimte` ✘
* En `halo.so` exporteert zelf **10** `hasp_*`-namen. Zet je de emulator in `LD_PRELOAD`, dan zijn er
  in één proces **twee leveranciers** van `hasp_login`, en wie wint hangt van de laadorde af.

Daarom is de emulator in dit voorstel een **bestand in de sandbox dat de hook opzoekt**
(`RAVENGUN_HASP_ELF_LIB=/rg/hasp/ravengun-hasp-x64.so`), en **geen** regel in `preload`.

---

## 2. De diff, per blok

Uitgangspunt: het **gecommitte** bestand, commit `ccd52fe`. De Halo-tak staat op regel 8 en heeft nu
`libdirs` (1 bron), `preload`, `exe`, `cwd`, `home`, `args`, `route`, `libs` (1 regel), `extra_ro`.
Er is geen `env`-blok. Er hoeft niets weg; dit is **alleen toevoegen**, op twee plekken en één nieuw
blok.

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

Plek: **direct ná** het `@data/runtime/lib-fmodstub`-object, binnen dezelfde `libdirs`-lijst.

### Blok 2 — `libs`: één regel toevoegen, achter de bestaande

```diff
       "libs": [
-        "@build/librg_rt_input.so"
+        "@build/librg_rt_input.so",
+        "/home/deck/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook-x64.so"
       ],
```

Plek: **direct ná** `"@build/librg_rt_input.so"`, met een komma erachter.

### Blok 3 — `env`: een nieuw blok, tussen `libs` en `extra_ro`

```diff
       "route": "tp-shm",
       "libs": [
         "@build/librg_rt_input.so",
         "/home/deck/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook-x64.so"
       ],
+      "env": {
+        "RAVENGUN_HASP_DIR": "/rg/hasp",
+        "RAVENGUN_HASP_GAME": "Halo Fireteam",
+        "RAVENGUN_HASP_ELF_LIB": "/rg/hasp/ravengun-hasp-x64.so",
+        "RAVENGUN_HASP_ELF_GAME": "/pm/g7/halo/game",
+        "RAVENGUN_HASP_ELF_LOG": "/pm/g7/halo/hasphook.log",
+        "RAVENGUN_HASP_ELF_UIT": "1"
+      },
       "extra_ro": [
         "/dev/shm/TeknoParrot_JvsState"
       ]
```

Plek: **tussen** `libs` en `extra_ro`. `play-native.sh` leest `env` als `GENV` en past die toe
**ná** `LD_PRELOAD` (regel 331 van de huidige `scripts/play-native.sh`); dit blok overschrijft
`LD_PRELOAD` dus niet — het zet er alleen variabelen bij. **[gemeten in de droogloop, §4]**

### Blok 4 — `preload`: **NIET wijzigen**

```diff
       "preload": "/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so",
```

Blijft exact zo. Reden: §1(a) — onze hook vindt in Halo nul hasp-symbolen, dus in `preload` zetten
zou een configuratie zijn die liegt. Zodra de adreslijst er is, is dit de verwisseling:

```diff
-      "preload": "/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so",
+      "preload": "/rg/lib/librg_rt_input.so:/rg/lib/libravengun-hasphook-x64.so",
```
en vervalt `RAVENGUN_HASP_ELF_UIT` uit blok 3. Dat is één regel plus één regel, en `halo.so` blijft
op zijn plek staan als bestand (een verwisseling, geen sloop).

---

## 3. Reden per regel

| regel | waarom precies dit |
|---|---|
| `"src": "@data/hasp"` | De emulator én de Halo-donglegegevens staan al geïnstalleerd in `~/.local/share/ravengun/hasp/`; `@data` is een bestaande placeholder, dus geen nieuw aanknopingspunt en geen absoluut hostpad in de sandbox. |
| `"dst": "/rg/hasp"` | Vrije, korte naam in de sandbox waar de hook en `dongle.c` naartoe kunnen wijzen. |
| de `note` erbij | Waarschuwt de volgende lezer dat de emulator hier **niet** voor `LD_PRELOAD` ligt — dat is de fout die §1(b) beschrijft. |
| `libs` + de hook | `libs` bindt elk bestand op `/rg/lib/<naam>` en zet `/rg/lib` in `LD_LIBRARY_PATH`; zonder deze regel kan de verwisseling niet, want die verwijst naar `/rg/lib/…`. Vandaag wordt de hook **niet** geladen. |
| `RAVENGUN_HASP_DIR=/rg/hasp` | `dongle.c` zoekt `<DIR>/<naam>/dongle.json` (en heeft een terugval op `<DIR>/<naam>/dropin/ravengun-hasp/<naam>`). |
| `RAVENGUN_HASP_GAME="Halo Fireteam"` | De **mapnaam** en het veld `game` in `dongle.json` — met spatie. Let op: de ElfLdr2-drop-in gebruikte `Halo` (dat werkt via de terugval op de patches-repo-indeling); in de native route met de geïnstalleerde indeling moet het `Halo Fireteam` zijn. |
| `RAVENGUN_HASP_ELF_LIB=/rg/hasp/ravengun-hasp-x64.so` | De hook heeft een standaardpad naar een **hostpad** (`/home/deck/.local/share/ravengun/hasp/…`) dat in de sandbox niet bestaat; expliciet is hier beter dan leunen op `LD_LIBRARY_PATH`. |
| `RAVENGUN_HASP_ELF_GAME=/pm/g7/halo/game` | De hook leest het spelbeeld van schijf voor zijn dynsym; zonder deze regel valt hij terug op `/proc/self/exe` (in de game hetzelfde bestand, maar dan werkt hij niet meer in een hulpproces). |
| `RAVENGUN_HASP_ELF_LOG=/pm/g7/halo/hasphook.log` | `/pm/g7/halo` is een **overlay**, dus dit logboek landt onder `~/.local/share/ravengun/native/Halo/<n>/upper/` en niet op de NAS. |
| `RAVENGUN_HASP_ELF_UIT=1` | Vangrail: zolang de hook Halo's ingangspunten niet kan vinden, mag hij niet patchen maar alleen meten. Verdwijnt bij de verwisseling. |
| `preload` onveranderd | §1(a): geen regel in de configuratie zetten die niets doet. |

---

## 4. Het droogloop-bewijs (geen spelstart)

Reproduceerbaar met `bash ~/projects/ravengun-patches/patches/Halo/probe/proposal_dryrun.sh`
(het script bouwt een **kladboom** in `/tmp/rg-proposed` met het gecommitte bestand + dit voorstel,
en raakt `packaging/` in de repo niet aan).

**1. `play-native.sh --dry-run` tegen het voorstel: exitcode 0, en dit is wat er langskomt**

```
--ro-bind … /rg/libfmod
--ro-bind … /rg/hasp
--ro-bind …/librg_rt_input.so  /rg/lib/librg_rt_input.so
--ro-bind …/libravengun-hasphook-x64.so  /rg/lib/libravengun-hasphook-x64.so
--setenv LD_LIBRARY_PATH /rg/libfmod:/rg/hasp:/rg/lib
--setenv LD_PRELOAD      /rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so
--setenv RAVENGUN_HASP_DIR        /rg/hasp
--setenv RAVENGUN_HASP_GAME       Halo Fireteam
--setenv RAVENGUN_HASP_ELF_LIB    /rg/hasp/ravengun-hasp-x64.so
--setenv RAVENGUN_HASP_ELF_GAME   /pm/g7/halo/game
--setenv RAVENGUN_HASP_ELF_LOG    /pm/g7/halo/hasphook.log
--setenv RAVENGUN_HASP_ELF_UIT    1
… /pm/g7/halo/game -f1920x1080 -n
```

Let op de twee dingen die het bewijs levert: `LD_PRELOAD` is **onveranderd** (de `env`-regels
overschrijven het niet), en alle paden bestaan — `--dry-run` weigert namelijk met een `die` als een
`mounts`-, `files`-, `libdirs`- of `libs`-bron ontbreekt.

**2. De drie ketens onder `/bin/ls`, met precies dezelfde `LD_PRELOAD` en omgeving**

| keten | exitcode | botsingen |
|---|---|---|
| `librg_rt_input.so : libravengun-hasphook-x64.so` | 0 | 0 |
| `halo.so : librg_rt_input.so : libravengun-hasphook-x64.so` | 0 | 0 |
| `ravengun-hasp-x64.so : librg_rt_input.so : libravengun-hasphook-x64.so` | 0 | 0 |

Geen `symbol lookup error`, geen `cannot open shared object`, geen crash. Dat dit veilig is, komt
doordat `librg_rt_input.so` een expliciete poort op `/proc/self/exe` heeft (eindigt de naam niet op
`/game`, dan doet zijn constructor niets — uit de bron) en de hook met `RAVENGUN_HASP_ELF_UIT=1`
alleen meet.

**3. `ldd` — de lader, ongewijzigd**

```
$ LD_LIBRARY_PATH=~/.local/share/ravengun/runtime/lib-fmodstub \
    ldd '…/Halo Fireteam/g7/halo/game' | grep -c 'not found'
0
```

**4. De koppeling hook ↔ emulator** (`probe/hook-emulator-coupling.c`): alle elf namen oplosbaar via
de `RTLD_LOCAL`-handle, en `hasp_login` **niet** in de globale naamruimte.

**5. De dongle end-to-end** (`hasp/src/selftest.c` tegen `~/.local/share/ravengun/hasp/Halo Fireteam`):
`GESLAAGD — 0 fout(en)`.

---

## 5. Wat er in de huidige versie staat dat moet blijven

Alles. Dit voorstel verwijdert niets en wijzigt geen bestaande waarde. Expliciet, zodat een
samenvoeging ze niet per ongeluk opruimt:

| bestaand element | blijft |
|---|---|
| `"preload": "/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so"` | **ja, ongewijzigd** (de verwisseling is een aparte, latere stap) |
| `libdirs` → `@data/runtime/lib-fmodstub` → `/rg/libfmod` | ja, en blijft de **eerste** van de twee zijn |
| `libs` → `"@build/librg_rt_input.so"` | ja, en blijft de **eerste** van de twee zijn |
| `mounts` (overlay `/pm/g7/halo`, ro `/pm/g7/dump`) | ja |
| `files` (`rtinit_original`, `run_game.sh_original`) | ja |
| `exe`/`cwd`/`home` (`/pm/g7/halo`), `args` (`-f{res}`, `-n`), `route` (`tp-shm`) | ja |
| `extra_ro` → `/dev/shm/TeknoParrot_JvsState` | ja; `env` komt **vóór** dit blok |
| `enabled: false`, `status: "geparkeerd"` | ja — dit voorstel verandert de status niet |

---

## 6. Wat er misgaat als de volgorde fout is

Dit is de reden dat de regels bestaan, en per geval is het meetbaar:

1. **De emulator in `LD_PRELOAD` zetten** (in plaats van hem door de hook te laten `dlopen`-en):
   `hasp_login` komt in de **globale** naamruimte — gemeten, §1(b). Met `halo.so` erbij (dat zelf 10
   `hasp_*`-namen exporteert) zijn er dan twee leveranciers van dezelfde naam, en wie wint hangt van
   de laadorde af. Dit is de fout die de `note` in blok 1 moet voorkomen.
2. **Onze hook vóór `halo.so` in `preload` zetten, met beide actief**: beide patchen dezelfde
   functies, en de laatstgepatchte wint; de verliezer zijn trampoline wijst dan naar de winnaar —
   een ketting door twee emulators. Daarom is het devies "nooit twee hooks tegelijk op dezelfde
   adressen", en daarom staat de hook vandaag **helemaal niet** in `preload`.
3. **De hook in `preload` zetten terwijl `RAVENGUN_HASP_ELF_UIT` ontbreekt**: hij vindt niets, dus
   er gebeurt niets, maar hij start wel een kijker-thread die tot 90 s (`RAVENGUN_HASP_ELF_WACHT`)
   blijft herscannen. Zonder de vangrail is dat ruis in een meting; met de vangrail is het één regel.
4. **`RAVENGUN_HASP_ELF_LIB` weglaten**: de hook valt terug op
   `/home/deck/.local/share/ravengun/hasp/ravengun-hasp-x64.so`, een **hostpad** dat in de sandbox
   niet bestaat. Het logboek zegt dan `GEEN emulator kunnen laden` en elke vervanging valt terug op
   de originele code — stil, zonder crash.
5. **`RAVENGUN_HASP_GAME` op `Halo` in plaats van `Halo Fireteam`**: `dongle.c` zoekt dan
   `<DIR>/Halo/dongle.json` en `<DIR>/Halo/dropin/ravengun-hasp/Halo`; met `/rg/hasp` als wortel
   bestaat geen van beide en er wordt **geen** dongle geladen. Dit is de meest waarschijnlijke
   vergissing, want in de ElfLdr2-drop-in is `Halo` juist wél goed.
6. **`@data/hasp` **na** `libs` zetten in plaats van in `libdirs`**: dan komt `/rg/hasp` niet in
   `LD_LIBRARY_PATH`, wat voor `RAVENGUN_HASP_ELF_LIB` niet uitmaakt (dat is een volledig pad) maar
   wel voor een latere `dlopen("ravengun-hasp-x64.so")` zonder pad.
7. **`env` vóór `libs` zetten**: onschadelijk voor de uitkomst, maar het breekt de leesorde van het
   bestand (elke andere game zet `route` → `libs` → `extra_ro`), en dat maakt diffs onnodig groot.

---

## 7. Wat hier niet in zit

* Geen wijziging aan `scripts/play-native.sh`. Twee dingen zouden het bestand netter maken en zijn
  **niet** voorgesteld omdat ze de launcher raken: een placeholder `@patches` (dan hoeft blok 2 geen
  absoluut `/home/deck`-pad te bevatten), en het honoreren van `RG_DROPIN_PRELOAD`
  (`docs/hasp-layer.md` §9 punt 2). Beide zijn een aparte afweging.
* Geen verwisseling van `halo.so`. Die is één regel en staat in §2, blok 4, met zijn voorwaarde.
* Geen wijziging aan `hasp/` of `tools/rt-hasphook/`. De adres-terugval die de verwisseling mogelijk
  maakt is daar een wijziging van de laderbouwer, niet van mij; de invoer (de zeven adressen) staat
  in §1(a).
