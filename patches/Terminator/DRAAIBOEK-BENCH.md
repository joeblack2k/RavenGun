# DRAAIBOEK — bench run for Terminator Salvation via our own loader

**Status: NOT RUN. Do not run this until the bench is free and the owner's screen is released.**
The bench takes one game at a time and this run brings up a window.

> **Er is nu een uitvoerbaar script dat dit hele draaiboek doet: `bash loader-run.sh`.**
> Zonder argument doet het fase 0 en 1 (preconditions + bewijs van de reparatie + droogloop) en start
> het **niets**; met `--start` doet het ook de echte run. Zie §8 voor wat al bewezen is en §9 voor de
> twee dingen die de laderbouwer nog moet weten.

**Purpose.** Prove, end to end and unambiguously, that `tools/rg-elfloader/` loads this game and reaches
the guest's own `main` after the `DT_HASH` fix — and that the next obstacle is the game's own dongle
check, not our loader.

**What this run does NOT do.** It does not attempt any dongle, HASP, keychip, integrity or checksum
work. When the guest stops on `DongleInitialize() FAILED!` that is a **success** for this run: it means
the loader is finished and the next problem is elsewhere.

---

## 0. Preconditions (check these first, they are cheap)

| # | check | command (on the Deck) | required result |
|---|---|---|---|
| 0.1 | bench free | `pgrep -af 'wine\|BudgieLoader\|rg-elfloader\|bowling_usa\|gamescope\|Terminator' \| grep -v pgrep` | nothing from another game |
| 0.2 | the benchmark game of this round is not running | same as 0.1 | — |
| 0.3 | NAS readable | `ls -l "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"` | 7,764,284 bytes |
| 0.4 | loader built **with the fix** | `grep -n 'DT_GNU_HASH\|strtab - (uint32_t)symtab' ~/projects/ravengun/tools/rg-elfloader/rg_elfloader.c` | shows the new code, **and** the binary is newer than the source: `stat -c '%y %n' ~/projects/ravengun/tools/rg-elfloader/rg_elfloader{,.c}` |
| 0.5 | loader is still 32-bit | `file ~/projects/ravengun/tools/rg-elfloader/rg-elfloader` | `ELF 32-bit LSB pie executable, Intel i386` |
| 0.6 | libraries resolve | `LD_LIBRARY_PATH=<ElfLdr2/libs>:<runtime/lib32> ldd "…/game"` | **no** `not found` line |

`<ElfLdr2/libs>` = `/home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs`
`<runtime/lib32>` = `/home/deck/.local/share/ravengun/runtime/lib32`
(`libglut.so.3` lives in the **second** one; with both on the path nothing is missing.)

**Run on a throwaway copy of the guest, never on the NAS file.**
The loader only reads it, but a copy keeps the NAS untouched and makes the run reproducible — and it
also keeps the guest's `argv[0]`/path out of the trailing-space problem:

```sh
mkdir -p /tmp/ts-bench && cd /tmp/ts-bench
cp "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game" ./game
sha256sum ./game
```

---

## 1. The run

**Working directory matters** — the engine looks for its data under absolute `/g3/…` paths, and the
guest's own relative lookups (`hasp/`, `T4User/`, `T4Data/`, `env/`) are relative to the cwd. Use the
view, which is the project's established way around the trailing-space folder name:

```sh
cd ~/.local/share/ravengun/runtime/views/Terminator
export LD_LIBRARY_PATH="/home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs:/home/deck/.local/share/ravengun/runtime/lib32"
export RG_ELFLOADER_VERBOSE=1
export DISPLAY=:0            # or leave it for the gamescope wrapper; the point is: exactly one display
mkdir -p /tmp/ts-bench/log
RG_ELFLOADER_VERBOSE=1 /home/deck/.local/share/ravengun/bin/rg-elfloader \
    /tmp/ts-bench/game 2>&1 | tee /tmp/ts-bench/log/run1.txt
```

Notes:
- `RG_ELFLOADER_VERBOSE=1` is what makes the outcome unambiguous; without it the loader prints only the
  `say()` lines and you lose the `dbg()` diagnostics that distinguish the failure modes in §3.
- Use the **installed** binary (`~/.local/share/ravengun/bin/rg-elfloader`) or the repo one
  (`~/projects/ravengun/tools/rg-elfloader/rg-elfloader`) — they are usually byte-identical; check with
  `md5sum` (0.4) so the log names the build you actually tested.
