# Terminator Salvation (Raw Thrills, 2010) — Linux-pagina

**Wat dit is.** Een publiek verslag van wat wij hebben gemeten aan deze kast en aan de Linux-kant
eronder, zodat iemand die later een eigen front-end, emulator of launcher bouwt niet opnieuw hoeft te
beginnen. **Elke bewering is gelabeld: GEMETEN (met cijfer, tijdstip of exact pad) of AFGELEID.** Waar
iets niet gemeten is, staat dat er.

**Wat hier NIET in staat.** Alles over hardwarematige beveiligings- en licentielagen (dongles,
keychips, integriteitscontroles) en onze notities daarover. Dat hoort bij het spel zelf, niet bij deze
publieke pagina, en het is bewust weggelaten. Deze pagina gaat over het spel en over de lader.

**Wat de kast is.** Een arcade-lichtgeweerkast van **Raw Thrills** uit **2010**: *Terminator
Salvation*. Het is een PC-achtige kast met een **32-bits i386 Linux** eronder en een geweer per speler.

**Het spelbestand dat wij onderzoeken.** `game` — 7.764.284 bytes, **ELF32 i386, ET_EXEC** (niet-PIE,
vast laadadres `0x08048000`), `e_entry = 0x08057240`, **13 `DT_NEEDED`** bibliotheken
(`libGL`, `libGLU`, `libglut`, `libpthread`, `librt`, `libtheora`, `libogg`, `libPhysXLoader`, `libm`,
`libgcc_s`, `libc`, `libdl`, `libstdc++`), sha256
`798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a`.
Naast `game` staan er nog drie 32-bits ELF's in de map: `t4-patch` (1.368.560 B),
`irgun/gunrec_update` (1.057.072 B) en `ServiceDongleApp/ServiceDongleApp` (8.994.378 B). **GEMETEN.**
De mapnaam op onze NAS eindigt op een **spatie** (`Terminator Salvation `) — dat kost je een middag als
je het niet weet, want sommige tools melden dan "directory name invalid".

> **Eén misverstand meteen weg:** `game` is **niet gepakt**: er zijn 29 sectiekoppen, 496 PLT-stubs,
> 1020 dynamische symbolen en **nul** `0xdeadbeef`-stubs; **0 van 497** importnamen is gewist. Dat is
> een zeldzaam prettige uitgangspositie: alles wat het spel importeert heeft een naam en is oplosbaar.
> **GEMETEN.**

---

## 1. Platform en kast

* **Kast:** Raw Thrills-lichtgeweerkast, 2010. Twee geweren. **GEMETEN** uit het spel zelf (het
  vraagt om `p1.x/p1.y/p2.x/p2.y`, twee start-knoppen en twee triggers) en uit de bestanden.
* **Systeem eronder:** **32-bits i386 Linux**. Het spel is een gewone glibc-ELF met
  `/lib/ld-linux.so.2` als interpreter en `DT_NEEDED` naar GL/GLUT/PhysX/Theora/Ogg. **GEMETEN.**
* **Beeld/API:** OpenGL via **GLUT** (het spel importeert 15 `glut*`-functies) en **PhysX 2.8**
  (`libPhysXLoader` + `libPhysXCore`, en tijdens de start opent het spel `libNxCooking.so`).
  `DT_RPATH` in het spel is `/usr/lib/PhysX/v2.8.1/`. **GEMETEN.**
* **Invoer / het I/O-spoor:** het spel opent bij de start actief **`/dev/sierra2`** en
  **`/dev/ttyS0`** (`O_RDWR|O_NONBLOCK`) en probeert **`/dev/js0`** (`O_RDONLY|O_NONBLOCK`) en
  **`/dev/dsp`** (`O_WRONLY`). De combinatie van een **Sierra**-knoop met een seriële poort en twee
  geweren wijst op een **serieel geweer-/bordtransport** voor deze titel. **GEMETEN** dat die paden
  worden geopend; **AFGELEID** dat `/dev/sierra2` het bordtransport van deze dialect is — het is niet
  bewezen dat het spel er iets mee doet als de knoop bestaat.
* **Ons I/O-profiel voor deze kast** (in onze eigen tooling) heet `RawThrillsGUN`: een blok van 64
  bytes met de knoppen op offset 8 (test, service, coin1/2, per speler start/trekker/granaat/herladen,
  volume) en **1 byte per as op offset 12/16/20/24** voor `p1.x`, `p1.y`, `p2.x`, `p2.y`, met
  links/boven = 0. **GEMETEN** (dat is de indeling die onze brug gebruikt en die met de echte Move
  werkte: richthoek binnen ~1 %).

