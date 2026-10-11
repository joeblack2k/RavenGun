# Go Go Strike

*RavenGun, 2026-10-11. Directe start (`scripts/launch-direct.py GoGoStrike --run`), gemeten op de
Deck. Algemene bevindingen over deze familie: `docs/elfldr2-route.md` in de loader-repo.*

## Stand

**Nog niet gestart in deze ronde, en de oorzaak is niet de game.** De starttest van 11 oktober
(`bash /home/deck/starttest-batch.sh 30 Halo GoGoStrike PacmanChomp`) geeft:

```
GoGoStrike                 start-niet 0        [launch-direct 22:46:52] klaar (exitcode 1)
```

en in `/tmp/rg-starttest-GoGoStrike.log` staat vlak daarvoor:

```
[gamescope] scriptmgr: Running script file '/home/deck/.config/gamescope/scripts/90-benq-pj-colorimetry.lua'
Failed to create backend.
[launch-direct 22:46:49] lader gestopt (exitcode 1); de game te volgen
```

`Failed to create backend.` is **gamescope**, niet de game: deze game start onder gamescope
(`gamescope: True` in het plan), en gamescope kon geen backend krijgen. De ElfLdr2-lader is in deze
start nooit aan de beurt gekomen. Dat is displaybezetting (er liepen op dat moment meerdere games en
gamescope-instanties), geen spel- of drop-inprobleem.

Er is voor deze game **geen drop-in** (`patches/GoGoStrike/dropin/` bestaat niet), dus de fout die
Halo en de HASP-games wel hebben — een inline-hook in `LD_AUDIT` — kan hier niet spelen.

## Wat de volgende meting moet laten zien

1. Een start zonder concurrerende gamescope-instantie; dan pas is te zien of de lader doorloopt.
2. Let op dezelfde twee regels die elke ElfLdr2-game geeft, en die **onschuldig** zijn:
   `Warning SHT_DYNSYM not found in section headers` en
   `Warning aborting symbol parsing due to invalid name ptr` (het `game`-bestand heeft nul
   sectiekoppen; de symbolen staan in `DT_HASH`).
3. De starttest kan deze game niet `START` noemen: de succesregels `Have fun` en `Loading game...`
   bestaan in de ElfLdr2-route niet (gemeten: 0 treffers in alle vier `BudgieLoader`-binaire
   bestanden, in `ElfLdr2/TeknoParrot.dll` en in de logboeken van alle zeven ElfLdr2-games). Zie
   `docs/elfldr2-route.md`, §4.
