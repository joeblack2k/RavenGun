# De beschermingslaag waar Terminator Salvation op stopt: `ptrace` + Yama

**Game:** Terminator Salvation — `game`, ELF32 i386, ET_EXEC, sha256
`798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a`, 7.764.284 bytes.
**Laag:** de gast wil dat zijn eigen kindproces hem mag aanhaken met `ptrace`. De host (SteamOS, kernel
met Yama) verbiedt dat: `kernel.yama.ptrace_scope = 1`.
**Status:** oorzaak **gemeten** (strace, door de laderbouwer) · mechanisme en maatregel **gemeten zonder
tracer en zonder game** (mijn microtoetsen) · de bevestiging op de echte gast en de A/B **staan klaar
en wachten op de bench**.

**Dit bestand is zelfstandig leesbaar.** Elk punt is gelabeld **GEMETEN** (een directe meting, met het
commando of de uitvoer erbij) of **AFGELEID** (mijn gevolgtrekking uit metingen, niet zelf gemeten).
Er is read-only op de NAS gewerkt en er is **niets aan de gast veranderd**.

---

## 0. Samenvatting in één alinea

Na de sprong naar `e_entry` start de gast zijn PhysX-opstart en forkt twee kindprocessen. Kind 1 doet
`ptrace(PTRACE_TRACEME)`; kind 2 doet `ptrace(PTRACE_ATTACH, ouder)` en krijgt **EPERM**, omdat Yama op
deze host alleen een **voorouder** toestaat een **nakomeling** te volgen en niet omgekeerd. Kind 2
eindigt met 1, de ouder ziet dat, geeft kind 1 SIGKILL en beëindigt zichzelf met -1 → **exit 255**. Op de
originele kast bestaat Yama niet, dus daar slaagt deze aanroep. De maatregel is één
`prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY)` in het proces waarin de gast draait — de door de kernel zelf
aangewezen ontsnappingsroute, per proces, zonder root, zonder de hostinstelling aan te raken, en zonder
de gast te patchen. Op systeemniveau is het verschil al bewezen: kind faalt met EPERM en eindigt met 1 →
met de maatregel slaagt de attach en eindigt het kind met 0.

---

## 1. Wat de laderbouwer met `strace` mat — GEMETEN (overgenomen met bron)

Bron: zijn runs 2 en 3, in `~/bw/terminator-elf/run2-strace/` en `~/bw/terminator-elf/run3-strace/`.

```
na de sprong:
  de gast zoekt `PhysX.conf`, opent `libNxCooking.so`, en er komen TWEE kindprocessen;
  kind 1 doet  ptrace(PTRACE_TRACEME)
  kind 2 doet  ptrace(PTRACE_ATTACH, ouder)  ->  EPERM   en eindigt met 1
  de ouder ziet dat, geeft kind 1 SIGKILL, en beeindigt zichzelf met -1  (= exit 255)

kernel.yama.ptrace_scope = 1
```

**Waarom dat alleen niet afdoende is — en dit is geen muggenzifterij.** `strace` is zélf een tracer. Een
proces dat al gevolgd wordt, gedraagt zich anders: `PTRACE_TRACEME` van kind 1 wordt afgewezen zodra de
ouder al gevolgd is, en de ouder ziet dus andere uitkomsten dan zonder tracer. De strace **bewijst de
aanroep en de EPERM**; hij bewijst niet dat de 255 *zonder* tracer dezelfde oorzaak heeft. Daarom §2 en
§5, die zonder tracer werken.

**Status van de omgeving:** GEMETEN. `cat /proc/sys/kernel/yama/ptrace_scope` → `1`; de map
`/proc/sys/kernel/yama/` bevat alleen `ptrace_scope`.

---

## 2. Mijn eigen metingen zonder tracer — GEMETEN

### 2.1 De gast importeert `ptrace` dynamisch, dus een preload kan meekijken

```
$ readelf -sW --dyn-syms "game" | awk '$7=="UND"' | grep -iE 'ptrace|fork|wait'
   2: … UND waitpid@GLIBC_2.0
  26: … UND fork@GLIBC_2.0
 347: … UND ptrace@GLIBC_2.0
```

Omdat de aanroep via de PLT loopt, is hij met `LD_PRELOAD` te onderscheppen **zonder dat wij tracer
worden**. Dat is het hele idee achter de waarnemer in §3.2: hij wikkelt alleen de functie `ptrace` in het
proces zelf en laat elke aanroep **ongewijzigd** door naar de echte `ptrace`.

### 2.2 De A/B op systeemniveau (`yama-toets.c`) — de kern van het bewijs

Een vaderproces en een kind; het kind doet `PTRACE_ATTACH` op zijn vader — precies de vorm van kind 2.
Het enige verschil tussen de twee modi is of de vader eerst `PR_SET_PTRACER_ANY` zet.