## 2. Wat het spel nodig heeft — en wat er in een dump ontbreekt

Alles hieronder komt uit een **bestandsmeting**: wij lieten het spel 5 seconden draaien met een
`LD_PRELOAD`-logger die elke `open`/`fopen`/`opendir`/`stat` van het spel vastlegt (geen `strace`, zie
§8). In totaal **88 regels** echte spelactiviteit. De fouten, gegroepeerd:

| wat het spel vraagt | hoe vaak | bestaat dat in de dump? |
|---|---|---|
| **`/g3/T4Data/etranslation/english.csv`** | 2× | **JA**, als `T4Data/etranslation/english.csv` (5.168 B) |
| **`/g3/T4Data/etranslation_app/english.csv`** | 2× | **JA**, als `T4Data/etranslation_app/english.csv` (5.880 B) |
| **`/T4User/log/syslog_bb3.txt`** (absoluut) | 8× | **JA**, als `T4User/log/syslog_bb3.txt` (12.019 B) |
| **`syslog_bb3.txt`** (relatief t.o.v. de werkmap) | 8× | nee — staat alleen in `T4User/log/` |
| `/home/deck/.PhysX.conf` en `/etc/PhysX.conf` | 1× elk | **nee**, nergens |
| `nethasp.ini`, `/home/deck/nethasp.ini`, `/etc/nethasp.ini` | 1× elk | **nee**, nergens |
| `/dev/js0` · `/dev/dsp` · `/dev/sierra2` | 1× elk | nee |
| `/dev/ttyS0` | 1× | **ja, maar `EPERM`** (rechten `660 root:uucp`) |

**De kern in één zin: het spel gebruikt twee ABSOLUTE wortels, `/g3/` (zijn datamap) en `/T4User/`
(zijn gebruikersmap), en die bestaan op een gewone machine niet — terwijl de bestanden die het daar
zoekt wél in de dump staan, alleen onder een *relatief* pad.** **GEMETEN.** Alle andere paden die het
spel met succes opent zijn wél aanwezig, inclusief de 13 bibliotheken en de USB-opsomming
(`/dev/bus/usb/...`).

**Wat optioneel blijkt.** Het spel **stopt niet** bij de dingen die het niet vindt: het leeft na
`PhysX.conf` nog **4,1 seconde** door en eindigt met **exit 0**. Die twee bestanden worden ook niet
hetzelfde aangeraakt: `PhysX.conf` wordt met `mode="rb"` geopend, dus **gelezen**; het andere wordt
alleen met `stat` **gepeild** op bestaan en niet gelezen. **GEMETEN** dat ze ontbreken en hoe ze worden
aangeraakt; **AFGELEID** dat ze geen onmiddellijke fataal zijn.

**Ons besluit over die twee configuratiebestanden, en dat is een principe:** wij leveren ze **niet**
aan. In de dump, in de megeleverde runtime en in onze eigen repositories bestaat **geen bron** voor hun
inhoud. **Inhoud verzinnen zou de volgende meting onbruikbaar maken**, want dan meet je je eigen
verzinsel. **GEMETEN** dat er geen bron is.

## 3. Het officiële startscript

Wij hebben in deze map **geen officieel startscript** gevonden. Wat er wel staat is
`GeneralPurposeStateMachine.txt` (5.782 B, een state-machinebeschrijving) en de mappen `T4Data`,
`T4User`, `env`, `irgun` en `ServiceDongleApp`.

**Waarschuwing voor iedereen die dit leest:** als je ergens een start- of installatiescript van een kast
tegenkomt, **voer het niet uit** om te kijken wat het doet. Neem alleen de delen over die je nodig hebt
(en lees ze eerst). Zulke scripts zijn geschreven voor de kast, niet voor jouw machine, en ze kunnen
schrijven naar paden die je niet verwacht.

## 4. De route die wij gebruiken

**De lader.** Onze eigen 32-bits ELF-lader (`rg-elfloader`) mapt de `PT_LOAD`-segmenten van het spel op
hun eigen adressen, past de `R_386`-verplaatsingen toe (`GLOB_DAT`, `JMP_SLOT`, `COPY`), bouwt een
"shim" die de symbolen van het spel exporteert, en springt naar `e_entry`. Voor deze game werkt dat:
**`spring naar e_entry 0x8057240`**, en daarna doet het spel zelf iets. **GEMETEN.**

