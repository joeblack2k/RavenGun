# De regio/PCB-laag

*Ronde 6 (2026-10-11). De indeling van de mappen staat in `docs/dropins.md`; dat contract wordt hier
niet herhaald.*

**Kort:** de zeven games uit deze opdracht hebben met drie verschillende dingen te maken die allemaal
"regio" heten. Alleen de Sega Ring-games (LGI, LGI3D, LGS) vragen echt een **regio/PCB-dongle**, en
voor die drie staat de waarde in `patches/<Game>/dongle.json`. Voor die drie is de waarde vandaag
**geen blokkade**: de TeknoParrot-kern die ze draait kent de regiokeys niet eens, de games vragen de
regio aan hardware die er niet is, en ze gaan verder met hun eigen `DefaultRegion`. De andere vier
hebben geen dongle maar een **bestandswaarde** (GaiaAttack4, HauntedMuseum, HauntedMuseumII) of een
**kerninstelling** (RainbowBombergirl); daar is geen `dongle.json` en dat is met bewijs hieronder
onderbouwd.

Er is **geen binary** gebouwd. De laag is gegevens plus `scripts/region.py`; zie "Wat er nog mist"
voor de enige reden waarom er ooit een binary bij zou komen.

---

## 1. Het mechanisme

### 1.1 De keten

```
GameProfiles/<P>.xml  ConfigValues: General/DongleRegion + General/PcbRegion (Dropdown, 5 waarden)
        |
        |  TeknoParrotUi schrijft het profiel naar teknoparrot.ini naast de game-exe
        v
teknoparrot.ini       [General] DongleRegion=JAPAN / PcbRegion=JAPAN
        |
        |  de kern leest .\teknoparrot.ini   <- string staat in de kern
        v
TeknoParrot.dll       ... en doet er voor deze games niets mee (zie 1.3)
        |
        v
game-exe              vraagt de regio aan de hardware:
                      amDongleGetRegion() -> dongle/keychip
                      fwAM readRegionEeprom / readRegionBuSram -> EEPROM en backup-SRAM
                      alles faalt -> de game gaat verder met DefaultRegion
```

De game leest **niet** zelf `teknoparrot.ini`: in `LGI_RingW_F_safe.exe`, `LGI.exe`,
`GameSampR_RingWide.exe`, `mxGetHwInfo.exe` en `game.exe` van Rainbow Bombergirl komt de tekst
`teknoparrot` nul keer voor. De waarde moet dus **in het proces** bij de game komen, en dat is de
enige route die overblijft: de kern antwoordt de aanroepen van de game.

### 1.2 Wat de game zelf doet (bewijs)

`strings` op de game-executables op de NAS:

| Game | Wat er in de executable staat |
|---|---|
| LGI (`LGI_RingW_F_safe.exe`) | `amDongleOpen`, `amDongleSetupKeychip`, `amDongleGetRegion`, `amDongleGetModelType`, `amDongleGetPlatformId`, `amDongleGetGameId`, `amDongleBillingGetKeychipId`, `amDongleSendAndRecvEx`; `Dongle::initialize() failed. but reason is unknown...`; `Keychip Not Found`; `fwAM::getSerialID SerialID[%s] KeychipID[%s]`; `fwAM> readRegionEeprom [%s] OK`, `fwAM> readRegionBuSram [%s] OK`, `setupRegionEeprom`, `setupRegionBuSram`; `DefaultRegion`; `Region is JPN.` / `Region is USA.` / `Region is EXP.` / `Region is CHN.`; `Unknown region!`; `amDongleGetRegion() is failed.` |
| LGI3D (`LGI.exe`) | hetzelfde als LGI (zelfde `fwAM`-bouwsel) |
| LGS (`GameSampR_RingWide.exe`) | `amDongle Ver.1.12 Build:Sep  4 2012`; `amDongleOpen`, `amDongleOpenEx`, `amDongleSetupKeychip`, `amDongleSendAndRecvEx`, `amDongleGetRegion`, `amDongleGetModelType`, `amDongleGetPlatformId`, `amDongleGetGameId`, `amDongleGetVersion`, `amDongleGetPicVersion`, `amDongleBillingGetKeychipId`; `keychip.appboot.region`, `keychip.appboot.gameid`, `keychip.appboot.platformid`; `Error Response KeychipError !!!` |

