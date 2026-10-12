# Rabbids Hollywood: waarom de uitgeleverde route na ~70 s sterft

Gemeten 2026-10-12 door de dongle-specialist (alleen lezen op de NAS, geen commits). Dit is de oorzaak
van het gedrag dat eerder als "onverklaard" in de statusbestanden stond.

## Het is een uitrolprobleem, geen verkeerde donglewaarde

* De plugin in de **uitgeleverde** view
  (`runtime/views-aagames/RabbidsHollywood/BepInEx/plugins/RavenGun.RabbidsHollywood.dll`, 32.768 B,
  mtime 2026-10-11 17:12, md5 `b4fe9d83…`) bevat **geen enkele Matrix-code**: geen
  `MatrixInit_Prefix`/`MatrixFind_Prefix`/`MatrixCount_Prefix`/`MatrixSerNr_Prefix`, en geen
  `Dongle_Count`/`Dongle_ReadSerNr`-teksten.
* De Matrix-fix kwam later in `plugins/unity/src/rabbids/Rabbids.cs` (19:53) en zit **alleen** in
  `build/unity-plugin/RabbidsHollywood/RavenGun.RabbidsHollywood.dll` (34.304 B, md5 `29fd3ddc…`) en in
  de **niet-uitgeleverde** view `views/RabbidsHollywood`.
* Route B is sinds 17:13 **niet meer gestart** (de `LogOutput.log` van die view is van 17:13), dus
  **nooit met de dongle-fix**.
* Dat logboek laat de fout ook zien: **nul `Matrix:`-regels** en `[Info : Unity Log] **** DONGLE NOT FOUND`,
  met als laatste regel `OnChooseWorldEnter` -- precies het symptoom "haalt de wereldkeuze en stopt
  binnen 50-70 s".
* En de oorzaak dat er niets antwoordde: `views-aagames/<Game>/Game_Data/Plugins` is een **symlink naar de
  NAS**, dus de `matrix64.dll` die daar (en in de spelmap) stond is de **originele** van 93.184 B
  (md5 `c86908ab…`), niet onze drop-in van 25.614 B.

**Bevestigd aan mijn kant:** die symlink is echt. In
`runtime/views-aagames/RabbidsHollywood/Game_Data/Plugins` wijst naar
`/home/deck/mnt/spidernas_emulation/Teknoparrot/Rabbids Hollywood /Game_Data/Plugins`. Dat betekent ook
dat een drop-in die daarheen schrijft **op de NAS zou schrijven** -- zie hieronder.

## Wat het spel vraagt

`SBK.Matrix.MatrixManager.Awake` -> `KDLGNPHHMKI`:

1. `Init_MatrixAPI() >= 0`
2. `Dongle_Count(85) > 0`
3. `Dongle_ReadSerNr(dongle, aantal, 85) > 0`

En anders: `DONGLE NOT FOUND`, 30 seconden `WaitForSeconds`, en `Application.Quit`.

## En het zou zichzelf moeten uitrollen

`play-unity.sh` (regels 72-75) draait bij **elke** start `aagames-setup.py` opnieuw met de build aan, en
`install_ravengun` kopieert de verse DLL naar de view. De fix hoort zich dus bij de volgende start zelf
te installeren -- **dat moet nog bewezen worden**, en dat is de eerste meting die nu loopt.

## Twee dingen om te repareren

1. **Bewijzen dat de verse DLL aankomt** in de uitgeleverde view (md5 vergelijken met de build-map).
   Een correcte fix die de view nooit bereikt is niets waard.
2. **`Game_Data/Plugins` lokaal maken** in `aagames-setup.py`. Het script belooft in zijn eigen
   docstring dat nieuwe bestanden van de game lokaal blijven, maar `link_all(..., copy_small=False)`
   linkt elk kind -- dus `Plugins` is een symlink naar de NAS. Een drop-in die daar landt, schrijft op de
   NAS. Dat is precies het risico dat deze meting aan het licht bracht.
