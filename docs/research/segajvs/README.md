# Sega JVS-games zonder de gesloten TeknoParrot-kern

*2026-10-17. Doel van de eigenaar: elke game start zonder TeknoParrot (geen `TeknoParrot(64).dll`, geen bestand uit de TeknoParrot-runtime).
Dit stuk gaat over de zeven Sega-games die vandaag op de gesloten 32-bits kern draaien met invoer via JVS: LGI, LGI3D, Transformers
(Human Alliance), GG, OG, SDR en LGS. Alles is statisch uit de exe's, de OpenParrot-bron en de bestaande metingen gehaald: er is voor dit
stuk geen enkele game gestart. Per bewering staat erbij hoe zeker ze is: **[gemeten]** (eerdere run of tool-uitvoer), **[gelezen]**
(code of tekst in de bron/exe), **[TP-team]** (antwoord van het TeknoParrot-team, niet geverifieerd), **[afgeleid]**.*

## Het korte antwoord

De kern doet voor deze games drie dingen die ertoe doen; een daarvan is al van ons, een is alleen in de exe te zien en een is onbekend
tot er één meting is.

| # | Wat de gesloten kern doet | Heeft OpenParrot het? | Stand |
|---|---|---|---|
| 1 | `CreateFile("COM4")` omleiden naar de pipe `\\.\pipe\TeknoParrot_JVS` plus de comm-API's (`SetCommState`, `GetCommModemStatus`, ...) | **Ja**, `OpenParrot/src/Functions/Ring_amLib/amJvs.cpp`, voor elke game die geen Type X is **[gelezen]**; alle zeven exe's openen letterlijk `COM4` **[gemeten]** (scan hieronder) | gedekt door `Core = ravengun`; op onze kern antwoordt de JVS-pipe bij LGI **[gemeten]** (`patches/EIGEN-KERN.md`) |
| 2 | De AM Library van de game (amMaster, amDongle, amInstall, amStorage, amNetwork, ...) in-proces afhandelen, zodat er geen AM-daemon op `127.0.0.1:40100..40115` nodig is | **Nee** (`grep` op de bron: geen `amMaster`, `pcpa`, `40100`) **[gelezen]** | **blokkeert** (LGI en LGS gemeten); `rg-direct` speelt nu de daemon, zie "De AM-daemon van rg-direct" |
| 3 | Een onvoorwaardelijke `CreateWindowExW`-hook (laat `Windowed=1` crashen, daarom draait elke game in een Wine-bureaublad of gamescope) | n.v.t. | verdwijnt vanzelf met onze kern **[TP-team]**; geen vervanging nodig |

Regio/PCB (`DongleRegion`, `PcbRegion`) hoeft niets: de 32-bits kern kent die sleutels niet eens (`docs/research/region-mechanisme.md`,
hoofdstuk 2, 0 treffers in het ontsleutelde beeld) en elke `dongle.json` van deze games staat op `enforced: false` **[gemeten]**.

**Conclusie.** Alle zeven staan klaar om op onze eigen kern geprobeerd te worden, maar geen enkele is bewezen. Eerste TV-meting (LGI,
2026-10-17 13:05, alleen `Core = ravengun` plus `pcp-listen.py --greet --prompt-after`): de game startte, belde 3x `:40106`
(`keychip.appboot.systemflag=?&cache=0`) en 2x `:40100` (`mxmaster.foreground.getcount=2`), kreeg alleen een kale `>` en eindigde na ~10 s
met exitcode 5. Dus punt 2 blokkeert, ook bij LGI. Daarop is de antwoordkant gebouwd: `rg-direct` speelt de AM-daemon (zie "De
AM-daemon van rg-direct"). Antwoordformaat en waarden komen uit de AM Library in de exe zelf (gelezen in LGI; de zes broers hebben
dezelfde sleutelnamen), niet uit TeknoParrot.

## Wat er in de exe's staat (`tools/dev/segajvs-scan.py`)

Elke game heeft de volledige SEGA AM Library statisch in de exe. Het hulpmiddel (alleen lezen) levert per exe de versies, de poorten die
als constante in code staan, de letterlijke `COM4`/`\\.\mxjvs`/`127.0.0.1` en per functienaam een begin-schatting.

| Game (exe) | AM Library | amMaster / amDongle | libpcp | poorten in code | JVS |
|---|---|---|---|---|---|
| LGI (`LGI_RingW_F_safe.exe`) | oud (2010) | 1.03 / 1.06 | 1.02 | 40100, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| LGI3D (`LGI.exe`) | oud (dec 2010) | 1.06 / 1.08 | 1.02 | 40100, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| GG (`RingGunR_RingWide.exe`) | 1.07 (2010) | 1.03 / 1.06 | 1.02 | 40100, 40102-40104, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| OG (`gs2.exe`) | 1.08 (2011) | 1.07 / 1.09 | 1.02 | 40100, 40102, 40103, 40106-40108, 40115 | `COM4`, `\\.\mxjvs` |
| SDR (`Game.exe`) | 1.08 (2011) | 1.07 / 1.09 | 1.02 | 40100, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| Transformers (`TF_Gun_R_Ring_dumped.exe`) | 1.17 (2013) | 1.13 / 1.14 | 1.08 | 40100, 40102-40104, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| LGS (`GameSampR_RingWide.exe`) | 1.17 (2012) | 1.13 / 1.12 | 1.08 | 40100, 40102, 40103, 40106, 40107, 40114, 40115 | `COM4`, `\\.\mxjvs` |

Reproduceren: `python3 -I tools/dev/segajvs-scan.py <exe>` (`--json` voor machineleesbaar, `--names REGEX` voor functienamen). De
functiebegin-adressen zijn een schatting (eerste byte na twee of meer `int3`); de poortadressen zijn constanten in code, geen
disassemblage. Controle op LGS tegen de handmatige lezing van het TeknoParrot-team: `amMasterInit` op `0x45D840` met `0x9CA4` (40100)
klopt, `amDongleInit` ligt in de schatting op de verkeerde buur (`0x45F000` i.p.v. `0x45F390`): gebruik de adressen als aanwijzing voor
`objdump`, niet als bewijs.

Wat daaruit volgt: LGI, LGI3D en SDR hebben de kleinste oppervlakte (alleen master en dongle); GG, OG en Transformers vragen ook install
(40102/40103) en netwerk (40104); LGS heeft daarnaast opslag (40114/40115).

## De AM-daemon