```
mode 0 (geen maatregel)
  kind: ptrace(PTRACE_ATTACH, 746903) -> -1, errno=1 (Operation not permitted)
  kind: CONCLUSIE: aanhaken van de ouder MAG NIET
  ouder: kind-eindstatus = 256 (exitcode 1)

mode 1 (vader doet PR_SET_PTRACER_ANY)
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY) -> 0 (ok)
  kind: ptrace(PTRACE_ATTACH, 746906) -> 0, errno=0 (geen fout)
  ouder: kind-eindstatus = 0 (exitcode 0)
```

**Dit is exact het gedrag uit de strace** — kind faalt met `EPERM`, `errno=1 (Operation not permitted)`,
en eindigt met **1** — en het laat zien dat één `prctl` in de ouder dat omkeert naar **0**. Buiten
strace, zonder game, en met `ptrace_scope` die **1 blijft**.

### 2.3 Dezelfde A/B, maar via de echte maatregel als `LD_PRELOAD`

```
zonder preload : PTRACE_ATTACH -> -1, errno=1  ; kind-eindstatus 256 (exitcode 1)
met libravengun-ptracer-permit.so
               : PTRACE_ATTACH ->  0, errno=0  ; kind-eindstatus 0
  logboek: [ravengun-ptracer-permit] pid 746914: PR_SET_PTRACER_ANY gezet: dit proces mag door een
           willekeurig proces worden aangehaakt (yama ptrace_scope blijft ongemoeid)
```

Daarmee is bewezen dat de maatregel zoals hij in de run gebruikt wordt (een preload, niet een
codewijziging) hetzelfde doet als de directe `prctl`.

### 2.4 De waarnemer werkt op een echte `ptrace`-aanroep — en is geen tracer

```
[ravengun-ptrace-log] pid 746929 ppid 746926: PTRACE_ATTACH(pid 746926) -> -1 FOUT: Operation not permitted
```

Die regel komt uit een gewoon 32-bits preload-proces, **niet** uit `strace` en niet uit een tracer. Dat
is precies wat arm A van §5 op de echte gast moet laten zien.

### 2.5 AFGELEID: waarom `PhysX.conf` en `libNxCooking.so` in de strace staan

**AFGELEID, niet gemeten:** of de twee kindprocessen bij de **PhysX**-opstart horen, of bij een
anti-debugcontrole die daarmee samenhangt, kan ik uit deze gegevens **niet** scheiden. Wat vaststaat is
de volgorde: PhysX-bestanden → twee kinderen → `EPERM` → 255.

**GEMETEN** hierbij: `libNxCooking.so` en `libPhysXCore.so` staan in `ElfLdr2/libs` en zijn dus
beschikbaar; een `PhysX.conf` heb ik in de spelmap **niet** gevonden (geen `*PhysX*` of `*.conf` in de
bovenste drie niveaus). De gast ziet die zoektocht dus mislukken en gaat door — de 255 komt van de
ptrace-stap, niet van het ontbrekende bestand.

---

## 3. De maatregel

### 3.1 Wat het is — GEMETEN

`libravengun-ptracer-permit.so`, een 32-bits preload van ±15 kB, waarvan de constructor één ding doet:

```c
prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
```

In de laderrun is dat het proces **waarin de gast draait** — precies het proces dat kind 2 wil aanhaken.

### 3.2 Waarom dít, en niet iets anders

- **Per proces, niet hostbreed.** `/proc/sys/kernel/yama/ptrace_scope` wordt **niet** aangeraakt; dat
  blijft `1` (GEMETEN, ook na de toets). Geen root nodig, en er verandert niets voor andere programma's.
- **Het dooft uit met het proces.** Zodra de game stopt is de ruimte weg. Er blijft geen
  systeeminstelling achter — anders dan bij `ptrace_scope=0`, wat een blijvende en veel bredere
  hostwijziging zou zijn.
- **Het is het mechanisme dat de kernel er zelf voor geeft.** `PR_SET_PTRACER` is de officiële,
  gedocumenteerde ontsnappingsroute van Yama: "dit proces stemt ermee in om gevolgd te worden".
- **De gast wordt niet aangeraakt.** Geen patch, geen vervalste retourwaarde, geen aangepaste
  vergelijking, geen hook op de controle zelf. De gast vraagt iets; de kernel zegt ja.

**GEMETEN detail dat je moet weten:** een `LD_PRELOAD` wordt door `fork()` doorgegeven, dus de
kindprocessen van de gast laden de maatregel óók en zetten hem op zichzelf. Voor ons doel is dat
onschadelijk — wij hebben de toestemming van de **ouder** nodig, en die is er — maar het betekent wel dat
de kinderen tijdens de run zelf ook aanhaakbaar zijn. De maatregel is in de praktijk dus iets breder dan
"alleen de ouder toestaan". **AFGELEID:** dat dit de handshake niet verstoort, omdat de ouder de tracee
is en zijn toestemming is wat telt. Arm B van §5 toetst het.

