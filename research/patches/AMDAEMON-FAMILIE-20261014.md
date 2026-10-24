# De daemon-familie: wat de daemon werkelijk controleert, en of wij het al antwoorden

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS plus onze eigen repo. **Er is niets gestart** — geen spel, geen daemon, geen
sandbox (de bench is bezet). Elke scanner is eerst geijkt — zie §6.

**Titels:** Mario Kart Arcade GP DX · Gunslinger Stratos 3 · Lost Land Adventure · en het al
doorgemeten House of the Dead: Scarlet Dawn als ijkpunt.
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/<titel>/`

**Labels:** **MEASURED** = uit bestanden gelezen, commando erbij · **OVERGENOMEN** = gemeten in een
eerdere ronde en uit dat document geciteerd, door mij niet opnieuw gemeten · **INFERRED** ·
**NIET BEPAALD**.

---

## 1. Het antwoord in één alinea

Er is niet één daemon-familie; er zijn **drie verschillende daemons** onder één noemer, en dat is de
belangrijkste uitkomst van deze ronde. **House of the Dead: Scarlet Dawn** draait Sega's `amdaemon` en
diens poort is lokaal: `amPlatformGetPlatformInformation()`. **Mario Kart DX en Lost Land Adventure**
draaien Namco's `AMAuthd` onder de noemer **AMCUS**, en die poort is **netwerk**: een ALL.Net- en
MUCHA-authenticatieserver met TLS-certificaten. **Gunslinger Stratos 3** draait **NESYS** (`nesysd_x64.dll`)
en die poort is **HTTPS naar `nesys.taito.co.jp` met een cliëntcertificaat**. **Wij antwoorden geen van
drieën.** Wat TeknoParrot doet is per titel verschillend en nooit een emulatie van de daemon: bij
Gunslinger een schakelaar `RemoveNesysMessages` die de NESYS-meldingen op het scherm onderdrukt, bij
Mario Kart DX een serveradres in het profiel (`Network/ServerAddress`), bij Lost Land Adventure het
starten van `RSLauncher.exe` in plaats van de daemon. Voor Scarlet Dawn is het de geïnjecteerde kern.
Ons eigen besluit staat al opgeschreven: *"De sleutel `AmAuthPort` … hoort bij de netwerk- en
authenticatielaag: **niet nabouwen**"* (`docs/research/antwoorden-tp-ronde5-22.md` r.457).

---

## 2. De gevraagde tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **House of the Dead: Scarlet Dawn** *(ijkpunt, al gemeten 2026-10-13)* | Sega `amdaemon` 2437; poort is **lokaal** | `amPlatformGetPlatformInformation()` — platform-/apparaatinformatie (SetupAPI/CFGMGR32, `amSysData`, `amPlatformGetDevInfo`), vóór de mountfase | **nee.** De geïnjecteerde TeknoParrot-kern laat de daemon `Process state: Advertise` halen; zonder kern sterft hij na 1 s. De keychip/`amDongle`-laag wordt in géén arm aangeraakt | een eigen leverancier van de platforminformatie in onze kern (zelfde haaktabel als de schijflatter-omleiding), plus de twee-programma-start | run **met display**, langere limiet, netjes gestopt; daarna pas de sleutelchipvraag |
| **Mario Kart Arcade GP DX** | Namco **AMCUS**: `AMAuthd` (ALL.Net) + `muchacd` (MUCHA) — géén dongle op deze laag | een **ALL.Net-authenticatieserver** over TCP met TLS: `authType=ALL.NET`, `allcfg-gameID=SBZB`, `auth_server_url`, `auth_server_sslcafile`, `auth_server_sslverify`, `auth_place_id`, `auth.ip4Addr`; statussen `ALE_ABEX_STAT_AUTH_OK/NG/TIMEOUT/BUSY`; MUCHA erbovenop met `cakey_mk3.pem` + `ImportCertificate.exe` | **nee.** TeknoParrot zet `Network/ServerAddress=164.132.246.154`, `ServerPort=49200`, `Gateway`, `PlayerId` in `MKDX.xml`/`MKDXUSA.xml` en laat zijn eigen kern naar een gemeenschapsserver wijzen. Ons besluit: niet nabouwen. `packaging/game-options.json`: *"Needs ALL.Net authentication and card stack"*, `effort: hoog` | de server aan de andere kant (ALL.Net + MUCHA), de certificaatketen, en de kaartstapel; ons `minime` kent de spel-ID's `SBZB`/`SDBK` **niet** in zijn startlijsten | géén benchrun: het is een netwerk-/licentievraag, geen spelvraag. Wel: `AMConfig.ini` op `offline` zetten in een **view-kopie** en meten of `AMAuthd` dan zonder server doorstart — dat is één daemonrun, geen spelrun |
| **Gunslinger Stratos 3** | Taito **NESYS** (`nesysd_x64.dll`, `nesys_x64.dll`) + NESiCA-kaartlezer | **HTTPS met cliëntcertificaat** naar de NESYS-server: `https://nesys.taito.co.jp:443/service/option/CardInfo.php` en `.../PlayInfo.php`; certificaatbeheer via `CRYPT32` (`CertOpenStore`, `CertFindCertificateInStore`, `CertGetNameStringA`); commando's `SCOMMAND_CERT_ERROR/INIT_NOTICE/REGULAR_NOTICE`; klassen `Cnesys`, `CnesysCardInfoInside`, `CnesysNetworkInfoInside`, `CnesysTenpoInside`; daarnaast `felica_nfc_library.dll`, `nfc.dll`, `nfcd.dll` | **nee, maar TeknoParrot omzeilt het met één vlag.** `Gunslinger.xml` heeft `RemoveNesysMessages` (`Bool`, *"On-screen Nesys messages not displayed."*) — dezelfde schakelaar als bij `Harikiri.xml` en `StreetFighterVTypeArcade*.xml`. De daemon wordt **niet** gestart: het profiel start `game.exe` direct | het hele NESYS-antwoord, of dezelfde weg als TeknoParrot (de meldingen onderdrukken) in onze eigen kern; het `license.olf`-bestand in `resource/finalizedwindows/` is nog niet uitgelezen | eerst `license.olf` (4.942 B) en de `nesica`/`Activate`-paden in `game.exe` statisch uitlezen; daarna bepalen of onderdrukken genoeg is. Dat is dezelfde vraag als bij Scarlet Dawns `GAME_MSG_SECURITY_CHALLENGE` |
| **Lost Land Adventure** | **twee lagen:** (a) HASP/Aladdin-dongle —zie [`patches/LostLandAdventure/BESCHERMING-STATUS.md`](LostLandAdventure/BESCHERMING-STATUS.md); (b) Namco **AMCUS** ALL.Net + MUCHA | (b) `authType=ALL.NET`, `allcfg-gameID=SDBK`, `amdcfg-offlineMode=DISABLE`, MUCHA-server `https://lla-front.mucha-prd.nbgi-amnet.jp:10232/mucha_front/` met `sslcafile=.\for_mucha.pem`, `game_cd=LLA1`, `game_board_id=PCB`; `muchacd` luistert op `tcp:0.0.0.0:12345` en de cliënt verbindt met `tcp:127.0.0.1:12345`. `PreprocessAmAuth.exe` bouwt de config **met de HASP-dongle** (`hasp_windows_x64_106993.dll`) | **nee.** TeknoParrot start `RSLauncher.exe` (`LostLand.xml`, `EmulatorType=TeknoParrot`) — de lader, niet de daemon. Voor de dongle hebben we wél een shim; voor AMCUS niets | het ALL.Net/MUCHA-antwoord, en de wetenschap **of `PreprocessAmAuth` de dongle nodig heeft om de config te bouwen** (dat koppelt de twee lagen aan elkaar) | één daemonrun met de HASP-shim in sonde-modus en `AMConfig_EXDevOffline.ini` (die zet `offlineMode=ENABLE`): meet of `AMAuthd` zonder server doorstart en wat `PreprocessAmAuth` van de dongle leest |

