# Mario Kart Arcade GP DX — beschermingsstatus (AMCUS / ALL.Net + MUCHA)

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, daemon of sandbox gestart** (de bench
is bezet). Elke scanner is eerst geijkt — zie §5.

**Spel:** Mario Kart Arcade GP DX (Namco ES3, 32-bits Windows, D3D11).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Mario Kart DX/`
**Hoort bij:** [`../AMDAEMON-FAMILIE-20261014.md`](../AMDAEMON-FAMILIE-20261014.md)
**Let op:** dit is een stuurgame (stuur, pedalen, camera, kaartlezer), geen lightgun-titel.

**Kernbevinding:** de beschermingslaag is **AMCUS**: Namco's `AMAuthd` (ALL.Net-authenticatiedaemon)
met `muchacd` (MUCHA-kaartdaemon) erbovenop, TLS met `.pem`-certificaten, en géén dongle. De poort is
**netwerk**, niet lokaal. Deze laag staat **los** van de andere vier titels in deze lane, en het is geen
keychip-dongle.

---

## 1. De tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **Mario Kart Arcade GP DX** | Namco **AMCUS**: `AMAuthd` (ALL.Net) + `muchacd` (MUCHA). Géén dongle op deze laag | een **ALL.Net-authenticatieserver** over TCP met TLS: `amdcfg-authType=ALL.NET`, `allcfg-gameID=SBZB`, `auth_server_url`, `auth_server_sslcafile`, `auth_server_sslverify`, `auth_place_id`, `auth.ip4Addr`; statussen `ALE_ABEX_STAT_AUTH_OK/NG/TIMEOUT/BUSY`, `PrintAuthResponse`, `OnReceiveAuthResponse()`; MUCHA erbovenop met `cakey_mk3.pem` + `ImportCertificate.exe`; `amdcfg-writableConfig=WritableConfig.ini`; hervalidatie 07:00; timeouts 60 s | **nee.** TeknoParrot zet in `MKDX.xml`/`MKDXUSA.xml` de velden `Network/Gateway`, `Network/ServerAddress=164.132.246.154`, `Network/ServerPort=49200`, `Network/PlayerId` en laat zijn eigen kern naar een gemeenschapsserver wijzen. Ons eigen besluit (`docs/research/antwoorden-tp-ronde5-22.md` r.457): *"niet nabouwen"*. Ons `minime` kent `SBZB` niet in zijn startlijsten | de server aan de andere kant (ALL.Net + MUCHA), de certificaatketen en de kaartstapel. `packaging/game-options.json`: *"Needs ALL.Net authentication and card stack"*, `effort: hoog` | **geen benchrun voor de bescherming.** Eén daemonrun met `AMConfig.ini` in een **view-kopie** waarin `offlineMode`/`allcfg-authServerAddr` op een lokale waarde staat — meet of `AMAuthd` zonder server doorstart. Dat is één daemonrun, geen spelrun |

---

## 2. Wat er gemeten is

**MEASURED** — `file`/`objdump -p`:

| bestand | grootte | wat het is |
|---|---|---|
| `AMCUS/AMAuthd.exe` | 281.088 | PE32 i386 GUI; importeert `WS2_32`, `IPHLPAPI`, `ADVAPI32`, `KERNEL32`, `MSVCP100`, `MSVCR100`, `SHLWAPI`, `USER32`, `ole32` — **netwerk, geen dongle** |
| `AMCUS/AMUpdater.exe` | 108.544 | PE32 i386 GUI; `d3d9`, `d3dx9_43`, `GDI32` — update-tool met venster |
| `AMCUS/iauthdll.dll` | 9.728 | **COM in-proc server**: `DllCanUnloadNow`, `DllGetClassObject`, `DllRegisterServer`, `DllUnregisterServer`; `RPCRT4` |
| `AMCUS/MuchaBin/muchacd.exe` | 278.016 | MUCHA-kaartdaemon; `libcurl.dll`, `libeay32.dll`, `ssleay32.dll` (OpenSSL 1.0.x), `IPHLPAPI`, `WS2_32`, `WINMM` |
| `Mucha/muchacd-debug.exe` | 792.576 | debugversie |
| `Mucha/cakey_mk3.pem` | 1.651 | certificaat voor de MUCHA-verbinding |
| `ImportCertificate.exe` | 5.120 | PE32 **console, .NET** — zet dat certificaat in de Windows-certificaatopslag |
| `PreprocessAmAuth.exe` | 23.040 | bereidt de `AMAuthd`-config voor |
| `MK_AGP3_FINAL.exe` | 7.432.704 | het spel |
| `Data/DongleData/` | — | map die de game verwacht en die **leeg** is |

**MEASURED** — de scherpste regels uit `AMAuthd.exe` (`strings -a -n 5`, gefilterd):

```
INFO  : AMAuthd wakeup... Ver:%s.%s-%04d-%s-%s-%s
INFO  : Auth Server IP : %s
INFO  :   authType           = %s
MH INFO  :       auth_server_url        = %s
MH INFO  :       auth_server_sslcafile  = %s
MH INFO  :       auth_server_sslverify  = %s
MH INFO  :       auth_place_id          = %s
AH INFO  :     1auth.mode          = %d
AH INFO  :     auth.ip4Addr        = %08X (%d.%d.%d.%d)
AH INFO  : ALE_ABEX_STAT_AUTH_OK / _NG / _TIMEOUT / _BUSY
AH INFO  : PrintAuthResponse        /  OnReceiveAuthResponse() was called.
MH INFO  :       auth_store_router_ip   = %s
AH ERROR : ALE_ABEX_STAT_ERR_AUTH_COMMUNICATION / _AUTH_NG / _AUTH_TIMEOUT
```

Tellingen per merkteken: `Mucha` **309**, `muchacd` 4, `authd` 7, `iauth` 1, `dongle` 1, `region` 5,
`offline` 1, **`keychip` 0**, **`amDongle` 0**. Dat laatste is de inhoudelijke uitkomst: **deze daemon
vraagt geen keychip en geen dongle.**

**MEASURED** — `AMCUS/AMConfig.ini`:

```
[AMUpdaterConfig]        amucfg-title=COCO   amucfg-lang=JP   amucfg-game_rev=1
[AMAuthdConfig]          amdcfg-authType=ALL.NET   amdcfg-resoNameTimeout=180
                         amdcfg-writableConfig=WritableConfig.ini
