# Lost Land Adventure — beschermingsstatus (HASP/Aladdin-dongle)

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS (alleen gelezen). Er is **niets uitgevoerd**. Elke scanner is eerst op een
bekend antwoord geijkt (zie §6).

**Spel:** `Lost Land Adventure` (Namco ES3X, Unreal Engine 3, 64-bits, 2014) — **64-bits Windows PE**.
**Werkmap in het archief:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Lost Land Adventure/`

**Kernbevinding:** de dongle-runtime van dit spel is **byte-identiek aan die van Time Crisis 5**
(sha256 `a68970eba386013f…`). Alles wat voor TC5 geldt, geldt hier ongewijzigd. Er is één verschil dat
de moeite waard is: hier gebruikt **`RSLauncher.exe` ook `hasp_write`**, wat betekent dat er
waarschijnlijk een **schrijfbaar, blijvend** donglegeheugen nodig is. En er is een **tweede,
onafhankelijke beveiligingslaag** (`AMCUS/` = ALL.Net/MUCHA) die niets met de dongle te maken heeft.

---

## 1. Welke variant

**MEASURED** — **Sentinel LDK / HASP-HL, 64-bits Windows PE, in-process statisch per ordinaal
geïmporteerd**, uit **exact dezelfde vendor-runtime als Time Crisis 5**.

| pad | grootte | md5 | sha256 |
|---|---|---|---|
| `LLA/Binaries/Win64/hasp_windows_x64_106993.dll` | 788.992 | `4c05498f059d19cdff13f59395304b0a` | `a68970eba386013f6c9e359e76cd5f65a9aea49d98696067db8aab46003547bb` |
| `hasp_windows_x64_106993.dll` (spelwortel) | 788.992 | `4c05498f059d19cdff13f59395304b0a` | `a68970eba386013f…` (identiek) |

Deze sha256 is **gelijk aan** `Time Crisis 5/TC5/Binaries/Win64/hasp_windows_x64_106993.dll` en aan
`Time Crisis 5/TC5/Binaries/Win64/…`. Meting: PE64 (`machine 0x8664`), imagebase `0x02000000`,
6 secties, **25 benoemde uitvoeren, ordinalbasis 1**, importeert **alleen `KERNEL32.dll` (49 functies)**,
en dezelfde interne teksten als bij TC5 (`SYSTEM\CurrentControlSet\Services\hasplms\Parameters`,
`Aladdin Knowledge Systems\HASP\Driver\Installer`, `nethasp.ini`, `hlserver`/`HLSERVER`/`HLS_IPADDR`,
`NH_TCPIP`, `<haspscope><license_manager hostname="localhost">`, `<hasp type="HASP-HL" />`,
`<hasp type="HASP-SL" />`, `v2c`, `<haspid>%u</haspid>`).

**Betekenis:** de runtime is een cliënt van de **`hasplms`-service** en/of een **NetHASP-server over
UDP** en/of een **V2C-softlicentie**. De volledige uitvoertabel is dezelfde 25 namen op basis 1
(`@2 hasp_decrypt`, `@3 hasp_encrypt`, `@13 hasp_login`, `@14 hasp_logout`, `@15 hasp_read`,
`@17 hasp_write`, …, `@25 hasp_detach`); zie het TC5-dossier §1 voor de volledige lijst.

### De meegeleverde installer — MEASURED

`haspdinst.exe`, **11.586.664 bytes**, md5 `b5495e7d2d40ee185a998114816009a8`, sha256
`8ee6dec2508a49f68c0375a1868aca276ab08ffb123b444594da2bfc64958671`. PE**32** (`0x14c`), imagebase
`0x400000`, importeert alleen `USER32.dll` (29), `GDI32.dll` (7), `KERNEL32.dll` (80).

Stringtellingen in dat bestand — dit is de **Sentinel-LDK-runtime-installer**, niet iets wat het spel
aanroept:

```
hasp 260 · HASP 124 · hasp_login 4 · hasp_read 1 · hasp_windows 6 · hasplms 13 · haspdinst 17
aksusb 22 · Sentinel 19 · Dongle 8 · dongle 2 · nethasp.ini 1 · NetHASP 2
```

Hij installeert dus de `hasplms`-service, de legacy Aladdin-HASP-driver (`aksusb`) **en** NetHASP.
**MEASURED: `haspdinst.exe` staat niet in de TC5-map — alleen hier.**

---

## 2. Wat het spel vraagt — MEASURED tot op de ordinaal

### Het contract: **drie** binaries, in **twee** mappen

| binary | grootte | sha256 | importeert `hasp_windows_x64_106993.dll` op ordinalen |
|---|---|---|---|
| `LLA/Binaries/Win64/DomeShooterGame-Win64-Shipping.exe` | 25.463.808 | `ef7431c026d88d0868e1162b7b7a86a8ff718968d1d82d0d2b064c0fc1003e9a` | **2, 15, 13, 14** = `hasp_decrypt`, `hasp_read`, `hasp_login`, `hasp_logout` |
| `RSLauncher.exe` (spelwortel) | 3.652.096 | `e3f848d78af3a8f55a72d907f474f3fa74319b467af621714ac9d1bb8ea19728` | **3, 14, 13, 17** = **`hasp_encrypt`**, `hasp_logout`, `hasp_login`, **`hasp_write`** |
| `PreprocessAmAuth.exe` (spelwortel) | 36.864 | `7f68972ec0d306dfaf78d571dcc9e6660aea25b6e77a7fd586942e44fc9e92f0` | **2, 14, 15, 13** = `hasp_decrypt`, `hasp_logout`, `hasp_read`, `hasp_login` |

Alle drie importeren **op ordinaal, zonder namen** (`names: []`).

**Praktische consequentie (MEASURED):** de drop-in moet in **twee** mappen staan —
`LLA/Binaries/Win64/` (voor de game-exe) **en** de spellwortel (voor `RSLauncher.exe` en
`PreprocessAmAuth.exe`). Eén map is niet genoeg.

**Het belangrijkste verschil met TC5 — MEASURED:** `RSLauncher.exe` importeert **`hasp_write` (17)** en
**`hasp_encrypt` (3)**. `RSLauncher.exe` draait **vóór** de game. **INFERRED (sterk):** er moet dus
waarschijnlijk een **schrijfbaar en blijvend** donglegeheugen zijn: wat de launcher schrijft, moet de
game daarna terug kunnen lezen. Een statisch beeld zoals bij Halo is dan niet genoeg; de emulator moet
de schrijfactie in de kopie van de view bewaren (precies zoals `patches/Terminator/dongle.json` al
voorschrijft: *"de emulator schrijft dan in de kopie in de view, nooit in de patchmap"*).

> **Valkuil, expliciet gemeten:** de zeven treffers op `Sentinel` in de UE3-game-exe zijn
> `AAutoTestManagerexecDoSentinel_*` — **Unreal-automatiseringsnamen, niet de Sentinel-dongle.**

### Wat er NIET is — MEASURED

Geen `hasp/`-map, geen antwoordtafel, geen `hhl_mem.dmp`, geen `nethasp.ini`, geen `.v2c`, geen
opgeslagen donglegegevens. De hele LLA-boom is doorzocht op namen met
`hasp`/`nethasp`/`aladdin`/`dongle`/`sentinel`/`aksusb`; er is **alleen**:

```
      788992  hasp_windows_x64_106993.dll
    11586664  haspdinst.exe
      788992  LLA/Binaries/Win64/hasp_windows_x64_106993.dll