De `am`-familie (`amDongle`, `amEeprom`, `amSram`, `amBackup`, `amDipsw`, `amHm`, `amJvs`,
`amMaster`, `amPlatform`, `amInstall`, `amVideo`, `amOemstring`) is **statisch in de game gelinkt**:
geen van de exe's importeert een `am*.dll`. De drivers eronder worden via `SetupAPI` +
`DeviceIoControl` gezocht (`amDipswCreateDeviceFile`, `amEepromCreateDeviceFile`,
`SetupDiEnumDeviceInterfaces`, `DeviceIoControl Error(%ld)`).

De regiowaarde is dus **geen bestand en geen registerwaarde**: hij komt van een stuk hardware
(dongle/keychip, EEPROM, backup-SRAM) dat er in emulatie niet is.

### 1.3 Wat de kern doet (bewijs)

De TeknoParrot-kernen zijn gepackt; de methode om ze leesbaar te maken staat in
`docs/14_unpacking.md`. In de gedecrypteerde beelden (`cores/dump/`, op deze machine gemaakt met
`re/TpHost32.exe` + `re/dump_proc.py`) is te tellen welke tekst er wel en niet in staat:

| Tekst | 32-bits `TeknoParrot.dll` | 64-bits `TeknoParrot64.dll` |
|---|---|---|
| `teknoparrot.ini` | ja (3×) | ja |
| `FreePlay`, `Windowed`, `HideCursor`, `Enable Outputs`, `FFB Blaster` | ja | ja |
| `SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/10` | ja (4×) | – |
| `Region` | **nee (0×)** | ja |
| `DongleRegion`, `PcbRegion` | **nee** | **nee** |
| `amDongle` | **nee (0×)** | ja (`amDongleGetNetworkAddress: %d`) |
| `amEeprom`, `amSram`, `amJvs` | **nee** | – |
| `JAPAN`, `USA`, `JAP2N`, `EXPORT`, `CHINA` | **nee** | `JAPAN`, `EXPORT` |
| `System_getRegionCode`, `AllnetAuth_getRegionCode` | **nee** | ja |
| `System\Sega\SystemProperty\keychip` | **nee** | ja |
| aantal unieke strings | 365.120 | 425.756 |

De 32-bits kern leest `teknoparrot.ini` — hij kent `FreePlay`, `Windowed`, `HideCursor`,
`Enable Outputs` en `FFB Blaster` bij naam — maar kent `DongleRegion`, `PcbRegion` en `Region`
**niet**. Een sleutel die de kern niet bij naam kent, kan hij niet lezen. De regiokeys zijn voor deze
kern dus dood gewicht.

Waar de regiocode wél zit is de **64-bits** kern, en dat is een andere familie: `Region` als
profielveld, `System_getRegionCode`/`AllnetAuth_getRegionCode`, `amDongleGetNetworkAddress` en
`System\Sega\SystemProperty\keychip` horen bij de **ALLS/amdaemon**-emulatie (Sega's nieuwere
platform). Dat is dezelfde soort in-process namaak als de `amMaster`/`amLib`-aanroepen naar poort
40100 die al eerder gemeten zijn (`findings/LetsGoSafari.md`).

### 1.4 Waarom LGI, LGI3D en LGS tóch spelen

Omdat de regiocontrole **niet fataal** is. De game vraagt de dongle, krijgt niets, meldt
`amDongleGetRegion() is failed.` / `Keychip Not Found` / `Dongle::initialize() failed. but reason is
unknown...` en gaat verder met zijn eigen `DefaultRegion`. Dat is precies wat de meetresultaten laten
zien: LGI, LGI3D en LGS staan op `playable` en bij alle drie is de regio nooit aangeraakt. Het
TeknoParrot-team antwoordde op de vraag erover ook alleen "alleen vermelden, niet aanraken"
(`findings/LetsGoIsland.md`, `findings/LetsGoSafari.md`).

