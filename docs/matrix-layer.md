# De Matrix/SBK-donglelaag

*Ronde 12, 2026-10-11. Wat de Matrix-dongle is, wat de game er echt van verbruikt, wat TeknoParrot
doet, wat wij doen, en per game de status. Sinds `docs/geen-dongle-in-de-loader.md` is een
software-dongle geen loaderbegrip meer maar een **drop-in**; het mechanisme staat in
`docs/dropins.md` en wordt hier niet herhaald.*

## 1. Wat de dongle is

De Matrix (ook "SBK") is een **LPT-dongle**: een oude beveiligingsdongle in de printerpoort. Drie
games in de inventaris laden `matrix32.dll`/`matrix64.dll`:

| Game | Laadt | Status |
|---|---|---|
| **Rabbids Hollywood** | `matrix64.dll` (64-bits Unity) | **de game sterft eraan** — deze laag is daarom voor deze game |
| Rampage | `matrix64.dll` | tolereert het ontbreken: logt `MATRIX_NO_DONGLE_FOUND` en speelt door |
| Tomb Raider | `matrix64.dll` | blokkeerde niet op de dongle maar op het USB-I/O-bord; al opgelost met de plugin |

De oorspronkelijke bestanden staan op de NAS, byte-identiek in alle drie de games (zowel in de
hoofdmap als in `Game_Data/Plugins/`):

| Bestand | Grootte | Datum | Machine | md5 |
|---|---|---|---|---|
| `matrix32.dll` | 114688 | 2009-01-15 | PE32 i386 | `6e75f453dca9380144620d80f467a0e6` |
| `matrix64.dll` | 93184 | 2009-01-15 | PE32+ x86-64 | `c86908ab10b1d9249c896e42b7618f67` |

Beide importeren alleen `KERNEL32.dll`, en daaruit onder andere `DeviceIoControl`, `CreateFileA` en
`GetPrivateProfileStringA`/`GetPrivateProfileIntA`. De DLL leest dus `C:\windows\SYSTEM\MATRIX.INI`
en praat via `DeviceIoControl` met een stuurprogramma van de dongle. Onder Wine is er geen
stuurprogramma en geen dongle, dus vindt hij niets — dat is precies waarom de game zich afsluit.

## 2. De API: 113 namen, niet 27

`findings/MATRIX-dongle-API.md` noemt 27 functies; dat zijn de functies die de C#-wrapper
`MatrixDLLCSharp.MatrixWrapper` (een naamruimte ín `Assembly-CSharp.dll`, geen aparte assembly)
gebruikt. **De DLL zelf exporteert 113 namen**, in beide bitbreedtes, in dezelfde volgorde, met
ordinal base 1:

* de 27 van de wrapper;
* Java/JNI-wrappers (`Java_Matrix_*`, `Java_de_matrixlock_Matrix_*`);
* poort-I/O (`OpenIWPort`, `CloseIWPort`, `ReadPort`, `WritePort`, `HiResWait`, `HiResCntPresent`);
* hulpfuncties (`GetSystemVersion`, `MasterKey_Find`, `MasterKey_Find_USB2`, `SetConfig_MatrixNet`,
  `Dongle_FindEx`, `Dongle_GetDriverFlag`, `Dongle_SetDriverFlag`, `Dongle_SetLedFlag`,
  `Dongle_GetLoginFlag`, `get_user_name`, `DllRegisterServer`, `DllUnregisterServer`).

De **argumentgroottes zijn gemeten, niet geraden**: elke stdcall-functie in het origineel eindigt op
`ret N`, en N is het aantal argumentbytes. De 27 van de wrapper:

| Functie | args | terug | Functie | args | terug |
|---|---|---|---|---|---|
| `Init_MatrixAPI()` | 0 | short | `Dongle_ReadData(int,int&,short,short,short)` | 20 | short |
| `Release_MatrixAPI()` | 0 | short | `Dongle_ReadDataEx(...)` | 24 | short |
| `GetVersionAPI()` | 0 | int | `Dongle_ReadSerNr(int,short,short)` | 12 | **int** |
| `GetVersionDRV()` | 0 | int | `Dongle_WriteData(...)` | 20 | short |
| `GetVersionDRV_USB()` | 0 | int | `Dongle_WriteDataEx(...)` | 24 | short |
| `SetW95Access(short)` | 4 | void | `Dongle_WriteKey(int,int&,short,short)` | 16 | short |
| `GetPortAdr(short)` | 4 | short | `Dongle_GetKeyFlag(int,short,short)` | 12 | short |
| `PausePrinterActivity()` | 0 | short | `Dongle_Exit()` | 0 | short |
| `ResumePrinterActivity()` | 0 | short | `GetConfig_MatrixNet(short)` | 4 | int |
| `Dongle_Find()` | 0 | short | `LogIn_MatrixNet(int,short,short)` | 12 | short |
| `Dongle_Version(short,short)` | 8 | int | `LogOut_MatrixNet(int,short,short)` | 12 | short |
| `Dongle_Model(short,short)` | 8 | int | `Dongle_EncryptData(int,int&,short,short)` | 16 | short |
| `Dongle_MemSize(short,short)` | 8 | short | `Dongle_DecryptData(int,int&,short,short)` | 16 | short |
| `Dongle_Count(short)` | 4 | short | | | |

