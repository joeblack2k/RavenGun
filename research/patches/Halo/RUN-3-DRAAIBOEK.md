# Halo: Fireteam Raven — draaiboek voor run 3 (FileTest overslaan met START)

*RavenGun, 2026-10-13. Opzet voor de goedgekeurde volgende meting. Bench-vrij voorbereid; de run zelf
kost ongeveer 120 s. Geen commit.*

## 0. Waarom deze run, in één alinea

Run 2 gaf uitkomst (iii): de tap was aantoonbaar geladen, maar er kwam **geen enkele** sysfs-attribuut-
of descriptoropening langs -- de game is nooit bij zijn FTDI-code aangekomen. En de FileTest-regel
stond er wel: `FileTest Failure: '/pm/g7/halo/game'`, 11 s na de start. **De FTDI-attribuutregels zijn
er dus NIET terwijl de FileTest faalt**, waaruit volgt dat het FTDI-pad niet vóór die controle komt:
de FileTest is een **voorwaarde**, geen zijspoor. Daarom is de volgende meting: dezelfde tap, dezelfde
opzet, hetzelfde meetinstrument -- en alleen de FileTest overgeslagen via START.

## 1. Hoe START hier werkt (het instrument)

De invoerhulp `tools/rt-input-shim/rg_rt_input.c` is **al** de preload van Halo in het recept
(`librg_rt_input.so`). Wat hij doet, uit zijn eigen kop en uit `docs/procedures.md` en
`docs/decisions.md`:

* hij leest een schakelaarbeeld van 64 bytes (standaard `/dev/shm/TeknoParrot_JvsState`, te
  overschrijven met **`RG_RT_SHM=<pad>`**) en zet de kastschakelaars in de eigen schakelaartabel van
  de game;
* een schakelaar is een **teller van indrukken**: de game leest `[ingang+4] != 0` als "ingedrukt", dus
  de hulp zet een **korte puls** (voor START 300 ms; `RG_RT_PULSE_MS` overschrijft) op de **stijgende
  flank** (`if (now && !was)`);
* bitindeling in dat beeld, op **offset 8 als 32-bits getal**: **bit 6 = START**, bit 0 = test,
  bit 1 = service, bit 2 = munt 1;
* `RG_RT_LOG=1` laat de hulp zelf melden: `indruk bit 6 -> bord 0 schakelaar 0`.

**En dit is gemeten, niet gehoopt:** `docs/decisions.md` zegt letterlijk: *"Gemeten: een START-bit in
het gedeelde geheugen laat de game van het FileTest-scherm naar 'Webcam Init' gaan."*

Ik schrijf **niet** in `/dev/shm/TeknoParrot_JvsState` (dat is van de IO-brug/daemon). In plaats
daarvan maak ik een eigen bestand van 64 bytes, hang dat **read-only** in de sandbox, en zet
`RG_RT_SHM` daarop. De gast ziet het alleen-lezen; ik kan het op de host blijven aanpassen, en omdat
het hetzelfde bestand is ziet de hulp de verandering meteen:

```bash
mkdir -p ~/.local/share/ravengun/tap
python3 -c "open('/home/deck/.local/share/ravengun/tap/start.jvs','wb').write(bytes(64))"
# in de sandbox:  --bind-ro ~/.local/share/ravengun/tap/start.jvs /rg/jvs
# en:             --env RG_RT_SHM=/rg/jvs  --env RG_RT_LOG=1
```

## 2. Het commando

```bash
cd ~/projects/ravengun
scripts/play-native.sh --game Halo --headless --seconds 120 \
  --bind-ro ~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so /rg/lib/libusb-tap.so \
  --bind-ro ~/.local/share/ravengun/tap/start.jvs /rg/jvs \
  --bind-rw ~/.local/share/ravengun/tap /rg/tap \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
  --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
  --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1
```

**Het moment van de START-indruk.** De FileTest-melding komt ongeveer 11 s na de start (run 2:
start 10:04:20, melding 10:04:31). Daarom:

1. **t ≈ 20 s**: bit 6 zetten (`python3 -c "import struct;d=bytearray(open(p,'rb').read());struct.pack_into('<I',d,8,1<<6);open(p,'wb').write(d)"`).
   Dat is de stijgende flank waarop de hulp pulst.
