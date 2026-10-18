<!-- Gegenereerd door scripts/migrate-game-settings.py; verwijder deze regel als je het bestand zelf bewerkt. -->
# Infinity Blade FX

Instellingen: `game.ini`.
Bevindingen en meetgeschiedenis: `findings/InfinityBladeFX.md` (op de NAS naast de games, niet in deze repo).

## De Matrix/SBK-dongle

Deze game is de **enige in de inventaris die de Matrix-API native importeert** in plaats van via de
managed `MatrixDLLCSharp.MatrixWrapper` van de Unity-games. Bewijs per stap:

* `Binaries/Win32/SwordGame.exe` (49878016 bytes) heeft `matrix32.dll` in zijn **importtabel**, met
  precies drie ordinalen: **#40, #55 en #61**. Het is de enige game waar `matrix32.dll` als module in
  de imports staat.
* De exporttabel van het originele `matrix32.dll` (in die map bewaard als
  `matrix32.TEKNOPARROT.dll`, 114688 bytes, md5 `6e75f453dca9380144620d80f467a0e6`, ordinal base 1,
  113 namen) geeft bij die ordinalen: **#40 = `Init_MatrixAPI`**, **#55 = `Dongle_Count`**,
  **#61 = `Dongle_ReadSerNr`**.
* `Binaries/Win32/matrix32.dll` is TeknoParrot's eigen emulator (109056 bytes,
  md5 `3a5bed5fc3de3797073fced37b33d41f`). Zijn exports op die drie ordinalen zijn met objdump
  gelezen:

  | Ordinaal | Naam | Bytes | Terug |
  |---|---|---|---|
  | 40 | `Init_MatrixAPI` | `33 c0 c3` (`xor eax,eax; ret`) | **0** |
  | 55 | `Dongle_Count` | `b8 02 00 00 00 c3` (`mov eax,2; ret`) | **2** |
  | 61 | `Dongle_ReadSerNr` | `b8 2c 0f 01 00 c3` (`mov eax,0x10f2c; ret`) | **69420** |

* `Binaries/Win32/readme.txt` beschrijft precies de werkende route: hernoem `matrix32.dll` naar
  `matrix32.TEKNOPARROT.dll` en zet TeknoParrot's `matrix32.dll`, `dinput8.dll` en `TPFIX.ini` in
  dezelfde map.
* `SwordGame.exe` bevat de UTF-16 teksten `Failed to initialize the MatrixAPI`,
  `No Dongle Present. ERROR` en `No Dongle! ERROR`: de game controleert de MatrixAPI zelf.

Wat de laag antwoordt (dezelfde drie getallen als TeknoParrot's emulator):

| Functie | Terug | Waarom |
|---|---|---|
| `Init_MatrixAPI` | 0 | 0 = in orde |
| `Dongle_Count(...)` | 2 | letterlijk de waarde van TeknoParrot's export op ordinaal 55 |
| `Dongle_ReadSerNr(...)` | 69420 | letterlijk de waarde van TeknoParrot's export op ordinaal 61 (0x10F2C), geen echt serienummer |

### Wat niet gemeten is

* **De poort.** De drie imports staan in de importtabel, maar er is geen enkele statische aanroep naar
  te vinden: geen `call dword ptr [<IAT>]`, geen `E8`-aanroep naar de import-thunk, en de IAT- en
  thunk-adressen komen nergens anders in het bestand voor (ook niet in `.reloc`). De aanroepen worden
  tijdens het draaien opgelost. `matrix/matrix.ini` zet de poort daarom **niet**; de bibliotheek
  gebruikt dan haar standaardwaarde (85). Dat verandert niets aan het antwoord, want `Dongle_Count` en
  `Dongle_ReadSerNr` gebruiken de poort niet — TeknoParrot's emulator geeft op ordinaal 55 eveneens
  altijd 2 terug, ongeacht de poort.
* **Of de game zich zonder dongle afsluit.** De game is geparkeerd op een ander probleem (UE3 start,
  daarna wit of zwart beeld; het log stopt bij `Kaboom: InitLib() failed`,
  `findings/InfinityBladeFX.md`). Daarom staat er geen `enforced` in `dongle.json`.

## De drop-in

```
dropin/
  dropin.ini                              To = Binaries/Win32, DllOverride = native,
                                          Env = RAVENGUN_MATRIX_DIR=matrix;...
  matrix32.dll                            onze eigen Matrix-donglelaag (matrix/src/, scripts/build-matrix.sh)
  matrix/matrix.ini                       de gegevens: aantal 2 (poort bewust niet gezet)
  ravengun-hasp/InfinityBladeFX/dongle.json    welke dongle deze game vraagt (kind matrix) + het bewijs
```

Deze game is 32-bits en importeert `matrix32.dll`; `matrix64.dll` staat bewust **niet** in de drop-in.
De gegevensmap staat naast de bibliotheek, want de werkmap van een UE3-game is doorgaans de map van de
exe en niet de hoofdmap van de game. Er is geen `[Dongle]`-sectie in `game.ini`: de loader kent geen
dongles meer (`docs/geen-dongle-in-de-loader.md`); de dongle is een gewone drop-in.

**Let op bij het uitrollen:** in deze map staat al een `matrix32.dll` (TeknoParrot's emulator) en is het
origineel bewaard als `matrix32.TEKNOPARROT.dll`. Onze drop-in vervangt die `matrix32.dll`. Draait de
game rechtstreeks van de NAS, dan komt er niets aan — er is een lokale view onder
`~/.local/share/ravengun/runtime/views/InfinityBladeFX/` nodig (de motor schrijft nooit naar de NAS).

### Meting in het spel (2026-10-11)

Gemeten: de drop-in komt aan. De motor rolt `matrix32.dll` (25614 bytes, onze eigen build) en
`matrix/matrix.ini` uit naar `views/InfinityBladeFX/Binaries/Win32/`, en de game start via de lader:

```
Game:   Z:\...\views\InfinityBladeFX\Binaries\Win32\SwordGame.exe (1.0.7887.0)
Working directory: Z:\...\views\InfinityBladeFX\Binaries\Win32
Loading game...
Success!
Loading core...
Resolved target kernel32.dll procedure: local=7BE7E2F4 remote=7BE7E2F4 rva=0xE2F4
```

De game leefde na 30 seconden nog. **Onze bibliotheek is in die proef niet aangeroepen**: er is geen
`matrix.log`, en de game stopt zoals bekend op `Kaboom: InitLib() failed` (hij is geparkeerd op dat
probleem, niet op de dongle). Of de Matrix-controle van deze game onze antwoorden nodig heeft, is dus
niet in het spel vastgesteld — alleen dat de bestanden op de juiste plek staan en dat de lader niet op
onze DLL struikelt. Dat is precies de reden dat `dongle.json` geen `enforced`-claim heeft.

Controleren:

```
python3 scripts/matrix.py --game InfinityBladeFX
python3 scripts/launch-direct.py InfinityBladeFX --plan      # kijk naar de dropins-regel
```