`matrix/src/controleer-abi.py` vergelijkt onze DLL met het origineel op naam én op `ret N`;
`scripts/build-matrix.sh` draait die controle na elke bouw. De laatste bouw: **113 namen identiek,
0 verschillen in de argumentgroottes**, voor `matrix32.dll` én `matrix64.dll`.

## 3. Wat TeknoParrot doet — en wat daaruit blijkt

TeknoParrot's eigen `matrix32.dll` staat in `Infinity Blade FX/Binaries/Win32/` (109056 bytes,
MSVC 14.24, 2023-02-04, md5 `3a5bed5fc3de3797073fced37b33d41f`); het origineel is daar bewaard als
`matrix32.TEKNOPARROT.dll`. Het exporteert dezelfde 113 namen — maar het is **geen herimplementatie**:

* `DllMain(DLL_PROCESS_ATTACH)` bouwt de letterlijke tekst `.\matrix32.TEKNOPARROT.dll`
  (RVA `0x100183ec`), doet `LoadLibraryA` daarop en vult met `GetProcAddress` een tabel van 113
  functiepunten;
* **110 van de 113 exports zijn `jmp DWORD PTR [slot]`** — ze geven de aanroep door aan het origineel;
* **drie zijn overschreven**, en dat zijn precies de beslissende:

| Export | Code in TeknoParrot's DLL | Terug |
|---|---|---|
| `Init_MatrixAPI` | `xor eax,eax; ret` | **0** (in orde) |
| `Dongle_Count` | `mov eax,2; ret` | **2** |
| `Dongle_ReadSerNr` | `mov eax,0x10f2c; ret` | **69420** |

TeknoParrot laat dus het origineel het werk doen en beantwoordt alleen de drie vragen die de game
stelt. Dat is meteen het bewijs dat de dongle voor deze game **geen gegevens** levert: als de game
dongle-geheugen las, zou het doorgeven aan een DLL zonder dongle misgaan.

## 4. Wat Rabbids Hollywood echt verbruikt

Uit de IL van `Assembly-CSharp.dll` (gelezen met `re/dnlib.py`), `SBK.Matrix.MatrixManager`:

`Awake` roept `KDLGNPHHMKI()` aan, en die doet:

```il
Init_MatrixAPI()                          ; < 0  -> "***** Failed to initialized MatrixAPI correctly"
Dongle_Count(85)                          ; <= 0 -> code 2
Dongle_ReadSerNr(dongle, aantal, 85)      ; <= 0 -> code 3
                                          ; anders code 0
```

Geeft die controle iets anders dan 0 terug, dan start `Awake` de coroutine
`MatrixManager+FKNNJPMOCDC`, en die doet:

```il
ldstr "**** DONGLE NOT FOUND"      ; Debug.Log
ldc.r4 30.0 / WaitForSeconds       ; 30 seconden wachten
call UnityEngine.Application::Quit
```

Slaagt de controle, dan start `MatrixManager+GJOEKNLLLHL`: die logt `**** DONGLE FOUND: <getal>` en
doet verder niets.

**Dat is de hele dongle-eis: twee getallen.** Geen `Dongle_ReadData`, geen `Dongle_WriteData`, geen
geheugenimage. De 30 seconden wachttijd plus de opstarttijd verklaren ook de gemeten ~60 seconden:
de game sterft niet meteen, maar een halve minuut na de controle.

Twee details die het onderzoek eerder op een dwaalspoor brachten:

* Er staat een **tweede, ongebruikte controlevariant** in de assembly (`EGIEJAEHODM`) die
  `Dongle_Count(40) > 1` eist. Die wordt door niets aangeroepen; de variant die `Awake` gebruikt
  (`KDLGNPHHMKI`) eist `Dongle_Count(85) > 0`. Wie alleen naar de eerste kijkt, concludeert ten
  onrechte dat de poort 40 is en dat het aantal minstens 2 moet zijn.