---

## 3. Wat er per daemon gemeten is

### 3.1 Scarlet Dawn — `amdaemon` (OVERGENOMEN uit `patches/HOTDSD/`)

Uit [`AB-UITKOMST.md`](HOTDSD/AB-UITKOMST.md) en [`AB-PLATFORMCHECK.md`](HOTDSD/AB-PLATFORMCHECK.md),
ronde 2026-10-13, **daemon-only armen, geen spel voor de platformvraag**:

* direct gestart sterft de daemon na ~1 s op `amw_platform_manager.cpp` r.102,
  `am::platform::Manager::initialize`, `amPlatformGetPlatformInformation(). ErrCode 0.`;
* met de lader + **welke** DLL dan ook geïnjecteerd leeft hij ≥30 s;
* alleen met de TeknoParrot-kern raakt hij de mount-/appfile-laag (`AMFS` 95×, `appfile` 11×);
* **de keychip is in geen enkele arm aangeraakt** (`FddDriver` 0×, `amDongle`/`amyDongle` 0×), ook niet
  in de arm die de mountfase haalde;
* wat de daemon naar buiten doet (`docs/amdaemon-layer.md`): `amMasterInit/Update/GetResult`,
  `alpb*` (ALL.Net Payment/Billing), en hij importeert `winhttp.dll`, `ws2_32.dll`, `crypt32.dll`,
  `bcrypt.dll`;
