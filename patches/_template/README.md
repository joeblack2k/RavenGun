# _template — kopieer deze map om een game toe te voegen

Kopieer deze map naar `patches/<JouwGame>/` en werk de bestanden bij. Zie
`docs/een-game-toevoegen.md` (loader-repo) voor de stappen en `docs/dropins.md` voor het contract.

## Wat je mag weglaten

- `dropin/` — als de game geen bestanden van ons nodig heeft.
- `patches.ini` — als de game geen bytepatches nodig heeft.
- `dropin/ravengun-hasp/` — als de game **geen** dongle vraagt. Aanwezigheid van `dongle.json`
  betekent "deze game vraagt een dongle".

Wat je nooit mag weglaten: `game.ini` en deze `README.md`.

## De dongle: kies het soort en kopieer een bestaand voorbeeld

`dongle.json` staat in `dropin/ravengun-hasp/<JouwGame>/`, met de geheugenbestanden in `memory/`
ernaast. Het veld `kind` bepaalt wat de rest betekent. Er is geen `[Dongle]`-sectie in `game.ini`:
een dongle is een gewone drop-in.

| `kind` | Voorbeeld in deze repo | Wat er verder in staat |
|---|---|---|
| `hasp` | `patches/Terminator/` | `memory` met per fileid een bestand; `haspid`, `features`, `keycaps` als je ze gemeten hebt |
| `matrix` | `patches/RabbidsHollywood/` | de waarden die `matrix32.dll`/`matrix64.dll` moeten antwoorden |
| `region` | `patches/LGI/` | `region` en `pcb` met de waarde uit het TeknoParrot-profiel, plus `enforced` en `enforced_basis` |
| `sega-scan` | (nog geen voorbeeld) | de DK2-sleutel die de game zoekt |
| `amdaemon` | (nog geen voorbeeld) | de antwoorden die de daemon zelf nodig heeft |

**Eerlijkheid boven volledigheid.** Is een waarde niet gemeten, schrijf dat dan op in `notes` of in
`enforced_basis` in plaats van iets te verzinnen. Een beschrijving met "aanname, niet gemeten" is
meer waard dan een getal dat klopt noch klopt.

## Schrijf op wat je gemeten hebt

Schrijf in de README **wat je hebt gemeten en waarom** — dat is voor de volgende persoon meer waard
dan de instellingen zelf. Er zijn zeven kaarten die de stand bijhouden; die worden gegenereerd, dus
je hoeft ze niet met de hand bij te werken:

| Kaart | Wat je erin terugvindt |
|---|---|
| `patches/STAND.md` | volledigheid van de map (`game.ini`, `[Status]`, drop-in, dongle) |
| `patches/STARTTEST.md` | of de game met de eigen lader start en blijft draaien |
| `patches/ZONDER-LADER.md` | of hij ook met alleen `wine <exe>` start |
| `patches/PATCHES-PER-GAME.md` | welke patch nodig is en wat een start zonder TeknoParrot blokkeert |
| `patches/REGIO-PER-GAME.md` | of hij een regio/printplaat-dongle vraagt |
| `patches/EIGEN-KERN.md` | of onze eigen kern hem herkent, en welk profiel er nog moet komen |
| `patches/ELF-NATIVE.md` | of de ELF van een ElfLdr2-game ook los te starten is |

De generators staan in dezelfde map (`stand-gen.py`, `starttest-gen.py`, `regio-gen.py`,
`kern-herkenning.py`, `proef-zonder-lader.py`, `elf-native.py`). `readme-gen.py` vult een **lege**
README uit die kaarten; heeft je README al inhoud, dan laat dat script hem met rust.