* `Dongle_EncryptData`/`Dongle_DecryptData` komen op dertien plaatsen voor (de versleuteling van de
  savegame), maar niet op het opstartpad. Onze laag laat ze ongemoeid en geeft 0 terug, net als bij
  de HASP; zolang beide richtingen hetzelfde behandelen blijft de opslag in zichzelf consistent. Dat
  is in deze metingen niet uitgeoefend.

## 5. Onze laag, en zijn eigen gegevens

`matrix/src/` bouwt twee bestanden met `scripts/build-matrix.sh`:

```
matrix/out/matrix32.dll    i686
matrix/out/matrix64.dll    x86-64
matrix/out/selftest        native Linux: controleert de teruggeefwaarden zonder Wine
```

De laag is **niet game-specifiek**: dezelfde twee DLL's bedienen elke game die de Matrix-API
gebruikt. Wat één game nodig heeft staat in de map van die game, als drop-in:

```
patches/RabbidsHollywood/dropin/
  dropin.ini                 per bestand: To, DllOverride, Env (docs/dropins.md)
  matrix64.dll               onze bibliotheek
  matrix/matrix.ini          de gegevens van deze dongle: poort 85, aantal 2
```

**De bibliotheek leest zijn eigen gegevens**, niet de loader
(`docs/geen-dongle-in-de-loader.md`). De drop-in geeft de weg mee met
`Env = RAVENGUN_MATRIX_DIR=matrix`; de bibliotheek zoekt `matrix.ini` daar, en anders naast zichzelf.
Een game die wél dongle-geheugen leest zet in datzelfde bestand `memory = matrix.bin` en legt dat
bestand ernaast. Voor Rabbids Hollywood is er geen geheugen, dus staat die regel er niet.