```

**Dus: de fileids, offsets, lengtes en verwachte bytes zijn onbekend, en ze staan nergens.**

---

## 2b. De tweede laag: `AMCUS/` is ALL.Net/MUCHA — **geen** dongle

Dit is belangrijk om niet te verwarren. Ik heb `AMCUS/` volledig nagelopen omdat mijn eigen
zoekpatroon (`nbam|iauth`) erop sloeg; **geen enkel bestand daar importeert iets van HASP.**

| bestand | grootte | wat het is (MEASURED) |
|---|---|---|
| `AMCUS/AMAuthd.exe` | 294.400 | ALL.Net-authenticatiedaemon; importeert `WS2_32`(26), `KERNEL32`(53), `MSVCP100`, `MSVCR100`, `ole32`, `SHLWAPI`, `USER32`, `ADVAPI32`; **geen HASP**. Strings: `AMAuth` 8, `muchacd` 4, `dongle` 1 |
| `AMCUS/MuchaBin/muchacd.exe` | 324.608 | MUCHA-kaartdaemon; `WS2_32`(24), `MSVCR90`, `WINMM`, `IPHLPAPI`, `libcurl.dll`, `USER32`, `GDI32`; **geen HASP** |
| `AMCUS/MuchaBin/muchacd-debug.exe` | 1.054.720 | debugversie; **0 imports** |
| `AMCUS/iauthdll.dll` | 11.264 | **COM in-proc server**: uitvoeren `@1 DllCanUnloadNow`, `@2 DllGetClassObject`, `@3 DllRegisterServer`, `@4 DllUnregisterServer`, **0 imports**, geen hasp-tekst |
| `AMCUS/MuchaBin/libcurl.dll`, `libeay32.dll`, `ssleay32.dll` | 317.952 / 1.653.760 / 346.112 | HTTPS-cliënt + OpenSSL 1.0.x voor de MUCHA-verbinding |
| `AMCUS/AMConfig*.ini` (7 stuks) | ~2.8 kB elk | configuratie; zie hieronder |
| `wajvio.dll` (spelwortel) | 122.880 | Namco **WAJ VIO**-printplaatstuurprogramma; 18 uitvoeren `WAJVClose`…`WAJVUseSio`; **geen HASP** |
| `nbamsavdat.dll`, `Nbam_QR_Code.dll`, `nbamMfCapture.dll` | 28.160 / 180.224 / 44.032 | Namco-hulpbibliotheken (save-data, QR-code, Media-Foundation-opname); **geen HASP** |

**MEASURED** uit `AMCUS/AMConfig.ini`:

```
[AMAuthdConfig]  amdcfg-authType=ALL.NET            amdcfg-writableConfig=C:\LLAWFiles\WritableConfig.ini
                 amdcfg-offlineMode=DISABLE
