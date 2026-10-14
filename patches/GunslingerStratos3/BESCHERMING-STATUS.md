# Gunslinger Stratos 3 — beschermingsstatus (NESYS)

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, daemon of sandbox gestart** (de bench
is bezet). Elke scanner is eerst geijkt — zie §5.

**Spel:** Gunslinger Stratos 3 (Taito, 64-bits Windows, Wwise-audio).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Gunslinger Stratos 3/`
**Hoort bij:** [`../AMDAEMON-FAMILIE-20261014.md`](../AMDAEMON-FAMILIE-20261014.md)

**Kernbevinding:** de beschermingslaag is **NESYS** (Taito's netwerk): `nesys_x64.dll` /
`nesysd_x64.dll` praten **HTTPS met een cliëntcertificaat** met `nesys.taito.co.jp`, met
certificaatbeheer via `CRYPT32`. TeknoParrot emuleert dat niet — het heeft één schakelaar,
**`RemoveNesysMessages`**, die de NESYS-meldingen op het scherm onderdrukt, en start `game.exe` direct.
Wij hebben die schakelaar niet.

---

## 1. De tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **Gunslinger Stratos 3** | Taito **NESYS** (`nesysd_x64.dll`, `nesys_x64.dll`) + NESiCA-kaartlezer (`felica_nfc_library.dll`, `nfc.dll`, `nfcd.dll`) | **HTTPS met cliëntcertificaat** naar de NESYS-server: `https://nesys.taito.co.jp:443/service/option/CardInfo.php` en `https://nesys.taito.co.jp/service/option/PlayInfo.php`; certificaatbeheer via `CRYPT32` (`CertOpenStore`, `CertFindCertificateInStore`, `CertGetNameStringA`); commando's `SCOMMAND_CERT_ERROR`, `SCOMMAND_CERT_INIT_NOTICE`, `SCOMMAND_CERT_REGULAR_NOTICE`; klassen `Cnesys`, `CNesysGameInfo`, `CnesysCardInfoInside`, `CnesysGlobalIpInside`, `CnesysNetworkInfoInside`, `CnesysPingInside`, `CnesysTenpoInside`, `CnesysUpConfigInside`, `nsfCert` | **nee, maar TeknoParrot omzeilt het met één vlag.** `Gunslinger.xml` heeft `RemoveNesysMessages` (`Bool`, standaard 0, *"On-screen Nesys messages not displayed."*) — dezelfde schakelaar als `Harikiri.xml` en `StreetFighterVTypeArcade*.xml`. Het profiel start `game.exe` direct; `nesysd` wordt **niet** gestart. Wij hebben geen equivalent | het hele NESYS-antwoord, óf dezelfde weg als TeknoParrot: de meldingen onderdrukken. Daarnaast is `resource/finalizedwindows/license.olf` (4.942 B) nog niet uitgelezen | eerst **statisch**: lees `license.olf` en de `nesica`/`Activate`-paden in `game.exe`. Daarna bepalen of onderdrukken genoeg is — dat is dezelfde vraag als Scarlet Dawns `GAME_MSG_SECURITY_CHALLENGE` |

---

## 2. Wat er gemeten is

**MEASURED** — `objdump -p`:

| bestand | grootte | importeert | uitvoer |
|---|---|---|---|
| `nesys_x64.dll` | 150.016 | `ADVAPI32`, **`CRYPT32`**, `KERNEL32`, `MSVCP100`, `MSVCR100`, `ole32` | `getCnesysInstance` |
| `nesysd_x64.dll` | 796.672 | `ADVAPI32`, **`CRYPT32`**, `KERNEL32`, `ole32` | `getCnesysInstance` |
| `game.exe` | 52.772.352 | — | — |
| `felica_nfc_library.dll` | 341.688 | — | — |
| `nfc.dll` / `nfcd.dll` | 15.360 / 64.000 | — | — |
| `ts3client_win64.dll` | 3.495.936 | — | — (TeamSpeak-cliënt voor de koptelefoons) |

