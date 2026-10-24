# Testcases voor onze eigen ELF-lader

Onze eigen lader (`tools/rg-elfloader/`, 32-bits i386) had als enige bewezen testcase **GoGoStrike**.
Dat spel is op verzoek van de eigenaar van de NAS verwijderd, dus de vraag is: bouwt de lader nog, en
waar kunnen we hem nu op testen? Gemeten 2026-10-12.

## De lader bouwt nog

`bash scripts/build-elfloader.sh` → **exit 0**, "bouwen: i386 -> rg-elfloader", resultaat een
`ELF 32-bit LSB pie executable, Intel i386`, en hij wordt opnieuw naar
`~/.local/share/ravengun/bin/rg-elfloader` geplaatst. Er is dus niets stukgelopen door het verwijderen
van GoGoStrike; alleen de testcase is weg.

## Wat er nog is: de ElfLdr2-games met een `game`-bestand

| game | klasse | sectiekoppen | dynsym | opmerking |
|---|---|---|---|---|
| Big Buck Hunter Pro | ELF32 | `SHNUM=0` | 188 | **ongepakt** (SHNUM=0 zegt hier niets -- bekend) |
| Big Buck Hunter Pro Home | ELF32 | `SHNUM=0` | 197 | idem |
| Target Terror Gold | ELF32 | `SHNUM=36` | 209 | ongepakt |
| Terminator Salvation | ELF32 | `SHNUM=29` | **1020** | **waarschijnlijk ongepakt**; grote symbooltabel |
| GhostBusters | ? | `SHNUM=30` | - | sectiekoppen aanwezig |
| Jurassic Park | ? | `SHNUM=0` | - | gepakte familie |
| Walking Dead | ? | `SHNUM=0` | - | gepakte familie |

**Dus:** de natuurlijke **regressietestcases** voor onze lader zijn nu
**Big Buck Hunter Pro / Pro Home** (ongepakt, nog aanwezig, `game` van ~51 MB) en **Target Terror Gold**.
En **Terminator Salvation** ziet er ongepakt uit, wat het de echte kandidaat maakt om onze lader op in te
zetten -- precies wat de zes-game-opdracht vraagt.

## Een valkuil in mijn eigen heuristiek

Ik had "een map met een `game`-bestand = een ElfLdr2-game" gebruikt. Dat klopt niet:

* **Twee valse positieven:** `Let's Go Jungle Lost on the Island of Spice` en `Let's Go Jungle Special`
  hebben een `game` van **613 en 680 bytes** -- dat zijn **shell-scripts**, niet de spellen:
  `#!/bin/bash`, met `export LD_LIBRARY_PATH=/home/disk0/:/usr/local/ssound/final1.5/lib`, `TEA_DIR=/home/disk0`,
  `GLIBCPP_FORCE_NEW=1`, een verwijzing naar `$title.def` en "boot", en een `XF86Config`-tak. De
  commentaarregels zijn Japans. De echte spellen staan elders in de map, met `data/bank_*.xaf` als data.
* **Een valse negatief:** Halo heeft zijn `game` niet in de wortel maar op `g7/halo/game`, dus mijn
  `find -maxdepth 1`-telling miste hem.

**De juiste bron is het `ExecutableName` uit het TeknoParrot-profiel**, niet de bestandsnaam `game`.

**En een aardigheid die eruit voortkomt:** die twee Let's Go Jungle-scripts zijn de **originele
opstartrecepten van de kast** -- welke bibliotheekpaden, welke omgevingsvariabelen, welke volgorde. Voor
een eigen lader op die titels is dat precies de informatie die je anders moet raden.