2. **t ≈ 60 s**: als de game niet bewogen is (geen nieuwe regels in zijn logboek, geen USB-regels),
   de bit **terugzetten en opnieuw zetten** -- dat geeft een tweede puls. Een puls van één keer is
   immers geen bewijs dat de route niet werkt.

Venster 120 s is ruim: de FileTest is na ~11 s klaar (met START overgeslagen), en de stappen daarna
(Webcam Init, geweerkalibratie) zijn in de eerdere metingen seconden werk, geen minuten. `--seconds`
groter maken kost alleen bench en voegt hier niets toe.

## 2b. Met beeld op de tv (opdracht van de eigenaar), in plaats van `--headless`

**Wat er verandert is één vlag, en dat is met opzet zo klein mogelijk.** In `play-native.sh`:

| | met `--headless` (run 2) | met `--gamescope` (run 3) |
|---|---|---|
| de wrapper | `gamescope --backend headless -W 1920 -H 1080 -w W -h H` | `gamescope -f -W 1920 -H 1080 -w W -h H --force-grab-cursor` |
| waar het beeld heen gaat | nergens (headless backend) | **beeldvullend op de tv** (`-f`) |
| de game zelf | draait binnen gamescope, `-f1920x1080 -n` | **idem, ongewijzigd** |

De **wrapper blijft dus staan**: alleen de backend van gamescope verandert. Dat is bewust -- als ik
gamescope helemaal wegliet en de game in het sessievenster zette, dan veranderde ik **twee** dingen
tegelijk (geen wrapper én een ander beeldpad) en was run 3 niet meer te vergelijken met run 2. Nu is
het verschil precies één ding: waar gamescope zijn beeld naartoe stuurt.

**Wat er aan de omgeving gebeurt** (GEMETEN in `play-native.sh`):

* `DISPLAY` en `XAUTHORITY` worden door het script zelf opgehaald uit het commandoregel van
  `kwin_wayland` (`--xwayland-display :0`, `--xwayland-xauthority /run/user/1000/xauth_CsRMXH`,
  regels 247-258), want in een SSH-sessie staat `DISPLAY` leeg. Het `XAUTHORITY`-bestand wordt
  read-only in de sandbox gehangen (regel 319) zodat de game bij Xwayland `:0` kan.
* Op deze Deck hangt de tv aan **`card0-HDMI-A-1` en die status is `connected`** -- het beeldpad is er.
* **Ik zet er `--mute` bij.** Zonder `--headless` slaat het script de geluidsopzet niet meer over
  (regel 321: `if (( ! MUTE && ! HEADLESS ))`). Halo heeft toch geen geluid (FMOD 1.10 ontbreekt), dus
  die stap kan alleen ruis of vertraging toevoegen; met `--mute` blijft het verschil met run 2 bij de
  beeldbackend alleen. Dat is dezelfde afweging als bij de FileTest: het minst ingrijpende middel.

**Het volledige commando voor run 3 wordt dus run 2 met `--headless` vervangen door `--gamescope`,
plus `--mute`, plus de twee regels voor de START-route** (het schakelaarbeeld en `RG_RT_SHM`/`RG_RT_LOG`).

**Moet de eigenaar iets doen? NEE.** De START-indruk komt uit het schakelaarbeeld: ik zet op de host
bit 6 op **t ≈ 20 s** (en zo nodig nog eens op **t ≈ 60 s**), de invoerhulp leest dat bestand elke
250 ms en zet de schakelaar als puls in de tabel van de game. Er hoeft dus niemand op een knop te
drukken; hij kan gewoon kijken. Twee praktische dingen die ik wel vraag: **niet met muis of toetsenbord
aanraken tijdens de run** (de game leest zijn eigen invoerapparaten, en gamescope heeft met
`--force-grab-cursor` de aanwijzer vast), en niet schrikken van een zwart scherm in de eerste
seconden -- dat is de opstart tot het venster er is.