**Gevolg voor deze laag:** de waarden in `dongle.json` zijn de antwoorden die een echte
dongle-emulator moet geven, niet iets wat vandaag een game tegenhoudt. Daarom staat er
`"enforced": false` in, en daarom is er geen binary.

---

## 2. Drie dingen die "regio" heten

Ze hebben niets met elkaar te maken. `scripts/region.py` houdt ze uit elkaar en meldt per game welke
van toepassing is.

| Mechanisme | Wat het is | Games | Waar de waarde staat |
|---|---|---|---|
| `dongle` | de Sega Ring/Lindbergh regio- en printplaat-dongle; de game roept `amDongleGetRegion()` aan | LGI, LGI3D, LGS (en in de profielen ook HOTD4, Rambo, Golden Gun, Border Break, Sega InitialD, Guilty Gear, …) | `patches/<Game>/dongle.json` |
| `gun-ini` | de Taito Type X2-geweren lezen een `REGION`-regel uit hun **eigen ini** | GaiaAttack4 (`MINIGUN.INI`), HauntedMuseum (`MUSEUM.INI`), HauntedMuseumII (`HAUNTED2.INI`) | profielveld `Region`; `scripts/launch-direct.py` schrijft de regel (`taito_gun_ini`) |
| `core-region` | het profielveld `Region` van de 64-bits kern (WartranTroopers/amdaemon-route) | RainbowBombergirl (`Region = EXPORT`) | profielveld `Region` |

Het `gun-ini`-bewijs is direct: in de gamemap staat het bestand al met de regel erin —

```
Gaia Attack 4/MINIGUN.INI     REGION\t\tE   + CNFNAME .\OpenParrot\GA4
Haunted Museum/MUSEUM.ini     REGION\t\tE   + CNFNAME .\OpenParrot\HM
Haunted Museum II/haunted2.ini REGION\t\tE  + CNFNAME .\OpenParrot\HM2
```

— en de executable noemt die ini bij naam (`MINIGUN.INI` + `REGION` + `CNFNAME`; bij HauntedMuseumII
staan er zelfs `regione`, `regionc` en `regionj` in, de drie regiocode-paden). Het profielveld is
daar van het type `Text` met de hint `Valid options: A C E I`; `E` is de waarde die er nu staat en
betekent Export.

Het `core-region`-bewijs: `Region` staat als tekst in de 64-bits kern en niet in de 32-bits kern, en
Rainbow Bombergirls `game.exe` is PE32+ (x86-64) met een `Region`-veld met de keuzes `JAPAN|EXPORT`.
De game zelf noemt `teknoparrot` en `region` nergens, dus de waarde gaat via de kern (of de
BepInEx-plugin) naar binnen.

---

## 3. Per game

| Game | Mechanisme | Waarde | Dongle? | Status van de regio |
|---|---|---|---|---|
| **LGI** | `dongle` | regio `JAPAN`, printplaat `JAPAN` | ja → `patches/LGI/dongle.json` | geen blokkade (`enforced: false`); speelbaar |
| **LGI3D** | `dongle` | regio `USA`, printplaat `USA` | ja → `patches/LGI3D/dongle.json` | geen blokkade; speelbaar |
| **LGS** | `dongle` | regio `USA`, printplaat `USA` | ja → `patches/LGS/dongle.json` | geen blokkade; speelbaar |
| **GaiaAttack4** | `gun-ini` | `Region = E` (Export) | **nee** | geen blokkade; de regel staat al in `MINIGUN.INI`; geparkeerd om het zwarte beeld/de JVS-ketting, niet om de regio |
| **HauntedMuseum** | `gun-ini` | `Region = E` | **nee** | idem (`MUSEUM.ini`); geparkeerd om het zwarte beeld |
| **HauntedMuseumII** | `gun-ini` | `Region = E` | **nee** | idem (`haunted2.ini`); geparkeerd om het zwarte beeld |
| **RainbowBombergirl** | `core-region` | `Region = EXPORT` | **nee** | geen blokkade; de 64-bits kern handelt de regio af; geparkeerd om de aanraakbediening |