**MEASURED** — de scherpste regels uit `nesysd_x64.dll`:

```
https://nesys.taito.co.jp:443/service/option/CardInfo.php
https://nesys.taito.co.jp/service/option/PlayInfo.php
CertOpenStore / CertFindCertificateInStore / CertFreeCertificateContext / CertGetNameStringA
SCOMMAND_CERT_ERROR / SCOMMAND_CERT_INIT_NOTICE / SCOMMAND_CERT_REGULAR_NOTICE
.?AVCnesys@@  .?AVCNesysGameInfo@@  .?AVCnesysCardInfoInside@@  .?AVCnesysGlobalIpInside@@
.?AVCnesysNetworkInfoInside@@  .?AVCnesysPingInside@@  .?AVCnesysTenpoInside@@  .?AVnsfCert@@
.?AVCDllNesys@@  .?AVCnesysEventData@@  .?AVCnesysUpConfigInside@@
F:\guns3\soft\nesys\nesys2010\x64\Debug\nesysd_x64.pdb
NESYS      nesys_games
```

Tellingen: `nesys` 37×, `cert` 15×, `certificate` 2×, `https://` 2×.

**MEASURED** — de scherpste regels uit `game.exe`: `CConnectNesysServerTestTask`, `CNesysServerTest`,
`CTaskNesysSender`, `CNesysCardReader`, `NESYS`, en aan de telzijde `nesica` 53× · `NESiCA` 53× ·
`Activate` 27× · `region` 24× · `license` 5× · `HASP` 4× · `olf` 2× · `offline` 4× · `/auth/token` ·
`authToken`.

**MEASURED** — `resource/finalizedwindows/license.olf`: **4.942 bytes**, `file -b` zegt `data`, de
eerste bytes geven de strings `flo` en `BILZ`. Het bestand is **niet uitgelezen** — dat is een open
punt.

**MEASURED** — `game.inf` (267 B) is **geen tekst**: de eerste bytes zijn hoog-entropisch
(`data`). Hetzelfde geldt voor `resource/finalizedwindows/license.olf`. Beide zijn dus
versleutelde/gecodeerde spelbestanden, geen configuratie.

**MEASURED** — TeknoParrot-profiel `Gunslinger.xml`: `EmulationProfile=GunslingerStratos3`,
`EmulatorType=TeknoParrot`, `ExecutableName=game.exe`, `Patreon=true`, en als enige relevante veld:

```
<CategoryName>General</CategoryName>
<FieldName>RemoveNesysMessages</FieldName>
<FieldValue>0</FieldValue>
<FieldType>Bool</FieldType>
<Hint>On-screen Nesys messages not displayed.</Hint>
```

Diezelfde schakelaar staat in `Harikiri.xml`, `StreetFighterVTypeArcade.xml` en
`StreetFighterVTypeArcade353.xml` (MEASURED: `grep -rn RemoveNesysMessages GameProfiles/*.xml` geeft
die vier bestanden). Het is dus een **gedeeld Taito-NESYS-antwoord**, geen Gunslinger-eigenaardigheid.
**TeknoParrot start de NESYS-daemon niet** en probeert de server niet na te bouwen; het onderdrukt de
melding.

