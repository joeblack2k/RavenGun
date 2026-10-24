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

### De patch (de route die uitgerold wordt)

`plugins/unity/src/rabbids/Rabbids.cs` beantwoordt dezelfde aanroepen in de managed wrapper. Die route
werkt en heeft geen bestand in de view nodig. Ze is sinds deze ronde ook de route die **daadwerkelijk
uitgerold wordt**: `play-unity.sh` → `aagames-setup.py` kopieert de plugin bij elke start naar
`views-aagames/RabbidsHollywood/BepInEx/plugins/` (gemeten: view-DLL ging van md5 `b4fe9d83…` naar
`29fd3ddc…` en daarna naar `e0eaa423…`, telkens identiek aan `build/unity-plugin/RabbidsHollywood/`).
De drop-in is het alternatief.

**LET OP — `RAVENGUN_MATRIX_DLL=1` is een blinde schakelaar.** Hij zet de Matrix-patches van de plugin
uit en vertrouwt erop dat de drop-in `matrix64.dll` het overneemt. Op de uitgeleverde route staat **geen**
drop-in: `views-aagames/<Game>/Game_Data/Plugins/matrix64.dll` is de originele van 93.184 B
(`c86908ab…`). Wie die variabele daar in de omgeving zet (bij het zoeken naar een dongle-probleem is dat
verleidelijk), haalt de dongle-antwoorden weg en dan sterft de game weer na ~70 s met
`**** DONGLE NOT FOUND` — zonder dat er iets in het log wijst naar de variabele. De plugin kan zich hier
niet tegen verdedigen: hij kan niet zien of de DLL die geladen is de onze is.

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

**Op de uitgeleverde route gemeten (2026-10-12).** Alle metingen hierboven komen van de
`launch-direct`-route (`views/RabbidsHollywood`) of van de drop-in. De route die wij uitrollen is
`play-halo.sh --profile RabbidsHollywood` → `play-unity.sh` → `views-aagames`. Die view droeg tot deze
ronde een plugin van vóór de Matrix-fix (32.768 B, md5 `b4fe9d83…`, mtime 2026-10-11 17:12) met **geen
enkele Matrix-regel**; in dat log staat `**** DONGLE NOT FOUND` en nul `Matrix:`-regels. Dat was de hele
"hij stopt na ongeveer een minuut" — niet een verkeerde donglewaarde, maar een plugin die het antwoord
niet bevatte.

Daarna, met de plugin die `aagames-setup.py` zelf in de view zet (view-DLL identiek aan de build), op
diezelfde uitgeleverde route: **288 s** in de attract-lus, zelf gestopt, met in het log

```
37: Patch: MatrixWrapper.Init_MatrixAPI
39: Patch: MatrixWrapper.Dongle_Count
40: Patch: MatrixWrapper.Dongle_ReadSerNr
97: Matrix: Init_MatrixAPI -> 0 (onze eigen dongle-laag)
98: Matrix: Dongle_Count(85) -> 2 (onze eigen dongle-laag)
99: Matrix: Dongle_ReadSerNr -> 69420 (onze eigen dongle-laag, vaste waarde)
100: [Info : Unity Log] **** DONGLE FOUND: 69420
```

0 x `DONGLE NOT FOUND`, 0 x `Application.Quit`, 0 x `IO BOARD ERROR`, en de daemon meldde op het
stopmoment `VERBONDEN met de game, 120 pakketten/s`. `Dongle_Find` is gepatcht maar wordt door het
opstartpad niet aangeroepen (er is geen `Matrix: Dongle_Find`-regel).

**Wat die meting niet zegt.** Ze liep headless (`gamescope --backend headless`) zodat het scherm van de
eigenaar vrij bleef, en in een headless gamescope komen de virtuele toetsen van `ravengunctl key` niet
aan (die gaan naar de compositor van de sessie, waar geen venster is). **Munt, start en vizier zijn dus
niet in deze meting uitgeoefend.** Op deze route zijn munt en start eerder wél gezien — log van
2026-10-11 17:13, nog met de oude plugin: munten liepen op tot 6, `key 1` gaf `speelt 1` en
`OnChooseWorldEnter` kwam in beeld. Dat is een andere run dan de 288 s, dus "start en leeft" en
"munt/start/vizier" staan nog niet in één meting naast elkaar. Een proef op de tv moet dat samenbrengen.
Volledige meting en afweging: `DONGLE-ONDERZOEK.md`.

