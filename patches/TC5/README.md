# TC5 (Time Crisis 5)

**Status: playable** — de game start en blijft draaien via de eigen lader (gemeten met
`scripts/launch-direct.py TC5 --run`: de kern laadt de game, "Have fun :)").

## De dongle is nog niet compleet

De game vraagt een HASP-dongle: `TimeCrisisGame-Win64-Shipping.exe` en `RSLauncher.exe` importeren
`hasp_windows_x64_105293.dll` op ordinalen 2, 13, 14 en 15 (`hasp_decrypt`, `hasp_login`,
`hasp_logout`, `hasp_read`). Onze vervanging staat in `dropin/` en wordt met `WINEDLLOVERRIDES` in de
view van de game gezet.

Wat er **wel** is: de donglebeschrijving (`dropin/ravengun-hasp/TC5/dongle.json`, soort `hasp`) en de
bibliotheek.

Wat er **niet** is: de **geheugenimages**. `"memory"` is leeg, dus `scripts/hasp.py --game TC5 --json`
meldt deze dongle als onvolledig:

```
dropin/ravengun-hasp/TC5/dongle.json: geen 'memory'-items: de dongle-image van deze game is nog
niet vastgelegd.
```

Daarom staat `[Dongle]` **niet** aan in `game.ini` (zie `docs/per-game-patches.md`, "Dongle per game"):
de eigenaar kan hem aanzetten, maar de launcher weigert dan terecht, want er is geen image om te
lezen. Tot die tijd emuleert de TeknoParrot-kern de dongle van TC5 — dat is precies het stuk dat nog
aan TeknoParrot hangt.

## Wat er nodig is om die laatste stap te zetten

De image vastleggen: welke fileids de game opvraagt (`hasp_read`) en welke bytes hij terug verwacht.
Zonder de fysieke dongle kan dat alleen door de antwoorden van de bestaande emulatie af te vangen en
als image vast te leggen. Zie `docs/hasp-layer.md`.