- If the loader is wired through `scripts/play-native.sh`, the equivalent is
  `scripts/play-native.sh --game Terminator --loader`; that path adds `gamescope` and a bubblewrap view.
  **Prefer the direct invocation above for the first run**, because the wrapper's extra layers make a
  failure harder to attribute. Do the wrapped run only after the direct run succeeds.

Wrap it in a timeout so a hang does not hold the bench:

```sh
timeout 90 /home/deck/.local/share/ravengun/bin/rg-elfloader /tmp/ts-bench/game 2>&1 | tee /tmp/ts-bench/log/run1.txt
echo "exit=$?"
```

---

## 2. What SUCCESS looks like (the unambiguous outcome)

The log must contain these four lines, in this order:

```
lib-defs: <N> gedefinieerde symbolen uit de NEEDED-libs gelezen
shim geladen: <M> gast-symbolen in de globale scope
geladen; spel /tmp/ts-bench/game krijgt argv[0]=/tmp/ts-bench/game
spring naar e_entry 0x8057240 (esp 0x……, argc …, envc …, <K> auxv)
```

with:

| field | expected value | meaning |
|---|---|---|
| `<N>` lib-defs | small (the GNU_HASH-only libs contribute nothing until step 4 of the plan) | `parse_lib_file()` ran; not a gate |
| `<M>` guest symbols | **in the low hundreds** (the guest defines 522 dynamic symbols; `shim_skip()` and `lib_defines()` remove some) | **this line existing at all is the proof that the `DT_HASH` fix works** |
| `e_entry` | **exactly `0x8057240`** | the right guest, the right build (it is permuted if the file was swapped) |
| `argc` | the number of args you passed +1-style accounting by the loader | the loader got its argv plumbing |
| `<K>` auxv | ~17-22 | the SysV start stack was built |

**Then the guest itself must take over and do this:**

1. Initialise GL/GLUT through the loader-supplied libraries and open a window (in this direct run, a
   window titled roughly *TeknoBudgie — Terminator Salvation* at about 1360x768; through `gamescope`,
   fullscreen).
2. Reach its own `main` and the dongle path.
3. Print its own text and exit **255** with the group
   `Begin HL-RUS License ---` / `DongleInitialize() FAILED!` / `End HL-RUS License ---`.

That paragraph is the success criterion for this run. It is exactly the same wall the project already
measured for the plain native route, and the same one TeknoParrot's closed core handles.

---

## 3. Reading a deviating outcome — first suspect per failure mode

| what you see | where it died | first suspect | what to check |
|---|---|---|---|
| `rg-elfloader: FATAAL: geen DT_HASH: kan het aantal gast-symbolen niet bepalen (shim)` | still in the loader, **before** `maak_shim()` produced anything | **the `DT_HASH` fix is not in the binary you ran** | 0.4: source has the new code; binary newer than source; and you ran that binary (`md5sum` it into the log). This is the single most likely explanation for this exact line. |
| `FATAAL: kan de shim niet bouwen: …` with a `gcc` error below it | `maak_shim()` | the generated `shim.c` did not compile — usually a symbol name that is not a legal assembler identifier, or the missing dev header `gnu/stubs-32.h` | set `RG_ELFLOADER_INC=~/projects/ravengun/tools/rg-elfloader/inc` (that directory exists for exactly this reason) and look at the `gcc` line in the log |
| `FATAAL: dlopen(<lib>) mislukt` | `laad_needed()` | a library is not on the search path | 0.6. `libglut.so.3` is the one that bites: it is in `runtime/lib32`, **not** in `ElfLdr2/libs` |
| `FATAAL: kan het spelbeeld niet op 0x8048000 mappen` | `map_segments()` | the address range is occupied — almost always **the loader was launched twice**, or a leftover process still holds part of `0x08048000-0x08cee000` | 0.1, then `pgrep -af rg-elfloader` |
| `FATAAL: kan segment <n> niet inlezen` | `map_segments()` | a truncated copy of the guest | `sha256sum` the copy against the NAS file (§0) — 7,764,284 bytes |
| `LET OP: <k> onopgeloste symbolen in DT_JMPREL` and then a crash in the guest | the loader, but **after** the jump | name resolution gaps | if `k` is small and only touches `glut*`, `libglut` was not loaded (0.6). If `k` is large, this is a real finding: capture the list, it names what is missing. |
| guest starts, **window appears**, then dies with a **SIGSEGV / core dump** rather than exit 255 | inside the guest, before the dongle | the loader is essentially done; the fault is guest-side (this is where the packed-class games died on a `0xb750xxxx` slot) | get a backtrace: `gdb -q --batch -ex bt /home/deck/.local/share/ravengun/bin/rg-elfloader core`. Compare against §2 step 3 — if the strings `HL-RUS`/`DongleInitialize` are **not** in the output, the guest died before the dongle check. |
| **exactly the §2 lines, then exit 255 with `DongleInitialize() FAILED!`** | nowhere — this is the target | — | **SUCCESS.** The loader is finished. Record `exit=255` and the log. |
| the §2 lines, then the guest runs **past** the dongle check and continues | — | — | Still a success for the loader (better than expected); record what happens next. It would mean the dongle is *not* the blocker in this configuration. |
| hang with a window, no exit | — | guest is waiting (e.g. on the licence manager / a socket) | `timeout` catches it; capture `pgrep -af rg-elfloader` and `/proc/<pid>/wchan` before killing |
| the loader prints the §2 lines but **no window ever appears** | the guest got control but produced no output | GL/library mismatch, or the guest needs `LD_LIBRARY_PATH` minus the 64-bit dirs | check `ldd` again against the exact environment used, and try the `librt-glfix.so` drop-in as `LD_PRELOAD` |

