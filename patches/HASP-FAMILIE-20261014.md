# HASP-familie — vier onbehandelde titels, samengevat

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS, alleen gelezen. **Er is niets uitgevoerd** — geen spel, geen lader, geen
sandbox (de bench is bezet door een andere lane).

**IJking:** elke scanner is eerst op een bekend antwoord getest voordat een lege uitslag als bewijs
gold. Een eerste PE-lezer bleek de COFF-kop verkeerd te lezen; de ijking gaf onmiddellijk FAIL en de
lezer is gerepareerd en opnieuw getest (`ParrotPatcher.exe`, `TeknoParrotUi.dll`, de vendor-DLL, onze
eigen shim). Alle cijfers hieronder komen van ná die reparatie.

Per game staat het volledige dossier in `patches/<Game>/BESCHERMING-STATUS.md`.

---

## De tabel

| game | variant | wat het vraagt | wat we houden | wat ontbreekt | voorgestelde test |
|---|---|---|---|---|---|
| **Aliens Armageddon** | Aladdin/HASP-klassiek + `aksusbd`-daemon + NetHASP, **statisch in een 32-bits ELF** (`game`, sha256 `be7ebd1b…acf`). Geen `libhasp`, 0 hasp-dynsyms, 0 hasp-verplaatsingen. Strings: `ReInitAladdinDongles`, `classic_check_local_port: invalid HASP ID`, `/etc/init.d/aksusbd restart`, `nethasp.ini` | donglegeheugen `0xFFF0` (48 B) + `0xFFF2` (64 B), plus een **uitdaging→antwoordtafel van 14.674 antwoorden** (`hasp/`) voor `hasp_encrypt`/`hasp_decrypt`. `hhl_mem.dmp` ontleedt met de op Terminator geijkte regel: **CRC klopt**, landcode 0, serienummer 1018, kenmerken alle `00` (= aan) | **alles, in de spelmap zelf.** `hasp/` + `hhl_mem.dmp` staan er; `dongle_crypto_replay()` in `dongle.c` implementeert de regel al; `patches/Terminator/dongle.json` is het kant-en-klare voorbeeldformaat; Terminator's 8/8 is de positieve controle | **het aangrijpingspunt:** de aanroepen zijn statisch, dus `LD_PRELOAD` wordt niet bereikt (zelfde blokkade als Halo/Terminator/JP/Galaga). En: is de daemon-route (`/tmp/.aksusb`, UDP `:475`) nodig in plaats van de API-route? | `game` onder onze eigen lader met `RAVENGUN_HASP_VERBOSE=1` en `crypto_table` op AA's `hasp/`; **lees de treffers/missers op de antwoordtafel**. Controle: dezelfde run op Terminator (8/8 gemeten) |
| **Aliens Extermination** | HASP-klassiek (HASP4), **statisch in een 32-bits PE** (`aliens dehasped.exe`, 89,7 MB, sha256 `7124464671…eae`). **Geen** HASP-DLL-import (geijkt). Transporttabel in de exe: `HASPDOSDRV`, `HASPUT16.DLL`, `NETAPI32.DLL`+`Netbios`, `wsock32.dll`. **Daarnaast** directe USB-HID-opsomming (`hid.dll`, `setupapi.dll`) | een HASP4-antwoord via één van die transporten, en/of een HID-dongle die aan de opgesomde attributen voldoet. `dongle.tga` (512×512, 32 bpp) is het **"dongle ontbreekt"-scherm**, geen dongle-data | **weinig.** Alleen het route-inzicht uit `LICENTIE-NETHASP.md` (NetHASP over sockets → een schone licentie-/daemon, geen gamedpatch) en de protocolmeting van Terminator als hypothese | **de antwoorden zelf** — geen tafel, geen dump, geen bestand, nergens. Plus: welk transport werkelijk gebruikt wordt, waar de HID-VID/PID vandaan komt, en of HID de dongle of de IO-kaart is. `…exe.original` is **byte-identiek** → er is niets "dehasped" | één run met een waarnemer die **zowel** `connect`/`sendto`/`recvfrom` **als** `SetupDiEnumDeviceInterfaces`+`HidD_GetAttributes` (VID/PID) logt. Controle: dezelfde waarnemer op Terminator (AF_UNIX `/tmp/.aksusb` ×2 errno=2, dan UDP 127.0.0.1:475 + broadcast, 4×, geen antwoord) |
| **Time Crisis 5** | Sentinel LDK / **HASP-HL**, 64-bits PE, **statisch per ordinaal**. Vendor-runtime `hasp_windows_x64_105293.dll` = 788.992 B, sha256 `a399f93d…414` (staat 2×), plus `…106993.dll` sha256 `a68970eb…7bb`. **25 uitvoeren, ordinalbasis 1, alleen `KERNEL32` geïmporteerd** → de runtime is een **cliënt van de `hasplms`-service / NetHASP-UDP / een V2C-licentie** | game-exe op ordinalen **2, 15, 13, 14**; `RSLauncher.exe` op **14, 2, 13, 17, 3, 15** — **zes**, inclusief `hasp_write` en `hasp_encrypt` (correctie op de bestaande notitie, die 2/13/14/15 zegt). De **fileids/offsets/lengtes/bytes zijn onbekend** | de **drop-in shim bestaat al en laadt al** (`hasp_windows_x64_105293.dll` 126.967 B, sha256 `b6b408dc…8e9`) en heeft **exact hetzelfde ordinaalcontract** (gemeten: 25, basis 1, `@13 = hasp_login`). PE-route, `WINEDLLOVERRIDES` en de `hasp_probe.exe`-controle zijn bewezen | **de vraaglijst: welke `(fileid, offset, lengte)` het spel leest en wat er terug moet.** Staat in geen enkel bestand. Bovendien: of `RSLauncher`'s `hasp_write` moet blijven staan, en de `haspid`/features voor de XML | één run van de game-exe **zonder de TeknoParrot-kern** met de shim in sonde-modus (log elke `hasp_login`-scope, elke `(fileid, offset, lengte)`, elke encrypt/decrypt-lengte). Controle: dezelfde sonde op Halo → `greep 0xe6f`, `hasp_read(65524, 112 B)` = `2c 03 ab 63 2a 91 92 3f` |
| **Lost Land Adventure** | **Identiek aan TC5.** `hasp_windows_x64_106993.dll` = 788.992 B, sha256 `a68970eb…7bb` — **byte-identiek aan TC5's 106993**. Plus `haspdinst.exe` (11,6 MB, Sentinel-LDK-runtime-installer: `hasplms`, `aksusb`, NetHASP) | **drie** binaries: game-exe **2, 15, 13, 14**; `RSLauncher.exe` **3, 14, 13, 17** (**`hasp_write`**!); `PreprocessAmAuth.exe` **2, 14, 15, 13**. Shim moet in **twee** mappen. **Fileids/offsets/bytes onbekend** | alles van TC5, **ongewijzigd geldig** omdat de vendor-DLL byte-identiek is; `build-hasp.sh` maakt de naam al; de schrijf-naar-de-view-regel uit `patches/Terminator/dongle.json` is hier nodig | hetzelfde als TC5, **plus**: of `hasp_write` een **blijvende, schrijfbare** dongle vereist (een statisch beeld volstaat dan niet), en of de regio uit het licentieblok komt. **Let op:** `AMCUS/` (ALL.Net/MUCHA: `AMAuthd`, `muchacd` op tcp :12345) is een **tweede, onafhankelijke laag** — géén dongle (nagelopen: geen enkele HASP-import) | één run van `RSLauncher.exe` → game-exe met de sonde-shim in beide mappen, met **write-back-logging** (fileid, offset, lengte **en de weggeschreven bytes**). Controle: TC5's game-exe met dezelfde sonde (identieke vendor-DLL) + `hasp_probe.exe` op Halo |

