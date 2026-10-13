# De beschermingslaag waar Terminator Salvation op stopt: ptrace + Yama

**Game:** Terminator Salvation (`game`, ELF32 i386, sha256 `798dec07…b206a`)
**Laag:** de gast wil dat zijn eigen kindproces hem mag aanhaken met `ptrace`; de host verbiedt dat.
**Status:** oorzaak **gemeten** (strace) + mechanisme **gemeten zonder tracer** (microtoets); de
game-run ter bevestiging en de A/B staan klaar en wachten op de bench.

Alles hieronder is read-only op de NAS. Er is niets aan de gast veranderd.

---

## 1. Wat de laderbouwer met strace mat (overgenomen, METEN)

Uit `~/bw/terminator-elf/run2-strace/` en `run3-strace/`:

```
na de sprong:
  de gast zoekt `PhysX.conf`, opent `libNxCooking.so`, en er komen TWEE kindprocessen;
  kind 1 doet  ptrace(PTRACE_TRACEME)
  kind 2 doet  ptrace(PTRACE_ATTACH, ouder)  ->  EPERM   en eindigt met 1
  de ouder ziet dat, geeft kind 1 SIGKILL, en beeindigt zichzelf met -1  (= exit 255)

kernel.yama.ptrace_scope = 1
```

**Waarom dat een strace alleen niet afmaakt (en dit is geen muggenzifterij):** `strace` is zélf een
tracer. Een proces dat al gevolgd wordt, gedraagt zich anders: `PTRACE_TRACEME` van kind 1 wordt
afgewezen zodra de ouder al gevolgd is, en de ouder ziet dus andere uitkomsten. De strace **bewijst de
aanroep en de EPERM**, maar niet dat de 255 zonder tracer dezelfde oorzaak heeft. Daarom §3.

---

## 2. Wat ik zonder tracer heb gemeten (METEN, `ts/71`)

### 2.1 De gast importeert ptrace dynamisch — dus een preload kan meekijken

```
$ readelf -sW --dyn-syms "game" | awk '$7=="UND"' | grep -iE 'ptrace|fork|wait'
   2: … UND waitpid@GLIBC_2.0
  26: … UND fork@GLIBC_2.0
 347: … UND ptrace@GLIBC_2.0
```

Omdat de aanroep via de PLT loopt, is hij met `LD_PRELOAD` te onderscheppen **zonder dat wij tracer
worden**: `libravengun-ptrace-log.so` wikkelt alleen de `ptrace`-functie in het proces zelf en laat elke
aanroep **ongewijzigd** door naar de echte `ptrace`.

### 2.2 De A/B op systeemniveau (`yama-toets.c`)

Een vaderproces en een kind; het kind doet `PTRACE_ATTACH` op zijn vader — precies de vorm van kind 2.

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

**Dit is exact het gedrag uit de strace: kind faalt met EPERM en eindigt met 1.** En het laat zien dat
één `prctl` in de ouder het omkeert.

### 2.3 Dezelfde A/B, maar via de echte maatregel (LD_PRELOAD)

```
zonder preload        : PTRACE_ATTACH -> -1, errno=1   ; kind-eindstatus 256 (exitcode 1)
met libravengun-ptracer-permit.so
                      : PTRACE_ATTACH -> 0, errno=0   ; kind-eindstatus 0
  logboek: [ravengun-ptracer-permit] pid 746914: PR_SET_PTRACER_ANY gezet: dit proces mag door een
           willekeurig proces worden aangehaakt (yama ptrace_scope blijft ongemoeid)
```

### 2.4 De waarnemer werkt (en is geen tracer)

```
[ravengun-ptrace-log] pid 746929 ppid 746926: PTRACE_ATTACH(pid 746926) -> -1 FOUT: Operation not permitted
```

Die regel komt uit een gewoon preload-proces, niet uit strace. **Daarmee is de oorzaak buiten strace
vast te stellen**; de game-run die dat op de echte gast bevestigt staat klaar in `yama-ab.sh` (arm A).

### 2.5 AFGELEID: waarom `PhysX.conf` en `libNxCooking.so` in de strace staan

