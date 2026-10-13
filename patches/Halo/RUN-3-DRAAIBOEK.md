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
