# Halo: Fireteam Raven — onze eigen HASP-laag in de native route

*RavenGun, ronde "loader-specialist Halo", 2026-10-12. Bench-vrij gemeten: er is geen spel
gestart, de NAS is alleen gelezen, `halo.so` is niet aangeraakt.*

Dit document hoort bij het **voorstel** voor `packaging/native-games.json` dat in
`NATIVE-RECIPE-PATCH.md` staat (met de diff per blok en de droogloop). Het bestand
`packaging/native-games.json` zelf staat op zijn gecommitte basis; de coördinator zet het voorstel
erin. Hier staat het waarom en het bewijs; daar staat de diff.

---

## 1. Wat er in het recept moet (voorstel)

Zie `NATIVE-RECIPE-PATCH.md` §2 voor de exacte regels en de plek. Samengevat:

```json
"libdirs": [
  { "src": "@data/runtime/lib-fmodstub", "dst": "/rg/libfmod",
    "note": "FMOD 1.10 ontbreekt (propriëtair); de stub geeft geen geluid" },
  { "src": "@data/hasp", "dst": "/rg/hasp",
    "note": "de HASP-emulator + de donglegegevens. De inline-hook laadt de emulator hiervandaan
             met dlopen(RTLD_LOCAL), niet met LD_PRELOAD." }
],
"preload": "/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so",
"libs": [
  "@build/librg_rt_input.so",
  "/home/deck/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook-x64.so"
],
"env": {
  "RAVENGUN_HASP_DIR":      "/rg/hasp",
  "RAVENGUN_HASP_GAME":     "Halo Fireteam",
  "RAVENGUN_HASP_ELF_LIB":  "/rg/hasp/ravengun-hasp-x64.so",
  "RAVENGUN_HASP_ELF_GAME": "/pm/g7/halo/game",
  "RAVENGUN_HASP_ELF_LOG":  "/pm/g7/halo/hasphook.log",
  "RAVENGUN_HASP_ELF_UIT":  "1"
}
```

`preload` is **niet** veranderd: `halo.so` blijft staan. Dit is de verwisseling nog niet, het is het
gereedschap klaarzetten. Zie §3 en §4. Dat `preload` ongemoeid blijft, is een meting en geen
omissie: zie §3.

### Reden per element

| element | waarom precies dit |
|---|---|
| `libdirs` → `@data/hasp` → `/rg/hasp` | De emulator **en** de donglegegevens staan al geïnstalleerd in `~/.local/share/ravengun/hasp/` (`ravengun-hasp-x64.so`, `ravengun-hasp.so`, `Halo Fireteam/{dongle.json,memory/*}`). `@data` is het bestaande aanknopingspunt in `play-native.sh`, dus dit kost geen nieuwe placeholder en geen absolute pad in de sandbox. |
| `env.RAVENGUN_HASP_DIR=/rg/hasp` | `dongle.c` zoekt `<DIR>/<naam>/dongle.json`, en heeft daarnaast een terugval op `<DIR>/<naam>/dropin/ravengun-hasp/<naam>` (regel 388-393 van `hasp/src/dongle.c`). Met `/rg/hasp` klopt de geïnstalleerde indeling. |
| `env.RAVENGUN_HASP_GAME="Halo Fireteam"` | **Let op:** dit is de mapnaam én het veld `game` in `dongle.json` (met spatie), niet het profiel `Halo`. De drop-in onder ElfLdr2 gebruikte `Halo` (dat werkt via de terugval op de patches-repo-indeling); in de native route gebruiken we de geïnstalleerde indeling, dus `Halo Fireteam`. |
| `env.RAVENGUN_HASP_ELF_LIB=/rg/hasp/ravengun-hasp-x64.so` | De hook laadt de emulator zelf. Hij heeft een standaardpad (`/home/deck/.local/share/ravengun/hasp/ravengun-hasp-x64.so` — een absoluut pad naar de host, dat in de sandbox niet bestaat) en de naam `ravengun-hasp-x64.so`. Expliciet is hier beter dan leunen op `LD_LIBRARY_PATH`. |
| `env.RAVENGUN_HASP_ELF_GAME=/pm/g7/halo/game` | De hook leest het spelbeeld van schijf voor zijn dynsym. Het standaardpad is `/proc/self/exe`, en dat is in de game hetzelfde bestand — maar expliciet maken laat de hook ook werken als hij in een hulp-proces belandt. |
| `env.RAVENGUN_HASP_ELF_LOG=/pm/g7/halo/hasphook.log` | Relatief aan de werkmap zou ook werken; absoluut in de overlay (`/pm/g7/halo`, overlay-modus) betekent dat het logboek in `~/.local/share/ravengun/native/Halo/<n>/upper/` landt en niet op de NAS. |
| `env.RAVENGUN_HASP_ELF_UIT=1` | **Vangrail.** Zolang onze hook Halo's ingangspunten niet kan vinden (§3) mag hij niet proberen te patchen. `UIT` laat hem alleen meten en loggen. Verdwijnt bij de verwisseling (§4). |
| `libs` → de hook | `libs` bindt elk bestand op `/rg/lib/<naam>` en zet `/rg/lib` in `LD_LIBRARY_PATH`. Zonder deze regel kan de verwisseling niet: `preload` verwijst naar `/rg/lib/...`. De hook is vandaag **niet** vooraf geladen, dus deze regel doet nog niets. |
| `preload` onveranderd | Zie §3: onze hook vindt in Halo **nul** hasp-symbolen. Iets in `preload` zetten dat niets doet, is een leugen in de configuratie. |