* `AMDaemon/config.json` heeft **geen** `allnet*`-sectie; de daemon valideert `allnet_auth.enable`
  tegen `network.enable`.

**Betekenis voor deze kaart:** de poort van deze daemon is de **lokale platforminformatie**, niet een
server en niet een dongle. Het antwoord dat TeknoParrot levert is zijn geïnjecteerde kern.

### 3.2 Mario Kart DX — `AMCUS/AMAuthd.exe` + `AMCUS/MuchaBin/muchacd.exe`

**MEASURED** — `file`/`objdump -p`:

| bestand | grootte | wat het is |
|---|---|---|
| `AMCUS/AMAuthd.exe` | 281.088 | PE32 i386 GUI; importeert `WS2_32`, `IPHLPAPI`, `ADVAPI32`, `KERNEL32`, `MSVCP100`, `MSVCR100`, `SHLWAPI`, `USER32`, `ole32` — **netwerk, geen dongle** |
| `AMCUS/AMUpdater.exe` | 108.544 | PE32 i386 GUI; `d3d9`, `d3dx9_43`, `GDI32` — de update-tool met venster |
| `AMCUS/iauthdll.dll` | 9.728 | **COM in-proc server**: `DllCanUnloadNow`, `DllGetClassObject`, `DllRegisterServer`, `DllUnregisterServer`; importeert `RPCRT4` |
| `AMCUS/MuchaBin/muchacd.exe` | 278.016 | MUCHA-kaartdaemon; `libcurl.dll`, `libeay32.dll`, `ssleay32.dll` (OpenSSL 1.0.x), `IPHLPAPI`, `WS2_32`, `WINMM` |
| `Mucha/cakey_mk3.pem` | 1.651 | certificaat voor de MUCHA-verbinding |
| `ImportCertificate.exe` | 5.120 | PE32 **console, .NET** — zet dat certificaat in de Windows-certificaatopslag |
| `PreprocessAmAuth.exe` | 23.040 | bereidt de `AMAuthd`-config voor |

**MEASURED** — de scherpste regels uit `AMAuthd.exe`
(`strings -a -n 5 | grep -i -E 'cert|auth|much'`):

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
AH INFO  : PrintAuthResponse
MUCHA_ERROR_CA_AUTHENTICATION_DENIED        (alleen in de LLA-versie gezien)
```

**MEASURED** — `AMCUS/AMConfig.ini`:

```
[AMUpdaterConfig]  amucfg-title=COCO   amucfg-lang=JP   amucfg-game_rev=1
[AMAuthdConfig]    amdcfg-authType=ALL.NET   amdcfg-resoNameTimeout=180
                   amdcfg-writableConfig=WritableConfig.ini
[AllnetConfig]     allcfg-gameID=SBZB        allcfg-gameVer=1.10
                   ;allcfg-tenpoAddr=;  ;allcfg-authServerAddr=;     <- leeg/commented
