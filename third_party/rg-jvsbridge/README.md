# rg-jvsbridge — RavenGuns eigen JVS-brug (vervangt TeknoParrots pipehelper.exe)

`pipehelper.exe` was de laatste TeknoParrot-binary die we bij elke game in de Wine-prefix draaiden: hij
maakt de Windows named pipe `\\.\pipe\TeknoParrot_JVS` en de named shared memory `TeknoParrot_JvsState`,
spiegelt die naar/van `/dev/shm/...`, en brugt de pipe over TCP naar de Linux-kant (de daemon /
`JvsPipeServer` in `scripts/launch-direct.py`). `rg-jvsbridge.exe` doet precies dat, maar is van ons.

## Clean-room

`rg_jvsbridge.c` is geschreven **vanuit de gedragsspec** — het waarneembare gedrag en de opdrachtregel van
pipehelper, gemeten via `strings`/`objdump`/disassemblage en vastgelegd in
`docs/research/eigen-runtime-inventaris.md` — en **niet** vanuit TeknoParrots `pipehelper.c` (GPL-3). Alleen
het protocol/de interface is overgenomen, geen broncode-expressie. Licentie: GPL-3.0-or-later.

## Twee modi (zoals launch-direct.py ze aanroept)

- **pipe:** `rg-jvsbridge <pipenaam> <host> <poort> [<shmnaam> <shmgrootte> <shmbronpad>]` — maakt de named
  pipe, wacht tot de game verbindt, verbindt dán als TCP-client naar `host:poort`, en brugt kale bytes in
  beide richtingen (pipe→host via `send`, host→pipe in stukken ≤0x100 via `WriteFile`). Vóór elke host→pipe-chunk
  wordt byte 0 (de verbind-vlag) van de bron naar de game-mapping gespiegeld. Bij een gesloten pipe: cyclus
  herstarten (nieuwe pipe-instantie + nieuwe TCP). Met de extra shm-argumenten draait de shm-spiegel als thread.
- **shm:** `rg-jvsbridge shm <shmnaam> <shmgrootte> <shmbronpad>` — alleen de shm-spiegel (geen pipe, geen TCP),
  voor de profielen die geen JVS-pipe gebruiken.

De shm-spiegel is een drie-weg per-byte-vergelijking (bron wint) met `Sleep(1)`. De stderr-regels houden het
voorvoegsel `pipehelper:` en de woorden "created" / "shm mirror active", zodat `launch-direct.py` (dat daarop
wacht) niet hoeft te wijzigen.

## Bouwen

`scripts/build-jvsbridge.sh` — `x86_64-w64-mingw32-gcc` in de gepinde podman-container van de OpenParrot-build.
Uitvoer: `~/.local/share/ravengun/jvsbridge/rg-jvsbridge.exe` + `BUILDINFO.txt`. 64-bit; werkt ook voor 32-bit
games (named pipes/mappings zijn bitness-onafhankelijk).

## Status

- **shm-pad bewezen:** Gashaaaan (tp-shm) leeft op onze kern met deze brug; de brug maakt de pipe en de
  shm-spiegel ("created" / "shm mirror active").
- **pipe-pad:** het protocol volgt de spec, maar het volledige bruggen (game verbindt → TCP → JVS-frames) is
  nog niet headless bevestigd — de enige pipe-route-games (LGI/LGS/LGI3D) zijn kieskeurig en vragen de daemon
  met het JVS-bord + de tv met lightgun. Dat is de laatste verificatiestap (eigenaar).

`launch-direct.py` kiest `rg-jvsbridge.exe` als die bestaat, en valt anders terug op `pipehelper.exe`.