### 3.3 Een eerdere projectmeting die ik niet kon reproduceren — GEMETEN, met waarschuwing

`docs/research/jurassicpark-stand.md` regel 105-107 meldt dat de GL-fix onder `RAVENGUN_GLFIX_FAULT=1`
weliswaar `prctl(PR_SET_PTRACER_ANY)` zet, maar dat **een kale `gdb`-attach toen toch `EPERM` gaf**.

In mijn microtoets lukt een kale `PTRACE_ATTACH` mét de maatregel **wel**. **AFGELEID** verklaring: de
maatregel geldt voor het proces dat hem zet, niet voor zijn kinderen of voor een ander proces in de
keten; staat de `prctl` in proces A en haak je proces B aan, dan helpt het niet. Daarom noteer ik het:
**als een attach in de praktijk alsnog `EPERM` geeft, is de eerste vraag "in welk proces is de `prctl`
gezet?"** — niet "werkt de maatregel niet?".

---

## 4. Het grensoordeel — is dit "de host toeschietelijker maken" of "de controle laten slagen"?

**De tegenwerping eerst, eerlijk:** het mechanisme dat de gast gebruikt is een anti-tamper-handshake.
Door de attach toe te staan maken wij een zelfcontrole van de gast mogelijk die hier zou falen. Je kunt
dus zeggen: we helpen de gast langs een bescherming.

**Mijn oordeel: het is het eerste — een hostbeperking wegnemen — en dat is op drie gronden te
onderbouwen.**

1. **Wat wordt weggenomen is een eigenschap van de HOST, niet van de game.** `yama.ptrace_scope` is
   Linux-hardening die **ná** deze kast is gekomen (Yama kwam in 2.6.36, later dan deze 2010-kast). Op de
   originele kast bestaat de beperking niet, dus daar slaagt deze aanroep zonder meer. Wij veranderen de
   kernel niet en de game niet; we zetten één proces in de toestand die op de kast de normale was.
2. **De bescherming wordt bevredigd, niet omzeild.** Als de gast controleert "mag mijn kind mij volgen?",
   dan is het antwoord op de kast ja. Wij zorgen dat het antwoord hier ook ja is, via de weg die de
   kernel daarvoor aanwijst. Er wordt geen keuring overgeslagen en geen uitkomst verzonnen.
3. **Er is precedent in dit project, bewust en opt-in.** `jurassicpark-stand.md` regel 105 laat zien dat
   exact deze `prctl` al in de GL-fix zit, onder `RAVENGUN_GLFIX_FAULT=1`, "zodat een losse gdb kan
   aanhaken ondanks `ptrace_scope=1`". Dit is dus geen nieuwe categorie maar dezelfde maatregel, nu niet
   voor diagnose maar omdat de gast erom vraagt.

**Waar de grens voor mij wél ligt, en waar ik zou stoppen:** als de volgende stap zou zijn om de
**vergelijking in de gast te patchen**, om `ptrace` een **verzonnen `0`** te laten teruggeven, of om de
**controle over te slaan**, dan is dat het omzeilen van de bescherming zelf. Dat doe ik niet; dat is een
eigenaarsbeslissing. **Dat is hier niet nodig**: de kernel geeft het juiste antwoord zodra het proces
toestemming geeft.

**Twee dingen die ik er expliciet bij zeg, zodat niemand later verrast is:**

- **`PR_SET_PTRACER_ANY` is breder dan strikt nodig.** Het staat *elk* proces toe dit proces aan te
  haken, niet alleen het kind van de gast. Smaller kan met `PR_SET_PTRACER` plus het concrete PID van
  dat kind, maar dat PID kent de ouder pas nadat de gast zelf geforkt heeft — dat vraagt dus een hook op
  `fork` **in de gast**. Meer ingreep in de gast voor minder breedte; ik geef er de voorkeur aan het bij
  de kern-maatregel te houden en de breedte te melden. **Wil de eigenaar smaller, dan is dat een kleine
  vervolgstap.**
- **De maatregel staat in de preload van de run, niet in de lader.** Daarmee is hij per game aan en uit
  te zetten en hoeft de laderbouwer er niets voor te veranderen.

---

## 5. De A/B op de bench — één script, twee armen

`bash yama-ab.sh --start` doet alles: preconditions, hulpmiddelen bouwen, dan **twee runs** met een
plafond van **90 s per arm**, en daarna een verdict per arm. Zonder `--start` doet het alleen de
voorbereiding en start het **niets**. De NAS blijft alleen-lezen: de gast is een `/tmp`-kopie en de
werkmap is een `/tmp`-view met symbolische koppelingen voor de grote leesbestanden en **kopieën** van de
bestanden waarin de gast schrijft (`T4User/`, `GeneralPurposeStateMachine.txt`). Na elke arm worden de
logboeken **direct naar de patchmap gekopieerd**, zodat er niets verloren gaat als de tijd ons inhaalt.