### Let op: deze game gebruikt de dongle NIET voor zijn savegames (gecorrigeerd 2026-10-12)

Hier stond dat de savegame-versleuteling via `SBK.Matrix.MatrixManager` en de dongle loopt. **Dat is gemeten
en onjuist.** Wat er werkelijk is:

* Van de **zes Encrypt-wrappers** in `SBK.Matrix.MatrixManager` (`KPHIPOINDLP`, `BMNNMICCONF`, `EDNBLKCLMOI`,
  `NDIBJIHIEGD`, `NKLMBDBGNGM`, `MIBFIOJCFDO`) heeft **geen enkele een verwijzing** in de hele IL: ze zijn in
  deze build dode code. Daarom verschijnt `Matrix: Dongle_EncryptData -> 0` in geen enkele run. Van de zeven
  Decrypt-wrappers worden er zes aangeroepen, alleen vanuit `MatrixManager` zelf plus twee keer vanuit
  `NGBPMKLAEPB::MJDMKLODGIG`; de buffer gaat per blok van 8 byte als `ref int` naar binnen. Dat is geen
  savegame-pad.
* Het savegame-lees- en schrijfpad komt **niet** bij `MatrixManager` uit: `SaveGameManager::WriteData ->
  IJGPGJJKJAG::AJAKOIOBKPI + JEJBHONPKAL`, `ReadSaveGame -> IJGPGJJKJAG::JEJBHONPKAL`, bij falen
  `"Savegame corrupted : " + " will be resetted"` → `SendSaveGameData` → `InternalResetSaveGame`, met een
  MD5-controle (`"Corrupted savegame, bad MD5 match"`). `IJGPGJJKJAG` is een **andere klasse**: de
  IO-providerlaag, zonder sleutel.
* De versleuteling zit **niet in managed code**: een zoekactie over de hele assembly naar
  `Rijndael|Aes|CryptoServiceProvider|CreateDecryptor|CreateEncryptor|get_IV|set_IV` levert geen enkele
  crypto-aanroep op (alleen `MD5CryptoServiceProvider` voor de integriteitscontrole). De
  `Bad PKCS7 padding`-melding komt dus uit een **native** module; de enige kandidaat die de game daarvoor
  meelevert is **`Game_Data/Plugins/sxCore.dll` met `sxCoreSettings.txt`**.
* Wat de dongle er wél mee te maken heeft: `ServerRequestManager::GetDeviceID` geeft
  `MatrixManager.GetDongleSerialNumber().ToString()` terug als die > 0, anders `"0"`. Die waarde wordt in het
  savegame geschreven. Onze 69420 is dus de apparaat-ID — stabiel, maar hij is bij de dongle-fix veranderd
  van `"0"` naar `69420`, waardoor savegames van vóór die fix onleesbaar zijn.
* De twee-startproef (`rg_savegame.sh`, 2026-10-12 22:48-22:54) is gedraaid en is **mislukt**: run 2 las het
  bestand van run 1 en zette het alsnog terug (`PKCS7=4 corrupted=3 resetted=3`). De poort was schoon (view =
  build = `e0eaa423…`), beide runs leefden 170 s en de NAS bleef onaangeroerd — de meting is dus geldig, de
  conclusie is dat dit niet met de dongle op te lossen is.

Onze `MatrixEncrypt_Prefix`/`MatrixDecrypt_Prefix` blijven staan: ze zijn bewezen onschadelijk en dekken de
semantiek die de drop-in al had, maar ze kunnen dit probleem **per definitie niet oplossen**. Bewijs en
afweging: `DONGLE-ONDERZOEK.md` §10 en §11.


`Dongle_ReadData`, `Dongle_WriteData` en `Dongle_MemSize` komen in de hele assembly **niet** voor
(`grep-il` met dnlib): deze game leest dus geen dongle-geheugen, en daarom is er geen memory-lijst in
`dongle.json`.