**MEASURED** — onze eigen kant: er is **geen patchmap** voor deze titel en geen enkele vermelding van
`nesysd`, `nesys_x64`, `RemoveNesysMessages` of NESYS in onze scripts, docs of packaging. De enige
treffers op `nesys` in onze repo zijn CMake-logs (ruis: het woord komt in buildpaden voor). `F:\guns3\`
in het PDB-pad toont dat deze DLL uit een **debug-build** van de ontwikkelaar komt.

---

## 3. Wat deze laag níet is (belangrijk om niet te verwarren)

* **Geen `amdaemon` en geen AMCUS.** Drie verschillende daemons van drie makers; zie
  [`../AMDAEMON-FAMILIE-20261014.md`](../AMDAEMON-FAMILIE-20261014.md) §5.
* **Geen keychip/dongle in deze DLL's.** `nesys_x64.dll` en `nesysd_x64.dll` importeren `CRYPT32` voor
  **certificaten**, niet voor een dongle. `game.exe` noemt `HASP` 4× — dat is een **aparte, niet
  onderzochte** mogelijke laag in de game-exe zelf en verdient een eigen meting.
* **`license.olf` is geen licentiebestand van ons.** Het is speldata (4.942 B, `BILZ`-kop) en het is nog
  niet uitgelezen.

---

## 4. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

Alle stappen hier zijn **statisch en zonder bench**; er hoeft niets gestart te worden.

1. **Lees `license.olf` uit** (4.942 B). Is het ondertekend, en waarmee? `BILZ` doet denken aan een
   eigen containerformaat; `strings` en een hexdump zijn de eerste stap. **Dit is mijn eerste
   aanbeveling**, want het bepaalt of "meldingen onderdrukken" genoeg is of niet.
2. **Lees de `nesica`/`Activate`-paden in `game.exe`** (53 en 27 treffers). Roept de game de NESYS-DLL
   aan, of praat hij er rechtstreeks mee? En wat doet hij als het antwoord uitblijft?
3. **De `HASP`-vermeldingen in `game.exe`** (4×) nagaan. Als dat een echte dongle-laag is, is dat een
   **tweede** bescherming naast NESYS — precies zoals bij Lost Land Adventure. **IJking:** vergeleken
   met een bekend HASP-bestand (`Time Crisis 5/RSLauncher.exe`, waar `hasp_windows` letterlijk
   voorkomt). Een treffer van 4 in een 52 MB exe kan ook licentie-jargon zijn ("licensed under…") —
   dat is met de ijkzoektocht te scheiden.
4. **Vergelijk met `Harikiri.xml` en `StreetFighterVTypeArcade.xml`.** Als TeknoParrot daar met
   dezelfde vlag volstaat, is dat een sterk argument dat onderdrukken genoeg is — en dan is onze
   behoefte een **eigen equivalent van die vlag**, geen NESYS-emulatie.

**Positieve controle bij stap 3:** de HASP-zoektocht moet in `Time Crisis 5/RSLauncher.exe` wél
`hasp_windows` vinden en in `nesysd_x64.dll` niet — anders zoekt de scanner naar de verkeerde dingen.
**Wat de hypothese zou weerleggen:** blijkt `license.olf` ondertekend en verplicht, dan is onderdrukken
niet genoeg en moet er wél iets aan de serverkant gebeuren.

---

## 5. Bewijsvoering — ijking

| scanner | ijkpunt | uitslag |
|---|---|---|
| `strings` + regex op PE's | `This program cannot be run in DOS mode` moet in elke PE zitten | **PASS** — 1× in beide NESYS-DLL's |
| `strings`-dekking | ≥5-tekens-regels talrijk genoeg dat een leeg resultaat iets betekent | **PASS** — 2.567 in `nesysd_x64.dll`, 544 in `nesys_x64.dll` |
| merktekenzoeker, positieve controle | `nesys` moet ruim gevonden worden | **PASS** — 37× in beide |
| `objdump -p` (PE-importlezer) | de NESYS-DLL's moeten `KERNEL32.dll` **en** `CRYPT32.dll` importeren (de certificaatlaag is de kern van de vondst) | **PASS** — beide |
| `objdump -p`, tweede ijkpunt | de importlezer moet `getCnesysInstance` als uitvoer tonen | **PASS** — in beide DLL's |
| `file -b` | `game.inf` en `license.olf` mogen niet als "ASCII text" doorgaan als ze binair zijn | **PASS** — beide `data`, dus de conclusie "niet uitgelezen" is terecht |

**Gereedschap (op de deck, read-only):** `/home/deck/rgwork/s7_step2inv.sh`, `s8_daemon.sh`,
`s9_ourside.sh`, `s10_ourside2.sh`; uitvoer in de bijbehorende `.out`-bestanden. Er is niets op de NAS
geschreven.
