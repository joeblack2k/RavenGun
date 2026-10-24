# Luigi's Mansion: de pakker in `VACUUM.exe` gemeten

Gemeten 2026-10-12 door RavenGun, alleen lezen op de NAS. Bron:
`Luigi's Mansion Arcade/exe/x64/VACUUM.exe` (3.436.544 B). Narekenen: `re/_vacuum-pe2.py`.

## Wat er in het bestand staat

Het is een **x64 PE32+** met `ImageBase = 0x140000000` en het entry point op RVA `0xf23125`
(VA `0x140f23125`). En dan het belangrijkste:

| sectie | VA | virtuele omvang | **bytes in het bestand** | rechten |
|---|---|---|---|---|
| `.text` | `0x00001000` | 3.496.500 | **0** | R-X |
| `.rdata` | `0x00357000` | 1.329.995 | **0** | R-- |
| `.data` | `0x0049c000` | 9.083.712 | **0** | RW- |
| `.pdata` | `0x00d46000` | 172.140 | **0** | R-- |
| `.capcom0` | `0x00d71000` | 1.719.331 | **0** | R-X |
| `.capcom1` | `0x00f15000` | 3.434.464 | **3.434.496** (offset `0x400`) | R-X |
| `.reloc` | `0x0125c000` | 24 | 512 | R-- |
| `.rsrc` | `0x0125d000` | 233 | 512 | R-- |

**Vijf secties hebben een groot virtueel bereik en nul bytes in het bestand.** De enige sectie met echte
data is **`.capcom1`**, met een **entropie van 7,9553** -- dat is de ontpakker. En het **entry point valt
binnen `.capcom1`**: het programma begint dus in de stub, niet in het spel.

De datadir bevestigt het: `import` (RVA `0x10980d8`), `export` (`0x121cc00`), `exception`
(`0x122eb70`), `iat` (`0x120d000`) en `loadconfig` (`0x122eb00`) wijzen allemaal naar bereiken waarvan de
bytes **niet in het bestand** staan. Er is dus ook geen bruikbare import-tabel om statisch uit te lezen.

## Wat dat verklaart

Wie op het entry point pauzeert, injecteert of schrijft, **breekt het ontpakken** -- en krijgt dan
"illegal instruction"-achtige symptomen zonder dat er iets mis is met het spel. Dat is precies wat de
eerdere notitie beschreef.

## Een correctie op de eerdere notitie

In `packages/games-status.md` staat bij Pigs Might Fly: *"injectie vanaf het startpunt geeft 'Unhandled
illegal instruction at address 0044A5F4' (zoals Luigi's Mansion)"*.

**Dat adres bestaat niet in dit image.** Met `ImageBase = 0x140000000` is `0x0044A5F4` een 32-bits
adres, en op de bijbehorende plek in het bestand staat hoogentropische data (`ab ea 5f 3b df 56 31 53
…`). Het adres komt vrijwel zeker uit **Pigs Might Fly's 32-bits `Shell.exe`**, en de notitie heeft het
naar Luigi's Mansion gegeneraliseerd -- "zoals Luigi's Mansion" ging over de **soort** fout, niet over
hetzelfde adres. Wie dit leest: zoek in `VACUUM.exe` niet naar `0x0044A5F4`.

## De weg vooruit

1. **Het beeld dumpen uit het geheugen**, precies zoals bij BudgieLoader is gedaan: het spel met de
   werkende route starten en dan `/proc/<pid>/mem` uitlezen terwijl het draait. Dat geeft de ontpakte
   `.text`, de echte import-tabel, de echte relocaties en het echte **OEP** (original entry point).
2. Twee valkuilen uit die eerdere meting: `systemd-logind` doodt de procesgroep als de ssh-sessie sluit
   (draai als systemd-user-unit), en `ptrace_scope=1` is geen probleem zolang het dumpende script de
   **ouder** is van het spel.
3. Daarna is het gewone laderwerk: het OEP als startpunt, de echte imports oplossen, en de I/O via het
   gedeelde geheugen `TeknoParrot_JvsState` (`inputProtocol shared-luigi-mansion`, bits op offset 8, assen
   op 12..15).

