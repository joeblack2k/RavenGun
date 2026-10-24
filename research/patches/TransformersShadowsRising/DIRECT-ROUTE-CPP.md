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

## Tweede proef 2026-10-14 18:05: beide borden (`Boards = SaeGunBoard, SaeIoBoard`)

- De daemon maakt nu beide borden ("SaeGunBoard via uinput, SaeIoBoard via uhid"). Let op: `Board = SaeGunBoard` moet blijven staan;
  met alleen `Boards` keurt de startcontrole de hid-board-route af en valt de start terug op het script en TeknoParrotUi (gezien, meteen gestopt).
- START: de wereldkeuze loopt nu door en het spel begint ("PULL THE TRIGGER!", score 500, Shell-log: health 35 -> 15). Vooruitgang t.o.v. 17:10.
- Daarna bevriest het beeld opnieuw (opnames 18:10:50 en 18:13:46 identiek, `tf1314.png`), ~1 minuut na START, net als bij de eerste proef.
  Het hoofdproces van Transformers2.exe draait door (~55 % CPU, niet geblokkeerd).
- Shell-logboek `GameStateMachine - 261014.log` (op de NAS, alleen gelezen; de Shell-klok loopt 2 uur achter): de Shell stuurt elke 10 s
  `SHELL_MSG_CHALLENGE`. In beide runs is de laatste regel een challenge kort voor het bevriezen (15:14:50 -> bevroren ~17:15:40 lokaal;
  16:09:30 -> bevroren vóór 18:10:50 lokaal); daarna logt het spel niets meer. Aanwijzing, niet bewezen: het antwoord op die challenge
  (Shell <-> spel) loopt mis. Volgende stap: in Shell.exe/Transformers2.exe nagaan waar het antwoord vandaan moet komen.