| | arm A | arm B |
|---|---|---|
| `libravengun-ptrace-log.so` (waarnemer) | ja | ja |
| `libravengun-ptracer-permit.so` (maatregel) | **nee** | **ja** |
| al het andere | identiek | identiek |

Per arm wordt vastgelegd: **exitcode**, **levensduur in seconden**, **of de `ATTACH` slaagde** (uit het
waarnemer-logboek) en **of de gast een eigen melding gaf**.

### 5.1 Wat elke uitkomst betekent — VOORAF vastgelegd

| arm A | arm B | betekenis | wat ik dan doe |
|---|---|---|---|
| `ATTACH → EPERM` | `ATTACH → 0` **en** de gast leeft langer of geeft een eigen melding | **TREFFER.** De oorzaak is zonder tracer bevestigd (A) én de maatregel haalt de blokkade weg (B). | dit melden als het resultaat; de volgende laag in kaart brengen (PhysX of de dongle) |
| `ATTACH → EPERM` | `ATTACH → 0`, maar de gast stopt **op dezelfde plek** met 255 en zegt niets extra's | **ptrace was een SYMPTOOM, niet de blokkade.** De handshake was een gevolg van iets anders dat misging. | net zo hard melden; **PhysX wordt de volgende verdachte** (`PhysX.conf`, `libNxCooking.so`, de cooking-stap) |
| `ATTACH → EPERM` | `ATTACH → EPERM` **ook met de maatregel** | de maatregel werkte niet in het juiste proces (§3.3) | eerst het logboek van `libravengun-ptracer-permit.so` lezen: in welk proces is de `prctl` gezet? |
| `ATTACH → 0` **zonder** maatregel | `ATTACH → 0` | de Yama-hypothese is fout voor deze gast (hij geeft dan zelf toestemming, of het is geen upward-attach) | luid melden; dan moet de hele aanname opnieuw, en is de strace-lezing niet wat ik dacht |
| geen `ptrace`-regel in A | geen `ptrace`-regel in B | de gast neemt deze keer een ander pad (bijvoorbeeld omdat een eerder bestand nu wél gevonden wordt) | melden dat de strace-meting situationeel was; de twee runs zijn dan niet vergelijkbaar |
| A en B gedragen zich **identiek** | | geen effect van de maatregel | een tweede laag zoeken; de maatregel blijft staan als hij geen kwaad doet |

**Wat een treffer is:** in arm B slaagt de `ATTACH` **en** leeft de gast langer **of** geeft hij een eigen
melding (bijvoorbeeld de donglemelding uit `LADER-README.md` §6, of een PhysX-fout). **Een langere
levensduur alleen al is een treffer**, want dan is de ptrace-blokkade opgeheven.

**Wat een negatief resultaat betekent, en dat meld ik dan net zo hard:** slaagt de `ATTACH` in arm B maar
stopt de gast alsnog met 255 op precies dezelfde plek zonder iets extra's te zeggen, dan was de
ptrace-stap **niet** de blokkade maar een symptoom, en dan is de volgende verdachte de **PhysX-stap**
zelf. Dan gaat deze laag terug naar "gemeten, maar niet de oorzaak" en verschuift het werk.

### 5.2 De uitkomst van arm A is ook het antwoord op "bevestig het zonder strace"

Arm A draait de gast **zonder** maatregel en **met** de waarnemer. Als daar
`PTRACE_ATTACH … FOUT: Operation not permitted` staat gevolgd door exit 255, dan is de oorzaak op de
echte gast bevestigd door een meting die **geen tracer** is. Dat is de hele reden dat de waarnemer
bestaat.

---

## 6. Reproduceren zonder de bench

Alles in §2 en §3 is zonder game en zonder bench te herhalen:

```sh
D=~/projects/ravengun-patches/patches/Terminator
W=/tmp/ts-yama; mkdir -p $W
INC=~/projects/ravengun/tools/rg-elfloader/inc          # nodig: gnu/stubs-32.h ontbreekt op SteamOS

gcc -m32 -pie -fPIE -O1 -I$INC -o $W/yama-toets                       $D/yama-toets.c
gcc -m32 -shared -fPIC -O1 -I$INC -o $W/libravengun-ptrace-log.so     $D/ptrace-log.c -ldl
gcc -m32 -shared -fPIC -O1 -I$INC -o $W/libravengun-ptracer-permit.so $D/ptracer-permit.c

cat /proc/sys/kernel/yama/ptrace_scope      # 1
$W/yama-toets 0                             # verwacht: kind EPERM, kind-eindstatus 256 (exit 1)
$W/yama-toets 1                             # verwacht: kind ok,   kind-eindstatus 0

LD_PRELOAD=$W/libravengun-ptracer-permit.so RAVENGUN_PTRACER_LOG=/tmp/p.log $W/yama-toets 0
LD_PRELOAD=$W/libravengun-ptrace-log.so    RAVENGUN_PTRACE_LOG=/tmp/t.log  $W/yama-toets 0; cat /tmp/t.log
```