**Rule of thumb for attribution:** if the log contains `spring naar e_entry`, then **our loader did its
job** and everything after that belongs to the guest. If it does not, the failure is ours. That single
line is the boundary.

---

## 4. What to capture for the report

```sh
{
  echo "== date/host =="; date; uname -a
  echo "== loader ==";  md5sum /home/deck/.local/share/ravengun/bin/rg-elfloader \
                            ~/projects/ravengun/tools/rg-elfloader/rg-elfloader
  echo "== source ==";  grep -n 'DT_GNU_HASH' ~/projects/ravengun/tools/rg-elfloader/rg_elfloader.c
  echo "== guest ==";   sha256sum /tmp/ts-bench/game
  echo "== libs ==";    LD_LIBRARY_PATH="$LD_LIBRARY_PATH" ldd /tmp/ts-bench/game
  echo "== run ==";     cat /tmp/ts-bench/log/run1.txt
} > /tmp/ts-bench/report.txt 2>&1
```

A screenshot of the window (if one appears) belongs in the report next to the log, exactly as the
GoGoStrike proof was recorded — with the caveat that GoGoStrike is gone from the NAS, so **this run is
the only reproducible end-to-end proof our loader can currently produce.** That makes it worth doing
cleanly and recording completely.

---

## 5. Follow-ups this run unlocks

1. **If success (exit 255 on the dongle):** the loader milestone is achieved. Next is `/g3/` + the
   dongle anchor work in the README (§4 and §6). The bench can then be handed to the next game.
2. **A dynamic anchor for the eight UNKNOWN dongle functions.** The static map has no anchor for them
   (README §4). This run gives one: `main` calls `DongleInitialize`, and the measured game xref
   `0x080c2cdb` lies in `main`. Run the loader under `gdb` with a breakpoint there and record the call
   target that follows — that is `DongleInitialize`'s address, **by execution, not by inference**.
   Concretely, a second bench slot:
   ```sh
   gdb -q --args /home/deck/.local/share/ravengun/bin/rg-elfloader /tmp/ts-bench/game
   (gdb) break *0x080c2cdb
   (gdb) run
   (gdb) x/8i $eip
   ```
   Success = a `call` within the following few instructions whose target, when disassembled
   (`x/8i <target>`), opens with the `GetAladdinHLPass`/`DongleInit` call pattern recorded in the
   README. That address then becomes the anchor for `AcquireAladdinDongle`/`SysGetDongleHID`/
   `DongleGetID` in the same step.
3. **`/g3/` verification.** If the run stops with a missing-file error instead of the dongle, the
   `/g3/` binding from plan step 3 is the fix and this run should be repeated with it.

---

## 6. Do-not list

- Do not run this while another game is on the bench.
- Do not write anything to `/home/deck/mnt/spidernas_emulation/…`; work on `/tmp/ts-bench/game`.
- Do not chase the dongle in this run. Stop at the measured `exit 255` and report it.
- Do not run the loader against the NAS file directly — the copy keeps the run attributable and the NAS
  untouched.

---

## 7. Extra bewijs dat DEZE run kan leveren: de adreslijst (`hasp-haken.ini`)

Sinds de generieke HASP-hook een adreslijst kan lezen (`patches/Terminator/hasp-haken.ini`) kan deze
benchrun iets bewijzen wat **geen enkele statische meting kan**: of de gast die functies ook werkelijk
**aanroept**. Tot nu toe zijn de adressen alleen statisch bekend — bewezen, maar niet aangeroepen.