---

## De grootste onbekende, in één zin

**Wat de games werkelijk vragen: de exacte `(fileid, offset, lengte)` → verwachte bytes van TC5 en
Lost Land Adventure.** Die staat in geen enkel bestand in het archief (geen `hasp/`, geen
`hhl_mem.dmp`, geen `nethasp.ini`, geen `.v2c`), en de enige route die hem zou hebben blootgelegd — de
gesloten TeknoParrot-kern — antwoordt eerder dan wij: **gemeten** dat onze DLL wél in
`/proc/<pid>/maps` van het spel staat met de juiste sha256, maar dat er **nul** regels van onze laag
komen. Voor Aliens Armageddon ligt het antwoord wél in de spelmap (14.674 antwoorden + een ontleedbaar
`hhl_mem.dmp`); voor Aliens Extermination ligt er niets. **TC5 en Lost Land Adventure zijn dus de enige
twee waar de vraag onbekend is én er niets in het archief staat om hem uit af te leiden — en één
sonde-run zonder de TeknoParrot-kern levert die vraag in één keer op.**

---

## Wat direct opvalt (kort)

1. **Aliens Armageddon is bijna klaar.** De antwoordtafel én het geheugenbeeld zitten in de spelmap, en
   `hhl_mem.dmp` ontleedt met de op Terminator geijkte regel: **CRC klopt**. Wat rest is het
   aangrijpingspunt, en dat is dezelfde PLT/GOT-koppeling die al voor vier andere titels gepland staat.