**Wat de eigenaar op de tv zou moeten zien als het lukt:** eerst het FileTest-scherm van de game, dan
-- als de START-indruk aankomt -- de stap erna ("Webcam Init"), dan het kalibratiescherm. Blijft het
FileTest-scherm staan, dan is dat geval B en weet ik dat de START-indruk niet is aangekomen (en dan
meldt de hulp dat ook in het logboek).

### Gevolgen voor de vergelijkbaarheid met run 2 (vooraf opgeschreven)

Run 2 (headless) had: **start 10:04:20, eerste nieuwe regel in het logboek van de game 10:04:31 = elf
seconden**, en dat was het FileTest-oordeel. Wat ik verwacht voor run 3 met beeld:

* **Het FileTest-tijdstip hoort vrijwel gelijk te blijven.** Die controle is een bestandscontrole in de
  opstart en gebeurt vóór zinvol beeldwerk; in run 2 was gamescope al lang op toen hij viel. Ik
  verwacht dus opnieuw een oordeel in dezelfde orde (ca. 10-15 s).
* **Wat kan schuiven:** de tijd tot gamescope klaar is (met een echte DRM-/wayland-uitgang erbij kan dat
  een seconde of wat schelen), en alles wat ná de FileTest **beeldafhankelijk** is ("Webcam Init", het
  kalibratiescherm). Voor die stappen heb ik geen run-2-vergelijking, want run 2 kwam er nooit.
* **De vergelijking die ik achteraf maak:** het aantal seconden van de start (tijdstempel in de naam van
  het launcherlogboek, `native-Halo-YYYYMMDD-HHMMSS`) tot de eerste nieuwe regel in `gamelog.txt`, en
  tot de FileTest-regel. Ik noteer beide getallen naast die van run 2.
* **En als het verschil groot is, is dat een bevinding over onze meetsetup en geen ruis:** dan betekent
  het dat de beeldbackend van gamescope de opstarttijd van de game meetbaar beïnvloedt, en dan moeten
  we bij elke vergelijking tussen runs het beeldpad meenemen. Dat zeg ik dan met de getallen erbij in
  plaats van het weg te moffelen.

**Als de tv-route niet lukt, draai ik headless door** en meld ik de fout: een meting die loopt is beter
dan een opzet die mooi hoort te zijn. Concreet herken ik dat aan: gamescope start niet op (geen
`node ID`-regel in `gamescope-Halo.log`), of het venster komt niet op de tv terwijl gamescope wel
leeft, of de game start niet binnen ~30 s. In alle drie de gevallen: stoppen, `--headless` terug, en de
fout letterlijk doorgeven.

## 3. De verwachtingen VOORAF (in dezelfde vorm als §4b van run 2)

**Geval A -- de verwachte geslaagde meting zonder bord.** De hulp meldt `indruk bit 6 -> bord 0
schakelaar 0`, het logboek van de game gaat verder dan de FileTest-regel (bijvoorbeeld `Webcam Init`),
en de tap laat **wel** attribuutregels zien:
```
attr idVendor = ...   (.../idVendor)
attr idProduct = ...   (.../idProduct)
APPARAAT ... idVendor=XXXX idProduct=YYYY
```
met **nergens** `0403:6001`, en het spel meldt zelf `found: 0` (of equivalent). **Dan is de FTDI-poort
de enige blokkade**: de keten spelproces -> libusb -> sysfs is bewezen, de game heeft de
apparaatlijst opgesomd en er zat geen bord bij. Dat is de uitkomst waar de eigenaar een beslissing op
kan nemen (bord aansluiten of niet).

**Geval B -- de hulp meldt de indruk, maar de tap blijft leeg.** Dan ligt de blokkade **na** de
FileTest en **vóór** de apparaatlijst, en is de volgende verdachte niet de tap maar wat daar tussen
zit. De gedocumenteerde volgorde is: START -> **Webcam Init** -> geweerkalibratie -> attract-lus. De
eigen notitie van het project zegt dat de game met de **gerepareerde FMOD-stub** door "Webcam Init"
naar de kalibratie loopt -- met andere woorden: de stap tussen de FileTest en het geweerkalibratie-
scherm is de webcam/FMOD-kant, en de FTDI-opsomming hoort bij de geweerkalibratie. Bij geval B is de
volgende meting dus: kijken of de game "Webcam Init" haalt (staat het in zijn logboek?) en of de
FMOD-stub meedoet. **Belangrijk:** dan is de tap niet de fout en hoeft hij niet aangepast.