De gast zoekt `PhysX.conf` en opent `libNxCooking.so` — dat is PhysX-initialisatie, en die twee
kindprocessen zijn de PhysX-cooking-subprocessen. **AFGELEID**, niet gemeten: dat de ptrace-handshake
deel is van die PhysX-opstart (of van een anti-debugcontrole eromheen) kan ik uit deze gegevens niet
scheiden. Wat vaststaat is de volgorde: PhysX-bestanden, dan de twee kinderen, dan EPERM, dan 255.

`libNxCooking.so` en `libPhysXCore.so` staan in `ElfLdr2/libs` en zijn dus beschikbaar; `PhysX.conf`
heb ik in de spelmap niet gevonden (**gemeten**: geen `*PhysX*` of `*.conf` in de bovenste drie niveaus),
wat betekent dat de gast die zoektocht ziet mislukken en doorgaat — de 255 komt van de ptrace-stap.

---

## 3. De maatregel: `libravengun-ptracer-permit.so`

Een 32-bits preload van ~15 kB waarvan de constructor één ding doet:

```c
prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
```

In de laderrun is dat het proces **waarin de gast draait** — precies het proces dat kind 2 wil aanhaken.

### 3.1 Waarom dít, en niet iets anders

- **Per proces, niet hostbreed.** `/proc/sys/kernel/yama/ptrace_scope` wordt **niet** aangeraakt. Er is
  geen root nodig en er verandert niets voor andere programma's op de machine.
- **Het dooft uit met het proces.** Zodra de game stopt, is de ruimte weg. Er blijft geen systeem-
  instelling achter (in tegenstelling tot `ptrace_scope=0`, wat een blijvende hostwijziging zou zijn en
  veel breder dan nodig).
- **Het is het mechanisme dat de kernel er zelf voor geeft.** `PR_SET_PTRACER` is de officiële,
  gedocumenteerde ontsnappingsroute van Yama: "dit proces stemt ermee in om gevolgd te worden". We
  gebruiken de deur, we breken hem niet open.
- **De gast wordt niet aangeraakt.** Geen patch, geen vervalste retourwaarde, geen aangepaste
  vergelijking, geen hook op de controle zelf. De gast vraagt iets; de kernel zegt ja.

### 3.2 De grensvraag, eerlijk beantwoord

De vraag is of dit "de host even toeschietelijk maken als de kast" is, of "de controle laten slagen".

**Mijn oordeel: het eerste, en dat is te onderbouwen.** Drie punten:

1. **Wat wordt weggenomen is een eigenschap van de HOST, niet van de game.** `yama.ptrace_scope` is een
   Linux-hardeningfunctie uit 2010+ (Yama kwam in 2.6.36, later dan deze kast). Op de originele kast was
   er geen Yama, dus daar slaagt deze aanroep zonder meer. Wij veranderen de kernel niet en de game
   niet; we zetten één proces in de toestand die op de kast de normale was.
2. **De bescherming wordt niet omzeild, ze wordt bevredigd.** Als de gast controleert "mag mijn kind mij
   volgen?", dan is het antwoord op de kast ja. Wij zorgen dat het antwoord hier ook ja is, op de manier
   die de kernel daarvoor aanbiedt. Er wordt geen keuring overgeslagen en geen uitkomst verzonnen.
3. **Er is precedent in dit project, bewust en opt-in.** `docs/research/jurassicpark-stand.md` regel
   105: de GL-fix zet onder `RAVENGUN_GLFIX_FAULT=1` al `prctl(PR_SET_PTRACER_ANY)` "zodat een losse
   gdb kan aanhaken ondanks `ptrace_scope=1`". Dit is dus geen nieuwe categorie maar dezelfde maatregel,
   nu niet voor diagnose maar omdat de gast erom vraagt.

**Waar voor mij de grens wél ligt, en waar ik zou stoppen:** als de volgende stap zou zijn om de
vergelijking in de gast te patchen, `ptrace` een verzonnen `0` te laten teruggeven, de controle over te
slaan of een retourwaarde te faken, dan is dat het omzeilen van de bescherming zelf en dan doe ik het
niet — dat is een eigenaarsbeslissing. Dat is hier niet nodig: de kernel geeft het juiste antwoord
zodra het proces toestemming geeft.

