# Rabbids Hollywood Arcade

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/RabbidsHollywood.md` (op de NAS naast de games, niet in deze repo).

## De Matrix/SBK-dongle

Deze game sterft aan zijn eigen dongle-controle. Wat hij precies verbruikt, wat TeknoParrot doet en
welk bewijs daarvoor is, staat in `docs/matrix-layer.md`. Kort:

* `SBK.Matrix.MatrixManager.Awake` eist `Dongle_Count(85) > 0` én `Dongle_ReadSerNr(..., 85) > 0`.
  Zonder die twee antwoorden volgt `**** DONGLE NOT FOUND` en `UnityEngine.Application.Quit`,
  ongeveer 30 seconden later.
* De game leest **geen** dongle-geheugen: `Dongle_ReadData`/`Dongle_WriteData` komen in het spelpad
  niet voor. Er is dus geen dongle-image nodig.
* Er is geen `dongle.json` en geen `[Dongle]`-sectie: de loader weet niets van dongles
  (`docs/geen-dongle-in-de-loader.md`). Alles wat deze game nodig heeft staat in deze map.

### De drop-in (de bedoelde route)

```
dropin/
  dropin.ini            To = Game_Data/Plugins, DllOverride = native, Env = RAVENGUN_MATRIX_DIR=matrix;...
  matrix64.dll          onze eigen Matrix-donglelaag (matrix/src/, scripts/build-matrix.sh)
  matrix/matrix.ini     de gegevens van deze dongle: poort 85, aantal 2
```

De bibliotheek leest `matrix.ini` zelf; de loader weet niet wat erin staat. **Let op `To`:** de game
laadt `matrix64.dll` uit `Game_Data/Plugins`, niet uit de hoofdmap (gemeten met
`WINEDEBUG=+loaddll`). Omdat `Game_Data` in de view een koppeling naar de NAS is, moet wie het
bestand plaatst die map eerst lokaal maken; `scripts/matrix.py --install` doet dat, en zet ook de
gegevensmap naast de bibliotheek. Controleren: `scripts/matrix.py --game RabbidsHollywood`.

### De patch (terugval)

`plugins/unity/src/rabbids/Rabbids.cs` beantwoordt dezelfde aanroepen in de managed wrapper. Die route
werkt vandaag en heeft geen bestand in de view nodig; `RAVENGUN_MATRIX_DLL=1` (uit `dropin.ini`) zet
hem uit zodat de DLL het alleen doet.

De dongle-patch staat **niet** in `patches.ini`: dat formaat heeft er bewust geen categorie voor (zie
de toelichting bij `PATCH_CATEGORIES` in `scripts/gamesettings.py` en docs/per-game-patches.md).