### 7.1 Wat er al statisch bewezen is (zonder bench)

`bash next-measurement.sh` + `bash hasplijst-toets.sh` in `patches/Terminator/`:

- 50 van 58 dongle/HASP-functies gelokaliseerd met bewijs; de hele `hasp_*`-kern graad A (byte-identiek).
- De adreslijst is gekeurd en aangehaakt in een testprogramma dat het **echte** gastbeeld op zijn eigen
  virtuele adressen mapt (0x08048000..0x08cee000), zonder de game te starten:
  **10 van 11 GELDIG en aangehaakt, 1 terecht geweigerd** (`hasp_free`, een `jmp`-thunk, zie §7.4).

Dat bewijst dat de adressen kloppen. Het bewijst **niet** dat de game ze aanroept.

### 7.2 De run, met de adreslijst erbij

Zelfde run als §1-§2, met drie extra variabelen erbij:

```sh
cd ~/.local/share/ravengun/runtime/views/Terminator
export LD_LIBRARY_PATH="/home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs:/home/deck/.local/share/ravengun/runtime/lib32"
export RAVENGUN_HASP_ELF_GAME=/tmp/ts-bench/game
export RAVENGUN_HASP_ELF_ADRESLIJST=~/projects/ravengun-patches/patches/Terminator/hasp-haken.ini
export RAVENGUN_HASP_ELF_LOG=/tmp/ts-bench/log/hasp.log
export RAVENGUN_HASP_VERBOSE=1          # NODIG: hh_meld() logt "aanroep: <naam>" alleen met deze vlag
timeout 90 /home/deck/.local/share/ravengun/bin/rg-elfloader /tmp/ts-bench/game 2>&1 | tee /tmp/ts-bench/log/run2.txt
```

De hook moet via `LD_PRELOAD` in het proces komen (of via de drop-in van de laderroute); zonder de hook
is er geen `aanroep:`-regel en levert deze sectie niets op. Controleer vooraf dat de hook geladen wordt:
de log hoort te beginnen met `=== ravengun-hasphook (pid …)` en de regel
`adreslijst: 11 regel(s) … (basis: absoluut)`.

### 7.3 Wat je ziet, en wat het betekent

`hh_meld()` schrijft bij elke aanroep één regel `[ravengun-hasphook] aanroep: <naam>`. Dat maakt de
aanroeplijst meetbaar. Per uitkomst:

| wat je in de log ziet | wat het betekent | wat je daarna doet |
|---|---|---|
| **`aanroep:` voor alle tien, en de game komt voorbij `DongleInitialize() FAILED!`** | de emulator antwoordt geaccepteerd; het HASP-pad is niet meer de blokkade | verder kijken waar hij nu stopt (GL? `/g3/`?) — dat is het volgende probleem, niet dit |
| **`aanroep:` voor een deel, daarna exit 255** | de game stopt **tussen** twee aanroepen: de eerste functie die **niet** in de log staat is waar hij afhaakt | de emulator moet de aanroepen *vóór* dat punt kloppend beantwoorden; dat is de kortste route naar verder |
| **`aanroep: hasp_get_info` / `hasp_get_sessioninfo` wel, `hasp_login` nooit** | de game bewandelt een ander pad dan verwacht (eerst licentie-informatie, dan pas inloggen) | de emulator moet `get_info`/`get_sessioninfo` als eerste kloppend beantwoorden |
| **géén enkele `aanroep:`-regel, en tóch exit 255 met `DongleInitialize() FAILED!`** | de blokkade zit **vóór** de HASP-API: in de engine zelf (`DongleInit`/`DongleOpen`) of in de `/g3/`-paden | dit is de meest waardevolle uitkomst: het verlegt het hele werk van "HASP emuleren" naar "`/g3/` leveren" (planstap 3). Meld dit expliciet. |
| **`aanroep:`-regels, dan een SIGSEGV in plaats van exit 255** | de crash zit ín of direct na een vervanger | kijk of het crashadres het gepatchte adres of zijn trampoline is (de log noemt de trampoline per functie). Zo ja: ABI/aanroepconventie van die ene functie klopt niet |
| **de hook haakt 10 van 11 aan, maar de game crasht meteen bij de eerste aanroep** | het patchen op zich ging goed (statisch bewezen), maar de vervanger of de trampoline is niet ABI-compatibel met deze build | dit is precies de `ret 0x4/0x8`-kwestie die het project al bij GL zag (cdecl vs stdcall); per functie te controleren |
| **de game komt verder dan ooit en raakt `hasp_free`** | de weigering in §7.4 heeft dan geen gevolg: het is een geheugen-vrijgave, geen donglevraag | niets doen; noteer het als bevestiging |
| **de game sterft exact bij `hasp_free`** | dan heeft de weigering wél een gevolg en moet er een andere weg komen (de aanroep onderscheppen bij de beller, of de alias-keten volgen) | melden; dit is de enige uitkomst waarin §7.4 een probleem is |

