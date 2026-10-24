# Aliens Extermination zonder TeknoParrot - stand 2026-10-17

`game.ini` hier staat nog op de gesloten TeknoParrot-kern (werkt, gespeeld op 2026-10-17). De vervanging is een kandidaat en
staat NIET in deze map, omdat een drop-in hier ook onder de gesloten kern zou meedraaien:

- `docs/research/losse/AliensExtermination.game.ini` (loader-repo): `[Loader] Build = ravengun, Core = ravengun`, `AimAspect = 4:3`.
- `docs/research/losse/AliensExtermination.dropin/`: `rg-aliens-hook.dll` + `dropin.ini` (Preload). Bron: `tools/rg-aliens/`.
- Analyse en tv-test: `docs/research/losse/AliensExtermination.md`.

Kort: de game krijgt zijn geweren van `winmm!joyGetPosEx(0/1)` en munt/start/test van `GetAsyncKeyState`; de HID-apparaten
(VID 04B4, PID 6870 en 7013) worden alleen beschreven, nooit gelezen. De haak-DLL zet die twee importplekken om en vult ze uit
`TeknoParrot_JvsState` (tp-shm-indeling AliensExtermination), met de eigen kalibratie van de game (ABH.ADJ).

Verhuizen na een geslaagde proef: de drie bestanden hierboven naar deze map (`game.ini` vervangen, `dropin/` aanmaken).