**Wat de game vraagt [gelezen].** `amMasterOpen` (`0x45D9C0` in LGS) belt `127.0.0.1:40100` met een time-out van 60 s; `amMasterOpenEx` herhaalt
bij fout `-0xE` zonder teller. `amDongleSendAndRecvEx` (`0x461800`) roept `amDongleOpenEx` aan voor elke vraag (`keychip.appboot.region`,
`.gameid`, `.modeltype`, `.platformid`, `.networkaddr`, `.systemflag`, `keychip.version`, `keychip.billing.*`, ...). Een fout in de
init wordt alleen gelogd (de functie op `0x417e00` formatteert in een buffer: een logger, geen afbreking); een wachtlus in `OpenEx` is dat niet.

**Het protocol (libpcp) is tekst [gelezen].** Het verzoek is `commando=argument\r\n` met daarna `&sleutel=waarde\r\n` per extra veld,
hoogstens 256 bytes (`pcppSetSendPacket`/`pcppAddSendPacket`, formaten `%s=%s\r\n` en `&%s=%s\r\n`). De ontvanger tokeniseert op `=`, `&` en
CRLF (`pcppCheckStr`); woordtekens zijn letters, cijfers en `. _ - : @ % / \ # { }`. De **daemon eindigt zijn antwoord met een prompt
`>`** (`pcppCheckPrompt` zoekt dat teken in de ontvangen bytes en wacht erop met `pcppRecvPrompt`). Dat verklaart waarom de oude proef
(luisteraar die nulbytes en `PCP\0` stuurde) de game niet verder hielp: er kwam nooit een `>`. Wat nog niet gelezen is: de exacte opbouw
van een antwoord (welk veld het commando herhaalt, welk resultaatveld), per vraag de waarde-indeling, en wat install/storage/network
verwachten.

**Of het blokkeert [onbekend].**
* LGS: ja, zonder kern hangt de game voor COM4 op 40100 (`docs/decisions.md`, IO-brug stap 3) **[gemeten]**.
* LGI: onze kern kwam tot een antwoordende JVS-pipe na 25 s **[gemeten]**, maar `amJvsInitEx` staat in de init-volgorde (`0x401acc` in LGS) vóór
  elke AM-vraag, dus dat bewijst niet dat de AM-daemon niet nodig is.
* GG, OG, SDR, Transformers, LGI3D: niet gemeten.

Het TeknoParrot-team zegt dat de kern dit in-proces nabootst ("amNetwork emulator ... cannot listen on TCP", `TP_INSTANCE`, `AmAuthPort`)
**[TP-team]**; die tekst komt uit de 64-bits kern. In het ontsleutelde 32-bits beeld zijn `amDongle`/`amEeprom`/`amSram`/`amJvs` niet
te vinden (`region-mechanisme.md`), dus voor de RingWide-kern is niet vastgesteld *hoe* hij het doet.

## De AM-daemon van rg-direct

**Keuze: in `rg-direct`, niet in de daemon.** `rg-direct` is het hoofdproces van een run (hij host ook de JVS-pipe); de luisteraar leeft dus
precies zolang de game, kan een andere game niet storen, en het logboek staat bij dat van de JVS-pipe. De daemon blijft ongemoeid
(geen nieuwe poorten die blijven staan, geen herstart-toestand). Code: `src/ravengun/launch/amdaemon.{hpp,cpp}` (ontleden, antwoorden,
`Listener`), aangezet met `[Launch] AmDaemon = true`; antwoorden per sleutel in `[AmDaemon]` van `game.ini` (Dolphin-model: de standaard
in de code, de afwijking per game in de data). Staat een poort al bezet (bv. `pcp-listen.py`), dan meldt hij dat en gaat verder met de rest.

**Wat de game van een antwoord leest** (LGI: `pcppRecvResponse` 0x6F5150, `pcppCheckRecvMsg` 0x6F4E20, `amMasterCheckResponse` 0x4848B0,
`amDongleSetupKeychip` 0x48B2B0, `amDongleResponseCheck` 0x48C260, `amDongleGetXxx` 0x48B890-0x48BCB0) **[gelezen]**:

| Onderdeel | Regel |
|---|---|
| Opbouw | `sleutel=waarde[&sleutel=waarde]\r\n`; de game wacht eerst op `>` en stuurt dan; na het antwoord weer `>` (wij sturen beide in een write) |
| Eerste sleutel | moet de sleutel van het verzoek zijn, met een waarde (`pcpaGetKeyword(0)`); begint hij met `?` dan is het een fout |
| `code` | optioneel veld; afwezig = goed, `atoi(code)` 1..5 = fouten (-10 ... -6); wij sturen het nooit |
| `keychip.appboot.systemflag` | 2 hexcijfers (1 byte, hoofdletters); bit 0 zet een vlag die fwAM bewaart |
| `keychip.version` | 4 hexcijfers (2 bytes), als 16-bit woord (bytes omgedraaid) bewaard |
| `.gameid` / `.platformid` | tekst, hoogstens 4 / 3 tekens (`strcpy_s` met 5 / 4) |
| `.modeltype`, `.region`, `.dvdflag` | 2 hexcijfers; region is een bitveld: 01 JPN, 02 USA, 04 EXP, 08 CHN (de tabel in `fwRegion` 0x671C70; andere waarden = DefaultRegion) |
| `.networkaddr` | IPv4-tekst (`inet_addr`) |
| `.billing.keyid` / `.mainid` | tekst, hoogstens 16 / 11 tekens; `.playcount/.playlimit/.nearfull`: 8 hexcijfers |
| `mxmaster.foreground.getcount=N` | antwoord `mxmaster.foreground.getcount=N&count=C`: `atoi(waarde)` moet N zijn, `count` (strtoul) is het resultaat |

De volgorde van `amDongleSetupKeychip` is: systemflag, dan version, dan is de keychip "klaar"; daarna vraagt fwAM keyid, gameid, region,
modeltype, systemflag en platformid (`0x6F0D30`). Elke mislukte vraag geeft in het spel een regel `amDongleXxx() is failed.`

**Wat gegokt is** (de game vergelijkt het nergens, gelezen): `platformid` (AAM, voor RingEdge AAL), `modeltype` (01), `version` (0101), `keyid`/`mainid`
(nullen), billing-tellers, `systemflag` (00), `getcount.count` (1). Het spel-id is gelezen uit `MACtbl/*_Table.dat` of `gameid.txt`
(LGI SBUH, LGI3D SBWB, LGS SDAX, GG SBVE, Transformers SBYR; OG en SDR hebben er geen: onbekend). Als een gok de game laat afbreken staat dat
in het logboek als `amDongleXxx() is failed` of een volgend verzoek; pas dan de waarde aan in `[AmDaemon]`.