### 7.4 De ene weigering, en waarom die geen fout is

`hasp_free` staat met een **correct** adres in de lijst (`0x083baab0`) maar de hook weigert het, met als
reden `eerste bytes e9 ab 22 00 zijn geen prologue`. Dat is juist: in deze build is `hasp_free` een
alias — zijn hele lichaam is `jmp rel32` naar `0x083bcd60`. De prologue-check van de hook accepteert
`0xe9` niet, en dat hoort zo: je kunt geen 5-byte sprong over een 5-byte sprong zetten zonder de
trampoline te laten kloppen.

Het helpt **niet** om in plaats daarvan het alias-doel te hooken: `0x083bcd60` is zelf ook weer een
`jmp rel32` (`e9 ab be 04`). Voor `hasp_free` is er in deze build dus geen enkel punt met een prologue.

Inhoudelijk hoeft dat geen probleem te zijn: `hasp_free` geeft geheugen vrij dat door `hasp_get_info` /
`hasp_get_sessioninfo` is gereserveerd; het is geen functie die iets van de dongle vraagt. **De tien die
wél aangehaakt worden zijn de tien die de dongle daadwerkelijk aanspreken** — precies de tien met graad A.
Of dat in de praktijk genoeg is, is exact wat de `aanroep:`-log in §7.3 uitwijst.

### 7.5 Een kleine tekstverbetering voor de hook (geen bug)

In de samenvatting zegt de hook voor een **geweigerd** adres:

```
niet aangehaakt: hasp_free              (niet in de dynsym en niet in de adreslijst)
```

Dat is misleidend: het adres staat wél in de adreslijst, maar is bij de keuring afgewezen — de reden
staat een paar regels eerder wél goed in hetzelfde logboek. `hh_adres_zoek()` geeft alleen *geldige*
regels terug, dus de samenvatting kan het onderscheid niet meer zien. Suggestie: onthoud bij de keuring
per regel of hij is afgewezen, en gebruik dan de tekst
`(in de adreslijst maar afgekeurd: <reden>)`. Geen functioneel probleem — alleen een logregel die een
lezer op het verkeerde been zet.

---

## 8. Wat al bewezen is ZONDER de bench (fase 0 en 1 van `loader-run.sh`)

De laderbouwer heeft de reparatie **al gebouwd**, en die heeft er een ontleed-modus bij gezet:
`rg-elfloader --ontleed-alleen <game>` (alias `--dry-run`) doet alles t/m de relocaties en **start de
gast niet**. Daarmee is de hele keten op deze game al bewezen, voordat de bench aan de beurt is.

---

## 10b. DE RUN VAN DE LADERBOUWER — **DE LADER HEEFT HET GEDAAN** (2026-10-13)

Nadat de laderbouwer `laad_needed()` repareerde (de herkansings-lus in **beide** modi), deed hij een
echte run op deze game. **Alle vier de succesregels zijn gehaald** en er is **nul** FATAAL-regel:

```
rg-elfloader: shim geladen: 515 gast-symbolen in de globale scope
rg-elfloader: geladen: libGL.so.1 … libglut.so.3 … libstdc++.so.6      (13 van 13, 2 in een latere ronde)
rg-elfloader: geladen; spel /tmp/.../game krijgt argv[0]=/tmp/.../game
rg-elfloader: spring naar e_entry 0x8057240 (esp 0xf6d67e10, argc 1, envc 56, 17 auxv)
exitcode 255   ← van de GAST, niet van ons
```

**T1 tot en met T3 van mijn eigen voorspelling zijn uitgekomen**, en dit is de derde bevestiging van de
ONGEPAKTE klasse: geen enkele FATAAL, de sprong gehaald, en de 255 komt van de gast zelf.

### 10b.1 Het bewijs dat de herkansings-lus het verschil maakte

