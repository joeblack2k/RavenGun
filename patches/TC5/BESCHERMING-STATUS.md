# Time Crisis 5 — beschermingsstatus (HASP/Aladdin-dongle)

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS (alleen gelezen). Er is **niets uitgevoerd**. Elke scanner is eerst op een
bekend antwoord geijkt (zie §6).

**Spel:** `Time Crisis 5` (Namco ES3X, Unreal Engine 3, 64-bits) — **64-bits Windows PE**.
**Werkmap in het archief:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Time Crisis 5/`

**Samenvatting:** de variant en het *contract* zijn volledig gemeten, en onze drop-in bestaat al en
laadt al in het spelproces. Wat ontbreekt is één ding: **welke fileids/offsets/lengtes het spel
werkelijk leest en wat er terug moet.** Dat staat in geen enkel bestand in het archief.

---

## 1. Welke variant

**MEASURED** — **Sentinel LDK / HASP-HL, 64-bits Windows PE, in-process statisch per ordinaal
geïmporteerd**, uit een **geversioneerde vendor-runtime die zélf een service-/drivercliënt is**.

### De vendor-runtime

Twee builds, **beide 788.992 bytes**, en ze staan op **drie** plaatsen:

| pad | grootte | md5 | sha256 |
|---|---|---|---|
| `TC5/Binaries/Win64/hasp_windows_x64_105293.dll` | 788.992 | `095c1a6d8a8a7b6d3d81dc28e650eef2` | `a399f93d659164c3720b5b3b0c3d6b79f6023610cbbb7f31e0f46825276c3414` |
| `TC5/Binaries/Win64/hasp_windows_x64_106993.dll` | 788.992 | `4c05498f059d19cdff13f59395304b0a` | `a68970eba386013f6c9e359e76cd5f65a9aea49d98696067db8aab46003547bb` |
| `hasp_windows_x64_105293.dll` (spelwortel) | 788.992 | `095c1a6d8a8a7b6d3d81dc28e650eef2` | `a399f93d659164c3…276c3414` (identiek aan de eerste) |

Beide: PE64 (`machine 0x8664`), imagebase `0x02000000`, 6 secties, **25 benoemde uitvoeren,
ordinalbasis 1**, en — opmerkelijk — **importeert alleen `KERNEL32.dll` (49 functies)**. De runtime
bevat dus zelf geen dongle-logica maar is een cliënt van iets buiten het proces:

```
0x9313a  SYSTEM\CurrentControlSet\Services\hasplms\Parameters
0x93cea  Aladdin Knowledge Systems\HASP\Driver\Installer   ..  DrvPkgVersion
0xaa870  nethasp.ini
0x93cea  hlserver .. udp .. HLSERVER .. HLS_IPADDR .. 255.255.255.255
0x93e83  NH_TCPIP .. NH_SERVER_NAME .. NH_SEND_RCV .. NETHASP_00112233445566zz
0x93a4b  <haspscope><license_manager hostname="localhost"><hasp type="HASP-HL" /></license_manager>
0x93acb  <hasp type="HASP-SL" />   ..   v2c   ..   HASP_ALI_TIMEOUT
0x9426c  <nethasptype>unlimited</nethasptype> .. <haspid>%u</haspid> .. <hasphl/> <newintf/> <hardlock/>
```

**MEASURED, en dit is het variant-antwoord:** de runtime kan op **drie** manieren bevredigd worden —
(a) de **`hasplms`-service** (Sentinel License Manager) op deze machine, (b) een **NetHASP-server over
UDP** (`hlserver`, `HLSERVER`, broadcast `255.255.255.255`, `NH_TCPIP`), of (c) een **V2C-softlicentie**
(`v2c`). Lettend op `license_manager hostname="localhost"` zoekt hij standaard een **lokale**
licentiemanager.

### De volledige uitvoertabel — MEASURED (identiek voor beide builds)

```
@1  hasp_datetime_to_hasptime   @10 hasp_legacy_encrypt    @19 hasp_login_port
@2  hasp_decrypt                @11 hasp_legacy_set_idletime @20 hasp_enable_trace
@3  hasp_encrypt                @12 hasp_legacy_set_rtc    @21 hasp_login_ex
@4  hasp_free                   @13 hasp_login             @22 hasp_get_info
@5  hasp_get_rtc                @14 hasp_logout            @23 hasp_login_scope
@6  hasp_get_sessioninfo         @15 hasp_read              @24 hasp_get_version
@7  hasp_get_size               @16 hasp_update            @25 hasp_detach
@8  hasp_hasptime_to_datetime   @17 hasp_write
@9  hasp_legacy_decrypt         @18 hasp_get_trace
```

`haspdinst.exe` (de Sentinel-LDK-runtime-installer) staat **niet** in de TC5-map — alleen in Lost
Land Adventure. **MEASURED.**

---

## 2. Wat het spel vraagt — MEASURED tot op de ordinaal

### Het contract (dit is de harde kern)

| binary | grootte | sha256 | importeert `hasp_windows_x64_105293.dll` op ordinalen |
|---|---|---|---|
| `TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe` | 25.766.400 | `b2ac73e07c4a731eb50f4b09aa65269ff2642a3167fec0455fcfd7ec46bbf173` | **2, 15, 13, 14** = `hasp_decrypt`, `hasp_read`, `hasp_login`, `hasp_logout` |
| `RSLauncher.exe` (spelwortel) | 3.788.800 | `804b22daf8bad33a88f90bce758352bc006b182357b2083f254c04a81077ae75` | **14, 2, 13, 17, 3, 15** = `hasp_logout`, `hasp_decrypt`, `hasp_login`, **`hasp_write`**, **`hasp_encrypt`**, `hasp_read` |

Beide importeren **op ordinaal, zonder namen** (`names: []`).

> **CORRECTIE op de bestaande notitie.** `patches/TC5/dropin/dropin.ini` en
> `patches/TC5/dropin/ravengun-hasp/TC5/dongle.json` zeggen dat *beide* binaries op ordinalen
> **2, 13, 14 en 15** importeren. Voor de game-exe klopt dat; voor **`RSLauncher.exe` is het
> onvolledig** — die gebruikt er **zes**, inclusief **`hasp_write` (17)** en **`hasp_encrypt` (3)**.
> Onze shim levert alle 25 namen, dus de drop-in deugt nog steeds; maar wie `RSLauncher.exe` als
> maatstaf neemt, moet zes ordinalen verwachten, niet vier.

Beide exe's bevatten ook het **naamloze** literaal `hasp_windows_x64.dll` (2× elk; in de game-exe op
`0x17f3a0d` en `0x17f3a5f`, naast `Microsoft (R) LINK`). Dat is de **naam van het importlid in de
linker-stringtabel** (de `.lib`), geen tweede import. Onze build maakt dat bestand al, dus dit is
gedekt.

> **Valkuil, expliciet gemeten:** de zeven treffers op `Sentinel` in beide UE3-exe's zijn
> `AAutoTestManagerexecDoSentinel_*` — **Unreal-automatiseringsnamen, niet de Sentinel-dongle.**
> Niet als bewijs voor de dongle gebruiken.

### Wat er NIET is — MEASURED

Geen `hasp/`-map, geen antwoordtafel, geen `hhl_mem.dmp`, geen `nethasp.ini`, geen `.v2c`, geen
opgeslagen donglegegevens. De hele TC5-boom is doorzocht op namen met
`hasp`/`nethasp`/`aladdin`/`dongle`/`sentinel`/`aksusb` — er is **alleen**:

```
      788992  hasp_windows_x64_105293.dll
      788992  TC5/Binaries/Win64/hasp_windows_x64_105293.dll
      788992  TC5/Binaries/Win64/hasp_windows_x64_106993.dll