> **Let op voor wie dit namaakt:** onze lader las het aantal spelsymbolen eerst alleen uit `DT_HASH`.
> Dit spel heeft **geen `DT_HASH`**, alleen **`DT_GNU_HASH`** — dan bleef de teller nul en stopte de
> lader vóór alles. De oplossing is het aantal afleiden uit `(DT_STRTAB − DT_SYMTAB) / 16` (hier exact
> **1020**) of `DT_GNU_HASH` zelf parsen. **GEMETEN** (`1020 gast-symbolen (bron: (DT_STRTAB-DT_SYMTAB)/16, geen DT_HASH)`).

**De sandbox (bubblewrap).** Wij draaien het spel in een `bwrap`-sandbox met **expliciete binds**, niet
met `--ro-bind / /`. Dat is geen stijlkeuze maar een noodzaak:

* **`--ro-bind / /` werkt niet** als je nieuwe paden wilt toevoegen: `bwrap` kan dan `/g3` niet
  aanmaken → `bwrap: Can't create file /g3: Read-only file system`. Je moet de root uit **losse
  binds** opbouwen (`--ro-bind /usr /usr`, `--ro-bind /etc /etc`, …), dan blijft het root zelf een
  tmpfs en zijn nieuwe paden wél te maken. **GEMETEN.**
* **`/lib`, `/lib64`, `/bin` en `/sbin` zijn SYMLINKS** naar `usr/…`. Neem die over als
  `--symlink usr/lib /lib` en **niet** als `--ro-bind` van hun inhoud. Doe je dat wel, dan klopt de
  relatieve keten van de **32-bits** interpreter niet meer
  (`/usr/lib/ld-linux.so.2 -> ../lib32/ld-linux.so.2`) en sterft de lader met **exit 127** en
  `env: '…/rg-elfloader': No such file or directory` — terwijl het bestand er wél staat. **GEMETEN**
  (dat is ons precies overkomen).
* **Schrijvende mappen: gebruik een overlay.** Voor een map waarin het spel schrijft (hier `T4User`)
  gebruiken wij `--overlay-src <bron> --overlay <upper> <work> <doel>`: de **bron blijft ongerept** en
  alle schrijfacties landen in de `upper` (bij ons in `/tmp`). Dat is de nette manier om een spel te
  laten schrijven zonder de bron aan te raken. **GEMETEN**: het spel schreef 12.481 B in de upper
  terwijl het bronbestand op 12.019 B en zijn oude tijdstip bleef staan.

**De exacte opdrachten.** Onze run (ingekort; `<SPEL>` is de map op de NAS, `<W>` een schrijfmap in
`/tmp`):

```sh
# eenmalig: de shim-header die op dit systeem ontbreekt
INC=~/projects/ravengun/tools/rg-elfloader/inc        # bevat gnu/stubs-32.h

bwrap --die-with-parent \
  --ro-bind /usr /usr \
  --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin \
  --ro-bind /etc /etc --proc /proc --dev /dev --dev-bind-try /dev/pts /dev/pts \
  --dev-bind-try /dev/dri /dev/dri --dev-bind-try /dev/snd /dev/snd \
  --ro-bind /home /home --ro-bind /var /var \
  --ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 \
  --bind /tmp /tmp \
  --dir /g3 --ro-bind "<SPEL>" /g3 \
  --overlay-src "<SPEL>/T4User" --overlay <W>/upper <W>/work /T4User \
  --setenv LD_LIBRARY_PATH "<elfldr2-libs>:<lib32>" \
  --chdir "<W>/view" \
  rg-elfloader "<W>/view/game"
```

En de werkmap (`<W>/view`) bevat naast het spel `game` en de datamappen.

## 5. Invoer

* **GEMETEN:** het spel opent `/dev/sierra2`, `/dev/ttyS0` (`O_RDWR|O_NONBLOCK`), `/dev/js0`
  (`O_RDONLY|O_NONBLOCK`) en `/dev/dsp` (`O_WRONLY`), en het somt USB-apparaten op via
  `opendir("/dev/bus/usb")`. Bij onze eerste meting vond het daar echte nodes en opende die.
