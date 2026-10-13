# RUN 13 — START-ALLEEN: de attract-lus komt ZONDER enige TEST-puls

*RavenGun, 2026-10-13. Uitgevoerd **11:55:51 - 11:58:2x** (150 s, beeld op de tv, exitcode 0). Geen
crash (`FATAL-regels: 0`). Niets draait. Geen commit.*

## 1. DE BANKENTIJDLIJN (het scherpste instrument)

```
11:56:03  START-puls 1            -> FileTest Reported 1 errd files at startup!   (FileTest weg)
11:56:21  Failed to load FMOD sound bank: perm
11:56:24  Webcam: Could not ID device for player 0 / Aborting auto-id
11:56:26  Network initialization completed
11:56:32  Failed to load FMOD sound bank: ATTRACT      <-- DE ATTRACT-LUS, ZONDER ENIGE TEST-PULS
11:56:55  Failed to load FMOD sound bank: level1
11:57:12  WARNING: Detected more coin pulses ...       <-- de munt komt aan
11:57:17  WARNING: Detected more coin pulses ...
11:57:21  WARNING: Detected more coin pulses ...
11:58:04  Failed to load FMOD sound bank: level2
```

**En de hulp bevestigt dat er geen TEST was:** geteld in haar eigen meldingen --
**TEST (bit 0): 0**, START (bit 6): 6, munt (bit 2): 4. De `attract`-bank kwam er dus **zonder dat bit 0
ooit is gezet**.

## 2. WAT ER OP HET SCHERM STOND (foto per stap)

| foto | tijd | omvang | wat het is |
|---|---|---|---|
| `run13-01-na-filetstest.png` | 11:56 | 11,8 kB | zwart: de opstart, vlak na de FileTest |
| `run13-02-na-start1.png` | 11:56 | **1.466 kB** | het **kalibratiescherm** |
| `run13-03-na-start2.png` | 11:56 | **2.790 kB** | **ander scherm** (de kalibratie weg) |
| `run13-04`, `05-na-start3/4` | 11:56 | 1.376 / 1.270 kB | andere schermen |
| `run13-06-na-start5.png` | 11:57 | 3.238 kB | ander scherm |
| `run13-08-bij-attract.png` | 11:57 | 2.898 kB | de attract-lus |
| `run13-09-na-munt.png` | 11:57 | 2.374 kB | de attract-lus, met de munt erbij |
| `run13-10-eind.png` | 11:57 | 2.808 kB | de attract-lus |

## 3. DE CONCLUSIE: DE TEST-REGEL KAN WEG

Onze documentatie zegt (in `docs/decisions.md` regel 442 en `packaging/games-status.md` regel 65) dat de
**testknop** de kalibratie van gun 1 overslaat en START die van gun 2. **Op deze opstelling is dat niet
zo:** de TEST-pulsen deden in run 11 niets (logboek bleef exact op 256 regels, zeven identieke foto's) en
in run 13 is bit 0 **nooit gezet** -- en toch kwam de attract-lus er.

**Wat er werkelijk gebeurt (GEMETEN, drie runs):**
1. **START** (puls ~400 ms, herhalen) -> FileTest weg (één puls was genoeg in run 11, 12 en 13);
2. **START** blijven geven -> de kalibratie verdwijnt en de game loopt door;
3. **START** nog een keer of wat -> de **attract-lus** (bank `attract`, dan level1..level5);
4. **munt** (bit 2) -> geld erin (`Money In`), met de waarschuwing dat een lange indruk per beeld
   meerdere keren telt (14 ms is de juiste lengte voor $0,25).

**Aanbeveling voor de documentatie (die van jullie is, niet van mij):** haal de regel "TEST slaat de
kalibratie van gun 1 over" uit `docs/decisions.md` en `packaging/games-status.md`, of zet erbij dat die
op deze opstelling niet geldt -- **START alleen is voldoende** gebleken, in drie runs op een rij.

## 4. DE IDENTITEITSREGEL (nu automatisch, en deze run is de eerste die hem gebruikt)

Bovenaan het logboek van deze run:

```
== play-native Halo 20261013-115551
script: /home/deck/projects/ravengun/scripts/play-native.sh
  de versie uit de REPO (commit 5cdfaa8; scripts/ gelijk aan die commit); sha256 c19370b6d9d2ef09e759ca1338ae071d302a474991c9b034cb488ae553f7ebf9
  gegevens: /home/deck/projects/ravengun/packaging/native-games.json sha256 6ea6aa97f23801d8ecedd646162751754bdc212b7f36cfd92941e3a07f979ba9
  hulp: /home/deck/projects/ravengun/scripts/rg-paths.sh sha256 8ca01e47a2465b82f150c2570ed5523ccffaef2b16c6e1cf81f00b26f63db25a
  hulp: /home/deck/projects/ravengun/scripts/rg-identiteit.sh sha256 be4c411ffdd1d272d3088751c0d8d10c6f8efd7003a4caa5504492e05e5a6463
  bibliotheek: /home/deck/projects/ravengun/build/librg_rt_input.so sha256 53bca814770d681d614a3dabee962a9c2a0e3bb3fc6cf990e5b8f97ad78126dc
  meegegeven: /home/deck/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so sha256 9b63cea4c56fe0d55f6f4cad9fbf4991abc3c465f02448e3aafe34521828d198
  meegegeven: /home/deck/.local/share/ravengun/tap/start.jvs sha256 f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b
  het geinstalleerde script (/home/deck/.local/opt/ravengun/share/ravengun/scripts/play-native.sh) is hetzelfde
```

Dus: pad, **repo-commit `5cdfaa8`**, sha256 `c19370b6d9d2ef09…`, de gegevens, beide hulpen, de
bibliotheek, alles wat wij meegeven, **en** de bevestiging dat het geinstalleerde script hetzelfde is.
Dit werkte zonder dat er iets voor gevraagd hoefde te worden -- precies waar dat gereedschap voor is.

## 5. WAT DEZE RUN **NIET** OPLEVERT

* Geen aim en geen trekker (die velden schrijft de hulp niet; zie `HULP-TREKKER-EN-ASSEN.md`).
* Geen spelmodus: dit is de **attract-lus**, niet het spel zelf.
* Geen geluid (FMOD 1.10 ontbreekt; de banken falen met code 52 van onze eigen stub).
* Niets over het FTDI-/geweerbord (de tap zag geen opsomming).
* En geen uitspraak over waarom de oudere notitie het anders zag: wat hier vaststaat is dat **START
  alleen** volstaat op deze opstelling.