---

## 7. Status per punt

| bevinding | label | waar het op rust |
|---|---|---|
| `kernel.yama.ptrace_scope = 1` | **GEMETEN** | `/proc/sys/kernel/yama/ptrace_scope` |
| kind 1 `PTRACE_TRACEME`; kind 2 `PTRACE_ATTACH(ouder) → EPERM`; kind exit 1; ouder SIGKILL + -1 → 255 | **GEMETEN** | strace van de laderbouwer (runs 2 en 3) |
| dat de 255 **zonder** tracer dezelfde oorzaak heeft | **nog niet gemeten** | arm A van §5 — precies wat de waarnemer moet aantonen |
| `game` importeert `ptrace`/`fork`/`waitpid` dynamisch | **GEMETEN** | `readelf -sW --dyn-syms` |
| een kind dat zijn ouder aanhaakt krijgt `EPERM` (errno 1) en eindigt met 1 | **GEMETEN** | `yama-toets 0` (§2.2) |
| `PR_SET_PTRACER_ANY` in de ouder laat die attach slagen (kind exit 0) | **GEMETEN** | `yama-toets 1` (§2.2) |
| dezelfde maatregel werkt als `LD_PRELOAD` | **GEMETEN** | §2.3 |
| de waarnemer logt een echte aanroep zonder tracer te zijn | **GEMETEN** | §2.4 |
| `libNxCooking.so`/`libPhysXCore.so` in `ElfLdr2/libs`; geen `PhysX.conf` in de spelmap | **GEMETEN** | §2.5 |
| de twee kinderen horen bij de PhysX-opstart (of bij een anti-debugcontrole eromheen) | **AFGELEID** | §2.5 — niet te scheiden met deze gegevens |
| de maatregel verstoort de handshake niet, ook al erven de kinderen hem | **AFGELEID** | §3.2 — arm B toetst het |
| de eerdere `gdb`-EPERM kwam doordat de `prctl` in een ander proces stond | **AFGELEID** | §3.3 |
| het wegnemen van Yama is een hostbeperking wegnemen, niet de bescherming omzeilen | **OORDEEL** | §4, met de tegenwerping en de grens erbij |

---

## 8. DE METING VAN 2026-10-13 — UITKOMST (GEMETEN)

`bash yama-ab.sh --start`, twee armen, verder identiek. Logboeken:
`logs-yama-ab-20261013-0930/`. Lader: 35.204 B, sha256
`7f83edb291f0065c54122392679ef0ffe65190c909d596485e56460e32ac4549`.

```
  A    exit=255   levensduur=0s     ATTACH-ok=0    ATTACH-EPERM=1
  B    exit=0     levensduur=4s     ATTACH-ok=390  ATTACH-EPERM=0
```

### 8.1 Arm A — de bevestiging ZONDER tracer (GEMETEN, en dit is het antwoord op taak (a))

Het waarnemer-logboek in arm A is **compleet twee regels**:

```
[ravengun-ptrace-log] pid 752447 ppid 752440: PTRACE_TRACEME() -> 0 (ok: ik laat mij volgen)
[ravengun-ptrace-log] pid 752448 ppid 752440: PTRACE_ATTACH(pid 752440) -> -1 FOUT: Operation not permitted
```

Dat is **exact de strace-bevinding**, nu uit een meting die zelf geen tracer is: kind 1 doet `TRACEME` en
dat lukt; kind 2 haakt de ouder (752440) aan en krijgt `EPERM`; exit **255**. **De strace-lezing gaat dus
ook zonder tracer op.** De gast gaf in arm A geen enkele eigen melding.

### 8.2 Arm B — de maatregel haalt de blokkade weg (GEMETEN)

Het maatregel-logboek bevestigt dat de `prctl` in het gastproces is gezet:

```
[ravengun-ptracer-permit] pid 752482: PR_SET_PTRACER_ANY gezet: dit proces mag door een willekeurig
proces worden aangehaakt (yama ptrace_scope blijft ongemoeid)
```

En het waarnemer-logboek laat **390 van 390 `PTRACE_ATTACH` slagen** (`-> 0 (ok)`), met 390 `DETACH`,
allemaal op dezelfde ouder (pid 752482), vanuit **392 verschillende kindprocessen**, en `TRACEME` één keer.
In arm A waren dat er **twee regels** totaal. Daarna eindigt de gast met **exit 0** na **4 s** (arm A:
exit 255 binnen 1 s).

