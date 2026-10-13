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