De `io_board` van de Ring-games is `837-14572` (`SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/10`);
die tekst staat in de 32-bits kern en in de JVS-route van de daemon (`packaging/jvs-boards.json`,
bord `LGI`). Het is dezelfde printplaat waar het profiel de regiowaarde aan hangt.

De vijf waarden die de profielen bieden zijn `JAPAN|USA|JAP2N|EXPORT|CHINA`. De games zelf kennen er
maar **vier** bij naam: `JPN`, `USA`, `EXP`, `CHN`. `JAP2N` heeft dus geen eigen naam in de game;
`dongle.json` noteert daarvoor `game_name: "JPN"` als **aanname**, niet als meting.

---

## 4. De laag zelf

Per game die een regio-dongle vraagt staat `patches/<Game>/dongle.json` (soort `kind: "region"`):

```json
{
  "kind": "region",
  "region": { "field": "DongleRegion", "name": "JAPAN", "game_name": "JPN" },
  "pcb":    { "field": "PcbRegion",    "name": "JAPAN", "game_name": "JPN" },
  "enforced": false
}
```

Daarnaast staan er velden in die het naspeuren makkelijk maken: `platform`, `profile`, `exe`,
`io_board`, `api` (welke `amDongle`-functies de game aanroept), `game_reads`, `fallback`, `evidence`
en `notes`. Onbekende velden negeert `scripts/region.py`.

`enforced` betekent: **true = de game start niet zonder dit antwoord.** Voor LGI, LGI3D en LGS staat
het op `false`, met de reden in `notes`.

### `scripts/region.py`

```
scripts/region.py [--game <Game>] [--json]
  zonder --game: alle games met een dongle.json (mappen die met _ beginnen blijven erbuiten)
  met --game:    die ene game, met of zonder dongle.json
  exit 0 = in orde, 1 = niet in orde, 2 = verkeerd gebruik
```

Wat het per game doet:

1. `patches/<Game>/dongle.json` lezen (gebruikerslaag `~/.local/share/ravengun/patches/<Game>/`
   gaat voor), en `game.ini` uit beide lagen samenvoegen (gebruikerslaag wint per sleutel).
2. De waarden toetsen aan de keuzelijst van het profiel (`GameProfiles/<P>.xml` /
   `UserProfiles/<P>.xml`) en aan `game.ini`. `dongle.json` is de bron van waarheid: wijkt `game.ini`
   af, dan is dat een fout.
3. De game-executable scannen op de merkers van §1.2, en zo bepalen welk mechanisme deze game
   gebruikt. Vraagt de game een dongle terwijl `dongle.json` ontbreekt, dan is dat een fout.
4. Melden of de kern van deze game de regio afhandelt (de tabel uit §1.3) en of de waarde is
   afgedwongen.

Voor de launcher: `DongleRegion` en `PcbRegion` zijn in de Ring-profielen van het type `Dropdown`,
dus de waarde hoort **als tekst** in `teknoparrot.ini` (`DongleRegion=JAPAN`). Alleen een veld van het
type `DropdownIndex` gaat als index de ini in.

---

## 5. Testbewijs

### 5.1 De zeven games, op de Deck

```
$ cd ~/projects/ravengun && python3 scripts/region.py --game LGI --json     # exit 0
  mechanisme: dongle      exe: .../LGI_RingW_F_safe.exe
  merkers   : amDongleGetRegion, amDongleOpen, amDongleSetupKeychip, amDongleGetModelType,
              amDongleGetPlatformId, amDongleGetGameId, amDongleBillingGetKeychipId,
              amDongleSendAndRecvEx, amDongle Ver., readRegionEeprom, readRegionBuSram,
              setupRegionEeprom, setupRegionBuSram, keychip.appboot.region, DefaultRegion,
              Dongle::initialize(), Keychip Not Found, Region is JPN./USA./EXP./CHN.
  veld DongleRegion  profiel=JAPAN  game.ini=JAPAN  opties=JAPAN,USA,JAP2N,EXPORT,CHINA
  veld PcbRegion     profiel=JAPAN  game.ini=JAPAN  opties=JAPAN,USA,JAP2N,EXPORT,CHINA
  kern      : TeknoParrot.dll (32-bits) | regio afgehandeld: nee
  enforced  : False       fouten: -
```