### Waarom deze volgorde (de vraag die er letterlijk lag)

* **`librg_rt_input.so` interponeert niets.** `nm -D --defined-only build/librg_rt_input.so` geeft
  **nul** dynamische symbolen. Hij doet zijn werk in een constructor en hangt daarna aan een thread.
  Zijn plaats in `LD_PRELOAD` ten opzichte van de rest is dus vrij. **[gemeten]**
* **Onze hook interponeert ook niets.** `nm -D --defined-only libravengun-hasphook-x64.so` geeft
  precies twee namen: `la_version` en `la_preinit`. Hij werkt door code in het spelbeeld te
  herschrijven, niet door symbolen over te nemen. Ook hier: geen volgordeeis. **[gemeten]**
* **De emulator hoort *niet* in `LD_PRELOAD`, en dat is de enige echte volgordevraag.** Hij levert
  de symbolen (25 `hasp_*`-exporten) en moet in een **eigen** naamruimte blijven. Gemeten:
  * via de hook, met `dlopen(RTLD_LOCAL)`: `hasp_login` staat **niet** in de globale naamruimte ✔
  * met de emulator in `LD_PRELOAD`: `hasp_login` staat **WEL** in de globale naamruimte ✘
  * en `halo.so` exporteert er zelf al **10** (`hasp_login`, `hasp_read`, …). Twee leveranciers van
    dezelfde naam in één proces is precies wat de hook met `RTLD_LOCAL` voorkomt.
  **[alle drie gemeten, zie §5]**
* **Als zowel `halo.so` als onze hook zouden patchen**, bepaalt de `LD_PRELOAD`-volgorde wie als
  laatste patcht en dus wint, en de verliezer zijn trampoline wijst naar de winnaar — een ketting
  door twee emulators. Daarom is de verwisseling een **verwisseling**: nooit twee hooks tegelijk op
  dezelfde adressen. Vandaag is dat geen probleem omdat onze hook niets vindt (§3).
* Om dezelfde reden is de hook **laatst** gezet in de `preload`-regel van §4: hij moet op een
  volledig opgestart proces patchen, niet er middenin.

---

## 2. Hoe je dit herhaalt

Alles hieronder is bench-vrij en start het spel niet.

```bash
# 1. laadt alles, en botst er niets?  (drie ketens, meten-only)
bash ~/projects/ravengun-patches/patches/Halo/probe/task1_dryrun.sh

# 2. het recept: geldige JSON, alle paden bestaan, en het echte bwrap-commando
cd ~/projects/ravengun && scripts/play-native.sh --game Halo --dry-run

# 3. de koppeling hook <-> emulator (elf namen, RTLD_LOCAL, geen lek naar globaal)
cc -Wall -O2 -o /tmp/coupling \
   ~/projects/ravengun-patches/patches/Halo/probe/hook-emulator-coupling.c -ldl
/tmp/coupling ~/.local/share/ravengun/hasp/ravengun-hasp-x64.so

# 4. de dongle zelf, end-to-end (emulator + de geïnstalleerde Halo-gegevens)
cc -std=c99 -O2 -o /tmp/hasp-selftest \
   ~/projects/ravengun/hasp/src/selftest.c ~/projects/ravengun/hasp/src/dongle.c
/tmp/hasp-selftest "$HOME/.local/share/ravengun/hasp/Halo Fireteam"     # GESLAAGD — 0 fout(en)

# 5. de lader: 0 ontbrekende bibliotheken
LD_LIBRARY_PATH=~/.local/share/ravengun/runtime/lib-fmodstub \
  ldd "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game" | grep -c 'not found'
```