| | run vóór de reparatie (mijn run, §10) | run ná de reparatie (laderbouwer) |
|---|---|---|
| `libGLU.so.1` | `FATAAL: dlopen(libGLU.so.1) mislukt: undefined symbol: floor` | geladen, in een **latere ronde** |
| `libtheora.so.0` | (nooit bereikt) | geladen, in een **latere ronde** |
| einde | exit 127, geen sprong | exit 255 **van de gast**, sprong gehaald |
| FATAAL-regels | 1 | **0** |

Dat is precies de voorspelde uitwerking van de fix in §10.3: het faalpad vult nu ook `wacht[]` en de
bestaande lus (regel 591-606) draait in beide modi, dus een bibliotheek die pas laadt als een eerdere
`DT_NEEDED` er is, wordt nu in een tweede ronde alsnog geladen. **De droogloop-maskering van §10.2 is
daarmee opgeheven** — een groene `--ontleed-alleen` én een groene echte run wijzen nu dezelfde kant op.

### 10b.2 De adreslijst is nu dubbel bevestigd

Mijn conclusie uit §10.4 blijft staan en is **dubbel bevestigd**: in de run van de laderbouwer zat de
hook in het **laderproces** en haakte **10 van 11** doelen aan **op exact dezelfde adressen** als mijn
statische symbolenkaart, met `hasp_free` om dezelfde reden afgewezen. Twee onafhankelijke runs, hetzelfde
beeld — de kaart is dus niet aan één toeval-meting te danken.

**Wat er nu nog open is:** de aanroepmeting (`aanroep:`-regels) uit §7 — en die is inmiddels
achterhaald door §11: de gast bereikt de HASP-API niet omdat hij eerder op zijn eigen ptrace-controle
stopt.

---

## 10. DE BENCHRUN VAN 2026-10-13 — uitkomst, oorzaak en de fix

**Uitkomst: de lader is NIET klaar. Hij sterft vóór de sprong, in `laad_needed()`.**
De DT_GNU_HASH-reparatie is NIET de schuldige — die werkt (1020 symbolen uit STRTAB−SYMTAB). Het is een
**tweede, onafhankelijke fout in de lader, die alleen in de echte run bestaat.**

```
lader    : tools/rg-elfloader/rg-elfloader  34.876 B  (en de geinstalleerde kopie is identiek)
sha256   : 2190a1687b2cd13e000742778d64ac238f79243c66e10f1da6ef62c40825f454
gast     : sha256 798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
logboeken: patches/Terminator/logs-benchrun-20261013/{dryrun.txt,run.out,hook.log}
```

**De vier succesregels, feitelijk:**

| regel | gehaald? |
|---|---|
| `shim geladen: N gast-symbolen` | **JA** — `shim geladen: 515 gast-symbolen in de globale scope` |
| `geladen; spel …` | **NEE** |
| `spring naar e_entry 0x8057240` | **NEE** |
| de gast geeft zijn eigen donglemelding | **NEE** (de gast heeft nooit gedraaid) |

De run eindigde met `exit=127` en deze regel:

```
rg-elfloader: geladen: libGL.so.1
rg-elfloader: FATAAL: dlopen(libGLU.so.1) mislukt: …/ElfLdr2/libs/libGLU.so.1: undefined symbol: floor
```

### 10.1 De oorzaak (bewezen zonder bench, met `dlopen-orde-toets.c`)

`libGLU.so.1` uit `ElfLdr2/libs` heeft **nul `DT_NEEDED`-regels** (`readelf -d` toont alleen SONAME) en
heeft `floor`, `ceil`, `sqrt` en `_ZdaPv` ongedefinieerd. Hij vertrouwt er dus op dat libGL, **libm** en
**libstdc++** al in de *globale* scope staan. De loader dlopen't de `DT_NEEDED`-lijst van de gast in
volgorde, en daarin staat **libGLU op #2 en libm pas op #9** (libstdc++ op #13). Op het moment dat
libGLU geladen wordt, is libm er dus nog niet.

`dlopen-orde-toets.c` bootst precies dat na (mode A = libGL, libGLU):

```
mode A : libGL.so.1 GELADEN ; libGLU.so.1 MISLUKT -> undefined symbol: floor      <- de echte fout
mode B : libGL, libm, libGLU -> libGLU MISLUKT -> undefined symbol: _ZdaPv        <- de volgende in de rij
```

Dus de keten is langer dan één symbool: libGLU wil **libGL + libm + libstdc++** alle drie al in de
scope hebben. `LD_PRELOAD` van libGLU zelf helpt **niet** (dan faalt hij eerder op `glGetIntegerv`,
omdat libGL dan nog niet geladen is — bewezen).