LGI3D en LGS geven hetzelfde beeld met `USA`; GaiaAttack4, HauntedMuseum en HauntedMuseumII melden
`gun-ini` met de juiste ini; RainbowBombergirl meldt `core-region` met `TeknoParrot64.dll` en
"regio afgehandeld: ja". Alle zeven geven exit 0.

### 5.2 De foutpaden

Tien proeven in een tijdelijke patches-map (`RAVENGUN_PATCHES_DIR`/`RAVENGUN_USER_PATCHES_DIR`), alle
tien zoals verwacht:

| Proef | Uitkomst |
|---|---|
| gezonde LGI | exit 0 |
| regio `MARS` (niet in de keuzelijst) | exit 1, drie meldingen (geen Sega-waarde, niet in de keuzelijst, wijkt af van game.ini) |
| `game.ini` zegt `USA`, `dongle.json` zegt `JAPAN` | exit 1 |
| `pcb.game_name` hoort niet bij de regiowaarde | exit 1 |
| game vraagt een dongle, geen `dongle.json` | exit 1 |
| `kind: "hasp"` in plaats van `region` | exit 1 |
| gebruikerslaag overschrijft de meegeleverde map | exit 0 |
| `JAP2N` als waarde | exit 0 |
| game zonder enig regiomechanisme (Halo) | exit 0 |

### 5.3 Wat de proeven op de Deck verder lieten zien

- `mxGetHwInfo.exe` (de platformcontrole die `game.bat` vóór de game start) draait onder Wine in een
  eigen prefix, drukt alleen zijn versiebanner af en eindigt met **exit 1**. In de `+file`- en
  `strace`-tracés opent het **geen enkel bestand** dat met een regio te maken heeft: het somt PCI-,
  USB- en HID-apparaten op via SetupAPI en probeert drivers te openen. De regio zit dus niet in een
  bestand.
- LGI zelf, 45 s in een view met `WINEDEBUG=+file,+setupapi,+reg` (94.783 regels): de game leest
  `data\Fraw\fwDataDB\fwDataDB_Application.txt` en enumereert HID-apparaten, maar opent **geen enkel
  apparaatpad** (`\\.\…`) voor een dongle en er komt **geen enkele keer "region"** in de trace voor.
  De run haalt de dongle-aanroep dus niet (de game stopt op exitcode 5 zonder de kern), maar hij laat
  wel zien dat er in de vroege start geen regiobestand wordt gelezen.

De ruwe tracés en de strings-dumps staan niet in de repo (ze zijn groot); de commando's staan in
`docs/research/region-mechanisme.md`, zodat iedereen ze opnieuw kan maken.

---

## 6. Wat er nog mist

1. **De numerieke codering van de regio.** De game kent de namen `JPN`, `USA`, `EXP`, `CHN`, maar
   welke waarde `amDongleGetRegion()` teruggeeft en hoe die in de EEPROM en het backup-SRAM staat, is
   niet vastgesteld. Zonder dat is een echte dongle-emulator niet te schrijven.
2. **Het dongle-apparaat.** Welk apparaat (`\\.\…` of welke SetupAPI-klasse) de `amDongle`-laag
   opent, is niet gevonden: de 45 s-run van LGI haalt die aanroep niet. Een volgende meting is een
   run met de kern erbij en `WINEDEBUG=+setupapi,+file` (of een drop-in die `CreateFileW` en
   `DeviceIoControl` logt), en dan zoeken naar het eerste apparaat dat de game opent en dat geen
   toetsenbord, muis of HID is.
3. **`JAP2N`.** De game heeft geen eigen naam voor deze profielwaarde. De aanname `JAP2N → JPN` is
   niet gemeten.