[AllnetConfig]   allcfg-gameID=SDBK                 allcfg-gameVer=1.00
[MuchaSysConfig] syscfg-daemon_listen=tcp:0.0.0.0:12345     syscfg-client_connect=tcp:127.0.0.1:12345
[MuchaCAConfig]  cacfg-game_cd=LLA1                 cacfg-game_board_id=PCB
                 cacfg-auth_server_url=https://lla-front.mucha-prd.nbgi-amnet.jp:10232/mucha_front/
                 cacfg-auth_server_sslverify=1          cacfg-auth_server_sslcafile=.\for_mucha.pem
```

`AMConfig_JPDevOffline.ini` en `AMConfig_EXDevOffline.ini` zetten `amdcfg-offlineMode=ENABLE` — er
bestaat dus een **offline-modus**, los van de dongle. **INFERRED:** dit is de netwerk-/kaartlicentie
(ALL.Net-respons, plaats- en winkelcode), een **tweede, onafhankelijk probleem**. Het mag niet met de
dongle vermengd worden en het is geen `hasp`-kwestie.

---

## 3. Wat we al hebben dat helpt

**Bijna alles op de variant na — en de variant is identiek aan TC5.**

| wat | waar | dekt dit spel? |
|---|---|---|
| onze shim `hasp_windows_x64_106993.dll`, 126.967 B, sha256 `d4356d501fad85dbd01b41f0a0504fa12ae84874e31bab5ea601447f15744b98` | `ravengun/hasp/out/` | **ja** — `build-hasp.sh` maakt precies deze naam al |
| generieke `hasp_windows_x64.dll`, 126.967 B, sha256 `452cc70a21b1bb6b…719dd6` | `ravengun/hasp/out/` | **ja** (voor het naamloze literaal) |
| **het ordinaalcontract** | onze shim: 25 uitvoeren, basis 1, `@2/@3/@13/@14/@15/@17` — **exact** als de vendor-DLL | **ja, één-op-één** |
| de PE-injectieroute (`DllOverride = native` → `WINEDLLOVERRIDES`) | `hasp-layer.md` §4 | **ja** |
| de bestaande positieve controle van de Windows-kant | `hasp-layer.md` §0: `hasp_probe.exe` → greep `0xe6f`, `hasp_read(65524, 112 B)` = `2c 03 ab 63 2a 91 92 3f` | **ja** |
| de schrijf-naar-de-view-regel | `patches/Terminator/dongle.json` (notitie) | **ja, en hier nodig** — `hasp_write` wordt gebruikt |
| **omdat de vendor-DLL byte-identiek is: elke meting aan TC5's shim geldt hier ongewijzigd** | — | **ja** |

**Wat NIET overdraagt:**

* TC5's dongle-inhoud — die is zelf nog onbekend (zie het TC5-dossier §4). LLA erft die leemte.
* Halo's `haspid`/features/keycaps en het 112-byte `0xFFF4`-beeld.
* Terminator's antwoordtafel en `hhl_mem.dmp` (andere variant, andere kast, 32-bits ELF).
* De Linux-`.so`.

---

## 4. Wat ontbreekt

1. **De fileids, offsets, lengtes en verwachte bytes** — dezelfde leemte als TC5, en nergens in het
   archief te vinden.
2. **Of `hasp_write` (ordinaal 17) een blijvende schrijfactie is** en welke fileid beschreven wordt.
   Als dat zo is, moet de emulator een muteerbaar beeld bijhouden in de view en niet een statisch
   bestand.
3. **Welke van de drie binaries de schrijfactie doet** — vermoedelijk `RSLauncher.exe` (die draait
   eerst).
4. **Of de regio uit het dongle-licentieblok komt.** Terminator haalt zijn landcode uit
   `0xFFF2+0x1c` (en toonde daarmee `01.25.00.UK` in het testmenu). LLA levert
   `Data/romVersions/romVersion_{ASIA,CHN,INT,JPN}_{Offline,Online}.txt` — er is dus een regio-begrip,
   maar **of dat uit de dongle komt is niet gemeten.**
5. **De ALL.Net/MUCHA-laag** is een apart, onopgelost probleem (§2b). Niet met de dongle vermengen.

**Kosten:** laag tot middel. Punt 1–3 vallen in **één sonde-run** (§5), dezelfde als voor TC5, met
write-back-logging. Punt 4 volgt uit dezelfde run. Punt 5 is een eigen onderzoekstraject.

---

## 5. Voorgestelde test (NIET UITGEVOERD — de bench is bezet)

**Eén meting.** Start `RSLauncher.exe` → `DomeShooterGame-Win64-Shipping.exe` **onder onze eigen
lader** met de shim in **sonde-modus**, geplaatst in **beide** mappen (`LLA/Binaries/Win64/` én de
spelwortel), en log:

* elke `hasp_login` — `vendor_code` en `scope`;
* elke `hasp_read` / `hasp_write` — **fileid, offset, lengte**, en bij `hasp_write` **de
  weggeschreven bytes zelf**;
* elke `hasp_encrypt` / `hasp_decrypt` — lengte en de eerste bytes van de invoer;
* elke `hasp_get_info` / `hasp_get_sessioninfo` — welk `format`/`scope`.

**Wat de uitkomst ons geeft:** twee dingen in één keer — de **vraaglijst**
`(fileid, offset, lengte)`, én de **schrijflijst**. Die tweede is het beslissende verschil met TC5: als
`hasp_write` voorkomt, weten we dat een statisch beeld niet volstaat en dat het donglegeheugen in de
view muteerbaar moet zijn.

**Positieve controle, twee trappen:**

1. **TC5's game-exe met dezelfde sonde.** De vendor-DLL van LLA is **byte-identiek** aan die van TC5
   (`a68970eba386013f…`), dus dezelfde sonde hoort op beide een plausibele reeks te geven. Krijgt TC5
   een reeks en LLA een andere, dan is dat verschil **echt** en geen artefact van het gereedschap.
2. **`hasp_probe.exe` tegen Halo's dongle** — de bestaande, onafhankelijk gemeten controle:
   ordinalen `2/13/14/15` ok, greep `0xe6f`, `hasp_read(fileid 65524, 112 bytes)` met eerste 8 bytes
   `2c 03 ab 63 2a 91 92 3f`. Reproduceert de sonde die regels, dan is de meetketen betrouwbaar.

**Wat een uitkomst zou weerleggen:** de sonde meldt `hasp_write` maar de game leest dezelfde fileid
daarna met **andere** bytes terug en gedraagt zich identiek aan een run zonder write-back → dan is de
schrijfactie een sonde en volstaat een statisch beeld alsnog.

---

## 6. Bewijsvoering — hoe de scanners zijn geijkt

| scanner | ijkpunt | uitslag |
|---|---|---|
| PE-importlezer | `RSLauncher.exe` van TC5 moet `KERNEL32.dll` importeren | **PASS** — 19 DLL's, `KERNEL32.dll` gevonden |
| PE-exportlezer | onze shim `hasp_windows_x64_105293.dll` moet 25 uitvoeren op basis 1 hebben | **PASS** — 25, basis 1, `@13 = hasp_login` |
| stringzoeker | `hasp_windows_x64_105293.dll` moet in `TimeCrisisGame-Win64-Shipping.exe` staan, een verzonnen naam niet | **PASS** — 1 treffer; controle → 0 |
| PE-lezer, tweede ijkpunt | `ParrotPatcher.exe` moet `KERNEL32.dll` met **namen** (54 functies) tonen | **PASS** |
| uitvoertabel-lezer | de vendor-`…106993.dll` en onze shim moeten **dezelfde** 25 namen op **dezelfde** ordinalen geven | **PASS** — 25 vs 25, identieke volgorde |

**Teruggetrokken tijdens dit werk:** de eerste PE-lezer las `SizeOfOptionalHeader` op offset 12 in
plaats van 16; de ijking gaf onmiddellijk FAIL en de uitslagen zijn pas ná reparatie gebruikt.

**Gereedschap (op de deck, herhaalbaar):** `/home/deck/hasp_work/hasp_measure.py`,
`hasp_measure2.py`, `pelib.py`, `hasp_measure3.py`, `hasp_measure5.py`; uitvoer in `d.out` … `h.out`.
Alles read-only; er is geen spel, lader of sandbox gestart.