**Dit is het cijfer dat de poort opent: 390 van 390 geslaagde ATTACH's uit 392 verschillende
kindprocessen, tegen twee in arm A.** Een groot en meetbaar verschil: de handshake die de gast wil, lukt
nu volledig in plaats van één keer te mislukken. **AFGELEID** is wat er daarná gebeurt — of de gast met
die 392 kinderen *werkt* (een werkerpool die per taak een proces opstart) of *spint* (een lus die zichzelf
opjaagt) kan ik met deze gegevens **niet** scheiden. Daarom is er §8.5: dat is de meting die dat
onderscheid moet maken, en die is zonder bench voorbereid.

### 8.3 Wat dit betekent, en vooral wat NIET

**TREFFER volgens de tabel van §5.1, rij 1:** de ATTACH slaagt in arm B (390/390) en de gast leeft langer
(4 s tegen <1 s), met een andere exitcode (0 in plaats van 255). **De ptrace/Yama-laag is dus een echte
blokkade en de maatregel haalt hem weg.**

**Maar ik schrijf er meteen bij wat het NIET is, want dat is hier het belangrijkste:**

- **De gast zegt nog steeds niets.** `A.out` en `B.out` zijn **identiek op twee dingen na**: de tijdelijke
  padnaam en de stack-pointer in de `spring naar e_entry`-regel. Beide eindigen met die sprongregel en
  hebben daarna **geen enkele regel van de gast**. Er is dus **geen eigen melding** in arm B.
- **De exitcode 0 is geen "het spel loopt".** Het is een **schone zelfbeëindiging** van de gast. Dat is
  iets anders dan arm A, waar de gast door zijn eigen bescherming werd afgebroken (255). Maar het is ook
  niet het spelbeeld.
- **Het patroon van 390 aanroepen in 4 s is nieuw en vraagt uitleg.** In arm A maakte de gast twee
  kinderen voordat hij stierf; in arm B maakt hij er 392 en doet elk kind één `ATTACH`+`DETACH` op de
  ouder. Dat leest als een lus of als een werker-per-taak-opstart. **AFGELEID** dat dit "de gast komt
  voorbij de poort en begint zijn volgende fase" betekent — het is precies wat je zou verwachten als een
  poort opengaat, maar met deze gegevens kan ik het niet bewijzen.

**Conclusie, eerlijk:** de beschermingslaag is **opgeheven** (bewezen: 390/390 attach gelukt, geen 255
meer), en daarmee is deze laag **niet** meer de blokkade. Het spel loopt nog niet. De gast beëindigt
zichzelf schoon na 4 s zonder iets te zeggen, en **wat hem daartoe brengt is niet gemeten**.

**Volgens de vooraf afgesproken tabel is dit rij 1 en niet rij 2**: rij 2 was "de ATTACH slaagt maar de
gast stopt op dezelfde plek met **255** en zegt niets extra's" — de exitcode is hier **0**, niet 255, en
de gast leeft 4 s in plaats van <1 s. De ptrace-stap is dus geen symptoom gebleken. Maar omdat de gast
ook geen eigen melding geeft, is de eerlijke samenvatting: **deze laag is geklaard, de volgende is nog
niet geïdentificeerd.** De eerstvolgende verdachte blijft de **PhysX-stap** (§2.5), met dezelfde
kanttekening dat `PhysX.conf` in de spelmap ontbreekt.

### 8.4 Wat de volgende run zou moeten uitwijzen

1. **Waarom exit 0 en niet verder?** De logboeken van de gast zelf zijn leeg; de volgende stap is een
   meting die laat zien **wat** de gast na de handshake doet — de HASP-hook (`DRAAIBOEK-BENCH.md` §7) is
   nu wél zinvol, want de gast komt voorbij de poort. Nul `aanroep:`-regels zou dan voor het eerst
   betekenen "de blokkade zit vóór de HASP-API", en dat is nu een geldige conclusie.
2. **De 390 kinderen.** Uitzoeken of dat een werkerpool of een lus is (bijvoorbeeld door de ppid's en de
   tijdstippen mee te loggen). Dat is de goedkoopste manier om te zien of de gast *werkt* of *spint*.
3. **`PhysX.conf`.** Als de gast dat bestand zoekt en niet vindt, is dat de volgende kandidaat; het staat
   niet in de spelmap (GEMETEN).

### 8.5 De vervolgmeting: WERKT de gast of SPINT hij? (voorbereid, wacht op bench)

Het cijfer van §8.2 (392 kinderen in 4 s) is de aanleiding. De vraag is niet triviaal: een werkerpool die
per taak een proces opstart en een lus die zichzelf opjaagt zien er op één teller hetzelfde uit. Wat ze
onderscheidt zijn **tijd en gelijktijdigheid**, dus die gaan we meten.

**Wat er gebouwd is** (zonder bench, en **getest op twee proefprocessen met een bekende vorm**):

