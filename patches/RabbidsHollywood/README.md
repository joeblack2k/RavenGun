# Rabbids Hollywood Arcade

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/RabbidsHollywood.md` (op de NAS naast de games, niet in deze repo).

## De Matrix/SBK-dongle

Deze game sterft aan zijn eigen dongle-controle. Wat hij precies verbruikt, wat TeknoParrot doet en
welk bewijs daarvoor is, staat in `docs/matrix-layer.md`. Kort:

* `SBK.Matrix.MatrixManager.Awake` roept `KDLGNPHHMKI` aan, en die eist `Dongle_Count(85) > 0` én
  `Dongle_ReadSerNr(Dongle, aantal, 85) > 0`. Zonder die twee antwoorden volgt `**** DONGLE NOT FOUND`
  en `UnityEngine.Application.Quit`, ongeveer 30 seconden later.
* De poort is **85**, niet 40: de variant met poort 40 (`EGIEJAEHODM`) staat wel in de assembly maar
  wordt door niets aangeroepen.
* De game leest **geen** dongle-geheugen: `Dongle_ReadData`/`Dongle_WriteData` komen in het spelpad
  niet voor. Er is dus geen dongle-image nodig.
* Er is **geen `[Dongle]`-sectie** in `game.ini`: de loader kent geen dongles meer
  (`docs/geen-dongle-in-de-loader.md`). De dongle is een gewone drop-in, en de beschrijving van welke
  dongle deze game vraagt staat als data in de drop-in zelf:
  `dropin/ravengun-hasp/RabbidsHollywood/dongle.json` (`kind: "matrix"`, poort 85, aantal 2,
  serienummer 69420). De bibliotheek leest dat bestand niet; zij leest `dropin/matrix/matrix.ini`.

### De drop-in (de bedoelde route)

```
dropin/
  dropin.ini                              To = Game_Data/Plugins, DllOverride = native,
                                          Env = RAVENGUN_MATRIX_DIR=matrix;...
  matrix64.dll                            onze eigen Matrix-donglelaag (matrix/src/, scripts/build-matrix.sh)
  matrix/matrix.ini                       de gegevens van deze dongle: poort 85, aantal 2
  ravengun-hasp/RabbidsHollywood/dongle.json   welke dongle deze game vraagt (kind matrix) + het bewijs