### 10.2 Waarom de droogloop groen was en de run sterft

`rg_elfloader.c`:

```c
579:  h = dlopen(naam, RTLD_NOW | RTLD_GLOBAL);
580:  if (!h) {
581:      if (!rg_ontleed)
582:          fatal("dlopen(%s) mislukt: %s", naam, dlerror());   // <-- HIER STERFT DE ECHTE RUN
583:      if (nw < …) wacht[nw++] = naam;                        // <-- alleen in --ontleed-alleen
584:      continue;
585:  }
…
591:  do {   /* alleen --ontleed-alleen komt hier met nw > 0: nog een poging zolang er voortgang is */
```

De uitgestelde tweede poging (regel 591-606) is dus **alleen actief in de ontleed-modus**, en de
opmerking op regel 591 zegt dat zelf. Daarom meldde de droogloop `geladen (2e poging): libGLU.so.1` en
zag alles er groen uit, terwijl dezelfde lib in de echte run fataal is.

**Les voor de methode, niet alleen voor deze game: een groene `--ontleed-alleen` bewijst de gewone
run niet, want de ontleed-modus is met opzet toegeeflijker.**

### 10.3 De fix (voor de laderbouwer)

Laat het faalpad óók `wacht[]` vullen en gebruik de bestaande herpoging-lus in **beide** modi; roep
`fatal()` pas aan voor bibliotheken die na het convergeren van de lus nog steeds niet laden. Dat is
~5 regels en het maakt de bestaande lus (591-606) gedeeld in plaats van ontleed-only.

**Workaround voor de run** (staat nu in `loader-run.sh`, maar is nadrukkelijk niet de fix): zet
`libGL.so.1`, `libm.so.6` en `libstdc++.so.6` van `/usr/lib32` in de `LD_PRELOAD` van de **lader**,
náást de HASP-hook. Let op dat `LD_PRELOAD` alleen via `env` op de lader gezet wordt, anders krijgt de
64-bits `timeout` hem en krijg je `wrong ELF class: ELFCLASS32 … ignored`-ruis in het logboek (dat
gebeurde in deze run; onschadelijk maar vervuilend).

### 10.4 Wat deze run WEL heeft opgeleverd

- **De reparatie is bevestigd in een echte run:** `1020 gast-symbolen (bron: (DT_STRTAB-DT_SYMTAB)/16,
  geen DT_HASH)`, shim 515 symbolen, alle 13 bibliotheken vinden hun weg (met de workaround).
- **De adreslijst is tegen het LEVENDE beeld gevalideerd.** De hook keurde in het laderproces alle 11
  regels opnieuw: 10 × `GELDIG` op exact dezelfde adressen als mijn statische kaart, en `hasp_free`
  afgewezen met dezelfde reden. Dat is een onafhankelijke bevestiging van de hele symbolenkaart.
- **De aanroepmeting is ONBESLIST, niet negatief.** Er zijn nul `aanroep:`-regels, maar er is ook niet
  gesprongen — de gast heeft nooit gedraaid. Dit is dus **niet** het "de blokkade zit vóór de HASP-API"-
  signaal van §7.3. `loader-run.sh` meldt dat nu expliciet zo en trekt er geen conclusie uit.
- **Extra constraint gemeten:** `libGLU.so.1` eist een **uitvoerbare stack** (`PT_GNU_STACK` RWX); zonder
  `-Wl,-z,execstack` in de aanroeper faalt `dlopen` al op die eis. De lader voldoet eraan (de gast zelf
  heeft ook `PT_GNU_STACK` RWX), maar het is een echte eis om te weten.

---

**Uitgevoerd op 2026-10-12, op een /tmp-kopie van de echte `game`:**

```
lader            : tools/rg-elfloader/rg-elfloader  30.416 B
sha256           : 4ba14401062663a876f6da5eac06009a8cf323b8da1ee688c8555392adb6d719
gast             : sha256 798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
rg-elfloader: 1020 gast-symbolen (bron: (DT_STRTAB-DT_SYMTAB)/16, geen DT_HASH)   <- DE REPARATIE
rg-elfloader: shim geladen: 515 gast-symbolen in de globale scope
rg-elfloader: geladen: libGL.so.1 … libglut.so.3 … (13 bibliotheken, 2 op de 2e poging)
rg-elfloader: stale-map: 496 JMPREL-entries, 0 met stale 0xb7-adres + naam
rg-elfloader: DT_REL: 510 toegepast, 6 COPY, 1 onopgelost
rg-elfloader: DT_JMPREL: 495 toegepast, 0 COPY, 1 onopgelost
gast-dynsyms: 1020 (bron: (DT_STRTAB-DT_SYMTAB)/16, geen DT_HASH)
shim geladen: 515 van de 1020 gast-symbolen in de globale scope
relocaties: 1011 toegepast, 0 RELATIVE (ongewijzigd), 2 onopgelost
spel NIET gestart (--ontleed-alleen)
```