Het serienummer staat bewust **niet** in `matrix.ini`: dat is een vaste, zelfgekozen waarde in de
bibliotheek (0x10F2C, dezelfde als TeknoParrot's emulator). Er hoort geen echt serienummer in een
bestand te staan, en er wordt er ook geen gelezen of gelogd.

De waarden die de laag teruggeeft:

| Functie | Terug | Waarom |
|---|---|---|
| `Init_MatrixAPI` | 0 | 0 = in orde; negatief betekent "niet geïnitialiseerd" en dan gebruikt de game de rest niet |
| `Dongle_Find` | 1 | gevonden |
| `Dongle_Count(poort)` | 2 | de controle eist > 0; de ongebruikte tweede variant eist > 1. 2 voldoet aan allebei |
| `Dongle_ReadSerNr(...)` | 69420 | de controle eist > 0; vaste waarde, nooit een echt serienummer |
| `Dongle_MemSize` | grootte van het geheugen, of 0 | eerlijk: zonder gegevensbestand is er geen geheugen |
| `Dongle_ReadData`/`WriteData` | 0 bij gegevens, anders 1 | leest/schrijft het geheugenbestand als dat er is |
| `Dongle_EncryptData`/`DecryptData` | 0, gegevens ongemoeid | net als bij de HASP: beide richtingen gelijk behandelen houdt de opslag consistent |
| de overige 106 namen | 0 | stubs, zodat de DLL een echte drop-in is |

## 6. Twee routes naar hetzelfde antwoord

### Route A — de patch (terugval)

`plugins/unity/src/rabbids/Rabbids.cs` beantwoordt dezelfde vier aanroepen in de managed wrapper.
Deze route werkt vandaag en heeft geen bestand in de view nodig.

Twee dingen waren daar mis, en die verklaren het hele "DONGLE NOT FOUND"-verhaal:

1. de uitgeleverde plugin faalde op een **parameternaam** in de Harmony-prefix (`poort` terwijl het
   argument `Port` heet), waardoor `Dongle_Count` helemaal niet gepatcht werd;
2. zelfs met een geslaagde patch was het **niet genoeg**: de prefix beantwoordde alleen
   `Dongle_Count`, en de controle gaat daarna door naar `Dongle_ReadSerNr`. Zonder die tweede is het
   antwoord alsnog code 3 → `DONGLE NOT FOUND` → `Application.Quit`.

`RAVENGUN_MATRIX_DLL=1` (gezet door `dropin.ini`) zet deze patches uit, zodat de DLL het alleen doet.

### Route B — de drop-in (de bedoelde route)

`patches/RabbidsHollywood/dropin/` bevat onze `matrix64.dll` plus zijn gegevens. **Let op de
doelmap: `Game_Data/Plugins`, niet de hoofdmap van de game.** Met `WINEDEBUG=+loaddll` geladen blijkt
de game *beide* exemplaren te laden, maar de aanroepen uit `MatrixDLLCSharp` komen uit
`Game_Data/Plugins/matrix64.dll`. Met onze DLL alleen in de hoofdmap sloot de game zich na ongeveer
een minuut nog steeds af.

`Game_Data` is in de view een koppeling naar de NAS, dus wie dit bestand plaatst moet die map eerst
lokaal maken (een echte map met koppelingen naar de rest); anders wijst het doel naar de NAS.
`scripts/matrix.py --install` doet dat, en zet ook de gegevensmap naast de bibliotheek.

## 7. Metingen

Alle metingen met `scripts/launch-direct.py RabbidsHollywood --run`, het BepInEx-log in de view, en
het aantal seconden dat de game leefde (afgebroken door een tijdslimiet betekent: de game liep nog).

| Run | Route | Uitkomst |
|---|---|---|
| basis | plugin-patch die niet toepast (`poort`/`Port`) | **57 s**, daarna `Application.Quit` |
| na de parameternaam-fix, alleen `Dongle_Count` → 1 | patch | **57 s** — bewijst dat `Dongle_ReadSerNr` de ontbrekende schakel is |
| `Dongle_Count` → 2 én `Dongle_ReadSerNr` → 69420 | patch | **> 200 s**, geen fouten in het log |
| drop-in alleen in de hoofdmap van de game | DLL | **~62 s** — de game gebruikt die kopie niet |
| drop-in in `Game_Data/Plugins`, patches uit | DLL | **132 s** en **92 s** in twee runs |
| idem, met de gegevensmap naast de bibliotheek (definitieve revisie) | DLL | **92 s**, game liep nog |

Het BepInEx-log van een geslaagde DLL-run laat zien dat onze DLL werkelijk is aangeroepen — dat log
komt uit de DLL zelf (`RAVENGUN_MATRIX_LOG`), niet uit de plugin:

```
[matrix] Init_MatrixAPI -> 0 = 0
[matrix] Dongle_Count -> aantal, poort = 85
[matrix] Dongle_ReadSerNr -> 69420, poort = 85
```

En de definitieve revisie, in de view, laat zien dat de bibliotheek haar eigen gegevens daar vindt:

```
[matrix] gegevensmap gevonden = 0
[matrix] matrix.ini gelezen, poort = 85
[matrix] matrix.ini gelezen, aantal = 2
[matrix] Init_MatrixAPI -> 0 = 0
[matrix] Dongle_Count -> aantal, poort = 85
[matrix] Dongle_ReadSerNr -> 69420, poort = 85
```

Datzelfde is ook zonder game aangetoond met de native zelftest: met een `matrix/matrix.ini` naast de
binary (zonder `RAVENGUN_MATRIX_DIR`) meldt het log `matrix.ini gelezen, poort = 85, aantal = 7` en
geeft `Dongle_Count` 7 terug.

## 8. En de vraag waarom DemulShooter's plugin de game 229 s in leven hield

Die vraag is met de bron te beantwoorden, en het antwoord is **nee, niet via de dongle**.
`views/RabbidsHollywood/BepInEx/plugins/UnityPlugin_BepInEx_RHA.dll` is DemulShooter's plugin
(assembly `UnityPlugin_BepInEx_RHA`, guid `argonlefou.mouse.rha`, "RHA Mouse Plugin 1.0.0"). Zijn
volledige patchlijst:

`CrosshairWindow.CrosshairMove`, `ApplicationManager.Awake` (postfix), `OperatorMenu.ResetData`
(postfix), `ArcadeManager.CheckIOBoard`, `ArcadeManager.GunMalfunctionCheck`,
`ArcadeManager.PushErrorPopup`, `NewInputManager.MouseControl`, `GunDetection.ExternCall`,
`PlayerPrefs.DeleteKey`, `LanguageLocalizer.SwitchLanguage`.

Een zoekactie over de hele IL van die assembly naar `Matrix` of `Dongle` levert **nul treffers**. De
plugin raakt de dongle-controle dus niet aan, en kan het verschil van 57 s naar 229 s niet via de
dongle verklaren. De controle staat in `MatrixManager.Awake` en loopt bij elke start; alleen de
uitkomst verandert er iets aan. Wat overblijft is variatie of een zijeffect van de venster- en
popuppatches op de coroutine-timing (`WaitForSeconds` loopt op speeltijd, en een spel dat niet
bijwerkt laat de 30 seconden niet verstrijken). Dat is niet uitgezocht.

Voor de beslissing maakt het niet uit: de patch-route is niet "de route die DemulShooter ook al
deed", maar de route die de twee ontbrekende antwoorden toevoegt.

## 9. Patch of DLL — de afweging

| | patch (route A) | DLL (route B) |
|---|---|---|
| werk | vier prefix-methoden | 113 exports, ABI-gelijk aan het origineel |
| versie-vast | ja: een game-update verandert de verduisterde methodenamen | nee: de API is van 2009 en verandert niet |
| dekt | alleen deze game, en alleen managed/Unity-games | elke game die de Matrix-API gebruikt, ook native |
| te bewijzen | het BepInEx-log | het BepInEx-log plus de ABI-controle |
| risico | een gemiste aanroep (zoals `Dongle_ReadSerNr`) valt pas op als de game sterft | moet in de juiste map landen (`Game_Data/Plugins`) |

**De regel die hieruit volgt: patch het kleinste beslispunt dat de game tegenhoudt, en emuleer alleen
de gegevens die de game echt verbruikt.** Voor Rabbids Hollywood zijn dat twee getallen, dus de patch
volstaat en is de DLL een alternatief. Voor de **HASP** ligt dat anders: daar leest de game echte
gegevens (twee geheugenimages van 112 bytes, `hasp/Halo Fireteam/dongle.json`), en dan is "altijd een
antwoord teruggeven" niet genoeg. Voor een **native** game (geen Unity) is "patchen" een bytepatch op
de executable, wat breekbaarder is dan een DLL; daar weegt de DLL zwaarder.

Beide routes staan daarom naast elkaar, met de drop-in als de bedoelde route en de patch als
terugval voor zolang de drop-in niet geleverd kan worden.

## 10. Status per game

| Game | Matrix-laag nodig? | Route | Status |
|---|---|---|---|
| **Rabbids Hollywood** | ja | drop-in (bedoeld) of patch (terugval) | **werkt**: de game blijft draaien (> 200 s gemeten) |
| Rampage | nee | geen | speelbaar; de game logt `MATRIX_NO_DONGLE_FOUND` en gaat door (findings/Rampage.md) |
| Tomb Raider | nee | geen | speelbaar; de blokkade was het USB-I/O-bord, niet de dongle (findings/TombRaider.md) |

Er is geen `dongle.json` en geen `[Dongle]`-sectie: de loader weet niets van dongles
(`docs/geen-dongle-in-de-loader.md`). Alles wat deze game nodig heeft staat in
`patches/RabbidsHollywood/`.

## 11. Bestanden

```
matrix/src/matrix.h              de API (113 namen) en de standaardwaarden
matrix/src/matrix.c              de laag zelf, plus de Java/JNI-stubs en het lezen van matrix.ini
matrix/src/matrix.def            de 113 exportnamen
matrix/src/selftest.c            zelftest zonder Windows (gcc, native)
matrix/src/controleer-abi.py     vergelijkt naam-tabel en `ret N` met het origineel
scripts/build-matrix.sh          bouwt matrix32.dll + matrix64.dll en draait beide controles
scripts/matrix.py                de drop-in controleren, plaatsen en de WINEDLLOVERRIDES teruggeven
patches/RabbidsHollywood/dropin/{dropin.ini,matrix64.dll,matrix/matrix.ini}
patches/RabbidsHollywood/README.md
plugins/unity/src/rabbids/Rabbids.cs     de patch (route A, terugval)
```

## 12. Open

* **De levering via de drop-in-motor werkt nog niet.** De view van Rabbids is een koppeling naar de
  NAS, dus `To = Game_Data/Plugins` wijst naar de NAS en de motor weigert terecht. Wie het bestand
  plaatst moet `Game_Data` (en `Plugins`) eerst lokaal maken; `scripts/matrix.py --install` doet dat
  al, en de motor wordt door een andere agent gerepareerd. Tot dan is de patch de werkende route.
* De laatste DLL-revisie (met het lezen van `matrix.ini` en de reparatie van de module-pad-resolutie)
  is in de game bevestigd: 92 s, en het log van de DLL laat zien dat hij `matrix.ini` naast zichzelf
  in de view vindt. Tijdens die proef stond `scripts/launch-direct.py` enige tijd stil door een
  bewerking van een andere agent (een `IndentationError`); de proef is daarna alsnog gedraaid.
* De 229 s van de DemulShooter-run is niet verklaard (zie §8). De plugin patcht de dongle-controle in
  elk geval niet.
* De savegame-versleuteling (`Dongle_EncryptData`/`Dongle_DecryptData`) is in onze DLL een no-op. Dat
  is in deze metingen niet uitgeoefend; als een game zijn opslag ermee valideert, moet dat opnieuw
  bekeken worden.
* Rampage en Tomb Raider zijn niet opnieuw gemeten; hun status komt uit de findings op de NAS.