2. **`game` van Aliens Armageddon is géén dongle-patch.** Het verschilt op **4 bytes** van
   `game.original`: twee `mov dword`-immediates, `0x780`/`0x438` (1920×1080) tegen `0x550`/`0x300`
   (1360×768). Puur resolutie. De dongle is onaangeroerd.
3. **De "dehasped"-exe van Aliens Extermination is byte-identiek aan zijn `.original`.** Er is niets
   verwijderd; de `HASP`- en `dongle.tga`-verwijzingen staan er nog. Niet als voorgepatcht behandelen.
4. **TC5's `RSLauncher.exe` gebruikt zes ordinalen, niet vier** — inclusief `hasp_write` (17) en
   `hasp_encrypt` (3). De bestaande notitie in `patches/TC5/dropin/` is op dat punt onvolledig.
5. **De archiefstatus van TC5 spreekt zichzelf tegen, en beide zijn waar:** `playable` geldt voor de
   **TeknoParrot-route** (de gesloten kern beantwoordt de dongle), terwijl **onze eigen drop-in geladen
   maar werkloos** is en `scripts/hasp.py` de dongle als onvolledig meldt (`"memory": []`).
6. **`Sentinel` in de UE3-exe's van TC5 en LLA is een valse positief**: alle zeven treffers zijn
   `AAutoTestManagerexecDoSentinel_*`, Unreal-automatiseringsnamen.
7. **De vendor-runtime is géén driver.** `hasp_windows_x64_106993.dll` importeert alleen
   `KERNEL32.dll`; hij is een **cliënt** van de `hasplms`-service. Dat is goed nieuws: onze drop-in
   vervangt hem volledig en er is geen driver of service nodig.
8. **De runtime ondersteunt ook V2C-softlicenties** (`.v2c`, `license_manager hostname="localhost"`).
   Een V2C is leveranciersondertekend en dus niet na te maken, maar het is een tweede, onafhankelijke
   weg die het onderzoeken waard is — en het verklaart waarom de runtime een *lokale* licentiemanager
   zoekt.

---

## Gereedschap en herhaalbaarheid

Op de deck, in `/home/deck/hasp_work/`, read-only:

`hasp_measure.py`, `hasp_measure2.py`, `pelib.py` (de geijkte PE-lezer), `hasp_measure3.py`,
`hasp_measure4.py`, `hasp_measure5.py`; uitvoer in `d.out`, `e.out`, `f.out`, `g.out`, `h.out`.

De NAS is uitsluitend gelezen. `~/projects/ravengun` is niet gewijzigd.
