# Koppeling en I/O — de A/B met `/g3` en `/T4User`, en de apparaatkant

**Game:** Terminator Salvation (`game`, sha256 `798dec07…b206a`), lader `7f83edb2…` (35.204 B).
**Vraag van deze meting:** gaan de ontbrekende bestanden weg met een **koppeling**, en verandert dat de
gepacede lus van 11 ms? Alles hieronder is **GEMETEN** tenzij met AFGELEID gemarkeerd.

---

## 1. Wat er gekoppeld is, en waar (terugdraaibaar)

Binnen één run, in een bubblewrap-sandbox. **Er is niets op de host of de NAS veranderd**; er bestaat
geen `/g3` of `/T4User` op het echte root, er blijft geen mount staan, en de schrijf-laag staat in
`/tmp` en mag weg.

| sandbox-pad | soort | bron |
|---|---|---|
| `/g3` | `--ro-bind` (lezen) | `"/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "` (let op de spatie) |
| `/T4User` | `--overlay-src` + `--overlay` | NAS-`T4User` als **onderlaag**, upper + work in `/tmp/ts-koppelmeting/{upper,work}` |
| `/home`, `/var`, `/usr`, `/etc`, … | `--ro-bind` per top-level pad | de host |
| `/tmp` | `--bind` (schrijfbaar) | de host |
| — | **geen `--unshare-*`** | zodat procesboom, netwerk en pid's identiek blijven aan de vorige meting: **één variabele** |

**Twee valkuilen die ik onderweg gemeten heb, zodat niemand ze opnieuw hoeft te vinden:**

1. **`--ro-bind / /` werkt niet.** Dan kan bwrap `/g3` en `/T4User` niet aanmaken:
   `bwrap: Can't create file /g3: Read-only file system`. De root moet je uit **expliciete** binds
   opbouwen, zoals de route in `play-native.sh` r185-190 ook doet.
2. **`/lib`, `/lib64`, `/bin` en `/sbin` zijn SYMLINKS** naar `usr/…`. Die moet je als `--symlink`
   overnemen en **niet** als `--ro-bind` van de inhoud: anders klopt de relatieve keten van de 32-bits
   interpreter niet (`/usr/lib/ld-linux.so.2 -> ../lib32/ld-linux.so.2`) en sterft de lader met
   **exit 127** en `env: '…/rg-elfloader': No such file or directory`, terwijl het bestand er wél staat.
   Dat is precies waarom de route `--symlink usr/lib /lib` gebruikt.

**De harde voorwaarde (NAS alleen-lezen) is bewezen, niet beloofd.** `--ro-bind /home` maakt de NAS in
de sandbox **onbeschrijfbaar**, en de koppelproef laat zien:
```
G3:leesbaar            (5168 B /g3/T4Data/etranslation/english.csv)
T4:leest-NAS-bron      (syslog_bb3.txt uit de NAS-onderlaag)
T4:schrijft            (naar /T4User/log)
NAS:alleen-lezen       (touch op de NAS mislukt)
```

---

## 2. De A/B: alleen de koppeling verandert

| | zonder koppeling | **met koppeling** |
|---|---|---|
| exitcode / levensduur | 0 / 5 s | 0 / 5 s |
| bestandsregels (echte gast) | 88 (van 2424; 2336 was eigen instrumentatie) | **89** |
| FOUT-regels | 29 | **17** |
| unieke ontbrekende paden | 13 | **10** |
| geslaagde ATTACH | 388 | 409 |
| kinderen | 389 | 410 |

**Weg, precies wat de koppeling moest raken (12 FOUT-regels):**
```
/T4User/log/syslog_bb3.txt              8x  -> gevonden
/g3/T4Data/etranslation/english.csv     2x  -> gevonden
/g3/T4Data/etranslation_app/english.csv 2x  -> gevonden
```
**Nieuw: geen enkele.** **Nog steeds fout (17 regels, 10 paden):**
```
syslog_bb3.txt            8x  errno=2   <- de RELATIEVE variant, in de werkmap; die koppeling raakt hem niet
nethasp.ini               1x  errno=2
/home/deck/nethasp.ini    1x  errno=2
/etc/nethasp.ini          1x  errno=2
/home/deck/.PhysX.conf    1x  errno=2
/etc/PhysX.conf           1x  errno=2
/dev/js0                  1x  errno=2
/dev/sierra2              1x  errno=2
/dev/dsp                  1x  errno=2
/dev/ttyS0                1x  errno=13 (EPERM)
```

**En de gast SCHREEF ook echt, lokaal:**
```
upper/log/syslog_bb3.txt : 12.481 B, mtime 2026-10-13 12:01:29   <- de gast schreef hier
NAS /T4User/log/syslog_bb3.txt : 12.019 B, mtime 2026-10-12 01:49 <- onaangeroerd
```
Dat is de harde voorwaarde in de praktijk: de gast **schrijft**, en het blijft in de lokale upper.