**Geval C -- geen enkele `indruk`-melding van de hulp.** Dan is de START-route zelf niet aangekomen:
fout in de proef (verkeerd pad, `RG_RT_SHM` niet doorgegeven, de bit op de verkeerde plek, of de
read-only koppeling). Dat is **geen** spelresultaat en ik meld het als proeffout.

**Geval D -- er staat wel `0403:6001`.** Dan hangt er toch een bord: attribuutregels, de markering
`<<< DIT IS HET FTDI-BORD`, en dan de `ctrl`-regels die laten zien wat de game vraagt. Dat is de
meting met bord.

**En de alternatieve verklaring die ik vooraf wil vastleggen** (uit het onderzoek van een andere
partij, waar een spel 60 s op een foutvenster bleef wachten omdat de officiele start een argument
meegeeft dat TeknoParrot niet meegeeft): **als de game na START nog steeds levend niets doet**, dan is
"hij wacht op iets dat de officiele start wel meegeeft" een tweede kandidaat naast "de FileTest houdt
hem tegen". Wat ik daar nu van weet: wij starten `/pm/g7/halo/game -f1920x1080 -n` (uit onze eigen
logboeken), en mijn voorbereidende zoektocht naar een officieel startscript in de NAS-map van Halo
leverde **niets** op -- de map werd onder de naam die ik probeerde niet eens uitgelezen. Dat punt is
dus **open en ongemeten**, en ik zeg dat liever dan dat ik het invul. Bij een volgende run waarin de
game levend niets doet, is dit het eerste dat ik uitzoek: welk argument of welke omgevingsvariabele
geeft de officiele start wel en wij niet.

*(Die zoektocht is inmiddels gedaan: zie `STARTMANIER.md`. De argumenten zijn identiek -- er ontbreekt
niets -- maar er is wel een echte stap die wij overslaan: `utils/rtinit -ct`, de hardwareprobe. Zie de
derde kandidaat hieronder.)*

### De derde kandidaat: `rtinit -ct` (mijn eigen vondst, `STARTMANIER.md` §3 en §8)

De officiele start doet **eerst** een hardwareprobe en gebruikt de returncode daarvan als sleutel
(`utils/run_game.sh`, regels 20-36):

```sh
/pm/g7/halo/utils/rtinit -ct
RET="$?"
if   [ $RET -eq 9 ]; then SCREEN_RES="3880x1080"   # originele dual-screen 4p
elif [ $RET -eq 4 ]; then SCREEN_RES="1920x1080"   # mounted-gun 2p
elif [ $RET -eq 3 ]; then SCREEN_RES="1920x1080"   # super-deluxe 4p tethered
elif [ $RET -eq 8 ]; then SCREEN_RES="1920x1080"   # 55" dedicated 2p tethered
fi
cd /pm/g7/halo
LD_PRELOAD=/pm/g7/halo/halo.so ${GAMEDIR}/game2 -f${SCREEN_RES} -n
```

**Wat het betekent als `rtinit -ct` op ONZE opstelling een code geeft die niet in die vier voorkomt**
(dus niet 9, 4, 3 of 8 -- en dat is goed mogelijk, want er hangt geen kast en geen bord aan de Deck):

* dan blijft `SCREEN_RES` **leeg** en wordt de officiele startregel letterlijk `game -f -n`, met een
  **leeg resolutieargument**;
* dan werkt de officiele route op onze opstelling dus **niet zoals bedoeld**, en is "wij geven de
  resolutie expliciet" geen weglating van ons maar een **bewuste afwijking**: wij vervangen die tak
  door een vaste `-f1920x1080`;
* en dan kunnen we de resolutielogica van de officiele start **niet** als referentie voor onze
  opstelling gebruiken, want die logica is hier niet van toepassing;
* **en het is de derde kandidaat voor de blokkade** tussen de FileTest en de apparaatlijst: op de kast
  loopt die probe voor de game en zou daar iets kunnen klaarzetten (cabinettype, of iets waar de
  dongle-/cabinetkant van `rtinit.so`/`halo.so` op voortbouwt). Wij draaien hem nooit.