- **`fork-log.c`** → `libravengun-fork-log.so`, een 32-bits preload die `fork`, `waitpid` **en** `wait`
  wikkelt (de gast importeert `fork@GLIBC_2.0` en `waitpid@GLIBC_2.0` dynamisch; `wait` is een ánder
  symbool, dus dat wikkelen we ook, anders zou een kind dat via `wait()` wordt opgeraapt voor de analyse
  nog leven). Per regel een monotone tijdstempel, het pid en het ppid, met deze gebeurtenissen: `start`,
  `kind-gemaakt kind=<pid>`, `kind-start`, `einde exit-code=<n>`, en
  `kind-opgeraapt kind=<pid> status=.. <geëindigd met code N | GESTOPT door signaal N>`.
  **De autoriteit is `kind-opgeraapt`, niet `einde exit-code`**: een kind dat via `_exit()` verdwijnt laat
  geen exit-regel achter, en dat zegt **niets** over hoe het eindigde. Alleen het wachtstatus-cijfer bij de
  ouder vertelt of een kind netjes eindigde of gestopt is. `kind-analyse.py` gebruikt daarom dat veld.
  Geeft de gast `wait(NULL)` mee (geen status), dan loggen we `status=(niet opgevraagd door de gast)` —
  levensduur en aantal zijn dan nog steeds gemeten.
- **`ptrace-log.c`** heeft nu ook een tijdstempel per regel (`+<seconden>`), zodat de ptrace-aanroepen op
  dezelfde tijdlijn staan als de kinderen.
- **`kind-analyse.py`** rekent het uit en print de criteria mee, zodat de conclusie narekenbaar is:
  1. aantal kinderen, waarneemvenster, **tempo in kinderen/s**;
  2. **intervallen** tussen opeenvolgende kinderen (min/mediaan/max + verdeling in bakken) — een lus laat
     intervallen naar nul zakken, een pool houdt ze grofweg regelmatig;
  3. **levensduur per kind** en **maximale gelijktijdigheid** — een pool heeft een *gebonden* aantal
     tegelijk en kinderen die lang genoeg leven om werk te doen; een lus heeft gelijktijdigheid 1 en een
     levensduur van bijna nul;
  4. wat elk kind deed (ptrace-aanroepen per kind, geslaagd/EPERM).

**Zelfproef met bekende uitkomst (GEMETEN).** Op twee proefprocessen waarvan we de vorm zelf bepaalden:

| proef | vorm | uitkomst van de analyse |
|---|---|---|
| A | 30 kinderen, elk 4 ms, geforkt elke 3 ms | 30/30 opgeraapt, intervallen 3 ms (100 % in de bak 1-5 ms), **gelijktijdigheid 30**, mediaan levensduur 48,7 ms → **"geen lus"** |
| B | 200 kinderen, elk ~0 ms, strikt na elkaar | 200/200 opgeraapt, intervallen < 1 ms (199/200), **gelijktijdigheid 1**, mediaan levensduur 0,1 ms → **"LUS (SPINT)"** |

De tool maakt het onderscheid dus op proeven waarvan we het antwoord kennen. Twee fouten in mijn eigen
gereedschap kwamen daarbij boven — `wait` was niet gewikkeld, en een `NULL`-status werd overgeslagen —
en die zijn gerepareerd. **Dat is precies waarom de zelfproef er is**, en het is ook de reden om de
uitkomst op de echte gast pas te geloven als deze analyse er hetzelfde over zegt.

**Wat de uitkomst dan betekent, vooraf:**

| waarneming | betekenis | actie |
|---|---|---|
| gebonden gelijktijdigheid (bv. 2-16), kinderen leven milliseconden tot seconden, intervallen regelmatig | **werkerpool**: de gast doet echt werk per kind | dan is de vraag "waarom stopt hij na 4 s" en niet "waarom spint hij"; PhysX/`/g3/` blijven de kandidaten |
| gelijktijdigheid 1, levensduur ≈ 0, intervallen < 1 ms die domineren, >100 kinderen | **lus**: de gast jaagt zichzelf op | dan is het zoeken naar de *reden* dat de cyclus niet stopt, en dat is een andere vraag dan een dichte poort |
| geen enkel `kind-opgeraapt` | de ouder raapt zijn kinderen niet op (of vraagt geen status) | dan weten we alleen DAT ze gemaakt zijn; de volgende vraag is wie ze opruimt |
| weinig kinderen deze keer | de gast gedroeg zich anders dan in de A/B | eerst vaststellen waarom de runs niet vergelijkbaar zijn |

**AFGELEID blijft staan:** werkerpool versus lus is met alleen fork-tijden niet hard te maken. De analyse
geeft een *richting*, geen bewijs; de scripttekst zegt dat er zelf bij.

### 8.6 De HASP-aanroepmeting (voorbereid, wacht op bench)