```

**Dus: de fileids, de offsets, de lengtes en de verwachte bytes zijn onbekend, en ze staan nergens in
het archief.**

### Waarom ze tot nu toe onvindbaar waren — MEASURED (eerder werk, `hasp-layer.md` §0 + ronde 35)

Onze drop-in **komt wél in het spelproces**: `hasp_windows_x64_105293.dll` staat in
`/proc/<pid>/maps` en dat bestand heeft de sha256 van onze drop-in (nagecontroleerd). En
`RAVENGUN_HASP_VERBOSE=1` bereikt het proces (het staat in `/proc/<pid>/environ`), en de stderr van
het spel is geen dode pijp (de leeskant is `fd 5` van `launch-direct.py` zelf). **Toch komt er nul
regels van onze DLL.** Conclusie: **de gesloten TeknoParrot-kern handelt de dongle af vóórdat wij aan
de beurt komen.** De fileids zijn dus pas te meten als die kern niet meer in de keten zit.

---

## 3. Wat we al hebben dat helpt

**Veel — dit is de best gedekte van de vier op de variant na.**

| wat | waar | dekt dit spel? |
|---|---|---|
| drop-in shim `hasp_windows_x64_105293.dll`, 126.967 B, sha256 `b6b408dcbf1a194771245a966de3feb6cdabd580ad531d8fe00601877649c8e9` | `ravengun/hasp/out/` | **ja** |
| drop-in shim `hasp_windows_x64_106993.dll`, 126.967 B, sha256 `d4356d501fad85dbd01b41f0a0504fa12ae84874e31bab5ea601447f15744b98` | `ravengun/hasp/out/` | **ja** |
| generieke `hasp_windows_x64.dll`, 126.967 B, sha256 `452cc70a21b1bb6be2a64dcbb8446b9cfe5e0b08afe9cc0a3c48903dab719dd6` | `ravengun/hasp/out/` | **ja** (dekt het naamloze literaal) |
| **het ordinaalcontract** | mijn meting: onze shim heeft 25 uitvoeren, basis 1, `@2/hasp_decrypt`, `@13/hasp_login`, `@14/hasp_logout`, `@15/hasp_read` — **exact** als de vendor-DLL | **ja, één-op-één** |
| de PE-injectieroute (`DllOverride = native` → `WINEDLLOVERRIDES`) | `hasp-layer.md` §4, `patches/TC5/dropin/dropin.ini` | **ja**, al ingericht (3 secties) |
| de bewezen werking van de Windows-kant | `hasp-layer.md` §0: `hasp_probe.exe` roept dezelfde vier ordinalen aan → greep `0xe6f`, `hasp_read(fileid 65524, 112 B)` eerste 8 bytes `2c 03 ab 63 2a 91 92 3f` | **ja** — dit is onze bestaande positieve controle |
| de vendor-DLL zélf (beide builds) staat op de share | archief | **ja** — we kunnen ons gedrag statisch tegen het echte vergelijken |

**Wat NIET overdraagt:**

* **Halo's dongle** (`haspid 748675309`, features `0`/`60`, keycaps, het 112-byte beeld voor
  `0xFFF4`). Andere dongle, andere kast.
* **Terminator's antwoordtafel en `hhl_mem.dmp`** — TC5 heeft geen antwoordtafel, en Terminator is een
  andere variant (Aladdin/aksusbd/NetHASP, 32-bits ELF). Alleen de *gedachte* "de game levert zijn
  donglegegevens zelf mee" draagt over — en bij TC5 is die niet uitgekomen.
* **De Linux-`.so`** — verkeerd platform.
* **De `hasp_get_info`-XML-waarden** zijn voor TC5 niet gemeten; de Halo-dossier laat zien dat dezelfde
  runtime twee verschillende `haspid`-waarden in twee templates heeft, dus gokken is gevaarlijk.

---

## 4. Wat ontbreekt

1. **De fileids, offsets, lengtes en verwachte bytes die het spel leest.** Dit is de hele resterende
   kloof, en het staat in **geen enkel bestand** in het archief.
2. **Of `RSLauncher.exe`'s `hasp_write` (17) en `hasp_encrypt` (3) een blijvend, schrijfbaar
   geheugenbestand eisen**, of dat het alleen een sonde is. (Bij Lost Land Adventure is dit nog
   scherper — zie dat dossier.)
3. **`haspid`, `features` en `keycaps`** voor de `hasp_get_info`-XML, als het spel die parseert. Een
   verkeerde `haspid` kan een spel laten weigeren ook al kloppen de antwoorden.
4. **Of de V2C-route bruikbaar is.** De runtime ondersteunt `v2c`-softlicenties; een V2C is
   leveranciersondertekend en dus niet na te maken, maar het verklaart waarom de runtime een
   `license_manager` op localhost zoekt — en het is een tweede, onafhankelijke weg die het
   onderzoeken waard is.

**Kosten:** laag. Punt 1 is **één run** met een sonde-shim (zie §5); geen nieuwe code aan de
emulator, alleen een logmodus. Punt 2 en 3 vallen in dezelfde run uit te lezen. Punt 4 is
documentatieonderzoek, geen bench.

### Contradictie in het archief — hier rechtgezet

`findings/INVENTARIS-dongle-per-game.md` r. 50 zet TC5 op **`playable`** met dongle `hasp` /
`drop-in`, terwijl `docs/hasp-layer.md` §7 en `patches/TC5/dropin/ravengun-hasp/TC5/dongle.json`
zeggen dat de drop-in **niet wordt aangeroepen** en de dongle-image **ontbreekt**. Beide zijn waar:

* `playable` geldt voor de **TeknoParrot-route** — daar beantwoordt de **gesloten kern** de dongle.
* **Onze eigen drop-in is geladen maar werkloos**, en `scripts/hasp.py` meldt de dongle van TC5 als
  **onvolledig** (`"memory": []`).

Dus: TC5 hangt voor de dongle nog volledig aan TeknoParrot, en dat is precies het laatste stuk dat
openstaat.

---

## 5. Voorgestelde test (NIET UITGEVOERD — de bench is bezet)

**Eén meting.** Start `TimeCrisisGame-Win64-Shipping.exe` **onder onze eigen lader** (dus zónder de
TeknoParrot-kern) met de drop-in shim in **sonde-modus**: dezelfde 25 uitvoeren op dezelfde ordinalen,
maar elke aanroep wordt gelogd en beantwoord met `HASP_STATUS_OK` plus een kanarie-buffer.

Wat gelogd moet worden:

* elke `hasp_login` — `vendor_code` en de `scope`-tekst;
* elke `hasp_read` / `hasp_write` — **fileid, offset, lengte**;
* elke `hasp_encrypt` / `hasp_decrypt` — de lengte en de eerste bytes van de invoer;
* elke `hasp_get_info` / `hasp_get_sessioninfo` — welk `format`/`scope` wordt gevraagd.

**Wat de uitkomst ons geeft:** de **volledige vraaglijst** — een geordende lijst van
`(fileid, offset, lengte)`. Daaruit volgt direct hoe groot het donglegeheugen moet zijn en welke
fileids erin moeten. Dat is exact het stuk dat nu ontbreekt.

**Positieve controle:** dezelfde opzet op **Halo Fireteam**, waar hetzelfde pad end-to-end gemeten is:

```
[ok ] ordinaal 2 (hasp_decrypt) / 13 (hasp_login) / 14 (hasp_logout) / 15 (hasp_read)
[ok ] hasp_login geeft HASP_STATUS_OK (0)      greep: 0xe6f
[ok ] hasp_read(fileid 65524, 112 bytes) geeft HASP_STATUS_OK
      eerste 8 bytes: 2c 03 ab 63 2a 91 92 3f