---

## 3. Wat er niet in paste, en wat er nu voor in de plaats is

> **Bijgewerkt.** Dit hoofdstuk beschreef eerst een gat: de hook vond in Halo niets, dus hij hoorde
> niet in `preload`. Dat gat is nu gedicht met een **generieke adres-route**; zie
> `NATIVE-RECIPE-PATCH.md` §1(a) en §4 voor het bewijs en de nieuwe receptregels. De meting hieronder
> blijft staan omdat ze de reden is dat de adres-route bestaat.

Onze hook vond zijn doelen **uitsluitend op naam** in de dynsym van het spel
(`hh_verzamel()` en `hh_haak_alles()` in `tools/rt-hasphook/hasp_inline_hook.c`; de vergelijking was
`strcmp(naam, hh_doelen[d].naam)`). Voor Halo is die lijst leeg:

```
$ RAVENGUN_HASP_ELF_GAME='.../Halo Fireteam/g7/halo/game' RAVENGUN_HASP_ELF_UIT=1 \
    LD_PRELOAD=librg_rt_input.so:libravengun-hasphook-x64.so /bin/ls
[ravengun-hasphook] spel-ELF gelezen uit .../g7/halo/game (RAVENGUN_HASP_ELF_GAME): 33554432 bytes, 9 program headers
[ravengun-hasphook] aantal symbolen uit DT_HASH: 220
[ravengun-hasphook] dynsym: 220 symbolen
[ravengun-hasphook] 0 hasp-symbolen in de dynsym van de game (gedefinieerd in de game zelf)
[ravengun-hasphook] RAVENGUN_HASP_ELF_UIT gezet: alleen gemeten, niets aangehaakt
```

Dat is geen verrassing maar de bevestiging van de statische meting: `game` heeft 220 dynsym-
symbolen, waarvan 182 ongedefinieerd en **allemaal** libc/pthread/socket/dl; er is geen enkel
`hasp_*`, en de byte-sequentie `hasp_login` komt **nul keer** in het hele bestand voor. **[gemeten]**

De kop van `hasp_inline_hook.c` zegt "`nm -D game` → wél `T hasp_login`". Voor Halo klopt dat niet, en
voor Jurassic Park's `game` ook niet (gemeten: ook nul hasp-namen in de dynsym). De conclusie in
`docs/hasp-layer.md` §0 dat de aanroepen statisch zijn klopt; de route "naam uit de dynsym" werkt
alleen waar die namen er nog zijn, en dat is bij deze familie nergens.

**Wat er nu is.** Een tweede weg naast de naam-route: de hook leest een **adreslijst per game** uit de
patches-repo (`patches/Halo/hasp-haken.ini`), keurt elk adres (uitvoerbaar `PT_LOAD`, binnen
`p_filesz`, herkenbare prologue, plus `grootte`/`ingang` als versiecontrole) en haakt dan aan.
Gemeten op een echt gemapt beeld: **7 van 7 aangehaakt in dezelfde run waarin de naam-route nul vond.**
De naam-route blijft de standaard en blijft werken (zelftest: 10 van 11, emulator antwoordt met greep
`0xE6F`). Zie `NATIVE-RECIPE-PATCH.md` §1(a), §4 en §6.

### De zeven adressen, met hun naam

`halo.so` loste hetzelfde probleem op met vaste adressen. Zijn `initialize()` op `0x569f` roept
**zeven keer** `flh_inline_hook` aan en schrijft de retourwaarde meteen in de bijbehorende
`real_hasp_*`-gleuf; de disassembly geeft dus de **naam** bij elk adres: **[gemeten]**

| # | adres in `game` | naam (uit de `real_hasp_*`-gleuf) | eerste bytes (prologue) |
|---|---|---|---|
| 1 | `0x00B2D630` | `hasp_login` | `55 41 57 41 56 41 55 41 54 53 48 81 ec 28 02 00` |
| 2 | `0x00B2DD48` | `hasp_logout` | `55 41 57 41 56 41 55 41 54 53 48 83 ec 28 89 fb` |
| 3 | `0x00B2E1AA` | `hasp_encrypt` | `55 41 57 41 56 41 54 53 48 83 ec 10 41 89 d6 49` |
| 4 | `0x00B2E391` | `hasp_free` | `53 48 89 fb e8 72 9c 00 00 e8 a1 a4 00 00 48 89` |
| 5 | `0x00B2F5AC` | `hasp_get_sessioninfo` | `55 41 57 41 56 41 55 41 54 53 48 83 ec 78 48 89` |
| 6 | `0x00B2FD63` | `hasp_read` | `55 41 57 41 56 41 55 41 54 53 48 83 ec 18 4d 89` |
| 7 | `0x00B2FE60` | `hasp_write` | `55 41 57 41 56 41 55 41 54 53 48 83 ec 38 89 4c` |