**Twee dingen die ik er expliciet bij zeg, zodat niemand later verrast is:**

- **`PR_SET_PTRACER_ANY` is breder dan strikt nodig.** Het staat *elk* proces toe dit proces aan te
  haken, niet alleen het kind van de gast. Smaller kan met `PR_SET_PTRACER` plus het concrete PID van
  dat kind, maar dat PID kent de ouder pas nadat de gast zelf geforkt heeft, dus dat vraagt een hook op
  `fork` in de gast. Dat is meer ingreep in de gast voor minder breedte; ik geef er de voorkeur aan om
  het bij de kern-maatregel te houden en de breedte te melden. **Als de eigenaar smaller wil, is dat
  een kleine vervolgstap** en dan doe ik dat.
- **De maatregel staat in de preload van de run, niet in de lader.** Daarmee is hij per game aan en uit
  te zetten en hoeft de laderbouwer er niets voor te veranderen.

### 3.3 Een eerdere meting die ik niet kan reproduceren (METEN, en een waarschuwing)

`docs/research/jurassicpark-stand.md` zegt dat een **kale `gdb`-attach ondanks `PR_SET_PTRACER_ANY`
toch `EPERM` gaf**. In mijn microtoets lukt een kale `PTRACE_ATTACH` mét de maatregel wél. Het verschil
is vermoedelijk *welk* proces de `prctl` zette (de maatregel geldt voor het proces dat hem zet, niet
voor zijn kinderen). **AFGELEID**, en daarom noteer ik het: als een attach in de praktijk alsnog EPERM
geeft, is de eerste vraag "in welk proces is de `prctl` gezet?" en niet "werkt de maatregel niet?".

---

## 4. De A/B op de bench (`yama-ab.sh`)

Twee runs, **identiek opgezet**, met als enige verschil of de maatregel voorgeladen is. Beide armen
hebben de waarnemer aan, zodat "waar stopt hij" meetbaar is zonder tracer.

| | arm A | arm B |
|---|---|---|
| `libravengun-ptrace-log.so` | ja | ja |
| `libravengun-ptracer-permit.so` | **nee** | **ja** |
| verwachting | `PTRACE_ATTACH → EPERM`, exit 255 | `ATTACH → 0`, gast leeft langer of geeft eigen melding |

Per arm wordt vastgelegd: **exitcode**, **levensduur**, **of de ATTACH slaagde** (uit het
waarnemer-logboek) en **of de gast zijn eigen melding gaf**. De uitkomst van arm A is tegelijk het
antwoord op taak (a): **dezelfde oorzaak aantonen zonder strace.**

**Wat een treffer is:** in arm B slaagt de `ATTACH` **en** leeft de gast langer **of** geeft hij een
eigen melding (bijvoorbeeld de donglemelding uit `LADER-README.md` §6, of een PhysX-fout). Een langere
levensduur alleen al is een treffer, want dan is de ptrace-blokkade opgeheven.

**Wat een negatief resultaat betekent, en dat meld ik dan net zo hard:** als de `ATTACH` in arm B
slaagt maar de gast alsnog met 255 stopt op precies dezelfde plek, dan was de ptrace-stap **niet** de
blokkade maar een symptoom, en dan is de volgende verdachte de PhysX-stap zelf (`PhysX.conf` dat
ontbreekt, of een tweede controle). Dan gaat deze laag terug naar "gemeten, maar niet de oorzaak".

---

## 5. Status en wat er nog moet gebeuren

| stap | status |
|---|---|
| oorzaak vastgesteld met strace | **gemeten** (laderbouwer) |
| mechanisme bewezen op systeemniveau, A/B | **gemeten** (§2.2, zonder game) |
| maatregel gebouwd en bewezen via preload | **gemeten** (§2.3) |
| waarnemer gebouwd, geen tracer | **gemeten** (§2.4) |
| bevestiging op de ECHTE gast zonder strace (arm A) | **wacht op bench** |
| A/B op de echte gast (arm B) | **wacht op bench** |

Opdracht: **vraag de bench aan bij de coördinator** zodra de Luigi-werker klaar is, draai
`bash yama-ab.sh --start`, en meld exitcode + levensduur + ATTACH-uitkomst + eigen melding per arm.