Het OEP is ook zonder dump te vinden (hardware-breakpoint op de eerste schrijfactie in `.text`, of de
laatste sprong van de stub volgen), maar de dump geeft méér: de imports en relocaties die het spel
werkelijk gebruikt.


## De twee helften: invoer is al van ons, de lader nog niet

Bij het natrekken van de I/O-kant bleek iets belangrijks: **de invoerhelft bestaat al in dit project.**

* `packaging/tp-shm-profiles.json` heeft een volledig profiel **`LuigisMansion`** met het protocol
  `shared-luigi-mansion`: stuurwoord van 32 bits op offset 8 (start bit 0/3, vacuümpknop = Strobulb op
  bit 1/4, vacuümhendel op bit 2/5, test 8, select switch P1 9, service 10, screen-out 16/17) en vier
  asbytes op 12..15 (P1Y, P1X, P2Y, P2X), kalibratie X 3..252 en Y 6..250, links/boven = 0.
* `scripts/tp-shm-swap.sh` laat onze daemon de **enige schrijver** van dat invoerblok zijn: TeknoParrotUi
  start nog wel de lader, maar zijn eigen invoer bereikt de game niet meer.

Dus de stand is: **onze invoer, hun lader.** Wat ontbreekt is precies de laderhelft, en dat is de gepakte
`VACUUM.exe`.

## En wat het eigen onderzoek over de moeilijkheid zegt

In `docs/research/zonder-teknoparrot.md` staat Luigi's Mansion beoordeeld als **"moeite: zeer hoog"**,
met het advies **"laatste in de rij, bij voorkeur TP onder Wine blijven proberen"**. De reden die daar
staat: een eigen **Sega Nu-platformlaag** met `oxGetHwInfo`, het I/O-bord `LMAIO.bin` via COM1, de drives
`X:` en `Y:`, keychip en netwerk. Ook: geen enkel open-sourceproject dekt deze game (Segatools noemt hem
niet).

**Dat is geen reden om het niet te doen, maar wel om te weten waar we aan beginnen.** Het is het meest
waarschijnlijke van de zes om veel tijd te kosten, en het is het enige van de zes dat al speelt.