Alle zeven beginnen met een normale x86-64-functieprologue en liggen in segment 1
(`0x400000..0x1462000`), dus binnen het bereik van het spelbeeld. Dit zijn de statisch gelinkte
Sentinel-LDK-ingangspunten van Halo. **[gemeten]**

`hasp_login_scope`, `hasp_decrypt`, `hasp_get_size` en `hasp_get_info` staan niet in de lijst:
`halo.so` haakt die vier niet aan, dus er is geen gemeten adres. De hook meldt ze na het aanhaken
apart (`niet aangehaakt: …`), zodat dat gat zichtbaar blijft.

**De wijziging is gemaakt** (in `tools/rt-hasphook/hasp_inline_hook.c`, met de gegevens in
`patches/Halo/hasp-haken.ini` — niet in de lader-repo): de hook leest een adreslijst, keurt elk adres
(uitvoerbaar `PT_LOAD`, binnen `p_filesz`, herkenbare prologue, plus `grootte`/`ingang` als
versiecontrole) en haakt dan aan. De naam-route blijft de standaard. Bench-vrij bewijs: 7 van 7
aangehaakt op een echt gemapt beeld waar de naam-route nul vond, en drie gemeten weigeringen bij een
fout adres of een verkeerde build. Zie `NATIVE-RECIPE-PATCH.md` §1(a) en §4. **[gemeten]**

Ook gerepareerd: de naam-route rekende `hh_base + st_value` en telde daarmee bij een ET_EXEC het
laagste segmentadres dubbel (Halo `hh_laag = 0x400000`, Jurassic Park `0x08048000`). Het is nu
`hh_base - hh_laag + st_value`, en dat is gemeten op een PIE- én een `-no-pie`-zelftest.

---

## 4. De verwisseling, wanneer je hem doet

Eén regel in het `Halo`-profiel, en niets aan `halo.so` zelf:

```diff
-      "preload": "/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so",
+      "preload": "/rg/lib/librg_rt_input.so:/rg/lib/libravengun-hasphook-x64.so",
```

Daar hoort de rest van het voorstel bij (de `@data/hasp`-bron, de hook in `libs`, het `env`-blok met
`RAVENGUN_HASP_ELF_ADRESLIJST`, en de adreslijst in `extra_ro`); zie `NATIVE-RECIPE-PATCH.md` §2.

**Terugrolen** is die ene regel terugzetten; `halo.so` is nooit gewijzigd of verwijderd en zit nog
steeds in de overlay op `/pm/g7/halo/halo.so`.

**Wat er misgaat als je de volgorde of de kanalen verkeerd zet** — concreet:

1. **De emulator in `LD_PRELOAD`** zet `hasp_login` (en 24 andere) in de globale naamruimte.
   Gemeten: §5, test 4. Met `halo.so` erbij zijn er dan **twee** leveranciers van dezelfde naam; wie
   wint hangt van de laadorde af, en de verliezer kan de winnaar niet meer bereiken.
2. **Beide hooks actief** (onze hook én `halo.so`, elk met hun eigen trampoline naar "het origineel")
   levert een ketting van twee vervangingen op. Wie als laatste patcht, ziet de ander als
   "origineel". Dat is geen crash maar wel een onvoorspelbare uitkomst; vermijd het.
3. **Onze hook vóór `halo.so` in `preload`** met beide actief: idem, en dan patcht `halo.so` als
   laatste — dus TeknoParrot's emulator antwoordt en onze eigen laag is voor niets geladen.
4. **`RAVENGUN_HASP_ELF_LIB` vergeten**: de hook valt terug op
   `/home/deck/.local/share/ravengun/hasp/ravengun-hasp-x64.so`, een **hostpad dat in de sandbox
   niet bestaat** (de sandbox heeft alleen `/rg/hasp` en `/rg/lib`). Het logboek zegt dan
   `GEEN emulator kunnen laden` en alle vervangingen vallen terug op de originele code.