**Wat ik bij de uitkomst meteen benoem**, in plaats van het dan pas te bedenken:

* blijft de tap **leeg** terwijl START werkt (geval B), dan is `rtinit -ct` -- of wat daar hoort te
  gebeuren -- een logische volgende verdachte, en niet de tap;
* raakt de tap **gevuld** (geval A of D), dan is die probe voor het FTDI-pad blijkbaar niet nodig en
  gaat deze kandidaat van tafel;
* en blijft de game levend niets doen, dan staan er twee kandidaten naast elkaar: de FileTest (waarvan
  `STARTMANIER.md` §6 juist zegt dat hij waarschijnlijk een **gemelde toestand** is en geen blokkade)
  en `rtinit -ct` die wij overslaan. De goedkoopste manier om die twee te scheiden is de probe **los**
  draaien met `halo.so` voorgeladen en kijken welke code eruit komt -- geen spelstart, maar wel een
  binary uit de spelmap, dus dat hoort in een bench-afspraak.

## 4. De levenscheck (dezelfde methode als run 2, plus één signaal)

1. **Autoriteit: het eigen logboek van de game** (`native/Halo/0/upper/pmuser/gamelog.txt`), met de
   **tijdstempel van de eerste nieuwe regel** erbij. Nieuw ten opzichte van run 2: ik let expliciet op
   regels **ná** de FileTest-regel -- dat is het bewijs dat START de controle daadwerkelijk oversloeg.
2. **Tweede signaal: `pgrep -x game`** (exacte naam) plus het aantal **libfmod-mappings** in
   `/proc/<pid>/maps`. Run 2 liet daarmee de echte game zien (17 threads, 10 fmod-mappings, dezelfde
   pid van t=20 s tot t=300 s); een wrapper heeft die mappings niet.
3. **Derde signaal (nieuw): de eigen melding van de hulp**, `RG_RT_LOG=1` -> `indruk bit 6 -> bord 0
   schakelaar 0`. Zonder die regel is er niets gedrukt en is de run ongeldig (geval C).

## 5. Wat deze run expliciet NIET oplevert

* **Geen uitspraak over het FTDI-protocol**: zonder bord geen baud, geen berichten, geen CRC. Alleen
  de vraag of de apparaatlijst langskomt en of er een bord bij zit.
* **Geen uitspraak over de invoerketen**: START komt hier uit het gedeelde geheugen via de hulp, niet
  van het RIO2-bord. De game ziet nog steeds geen bord; alleen de schakelaars komen binnen.
* **Geen uitspraak over aim, trekker of geluid.** FMOD 1.10 ontbreekt nog steeds; of de FMOD-stub in
  deze run meedoet, bepaalt of de kalibratieschermen te passeren zijn, en dat is geen FTDI-vraag.
* **Niet reproduceerbaar bewezen**: de projectnotitie zegt dat START het *spel* niet reproduceerbaar
  start (een keer wel). Dat START de **FileTest** overslaat is wel gemeten, maar één run is één
  datapunt; ik meld het als zodanig.
* **Geen uitspraak over de FileTest als mechanisme** (hash, grootte of tijdstempel): die vraag is met
  run 2 afgesloten voor wat betreft "veroorzaakt een extra bestand in de map het" -- nee.

## 6. Vooraf, zonder bench

* De tap is dezelfde als in run 2: `patches/Halo/probe/libusb-tap.so`, 29.888 B, sha256
  `9b63cea4c56fe0d5`, met de zelftest op een nagebootste apparaatlijst (attribuutregels, `APPARAAT`,
  de bordmarkering) -- die zelftest draait voor elke run opnieuw.
* Het schakelaarbeeld is een eigen bestand van 64 bytes, read-only in de sandbox; ik raak
  `/dev/shm/TeknoParrot_JvsState` van de daemon niet aan.
* De game-map blijft schoon: het logboek van de tap gaat naar `~/.local/share/ravengun/tap/`, buiten
  de game-map (de fout van run 1, gerepareerd in run 2, blijft gerepareerd).
* Er draait niets anders: de bench is één game tegelijk.