```

De bibliotheek leest `matrix.ini` zelf; de loader weet niet wat erin staat. **Let op `To`:** de game
laadt `matrix64.dll` uit `Game_Data/Plugins`, niet uit de hoofdmap (gemeten met `WINEDEBUG=+loaddll`).
De drop-in-motor maakt die map eerst lokaal als ze in de view een koppeling naar de NAS is, zodat er
nooit naar de NAS geschreven wordt. Controleren:

```
python3 scripts/matrix.py --game RabbidsHollywood
python3 scripts/launch-direct.py RabbidsHollywood --plan      # kijk naar de dropins-regel
```

Deze game is 64-bits en importeert `matrix64.dll`; `matrix32.dll` staat bewust **niet** in de drop-in.

### De patch (terugval)

`plugins/unity/src/rabbids/Rabbids.cs` beantwoordt dezelfde aanroepen in de managed wrapper. Die route
werkt en heeft geen bestand in de view nodig; `RAVENGUN_MATRIX_DLL=1` (uit `dropin.ini`) zet hem uit
zodat de DLL het alleen doet.

De dongle-patch staat **niet** in `patches.ini`, maar om een andere reden dan hier eerder stond. Het
formaat heeft de categorieën `dongle`, `region`, `keychip`, `integrity` en `activation` wél
(`PATCH_CATEGORIES` in `scripts/gamesettings.py`, en `docs/per-game-patches.md`, "Categorie en
beleid"). Ze staan hier niet omdat deze dongle **geen bytepatch** is: `matrix64.dll` moet naast de
game staan en de game moet hem laden. Dat is een drop-in, en die is hierboven beschreven. Een
bytepatch zou hier niets kunnen overslaan wat de DLL niet al doet.

Let op: `docs/per-game-patches.md` spreekt zichzelf hierover tegen — de ene alinea noemt die vijf
categorieën "gewone patches zoals elke andere", de andere ("Grenzen") verbiedt patches op dongle-,
keychip-, regio-, integriteits- en abonnementscontroles. Die vraag ligt bij de eigenaar; zie
`patches/PATCHES-PER-GAME.md`, "Een beleidstegenstelling die het werk in de weg zit".

### De meting

Zonder dongle stopt de game na ongeveer 57 seconden (`**** DONGLE NOT FOUND`, daarna
`Application.Quit`). Met `Dongle_Count` → 2 én `Dongle_ReadSerNr` → 69420 liep hij in de metingen meer
dan 200 seconden door; via de drop-in in `Game_Data/Plugins` 132, 92 en 92 seconden in drie runs
(`docs/matrix-layer.md`, "Metingen"). De waarden komen niet uit de lucht: TeknoParrot's eigen
`matrix32.dll` (in `Infinity Blade FX/Binaries/Win32`) geeft op zijn exports precies
`Init_MatrixAPI` → 0, `Dongle_Count` → 2 en `Dongle_ReadSerNr` → 69420.

**Opnieuw gemeten met de drop-in-motor (2026-10-11):** de drop-in is niet meer met de hand geplaatst
maar door `scripts/launch-direct.py --run` zelf uitgerold naar
`views/RabbidsHollywood/Game_Data/Plugins/`. De game leefde na **150 seconden** nog (de proef is daar
afgebroken; zonder dongle sterft hij rond 57 s). Twee onafhankelijke bewijzen dat onze bibliotheek het
antwoord gaf en niet de plugin:

`views/RabbidsHollywood/matrix.log` (geschreven door `matrix64.dll`, via `RAVENGUN_MATRIX_LOG`):

```
[matrix] gegevensmap gevonden = 0
[matrix] matrix.ini gelezen, poort = 85
[matrix] matrix.ini gelezen, aantal = 2
[matrix] Init_MatrixAPI -> 0 = 0
[matrix] Dongle_Count -> aantal, poort = 85
[matrix] Dongle_ReadSerNr -> 69420, poort = 85
```

en in `BepInEx/LogOutput.log` staat in die run alleen nog

```
[Info   :RavenGun Rabbids Hollywood Arcade] Matrix: de drop-in matrix64.dll beantwoordt de
dongle-controle (patches uit)
```

terwijl de plugin-regels `Matrix: Dongle_Count(85) -> 2` en `Matrix: Dongle_ReadSerNr -> 69420` (de
terugvalroute) in die run **ontbreken**. `RAVENGUN_MATRIX_DLL=1` uit `dropin.ini` zet de plugin dus
inderdaad uit, en de DLL doet het alleen.

Eén keer is de start mislukt met `Failed to find kernel32.dll in target process ... / Failed to Load
DLL via RemoteThread! (Error 2b)` — de lader, niet de dongle. Een controleproef met en zonder de
drop-in gaf in beide gevallen een levende game, dus dat is een losse laderstoring.

### Let op: deze game gebruikt de dongle ook voor zijn versleuteling

Acht methoden in `SBK.Matrix.MatrixManager` roepen `Dongle_EncryptData` of `Dongle_DecryptData` aan
(o.a. `KPHIPOINDLP`, `AJPGJFKKFIB`, `EDNBLKCLMOI`, `NKLMBDBGNGM`); dat is de savegame-versleuteling.
Onze laag laat de buffer **ongemoeid** en geeft 0 terug, net als bij de HASP-laag. Dat is met opzet:
zolang beide richtingen een no-op zijn, schrijft de game platte tekst weg en leest die ook weer als
platte tekst, en blijft de opslag in zichzelf consistent. Wat er **niet** gebeurt: gegevens die met een
echte dongle versleuteld zijn, kan deze laag niet ontsleutelen. In deze metingen is dat niet uitgeoefend
(`docs/matrix-layer.md`, "Open").

`Dongle_ReadData`, `Dongle_WriteData` en `Dongle_MemSize` komen in de hele assembly **niet** voor
(`grep-il` met dnlib): deze game leest dus geen dongle-geheugen, en daarom is er geen memory-lijst in
`dongle.json`.