4. **De 64-bits regiocode.** De 64-bits kern heeft `Region` en `System_getRegionCode`, maar welke
   waarde bij `EXPORT` hoort en wat de kern ermee doet is niet uitgezocht; dat is de amdaemon-kant,
   niet de Ring-dongle.
5. **Een binary.** Zolang punt 1 en 2 open zijn, is er niets om te bouwen. Zodra de codering en het
   apparaat bekend zijn, is de plek volgens `docs/dropins.md` `patches/<Game>/dropin/` met een sectie
   in `dropin.ini` — bijvoorbeeld `[libravengun-region.so]` met `Preload = true` en
   `Env = RAVENGUN_REGION_GAME=LGI`. De waarden die die bibliotheek moet antwoorden staan dan al in
   `patches/<Game>/dongle.json`.

---

## 7. Naspeuren

```bash
# waar de game de regio opvraagt (op de NAS, alleen lezen)
strings -a -n 4 "/home/deck/mnt/spidernas_emulation/Teknoparrot/Let's Go Safari/GameSampR_RingWide.exe" \
  | grep -i -E 'amdongle|keychip|region'

# wat de kern wel en niet kent (op Windows; docs/14_unpacking.md)
python re\_rgn_str.py cores\dump\TpHost32_8296_691b0000_TeknoParrot_dll.bin
grep -c 'DongleRegion\|PcbRegion\|Region' cores\dump\TpHost32_8296_691b0000_TeknoParrot_dll.strings.txt

# de eigen laag
python3 scripts/region.py --game LGI --json
python3 scripts/region.py
```


---

## 8. Wat wel en niet per game is nagelopen

Deze laag is per game nagelopen voor de games hieronder. **De overige games zijn NIET een voor een
gecontroleerd** op een regio/printplaat-dongle; ze hebben geen `dongle.json` met `kind: region`, en
dat betekent "geen bewijs gevonden", niet "bewezen geen dongle". Wie de lijst compleet wil, moet per
game de `amDongle*`-aanroepen in de executable nalopen (zie §7).

**Een regio/printplaat-dongle, vastgelegd** (`patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json`,
`kind: region`, `enforced: false` omdat de 32-bits kern `DongleRegion`/`PcbRegion` niet kent en het
profiel de regio zelf zet):

| Game | Regio | Bron van de waarde |
|---|---|---|
| LGI | JAPAN | profiel `SegaJvsLetsGoIsland` |
| LGI3D | JAPAN | idem |
| LGS | JAPAN | profiel `LGS` |
| OG (Operation G.H.O.S.T.) | JAPAN | `GameProfiles/OG.xml` + `findings/data/target_hw_patch.json`, **niet** aan een draaiende game gemeten (`[Status] = untested`) |

**Meetbaar GEEN regio-dongle, met het bewijs waarom** — dit zijn de gevallen die het vaakst
verkeerd gaan, want ze zien er op het eerste gezicht hetzelfde uit:

| Game | Wat de executable wél vraagt | Bewijs |
|---|---|---|
| Medaru No Gunman | de **keychip** van de Lindbergh/Ring-familie (`amDongleOpen`, `keychip.appboot.region/gameid/platformid`, `keychip.billing.*`, `keychip.tracedata.*`, `Keychip Not Found.`, `Wrong Region.`) | `patches/MedaruNoGunman/README.md` |
| GhostBusters | geen dongle maar een **`pm`-map**: de map van de elf moet `pm` heten (`GAME PATH CHECK FAILED!... path=%s`) | `patches/GhostBusters/README.md` |
| NERF | geen HASP-image: de dongle wordt in **managed code** gevraagd (`UDongleControl`) en een plugin beantwoordt die | `patches/NERF/README.md` |

**Open.** De keychip van Medaru No Gunman is een eigen laag (facturatie, tracelading, SSD-bewijs) en
is niet gebouwd; de `amdaemon`-kant ervan staat in `docs/amdaemon.md`. En de vraag uit de opdracht —
"de regio-laag voor de ~30 Sega-titels" — is dus **deels** beantwoord: vier games vastgelegd, drie
games met bewijs uitgesloten, de rest niet nagelopen.