Zie `hasp-meting.sh` en `DRAAIBOEK-BENCH.md` §7. Die is **nu voor het eerst zinvol**: de gast komt
aantoonbaar voorbij de poort (390 geslaagde ATTACH's), dus **nul `aanroep:`-regels betekent nu echt "de
blokkade zit vóór de HASP-API"** in plaats van "we zijn er nooit geweest". De opzet is één run met de
maatregel + de hook + beide waarnemers, en de uitkomsttabel staat in het script zelf.

---

## 9. De volgende meting: bestanden en `/g3/` (voorbereid, wacht op bench)

De aanroepmeting van §8.6 heeft de blokkade **vóór** de HASP-API gelegd. De volgende vraag is dus: waar
precies? De strace van de laderbouwer noemde al `PhysX.conf` en `libNxCooking.so`, en uit mijn eigen
eerste dossier komt de `/g3/`-hypothese: de engine gebruikt **absolute** paden onder `/g3/`, en
**`/g3` bestaat niet op deze host (GEMETEN)**.

**Geen strace.** Dat is geen voorkeur maar een noodzaak: strace is zelf een tracer en laat de gast op
zijn **eigen** ptrace-poort sterven (GEMETEN in §8.1: kind 2 krijgt EPERM zodra een vreemde al meeleest).
Met strace bereik je het venster met de bestandsopeningen dus nooit.

**Wat er gebouwd is:** `bestand-log.c` → `libravengun-bestand-log.so`, een 32-bits preload die vanuit het
proces zelf meekijkt. Hij dekt precies de functies die de gast **importeert** (GEMETEN met `readelf`):
`open`, `open64`, `openat`, `fopen`, `fopen64`, `opendir`, en de `stat`-familie zoals i386 die gebruikt:
`__xstat`, `__lxstat`, `__fxstat`, `__xstat64`. Het logboek wordt met **rauwe syscalls** geschreven, want
anders zou de waarnemer zijn eigen `fopen` aanroepen en recurseren.

**Zelfproef (GEMETEN, bench-vrij):** op een proef die één bestaand en drie niet-bestaande paden aanraakt
logt hij `open "/etc/hostname" -> ok (3)`, drie keer `FOUT errno=2`, waaronder
`__xstat "/g3/ServiceDongleApp/data/conf/x" -> FOUT errno=2`, plus de `fopen`-variant. De `/g3`-detectie
werkt dus. Eén van mijn twee "zou moeten slagen"-gevallen gaf `errno=75` omdat mijn **proef** de verkeerde
`_STAT_VER` meegaf — een fout in de proef, niet in de waarnemer; de gast geeft zijn eigen correcte versie mee.

**De verwachtingen staan vooraf in `bestand-meting.sh`**, met per uitkomst de betekenis:

| waarneming | betekenis | label |
|---|---|---|
| `PhysX.conf -> FOUT errno=2` | de gast zoekt dat bestand en vindt het niet: eerste concrete kandidaat | GEMETEN zodra het in het logboek staat |
| een `/g3/...`-pad `-> FOUT errno=2` | het absolute dataroot ontbreekt; de oplossing is dan een **host**-inrichting (pad leveren), geen gastwijziging | GEMETEN |
| geen enkele ENOENT | de blokkade zit **niet** in een bestand; volgende verdachte is de engine-logica zelf of de socket/ioctl-kant | GEMETEN |
| hetzelfde pad tientallen keren herhaald | de gast **pollt** daarop — dat verklaart de gepacede lus van 11 ms uit §8.5, één kind per tik | AFGELEID, tenzij de teller het onmiskenbaar maakt |
| bijna geen regels | hij sterft vóór hij iets van zichzelf aanraakt | GEMETEN |

**En de eerlijke grens vooraf:** als *alles* gevonden wordt, is deze meting een **negatief resultaat** en
zeg ik dat met zoveel woorden; dan verschuift het naar de engine-logica of de socket-kant, en niet naar
"nog een bestand zoeken".

---

## 10. Bestanden in deze map

| bestand | wat het is |
|---|---|
| `BESCHERMINGSLAAG-PTRACE.md` | dit dossier |
| `yama-toets.c` | de microtoets: kind haakt ouder aan, met en zonder maatregel |
| `ptracer-permit.c` | **de maatregel** (32-bits preload, zet `PR_SET_PTRACER_ANY`) |
| `ptrace-log.c` | **de waarnemer** (32-bits preload, geen tracer, logt elke `ptrace`-aanroep, nu met tijd) |
| `fork-log.c` | **de kind-waarnemer** (32-bits preload, logt `fork`/start/einde met tijd en ppid) |
| `kind-analyse.py` | rekent werkerpool-vs-lus uit de twee logboeken, met de criteria erbij |
| `yama-ab.sh` | de twee armen van de A/B als één script (`--start` om echt te draaien) |
| `hasp-meting.sh` | de aanroepmeting van de HASP-hook als één script, met de uitkomsttabel vooraf |
| `DRAAIBOEK-BENCH.md` | §7 de adreslijstmeting, §10 de eerdere laderrun, §10b de geslaagde run van de laderbouwer |
| `logs-yama-ab-20261013-0930/` | de vijf logboeken van de A/B (arm A én arm B) |
