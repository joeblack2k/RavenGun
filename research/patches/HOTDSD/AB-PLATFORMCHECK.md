# A/B: bevredigt de gesloten kern de platformcontrole?

**UITGEVOERD op 2026-10-13 (12:08–12:20). De uitkomst per arm staat in [`AB-UITKOMST.md`](AB-UITKOMST.md); hieronder staat de opzet en de verwachtingen zoals ze VOORAF zijn opgeschreven.**
Het script is `ab-platformcheck.sh`; zonder `--run` doet het niets en print het alleen het plan.

Dit hoort bij [`METING-1-DAEMON.md`](METING-1-DAEMON.md) §4–5 en [`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md).

## Waarom twee armen niet genoeg zijn

De oorspronkelijke opzet was twee armen: de daemon met de kern en de daemon zonder de kern. Het lezen van
de launcher-logs van 2026-10-11 liet zien dat die twee runs op **meer dan één punt** verschilden, dus
twee armen zouden die punten met elkaar verwarren. Dit is de stand van zaken:

| # | Variabele | Stand |
|---|---|---|
| V1 | **de geïnjecteerde kern** `TeknoParrot64.dll` | **nog te testen** — de hoofdverdachte |
| V2 | de prefix (`TeknoParrotUI` vs `ravengun`) | **weerlegd** — faalt identiek, en die prefix heeft ook geen SEGA-sectie |
| V3 | `OPENSSL_ia32cap=:~0x20000000` | **weerlegd** — faalt identiek |
| V4 | eerste-run-effect (warme prefix) | **weerlegd** — tweede en derde run falen identiek |
| V5 | de argumenten (`-c config.json -l <log>`) | **weerlegd** — zonder argumenten, alleen `-l`, alleen `-c`: alle drie falen identiek |
| V6 | **het modulepad / de omringende boom** — de 10-11-runs startten `…/App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe`, meting 1 startte een kopie met **alleen** de map `AMDaemon/` | **nog te testen** |
| V7 | `TP_LAUNCH_SESSION_ID` en de werkmap van de lader | ongetest; onwaarschijnlijk, maar wordt in arm A impliciet meegenomen |

Er is bovendien een **controlearm** nodig: de 10-11-runs zijn vier dagen oud en er kan sindsdien iets
veranderd zijn. Zonder die controle is een geslaagde arm niet te onderscheiden van "het werkte toen".

## De vier armen

| Arm | Wat | Variable die hij isoleert | Verwacht vooraf |
|---|---|---|---|
| **A** | lader + kern + daemon, uit een **view met dezelfde boomvorm** (`App/Hodzero/WindowsNoEditor/AMDaemon/…`) | V1 + V6 + V7 samen | **`Process state: setup Game Mode`, `Error 0`, `Process state: Advertise`** — reproduceert 2026-10-11 |
| **B** | dezelfde view, **daemon zonder kern** (preteren, zelfde werkmap) | V6 alleen (V1 uit) | **sterft na 1 s** met `amPlatformGetPlatformInformation` — zoals meting 1 |
| **C** | **alleen** de map `AMDaemon/` (de opzet van meting 1), zonder kern | de nulmeting, om A en B aan te relateren | sterft na 1 s — dit is de al gemeten uitkomst |
| **D** | **alleen als A niet reproduceert:** de exacte 10-11-opdracht, met de boom **op de NAS** | V6 volledig uit | `Advertise`; zo niet, dan is er sinds 10-11 iets veranderd en moet dat eerst weg |

**Waarom de pijpen er niet in zitten.** De 10-11-runs startten ook `pipehelper.exe` (de JVS-pijp en de
shm-spiegel). Die zijn voor de platformcontrole niet nodig — de daemon initialiseert vóór er ook maar
iets over een pijp gaat — en ze komen er later bij als de twee-exe-start aan de orde is. Dat houdt de
A/B klein.

**Waarom arm D de NAS aanraakt en A/B/C niet.** Arm D start de daemon vanaf zijn echte pad, en de daemon
kan daar zijn eigen bestanden aanmaken (`tmp.dmp`, en met `-c config.json` ook zijn AMFS-/appdata-sporen).
Dat is *het eigen gedrag van de vendeur-exe* en de projectafspraak staat toe dat het spel zijn eigen
sporen op de NAS schrijft — maar het is wel een schrijfactie, dus arm D hoort een bewuste keuze te zijn en
niet iets wat het script ongevraagd doet. **Arm D draait daarom alleen met `--arm-d` erbij.**

## Wat elke uitkomst betekent

| Uitkomst | Betekenis |
|---|---|
| A geeft `Advertise`, B sterft, C sterft | **De kern is de bevrediger.** De route "daemon zonder enige TeknoParrot-component" vraagt dan om het nabouwen van de platformcontrole: uitzoeken wat `amPlatformGetPlatformInformation()` precies opvraagt (SetupAPI/CFGMGR32-apparaatopsomming, `amSysData`, `amPlatformGetDevInfo`) en dat als haak in onze eigen kern leveren. Dat is hetzelfde proces en dezelfde haaktabel als de mountletter-omleiding |
| A geeft `Advertise`, **B geeft ook `Advertise`** | **De boomvorm was het probleem, niet de kern.** Dan is meting 1 misleidend geweest door de kale kopie, en is de daemon wél zelfstandig te draaien. Dat zou het hele plan vereenvoudigen — en dan is de eerste vraag waaróm de kale kopie faalde |
| A geeft geen `Advertise`, D wel | Er is sinds 10-11 iets veranderd (runtime-versie, prefix, Wine). Dat moet eerst worden opgelost; de kernvraag komt daarna |
| A geeft geen `Advertise`, D ook niet | De 10-11-route is niet reproduceerbaar. Dan is de eerstvolgende vraag wat er sinds die ochtend is veranderd, en is de hele kernvraag nog open |
| A sterft, B sterft, C sterft, D geeft `Advertise` | Kernsamenvatting als bij de eerste rij, maar met de bevestiging dat het pad op de NAS onderdeel is van de verklaring |

## Wat het script nodig heeft

* **benchvrijheid** — één run per arm, elk met een harde limiet van 90 s; in de praktijk sterft of slaagt
  de daemon binnen 2 s, dus dit is licht werk;
* **geen game, geen invoer, geen scherm** — de daemon is een consoleprogramma en de runs gaan met
  `env -u DISPLAY -u WAYLAND_DISPLAY`;
* de **lader en de kern** in `~/.local/share/ravengun/runtime/TeknoParrotUi-*/`; het script zoekt ze en
  print wat het gevonden heeft in plaats van een versie hard te coderen;
* ongeveer **1,3 GB** schijf voor de view en twee prefix-kopieën (op te ruimen met één `rm -rf`).

## Draaien

```sh
bash /home/deck/ab-platformcheck.sh            # print alleen het plan en de verwachtingen
bash /home/deck/ab-platformcheck.sh --run      # armen A, B en C
bash /home/deck/ab-platformcheck.sh --run --arm-d   # en arm D (raakt de NAS aan)
```

Het script print **eerst** de verwachtingen, dan per arm de uitkomst, en eindigt met een
verdict-tabel. Het ruimt alleen zijn eigen prefix-kopieën op (per proces wordt de `WINEPREFIX` uit
`/proc/<pid>/environ` gelezen) en controleert daarna of de NAS onaangeroerd is.