[AllnetConfig]           allcfg-gameID=SBZB      allcfg-gameVer=1.10
                         ;allcfg-tenpoAddr=;  ;allcfg-authServerAddr=;   <- leeg/uitgecommentarieerd
[AllnetOptionRevalTime]  allopt-reval_hour=7
[AllnetOptionTimeout]    allopt-timeout_connect/send/recv=60000
[MuchaAppConfig]         …
```

Er is **geen `amdcfg-offlineMode`-sleutel** in deze versie, terwijl de LLA-versie van de binary die
string wél noemt en LLA `AMConfig_*Offline.ini` meelevert. **NIET BEPAALD** of dat een andere build is
of een andere configuratiesleutel — dat is statisch oplosbaar (vergelijk de twee `AMAuthd.exe`-bestanden
op die string en op hun configuratietabel).

**MEASURED** — de spelmap bevat verder `ShareK/ShareK.exe` (+`ipcsv.dll`, `Ionic.Zip.dll`),
`Nbam_QR_Code.dll`, `eOkao{Ag,Co,Dt,Gn,Pc,Pt}.dll` (gezichtsherkenning), `ipccl.dll`, `bngrw.dll`,
`D3D11Ref.dll` en `init.ps1`/`init_w7.ps1`/`init_w10.ps1`/`project*.ps1`/`disp_wait_page.ps1`/
`sharek.ps1` — opstartscripts die de processor uitlezen (`G2120` = System ES3, anders BNA1) om tussen
Windows 7 en Windows 10 te kiezen. Die scripts zijn de bron van de `ShareK`- en `project`-stappen en
staan **niet** in verband met de bescherming.

**MEASURED** — TeknoParrot-profielen: `MKDX.xml` (`EmulationProfile=NamcoMkdx`),
`MKDX118.xml` (idem), `MKDXUSA.xml` en `MKDXUSA106.xml` (`NamcoMkdxUsa`) — alle vier
`EmulatorType=TeknoParrot`, `ExecutableName=MK_AGP3_FINAL.exe`. `MKDX.xml` en `MKDX118.xml` hebben de
netwerkvelden `Gateway`, `ServerAddress` (`164.132.246.154`), `ServerPort` (`49200`) en `PlayerId`;
`MKDXUSA.xml` en `MKDXUSA106.xml` alleen `Gateway`. **Er is geen veld voor `AMAuthd`, `muchacd` of een
certificaat** — TeknoParrot start de daemon dus niet maar laat zijn kern het netwerkverkeer opvangen.

**MEASURED** — onze eigen administratie: `packaging/game-options.json` noemt de twee versies
`mkdx_1_10_22` (NAS-map `MKDX 1.10.22`, profiel `MKDX`) en `mkdx_1_18_16` (profiel `MKDX118`), platform
*"Namco ES3 (32-bit, Win7/Win10 embedded, D3D11, ALL.Net + Mucha)"*, `recommended_route: nieuw`,
`effort: hoog`, en de statusregel *"Needs ALL.Net authentication and card stack"*. Eerdere research staat
in `findings/mkdx_research.md` en `findings/mkdx_configs.md` op de NAS; `AMCUS/AMConfig.ini` is in beide
versies **byte-identiek** (OVERGENOMEN uit `docs/research/findings-digest.md`).

**MEASURED** — er is **geen patchmap** voor deze titel: `ls patches/*[Mm]ario*` is leeg. We houden dus
niets.

---

## 3. Wat deze laag níet is (belangrijk om niet te verwarren)

* **Geen keychip/dongle.** `keychip` = 0 en `amDongle` = 0 in `AMAuthd.exe`. De enige `dongle`-treffer
  is tekst in de foutwoordenschat, geen API.
* **Geen Sega `amdaemon`.** Dat is een ander programma van een andere maker met een andere poort
  (lokaal: `amPlatformGetPlatformInformation()`); zie
  [`../AMDAEMON-FAMILIE-20261014.md`](../AMDAEMON-FAMILIE-20261014.md) §5.
* **Geen lightgun- of JVS-vraag.** De AMCUS-laag gaat over authenticatie en kaarten.
* **`Data/DongleData/` is leeg.** Dat is een verwachte map van de game, geen dongle. Wat erin hoort is
  NIET BEPAALD.

---

## 4. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

De vraag is hier **niet** "hoe emuleren we de daemon", maar **"start het spel zonder hem"**.

1. **Statisch, nu mogelijk:** vergelijk de `AMAuthd.exe` van Mario Kart DX met die van Lost Land
   Adventure op (a) de `offlineMode`-string, (b) de configuratiesleutels, (c) de opbouw van
   `AMAuthdConfig`. Als de MK DX-versie geen offline-modus heeft, is de vraag of de game hem überhaupt
   nodig heeft des te belangrijker.
2. **Statisch:** zoek in `MK_AGP3_FINAL.exe` naar de client-kant — hoe belt de game de daemon, en wat
   gebeurt er als dat antwoord uitblijft? **IJking:** de `Aime_acceptConfirm`/`AMAuthd`-vermelding in
   `docs/research/kern-abi.md` is een bekend ijkpunt voor hoe TeknoParrot dit aanpakt.
3. **Dan één daemonrun** uit een **view-kopie** met een aangepaste `AMConfig.ini` (lokale
   `authServerAddr`, of `offlineMode=ENABLE` als de binary die sleutel kent) — meet of `AMAuthd`
   doorstart en wat hij logt. Dat is één daemonrun, geen spelrun, en het raakt de NAS niet.

**Positieve controle:** de run moet de regel `AMAuthd wakeup... Ver:…` en daarna `Initialize AMAuthd`
laten zien; zonder die regels is de daemon niet eens begonnen en zegt de uitkomst niets.
**Wat de hypothese zou weerleggen:** als `AMAuthd` met een lege server zonder fout doorgaat naar zijn
volgende fase, dan is de ALL.Net-verbinding voor deze titel **niet** de dichte deur en valt de hele
netwerklaag af als werk.

**Wat hier expliciet níet hoort:** proberen de ALL.Net- of MUCHA-server na te bouwen. Dat is onze eigen
staande beslissing (`docs/research/antwoorden-tp-ronde5-22.md` r.457: *"niet nabouwen"*), en
`packaging/game-options.json` zet deze titel al op `effort: hoog` met de netwerk- en kaartstapel als
reden.

---

## 5. Bewijsvoering — ijking

| scanner | ijkpunt | uitslag |
|---|---|---|
| `strings` + regex op PE's | `This program cannot be run in DOS mode` moet in de PE zitten | **PASS** — 1× |
| `strings`-dekking | ≥5-tekens-regels moeten talrijk genoeg zijn dat een leeg resultaat iets betekent | **PASS** — 2.114 regels in `AMAuthd.exe` |
| merktekenzoeker, positieve controle | `Mucha` moet ruim gevonden worden | **PASS** — 309 |
| merktekenzoeker, negatieve controle | `keychip` en `amDongle` | **PASS** — 0 en 0, en dat is hier de **inhoudelijke** uitkomst |
| `objdump -p` (PE-importlezer) | `AMAuthd.exe` moet `KERNEL32.dll` importeren | **PASS** — 9 DLL's, `KERNEL32.dll` erbij |
| `objdump -p`, tweede ijkpunt | de importlezer moet een bekende ongewone DLL kunnen tonen | **PASS** — bij LLA's `PreprocessAmAuth.exe` verscheen `hasp_windows_x64_106993.dll` + `SETUPAPI` |

**Gereedschap (op de deck, read-only):** `/home/deck/rgwork/s7_step2inv.sh`, `s8_daemon.sh`,
`s9_ourside.sh`, `s10_ourside2.sh`, `s11_findings.sh`; uitvoer in de bijbehorende `.out`-bestanden. Er
is niets op de NAS geschreven.