* **GEMETEN:** onze eigen brug met het `RawThrillsGUN`-profiel werkte met de echte Move-controllers:
  het JVS-achtige kanaal verbond, het vizier volgde het raster binnen ~1 %, trekker en herladen
  werkten. Dat is gemeten met de **kast-SDK aan de andere kant**, niet met de lader hier beschreven.
* **AFGELEID:** dat `/dev/sierra2` het bordtransport van deze titel is. De naam (Sierra) plus een
  seriële poort plus twee geweren wijst die kant op, maar wij hebben niet gemeten wat het spel met die
  knoop doet als hij bestaat.
* **Onze aanpak voor die knoop, en waarom:** wij leveren **geen** echte seriële poort en wij verruimen
  **geen** rechten. In plaats daarvan maken wij een **synthetische knoop** (een PTY-paar, via `socat`,
  als `/dev/sierra2` in de sandbox gebonden) die onze brug kan aandrijven. **GEMETEN** dat het spel die
  knoop vervolgens **opent zonder fout**; **AFGELEID** dat dat de juiste transportvorm is.
* **Rechten als aandachtspunt:** `/dev/ttyS0` is `crw-rw---- root:uucp`. Een gewone gebruiker krijgt
  daar `EPERM` (errno 13). Dat is een **hostpermissie**; wij laten hem staan en lossen het niet op door
  rechten te verruimen. **GEMETEN.**

## 6. Timing

* **Levensduur in onze runs:** **5 seconden**, dan **exit 0** (het spel beëindigt zichzelf netjes).
  Zonder onze koppelingen was dat ook 5 s; met de koppelingen ook. **GEMETEN.**
