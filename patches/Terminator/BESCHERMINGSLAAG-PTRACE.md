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

## 8. Bestanden in deze map

| bestand | wat het is |
|---|---|
| `BESCHERMINGSLAAG-PTRACE.md` | dit dossier |
| `yama-toets.c` | de microtoets: kind haakt ouder aan, met en zonder maatregel |
| `ptracer-permit.c` | **de maatregel** (32-bits preload, zet `PR_SET_PTRACER_ANY`) |
| `ptrace-log.c` | **de waarnemer** (32-bits preload, geen tracer, logt elke `ptrace`-aanroep) |
| `yama-ab.sh` | de twee runs op de bench als één script (`--start` om echt te draaien) |
| `DRAAIBOEK-BENCH.md` | §10 de eerdere laderrun, §10b de geslaagde run van de laderbouwer, §7 de adreslijstmeting |