**Het logboek** (`direct-<Game>-*.log`): elke regel `AM-daemon :40106 <- <verzoek>  -> <antwoord>`; een sleutel die we niet kennen staat erin als
`[ONBEKEND: ...]` met een voorzichtig antwoord (`sleutel=0`); de eerste drie keer per sleutel volledig, daarna elke honderdste met `(#N)`;
aan het eind `AM-daemon: gestopt (N verzoeken, waarvan M onbekend)`. Meer verzoeken dan we kennen (install, storage, network, amMaster
`next/current/active`) verschijnen daar als eerste.

### Meting 2 (LGI, 2026-10-17 14:29): alles beantwoord, daarna exitcode 5

Het logboek `direct-LGI-20261017-142900.log` laat 10 verzoeken zien, 0 onbekend, in precies de volgorde van de code: `systemflag`, `version`
(`amDongleSetupKeychip`), dan `billing.keyid`, `gameid`, `region`, `modeltype`, `systemflag`, `platformid` (`0x6F0D30`, de dongle-init van fwAM), en
daarna 2x `mxmaster.foreground.getcount=2` op :40100 (de coordinator zag die niet, ze staan er wel: "meer verbindingen" kapt alleen de
verbindingsregels af). Beide kregen `getcount=2&count=1`, en dat is wat `amMasterCheckResponse` wil (state 9: `atoi(waarde)==2`, `count`
via `strtoul`, resultaat `count<=1`).

**Geen enkele waarde is afgewezen.** Dat volgt uit de code, niet uit gokken: een afgewezen waarde geeft in de AM Library alleen een
foutcode terug (`amDongleXxx() is failed` via de logger op 0x417E00, de init loopt door), en `ArcadeManager::initialize` (0x6EF7F0) vraagt na
de dongle nog de hardwaremonitor (`6F0F60`) en `amMasterInit`/`getcount` (`483FA0`, `4844F0`). Dat getcount kwam, bewijst dat de dongle-init
geslaagd terugkwam.

**Er is geen `exit(5)`-pad in de exe.** WinMain (0x8ABF80) eindigt met `xor eax,eax`; van de 14 `exit`-aanroepen gebruiken 13 code 1 en de laatste (CRT) geeft de waarde van WinMain (0), een
stapelcontrole geeft 0xC0000409 (laatste byte 9). OpenParrotLoader geeft in zijn standaardmodus (`TP_REMOTETHREAD` uit) de exitcode van de
HOOFDTHREAD van de game terug (`GetExitCodeThread`), en Wine kort een uitzonderingscode af op de laatste byte: **5 = 0xC0000005, een
toegangsfout in de game of in onze kern, ~2 s na het laatste AM-antwoord.** Er is dan ook geen "JVS-pipe: game verbonden": de game
kwam niet bij zijn JVS-thread (`JvsManager::initialize` 0x6EE520 -> `amJvsInitEx` 0x4856E0, direct na `ArcadeManager::initialize`).

**Waar de toegangsfout vermoedelijk zit** (statisch niet te bewijzen; de game moet hem laten zien): de AM Library opent op een echte kast
stuurprogramma's die Wine niet heeft: `\\.\mxsram` (`amSramInit` 0x4879F0: `CreateFileW` + `DeviceIoControl` 0x9C406000 voor de
versie, 0x70000 voor grootte en sectorgrootte), `\\.\mxsuperio` (hardwaremonitor), `\\.\columba` en via SetupDi een EEPROM
(`AM_PCA9535_MUTEX`) en de dipschakelaars (smbus). Elke afzonderlijke mislukking wordt netjes teruggegeven (bit in het masker van
`ArcadeManager::initialize`), maar de spelcode erna (`6E...`, `fwAM` BuSram/EEPROM-regio, foutschermen) kan een nul-wijzer gebruiken. De
gesloten kern moet dit ook nabootsen; OpenParrot heeft er niets van. Dat is de volgende laag, in dezelfde vorm als de AM-daemon: data
(welk apparaat, welke ioctl, welk antwoord) en een kleine hook in onze kern.

**Het adres vinden (de volgende meting).** In `game.ini` van LGI in de gebruikerslaag:

```ini
[Env]
WINEDEBUG = -all,+seh
```

Het logboek krijgt dan `dispatch_exception code=c0000005 ... addr=...` en daarvoor de laatste Windows-aanroepen. Het adres naar de
functie: `python3 -I tools/dev/segajvs-scan.py <exe> --where 0x<adres>` (functiebegin, dichtstbijzijnde am*-functie, de tekenreeksen die de
functie gebruikt). Staat het adres niet in `LGI_RingW_F_safe.exe` maar in `OpenParrot.dll` of een Wine-DLL, dan is het onze kern of Wine.

## Kastapparaten: rg-amhw (de crash op 0x6F0515 en wat erna komt)

*Meting 3 (LGI, 2026-10-17 17:32, `WINEDEBUG=-all,+seh`): toegangsfout op 0x006F0515 (`mov eax,[eax+8]`, eax = 0) in `teaArcade2`-initialize.
De nul is het resultaat van de tabelkeuze op het BORDTYPE (0x489240), en dat komt niet uit de AM-daemon maar uit de DMI-tabel van het BIOS.*

**Wat de game doet [gelezen, LGI]**

| Stap | Waar | Wat |
|---|---|---|
| 1 | `amOemstringGetOemstring` 0x48F6E0 | `CreateFileW("\\\\.\\columba")`, dan `DeviceIoControl(0x9C406104)` met in = {adres, 0, 1, lengte} en uit = 0x10000 bytes: een lees-fysiek-geheugen-opdracht. Eerst adres 0xF0000 (de BIOS-ruimte): hij zoekt `_DMI_` (index < 0xFFF8), leest daar de tabellengte (+6) en het tabeladres (+8). Dan dezelfde opdracht op dat adres. Beide keren moet er EXACT 0x10000 bytes terugkomen. |
| 2 | 0x48F660 (per tekenreeks) | structuur type 0: tekenreeks 2 = BIOS-versie, 3 = datum (hoogstens 10 tekens); type 1: tekenreeks 1 = **fabrikant**; type 11 (OEM-strings): tekenreeks 3 = **platform-id**. |
| 3 | `amPlatformGetPlatformId` 0x489010 | is de OEM-string `AAL` of `AAM`, dan is dat het platform-id; anders leidt hij het af uit de fabrikant (NEC = AAL, Supermicro/Advantech = AAM). Onbekend = fout. |
| 4 | `amPlatformGetBoardType` 0x488DF0 | AAL + NEC = 0; AAM + Supermicro = 1; AAM + Advantech = 2; **al het andere geeft een type waar de tabel van de game geen regel voor heeft**. LGI3D en Transformers kennen ook `AAL2` (RingEdge 2) = 3. |
| 5 | `amPlatform...` 0x489240 | kiest met het bordtype de tabel voor de backup-opslag (EEPROM- en SRAM-gebieden; type 0 heeft 2 MB SRAM, 1 en 2 hebben 256 KB) en geeft 0 terug bij een onbekend type: de nulwijzer. |