5. **`RAVENGUN_HASP_GAME` verkeerd** (`Halo` in plaats van `Halo Fireteam`): `dongle.c` zoekt dan
   `<DIR>/Halo/dongle.json` en via de terugval `<DIR>/Halo/dropin/ravengun-hasp/Halo`; met
   `/rg/hasp` als wortel bestaat geen van beide, en de emulator laadt **geen** dongle (hij zegt dat
   op stderr). Let op het verschil met de ElfLdr2-drop-in, waar `Halo` juist wél klopt.

---

## 5. Het droogloop-bewijs (alles hier is gemeten, zonder het spel te starten)

Draaien: `bash ~/projects/ravengun-patches/patches/Halo/probe/task1_dryrun.sh` en
`bash ~/projects/ravengun-patches/patches/Halo/probe/task1_verify.sh`.

**a. De drie ketens laden, en er botst niets.** Onder `/bin/ls` (veilig: `librg_rt_input.so` heeft
een expliciete poort op `/proc/self/exe` die eindigt op `/game` — uit de bron — en de hook staat in
meet-stand met `RAVENGUN_HASP_ELF_UIT=1`):

| keten | `LD_PRELOAD` | exitcode | logboek |
|---|---|---|---|
| C1 | `librg_rt_input.so : libravengun-hasphook-x64.so` | **0** | 220 dynsym-symbolen, 0 hasp |
| C2 | `halo.so : librg_rt_input.so : libravengun-hasphook-x64.so` | **0** | idem |
| C3 | `ravengun-hasp-x64.so : librg_rt_input.so : …hasphook…` | **0** | idem |

Geen enkele keten geeft een `symbol lookup error`, een `cannot open shared object` of een crash.

**b. Het recept ontleedt en alle paden bestaan.** `scripts/play-native.sh --game Halo --dry-run`
geeft **exitcode 0** en dit commando (ingekort):

```
bwrap … --ro-bind … /rg/libfmod … --ro-bind … /rg/hasp \
        --ro-bind …/librg_rt_input.so /rg/lib/librg_rt_input.so \
        --ro-bind …/libravengun-hasphook-x64.so /rg/lib/libravengun-hasphook-x64.so \
        … --setenv LD_LIBRARY_PATH /rg/libfmod:/rg/hasp:/rg/lib \
        --setenv LD_PRELOAD /rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so \
        --setenv RAVENGUN_HASP_DIR /rg/hasp --setenv RAVENGUN_HASP_GAME "Halo Fireteam" \
        --setenv RAVENGUN_HASP_ELF_LIB /rg/hasp/ravengun-hasp-x64.so \
        --setenv RAVENGUN_HASP_ELF_GAME /pm/g7/halo/game \
        … /pm/g7/halo/game -f1920x1080 -n
```

**c. De koppeling hook ↔ emulator.** `probe/hook-emulator-coupling.c` doet exact wat
`hh_laad_emulator()` + `hh_koppel_emulator()` doen:

```
[ok ] emulator met RTLD_LOCAL geladen: …/hasp/ravengun-hasp-x64.so
[ok ] hasp_login … hasp_free   (elf namen, alle elf)
[ok ] hasp_login staat niet in de globale naamruimte
GESLAAGD — 0 ontbrekend(e) naam/en
```

**d. En het tegenbewijs voor de regel "niet vooraf laden":** met dezelfde emulator in `LD_PRELOAD`
zegt dezelfde test `[let] hasp_login staat WEL in de globale naamruimte`.

**e. De dongle zelf, end-to-end** (`hasp/src/selftest.c` tegen de geïnstalleerde gegevens):
`GESLAAGD — 0 fout(en)`, met `dongle 'Halo Fireteam' geladen uit …/hasp/Halo Fireteam
(2 geheugenbestand(en), haspid 748675309)`, greep `0xE6F`, 112-byte image die met `2c 03 ab 63`
begint.

**f. De lader, ongewijzigd:** `ldd` met de FMOD-stub geeft **0** `not found`-regels.

---

## 6. Wat dit niet is

Dit is **geen** werkende vervanging van `halo.so` voor Halo. Het is: de emulator en de donglegegevens
staan in de sandbox op een plek die de hook kan vinden, met de juiste omgevingsvariabelen, bewezen
met een droogloop; en de enige ontbrekende schakel is met naam en adres benoemd. `halo.so` blijft
tot die schakel er is de dongle-antwoord van deze game.
