# Rabbids Hollywood Arcade

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/RabbidsHollywood.md` (op de NAS naast de games, niet in deze repo).

## De Matrix-dongle

`dongle.json` in deze map betekent: **deze game vraagt een dongle**, en wel een **Matrix/SBK-LPT-dongle**
(`kind: matrix`). Wat de game er precies van verbruikt, welke twee routes er zijn en welk bewijs
daarvoor is, staat in `docs/matrix-layer.md`.

Kort:

* De game controleert bij het opstarten `Dongle_Count(85) > 0` en `Dongle_ReadSerNr(..., 85) > 0`
  (SBK.Matrix.MatrixManager). Zonder die twee antwoorden volgt `**** DONGLE NOT FOUND` en
  `UnityEngine.Application.Quit`, ongeveer 30 seconden later.
* De game leest **geen** dongle-geheugen, dus er staat niets in `dongle/`.
* **Standaardroute (aan):** de Harmony-patch in `plugins/unity/src/rabbids/Rabbids.cs` beantwoordt de
  drie aanroepen. Er is dan geen bestand in de view nodig.
* **Alternatief (uit):** `dropin/` bevat onze eigen `matrix64.dll`; `dropin.ini` beschrijft waar hij
  heen gaat en welke omgevingsvariabelen erbij horen (docs/dropins.md). Aanzetten doet de eigenaar in
  zijn eigen laag met `[Dongle] Enabled = true` en `Source = drop-in` in `game.ini`.

De dongle-patch staat **niet** in `patches.ini`: dat formaat heeft er bewust geen categorie voor
(zie de toelichting bij `PATCH_CATEGORIES` in `scripts/gamesettings.py` en docs/per-game-patches.md).