Onder Wine bestaat `\\.\columba` niet, dus stap 1 faalt en het bordtype is onbekend. De platformkeuze bij het `keychip.appboot.platformid` van de
AM-daemon heeft hier niets mee te maken: de game leest die waarde nergens terug (in LGI: niets leest de platformid-plaats 0xC0D023 van de dongle-structuur;
de andere zes niet nagelopen). Wel staat de standaard nu op `AAM` en op `AAL` voor RingEdge, zodat de twee verhalen niet tegenspreken.

**Platform per game [gelezen: de exe's]** Alle zeven hebben dezelfde tekenreeksen (`AAL`, `NEC`, `AAM`, `Supermicro`, `Advantech`); LGI3D en
Transformers hebben daarnaast `AAL2`. Wat een game zelf over zijn kast zegt: `mxGetHwInfo.ini` noemt RingWide (grafische chip 1002:94cb,
256 KB SRAM) voor LGS en SDR, `app.ini` van LGI idem, Transformers RingEdge 2 (10de:1241, 2 MB SRAM). De game controleert het platform
niet tegen zijn eigen soort; elke geldige combinatie start. Gekozen:

| Game | OEM (platform-id) | Fabrikant | Bordtype | Reden |
|---|---|---|---|---|
| LGI, LGS, GG, OG, SDR | AAM | Supermicro | 1 | RingWide, 256 KB SRAM |
| LGI3D | AAL | NEC | 0 | RingEdge (metadata); mislukt dit, probeer AAM/Supermicro |
| Transformers | AAL2 | NEC | 3 | RingEdge 2, 2 MB SRAM; de nieuwere AM Library kent type 3 (0x69F5D0 heeft vier regels). NEC is nodig omdat `amPlatformGetPlatformId` er `AAL` uit afleidt |

**De drop-in `rg-amhw.dll`** (`tools/rg-amhw/`, bouwen met `tools/dev/build-amhw.sh`, 292 KB, statisch gelinkt, MinGW i686): zoals `rg-mfcam` zet
hij invoeren in de importtabel van de HOOFDmodule om (`CreateFileW/A`, `DeviceIoControl`, `CloseHandle`, `SetFilePointer`, `ReadFile`,
`WriteFile` en de comm-aanroepen); in kernel32 zelf wordt niets gepatcht, dus geen conflict met de MinGW-haken van onze OpenParrot-kern. Hij wordt met
`Preload = true` door de OpenParrot-lader geladen (`TP_PRELOAD_DLL`, na de kern en voor de game zijn eerste regel draait).

* `\\.\columba`: de DMI-tabel uit `amhw_dmi.h` (BIOS, systeem, OEM-strings, einde; SMBIOS 2.4 met geldige controlesommen), met de fabrikant en het
  platform-id uit `RG_AMHW_VENDOR` en `RG_AMHW_OEM`.
* `\\.\mxsram`: versie (`0x9C406000`, laag woord 1), geometrie (`0x70000`: 2 MB, sectoren van 0x200) en gewoon `SetFilePointer`/`ReadFile`/`WriteFile`,
  achter `mxsram.bin` in de map van de DLL (de view; nooit de NAS) zodat scores en instellingen een herstart overleven. Niet nodig om de crash te
  vermijden, wel voor `amBackupRead` (de regio uit het backup-SRAM, de testmodus, hoge scores).
* `\\.\mxjvs`: de JVS-lijn naar de pipe van de brug (zie Meting 4).
* Alles wat de game verder met `\\.\...` probeert (en de eerste 20 `SetupDiGetClassDevsA`-aanroepen) wordt GELOGD in `rg-amhw.log` en onveranderd
  doorgegeven: dat logboek is de lijst van wat er nog ontbreekt.

**Wat er nog niet is, en of het nodig is [gelezen, niet gemeten]**

| Apparaat | Waarvoor | Zonder | Nodig om voorbij de init te komen? |
|---|---|---|---|
| `\\.\mxsuperio` | hardwaremonitor (`amHmInit` 0x48D970: LPC-chip met bank-registers, temperaturen, fans) | `6F0F60` geeft 1, `ArcadeManager::initialize` zet bit 0x20 en logt "hardware monitor setup failed" | Nee voor de crash; waarschijnlijk wel een foutcode in beeld. Een LPC-chip nabootsen is werk van een dag; eerst kijken of het nodig is. |
| SMBus-driver via SetupDi (GUID-klasse, mutex `AM_PCA9535_MUTEX`), ioctl `0x9C406008` versie, `0x9C40200C` blokken | EEPROM (`amEeprom*`, de eerste twee backup-gebieden) en de dipschakelaars (PCA9535, `amDipsw*`, asynchroon met `0x9C402004`) | `amEepromInit`/`amDipswInit` falen (bits 1 en 8); `readRegionEeprom` meldt ERR en de game valt terug op `DefaultRegion` | Nee voor de crash. Voor het testmenu en de dipschakelaars wel. De structuur van de blokopdracht is al gelezen (0x27 bytes: status/cmd 9 = lezen, 8 = schrijven, slave +2, register +4, lengte +6, data +7). |
| `\\.\mxjvs` | JVS-stuurprogramma | JVS start niet; `c0c88c` (de knooppunten) blijft nul en `initCredit` stort in (0x6EDF7E) | **Ja, en nu nagebootst**: zie "Meting 4" hieronder. Eerder stond hier ten onrechte dat de game op `COM4` terugvalt. |

**Gemeten zonder game (Wine, wegwerp-prefix, `tools/dev/test-amhw-wine.sh`):** de DLL laadt, zet de importtabel om, geeft een handvat voor columba, de
twee leesopdrachten leveren een tabel op waaruit dezelfde code als in de game `Supermicro`/`AAM`/type 1 haalt, mxsram doet versie, geometrie, schrijven,
lezen en bestandsopslag, en een gewoon bestand loopt ongemoeid door de haken (alle 20 controles van de proef). De game zelf is niet gestart.

### Meting 4 (LGI, 2026-10-17 18:01): `\\.\mxjvs` en de toegangsfout op 0x6EDF7E

*De game praat JVS NIET via COM4. `JvsManager::initialize` (0x6EE520) wordt met argument 1 aangeroepen (0x674987) en kiest daarmee de STUURPROGRAMMA-modus
(`amJvsInitEx(2)`, 0x6EE56C); de COM-modus (`COM4`, 0x485767) is voor argument ongelijk aan 1 en wordt hier nooit gebruikt.*

**Waar 0x6EDF7E zit [gelezen].** `JvsManager::initCredit` (0x6EDE80). De regio-tak (`unknown region` bij een leeg SRAM, maar dat is hier niet de oorzaak: dat
logt alleen) loopt door naar `mov eax,[0xC0C88C]` en `mov eax,[eax]`: `0xC0C88C` is de tabel met JVS-knooppunten die `JvsManager::initialize` vult (0x6EE6D1) nadat
`amJvsInitEx` slaagde en `amJvsGetNodes` een aantal gaf. Mislukt de open van `\\.\mxjvs`, dan blijft de tabel nul terwijl het knooppuntaantal (`0xC0C888`) op
zijn beginwaarde staat; de crash is dus het gevolg van de mislukte open, niet van mxsram, mxsuperio of de SMBus.

**Het stuurprogramma [gelezen, 0x490B30 / 0x490780 / 0x4908A0 / 0x4909B1].** Het is een seriele lijn met een eigen verkeersopdracht:
* `CreateFileA("\\\\.\\mxjvs", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED)`, dan `GetCommState`, `SetCommState` (115200 8N1),
  `SetCommTimeouts`, `SetupComm(0x400,0x400)`, `PurgeComm(0xF)` op dat handvat.
* Verkeer: een enkele `DeviceIoControl(h, 0x9C402000, in, inLen, out, 0x100, &n, &overlapped)`. `in` zijn de JVS-bytes die de bovenlaag bouwt: het volledige kader
  (`E0`, knoop, aantal, gegevens, som, met `D0`-escapes), precies wat de COM-modus naar de lijn schrijft. `out` krijgt het antwoordkader (ook met `E0`; de
  COM-modus ontleedt hetzelfde in 0x490110). Het aantal bytes leest de game uit `OVERLAPPED.InternalHigh` nadat de gebeurtenis van de OVERLAPPED gezet is.
  Een globale reset (knoop FF, opdracht F0) krijgt geen antwoord.
* Sense-lijn: `GetCommModemStatus`; zonder adres alleen CTS (0x10), na de adrestoekenning CTS+DSR (0x30), net als OpenParrot's amJvs.cpp.

**Nagebootst in `rg-amhw.dll`** (`amhw_jvs.h` voor de kaderlogica, `jvs_transfer` in `rg_amhw.cpp`): `\\.\mxjvs` wordt de named pipe `\\.\pipe\TeknoParrot_JVS`
van onze brug (rg-jvsbridge, wacht tot 5 s tot hij er is; `RG_AMHW_JVS_PIPE` verlegt hem). De comm-aanroepen op dat handvat slagen, de sense-lijn komt uit
byte 0 van `TeknoParrot_JvsState`, en `0x9C402000` schrijft de bytes naar de pipe, leest het eerste complete kader terug (ruis ervoor wordt overgeslagen, `D0`-escapes
tellen als een byte, hoogstens 250 ms) en voltooit de OVERLAPPED meteen (`InternalHigh` = aantal bytes, gebeurtenis gezet, TRUE). Een globale reset wacht niet.
Dezelfde bytes dus als OpenParrot voor COM4 naar de pipe stuurt; de brug en `rg-direct` zijn ongewijzigd. In `rg-amhw.log` staan de eerste zes transacties
met hun bytes (`mxjvs: #1 [E0 FF 03 F1 01 F4] -> antwoord [E0 00 03 01 01 05]`), daarna elke 2000e, en elke 200e keer zonder antwoord.

**Onzeker, eerlijk:** dat het antwoordkader in de uitvoer van het echte stuurprogramma EXACT de lijnbytes zijn (met `E0`) volgt uit het feit dat de bovenlaag in
beide modi dezelfde ontleding gebruikt; gemeten aan een echt stuurprogramma is het niet. Staat er in `rg-amhw.log` "geen antwoord binnen 250 ms" terwijl de brug wel
verkeer ziet, dan is dat de eerste plek om te kijken.

**mxsuperio en SMBus:** nog niet nodig gebleken voor deze crash. Pas meten als `JVS-pipe: game verbonden` er is: de volgende log laat zien of er foutbits zichtbaar worden.

### Meting 5 (TV proef 8, 2026-10-17 18:05-18:13): vier foutschermen en twee stops, statisch ontleed

Alle zeven draaiden met mxjvs 9c67e68 (eerste rg-amhw). Hieronder per game wat de exe doet [gelezen] en wat de aanpassing is. Geen game gestart.

| Game | Gezien | Oorzaak [gelezen] | Aanpassing |
|---|---|---|---|
| LGS | Error 0949 Keychip Not Found | 949 = 0x3B5 uit fwAM; de keychip-vlag wordt pas gezet nadat `amDongleInit` de `<id>_Table.dat` las (`amDongleSetAuthConfig`). Op de NAS staat geen `SDAX_Table.dat`. Daarnaast leest de nieuwere AM Library de BIOS-ruimte in stukken van 0x8010 (0x474040/0x4741D0) en weigert een columba-antwoord waarvan `BytesReturned` niet gelijk is aan de buffer | `rg-amhw`: columba antwoordt met elke buffergrootte (tot 1 MB) en `BytesReturned` = buffer; een ontbrekende `_Table.dat` krijgt een vervanging |
| SDR | ERROR 0949 KEYCHIP NOT FOUND | Zelfde: `SBXM_Table.dat` (in de exe op 0x38B3B0) ontbreekt | Vervangende tabel; gameid `SBXM` in de overlay |
| OG | ERROR 3005 AMLIB INITIALZE FAILED | 3005 is het fatale pad van `AMLibContext::Init` (0x4AC2B0, `push 6; call 0x5106C0`): hij stopt bij de eerste mislukte stap. Eerste kandidaat is de dongle (`Dongle::Info` 0x4ACF00 -> `amDongleSetAuthConfig` 0x58CC90): `SBWA_Table.dat` ontbreekt. Daarna nog te verwachten: SMBus-EEPROM, hardwaremonitor (`\\.\mxsuperio`) en install/storage | Vervangende tabel; gameid `SBWA`. Of OG daarna verder komt is zonder start niet vast te stellen; zo niet, dan staat de volgende stap in `rg-amhw.log` als `niet nagebootst` |
| Transformers | Windows-dialoog "Runtime Error! R6002 floating point support not loaded" | `TF_Gun_R_Ring_dumped.exe` heeft `.rdata` met kenmerken 0xC0000040 (lezen+SCHRIJVEN). De VC-opstartcode (`_cinit`) roept `_FPinit` alleen aan als `_IsNonwritableInCurrentImage` zegt dat het pointerveld in een NIET-schrijfbare sectie staat; hier dus niet. Onder de TP-kern gaat dit goed (hypothese: de TP-lader past de kenmerken aan), de generieke route niet. Ook heeft de gedumpte exe geen namentabel in de importtabel | `rg-amhw` zet vóór de CRT-start de schrijfbit van `.rdata` in de kop in het geheugen uit (`kop: .rdata stond op schrijfbaar ...` in de log); `patch_iat` valt terug op vergelijken met het adres van GetProcAddress |
| LGI3D | exitcode 5 (0xC0000005) < 90 s | Zelfde columba-bufferweigering als LGS (RingEdge, `AAL`/NEC), met een nulwijzer als gevolg bij het bordtype | Columba-fix; komt er daarna weer een toegangsfout, dan geeft `+seh` het adres |
| GG | exitcode 0 < 90 s | Een nette stop: `SBVE_Table.dat` (pad `..\fs\table\SBVE_Table.dat` t.o.v. de exe) ontbreekt, de init meldt dat en de game sluit af zonder scherm | Vervangende tabel; de DLL maakt hem in `rg-amhw-tables/` naast zichzelf |

**De tabel.** De games lezen er blokken uit (aantal gecontroleerd, geen som over de inhoud). Een bestand van 1 190 000 bytes (de grootte van de echte tabellen van SBUH,
SBWB, SBYR) met vaste pseudo-willekeurige bytes (zaad = FNV van de bestandsnaam) volstaat voor de leesstap [gelezen]; of de game de inhoud later nog beoordeelt, is NIET
bevestigd. De DLL schrijft hem alleen naast zichzelf (in de view), nooit naar de NAS, en alleen als het opgegeven pad niet bestaat en het een leesopening is.

**Install en storage (poorten 40102/3 en 40114/5).** De nieuwere AM Library (LGS, GG, OG, Transformers) gebruikt daar `request=<naam>&...` en verwacht
`response=<naam>&result=success` terug (0x466CA0 install, 0x46C850 storage, resultaatcontrole 0x465520 / 0x46B810). De AM-daemon van rg-direct beantwoordt nu elk
`request=<naam>` zo; extra velden per verzoek kunnen in `[AmDaemon]` als `response.<naam> = k=v&k2=v2` (alleen letters/cijfers en `. _ - : @ % / # { } \ & =`). Welke
velden een verzoek nodig heeft is nog niet gemeten: staat er in het logboek van rg-direct een `request=<naam>` met de melding "result=success", maar loopt de game
daarna vast, dan is dat de eerstvolgende plek om te kijken.

**Onzeker:** (1) of de gedumpte Transformers-exe na R6002 nog andere dumpresten heeft; (2) of OG alleen op de tabel strandt of ook op EEPROM/hardwaremonitor;
(3) de oorzaak van de GG-stop is uit de padstring en de volgorde in de init afgeleid, niet uit een meting van die run.

### Meting 6 (LGI, TV proef 9, 2026-10-17 18:33): mxjvs werkt, daarna "Error 0949 Keychip Not Found" door een fout keychip-antwoord

mxjvs werkt (identificatie `SEGA CORPORATION;I/O ...`, ~60 polls/s met antwoord), geen crash. De AM-daemon zag twee onbekende sleutels op :40106 en stuurde de
waarde terug: `keychip.ds.compute=E0EC70DDDC7988&page=1` en `keychip.ssd.proof=CCF8FFBACAC583ABBFCDF3010A45C039`.

**Waar 0949 gezet wordt [gelezen, LGI_RingW_F_safe.exe].** `0x6719B0` (fwAM.cpp) vertaalt een code naar een tekst: `0x3B5` = "Keychip Not Found" (949 decimaal). De code wordt gezet
in `0x671AD0` op `0x671AE8`: `call 0x6EF4C0; test al,al; jne ok; mov [esp+0x10],0x3B5`. `0x6EF4C0` leest byte `0xC0D02A` = `[keychip-object + 0x1E]`. Dat byte wordt door
`0x6F0C70` / `0x6F0D30` gezet op `(0x48B270() == 1)`, en `0x48B270` is `[0xC2D664] != 0 && [0xC2D668] != 0`. Dus: de keychip-vlag is waar zolang amDongle denkt dat de keychip er
is (`C2D664`) EN dat hij echt is (`C2D668`). Beide worden 1 aan het eind van `amDongleSetupKeychip` (`0x48B73F`/`0x48B745`, na systemflag, version, gameid, region, ...). `C2D668` gaat
terug op 0 bij: `Verify MAC NG!!!` (`0x48B018`, ds.compute fout), `Verify SSD NG!!!` (`0x48B073`, ssd.proof fout), `PCP Connect Time Out Error` (`0x48BFEE`, geen antwoord) en
een antwoordcode -12 `Error Response KeychipError` (`0x48C327`). In deze run kwam geen van de laatste twee voor (de AM-daemon antwoordde op alles): het is dus de ssd.proof-controle.

**Wat de game met het antwoord doet [gelezen].**
* Antwoordgrammatica (`amDongleResponseCheck`, `0x48C2F4`): de eerste sleutel moet gelijk zijn aan de gevraagde (`keychip.ds.compute` / `keychip.ssd.proof`), anders -5; de waarde
  is hex (`0x48D4C0`: hoofd- of kleine letters), niet leeg; `code=` mag ontbreken. Lengte komt uit `strlen(waarde)/2`.
* **Hij toetst het antwoord inhoudelijk, maar tegen een waarde die in zijn eigen bestand staat.** `amDongleUpdate` (`0x48AFA4`..`0x48B085`) vergelijkt het ontvangen antwoord dword voor
  dword met een verwacht antwoord dat bij de uitdaging in het geheugen staat (`C2DBF7` voor ds, 20 bytes; `C2DBCC` voor ssd, 16 bytes). Beide komen uit `<spel-id>_Table.dat`
  (LGI: `MACtbl/SBUH_Table.dat`, 1 190 000 bytes), geladen door `0x48D670` (ds) en `0x48D810` (ssd): 10000 records van 87 bytes (uitdaging 7 + 4 pagina's van 20) en daarna 10000 records
  van 32 bytes (uitdaging 16 + antwoord 16); 100 willekeurige records gaan naar het geheugen en worden om de beurt gebruikt. Het verwachte antwoord is het bestandsantwoord met een vaste
  byteverwisseling (ds: `uit[j] = bestand[{15,5,13,14,10,1,2,11,16,7,4,18,12,6,3,0,17,8,19,9}[j]]`, ssd: `{6,8,4,12,7,13,1,10,2,3,11,14,15,0,5,9}`); de uitdaging gaat ruw mee. Dezelfde
  verwisselingen staan in LGI3D, LGS, GG, OG, SDR en TF (machinaal uit de code gehaald).
* **Dus geen geheime sleutel nodig.** De echte keychip rekent, maar de game bevat alle verwachte antwoorden vooraf: een AM-daemon die dezelfde tabel leest antwoordt exact wat de game verwacht.
  Dat is de oplossing: `keychip.ssd.proof=CCF8FF...` staat in de echte `SBUH_Table.dat` op byte 1187664 (= 870000 + 9927*32), dus een gewone verifieerbare uitdaging; wij stuurden haar terug
  in plaats van het antwoord, daardoor `Verify SSD NG!!!`.
* **Uitzondering: 1 op 10** (`rand() % 10 == 0`, `0x48B0C2`/`0x48B14C`) stuurt de game een willekeurige uitdaging en schakelt de vergelijking uit (`C2D678 = 1`). `E0EC70DDDC7988` staat
  nergens in de tabel: dat was zo'n geval en zou dus ook met de echo goed zijn geweest.
* **De vaste uitdaging** `2CFECBC71CF1E4` (alleen als bit 0 van `keychip.appboot.systemflag` gezet is, `C2D66C`) heeft vier vaste antwoorden in de exe (LGI `0x9506F8`), in alle zeven gelijk.

**Wat nu gedaan wordt** (`tools/rg-amhw/amhw_table.h`, `src/ravengun/launch/amdaemon.*`): de AM-daemon antwoordt op `keychip.ds.compute` en `keychip.ssd.proof` uit de tabel. Hij zoekt
`<spel-id>_Table.dat` (spel-id uit `keychip.appboot.gameid`) in `MACtbl/`, `fs/table/`, `rg-amhw-tables/` en `exe/rg-amhw-tables/` onder de werkmap, de exe-map en de map erboven, of in het pad uit
`[AmDaemon] auth.table`. Staat hij nergens (LGS, GG, OG, SDR hebben op de NAS geen tabel), dan genereert hij byte voor byte dezelfde vervangende tabel als `rg-amhw.dll` aan de game geeft
(`fake_table`, gecontroleerd in de Wine-proef). Een uitdaging die niet in de tabel staat (de willekeurige) krijgt nullen van de juiste lengte.

**Gegokt of niet gemeten:** (1) de verwisselingen zijn uit de code gelezen en in zeven exe's gelijk gevonden, maar nooit tegen een echte keychip gecontroleerd; klopt er één niet, dan zie je
`Verify SSD NG!!!`/`Verify MAC NG!!!` in het AM-logboek van de game (regel `amDongleUpdate`) en blijft 0949 staan; (2) of de tabel van een nieuwere AM Library dezelfde records heeft is uit de
vergelijkbare lees- en omzetcode afgeleid, niet gemeten; (3) de lengte van het antwoord bij een niet-gevonden uitdaging (nullen) is goed voor de willekeurige gevallen, een fout bij een
verifieerbare uitdaging (tabel die de game anders laadt dan wij) geeft dezelfde 0949.

## Testrecept (TV)

Per game, in deze volgorde. Voorbeeld LGI; voor een andere game dezelfde stappen met zijn map in `docs/research/segajvs/patches/<Game>/`.

1. Bouwen (container, geen netwerk): `tools/dev/build-amhw.sh` -> `build-amhw/rg-amhw.dll`; `cmake --build build` + `install-user` + daemon herstarten (voor
   de AM-daemon en `rg-direct`, zoals eerder).
2. Zetten in de gebruikerslaag (de meegeleverde `dropin.ini` met de dongle blijft staan: de secties worden samengevoegd):
   `mkdir -p ~/.local/share/ravengun/patches/LGI/dropin`,
   `cp docs/research/segajvs/patches/LGI/game.ini ~/.local/share/ravengun/patches/LGI/game.ini` (vervangt de vorige overlay),
   `cp docs/research/segajvs/patches/LGI/dropin/dropin.ini build-amhw/rg-amhw.dll ~/.local/share/ravengun/patches/LGI/dropin/`.
   Zet daarin ook `[Env]` `WINEDEBUG = -all,+seh` zolang je het crashadres wilt zien (haal het weg voor een gewone run).
3. Controle voor de start: `build/rg-direct --plan LGI` toont bij `dropin_lines` `preload: rg-amhw.dll -> TP_PRELOAD_DLL=...`.
   Voor meting 5 (LGS, SDR, OG, Transformers, LGI3D, GG): zoek in `rg-amhw.log` ook naar `columba: fysiek geheugen 0x000F0000 (lengte 0x... buffer 0x8010)`,
   `tabel: ...SBxx_Table.dat ontbreekt; vervangen door ...\rg-amhw-tables\...` en, alleen bij Transformers, `kop: .rdata stond op schrijfbaar`. De AM-regels van
   rg-direct tonen `request=<naam>` met "result=success".
4. Dubbelklik. In de view (`~/.local/share/ravengun/runtime/views/LGI/`) verschijnt `rg-amhw.log`. Verwacht daarin: `rg-amhw geladen in ...LGI_RingW_F_safe.exe`,
   `importtabel: KERNEL32.dll!CreateFileW omgezet` (en DeviceIoControl, CloseHandle, SetFilePointer, ReadFile, WriteFile), `columba: fysiek geheugen
   0x000F0000 ... BIOS-ruimte met _DMI_`, dan `0x000E0000 ... DMI-tabel`, `mxsram: geopend`. Staat die log er niet, dan is de DLL niet geladen.
5. Slaagt: geen toegangsfout op 0x6F0515; in het logboek van rg-direct komen na de AM-regels `JVS-pipe: game verbonden` en pakketten. Stuur dan
   `rg-amhw.log` en de laatste 40 regels van `direct-LGI-*.log`: elke `niet nagebootst`-regel is een volgend apparaat.
6. Faalt het opnieuw met een toegangsfout: stuur het adres (`dispatch_exception ... addr=`) en de regels van `rg-amhw.log`; zoek de functie met
   `python3 -I tools/dev/segajvs-scan.py <exe> --where 0x<adres>`.

Per game verandert alleen de map; de verwachte `columba`/`mxsram`-regels zijn gelijk. GG en Transformers staan in `exe/` (de view-wortel is daar al
`exe/`, dus `To = .`). SDR draait nu zonder view (`View = off`); de overlay zet `View =` (leeg = automatisch) omdat een drop-in een lokale view nodig heeft.

## De test per game (voor de TV)

Voorwaarde voor elke test: de eigen OpenParrot-build staat in `~/.local/share/ravengun/openparrot/current/` (Win32-kern en lader) en
`rg-jvsbridge.exe` in `~/.local/share/ravengun/jvsbridge/`; dat is wat `rg-direct --plan` voor de kandidaat al aanwijst (gecontroleerd,
zie "Plan-controle").

1. Overlay in de gebruikerslaag zetten: `mkdir -p ~/.local/share/ravengun/patches/LGI && cp docs/research/segajvs/patches/LGI/game.ini ~/.local/share/ravengun/patches/LGI/game.ini (en de drop-in uit het Testrecept hierboven)`.
2. Géén `pcp-listen.py` tegelijk: de AM-daemon van rg-direct gebruikt dezelfde poorten (een bezette poort wordt gemeld en overgeslagen).
   `pcp-listen.py` blijft het meetgereedschap om *zonder* onze antwoorden te zien wat een game vraagt (bv. een nieuwe game die nog geen
   `[AmDaemon]` heeft): start het dan vóór de game en laat `[Launch] AmDaemon` weg.
3. Dubbelklik in de app. Wat te zien en te melden, in volgorde:
   * Venster/beeld binnen 2 minuten? Zo niet: stuur de regels `AM-daemon ...` en de laatste 30 regels van het logboek.
   * Attract; START (toets 1 even vasthouden); schieten met muismodus; 5 minuten door (regel "spel gehaald").
   * Logboek: `~/.local/state/ravengun/logs/direct-<Game>-*.log`, let op `JVS-pipe: game verbonden` en het aantal pakketten.
4. Slaagt het: de drie regels uit de overlay in `patches/<Game>/game.ini` zetten (`[Loader] Core = ravengun`) en de overlay weghalen.

| Game | Wat anders is aan de test |
|---|---|
| LGI | Eerste kandidaat: gemeten dat de game bij `systemflag` en `getcount` stokt zonder daemon; nu beantwoord. Let op de 70 s laadtijd en de regels `amDongleXxx() is failed`. |
| LGI3D | Zelfde als LGI; rood-cyaan 3D-beeld is normaal. |
| LGS | Beslissend voor punt 2. Zonder daemon: hang bij "Now Loading" en een `connect` naar 40100 in het logboek. Stuurwiel/knoppen via gamepad of Move zoals nu. |
| GG, SDR | Dream Raiders vraagt de kalibratie bij de eerste start (zie `patches/SDR/game.ini`). |
| OG | Films via Gst32 + registerbestand blijven zoals ze zijn; zie het experiment hieronder als de AM-daemon blokkeert. |
| Transformers | `Windowed = 1` blijft nodig onder gamescope. |

### Experiment OG (alleen als OG op de AM-daemon blijkt te hangen)

`gs2.ini` in de spelmap heeft `EnableAMLib=1` en `EnableJvs=1`; de exe kent `AMLIB NOT LOADED.` en `getAMLibInstance() is NULL`
**[gelezen]**. Een drop-in `gs2.ini` met `EnableAMLib=0` (via `patches/OG/dropin/dropin.ini`, sectie `[gs2.ini]` met `To = .`) zou de
AM Library overslaan. Of OG daarna nog JVS via `amJvs` opent is niet gelezen; alleen proberen als de gewone kandidaat op de daemon
hangt. Het origineel op de NAS blijft ongemoeid (de view krijgt de eigen kopie).

## Plan-controle (2026-10-17, `rg-direct --plan`, geen start)

Voor elke kandidaat (`RAVENGUN_PATCHES_DIR` = patches-repo, `RAVENGUN_USER_PATCHES_DIR` = tijdelijke map met de overlay): verdict `ok`, lader
`openparrot/current/OpenParrotWin32/OpenParrotLoader.exe`, kern `.../OpenParrot.dll` (`core_source: ravengun`), JVS-pipehulp
`jvsbridge/rg-jvsbridge.exe`, route `jvs`/pipe met het bord uit `game.ini`. In de opdrachtregel staat geen bestand uit de
TeknoParrot-runtime meer. Wat de centrale route nog uit die map leest is werk van de centrale ontkoppeling, niet van deze games:
de werkmap (`cwd`) en de `GameProfiles/<P>.xml`-gegevens (`tp`), en het oude `UserProfiles/<P>.env` (alleen LGI/LGI3D/LGS hadden er een;
`game.ini` wint).

## Wat niet is gedaan, en waarom

* Geen `Type`-wijziging in `patches/<Game>/game.ini`: tot de test slaagt blijft elke game op de werkende gesloten kern.
* Geen drop-in-DLL voor de seriele/JVS-kant: die doet onze kern al (punt 1).
* Geen antwoorden voor de AM-diensten die nog geen enkel verzoek hebben laten zien (install, storage, network, `mxmaster.*` anders dan
  `getcount`): onbekende sleutels krijgen een voorzichtig antwoord en staan als ONBEKEND in het logboek; de eerste meting vult ze aan.

## Volgende stap

1. LGI op de TV met de overlay (zie boven). Per volgend onbekend verzoek in het logboek: de ontleding in de exe lezen
   (`tools/dev/segajvs-scan.py <exe> --names '^amDongle'` geeft de functiebegin-adressen, `objdump -d -M intel` de code) en de waarde
   in `amdaemon.cpp` of `[AmDaemon]` zetten.
2. Voor LGS, GG, OG, Transformers komen install (40102/40103), netwerk (40104) en opslag (40114/40115) erbij: hun
   `amInstallResponseCheck`, `amNetwork...` en `amStorageResponseCheck` lezen zodra het logboek laat zien welke sleutels ze sturen.
