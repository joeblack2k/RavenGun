# Transformers: Shadows Rising op de directe route in C++ (rg-direct): start ja, bevriest in de wereldkeuze

Datum: 2026-10-14, dubbelklik in RavenGun om 17:10:17 (logboek `direct-TransformersShadowsRising-20261014-171017.log`).
Invoer: route hid-board, `Board = SaeGunBoard` (uinput). Bench: muismodus + `ravengunctl key`.

| Tijd | Gezien |
|---|---|
| 17:11 | "SYSTEM INITIALIZING / LOADING" |
| 17:12:45 | titel, "PRESS START BUTTON", FREE PLAY (laden ~2,5 min) |
| 17:15 | START (toets 1, 0,3 s): STAGE SELECT met aftelling, het rode vizier volgt de muis |
| 17:15:40 - 17:17 | het beeld staat stil: aftelling op 03, vizier op dezelfde plek, terwijl `Transformers2.exe` ~56 % CPU gebruikt |

Gestopt om 17:18. Niet `working`.

Mogelijk verband (niet gemeten): VERIFICATIERUN-UITKOMST.md meldt dat de shell het IO-bord (`SaeIoBoard`, uhid) niet vindt
("Couldn't find IOBoard device"); deze game.ini noemt alleen `Board = SaeGunBoard`. Volgende proef: `Boards = SaeGunBoard,
SaeIoBoard` en kijken of de wereldkeuze dan doorloopt.