```

Als de sonde op Halo die regels exact reproduceert (inclusief `fileid 65524`, `112 bytes`,
eerste 8 bytes `2c 03 ab 63 2a 91 92 3f`), dan is de meetketen betrouwbaar — en is een **andere**
fileidreeks bij TC5 een echte eigenschap van TC5 en geen artefact van het gereedschap.

**Wat een uitkomst zou weerleggen:** de sonde levert bij TC5 **nul** aanroepen, ook zonder de
TeknoParrot-kern → dan is onze DLL niet de DLL die het spel laadt (naam/pad/ordinaal fout) en moet
eerst het laadpad kloppen voordat de fileids zin hebben.

---

## 6. Bewijsvoering — hoe de scanners zijn geijkt

| scanner | ijkpunt | uitslag |
|---|---|---|
| PE-importlezer | `RSLauncher.exe` van TC5 moet `KERNEL32.dll` importeren | **PASS** — 19 DLL's, `KERNEL32.dll` gevonden |
| PE-exportlezer | onze shim `hasp_windows_x64_105293.dll` moet 25 uitvoeren op basis 1 hebben, `@13 = hasp_login` | **PASS** — 25, basis 1, `@2 hasp_decrypt`, `@13 hasp_login`, `@14 hasp_logout`, `@15 hasp_read` |
| stringzoeker | het literaal `hasp_windows_x64_105293.dll` moet in `TimeCrisisGame-Win64-Shipping.exe` staan, en een verzonnen naam niet | **PASS** — precies 1 treffer op `0x16818d8`; `ZZZABSENTZZZ` → 0 |
| PE-lezer, tweede ijkpunt | `ParrotPatcher.exe` (64-bits) moet `KERNEL32.dll` (54 functies), `USER32.dll` en `ADVAPI32.dll` met **namen** tonen | **PASS** |

**Teruggetrokken tijdens dit werk:** de eerste PE-lezer las de COFF-kop verkeerd
(`SizeOfOptionalHeader` op offset 12 in plaats van 16), waardoor de sectietabel verschoof en de
importlezer onzin gaf. **De ijking ving dat onmiddellijk** — C1 en C2 gaven FAIL. Alle uitslagen in
dit document komen van ná de reparatie, en de gerepareerde lezer is daarna nog eens los getest op vier
bekende Windows-binaries (`ParrotPatcher.exe`, `TeknoParrotUi.dll`, de vendor-DLL en onze shim).

**Gereedschap (op de deck, herhaalbaar):**
`/home/deck/hasp_work/hasp_measure.py`, `hasp_measure2.py`, `pelib.py`, `hasp_measure3.py`,
`hasp_measure5.py`; uitvoer in `d.out` … `h.out`. Alles read-only; er is geen spel, lader of sandbox
gestart.