[AllnetOptionRevalTime]  allopt-reval_hour=7
[AllnetOptionTimeout]    allopt-timeout_connect=60000 (idem send/recv)
[MuchaAppConfig]   …
```

Verder, **MEASURED**: `Mario Kart DX/Data/DongleData/` bestaat maar is **leeg**; de spelmap heeft ook
`ShareK/ShareK.exe` (met `ipcsv.dll`), `Nbam_QR_Code.dll`, `eOkao*.dll` (gezichtsherkenning),
`ipccl.dll` en `Dns`-onafhankelijke `init.ps1`/`init_w7.ps1`/`init_w10.ps1` die alleen een
processornaam (`G2120` / anders) uitlezen om de juiste initscript te kiezen. Van de AMCUS-laag is er
**geen `offlineMode`-sleutel** in de MK DX-versie van `AMConfig.ini`.

### 3.3 Gunslinger Stratos 3 — `nesys_x64.dll` / `nesysd_x64.dll`

**MEASURED** — `objdump -p`:

| bestand | grootte | importeert | uitvoer |
|---|---|---|---|
| `nesys_x64.dll` | 150.016 | `ADVAPI32`, **`CRYPT32`**, `KERNEL32`, `MSVCP100`, `MSVCR100`, `ole32` | `getCnesysInstance` |
| `nesysd_x64.dll` | 796.672 | `ADVAPI32`, **`CRYPT32`**, `KERNEL32`, `ole32` | `getCnesysInstance` |
| `game.exe` | 52.772.352 | (groot; `nesica`/`NESiCA` 53×, `license` 5×, `olf` 2×, `Activate` 27×, `HASP` 4×) | — |

**MEASURED** — de scherpste regels uit `nesysd_x64.dll`:

```
https://nesys.taito.co.jp:443/service/option/CardInfo.php
https://nesys.taito.co.jp/service/option/PlayInfo.php
CertOpenStore / CertFindCertificateInStore / CertFreeCertificateContext / CertGetNameStringA
SCOMMAND_CERT_ERROR / SCOMMAND_CERT_INIT_NOTICE / SCOMMAND_CERT_REGULAR_NOTICE
.?AVCnesys@@  .?AVCNesysGameInfo@@  .?AVCnesysCardInfoInside@@  .?AVCnesysGlobalIpInside@@
.?AVCnesysNetworkInfoInside@@  .?AVCnesysPingInside@@  .?AVCnesysTenpoInside@@  .?AVnsfCert@@
F:\guns3\soft\nesys\nesys2010\x64\Debug\nesysd_x64.pdb
NESYS      nesys_games
```

En uit `game.exe`: `CConnectNesysServerTestTask`, `CNesysServerTest`, `CTaskNesysSender`,
`CNesysCardReader` — de ontwikkelaarstaken die de NESYS-server testen. Plus `felica_nfc_library.dll`
(341.688), `nfc.dll`, `nfcd.dll` voor de kaartlezer, en `resource/finalizedwindows/license.olf`
(4.942 B, `data`, kop `flo`/`BILZ`).

**MEASURED, onze kant:** `Gunslinger.xml` — `EmulationProfile=GunslingerStratos3`,
`EmulatorType=TeknoParrot`, `ExecutableName=game.exe`, en één relevant veld:

```
<FieldName>RemoveNesysMessages</FieldName>  <FieldValue>0</FieldValue>  <FieldType>Bool</FieldType>
<Hint>On-screen Nesys messages not displayed.</Hint>
```

Diezelfde schakelaar staat in `Harikiri.xml` en `StreetFighterVTypeArcade.xml` /
`StreetFighterVTypeArcade353.xml` — het is dus een gedeeld Taito-NESYS-antwoord, geen
Gunslinger-eigenaardigheid. **Er is geen enkele `AmAuthPort`- of `nesysd`-start in ons eigen script- en
dossierbestand** (MEASURED: `grep -ril AMAuthd|AMUpdater|allnet|nesys|Mucha|amcus` over
`ravengun-patches` geeft alleen onze eigen verslagen, `patches/LuigisMansion/BESCHERMINGSLAAG.md`,
`patches/HASP-FAMILIE-20261014.md` en het bestaande LLA-bestand).

### 3.4 Lost Land Adventure — `AMCUS/AMAuthd.exe` + `PreprocessAmAuth.exe`

**MEASURED** — `objdump -p`:

| bestand | grootte | importeert |
|---|---|---|
| `AMCUS/AMAuthd.exe` | 294.400 | PE32+ x64; `WS2_32`, `ADVAPI32`, `KERNEL32`, `MSVCP100`, `MSVCR100`, `SHLWAPI`, `USER32`, `ole32` |
| `PreprocessAmAuth.exe` | 36.864 | PE32+ x64 console; `KERNEL32`, `MSVCP100`, `MSVCR100`, **`SETUPAPI`**, **`hasp_windows_x64_106993.dll`**, `ole32` |
| `AMCUS/iauthdll.dll` | 11.264 | COM in-proc server; `RPCRT4` |
| `bng_cert.pem` (spelwortel) | 3.637 | certificaat |
| `AMCUS/for_mucha.pem` | 1.678 | MUCHA-CA-certificaat |
| `haspdinst.exe` / `hasp_windows_x64_106993.dll` | 11.586.664 / 788.992 | Sentinel-LDK-installer en -runtime (identiek aan Time Crisis 5) |

**MEASURED** — `PreprocessAmAuth.exe` **importeert de HASP-runtime**. Dat is nieuw: de AMCUS-laag en de
dongle-laag zijn bij deze titel dus niet onafhankelijk — het voorbereiden van de authconfig gebeurt
**met de dongle erbij**. De rest van de AMCUS-details (MUCHA-server, `SDBK`, `offlineMode`, de
`AMConfig_*Dev*.ini`-varianten) staat in
[`patches/LostLandAdventure/BESCHERMING-STATUS.md`](LostLandAdventure/BESCHERMING-STATUS.md) §2b en is
daar met dezelfde methode gemeten.

---

## 4. Wat wij aan de serverkant al hebben — en waar het ophoudt

| wat | waar | dekt het deze titels? |
|---|---|---|
| `minime`: een zelfgehoste **ALL.Net/AiMe-server** in TypeScript (`POST /sys/servlet/PowerOn`, base64+zlib, `key=value` zonder escaping, antwoord in Shift-JIS; poorten 80 ALL.Net, 8443 billing-TLS, 22345 AiMeDB; namaak-PKI met `CN=ib.naominet.jp`) | `SegaTools/bin/minime/` op de NAS; beschreven in `ravengun/docs/research/amdaemon-minime.md` (OVERGENOMEN) | **deels.** Het kent in zijn startlijsten alleen `SDDF` (Initial D Zero), `SDBT` (Chunithm) en `SBZV` (Project Diva). **`SBZB` (Mario Kart DX) en `SDBK` (Lost Land Adventure) staan er niet in** — die twee vallen dus buiten de startlijst, net als `SDET` (Scarlet Dawn) |
| de `TFM`-shellprotocolbeschrijving (`GAME_MSG_SECURITY_CHALLENGE` → `SHELL_MSG_SECURITY_AUTHENTICATE`) | `findings/loaders/SegaShell/DOCS.md` + `BERICHTEN.md` (OVERGENOMEN) | **nee** — dat hoort bij de Ring-titels (Transformers, Men in Black), niet bij deze vier |
| ons besluit over de netwerk-/authenticatielaag | `docs/research/antwoorden-tp-ronde5-22.md` r.457 en r.497 (OVERGENOMEN) | **ja, als besluit:** *"niet nabouwen"* — tenzij een titel niet zonder start |
| de HASP-shim (25 uitvoeren, basis 1) | `ravengun/hasp/out/` | **ja voor LLA's donglelaag**, niet voor AMCUS |

**Wat dat betekent:** de serverkant is niet ons probleem zolang de vraag "start het spel zonder?" niet
met nee is beantwoord. Die vraag is bij deze drie titels **niet gemeten** — en dat is precies waarom
de voorgestelde proeven hierboven alle drie dezelfde vorm hebben: **zet de daemon in zijn eigen
offline-stand** (Mario Kart DX en LLA hebben daar configuratie voor; Gunslinger heeft de
`RemoveNesysMessages`-weg) en kijk of het spel doorstart. Dat is een daemon- of spelrun en mag pas op
een vrije bench.

---

## 5. Eén waarschuwing over de naam "AM-daemon"

`amdaemon` (Sega, Scarlet Dawn) en `AMAuthd`/AMCUS (Namco, Mario Kart DX en Lost Land Adventure) zijn
**verschillende programma's van verschillende makers** met verschillende poorten:

| | `amdaemon` | `AMAuthd`/AMCUS | `nesysd` |
|---|---|---|---|
| maker | Sega | Namco (AMCUS/MUCHA) | Taito (NESYS) |
| poort van de daemon | **lokaal**: `amPlatformGetPlatformInformation()` | **netwerk**: ALL.Net-server + MUCHA, TLS met `.pem` | **netwerk**: HTTPS + cliëntcertificaat naar `nesys.taito.co.jp` |
| sporen in de spelmap | `AMDaemon/`, `AMFS`, `appfile.dat` | `AMCUS/`, `Mucha/`, `MuchaBin/` | `nesys*_x64.dll`, `resource/finalizedwindows/license.olf` |
| wat TeknoParrot doet | kern injecteren | serveradres in het profiel | meldingen onderdrukken met een vlag |

Wie deze drie onder één "amdaemon-laag" schuift, bouwt één antwoord voor drie verschillende vragen.
Deze kaart houdt ze daarom gescheiden.

---

## 6. Bewijsvoering — ijking van de scanners

| scanner | ijkpunt | uitslag |
|---|---|---|
| `strings` + regex op PE's | `This program cannot be run in DOS mode` moet in elke PE zitten | **PASS** in alle onderzochte bestanden (1×) |
| `strings`-dekking | het aantal regels ≥5 tekens moet plausibel zijn (2.114 in `AMAuthd.exe` MK DX, 2.567 in `nesysd_x64.dll`) | **PASS** — beide groot genoeg dat een leeg resultaat iets betekent |
| merktekenzoeker, positieve controle | `Mucha` moet 309× in `AMAuthd.exe` (MK DX) zitten | **PASS** — 309 |
| merktekenzoeker, negatieve controle | `keychip` in dezelfde daemons | **PASS** — 0, en dat is hier de *inhoudelijke* uitkomst, niet een fout |
| `objdump -p` (PE-importlezer) | `AMAuthd.exe` moet `KERNEL32.dll` importeren | **PASS** — 9 DLL's, `KERNEL32.dll` erbij |
| `objdump -p`, tweede ijkpunt | `PreprocessAmAuth.exe` moet de HASP-runtime als import noemen (de reden dat we dit bestand onderzochten) | **PASS** — `hasp_windows_x64_106993.dll` + `SETUPAPI` |
| regressie-ijking op mijn eigen eerdere vondst | de merktekenzoeker mocht de `kswap`/`i2c`/`lbb`-treffers in de Windows-binary's **niet** als echt rapporteren | **PASS na weerlegging** — `VkSwapchain*`, `IXI2Client`, `uni2C7C`, `@lBBl@`, `…SDTVDONGLE`; zie [`ELF-SEGA-FAMILIE-20261014.md`](ELF-SEGA-FAMILIE-20261014.md) §5 |

**Gereedschap (op de deck, herhaalbaar, alles read-only):** `/home/deck/rgwork/s7_step2inv.sh`,
`s8_daemon.sh`, `s9_ourside.sh`, `s10_ourside2.sh`, `s11_findings.sh`, met uitvoer in de
bijbehorende `.out`-bestanden. Er is **niets op de NAS geschreven**, en er is geen spel, daemon of
sandbox gestart.

---

## 7. Wat open blijft

1. **Start elk van de drie zonder zijn netwerk?** Voor geen van de drie gemeten. Dit is de
   beslissende vraag: zolang het antwoord "ja" kan zijn, is de hele auth-laag voor ons een
   *configuratie*probleem en geen *emulatie*probleem.
2. **`license.olf` in Gunslinger Stratos 3** is niet uitgelezen (4.942 B, `data`). Als daar een
   ondertekende licentie in zit, is onderdrukken van de NESYS-meldingen misschien niet genoeg.
   **Statisch oplosbaar.**
3. **Of `AMAuthd` bij Mario Kart DX een offline-modus hééft.** De LLA-versie van de binary noemt
   `offlineMode` en LLA levert `AMConfig_*Offline.ini`; de MK DX-binary noemde die string niet in mijn
   meting en zijn `AMConfig.ini` heeft de sleutel niet. Dat kan twee dingen zijn — andere build, of
   een andere configuratiesleutel. **Statisch oplosbaar** door `AMAuthd.exe` van MK DX op de
   `offlineMode`-string en op de configuratiesleutels te vergelijken met die van LLA.
4. **`Data/DongleData/` is leeg bij Mario Kart DX.** Dat is een map die de game zelf verwacht. Wat
   daar hoort te staan is niet bepaald; het zou de MUCHA-/kaartkant kunnen zijn.
5. **De HASP-koppeling in `PreprocessAmAuth.exe`.** Het bestand importeert de dongle-runtime, maar
   *welke* fileid of welk veld het leest is niet bepaald. Dat is dezelfde leemte als in het LLA-dossier
   §4, en dezelfde sonde-run kan het antwoorden.
