# Rampage

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/Rampage.md` (op de NAS naast de games, niet in deze repo).

## De Matrix/SBK-dongle

Deze game **vraagt** een Matrix/SBK-dongle, maar hij valt zonder dongle terug en blijft spelen. Bewijs
per stap:

* In `Game_Data/Managed/Assembly-CSharp.dll` staan `MatrixManager`, `MatrixDLLCSharp.MatrixWrapper`,
  `Dongle_Count`, `Dongle_ReadSerNr` en `Init_MatrixAPI`; in de spelmap en in `Game_Data/Plugins` staat
  `matrix32.dll` én `matrix64.dll`.
* `MatrixManager::GetSerial` (aangeroepen uit `MatrixManager::Start`) doet:
  `ldc.i4.s 85` → `Dongle_Count(85)`; is dat ≤ 0 dan `return 2`; anders
  `Dongle_ReadSerNr(Dongle, aantal, 85)`. **De poort is dus 85**, net als bij Rabbids Hollywood en
  Tomb Raider. De IL staat in `dropin/ravengun-hasp/Rampage/dongle.json`.
* `MatrixManager::Start` roept `Process::Kill()` aan als `GetSerial` iets anders dan 0 teruggeeft —
  maar `GetSerial` keert meteen met 0 terug als het veld `Dongle` leeg is, en niets in de assembly zet
  dat veld. In deze assembly roept geen enkele `MatrixManager`-methode `Application::Quit`. Daarom is
  `enforced` in `dongle.json` **false**, en dat klopt met de meting: de game start en speelt zonder
  dongle (`findings/Rampage.md`).
* De tekenreeks `MATRIX_NO_DONGLE_FOUND` is een lid van de `MatrixError`-enum, geen logregel.

De dongle is hier dus een **aanvulling**, geen voorwaarde. Wat de laag antwoordt:

| Functie | Terug | Waarom |
|---|---|---|
| `Init_MatrixAPI` | 0 | 0 = in orde |
| `Dongle_Count(85)` | 2 | de controle eist > 0; 2 is ook wat TeknoParrot's eigen emulator geeft |
| `Dongle_ReadSerNr(..., 85)` | 69420 | de controle eist ≥ 0 (Rampage) respectievelijk > 0; vaste waarde 0x10F2C, nooit een echt serienummer |

## De drop-in

```
dropin/
  dropin.ini                            To = Game_Data/Plugins, DllOverride = native,
                                        Env = RAVENGUN_MATRIX_DIR=matrix;...
  matrix64.dll                          onze eigen Matrix-donglelaag (matrix/src/, scripts/build-matrix.sh)
  matrix/matrix.ini                     de gegevens van deze dongle: poort 85, aantal 2
  ravengun-hasp/Rampage/dongle.json     welke dongle deze game vraagt (kind matrix) + het bewijs
```

Deze game is 64-bits (`[Game] Is64Bit = true`) en importeert `matrix64.dll`; `matrix32.dll` staat
bewust **niet** in de drop-in. Er is geen `[Dongle]`-sectie in `game.ini`: de loader kent geen dongles
meer (`docs/geen-dongle-in-de-loader.md`); de dongle is een gewone drop-in.

**Nog niet gemeten:** de drop-in-motor rolt een drop-in altijd uit, ook voor een game die het zonder
dongle al doet. Of deze game met onze `matrix64.dll` in plaats van het origineel precies hetzelfde
blijft doen, is niet in het spel getest. De aanroepen die de game doet (`Init_MatrixAPI`,
`Dongle_Count`, `Dongle_ReadSerNr`) krijgen dezelfde antwoorden als van TeknoParrot's emulator, en de
rest van de 113 namen is een stub; `Dongle_ReadData`/`Dongle_WriteData` komen op dit pad niet voor.

### Meting in het spel (2026-10-11)

Wel gemeten: de drop-in komt aan (de motor rolt hem uit naar
`views/Rampage/Game_Data/Plugins/`) en **onze** bibliotheek wordt geladen en aangeroepen. De game
leefde na 30 seconden nog. `views/Rampage/matrix.log` (geschreven door `matrix64.dll`):

```
[matrix] gegevensmap gevonden = 0
[matrix] matrix.ini gelezen, poort = 85
[matrix] matrix.ini gelezen, aantal = 2
[matrix] Init_MatrixAPI -> 0 = 0
```

**Let op wat daar NIET staat:** `Dongle_Count` en `Dongle_ReadSerNr` worden niet aangeroepen. Dat is
precies wat de IL hierboven voorspelt: `MatrixManager::GetSerial` keert meteen met 0 terug omdat het
veld `Dongle` leeg is, en `MatrixManager::Start` komt dus nooit aan de `Process::Kill()`. Deze game
raakt de dongle in de praktijk niet aan — het bewijs dat `enforced = false` klopt.

`Dongle_ReadData`, `Dongle_WriteData`, `Dongle_MemSize`, `Dongle_EncryptData` en
`Dongle_DecryptData` komen in de hele assembly **niet** voor (`grep-il` met dnlib). Deze game leest dus
geen dongle-geheugen en gebruikt de dongle ook niet voor zijn versleuteling; daarom is er geen
memory-lijst in `dongle.json` en raakt de no-op-versleuteling van onze laag deze game niet.

Controleren:

```
python3 scripts/matrix.py --game Rampage
python3 scripts/launch-direct.py Rampage --plan      # kijk naar de dropins-regel
```

Deze game draaide rechtstreeks van de NAS; er is een lokale view onder
`~/.local/share/ravengun/runtime/views/Rampage/` nodig voordat een drop-in ergens aankomt (de motor
schrijft nooit naar de NAS).