**Wat dit betekent:** de koppeling doet exact wat voorspeld was (20 van de 29 foutregels waren van deze
twee roots; 12 daarvan zijn echte regels omdat de absolute syslog 8x en de `/g3`-CSV's 2x+2x langskomen).
**De lat is niet gehaald** — nog steeds 5 s en exit 0, geen attract- of spelmodus — en dat was ook niet
de vraag.

---

## 3. De gepacede lus: **NIET veranderd** — en dat is een meting

| maat | zonder koppeling | met koppeling |
|---|---|---|
| intervallen tussen kinderen | 0,010 / **0,011** / 0,011 s, 100 % in 5-20 ms | 0,010 / **0,011** / 0,011 s, 100 % in 5-20 ms |
| mediane levensduur per kind | **0,4 ms** | **0,4 ms** |
| maximale gelijktijdigheid | **2** | **2** |
| ptrace-aanroepen per kind | min 1, mediaan 1, max 1 | min 1, mediaan 1, max 1 |
| gestopt door signaal | 0 | 0 |

De vorm is **identiek**; alleen het aantal tikken steeg van 389 naar 410 (+5 %, binnen de variatie van
eerdere runs: 388, 389, 389, 410). **Conclusie: de lus wordt NIET gedreven door de ontbrekende
`/g3`- of `/T4User`-bestanden.** Mijn eerdere AFGELEID ("de ouder had na de mislukte configuratie niets
te doen dan tikken") is daarmee **verzwakt**: de lus loopt door nadat die opzoekingen slagen. De lus
blijft een gemeten verschijnsel zonder gemeten oorzaak.

---

## 4. De apparaatkant

| pad | status | betekenis |
|---|---|---|
| `/dev/sierra2` | ontbreekt | **vermoedelijk het gunboard van deze dialect** (RawThrillsGUN). AFGELEID uit de naam en uit het feit dat de engine er actief naar opent; niet bewezen |
| `/dev/ttyS0` | **bestaat, EPERM (errno 13)** | `crw-rw---- root:uucp`; `deck` zit niet in `uucp`. Een **hostpermissie** |
| `/dev/js0` | ontbreekt | de oude joystick-interface; legacy |
| `/dev/dsp` | ontbreekt | OSS-audio; deze host heeft PipeWire/Pulse, geen OSS |

### 4a. De aanroep bewijst de verwantschap van `sierra2` en `ttyS0` — GEMETEN

De bestandslogger logt sinds meting 2 ook de vlaggen, en dat levert het scherpste bewijs dat wij over
deze twee knopen hebben:

```
open "/dev/ttyS0"   flags=0x902 (O_RDWR|O_NONBLOCK) -> FOUT errno=13 (EPERM)
open "/dev/sierra2" flags=0x902 (O_RDWR|O_NONBLOCK) -> FOUT errno=2  (ontbreekt)
```

**Identieke vlaggen: lezen én schrijven, niet-blokkerend.** Wie dit later ziet, herkent het in één
oogopslag: de engine behandelt `/dev/sierra2` precies zoals de **seriële poort**. Dat maakt de
seriële lezing van `sierra2` aanzienlijk sterker dan de naam alleen.
**De splitsing, en die houd ik aan:** de **aanroep** is GEMETEN (vlaggen, volgorde, tijdstip); dat
`sierra2` daarmee het **geweer-/bordtransport** van deze dialect is, blijft **AFGELEID** — wij hebben
niet gemeten wat de engine met de knoop doet als hij bestaat. En hetzelfde geldt voor `ttyS0`: dat het
spel die poort **wil**, is gemeten; dat het hem **nodig heeft**, is het niet — de open faalt en de gast
loopt daarna nog altijd 4 seconden door en eindigt met 0.

**Wat het aanwezig zijn van die knopen WEL en NIET doet (GEMETEN, proef A):** met synthetische knopen
voor `/dev/dsp` en `/dev/js0` (gewone bestanden in de sandbox) slagen die twee opens, en **het gedrag
van de gast verandert niet**: 89 regels, dezelfde lus, exit 0. Het spel **peilt** ze dus en gaat verder.
Het leveren van die knopen is **niet** wat de zaak opent.

**Wat ik zou willen wijzigen voor `/dev/ttyS0`, en ik doe het niet zonder beslissing:**
de rechten van een apparaatknoop verruimen is een **blijvende hostwijziging** (`usermod -aG uucp deck`,
of een udev-regel). Drie opties, in volgorde van voorkeur:
1. **Niets aan de echte `ttyS0` doen.** Voor een lichtgeweer is de kastkant een seriële poort die wij
   zelf invullen: een **synthetische knoop** (bijvoorbeeld een PTY via `socat`, als `/dev/sierra2`
   gekoppeld) die onze I/O-brug aandrijft. Dat is dezelfde gedachte als bij de andere games en het
   vraagt **geen** hostpermissie. Dit is mijn aanbeveling.
2. `deck` in de groep `uucp` zetten — klein en omkeerbaar, maar het geeft de game ook toegang tot de
   echte seriële poort, en het is een blijvende wijziging.
3. `chmod`/udev op `ttyS0` — breder dan nodig; niet doen zonder expliciete opdracht.

**`/dev/sierra2` is een I/O-spoor voor later, geen stap voor nu.** Het staat hier zodat het niet
verdwijnt: als de engine zijn gunboard via die knoop zoekt, dan is "een knoop met die naam leveren" de
eerste thing om te proberen, en dat kan **binnen de sandbox** (`--dev-bind` of een PTY) zonder de host
aan te raken.

---

## 5. Status

| stap | status |
|---|---|
| oorzaak: 20 van 29 fouten komen van twee ontbrekende absolute roots | **GEMETEN** |
| koppeling gemaakt, terugdraaibaar, NAS onaangeroerd | **GEMETEN** (§1) |
| koppeling haalt 12 foutregels weg, geen nieuwe | **GEMETEN** (§2) |
| de gast schrijft en dat blijft lokaal | **GEMETEN** (§2) |
| koppeling verandert de gepacede lus niet | **GEMETEN** (§3) |
| dat de lus een wacht/herkansingscadans is | **AFGELEID**, en nu zwakker: hij loopt door ná de geslaagde opzoekingen |
| `/dev/sierra2` is het gunboard | **AFGELEID** |
| `PhysX.conf` / `nethasp.ini` zijn de blokkade | **NIET GEMETEN**; geen bron voor de inhoud, dus niet aangeleverd |

**Volgende kandidaten, in deze orde:** (1) de relatieve `syslog_bb3.txt` in de werkmap — de goedkoopste
volgende koppeling, en de enige fout die nog 8× voorkomt; (2) de apparaatkant (`/dev/sierra2` als PTY);
(3) pas daarna de twee configuratiebestanden, en dan alleen door eerst te **meten** wat er gelezen wordt.

---

## 6. Meting 2: de relatieve syslog + een synthetische `/dev/sierra2` (GEMETEN)

`logs-koppel2-20261013-1211`, exit **0**, lader `7f83edb2…`. De koppelingen van meting 1 bleven staan;
daar kwamen twee dingen bij: een **kopie van `T4User/log/syslog_bb3.txt` uit de dump (12.019 B)** als
`syslog_bb3.txt` in de werkmap, en een **synthetische `/dev/sierra2`** (PTY via `socat`, als knoop in de
sandbox gebonden). De koppelproef was 9 van 9 groen, waaronder `SIERRA:aanwezig` én `SIERRA:openbaar`.

| | meting 1 | **meting 2** |
|---|---|---|
| bestandsregels | 89 | **51** |
| FOUT-regels | 17 | **11** |
| geslaagde ATTACH | 409 | 385 |

**Wat echt is opgelost (GEMETEN):** de **relatieve `syslog_bb3.txt`** (8 foutregels weg) en
**`/dev/sierra2`** (1 foutregel weg) staan niet meer in de foutenlijst.

### 6.1 Deze run is VERVUILD, en dat is mijn fout
Om een knoop te kunnen toevoegen moest ik `/dev` anders opbouwen (`--dev /dev` + losse binds in plaats
van `--dev-bind /dev /dev`). **Ik was `/dev/bus/usb` vergeten.** Het spel somt USB-apparaten op met
`opendir`, en nu verschijnen er **nieuwe** fouten die er in meting 1 niet waren:
```
opendir "/dev/bus/usb"   -> FOUT errno=2     <- NIEUW, door mijn apparaatverbouwing
opendir "/proc/bus/usb"  -> FOUT errno=2     <- NIEUW, de terugvaloptie
opendir ""               -> FOUT errno=2
```
Daardoor valt de daling van **89 naar 51 regels** en de ATTACH 409 → 385 **niet** toe te schrijven aan
de twee toevoegingen: het spel doet minder omdat de USB-opsomming stuk is. **De uitspraak van deze run
beperkt zich dus tot: die twee fouten zijn weg.** De rest is onbeslist.
**Fix voor meting 3:** één regel erbij, `--dev-bind-try /dev/bus/usb /dev/bus/usb` (en `/proc/bus/usb`
zoals het hoort), zodat de apparaatweergave een **superset** is van meting 1 plus `/dev/sierra2`.

**Les, en dezelfde soort als eerder vandaag:** bij een A/B waarin je de omgeving verbouwt, hoor je
**eerst de twee omgevingen te diffen** voordat je conclusies trekt. Ik had de apparaatlijst moeten
vergelijken in plaats van hem opnieuw te typen.

### 6.2 Wat de VLAGGEN en de MODE nu opleveren (GEMETEN, en dit was de winst)
Dit was de reden om de logger uit te breiden, en het antwoord is precies:
```
fopen  "/home/deck/.PhysX.conf"  mode="rb"                  -> FOUT 2   <- LEZEN
fopen  "/etc/PhysX.conf"         mode="rb"                  -> FOUT 2   <- LEZEN
__xstat "nethasp.ini" (en 2 paden)                          -> FOUT 2   <- alleen STAT, geen lezing
open   "/dev/js0"   flags=0x800(O_RDONLY|O_NONBLOCK)         -> FOUT 2
open   "/dev/ttyS0" flags=0x902(O_RDWR|O_NONBLOCK)           -> FOUT 13 (EPERM)
open   "/dev/dsp"   flags=0x1(O_WRONLY)                      -> FOUT 2
```
* **`PhysX.conf` wordt GELEZEN** (`mode="rb"`). Wie dat wil leveren moet dus **echte inhoud** hebben —
  en die hebben wij niet. Dat bevestigt het besluit om het niet aan te leveren.
* **`nethasp.ini` wordt alleen ge-`stat`**, niet gelezen. Dat is een feit voor het lokale dossier.
* **`/dev/ttyS0` wordt met `O_RDWR|O_NONBLOCK` geopend** — dat is geen peiling maar een echte
  open-poging voor lezen én schrijven. Dat ondersteunt het I/O-spoor van §4.
* **`/dev/dsp` wordt met `O_WRONLY` geopend** (audio-uitvoer) en **`/dev/js0` met `O_RDONLY|O_NONBLOCK`**
  (invoer peilen).

### 6.3 De lus is opnieuw NIET veranderd (GEMETEN)
kinderen **386** in **4,064 s** · tempo **95,0/s** · intervallen **0,010 / 0,011 / 0,011 s** (100 % in
5-20 ms) · mediane levensduur **0,4 ms** · gelijktijdigheid **2** · **1** ptrace per kind · **0** door
een signaal gestopt. Dat is dezelfde vorm als meting 1 en als de twee runs daarvoor. **De lus reageert
dus ook niet op deze twee toevoegingen.** Drie runs, drie keer dezelfde metronoom — de oorzaak zit niet
in de bestanden die wij tot nu toe hebben aangeraakt.

### 6.4 Een parserfout in mijn eigen vergelijking (en waarom dat erg was)
Mijn vergelijking haalde het pad op met een **gretige** regex (`.*"\([^"]*\)"`), die het **laatste**
quoted veld pakt. Sinds de logger `mode="rb"` schrijft, was dat dus `rb` — met als gevolg dat
`PhysX.conf` als "opgelost" verscheen en er een spookpad `rb` bij kwam. **Een valse succesmelding.** De
fix staat in het script: het pad is het **eerste** quoted veld (`s/^[^"]*"\([^"]*\)".*/\1/p`), en de
vergelijking wordt **overgeslagen** als het nieuwe logboek leeg is.

---

## 7. De route en de launcher — waar ze staan, en wat ze met de lader doen

**Zoek niet op de plek waar ik zocht.** Ik concludeerde eerst dat er geen launcher was, omdat ik keek in
`~/projects/ravengun/`, `tools/`, `ravengun-patches/` en `~`. **Hij staat in `scripts/`:**

| bestand | grootte | sha256 (kop) |
|---|---|---|
| `~/projects/ravengun/scripts/play-native.sh` | 24.594 B | `9f1fe80983e9a207…` |
| `~/projects/ravengun/scripts/play-halo.sh` | 25.045 B | — |
| `~/.local/opt/ravengun/share/ravengun/scripts/play-native.sh` | 20.274 B | de geïnstalleerde, **oudere** kop |

**Dat was mijn negende instrumentfout van de dag, en van dezelfde soort als de andere acht: een zoekactie
met de verkeerde REIKWIJDTE, niet een feit dat ontbrak.** De les staat hier zodat de volgende persoon niet
dezelfde lege zoekactie doet.

**Wat `play-native.sh` met de lader doet (GEMETEN, uit de bron):**
* hij leest het spelprofiel uit `packaging/native-games.json` (via een ingebouwde Python-uitlezer);
* hij kent een CLI-vlag `--loader` **en** een profielsleutel **`"loader": "rg-elfloader"`**; met die vlag
  start hij de game via **onze eigen lader** in plaats van de kernel-`ld.so`: hij bindt de laderbinary op
  `/rg/bin/rg-elfloader` en zet die vóór het gamecommando
  (`GAMECMD=(/rg/bin/rg-elfloader "${GAMECMD[@]}")`). De comment erboven zegt letterlijk: *"Onze eigen
  lader (rg-elfloader) voor de ElfLdr2-familie i.p.v. de kernel-ld.so."*
* **Hij kiest NIET de gesloten ElfLdr2-lader.** Die hoort bij **TeknoParrot zelf**: volgens
  `scripts/tp-add-game.py` start TeknoParrot 2.0.0.130 voor 32-bits ElfLdr2-games
  **`ElfLdr2/BudgieLoader.exe`** relatief aan de view, en daarom zet dat script een link `ElfLdr2` in de
  view. **De "gesloten route" is dus de TeknoParrot-route met BudgieLoader, niet `play-native.sh`.**

**Het Terminator-profiel bestaat — en het is GEPARKEERD (GEMETEN):**
```
"title": "Terminator Salvation", "enabled": false, "status": "geparkeerd",
"note": "Werkt al via TeknoParrot+gamescope. Native: laadt, maar stopt direct met exit 255
         (DongleInitialize() FAILED ...); niet omzeild.",
"arch": "i386", "mounts": [{"src":"@nas/Terminator Salvation ","dst":"/pm","mode":"overlay"}],
"libs": [ElfLdr2: libPhysXLoader, libPhysXCore, libNxCharacter, libNxCooking;
         lib32: libtheora, libglut, libXi, libGLU],
"exe": "/pm/game", "cwd": "/pm", "home": "/pm", "args": ["-n","-f{res}"], "res": "1360x768",
"route": "evdev"
```
Vier dingen die daaruit volgen en die ons werk raken:
1. **De bibliotheekherkomst in het profiel is anders dan in mijn run:** het profiel haalt `libtheora` en
   `libGLU` uit **`lib32`**, terwijl mijn lader ze uit **`ElfLdr2/libs`** laadde (GEMETEN in mijn uitvoer).
   Daarmee is de proef met de bibliotheekvolgorde **niet willekeurig**: hij brengt mijn opzet **naar de
   opzet die het project zelf opschrijft**, en dat is een sterker argument dan "één variabele".
2. **Het spel hoort met argumenten te starten: `-n -f1360x768`.** Mijn runs gaven **argc 1, geen
   argumenten** (GEMETEN in het laderlogboek). Dat is het grootste en nieuwste verschil met de bedoelde
   start, en het raakt "geen beeld" direct. **Eerste meting die volgt.**
3. **`"route": "evdev"`** — de invoer van de bedoelde route loopt via **evdev**, niet via een
   geweer-knoop. Dat nuanceert het `/dev/sierra2`-spoor: de **aanroep** is gemeten, maar de route van het
   project verwacht evdev.
4. **`/pm` is hier het koppelpunt van het profiel, NIET een naamseis van de lader.** Dat heb ik eerst
   verkeerd opgeschreven. De naamseis (`de map moet pm heten, anders GAME PATH CHECK FAILED`) hoort bij
   **GhostBusters**, niet bij deze titel — de laderbouwer heeft dat bevestigd. **Voor Terminator geldt dus
   geen mapnaam-eis**; het profiel koppelt de spelmap alleen op `/pm`.

### 7a. De gesloten route: wat de laderbouwer heeft bevestigd (GEMETEN, niet door mij)

* **Geen overlay op die route.** In de view zijn `game`, `T4Data` en `T4User` **symlinks naar de
  NAS-map**, dus de schrijfacties van het spel in zijn eigen `T4User` gaan **naar de NAS**. Dat botst met
  de grens die wij de hele dag hebben gehouden (en met de alleen-lezen-regel van de eigenaar), dus **die
  arm draait niet zoals hij is ingericht** en ik werk er niet omheen. De eigenaar beslist over een overlay
  voordat die route ooit start.
* **De route was speelbaar op 2026-10-09**: vizier, start, trekker en herladen. De gesloten route is dus
  voor deze titel in principe werkend geweest.
* **Kern**: de launcher geeft er **geen** mee (`core_dll: None`) en BudgieLoader krijgt alleen het
  spelpad; welke bibliotheek hij uit `ElfLdr2/` van de view laadt, is te lezen uit `/proc/<pid>/maps`
  tijdens een run.
* **Omgeving** van die route: `TP_REMOTETHREAD=1`, `tp_windowed=1`, `TP_LOGTOFILE=0`, `WINEDEBUG=-all`,
  `WINEPREFIX=…/prefixes/shared/wine`, plus `TP_GAMESCOPE=1` en `TP_GAMESCOPE_PATH=/usr/bin/gamescope`.
* **Beeld**: ja, en `rg-tv-env.sh` levert de bureaubladsessie; dat script **eindigt met 1 in plaats van
  stil headless te gaan**, en dat is precies waarom een eerdere tv-run bruikbaar was.
* Die route zet **drie drop-in-bestanden** van deze titel in de view; die horen bij wat er dan draait.
* **De note in het profiel is verouderd** ("stopt direct met exit 255"): dat is van vóór mijn werk. Met de
  ptrace-maatregel draait de gast **4 seconden**, doet zijn gepacede lus en eindigt met **exit 0**, met nog
  **8** bestandsfouten. De laderbouwer werkt die tekst bij.


**Wat nog bij de laderbouwer ligt** (niet mijn artefact om na te bouwen): of de **gesloten route**
(TeknoParrot + BudgieLoader) deze titel **hoort te starten** gezien `enabled: false`/`geparkeerd`, met welk
profiel en welke omgeving, en of een **beeldrun** daarvoor het juiste slot is.

---

## 8. De twee vervolgmetingen: argumenten en bibliotheekvolgorde (beide GEMETEN, beide NUL)

Beide zijn één variabele t.o.v. **meting 3** (`logs-koppel3-20261013-1335`: 89 regels, FOUT 8, ATTACH 392,
kinderen 393, lus 0,011 s / 0,4 ms / 2 tegelijk).

### 8.1 Arm 1 — de argumenten `-n -f1360x768` (`logs-argumenten-20261013-1347`)
**De variabele kwam aantoonbaar aan:** het laderlogboek zegt `argc 3` in de regel `spring naar e_entry`
(en `argv[0]=…/view/game`). Dat is nagelezen, niet aangenomen.

| maat | meting 3 | met argumenten |
|---|---|---|
| bestandsregels | 89 | **89** |
| FOUT-regels | 8 | **8** |
| ATTACH ok · kinderen | 392 · 393 | 390 · 391 |

Niets nieuw, niets verdwenen; de lus identiek (391 kinderen, 4,137 s, 94,5/s, 0,010/0,011/0,011 s, 0,4 ms,
gelijktijdigheid 2). **Dus: de argumenten veranderen deze fase niet.**
**Grens, en die is belangrijk:** mijn sandbox heeft **geen display**, dus `-f1360x768` (volledig scherm) kan
niet worden ingewilligd. De juiste lezing is **"geen effect zónder display"**, niet "geen effect". Het spel
haakt er ook **niet** vroegtijdig van af. Voor het beeld zelf is een **displayslot** nodig, met
`rg-tv-env.sh` (dat met 1 eindigt in plaats van stil headless te gaan).

### 8.2 Arm 2 — de bibliotheekvolgorde (`logs-bibvolgorde-20261013-1348`)
`LD_LIBRARY_PATH="$lib32:$ElfLdr2/libs"`, zodat `libGLU.so.1` en `libtheora.so.0` — die in **beide** mappen
staan — uit **lib32** komen. Dat is de configuratie die het spelprofiel van het project zelf declareert.

**Kwam de variabele aan? Ja, en het bewijs is een VINGERAFDRUK, niet een pad.** De regel
`geladen: libGLU.so.1` bevat **geen pad** (het `bibliotheken (13)`-blok met paden verschijnt alleen in
`--ontleed-alleen`, niet in een echte run). Maar het gedrag van de lader verschilt precies zoals een andere
herkomst voorspelt:
```
meting 3 (ElfLdr2 eerst):
  nog niet geladen (volgende ronde): libGLU.so.1: …/ElfLdr2/libs/libGLU.so.1: undefined symbol: floor
  nog niet geladen (volgende ronde): libtheora.so.0: …/ElfLdr2/libs/libtheora.so.0: undefined symbol: oggpackB_readinit
  geladen in een latere ronde: libGLU.so.1 (eerste poging: … ElfLdr2 … undefined symbol: floor)
nieuwe run (lib32 eerst):
  geladen: libGLU.so.1        <- in de EERSTE ronde, zonder klacht
  geladen: libtheora.so.0     <- idem
```
**Met ElfLdr2 vooraan MISLUKT de eerste laadpoging** van beide (ontbrekende symbolen) en lukt hij pas in een
latere ronde; **met lib32 vooraan laden ze meteen.** Dat is een echt verschil tussen de twee ordes, en het
betekent dat de door het project gedeclareerde configuratie **schoner laadt**.

| maat | meting 3 | lib32 eerst |
|---|---|---|
| bestandsregels | 89 | **89** |
| FOUT-regels | 8 | **8** |
| ATTACH ok · kinderen | 392 · 393 | 380 · 381 |

Niets nieuw, niets verdwenen; de lus identiek (381 kinderen, 4,029 s, 94,6/s, 0,010/0,011/0,011 s, 0,4 ms,
gelijktijdigheid 2, 1 ptrace per kind). **Dus: de bibliotheken drijven de metronoom niet.**

### 8.3 De vingerafdruk NAGETROKKEN met een GEPRINT PAD (`logs-maps-*-20261013-1351`)
De vingerafdruk van §8.2 is daarna **bevestigd** met `/proc/<pid>/maps` tijdens de run — hetzelfde middel
dat de laderbouwer voor de gesloten route noemde, en **op beide ordes toegepast** zodat de twee armen
identiek beschreven zijn:
```
orde ElfLdr2 eerst -> maps: …/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs/libGLU.so.1
                            …/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs/libtheora.so.0
orde lib32 eerst   -> maps: …/ravengun/runtime/lib32/libGLU.so.1
                            …/ravengun/runtime/lib32/libtheora.so.0
```
**Dus de variabele van arm 2 kwam definitief aan, en de vingerafdruk-lezing was juist.** Beide runs:
exit 0, 4 s, **89 regels**. (FOUT is hier **9** in plaats van 8, in **beide** ordes: deze proef laat de
synthetische `/dev/sierra2` weg — mijn bekende omissie — dus +1 fout. Omdat het in **beide** armen gebeurt,
blijft de vergelijking tussen de twee ordes schoon.)
**De conclusie van arm 2 staat daarmee op een geprint pad:** de bibliotheken drijven de metronoom niet.

### 8.4 Wat dat samen betekent voor "wiens lus is het"
Vier omgevingsveranderingen hebben de metronoom **niet** bewogen: de koppelingen (`/g3`, `/T4User`, de
relatieve syslog, `sierra2`), de apparaatknopen (`dsp`, `js0`), de **argumenten** en de
**bibliotheekvolgorde**. Daar staat tegenover dat onze **waarnemers** zijn uitgesloten (proef B: kinderen ook
zónder enige preload behalve de toestemming, max 2 tegelijk) en dat de `fork`/`ptrace`-aanroepen uit
**gastcode** komen (de lader **springt** naar `e_entry`).
**De enige overgebleven kandidaat aan onze kant is dus de OPZET van onze route** — de sandbox, de view, de
omgeving, of de lader zelf. De **gesloten route** blijft de scheidsrechter, en die draait niet zoals hij is
ingericht (geen overlay: hij schrijft via symlinks naar de NAS). Zie §7a.
**AFGELEID blijft AFGELEID:** dat de lus een wacht-/herkansingscadans van de gast is, is niet bewezen; alleen
dat hij niet van onze bestanden, onze waarnemers, onze argumenten of onze bibliotheken komt.

---

## 9. HET BEELD — de eerste keer dat deze titel tekent (GEMETEN)

Met de schermomgeving van de tv (`scripts/rg-tv-env.sh --check`: **`scherm :0 bereikbaar`**,
**`HDMI-A-1 connected primary 1920x1080+0+0`**) tekent het spel zijn **attract-/uitlegscherm**:

* **1920x1080, volledig scherm**;
* bovenin een **rode balk "GAME DESCRIPTION"** in geel;
* een **gerenderde 3D-scene**: een **zombie** in blauw overhemd met **rode strik** in een **donkere houten
  hal** met een **verlichte kroonluchter** en een tegelvloer;
* **links** een patroonband en **drie wapensymbolen** met munitie: geweer, pistool **"40"**, shotgun **"3"**;
* **linksonder** een **karakterportret** met **"P1"** en **levensvlammen**;
* **onderin**: **"PRESS START BUTTON"** met **"FREE PLAY"**.

**De demo ANIMEERT:** de tweede opname (controle, zonder argumenten) toont hetzelfde scherm verder in de
tijd — een **tekstvak** *"The creatures' attacks will deplete your life by one. Lose all of your life and
you'll be able to go no further."*, de levens teruggebracht naar **twee**, met een **gele markering** om
"P1" en de vlammen.

**De attributie: het SCHERM geeft het beeld, de ARGUMENTEN doen niets.** Met en zonder
`-n -f1360x768`: dezelfde 89 regels, dezelfde fouten, hetzelfde scherm, en `argc` 3 respectievelijk 1.
**De LAT IS NIET GEHAALD:** beide runs eindigen met **exit 0 na ~4 seconden**. Vijf minuten is het doel.

### 9.1 Wie beeindigt de vier seconden? **DE GAST zelf** (GEMETEN)
* mijn fork-waarnemer schrijft `einde exit-code=N` **alleen** als de gast zelf `exit()`/`exit_group`
  aanroept; de laatste twee regels zijn dat, voor **zowel het kind als de ouder**: `einde exit-code=0`;
* de **laatste regel die onze kant schrijft** is `spring naar e_entry 0x8057240 …` — **daarna niets**: geen
  lader-epiloog, geen opruimregel;
* de laatste ptrace-regel is een schone `PTRACE_DETACH -> 0`;
* **geen enkele timeout is afgegaan** (mijn startroute `timeout 45`, de runner `timeout 90`, levensduur 4 s).
**Dus: het is een `exit(0)` van de gast, en het is GEEN laderwerk.** Dat gaat niet naar de laderbouwer.

### 9.2 De lus en de vier seconden zijn **HETZELFDE verschijnsel** (GEMETEN)
```
eerste kind 0.025651 s · laatste kind 4.044253 s · laatste opraap 4.044661 s · EINDE 4.044665 s
-> tussen het laatste kind en het einde: 0.000 s   ·   380 kinderen x 11 ms = 4,18 s
```
De kinderen worden gemaakt **tot op de milliseconde van het einde**, zonder pauze: de lus beslaat de **hele**
levensduur en houdt **exact** op wanneer de gast zichzelf beeindigt. Daarmee heeft de lus een **betekenis**.
**AFGELEID blijft:** dat de lus het einde *veroorzaakt*, of dat het einde "de lus die opraakt" is, is met
tijdmeting niet te scheiden.

### 9.3 CORRECTIE: de "13 nieuwe fouten door het scherm" waren van MIJ, niet van het scherm
Ik rapporteerde FOUT 8 -> 21 met scherm en schreef dat toe aan het scherm. **Dat is fout.** De 21 regels zijn
de 8 syslog-, 4 `/g3`-CSV-, 3 nethasp-, 2 PhysX-, 1 `sierra2`-, 1 `js0`-, 1 `dsp`- en 1 `ttyS0`-regel —
**er zit geen enkele X-, GL- of fontfout bij**. De 13 "extra" zijn precies **de koppelingen die de eerdere
metingen hadden**: mijn **displayscript is ze kwijtgeraakt** (de `/T4User`-overlay, de
`syslog_bb3.txt`-kopie en de socat-PTY). Dus:
* **het scherm kost 0 nieuwe verschillende paden**, en het **beeld werd getekend zonder die koppelingen** —
  ze zijn voor het beeld niet nodig;
* de hypothese "een ontbrekend font of GL-entrypunt breekt de attract af" is daarmee **niet weerlegd maar
  ook niet getest**: zo'n fout bestond in deze run niet. **Eerst mijn displayscript repareren (verwachting:
  8 fouten, beeld blijft), dan pas is die vraag echt gemeten.**
* (Mijn eigen uitsplitsing bevatte twee spookpaden, `rb` en `a+b`, uit dezelfde gretige-regex-fout als
  eerder; die horen er niet in.)





---

## 10. De NAS-grens, met een meting eronder (GEMETEN)

Tot vandaag rustte de alleen-lezen-regel op onze discipline. Nu is er een meting:

```
bestanden in de spelmap gewijzigd na 13:40 : 0
bestanden in de spelmap vandaag aangeraakt : 0
mappen (., T4User, T4Data, env, hasp, irgun): allemaal 2026-10-09
T4User/log/syslog_bb3.txt                  : 12.019 B, 2026-10-12 01:49  (onveranderd)
GeneralPurposeStateMachine.txt              : 5.782 B, 2026-10-11
```

**Nul bestanden.** Dat is niet alleen een uitsluiting van "het spel heeft zijn eigen staat veranderd en
leest nu iets anders"; het is **positief bewijs dat de NAS de hele dag niet beschreven is**, terwijl er
tientallen runs zijn gedaan — allemaal met de spelmap als **read-only bron** en schrijfacties in een
**overlay-upper in /tmp**. De eigenaar noemde dit zijn belangrijkste regel; dit is het bewijs ervan.

---

## 11. DE SLUITENDE CONCLUSIE: deze gast maakt geen venster, langs GEEN enkele weg

**De gast LAADT alles, doet zijn bestandspeilingen, start zijn supervisor-lus van 10 ms en eindigt met
`exit(0)` zonder ooit zijn tekenopzet aan te raken** — dezelfde vorm als het kind dat na zijn
`PTRACE_DETACH` onvoorwaardelijk `exit(0)` doet. De vraag is daarmee niet meer "waarom komt er geen
beeld" maar **"waarom bereikt de gast zijn GLUT-opzet niet"**, en dat gaat over de vroege besturing van de
gast, niet over het scherm.

### 11.1 Wat er GEMETEN is (met het laadbewijs erbij)
```
+ 0.000001 pid=1052314 WIKKEL GELADEN (libravengun-glut-log actief in pid 1052314)
glutCreateWindow 0x · glutInit 0x · glutInitDisplayMode 0x · glutInitWindowSize 0x
glutInitWindowPosition 0x · glutGameModeString 0x · glutEnterGameMode 0x
glutMainLoop 0x · glutDisplayFunc 0x · glutSwapBuffers 0x · glutPostRedisplay 0x
exit=0  levensduur=4s  FOUT=21
```
**`glutInit` wordt niet eens aangeroepen** — de eerste regel van elk GLUT-programma, vóór alles. De
constructor die bij het laden een regel schrijft, is wat "niet geladen" van "nooit aangeroepen"
scheidt: **een instrument dat niet kan bewijzen dat het leeft, kan geen negatief melden.**

### 11.2 En de andere wegen: uitgesloten door de IMPORTLIJST te lezen, niet door te draaien
| weg | imports in `game` | verdict |
|---|---|---|
| **GLUT** | **15** (`glutInit` … `glutEnterGameMode`, `glutGameModeString`) | de enige vensterweg — en **niet aangeroepen** |
| **GLX** | 4, allemaal query/extension: `glXGetClientString`, `glXGetProcAddressARB`, `glXQueryVersion`, `glXSwapIntervalSGI` | **geen `glXCreateWindow`/`glXCreateContext`/`glXMakeCurrent` -> uitgesloten** |
| **X11** | 9, maar **geen** `XCreateWindow`, `XCreateSimpleWindow`, `XMapWindow`, `XMapRaised`, `XOpenDisplay` | **uitgesloten** |
| **SDL · EGL · GLFW · GLEW** | **0** | **niet geïmporteerd -> gratis uitgesloten** |

**Dit is sterker dan een proef zou zijn:** de importlijst is **uitputtend over de hele binary**, terwijl
een runtime-probe steekproeft. De laatste ontsnapping in mijn eigen antwoord — "hij zou via GLX/SDL/X11
kunnen" — is daarmee dicht, en dat kostte geen benchslot.

### 11.3 Vier displayhypothesen, vier metingen, alle vier uitgesloten
1. **het bord dat de focus houdt** — 14:07: bord én saver uit, spel tekende niet;
2. **"Show Desktop" dat alle vensters verbergt** — `showingDesktop=false`, en `showDesktop(false)` verandert niets;
3. **de desktoplaag over een getekend spel** — er is geen venster om over heen te liggen;
4. **onze eigen instrumentatie** — de lus bestaat ook zonder onze waarnemers (`pol2`, 1.005.599 monsters).
**De vraag is niet meer over het scherm.**