* **Opstarttijden binnen die 5 s (GEMETEN, tijdstippen in seconden na de sprong):**
  `0,026` `PhysX.conf` peilen · `0,026` eerste kindproces · `0,031` `/dev/urandom` lezen ·
  `0,032` `nethasp.ini` peilen · `0,036` tweede kind, daarna elke **~11 ms** een kind · `4,04`
  eindburst (syslog, vertaal-CSV's, apparaatknopen) · `4,15` het spel beëindigt zichzelf.
* **Waarschuwing over warme en koude cache.** Onze runs lazen het spel van een **netwerkmount**
  (CIFS); de eerste lezing van een bestand kan daardoor tientallen milliseconden tot seconden kosten,
  en daarna is het bestand in de cache. Alle tijdstippen hierboven zijn dus **niet** geschikt om
  prestaties mee te vergelijken, en een tweede run is meetbaar sneller op dezelfde plek. Vergelijk
  alleen runs met dezelfde opzet en hetzelfde tijdstip in de reeks.
* **Wat de 5 s niet is:** dat is geen laadscherm dat blijft staan, en ook geen spel. Zie §7 en §9.

## 7. Wat we op het scherm zagen

**Niets.** In onze runs verscheen geen venster en geen spelbeeld: de lader sprong naar `e_entry`, het
spel deed 5 seconden zijn ding (waaronder de kindprocessen van §8), en beëindigde zichzelf met code 0.
Wij zagen ook **geen enkele eigen melding van het spel** in de uitvoer; de enige regels zijn die van de
lader. **GEMETEN** (de uitvoerbestanden van beide runs eindigen met de regel
`spring naar e_entry 0x8057240 (esp …, argc 1, envc …, 17 auxv)` en daarna niets).

## 8. De eigenaardigheid die een buitenstaander moet weten

**Dit spel sterft op zijn eigen debug-poort zodra een vreemde meeleest.** Na de sprong forkt het spel
kinderen; het eerste kind doet `ptrace(PTRACE_TRACEME)` en een volgend kind doet
`ptrace(PTRACE_ATTACH, ouder)`. Op een moderne Linux met `kernel.yama.ptrace_scope = 1` mag een **kind**
zijn **ouder** niet aanhaken (alleen een voorouder mag een nakomeling volgen), dus die `ATTACH` krijgt
**`EPERM`**, het kind eindigt met 1, de ouder ziet dat en beëindigt zichzelf → **exit 255**.

* **GEMETEN** in een gewone run: kind 1 `PTRACE_TRACEME() -> 0`, kind 2
  `PTRACE_ATTACH(ouder) -> -1 FOUT: Operation not permitted`, exit 255.
* **GEMETEN** dat het met toestemming van de ouder (`prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY)` in het
  proces waarin het spel draait) wél lukt: **390 van 390** aanhakingen geslaagd, en het spel leeft dan
  4 seconden in plaats van dood te vallen. Dit is een **hostbeperking** die op de kast niet bestond; wij
  zetten de toestemming per proces en raken de systeeminstelling niet aan (`ptrace_scope` blijft 1).
* **De praktische waarschuwing: gebruik hier GEEN `strace`.** `strace` is zelf een tracer, en dan sterft
  het spel op deze poort vóórdat je het venster bereikt waarin je iets wilt zien. Wil je tóch meekijken,
  doe het dan vanuit het proces zelf (een `LD_PRELOAD`-bibliotheek die de functie wikkelt), niet met een
  tracer. **GEMETEN**: met `strace` bereik je de bestandsopeningen niet; met een eigen preload wel.

**De gepacede lus — een gemeten verschijnsel zonder gemeten oorzaak.** Na de start forkt het spel in
**~4 seconden** ongeveer **390 kinderen**, één per **11 ms**, met deze maten (identiek in drie runs,
met en zonder koppelingen):

* intervallen tussen de kinderen: **0,011 s** mediaan (100 % tussen 5 en 20 ms);
* **mediane levensduur per kind: 0,4 ms**;
* **maximaal 2 kinderen tegelijk**;
* **exact één `ptrace`-aanroep per kind**; geen enkel kind door een signaal gestopt;
* daarna een eindburst en een **schone exit 0**.

Dat is geen werkerpool (kinderen leven te kort om werk te doen) en geen klassieke spin (die zakt naar
nul in plaats van een metronoom van 11 ms te houden). **Wij noemen het een *gepacede* lus: één kind per
tik, elk met één handshake.** Dat de koppelingen uit §2 deze lus **niet** veranderen (zelfde 11 ms,
zelfde 0,4 ms, zelfde gelijktijdigheid 2) is ook **GEMETEN** — en het betekent dat de oorzaak ergens
anders zit. **AFGELEID, en niet bewezen:** dat de ouder "niets anders te doen heeft dan tikken" nadat
iets in de start niet lukte; de lus loopt namelijk door nadat de ontbrekende bestanden wél gevonden
worden.

## 9. Stand van zaken en open vragen

**De lat die wij onszelf stellen:** *gehaald = vijf minuten attract- of spelmodus*, en een beginscherm of
laadscherm telt niet.

**Stand: NIET gehaald.** Onze beste run komt tot **5 seconden** en **exit 0**, zonder beeld. Wat wél
staat:

* het spel **laadt en start** via onze eigen lader (1020 symbolen, 13 van 13 bibliotheken,
  `spring naar e_entry 0x8057240`); **GEMETEN**;
* wij weten **exact welke paden** het zoekt en **welke daarvan ontbreken** (§2); **GEMETEN**;
* de twee absolute wortels zijn **bewezen** de oorzaak van **20 van de 29** foutregels, en met een
  koppeling gaan er **12** weg zonder dat er één byte inhoud verzonnen wordt; **GEMETEN**;
* het spel **schrijft** naar zijn gebruikersmap en dat blijft **buiten de bron** (overlay); **GEMETEN**;
* de **gepacede lus** is gekarakteriseerd, en hij verandert niet door de koppelingen; **GEMETEN**.

**Open vragen, met per punt de volgende stap:**

1. **De relatieve `syslog_bb3.txt` in de werkmap** (de enige fout die nog 8× voorkomt). Wij leggen een
   **kopie van het bestand uit de dump** in de werkmap; dat is bestaande inhoud, geen verzinsel. De
   volgende stap is meten of die kopie de laatste 8 foutregels wegneemt en of het spel dan verder komt.
   *Let op:* de **modus** waarin het spel dat relatieve bestand opent hebben wij nog niet gemeten — onze
   logger logt sinds kort de vlaggen (`flags=`) en de `fopen`-mode, dus dat is de eerstvolgende meting.
2. **De apparaatkant.** `/dev/dsp` (OSS-audio) en `/dev/js0` (oude joystick) bestaan op een moderne
   Linux niet; `/dev/ttyS0` geeft `EPERM`. Stap: kijken wat het spel doet als die knopen er **wel**
   zijn (synthetische PTY's in de sandbox), en of de audio- en invoerpaden dan doorlopen.
3. **Wat de gepacede lus drijft.** Stap: de kinderen niet alleen op tijd meten maar ook **wat ze doen**
   (system calls per kind), want de lus is nu een verschijnsel zonder oorzaak.
4. **De twee configuratiebestanden** (`PhysX.conf`, `nethasp.ini`). Stap: **eerst meten** wat er gelezen
   wordt — en dat kan nu, want de logger geeft de vlaggen en de mode. Inhoud leveren doen wij pas als er
   een echte bron is; verzinnen is uitgesloten.

## 10. Wat Linux-specifiek is aan deze game

Voor iemand die dit op een andere machine of een ander systeem wil nabouwen: dit is wat wij aan de
**Linux-kant** zijn tegengekomen. Per punt staat of het **GEMETEN** is of een **aanname**.

* **32-bits.** Het spel is i386. Op een 64-bits distributie heb je de 32-bits runtime nodig
  (`/usr/lib32`, `ld-linux.so.2`), en een **32-bits lader**; een 64-bits proces kan de gast niet
  draaien. **GEMETEN.**
* **De ontbrekende dev-header.** Op SteamOS-achtige systemen ontbreekt `gnu/stubs-32.h`, waardoor
  `gcc -m32` faalt. Wij zetten er een minimale versie neer en compileren met `-I` naar die map
  (`tools/rg-elfloader/inc`). **GEMETEN** — zonder dat bouwt niets.
* **Sandbox met expliciete binds.** `bwrap` met `--ro-bind / /` werkt **niet** als je `/g3` of
  `/T4User` wilt toevoegen (read-only file system). Bouw de root uit losse binds. **GEMETEN.**
* **De `/lib`- en `/bin`-symlinks.** Neem ze over als `--symlink`, niet als bind van hun inhoud, anders
  breekt de relatieve 32-bits interpreterketen en sterft de lader met **exit 127** terwijl het bestand
  bestaat. **GEMETEN.**
* **`/dev` om een knoop toe te voegen.** In een gebonden `/dev` (`--dev-bind /dev /dev`) kun je **geen**
  nieuwe knoop aanmaken (`Permission denied`). Gebruik `--dev /dev` (bwrap's eigen devtmpfs) en bind de
  bestaande knopen apart. En **`/dev/ptmx` niet binden**: dat is daar een symlink en levert
  `Can't mount on symlink destination`. **GEMETEN.**
* **`/dev/pts` van de HOST binden** als je een PTY van buiten de sandbox wilt gebruiken; anders is
  `/dev/pts` een eigen instantie en is jouw PTY onbereikbaar. **GEMETEN.**
* **Schrijven zonder de bron te raken: overlayfs** (`--overlay-src` + `--overlay`). Op een systeem
  zonder overlayfs (of in een container zonder) moet je een andere weg kiezen: een kopie van de map
  maken, of het spel via `LD_PRELOAD` omleiden. **GEMETEN** dat overlay hier werkt; de alternatieven
  zijn **aannames**.
* **Apparaatrechten.** `/dev/ttyS0` is `660 root:uucp`; een gewone gebruiker krijgt `EPERM`. Op Windows
  bestaat dat pad niet en praat je met een heel andere driverlaag (of via een USB-HID-pad). **GEMETEN**
  voor Linux; het Windows-deel is een **aanname**.
* **`yama.ptrace_scope`.** Dit is puur Linux: op Windows is er geen Yama en is deze valkuil er niet.
  Zie §8. **GEMETEN.**
* **En wat iemand op Windows anders zou doen:** daar is er geen `bwrap` en geen `ld.so`; een 32-bits
  Windows-proces kan de ELF niet laden, dus je hebt hoe dan ook een vertaallaag of een eigen lader
  nodig. De **paden** uit §2 blijven hetzelfde verhaal (het spel gebruikt nog steeds `/g3/` en
  `/T4User/`), dus die koppeling moet je daar op een andere manier regelen. **AFGELEID / aanname** —
  wij hebben geen Windows-run van deze titel gemeten.
* **Netwerkmount en tijdmetingen.** Onze bestanden komen van een CIFS-mount; de eerste lezing is traag
  en daarna uit de cache. Doe je timing over een netwerkmount, dan meet je de mount. **GEMETEN**
  (de mountopties staan in §6).

---

*Deze pagina is een samenvatting van onze eigen metingen aan deze ene versie van het spel. Verwijs bij
twijfel naar de bestanden zelf: de sha256 van het spel staat bovenaan, en paden en aantallen hierboven
zijn allemaal terug te vinden met een bestandslogger of een eigen lader.*