**Wat dat bewijst, en waarom het klopt:**

| meting | mijn statische voorspelling | uitkomst | conclusie |
|---|---|---|---|
| gast-symbollen | 1020 = `(DT_STRTAB−DT_SYMTAB)/16` | **1020**, bron `STRTAB-SYMTAB` | de reparatie doet precies wat ik voorstelde; het is dezelfde formule en hetzelfde getal |
| toegepaste relocaties | 511 GLOB_DAT + 496 JMP_SLOT + 6 COPY = 1013 | 510 + 495 + 6 = **1011**, en 2 onopgelost | 1013 − 2 = 1011: **de boekhouding sluit exact** |
| onopgelost | alleen `__gmon_start__` (klassiek, onschadelijk) | **precies `__gmon_start__` ×2** | geen verrassingen; de naamresolutie is compleet |
| pakker-reparatie | no-op (0 stale slots) | `0 met stale 0xb7-adres` | bevestigt de ONGEPAKTE klasse onafhankelijk |
| bibliotheken | 13, met `libglut.so.3` uit `runtime/lib32` | 13 geladen, incl. `libglut.so.3` | de laatste linkhinderpaal is weg |

**Wat er dus nog NIET bewezen is:** alleen de sprong zelf — `spring naar e_entry 0x8057240` en wat de
gast daarna doet. Dat is precies fase 2, en dat is de enige stap die de bench nodig heeft.

**En de kern van de afspraak, nog een keer:** als de gast na de sprong **zijn eigen melding** geeft
(`Begin HL-RUS License` / `DongleInitialize() FAILED!`, exit 255), dan is dat een **GOEDE** uitkomst voor
de lader. Het betekent dat onze lader gedaan heeft wat hij moest doen en dat het volgende probleem bij de
gast ligt, niet bij ons. De run is dan geslaagd, ook al komt er geen spelbeeld.

---

## 9. Twee dingen die de laderbouwer nog moet weten (gemeten tijdens fase 0-1)

**9.1 De geinstalleerde kopie is een andere dan de gebouwde — en was de OUDE.**
`~/.local/share/ravengun/bin/rg-elfloader` had op 2026-10-12 21:46 nog
sha256 `af0535262b304e740cc0109d912263b26e756764c46a524a428476b134c4c30f` (25.468 B) — dat is de
**versie zonder de reparatie**. De gerepareerde lader staat in de repo
(`tools/rg-elfloader/rg-elfloader`, 30.416 B, 22:14:50). Wie de run met de geinstalleerde kopie doet,
krijgt dus weer `FATAAL: geen DT_HASH`. `loader-run.sh` gebruikt daarom de **repo-lader** en waarschuwt
hard als de twee verschillen. Aanbeveling: `scripts/build-elfloader.sh` opnieuw laten installeren, of
de run expliciet op het repo-pad houden.

**9.2 De `lib-defs`-lijst loopt nu tegen zijn plafond van 16384.**
```
lib-defs: 16384 gedefinieerde symbolen uit de NEEDED-libs
```
Dat is precies `sizeof(rg_defname)/sizeof(rg_defname[0])` in `rg_elfloader.c`. Voor de reparatie
leverden alleen de vier ElfLdr2-bibliotheken (met `DT_HASH`) definities; nu doen **tien** bibliotheken
mee via `STRTAB-SYMTAB`, en dan is 16384 te krap. `def_add()` laat stil vallen wat niet meer past, dus
`lib_defines()` ziet een deel van de lib-symbolen niet — en dan exporteert de smalle shim symbolen die
een lib ook definieert. Voor deze game is dat gemeten onschadelijk (mijn analyse: 8 botsingen, waarvan 6
de COPY-set die de shim met opzet houdt, en `_IO_stdin_used` in `shim_skip()`; er blijft 1 symbool over,
`tzname`), maar het is nu een **stille afkap** in plaats van een bewuste grens. Aanbeveling: het
plafond verhogen (of `def_add()` laten melden dat hij vol zit, net als bij de adreslijst).