De I/O-kant is bovendien niet het probleem dat overblijft: `game.bat` flasht firmware naar het bord via
COM1 (`sendFlash COM1 LMAIO.bin`), en uit de bevindingen blijkt die flash **niet nodig** te zijn -- onder
TeknoParrot speelt de game met alleen het gedeelde geheugen. De open vraag uit `teknoparrot-io.md` ("wat
doet de game op COM1 als er geen bord is") is dus de vraag die onze lader moet beantwoorden: het spel
moet niet stuklopen op een afwezige poort.


## De tweede laag: de anti-tamper-controle (en de grens die de eigenaar stelt)

De pakker is niet het hele probleem. In de patchkaart (`game.ini`), in
`docs/research/games-werkvolgorde.md` (regel 29) en in `third_party/openparrot/README.md` (regel 55) staat
de uitkomst van een eerdere poging:

* Op de **gesloten kern** werkt het spel: entry-point-route met `TP_REMOTETHREAD=off`, symlink-view,
  werkmap `exe\x64`, `Data -> DataGlobal/EN/Data`, `Movie/` als symlink naar de NAS, en de
  GStreamer-plug-ins `asf` + `libav` voor de `.wmv`-films. Titel en attract met beeld.
* Op **onze eigen (OpenParrot-afgeleide) kern** valt het om met **lader-exitcode 32**, en de conclusie
  van toen was: **"exit 32 = Capcom anti-tamper (0005 lost dat niet op)"**. OpenParrot-patch **0005** was
  speciaal voor dit spel geschreven (om COM1 als *aanwezig* te laten melden in plaats van als
  ontbrekend) en loste het niet op.
* Het opgeschreven eindoordeel: *"Geparkeerd met die reden; een eigen per-game hook is hier nodig, geen
  generieke infrastructuur."*

**Dus: laag 1 is de pakker, laag 2 is de bescherming.** De ontpakte code is nodig om te kunnen zien wat
er gebeurt, maar de vraag "waarom stopt hij op onze kern en niet op de hunne" is een vraag over de
**bescherming**, niet over het ontpakken.

### De grens

De eigenaar heeft dit werk **niet verboden** -- hij heeft het **verdeeld**. De afspraak is:

* **Claude** (de bouwer in de lader-repo) houdt zich niet bezig met dongle-, HASP-, keychip-,
  integriteits- of checksumwerk. Hij levert het aan als een gedocumenteerde overdracht.
* **Subagents** doen dat werk. Dat is bewust zo geregeld: het is delicaat werk en het hoort in een eigen
  baan met een eigen agent, niet in de handen van de bouwer van de rest.
* Voor Luigi betekent dat: het anti-tamper-mechanisme achter **exitcode 32** is een taak voor een
  subagent, en het is dus **geen dood spoor** voor het project.

**Correctie op een eerdere versie van dit document.** Hier stond dat dit werk "niet mag" en dat we bij
"alleen te verslaan" moesten stoppen. Dat was mijn eigen verkeerde doorvertaling van de instructie: ik
maakte van "wie het doet" een "of het gebeurt". Rechtgezet op 2026-10-12 nadat de eigenaar het expliciet
zei.

### En de omgeving is groen

Een pre-flight op 2026-10-12 laat zien dat een mislukte start **niet** aan de omgeving zal liggen: de
view is compleet (`Data -> DataGlobal/EN/Data` aanwezig, alle spelbestanden gelinkt, `VACUUM.exe`
3.436.544 B) en de GStreamer-plug-ins staan op hun plek (`tools/gst-extra/plugins/`: exact twee
bestanden, `libgstasf.so` en `libgstlibav.so`, gehaald met `scripts/fetch-gst-wmv.sh`). Een verschil in
gedrag tussen de twee kernen is dus een verschil **in de kern**.


## En een vastgelopen rest op de bench (2026-10-12)

Op de Deck liep een `VACUUM.exe` (PID 363640) dat er als een geslaagde run uitzag maar dat geen was:

| meting | uitkomst |
|---|---|
| `State` / `PPid` | `R` / **1** (de lader is weg, het kind is aan init gegeven) |
| threads | **1** (een spel dat de titel haalt heeft er vele: d3d, winegstreamer, audio) |
| CPU | **2,00 s in 2,0 s** -- vlak 100% van een kern |
| `.text` op `0x140001000` | **alles nul** |
| maps | `140000000-140001000 rwxp` (VACUUM.exe), `140001000-14125e000 rwxp [anoniem]` |

**Conclusie: nooit ontpakt.** Dat verklaart mijn dump van 18,3 MB volledig: `.text`, `.rdata`, `.data`,
`.pdata` en `.capcom0` honderd procent nul en alleen `.capcom1` gevuld is precies het beeld van een proces
dat nooit voorbij de Capcom-stub is gekomen. Mijn eerdere "óf nooit klaar met ontpakken, óf de pakker legt
het elders neer" valt dus terug tot de **eerste helft**. Het proces is opgeruimd (`kill -9`): het spinde
uren op een kern en zou elke meting op de bench vergiftigen.

**En een nuance op de verklaring die hiervoor werd geopperd.** De suggestie was dat onze eigen lader
`EB FE` (`jmp $`) op het entry point achterlaat (`RunTo()` in `main.cpp`), en dat de spelthread daar voor
eeuwig op blijft draaien. Op `0x140f23125` staat echter:

```
48 b8 80 e9 d4 f6 ff 6f      ->  movabs rax, 0x6ffff6d4e980
```

een gewone instructie, **geen `EB FE`**. En `kstkeip` leest 0, dus *waar* hij spinde is van buiten niet te
meten. **"Nooit ontpakt" is gemeten; "het was de `EB FE`-spinlus" is onbewezen** en hoort als hypothese in
het dossier te staan, niet als oorzaak.

**Les voor elke volgende dump:** eerst de levenscheck -- `PPid` niet 1, threads ruim boven 5, CPU laag of
onregelmatig in plaats van vlak 100%, en `.text` op `0x140001000` niet nul. En pas dán `sudo` op
`/proc/<pid>/mem`.
