# Tomb Raider Arcade

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/TombRaider.md` (op de NAS naast de games, niet in deze repo).

## De Matrix/SBK-dongle

Deze game **vraagt** een Matrix/SBK-dongle, maar hij valt zonder dongle stil terug. Bewijs per stap:

* In `Game_Data/Managed/Assembly-CSharp.dll` staan `SBK.Matrix.MatrixManager`,
  `MatrixDLLCSharp.MatrixWrapper`, `Dongle_Count`, `Dongle_ReadSerNr` en `Init_MatrixAPI`; in de
  spelmap en in `Game_Data/Plugins` staat `matrix32.dll` én `matrix64.dll`.
* `SBK.Matrix.MatrixManager::Awake` roept `ReadSerial` aan, en die doet: `ldc.i4.s 85` →
  `Dongle_Count(85)`; is dat ≤ 0 dan `return 2`; anders `Dongle_ReadSerNr(Dongle, DongleId, 85)`, en
  daarna `IsDonglePresent = reeks > 0`. **De poort is dus 85.** De IL staat in
  `dropin/ravengun-hasp/TombRaider/dongle.json`.
* Bij een mislukte controle start `Awake` de coroutine `DongleNotFound`. Die wacht
  `Random.Range(300.0, 600.0)` seconden en eindigt daarna: geen `Debug.Log`, geen
  `Application::Quit` en geen `Process::Kill`. In deze assembly roept geen enkele
  `MatrixManager`-methode `Application::Quit`. Daarom is `enforced` in `dongle.json` **false**.
* De tekenreeks `MATRIX_NO_DONGLE_FOUND` is een lid van de `MatrixError`-enum, geen logregel.
* Wat deze game oorspronkelijk blokkeerde was **niet** de dongle maar zijn eigen USB-I/O-bord
  ("IO BOARD ERROR", `GetVersionDRV_USB`, "Base not found"); dat is met de eigen plugin opgelost
  (`findings/TombRaider.md`).

Wat de laag antwoordt:

| Functie | Terug | Waarom |
|---|---|---|
| `Init_MatrixAPI` | 0 | 0 = in orde |
| `Dongle_Count(85)` | 2 | de controle eist > 0; 2 is ook wat TeknoParrot's eigen emulator geeft |
| `Dongle_ReadSerNr(..., 85)` | 69420 | de controle eist > 0; vaste waarde 0x10F2C, nooit een echt serienummer |

## De drop-in

```
dropin/
  dropin.ini                            To = Game_Data/Plugins, DllOverride = native,
                                        Env = RAVENGUN_MATRIX_DIR=matrix;...
  matrix64.dll                          onze eigen Matrix-donglelaag (matrix/src/, scripts/build-matrix.sh)
  matrix/matrix.ini                     de gegevens van deze dongle: poort 85, aantal 2
  ravengun-hasp/TombRaider/dongle.json  welke dongle deze game vraagt (kind matrix) + het bewijs
```

Deze game is 64-bits: `Game.exe` is PE32+ x86-64 en de managed wrapper noemt de naam `matrix64` in de
assembly. In de spelmap en in `Game_Data/Plugins` staat `matrix64.dll` (93184 bytes, md5
`c86908ab10b1d9249c896e42b7618f67`, byte-identiek aan die van Rampage en Rabbids Hollywood) naast
`matrix32.dll`. `matrix32.dll` staat bewust **niet** in de drop-in. Er is geen `[Dongle]`-sectie in
`game.ini`: de loader kent geen dongles meer (`docs/geen-dongle-in-de-loader.md`); de dongle is een
gewone drop-in.

Deze game draait via de **unity-tcp-route** (`plugins/unity/src/tombraider/`, zonder TeknoParrot). De
drop-in hoort bij de TeknoParrot-route; de plugin raakt de dongle-controle niet aan, dus er is geen
`RAVENGUN_MATRIX_DLL` nodig (die variabele hoort alleen bij de Rabbids-patch).

**Nog niet gemeten:** de drop-in-motor rolt een drop-in altijd uit, ook voor een game die het zonder
dongle al doet. Of deze game met onze `matrix64.dll` in plaats van het origineel precies hetzelfde
blijft doen, is niet in het spel getest.

### Meting in het spel (2026-10-11)

Wel gemeten: de drop-in komt aan (de motor rolt hem uit naar
`views/TombRaider/Game_Data/Plugins/`) en **onze** bibliotheek beantwoordt de dongle-controle van
deze game. De game leefde na 30 seconden nog. `views/TombRaider/matrix.log` (geschreven door
`matrix64.dll`):

```
[matrix] gegevensmap gevonden = 0
[matrix] matrix.ini gelezen, poort = 85
[matrix] matrix.ini gelezen, aantal = 2
[matrix] Init_MatrixAPI -> 0 = 0
[matrix] Dongle_Count -> aantal, poort = 85
[matrix] Dongle_ReadSerNr -> 69420, poort = 85
```

Dat is de volledige controle uit `SBK.Matrix.MatrixManager::ReadSerial`, met poort 85 en de twee
verwachte antwoorden. De plugin-route (`plugins/unity/src/tombraider/`) raakt de dongle niet aan, dus
dit is het bewijs dat de DLL het doet.

### Let op: deze game gebruikt de dongle ook voor zijn versleuteling

`SBK.Matrix.MatrixManager::EncryptWithDongle` en `DecryptWithDongle` roepen
`Dongle_EncryptData`/`Dongle_DecryptData` aan (aangeroepen uit `SBK.Matrix.MatrixMessage::Decrypt`).
Zij eisen eerst dat de reeks groter dan 0 is, en met onze laag is dat zo (69420), dus die aanroepen
gebeuren ook echt.

Onze laag laat de buffer **ongemoeid** en geeft 0 terug — dezelfde keuze als bij de HASP-laag
(`docs/matrix-layer.md`). Dat is met opzet: zolang **beide** richtingen een no-op zijn, schrijft de
game platte tekst weg en leest die ook weer als platte tekst, en blijft de opslag in zichzelf
consistent. Wat er **niet** gebeurt: gegevens die met een echte dongle versleuteld zijn, kan deze laag
niet ontsleutelen. In deze metingen is dat niet uitgeoefend (geen bestaande save met een echte dongle
getest). Zou een game zijn opslag op de versleuteling valideren, dan moet dit opnieuw bekeken worden.

`Dongle_ReadData`, `Dongle_WriteData` en `Dongle_MemSize` komen in de hele assembly **niet** voor
(`grep-il` met dnlib): deze game leest dus geen dongle-geheugen, en daarom is er geen memory-lijst in
`dongle.json`.

Controleren:

```
python3 scripts/matrix.py --game TombRaider
python3 scripts/launch-direct.py TombRaider --plan      # kijk naar de dropins-regel
```

Deze game draaide rechtstreeks van de NAS; er is een lokale view onder
`~/.local/share/ravengun/runtime/views/TombRaider/` nodig voordat een drop-in ergens aankomt (de motor
schrijft nooit naar de NAS).
