# Namco- en ALLS-games zonder TeknoParrot-kern: Point Blank X, Mario Kart DX, Time Crisis 5, HOTD Scarlet Dawn

*Stand 2026-10-17 (tweede ronde: TC5 en HOTDSD hebben nu een kandidaat). Doel van de eigenaar: elke game start zonder de gesloten `TeknoParrot(64).dll` en zonder bestand uit
`~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64`. Dit stuk gaat over de vier games die op de directe (Wine-)route
nog de gesloten kern laden. **Niets hiervan is op de tv gemeten**: er is niets gestart (geen game, geen Wine); alles is statisch gelezen
(objdump, strings, decompilatie van de Unity-assembly's, de open bronnen van TeknoParrotUI en OpenParrot) en gebouwd in de mingw-container.
Kandidaat-configuraties staan in `docs/research/namco/<Game>.game.ini` en, met hun drop-ins, in `patches/<Game>/game.kandidaat.ini` en `patches/<Game>/dropin.kandidaat.d/`. **Tweede ronde (TC5 en HOTDSD): de volledige kandidaat (`game.ini`, `dropin/`, `data/`) staat in `docs/research/namco/patches/<Game>/`; controle zonder TeknoParrot met `tools/dev/namco-candidates-check.sh`.** De meegeleverde
`patches/<Game>/game.ini` en `dropin/` zijn ongewijzigd, **behalve Mario Kart DX** (besluit eigenaar 2026-10-17: standaard zonder TeknoParrot,
de gesloten kern via de optie Online play); de andere blijven op de gesloten kern tot de tv het tegendeel laat zien.**

## Overzicht

| Game | Wat de kern deed | Onze vervanging | Stand |
|---|---|---|---|
| Point Blank X | de USIO-laag (`Plugins/bnusio.dll`: bord via WinUSB, vuurwapen via COM) naar het gedeelde geheugen leiden; schijfletters F:/G: | **`bnusio.dll` van ons** (`tools/rg-usio`, drop-in op de plaats van het echte bestand) + onze kern (generiek, schijfletter-omleiding patch 0004) | **klaar om te testen** |
| Mario Kart DX | JVS: `COM3` -> pipe `TeknoParrot_JVS`; ALL.Net/AMAuthd (COM-server) beantwoorden en naar de TP-gemeenschapsserver wijzen; schijfletters; `teknoparrot.ini` | JVS-over-COM3 in `rg-mfcam.dll` + onze kern, **standaard** (offline). Optie "Online play" in Properties zet `[Loader] Core = teknoparrot` terug (online, vraagt TeknoParrot) | **standaard offline; crash na ~15 s is ShareK (niet IAuth), kandidaat: eigen ShareK-object in `rg-mfcam.dll`, zie "Mario Kart DX: de crash na ~15 s"** (`docs/research/namco/patches/MarioKartDX/`) |
| Time Crisis 5 | WinUSB/SetupAPI-emulatie van het USIO-bord in het spelproces, de HASP-laag, aim/trekker | **`winusb.dll` van ons** (`tools/rg-winusb`, drop-in naast de exe) + onze kern + HASP-drop-in (bestond al) | **kandidaat klaar om te testen** (aim/trekker, pedalenbits en HASP gegokt) |
| HOTD Scarlet Dawn | platformgegevens voor `amdaemon.exe` (register), schijfletters AMFS/D/Y, USB-IO-bord (HID) uit de besturingspipe, twee-exe-start | `platform.reg` + onze kern + **`rg-allsio.dll`** (`tools/rg-allsio`, HID-nabootsing in de daemon, preload) | **kandidaat klaar om te meten** (productstring-staart en antwoorden gegokt; schijfletters open) |

## Wat een lege Wine-start van deze games mist (de gemeenschappelijke grondslag)

* Onze OpenParrot-kern op de generieke route (`[Loader] Core = ravengun`, patch 0003) laadt elke game zonder per-game hooks. Wat hij wel voor
  iedere game doet (patch 0004): `CreateFileA/W` en `CreateDirectoryA/W` op `<L>:\` of `<L>:/` worden `.\TeknoParrot\<L>\...` (cwd-relatief,
  `C:` en `Z:` nooit). Daarmee is de schijfletterlaag van de gesloten kern gedekt voor Point Blank X (`F:`/`G:` voor `nbamsavdat.dll` en
  `Setting.ini` `MAIN_PATH`/`BACKUP_PATH`) en Mario Kart (`G:/Contents`; de `./F...`-mappen staan al in de exe).
* Per-game hooks horen in de drop-in van de game, niet in de kern (`docs/dropins.md`). De loader laadt **een** preload-DLL per Windows-game
  (`TP_PRELOAD_DLL`); extra reparaties gaan dus in dezelfde DLL (zoals de pHResult-reparatie in `rg-mfcam.dll`).

---

## Point Blank X (EmulationProfile `PointBlankX`, 32-bit Unity, OpenGL)

**Wat het spel van het I/O-bord wil.** `Assembly-CSharp-firstpass.dll` heeft de klasse `BNUsio` met 48 `[DllImport("bnusio")]`-functies
(36 `usio_*`, 12 `gun_*`); `BNUsioController` (Assembly-CSharp.dll) is de enige gebruiker. Het echte `Plugins/bnusio.dll` (21 KB, MSVC 2013)
opent het USIO-bord via SetupAPI + WinUSB en het vuurwapen via een COM-poort (`Setting.ini`: `GUNPORT 2`); zijn exports zijn cdecl en
ongedecoreerd (gemeten: `ret` zonder argument). Wat het spel leest: `usio_GetSwIn()` (start 1P = `0x02000000`, 2P = `0x01000000`, test `0x80`,
service `0x4000`, omhoog `0x2000`, omlaag `0x1000`, enter `0x200`), `usio_GetCoin(0)` + `usio_DecCoin`, `gun_GetPositionX/Y(1|2)` (tienbits
waarde `<< 6`, zo doet het echte bestand het), `gun_IsTriggered`, `gun_IsInRange`, firmwareversie (moet 120..126 zijn). Zonder bord valt het
spel terug op muis/toetsenbord (`Setting.controller`).

**Wat de kern deed.** De gesloten kern haakt die laag in het spelproces en vult hem uit het gedeelde geheugen `TeknoParrot_JvsState`
(TeknoParrotUi `PointBlankPipe`: offset 8 bitveld, offsets 12/16/20/24 de assen van P1/P2). De afbeelding bit -> schakelaar is gesloten
code; wij hebben haar afgeleid uit wat het spel zelf eist (hierboven).

**Vervanging.** `tools/rg-usio/rg_usio.cpp` = `bnusio.dll` met dezelfde 48 exports (`objdump`: namenlijst identiek aan het echte bestand),
gevoed door een eigen pollende draad op het gedeelde geheugen. De logica staat in `tools/rg-usio/usio_model.h` en is getoetst
(`tests/test_usio_model.cpp`, 544 controles): munt/service = rand omhoog -> teller +1 (zoals de muntgever pulst), `DecCoin` niet onder 0,
assen 0..255 -> tienbits `<< 6` (max `0xFFC0`), y omgekeerd (Unity telt y van onderen; de terugval van het spel gebruikt
`Input.mousePosition / Screen`; `RG_USIO_FLIP_Y=0` zet dat uit). Bouwen: `tools/dev/build-usio.sh` (mingw i686 in de gepinde container,
statisch). Plan met de kandidaat: `rg-direct --plan` is `ok`; `core_source: ravengun`; `dropin_files` = `bnusio.dll` in
`PBX100-2-NA-MPR0-A63_Data/Plugins/` (de map wordt lokaal gemaakt, de NAS blijft ongemoeid).

**Niet gedekt / onzeker.** De richting van y is afgeleid, niet gemeten. `gun_IsInRange` is altijd waar (de daemon meldt geen "buiten
beeld"). De ticketautomaat (`TicketIO`, COM) is een ander subsysteem en bleef zoals op de gesloten kern. Terugslag (`gun_StrikeOnce`) doet niets.

**Test op de tv.** (1) kandidaat activeren: `patches/PointBlankX/game.kandidaat.ini` over `game.ini` en `dropin.kandidaat.d/` naar `dropin/`
(of in de gebruikerslaag `~/.local/share/ravengun/patches/PointBlankX/`). (2) dubbelklik. (3) `Plugins/rg-usio.log` in de view: eerste regel
"rg-usio (bnusio.dll) geladen", dan "gedeeld geheugen TeknoParrot_JvsState geopend" en "eerste aanroep: usio_Initialize ... gun_GetPositionX".
Geen log = het spel las het echte bestand. (4) munt (toets 5) -> credit; start; richt op linksboven: het vizier hoort linksboven te staan,
anders `[Env] RG_USIO_FLIP_Y = 0`. (5) 5 minuten spelen. Bewijs van het laden: `/proc/<pid>/maps` van het spelproces bevat
`Plugins/bnusio.dll` met de sha256 uit `dropin.kandidaat.d/`, en bevat **geen** `TeknoParrot.dll`.

---

## Mario Kart Arcade GP DX 1.18 (EmulationProfile `NamcoMkdx`, 32-bit, D3D10/11)

**Wat de kern deed (gemeten aan de exe, niet aan de kern).**

1. **JVS.** `CreateFileA("COM3", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)` op `0x8ac1f2`, daarna
   `GetCommState`/`SetCommState` (115200 8N1, `0x8ac2ab`), `SetCommTimeouts`, `SetupComm`, `PurgeComm`, `GetCommModemStatus` (sense-lijn).
   De kern zet COM3 om naar `\\.\pipe\TeknoParrot_JVS` (zelfde gedrag als `amJvs.cpp` in de OpenParrot-bron, daar voor `hookPort = "COM3"`).
2. **ALL.Net.** `CoCreateInstance(CLSID {4603BB03-058D-43D9-B96F-639BE908C1ED}, NULL, CLSCTX_LOCAL_SERVER, IID {045A5150-D2B3-4590-A38B-
   C1158678E1AC})` op `0x4010c0`: dat is de COM-server **`AMCUS/AMAuthd.exe`** (beide GUID's staan in `AMAuthd.exe`; de IID in het
   proxy/stub-bestand `AMCUS/iauthdll.dll`, dat 32 methodes aangeeft = IUnknown + **29 eigen methodes**). Het spel leest er `auth_state`,
   `daemon_state`, `daemon_error`, `last_auth_error`, de PowerOn-velden (`uri`, `host`, `place_id`, `country`, `region0`, `timezone`) en
   het IP van het spel; mislukt `CoCreateInstance`, dan zet het spel het vlag-byte van het wikkelobject op 0 en gebruikt vaste waarden
   (onder andere toestand 8 en -1, `0x461fc5`): dat is een bedoeld pad, geen crash. `AMAuthd` registreert zelf zijn klasse
   (`CoRegisterClassObject`) en kent `authType` = `ALL.NET` | `OFFLINE` | `NBLINE` in `AMConfig.ini`.
3. **De gemeenschapsserver.** `Network/ServerAddress`/`ServerPort`/`PlayerId`/`Gateway` komen alleen in `teknoparrot.ini`. In een
   geheugenbeeld van de gesloten kern staat het online-protocol van TeknoParrot: ondertekende sessies (`TPAUTH`, `TPSK1`, `TPENC1`,
   HMAC, "the server refused this cabinet", `[Network] OnlineID / OnlineSecret` uit het teknoparrot.com-account). Dat is een
   eigen, geauthenticeerd protocol van een externe dienst.
4. Overig: schijfletters (`G:/Contents`), 16 MiB-stapel (engine, `StackReserve`), `Polygon2D`-ViewLink en d3dx10-pHResult (al van ons).

**Vervanging.**

* JVS: `tools/rg-mfcam` kreeg een derde taak: met `RG_JVS_COMPORT=COM3` (in de drop-in) zet `rg-mfcam.dll` in de importtabel van de exe
  `CreateFileA` (COM3 -> pipe, met wachten tot de brug de pipe heeft) en de zeven comm-aanroepen om (slagen op dat handvat);
  `GetCommModemStatus` geeft 0x10, na adrestoekenning 0x30 (byte 0 van `TeknoParrot_JvsState`, door `rg-jvsbridge` gespiegeld).
  Beslislogica in `tools/rg-mfcam/jvs_com.h`, getoetst in `tests/test_jvs_com.cpp`. Logboek: `rg-mfcam.log`.
* Kern: `[Loader] Core = ravengun` (generiek).
* **Online werkt zonder de gesloten kern niet**, en wij bouwen het niet na: (a) het TPAUTH-protocol is dat van TeknoParrot's dienst en vraagt
  het account van de eigenaar; (b) een eigen `IAuth` (29 methodes, uitvoerstructuren die het spel veldgewijs leest) is zonder proef met het
  echte spel niet betrouwbaar te maken; (c) (zie ook de crash-sectie hieronder: de eerste tv-crash was niet IAuth) `AMAuthd.exe` in `OFFLINE`-modus zou de eigen code van het spel gebruiken, maar vraagt een
  tweede-exe-start met COM-registratie (`iauthdll.dll` `DllRegisterServer`, `LocalServer32`) en een `AMConfig.ini`-kopie met
  `amdcfg-authType=OFFLINE`; of het spel die offline-auth accepteert is niet gemeten. De kandidaat start het spel dus **zonder**
  `IAuth`: op het pad dat het spel zelf kent (vlag 0). Of het daarna tot de attract komt of na 2 minuten op de netwerkcontrole wacht
  (`ALL.NetCheck_TIMEOUT`, na 7200 = `0x1c20` ticks) is de vraag voor de tv.

### Mario Kart DX: de crash na ~15 s is ShareK, niet IAuth (statisch gemeten 2026-10-17)

**Wat de tv-meting (`WINEDEBUG=+seh`, `direct-MarioKartDX-20261017-151925.log`, regel 1970-1990) werkelijk zegt.** Het logboek is binair
(`grep -a`). De regel `*ERR* ComError Code = 80040154` komt van de catch-afhandelaar op `0x47E8F8`, en die hoort bij de functie op
`0x47E7D0` (`StartIpcClient` in `..\AppSystem\Network\ShareK\ShareKManager.cpp`; bronpad op `0x9b2bfc`), niet bij de wikkel van `AMAuthd`. De `CoCreateInstance` in die functie
(`0x47E834`) vraagt **CLSID `{ccc44b2a-709a-33a8-a1b0-5dbe25b19c2c}`, IID `{c2116f7f-cf46-3c68-b187-9d0fe9f70ed0}`**, `CLSCTX` 0x17 (GUID's op
`0x9b2bbc` / `0x9b2bac`). Dat zijn naam-gebaseerde GUID's (versie 3), zoals .NET ze voor een `ComVisible`-klasse zonder `[Guid]` afleidt; ze staan als
bytes of tekst in geen enkel bestand van de spelmap behalve de exe (scan van de hele map, ook `ipccl.dll`, `ShareK/*`, `AMCUS/*`). Het zijn de COM-klasse van **ShareK**: `init_w10.ps1` (de echte opstartketen, UTF-16) doet
`RegAsm.exe .\ipccl.dll` (de .NET-client) en start daarna `ShareK\ShareK.exe` via `sharek.ps1` in een lus; ShareK is de content-distributie tussen
kasten (`NET_USE_SHAREK`, `NET_SHAREK_CHECK_MASTER_SLAVE_*` in `Data/System/Config/NetConfig.csv`). De registratie ontbreekt in onze prefix.

| adres | wat |
|---|---|
| `0x47E7D0` (`StartIpcClient`, `ret` op `0x47E8E3`) | roept `0x47DF90` (Init, leest `NetConfig.csv` opnieuw) en doet dan **altijd** `CoInitialize` + `CoCreateInstance` (`0x47E834`), ook als Init de vlag `M+0x394` net op 0 zette |
| `0x47E269`, `0x47E29D` | Init: `_waccess_s(L"G:/Contents")` / `_mkdir("G:/Contents")` mislukt, log `Zipダウンロードパス生成失敗.ShareKは強制的に未使用.` (regel 1923 van het logboek) en vlag `M+0x394 = 0` |
| `0x47E870` | `call [vtbl+0x1c]` (`StartIpcClient`, `[out] VARIANT_BOOL`), `<0` gooit `_com_error` (throw-info `0xA32708`) |
| `0x47E8F8` | catch: logt `ComError Code = %08lx` (`0x47E92C`), laadt `M[0]` (de interface, hier nog 0) en doet `0x47E95E: mov ecx,[eax]` -> **toegangsfout** (`call [vtbl+0x20]` volgt) |
| `0x481031` | herstart (`state == 2`): dezelfde `CoCreateInstance` (`0x48107F`) met dezelfde catch-fout (`0x48114E`) |

**IAuth (AMAuthd) is wel een tweede, aparte vraag.** `CoCreateInstance(CLSID {4603BB03-...}, ctx 4 = LOCAL_SERVER, IID {045A5150-...})` zit in
de wikkel-constructor `0x401000` (`0x4010C0`) en **vangt zijn eigen mislukking af**: `hr < 0` -> logt (`0x4010CC`, via `0x6095C0`), `CoUninitialize`,
vlag-byte `[wikkel+0] = 0` (`0x4010D9`); geen uitzondering, geen crash. De wikkel hangt aan `ds:0xec29fc`; alle 151 plaatsen die hem gebruiken testen
eerst `cmp BYTE [wikkel],0` en gebruiken dan vaste waarden (`0x461910`: toestand 8, `0x461f71`: -1). De toestandsmachine op `0x461f1a..0x462033` telt
frames in `[esi+0x10]`; bij `0x1c20` (7200, 2 minuten) zet ze toestand 6 en logt alleen `* ALL.NetCheck_TIMEOUT:auth_state:%d daemon_state:%d
daemon_error:%d last_auth_error:%d last_lib_error:%d` (`0x9b08a8`). Het spel gebruikt negen van de 29 methodes (vtable-offsets `0x24` (56 plaatsen),
`0x28`, `0x30`, `0x34`, `0x38` (PowerOn-structuur van 0x8a2 bytes: `uri`, `host`, ... op `0x461a47..0x461c9b`, offsets `+0x78 +0x179 +0x27a +0x37a +0x47a
+0x4fa +0x57a +0x59a +0x69a +0x79a`), `0x3c`, `0x4c`, `0x74`, `0x78`; geen volledige signaturen, alleen `this` + een uitvoerpointer is bewezen voor
`0x24`/`0x28`/`0x38`). Het volledige IAuth-object is dus nodig voor een spel dat ALL.Net *volgt*, niet om op te starten.

**Gekozen weg (met bewijs).** Voor deze crash is geen van de twee IAuth-wegen nodig; de oorzaak is ShareK. Gekozen: **een eigen in-proces ShareK-object**
in `rg-mfcam.dll` (VIERDE TAAK, `tools/rg-mfcam/sharek_fake.h` + `rg_mfcam.cpp`), omdat (1) de oorzaak en de plek bewezen zijn (tabel hierboven),
(2) de interface volledig uit de aanroepplekken af te lezen is (36 gebruikte vtable-slots in `0x47C000..0x483400`, alle argumentgroottes: 8 bytes, zes
methodes 20 bytes), (3) een in-proces object geen tweede exe, COM-registratie of .NET (Mono) in de prefix vraagt, en (4) het gedrag erachter door het
spel zelf gedefinieerd is: status 2 (`0x47EEA5`, "ネットワーク未初期化により自動復旧待機") laat de tick op `0x47ECD0` elke keer het werk overslaan
(`0x47EEE3`). `NET_USE_SHAREK = FALSE` in `NetConfig.csv` zou de aanroep ook overslaan (`0x47E807` test de vlag), maar een bestand in de view is niet per kern te kiezen (een
drop-in of `ViewCopies` geldt ook voor Online play) en `[Patch] Kind = file` raakt alleen de lader/kern, geen spelbestand. Het is wel de terugval als het object tegenvalt.

**Het object** (`sharek_fake.h`): `CoCreateInstance` in de importtabel (`ole32.dll`, IAT `0x943870`) omgezet; alleen voor die ene CLSID komt een eigen object,
al het andere (ook IAuth) gaat naar de echte functie en wordt (eerste 16 aanroepen) gelogd. Slot 7 `StartIpcClient` -> `VARIANT_TRUE`; slot 33 (+0x84) herstartvraag
-> 0; slot 35 (+0x8c) status -> 2; alle andere gebruikte slots `S_OK` zonder te schrijven (de aanroeper zet hun uitvoer vooraf op 0: `mov [ebp-0x38],ebx`).
`Release` telt nooit onder 1 (statisch object). Alleen aan met `RG_SHAREK_FAKE=1` (dropin.ini `Env`) **en** zonder `TeknoParrot.dll` in het proces.

**Getoetst (zonder het spel).** `tests/test_sharek_fake.cpp` (176 controles: GUID-bytes zoals in de exe, de slottabel, 8/20 bytes, alleen slots 7/33/35 schrijven).
Daarnaast een wegwerpprogramma (`sk_probe.exe`, geen spel, wegwerp-prefix in de scratchpad, MinGW i686) dat `rg-mfcam.dll` met `RG_SHAREK_FAKE=1` laadt, via
`CoCreateInstance` het object vraagt en elk gebruikt slot aanroept zoals het spel (stdcall, 8 of 20 bytes) met een `esp`-controle: **0 mislukt**, IAuth gaat naar de
echte aanroep (`0x80040154`), `QueryInterface` weigert vreemde IID's. Dit bewijst de stapelbalans en de logica; niet dat het spel er verder mee komt.

**Online play (Core = teknoparrot) blijft ongemoeid.** (1) `game.ini` is ongewijzigd t.o.v. `patches/MarioKartDX/game.ini`; alleen `dropin.ini` kreeg
`;RG_SHAREK_FAKE=1`; (2) de DLL zet de omleiding niet aan als `TeknoParrot.dll` of `TeknoParrot64.dll` in het proces staat (zelfde controle als de
JVS-omleiding; logregel `ShareK-object uit: de TeknoParrot-kern is geladen`); (3) `--plan` met `[Loader] Core = teknoparrot` in de gebruikerslaag vraagt de kern uit de
TeknoParrot-map zoals voorheen (`kern ontbreekt: /nonexistent/TeknoParrot/TeknoParrot.dll` met een map die niet bestaat). Niet gemeten: een echte Online
play-run met deze DLL (de eigenaar speelt online; de TP-kern kent `CoCreateInstance` van ShareK/IAuth zelf, hoe is onbekend).

**AMAuthd zelf als tweede exe (weg b) — waarom niet, bewijs.** `AMAuthd.exe` (280 KB) importeert KERNEL32/USER32/ADVAPI32/ole32/WS2_32/IPHLPAPI (geen
setupapi, geen dongle-DLL), maar: `SetSystemTime`-rechten (`AdjustTokenPrivileges`), de ALL.Net-client `alAbEx`, MUCHA-geheugens (`muchaut_*`, daemon
`.\MuchaBin\muchacd.exe` op `tcp:0.0.0.0:8765`), en leest `WritableConfig.ini` (`serialID`, `netID`, `mode`) dat `PreprocessAmAuth.exe` schrijft uit
`HKLM\SOFTWARE\NBGI` (`ContentsPartitionPath`) en het schijfserienummer (`init_w10.ps1`: `Get-Disk ... SerialNumber`). `init_w10.ps1` start ook
`AMUpdater.exe -wait` (exitcode 1 = 60 s wachten). Dat zijn vijf losse onderdelen met schrijfacties in `AMCUS/` (NAS: schrijflaag) plus
`regsvr32 iauthdll.dll` in de prefix; niets daarvan is statisch te bewijzen werkend, en `rg-direct` kent geen COM-registratie (`[Patch] registry`
wordt niet toegepast). Weg (a) (eigen IAuth, dezelfde techniek als hierboven, negen methodes) is de volgende stap als de tv na 2 minuten op de
ALL.Net-controle vastloopt; de uitvoerstructuren (PowerOn) zijn bekend uit `0x4619D0..0x461E49`, de waarden die het spel als "OK" accepteert niet gemeten.

**Niet bewezen.** (i) Dat het spel met het ShareK-object tot de attract komt: er kan een volgende blokkade zijn (de ALL.Net-timeout, E05-xx).
(ii) De semantiek van de overige ShareK-slots; ze zijn `S_OK`/nul, wat de tick (status 2) nooit leest. (iii) `G:/Contents` aanmaken: het
spel logt `Zip...生成失敗` zolang `_mkdir("G:/Contents")` mislukt (schijfletter G: in de prefix); met ShareK gedwongen "ongebruikt" is dat onschadelijk.
(iv) Een volledige 5-minutenrun.

**Exact tv-testrecept (aan de coördinator; de bench is niet van deze sessie).**
1. Activeren: `docs/research/namco/patches/MarioKartDX/dropin/{dropin.ini,rg-mfcam.dll}` naar `~/projects/ravengun-patches/patches/MarioKartDX/dropin/`
   (de dll staat niet in git (`*.dll` is genegeerd) maar in de werkboom; opnieuw bouwen: `tools/dev/build-mfcam.sh --out docs/research/namco/patches/MarioKartDX/dropin`, reproduceerbaar, sha256 `3dca574ac7b1f6a97b53c1ec300c499308010c72f8f4de88271b2f7f9418b746`). Zorg dat de gebruikerslaag `~/.local/share/ravengun/patches/MarioKartDX/game.ini` voor deze run
   **geen** `Core = teknoparrot` heeft (nu wel, voor het online spelen van de eigenaar; terugzetten daarna) en `[Wine] Debug = +seh` voor de spelmeldingen.
2. Controle zonder TP: `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan MarioKartDX --patches <map>` (alle patches + deze `MarioKartDX`) geeft `ok`, `core_source
   ravengun`, `RG_SHAREK_FAKE=1`, geen pad in de TeknoParrot-map.
3. Dubbelklik in de app. In `rg-mfcam.log` (view): `importtabel: ole32.dll!CoCreateInstance omgezet (eigen ShareK-object ...)`, `ShareK: CoCreateInstance({CCC44B2A-709A-33A8-
   A1B0-5DBE25B19C2C}, ctx 0x17, {C2116F7F-...}) -> eigen object, hr 0x00000000`, `ShareK-object: slot 7 aangeroepen` (en 33, 35), en
   `CoCreateInstance({4603BB03-...}, ctx 0x4, {045A5150-...}) -> hr 0x80040154 (echte aanroep)` (de IAuth, bedoeld pad).
4. Spellogboek: **geen** `*ERR* ComError Code = 80040154` en geen `c0000005` op `0x0047E95E`; wel `Startup ShareK-Client.` en `ShareKステータス変化：ネットワーク未初期化...`.
5. 5 minuten attract/spel kijken, ook voorbij 2 minuten (`ALL.NetCheck_TIMEOUT` staat dan in het spellogboek): wat toont de tv (netwerkfoutscherm, E05-xx)?
   Bewijs van het laden: `/proc/<pid>/maps` bevat `rg-mfcam.dll` en **geen** `TeknoParrot.dll`.
6. Faalt het daarna op ALL.Net: eerst `rg-mfcam.log` (de regel van de echte `CoCreateInstance` van IAuth), dan weg (a) voor IAuth bouwen.

### Mario Kart DX: de stalldump van 2026-10-18 07:42 gelezen (hoofdlus draait; schijfletter-herleiding aangevuld)

**Wat de dump zegt** (`rg-mfcam.log`, 20 s en 45 s, identiek):

* **Hoofddraad.** `EIP win32u.dll+0x11d38` (de syscall-stub; Wine geeft een in een syscall opgeschorte draad dit adres), op de stapel `0x83D7D6`, `0x77070A/0x77070F`.
  `0x83D7D6` is de terugkeer na `call [0x943600]` = **`PeekMessageW`** in de berichtenlus van `0x83D630` (bij `0x83D7D4`; de lus daarna: `PeekMessageW(PM_REMOVE)`,
  `TranslateMessage`, `DispatchMessageW`, anders `call 0x8451E0`); `0x77070A` is de aanroep van die lus vanuit de hoofdfunctie (`0x77070F` = terugkeer).
  De hoofdthread wacht dus **niet** op een venster, bericht of gebeurtenis: hij draait de gewone lus, en `0x8451E0` (de scheduler) vindt kennelijk niets te doen
  (een tweede dump gaf hetzelfde EIP en dezelfde ESP: de lus is vrijwel helemaal PeekMessage).
* **Werkers wachten, niemand werkt.** 20 draden slapen in de werkerspool (`Sleep(30)` op `0x8527FB`), vier in `SleepEx`-pollinglussen (`0x607C1D`, `0x8040EC` (10 s), `0x5F88C7`, `0x5F534A`),
  de invoerdraad (`0x5F5310`, `SleepEx(8)`, leest de JVS-invoer via `0x8AB990`) en audiodraden (`mmdevapi`): dat verklaart de geluidsstroom, niet het beeld.
* **JVS is nooit geopend.** `COM3` staat hard in `0x8AC1B0`, aangeroepen via `0x8AB880` (alleen vanuit `0x6022D0`, een toestandsmachine die `[esi+0x3c] = 2` zet: de IO-controle van de
  opstartcontroles). Geen `JVS: CreateFileA(COM3)` in `rg-mfcam.log` van enige run met onze kern: de opstartcontroles bereiken die stap niet. De invoerdraad draait wel; `ds:0xec2470` (0 = JVS niet open, 1 na het openen op `0x8AB8F8`) is niet gemeten.
* **De camera-opstart (`0x84BEC0`) draait wel** (hij zit in `0x770770`, na `StartIpcClient`), maar `MFEnumDeviceSources` (`0x84BF5A`) wordt alleen aangeroepen als de camera in het testmenu aan staat.
  Dat testmenu-bestand (`TestMode/testmode_*.bin`) wordt nooit gelezen (zie hieronder): de camera staat dus uit. Dat is de verklaring voor het ontbreken van de camera-regel, niet een blokkade.
* **37,5 s na de init schrijft het spel een tweede `bookkeeping_*.bin.tmp`** (00:02:09 in de run van 00:01:31; 07:43:25 in die van 07:42:48) en daarna niets meer. Er gebeurt dus precies één ding op
  ~37,5 s (bij ~190 lus-ticks per seconde 7200 ticks = de `0x1c20` van `ALL.NetCheck_TIMEOUT`, maar die regel staat niet in het spellogboek). Niet onderzocht wat het is.

**Wat dit niet bewijst:** waarom het beeld zwart is. Een wachtende draad die iets blokkeert is er niet; de hoofdlus draait en de scheduler heeft geen werk. De eerste scène (opstartcontroles)
komt niet op gang. Kandidaten, in volgorde van bewijs: (1) de instellingen/dongle-staat die elke start leeg is (hieronder, gemeten); (2) de ALL.Net-controle zonder IAuth (toestand 6 zou een `*ERR*` loggen: niet gezien);
(3) iets in de TP-kern dat het spel anders aanbiedt (de TP-kern is gesloten, niet onderzoekbaar).

**Gebouwd: de schijfletter-herleiding aangevuld (ZESDE TAAK van `rg-mfcam.dll`, `drive_fix.h`).** Dezelfde regel als patch 0004 (`<L>:\` of `<L>:/` voor L niet C/Z naar `.\TeknoParrot\<L>\rest`) voor
de ingangen die de kern mist en die het spel wel gebruikt: `MSVCR100.dll!rename/_wrename/remove/_wremove/_waccess_s` en `KERNEL32.dll!FindFirstFileA/W/GetFileAttributesW`. Aan met
`RG_DRIVE_FIXUP=F` (nu in `dropin.ini`); alleen letter F: `G:/Contents` blijft zoals hij was (het spel zet ShareK uit). Uit zodra `TeknoParrot.dll` in het proces staat (Online play ongemoeid).
Getoetst: `tests/test_drive_fix.cpp` (24 controles) en een wegwerpprogramma onder Wine (`rename("F:/system/x.bin.tmp","F:/system/x.bin")` met `msvcr100`, `FindFirstFileA/W`, `GetFileAttributesW`,
`_waccess_s`, `remove`: alles landt in `.\TeknoParrot\F\system\`, `G:` niet; 0 mislukt). Niet getoetst: het spel zelf.

**Testrecept (aan de coördinator).**
1. dll en `dropin.ini` uit `docs/research/namco/patches/MarioKartDX/dropin/` activeren (bouwen: `tools/dev/build-mfcam.sh --out docs/research/namco/patches/MarioKartDX/dropin`); daarna `RG_STALL_DUMP=20,45,90`
   weer achter de `Env`-regel (of `--env`), `WINEDEBUG=-all,warn+seh`, `DXVK_HUD=fps` (of `frametimes`): een teller op de tv bewijst dat het spel `Present` aanroept.
2. Eerste run: in `rg-mfcam.log` `schijfletter: rename(F:/system/... => ...)` en in `TeknoParrot/F/{system,TestMode,Bookkeeping,DongleData}` nu `.bin` zonder `.tmp` (de oude `.tmp`-resten mogen blijven of weg).
3. Tweede run: de spelregels `ShareKSaveErrorInfo/ContentsInfo*/... load failed` moeten weg zijn (de bestanden worden nu gevonden en geladen). Verandert het beeld? Zo niet: de stalldump van die run.
4. Optioneel, alleen als de eigenaar zijn oude instellingen wil terug (camera aan, free play): `cp FTestMode/testmode_*.bin TeknoParrot/F/TestMode/` en idem `FBookkeeping`, `FDongleData`, `Fsystem` naar hun
   `TeknoParrot/F/`-map (view-data, geen NAS). Niet bewezen dat het spel dat nieuwste-bestand-eerst kiest; daarom niet automatisch.
5. Zwart beeld blijft met `.bin`-bestanden en een fps-teller: dan is het volgende meetvoorstel de TP-basislijn (dezelfde `warn+seh`-run met de TP-kern; de volgorde van de `*INF*`-regels na `Startup ShareK-Client.`
   en het eerste verschil is het antwoord), of een tweede stalldump-reeks (5, 10, 15, 30 s) om te zien of de `0x8451E0`-scheduler ooit werk vindt.

### Mario Kart DX: de volgende stap in de opstartvolgorde is de Banapass-kaartlezer (statisch + 08:14-meting)

**Wat de 08:14-meting laat zien.** De schijfletter-herleiding werkt (45 `schijfletter:`-regels, de FindFirstFile-zoekopdrachten vinden de overgenomen TP-bestanden); ongeveer 31 s na de ShareK-init
schrijft het spel een `bookkeeping_*.bin.tmp`, hernoemt die succesvol (`rename ... = 0`) en ruimt oude `bookkeeping*`-resten op. De hoofdlus is dezelfde (`PeekMessageW` in `0x83D630`), JVS blijft dicht.
De `system/*.bin` die de coordinator zocht horen niet bij deze stap: de opstart schrijft alleen bookkeeping, dus het ontbreken daarvan zegt niets.

**Wat de opstartvolgorde bepaalt (statisch).** De enige code die `COM3` opent is `0x8AC1B0`, alleen bereikt via `0x8AB880` <- `0x6022D0` (toestand `[esi+0x3c] = 2` in de opstartcontroles op `0x6027D0..0x604xxx`, met `[esi]` = het
systeemobject `ds:0xEC2A08`). In dezelfde familie (`0x601360..0x603300`) zit de **Banapass-kaartlezer-toestandsmachine**: `0x601550` met twaalf toestanden (`[lezer+0xF0]`, sprongtabel `0x601970`), die `bngrw.dll`
aanroept: `BngRwInit` (`0x771C7D`, in de init), `BngRwAttach` (`0x601426`, `0x602D5B`, `0x603210`; lezer 0 = `COM4`, lezer 1 = `COM2`), `DevReset`, `SetLedPower`, `ReqLed`, `ReqBeep`, `ReqWaitTouch`, `ReqCancel`, `ReqSendUrlTo`, `IsCmdExec`.
Elke aanvraag wacht op een callback (cdecl: lezer, resultaat, user) die het resultaatslot `{resultaat, lezer}` vult vanaf `{-1, 2}`; `-301` (`0xFFFFFED3`) of `-400` (`0xFFFFFE70`) is een fout die het spel met code `0x43/0x45`
(`0x6095C0`) logt en de "lezer aanwezig"-vlaggen `[0xEC2A08]+0x194/0x195` op 0 zet. **Dit is een component die onder de TP-kern anders werkt dan onder onze kern** (TP vervangt hem, wij niet), en hij zit vlak voor de stap die we missen: de echte `bngrw.dll` (in de spelmap; strings `SetCommState`, `SetCommMask`, `PurgeComm`) opent `COM4`/`COM2`, die onder Wine niet bestaan; de gesloten TP-kern vervangt de bibliotheek (Banapass-emulatie, kaartknop =
bit 0 van byte 8 van `TeknoParrot_JvsState`). Of het spel zonder lezer vastloopt of na een time-out (~30 s: de tweede bookkeeping-schrijfactie) naar een foutscherm gaat, is niet gemeten. **Niet bewezen** dat dit het zwarte beeld verklaart; wel
dat het de eerste ontbrekende emulatie op het pad naar de IO-controle is, maar de volgorde van de opstartcontroles (kaartlezer eerst, IO daarna) is uit de code afgeleid en niet bewezen.

**Gebouwd: ZEVENDE TAAK van `rg-mfcam.dll` (`bngrw_fake.h`, `rg_mfcam.cpp`).** `RG_BNGRW=trace` zet de elf `bngrw.dll`-importen om naar logger-omhulsels rond de echte functies (argumenten, uitkomst, en elke callback die de
bibliotheek geeft, met het resultaat dat het spel te zien krijgt); `RG_BNGRW=fake` geeft zelf antwoord: twee lezers die er zijn en geen kaart hebben (Attach/DevReset/ReqLed/ReqBeep/ReqSendUrlTo slagen met callback-resultaat 0;
ReqWaitTouch blijft open tot ReqCancel, `IsCmdExec` = 1 zolang). Uit met de TP-kern (`TeknoParrot.dll` geladen). Getoetst: `tests/test_bngrw_fake.cpp` (21 controles) en een wegwerpprogramma onder Wine tegen een stub-`bngrw.dll` met de
echte exportnamen (`_BngRwAttach@24` enz.): beide modi, stapelcontrole na elke aanroep, 0 mislukt. Niet getoetst: het spel zelf. De terugkeerwaarde/het resultaat dat het spel na `ReqCancel` verwacht en wat het met een kaart wil
is bewust niet nagebouwd (alleen "geen kaart").

**Testrecept 1 (eerst, onschadelijk): trace.** Dropin als nu (`RG_BNGRW=trace`), `WINEDEBUG=-all,warn+seh`, `RG_STALL_DUMP=20,45,90`. In `rg-mfcam.log` regels `bngrw: ...`: wat geeft de echte bibliotheek? `Attach(lezer 0, COM4,...) -> 1` en daarna een
`callback Attach(lezer 0): a=0 b=0x...`? Een `b=0xfffffed3` of geen callback = de lezer ontbreekt en het spel wacht/faalt daar. Blijft `IsCmdExec(lezer N) -> 1` staan, dan wacht het spel erop.
**Testrecept 2: fake.** `RG_BNGRW=fake` in de `Env`; kijk of `JVS: CreateFileA(COM3)` verschijnt (dan is de lezer de blokkade geweest) en of er beeld komt; zo niet: de nieuwe stalldump.
**Testrecept 3: TP-basislijn (alleen als 1 en 2 niets geven).** Zelfde dropin, `Core = teknoparrot` (de eigenaar heeft TP), `WINEDEBUG=-all,warn+seh`, `RG_STALL_DUMP=10,20,30` (de stalldump werkt ook onder TP; ShareK/JVS/drive/bngrw staan
dan uit). Kijk (a) in welke volgorde de `*INF*`-regels van het spel na `Startup ShareK-Client.` komen en op welke seconde de eerste daarna staat, (b) of de hoofdthread bij 10/20 s ook in `0x83D7D6` staat of al in een scène zit, (c) de tijd tot de camera-regel en de 500
effect-aanroepen in `rg-mfcam.log`. Het eerste verschil in de `*INF*`-volgorde wijst de ontbrekende stap aan.

### Mario Kart DX: de kaartlezer wordt niet bereikt; recept voor de TP-basislijn (08:38-meting)

**Meting 08:38 (`RG_BNGRW=fake`).** `bngrw: Init (nep)` staat 2,4 s na het laden van de dll, 7 ms voor de ShareK-regel (`BngRwInit` zit in de init op `0x771C7D`, dus de init loopt tot en met ShareK door). Daarna **geen enkele** andere `bngrw:`-regel:
de kaartlezer-toestandsmachine (`0x601550`) wordt dus niet aangeroepen, en de hoofdthread staat bij 20 s en 45 s onveranderd in `PeekMessageW` (`0x83D7D6`, stapel identiek aan die van 07:42). Dat maakt de kaartlezer **niet** de blokkade: hij is
een stap die pas aan de beurt komt als iets eerder in de opstartvolgorde gebeurt. Wat dat is, staat niet in onze code (de TP-kern is gesloten) en is statisch niet af te lezen zonder te weten welke taak de scheduler (`0x8451E0`) hoort te krijgen.
Daarom de TP-basislijn: dezelfde game, dezelfde dropin, met de gesloten kern.

**Recept (gebruikerslaag `~/.local/share/ravengun/patches/MarioKartDX/game.ini`, naast wat er al staat: `[Config] Network/PlayerId` en `[Loader] Core = teknoparrot`).** Voeg toe:

```ini
[Wine]
Debug = -all,warn+seh

[Env]
RG_STALL_DUMP = 10,20,30,45
```

* `Core = teknoparrot` (staat er al) is de basislijn; laat `RG_SHAREK_FAKE`, `RG_DRIVE_FIXUP`, `RG_BNGRW` en `RG_JVS_COMPORT` in de dropin staan: `rg-mfcam.dll` zet ze zelf uit zodra `TeknoParrot.dll` in het proces staat (logregels `... uit: de TeknoParrot-kern is geladen`). De stalldump draait wel.
* Zet **niets anders** (geen `RG_BNGRW`, geen andere dropin-wijziging) en gebruik dezelfde dll (`0e3b6e5d...`) en dezelfde view; de TP-run moet precies de eigen run zijn met alleen de kern gewisseld.
* Dubbelklik in de app, 50 s laten lopen, stoppen. De eigenaar speelt hier online: het spel mag verbinding maken.
* Daarna de gebruikerslaag terugzetten (de twee regels `Debug` en `RG_STALL_DUMP` weg).

**Wat te sturen** (twee runs: eigen kern zoals 08:38, en TP):
1. De spelmeldingen in volgorde: `tools/dev/game-log-order.py <logboek-eigen> <logboek-TP>` (decodeert Shift-JIS, toont de gemeenschappelijke kop en het eerste verschil; zonder tweede argument alle meldingen van een run). Het eerste verschil is de eerste stap die onder TP wel gebeurt.
2. `rg-mfcam.log` van de TP-run vanaf de laatste `rg-mfcam geladen`: de stalldumps bij 10/20/30/45 s (staat de hoofdthread nog in `0x83D7D6` of dieper in een scène?) en of `MFEnumDeviceSources`/de 500 `D3DX10CreateEffectFromFileA` verschijnen en op welke seconde.
3. Of er beeld is op 10 s, 20 s en 30 s (en wat).

**De keuze in de app (eigenaar 2026-10-17: "Keuze in de app").** `patches/MarioKartDX/game.ini` heeft nu `[Loader] Core = ravengun` als
standaard (zonder TeknoParrot, offline) en `options.json` een optie **Online play** (`ravengun.core`, `launch_env` `RG_LOADER_CORE`, kEnvMap
`[Loader] Core`; Dolphin-model: de app schrijft de keuze in de gebruikerslaag): `ravengun` (standaard) of `teknoparrot`. De tekst van de optie
zegt dat online spelen TeknoParrot vraagt (de map `TeknoParrotUi-2.0.0.130-linux-x64` in de runtime van RavenGun). Kiest iemand `teknoparrot`
zonder die map, dan weigert de start met "kern ontbreekt: ...TeknoParrot.dll" (gemeten met `rg-direct --plan` zonder map). Met de gesloten kern
zet `rg-mfcam.dll` zijn eigen JVS-omleiding uit (`GetModuleHandle("TeknoParrot.dll")`), zodat de kern COM3 zelf doet zoals voor deze wijziging.
`--plan` gemeten: standaard `core: ravengun`, `RG_JVS_COMPORT=COM3`; met de override `core: teknoparrot` (of de weigering als de map ontbreekt).

**Test op de tv.** (1) kandidaat activeren (alleen nodig als de standaard hierboven niet gewijzigd is; Mario Kart heeft ze nu als echte `game.ini`). (2) dubbelklik. (3) `rg-mfcam.log`: "JVS-omleiding
aan: COM3 -> \\.\pipe\TeknoParrot_JVS", "JVS: CreateFileA(COM3) -> pipe ..., handvat ...", plus de bestaande camera- en d3dx10-regels.
(4) `ravengunctl` toont JVS-verkeer; stuur/gas/Item werken zoals voor. (5) wat doet het spel met de ALL.Net-controle: attract binnen ~1 min,
of netwerkfoutscherm? Bij een blokkade: eerst `Data/System/Config/NetConfig.csv` en `AMCUS/AMConfig.ini` bekijken (zie hieronder).
(6) 5 minuten attract/spel, `/proc/<pid>/maps` zonder `TeknoParrot.dll`.

**Als de ALL.Net-controle blokkeert (volgende stappen, niet gedaan).** `Data/System/Config/NetConfig.csv` heeft schakelaars
(`NET_USE_LAN`, `NET_USE_INTERNET`, `NET_IGNORE_ALLNET_MAINTENANCE`, `NET_DUMMY_SERIAL_NO`, `NET_SECURE_FLAG`; in de exe staan alleen
`NET_GAMESERVER_ADDRESS`, `NET_IGNORE_ALLNET_MAINTENANCE`, `NET_USE_SHAREK`, `NET_INTERNET_PORT`, `NET_GROUP` als sleutelnamen, dus
niet alle regels van dit bestand worden nog gelezen) en `DefaultConfig.csv` heeft `USE_MUCHA`. Dat zijn instellingen van het spel zelf,
in de view te zetten met `ViewCopies` + een bestandspatch.

---

## Time Crisis 5 (EmulationProfile `TimeCrisis5`, 64-bit UE3, Namco ES3X)

*Stand 2026-10-17 (tweede ronde): **gebouwd en op de host getoetst, niet gestart.** Kandidaat: `docs/research/namco/patches/TC5/`.*

**Statisch gemeten (`TimeCrisisGame-Win64-Shipping.exe`, 25.766.400 B, `objdump -d` van de hele `.text`).** De exe importeert `WINUSB.DLL`
(8 functies), `SETUPAPI.dll` (`SetupDiGetClassDevsW`, `EnumDeviceInterfaces`, `GetDeviceInterfaceDetailW`, `DestroyDeviceInfoList` en de
`CM_*`-functies), `DINPUT8`, `XINPUT1_3` en `hasp_windows_x64_105293.dll`. **Geen van beide exe's importeert `bngrw.dll` of `iDmacDrv64.dll`**; dat die
naast de runtime-lader staan was een eis van de gesloten kern.

**Alles wat het spel van het USIO-bord wil staat in een compact blok `0x140001ca0`..`0x1400037ea` plus `0x140a55130`..`0x140a57620`:**

| Wat | Waar en wat het doet |
|---|---|
| Bord openen | `0x1400032d0`: `SetupDiGetClassDevsW({E54BF304-66A3-476F-A456-C6DDAB8C7078}, flags 0x12)` -> `EnumDeviceInterfaces` (tot `ERROR_NO_MORE_ITEMS`) -> `GetDeviceInterfaceDetailW` (twee keer, `cbSize` 8) -> `CreateFileW(pad, 0xC0000000, deel 3, OPEN_EXISTING, FILE_FLAG_OVERLAPPED)` -> `0x140003490`. De GUID staat in `.rdata` op `0x1414a5ba8`. **Geen `CM_*` hierbij.** |
| Pijpen zoeken | `0x140003490`: `WinUsb_Initialize` -> `QueryDeviceInformation(DEVICE_SPEED)` (moet niet-nul geven) -> `QueryInterfaceSettings(0)` -> `QueryPipe` voor elk eindpunt: bulk (type 2) met bit 7 = IN, bulk zonder bit 7 = OUT, interrupt (type 3) met bit 7 optioneel. Slagen = IN en OUT gevonden. |
| Register lezen | `0x140002f90`: `WinUsb_WritePipe(OUT, {0x10,0x00,adres(16 LE),lengte(16 LE)})`, dan `WinUsb_ReadPipe(IN, lengte)`; beide met een OVERLAPPED en daarna **`WaitForSingleObject(hEvent, INFINITE)`**: de gebeurtenis moet dus gezet zijn. |
| Register schrijven | `0x1400030b0`: `WritePipe(OUT, {0x90, ~(adres>>8)&0xF0, adres, lengte})`, `WritePipe(OUT, gegevens)`, `ReadPipe(IN, 1 byte)` als bevestiging (inhoud niet gelezen). |
| Communicatiedraad | `0x1400026b0`, elke ~10 ms: lees `0x100` @`0x0000`, `0x80` @`0x1080`, schrijf `0x20` @`0x1420`; eenmalig `0x80` @`0x1800`. Status `0` van de drie overdrachten = verbonden (`HasConnected`/`UpdateUSIOForUE`). |
| Opstart (`0x140a553a0`) | foutwoord @`0x0002` moet `0` zijn (anders een foutscherm met eindeloze lus: back-up-, firmware-/IRM1-, geheugen- of systeemfout), schrijft `0x220` bytes @`0x2000`, zet woord @`0x0028` op `0x8173`. |
| Invoer (`UpdateUSIOForUE`, `0x140a56d70`) | schakelaarwoord @`0x1080`: het spel leest de bits **9, 10, 11, 14 en 23** (vijf knopvlaggen die de UE-invoer vullen) en bits 14 en 15 in de muntlogica (`0x140002900`). Muntstanden @`0x1090` en `0x1092`: 14 bits teller + 2 bits status. |
| Aim en trekker | **niet over het bord.** `UTCMouseInterfacePlayerInput` leest `GetCursorPos` + `ScreenToClient` (`0x140a20c50`..) en de muisknop-berichten. Het bord levert schakelaars, munten en uitgangen (UE-natives `OpenUSIOForUE`, `UpdateUSIOForUE`, `GetUSIOCounter`, `GetUSIOGOut`, `HasConnectedUSIOForUE`). |
| De `CM_*`-boomwandeling | **geen onderdeel van het bord.** `0x1400020b0` (aangeroepen vanuit `0x140a6b8e0`) loopt over de USB-hostcontrollers (GUID `{3ABF6F2D-71C4-462a-8A92-1E6861E6AF27}` = `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`), vraagt via `DeviceIoControl` 0x220424/0x220408 de driver-sleutel en rootnaam, zoekt met `CM_Locate_DevNodeW`/`CM_Get_Child/Sibling/Parent`/`CM_Get_DevNode_Registry_PropertyW(DRIVER)` het bijbehorende apparaat en zoekt een USB-apparaat op VID/PID (0x0B9A en 0x0C10, volgorde niet vastgesteld) waarvan de 12 tekens van een tekenreeks met een "verwachte" waarde (`0x1416f4330`/`0x1416f4350`, `*` = jokerteken) worden vergeleken. Uitkomst in `0x1416f39b8`: 0 gelijk, 1 ongelijk, 2 geen apparaat, 3 meer dan een. Het is een controle van de herkomst van het bord, niet het transport. |

**Gebouwd.**

* `tools/rg-winusb/usio_board.h` (+ `tests/test_usio_board.cpp`, 69 controles, ctest `ravengun_usio_board_tests`): het registerbeeld en het
  6-byte-protocol. `tools/rg-winusb/rg_winusb.cpp` -> **`winusb.dll` (x64)**: de 8 `WinUsb_*`-functies van het spel (+ 4 onschuldige extra's), met
  de OVERLAPPED-gebeurtenis die het spel afwacht. Bij het laden zet hij in de importtabel van het spel vier SETUPAPI-functies en
  `KERNEL32!CreateFileW` om; **alleen de GUID van het bord wordt nagebootst**, al het andere (ook de hostcontroller-GUID en dus alle `CM_*`) gaat
  ongemoeid naar de echte functies. Het "apparaatpad" is een verzonnen pad dat `CreateFileW` als gewone gebeurtenis teruggeeft.
* Waarom `winusb.dll` en geen preload-DLL: de exe importeert `WINUSB.DLL` rechtstreeks, dus `WINEDLLOVERRIDES=winusb=n` plus het bestand naast de
  exe laadt hem bij het starten; zijn `DllMain` draait volgens de laadvolgorde van Wine (eerst binden, dan de initialisatie van de afhankelijkheden) pas na het binden van de importtabel van de exe (verwacht, niet gemeten; het logboek zegt het). Dat laat `TP_PRELOAD_DLL` vrij.
* Aim en trekker: een draad in de DLL zet de assen uit `TeknoParrot_JvsState` (offset 12/16) om in een absolute muisbeweging (`SendInput`) en de
  trekker (bit 0x10) in de linkermuisknop. **GOK**: het spel leest zijn muis, dus dit komt in de buurt van wat de kern deed, maar of het
  vizier zo goed staat is niet gemeten. Uit te zetten met `[Env] RG_TC5_AIM = off` / `RG_TC5_TRIGGER = off`.
* Kandidaat `docs/research/namco/patches/TC5/`: `game.ini` met `[Loader] Build = ravengun`, `Core = ravengun` (de oude `PostStartMs = 3000`
  hoorde bij de gesloten kern), `dropin/dropin.ini` (HASP zoals voorheen + sectie `[winusb.dll]`, `To = .`, `DllOverride = native`),
  `dropin/winusb.dll` (gebouwd met `tools/dev/build-winusb.sh`; `*.dll` staat in `.gitignore`, bouw hem opnieuw). `rg-direct --plan` is `ok`,
  `core_source: ravengun`, nul verwijzingen naar de TeknoParrot-map (`tools/dev/namco-candidates-check.sh`).
* Het spel start via `rg-direct` **rechtstreeks** `TimeCrisisGame-Win64-Shipping.exe` (niet `RSLauncher.exe`); de schijfletterlaag en
  `nbamsavdat.dll` van de launcher spelen dus niet mee.

**Niet bewezen / gegokt (in volgorde van waarschijnlijkheid dat het de eerste tv-proef breekt).**

1. **De bitafbeelding van pedalen en gunknop.** Wat het spel leest is gemeten (bits 9/10/11/14/23), wat elke bit fysiek is niet. Vastgesteld uit de
   Point Blank X-bibliotheek van dezelfde USIO-familie: test `0x80`, service `0x4000`, enter `0x200`. **Gegokt:** linker pedaal `0x800`, rechter `0x400`,
   gunknop `0x800000`. De DLL logt elke verandering van het schakelaarwoord; `[Env] RG_TC5_BITS = pedal_l=0x400,pedal_r=0x800,...` verandert de
   toewijzing zonder herbouwen.
2. **HASP.** De drop-in `ravengun-hasp` heeft nog een leeg `memory`-beeld (`dongle.json`); op de gesloten kern werd hij nooit aangeroepen
   (de kern vangt hem af). Zonder die kern moet het spel hem wel gebruiken. Het logboek van de HASP-laag (`RAVENGUN_HASP_VERBOSE=1`, stderr van de lader)
   zegt welke aanroepen de dongle krijgt.
3. **De controle op herkomst van het bord** (`0x1400020b0`). Wine geeft voor de hostcontroller-GUID geen controllers (verwachting, niet gemeten), dus
   de uitkomst is "geen apparaat" (2). Wat het spel daarmee doet (alleen de testmenu-regel, of ook een foutscherm) is niet vastgesteld. Als het een
   blokkade is, is het volgende werk een nagebootste hostcontroller met `DeviceIoControl`-antwoorden; dat is bewust niet gebouwd (het is een controle op
   de identiteit van het bord en vraagt een besluit).
4. **Aim/trekker via `SendInput`** (zie boven) en of het spel de cursor elke tik naar het midden zet.
5. **Munt:** standaard blijft `CoinKeys = lshift,t` (Shift+T, op de gesloten kern gemeten). Het bord telt de munt-bit niet (`coin = false`) om dubbel
   tegoed te voorkomen; `RG_TC5_BITS = coin=1` zet de teller @`0x1090` aan.
6. De bevestigingsbyte van een schrijfopdracht (nu `0x00`) en de waarden van het register `0x0000..0x00FF` (nu nul; alleen het foutwoord @2 is nodig).

**Test op de tv (exact).**

1. `cmake -S . -B build && cmake --build build -j` en `tools/dev/build-winusb.sh` (zet `winusb.dll` in `docs/research/namco/patches/TC5/dropin/`).
2. Kandidaat activeren: de inhoud van `docs/research/namco/patches/TC5/` (`game.ini`, `dropin/`) in de gebruikerslaag
   `~/.local/share/ravengun/patches/TC5/`, of controleren met `tools/dev/namco-candidates-check.sh`. Controle: `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan TC5 --patches <map>`
   -> `verdict ok`, `core_source ravengun`, `dropin_files` bevat `winusb.dll`.
3. Dubbelklik in de app. Daarna in `~/.local/share/ravengun/runtime/views/TC5/TC5/Binaries/Win64/rg-winusb.log`: **eerste regel** "rg-winusb (winusb.dll) geladen";
   vijf regels "importtabel: ... omgezet"; "SetupDiGetClassDevsW(USIO-GUID ...)", "CreateFileW(USIO-pad) -> handvat", "eerste aanroep: WinUsb_Initialize" ..
   "WinUsb_QueryPipe"; daarna "WinUsb_WritePipe"/"ReadPipe" en, na de eerste beweging, "invoer: schakelaarwoord ...". Geen log = het spel las het echte `winusb.dll`
   (controleer `WINEDLLOVERRIDES` in het plan en dat de view van TC5 gematerialiseerd is).
4. Bewijs van het laden: `grep -E 'winusb|TeknoParrot' /proc/<pid van TimeCrisisGame>/maps` toont `winusb.dll` met de sha256 van de drop-in en geen `TeknoParrot*.dll`.
5. Spel: attract -> munt (Move-knop, de daemon geeft Shift+T) -> "SELECT A GAME MODE". Bij de eerste aanwijzing: staat het vizier waar de Move wijst? Trekker, elk pedaal
   apart (kijk bij elke knop welke bit in het logboek verschijnt en wat het spel doet), 5 minuten spelen. Staat er een foutscherm (Japanse tekst "ファームウェアエラー"),
   dan heeft het spel het foutwoord niet nul gelezen: stuur het logboek.
6. Terug naar de gesloten kern: `[Loader] Build = teknoparrot` + `Core = teknoparrot` en de sectie `[winusb.dll]` weg.

---

## HOTD Scarlet Dawn (EmulationProfile `ALLSHOTDSD`, 64-bit UE4, twee exe's)

*Besluit eigenaar 2026-10-17: RavenGun mag de platformgegevens van `amdaemon.exe` zelf leveren.*
*Stand 2026-10-17 (bench 21:48): kandidaat via ravengund gestart, beide exe's laden onze kern en rg-allsio.dll. Wit beeld en fout 4105; NIET speelbaar. Kandidaat: `docs/research/namco/patches/HOTDSD/`.*

**Actuele meting.** `direct-HOTDSD-20261017-214823.log`: registerimport en preload slagen, de besturingspijp levert 64 bytes. `OSVersion` op de hoofdsleutel `HKLM\System\SEGA\SystemProperty` wordt via `RegQueryValueExA` gelezen (`0x14022edc0`), daarna als `%u_%u_%u` ontleed. De expliciet synthetische benchwaarde `1_0_0` verwijdert de bijbehorende fout. `amLibGetMainSerialId` (`0x140234030`) gebruikt `GetComputerNameA` en eist 15 tekens. `RG_ALLSIO_MAIN_ID=RAVENGUN0000001` haakt alleen de daemon-API aan; de hostnaam en gedeelde Wine-computernaam veranderen niet. De serial-id-fout verdwijnt. De validator verwerpt ongeldige waarden en behoudt dan de echte API; 179 protocol-/identiteitscontroles slagen.

**Resterende blokkades.** `amDongleInit -16`, `amHmInit -5`, ongeldige game-id en afhankelijke NVRAM-fouten blijven. De donglelaag opent `\??\FddDriver` (`0x14025e120`, `CreateFileW`, overlapped, lezen/schrijven); apparaatprotocol en hardwaremonitor zijn niet nagebouwd. Fout 4105 is hiermee nog niet exact aan één oorzaak gekoppeld. Het DLL-log bewijst hookinstallatie en pijpontvangst, niet dat HID-enumeratie of bordinitialisatie al is bereikt. Geen vijf minuten attract/spel en geen live bewijs met hernoemde TP-map. Tijdelijke actieve user-overrides en benchmuis zijn na de proeven uitgeschakeld; het werkende meegeleverde profiel is niet vervangen.

**Waarom `amdaemon.exe` zonder kern in 1 s stopt: gevonden (eerste ronde).** `am::platform::Manager::initialize()` (`amdaemon.exe` `0x1401e66a0`) roept
`amPlatformGetPlatformInformation()` (`0x14022eec0`) aan; die leest `HKLM\System\SEGA\SystemProperty\Static`, waarde **`PlatformId`** (4 tekens,
`ACA0` volgens `pxGetHwInfo.ini`). Ontbreekt die, dan volgt "ErrCode 0". `data/platform.reg` zet hem (`[Launch] Registry =`, mechanisme `docs/registry.md`).

**Het USB-IO-bord: nu in kaart gebracht (`amdaemon.exe`, libam `amUsbio` Rev 7255, `0x140230790`..`0x140233570`).**

| Wat | Gemeten |
|---|---|
| Zoeken | `0x14026f4a0`: `SetupDiGetClassDevsW(HID-GUID uit HidD_GetHidGuid, flags 0x12)`, `EnumDeviceInterfaces(index)`, `GetDeviceInterfaceDetailW`; eindigt met `ERROR_NO_MORE_ITEMS` -> `0x81000015`. `0x140232f80` zet elk pad in kleine letters en eist **`vid_0ca3`** (Sega). |
| Openen | `CreateFileW(pad, 0xC0000000, deel 3, OPEN_EXISTING, 0x40000080 = overlapped)`; `0x1402330d0`: `HidD_GetManufacturerString` moet met **"SEGA"** beginnen, `HidD_GetProductString` is de node-informatie. |
| Productstring (`ParseNodeInfo` `0x140231860`) | **7 velden gescheiden door `;`, dan `;` en een staart.** Velden: (0) naam, **exact `I/O CONTROL BD`** (< 16 tekens); (1) model < 9 tekens; (2) hex-byte; (3) hex-byte; (4) hex-woord; (5) tekst < 6 tekens; (6) hex-byte. Staart: items met `_` ertussen, `NAAM=waarde[,waarde]` in hex: `GOUT=n`, `PWMOUT=n`, `ADIN=n,m`, `ROTIN=n`, `COININ=n`, `SWIN=spelers,knoppen`, en `UQ1=`, `UQ2=`.. (commando + tot 3 parameters). `config.json` eist munt 2 sloten, schakelaars speler 2 met 8 knoppen, uitgang 20, eigen commando 65 met parameter 6. |
| Rapporten | 64 bytes. Verzoek: byte 0 = **0x10**, byte 1 = commando (0x01 time-out, 0x02 samplingtelling, 0x03 wis status, 0x04 algemene uitgang, 0x05 PWM, eigen UQ-commando's over de **interrupt**pijp: `WriteFile`/`ReadFile` overlapped; 0x84 modus, 0x85 firmware, 0x88 reset (arg 0xD9) over de **besturing**spijp: `HidD_SetOutputReport`/`HidD_GetInputReport`). Antwoord/ingang: byte 0 = **0x01**; ingang = 63 bytes erna. `RecvAcknowledge` (`0x140232c30`) is een gewone overlapped lezing; de daemon leest in een lus tot "nog niet klaar" (`GetOverlappedResult` = `ERROR_IO_INCOMPLETE`) en eist **minstens een rapport** per ronde (`0x1401eb813`). |
| Indeling van de ingang | volgens het bestaande AllsUsbIo-rapport (`packaging/control-pipe-profiles.json`): analoog 0-15, draaiknoppen 16-23, muntteller 24-27, schakelaars speler 1 28-29 en 2 30-31. |

**Gebouwd.**

* `tools/rg-allsio/allsio_proto.h` (+ `tests/test_allsio_proto.cpp`, 170 controles, ctest `ravengun_allsio_proto_tests`): de productstring (getoetst tegen de eisen
  hierboven), de rapportindeling, het afbeelden van de AllsUsbIo-pijp op het ingangsrapport.
  `tools/rg-allsio/rg_allsio.cpp` -> **`rg-allsio.dll` (x64)**: een preload-DLL die alleen in `amdaemon.exe` iets doet. Hij zet in de importtabel van de
  daemon `hid.dll` (5 `HidD_*`), `setupapi.dll` (4 functies; **alleen de HID-GUID**, de echte HID-apparaten van de machine blijven onzichtbaar) en `kernel32`
  (`CreateFileW` voor ons pad, `ReadFile`/`WriteFile`/`GetOverlappedResult`/`CancelIo` voor ons handvat) om. Een draad leest `\\.\pipe\TeknoParrotPipe` (de pijp
  die `rg-direct` al voedt, het AllsUsbIo-rapport); een tikdraad (2 ms) levert het nieuwste rapport aan de daemon (rapport-id 1 + pijpbytes 0..62).
* Kandidaat `docs/research/namco/patches/HOTDSD/`: `game.ini` (`[Loader] Core = ravengun`, zonder `RemoteThread =`: de standaardroute van de lader voor een eigen
  kern), `data/platform.reg`, `dropin/dropin.ini` (`[rg-allsio.dll]`, `To = .`, `Preload = true`) en `dropin/rg-allsio.dll` (`tools/dev/build-allsio.sh`; `*.dll` is
  genegeerd door git). `rg-direct --plan` is `ok`: kern `ravengun`, `TP_PRELOAD_DLL` naar `rg-allsio.dll`, starts `amdaemon.exe` en na 5 s `Hodzero-Win64-Shipping.exe`,
  nul verwijzingen naar de TeknoParrot-map (`tools/dev/namco-candidates-check.sh`).
* De twee-exe-start, de `AmDaemon`-antwoorder van `rg-direct` (voor de Ring-titels) en de schijfletters uit patch 0004 zijn ongewijzigd en niet nodig voor het bord.

**Niet bewezen / gegokt.**

1. **De tekst van de staart** (`GOUT=14_ADIN=8,E_ROTIN=4_COININ=2_SWIN=2,E_UQ1=41,6`): de syntax is uit de code gelezen, de waarden van `ADIN`/`ROTIN`/`SWIN` zijn afgeleid van de
   eisen in `config.json` en van wat ik van dit bord meen te weten. Of de daemon "gelijk" of "minstens" eist is niet gemeten.
2. **De antwoorden op de verzoeken.** Besturingsverzoeken krijgen `{0x01, commando, 0...}` terug; de interrupt-verzoeken (time-out, sampling, wis status, uitgangen) krijgen
   **geen** antwoord. Of de daemon op die antwoorden wacht is niet vastgesteld; het logboek laat elk verzoek zien (de eerste 24 bytes in hex).
3. **Byte-uitlijning van het ingangsrapport** (pijpbyte k op rapportbyte k+1). Als START/trekker verschoven reageren: `RG_ALLSIO_*` bevat daar nog geen knop voor; de logregel
   "eerste AllsUsbIo-rapport" en de daemon-log laten zien wat de daemon leest.
4. **Cadans.** De daemon wil per ronde minstens een rapport; bij een te lage periode (`RG_ALLSIO_PERIOD_MS`, standaard 2 ms) kan een ronde leeg zijn.
5. **De schijfletters AMFS/D/Y.** `HKLM\System\SEGA\SystemProperty\mount` wordt door de daemon zelf geschreven; of `<L>:\` van patch 0004 volstaat is niet vastgesteld
   (zonder kern raakte de daemon de mountlaag niet, A3g). Het eerste logboek zegt het: `amdaemon.log` (met `-l`).
6. De sleutelchip-/dongle-laag (`\??\FddDriver`) is in de bench van 21:48 een actuele blokkade: `amDongleInit -16`; de hardwaremonitor meldt daarnaast `amHmInit -5`. Geen van beide apparaatprotocollen is nagebouwd.

**Test op de tv (exact).**

1. `cmake -S . -B build && cmake --build build -j` en `tools/dev/build-allsio.sh`.
2. Kandidaat: de inhoud van `docs/research/namco/patches/HOTDSD/` (`game.ini`, `data/`, `dropin/`) in `~/.local/share/ravengun/patches/HOTDSD/`; controle
   `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan HOTDSD --patches <map>` of `tools/dev/namco-candidates-check.sh`.
3. Dubbelklik. In `~/.local/share/ravengun/runtime/views/HOTDSD/App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64/rg-allsio.log`: "rg-allsio geladen in amdaemon.exe";
   14 regels "importtabel: ... omgezet" (geen "NIET in de daemon" voor `hid.dll`/`setupapi.dll`); "SetupDiGetClassDevsW(HID-GUID", "CreateFileW(USB-IO-pad)", dan
   "verzoek #1 ..." (de eerste is waarschijnlijk 0x88 reset of een time-out), "besturingspijp ... geopend" en "eerste AllsUsbIo-rapport".
   In het daemon-logboek: `Process state: setup Game Mode` zonder `amw_platform_manager.cpp`-uitzondering, daarna `Process state: Advertise` en **geen** `UsbioNode`/`amUsbio`-fout.
4. Spel: START (Move), trekker, subtrekker, controllerkeuze, munt, test: werkt het zoals op de gesloten kern (8:44 min aaneen gemeten)? 5 minuten attract of spel.
   Bewijs van het laden: `/proc/<pid van amdaemon>/maps` bevat `rg-allsio.dll` en niet `TeknoParrot64.dll`.
5. Blijft de daemon bij het bord hangen: stuur `rg-allsio.log` en `amdaemon.log`; de verzoeken in het logboek zijn precies wat nog ontbreekt.

---

## Bestanden

| Pad | Wat |
|---|---|
| `tools/rg-usio/usio_model.h`, `rg_usio.cpp` | USIO-model (getoetst) en `bnusio.dll` |
| `tools/dev/build-usio.sh` | bouwt `bnusio.dll` in de mingw-container |
| `tools/rg-mfcam/jvs_com.h`, `rg_mfcam.cpp` | JVS-over-COM-logica (getoetst) en de derde taak van `rg-mfcam.dll` |
| `tools/dev/build-mfcam.sh` | kopieert nu ook `jvs_com.h` |
| `tests/test_usio_model.cpp`, `tests/test_jvs_com.cpp` | ctest `ravengun_usio_model_tests`, `ravengun_jvs_com_tests` |
| `patches/PointBlankX/`: `game.kandidaat.ini`, `dropin.kandidaat.d/`; `patches/MarioKartDX/` (nu de echte `game.ini`, `dropin/`, `options.json`); `patches/HOTDSD/game.kandidaat.ini` + `data/platform.reg` | kandidaat-`game.ini` + drop-ins (ravengun-patches, ongecommit) |
| `docs/research/namco/PointBlankX.game.ini`, `MarioKartDX.game.ini`, `HOTDSD.game.ini` | dezelfde `game.ini` in deze repo |
| `tools/rg-winusb/usio_board.h`, `rg_winusb.cpp`, `tools/dev/build-winusb.sh`, `tests/test_usio_board.cpp` | TC5: USIO-registerbeeld + 6-byte-protocol (getoetst) en `winusb.dll` (x64) |
| `tools/rg-allsio/allsio_proto.h`, `rg_allsio.cpp`, `tools/dev/build-allsio.sh`, `tests/test_allsio_proto.cpp` | HOTDSD: ALLS USB-IO-protocol (getoetst) en `rg-allsio.dll` (x64, preload) |
| `docs/research/namco/patches/TC5/`, `docs/research/namco/patches/HOTDSD/` | volledige kandidaten: `game.ini`, `dropin/` (met `dropin.ini`), HOTDSD ook `data/platform.reg`; de DLL's zijn door `.gitignore` uitgesloten en worden met de build-scripts gemaakt |
| `tools/dev/namco-candidates-check.sh` | legt de kandidaten over de echte patches-map (tijdelijke kopie) en maakt een `rg-direct --plan` zonder TeknoParrot-map |
