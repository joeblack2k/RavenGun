# Aliens Extermination — beschermingsstatus (HASP/Aladdin-dongle)

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS (alleen gelezen). Er is **niets uitgevoerd**. Elke scanner is eerst op een
bekend antwoord geijkt (zie §6).

**Spel:** `Aliens Extermination` (Global VR) — **32-bits Windows PE**, geen Linux-kant.
**Werkmap in het archief:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Aliens Extermination/`

Dit is de **duurste** van de vier: er staat in het archief vrijwel niets van de dongle in.

---

## 1. Welke variant

**MEASURED** — **HASP-klassiek (HASP4, de Aladdin-"HASP"-familie, niet HASP-HL), statisch in de game
gelinkt**, met vier mogelijke transporten, **plus een directe USB-HID-donglecontrole**.

De importtabel van de exe bevat **geen enkele** HASP-DLL:

```
aliens dehasped.exe : PE32 (machine 0x14c), imagebase 0x400000, 15 DLL-imports
DLLs: advapi32.dll(2), avifil32.dll(7), gdi32.dll(4), glu32.dll(1), hid.dll(4),
      kernel32.dll(59), msvfw32.dll(2), opengl32.dll(51), openal32.dll(23),
      setupapi.dll(4), user32.dll(6), winmm.dll(2), ws2_32.dll(1), alut.dll(2), glut32.dll(15)
hasp-matching: []          <-- LEEG, en de scanner is geijkt (C1/C2 PASS, §6)
```

| | |
|---|---|
| bestand | `DATA/aliens dehasped.exe` |
| grootte | **89.698.304 bytes** |
| md5 | `9ed286aafe474a16c07f7d62b5c06ecb` |
| sha256 | `71244646710b5dc73a9aa07af185cb400593266d6b546000e33bea312d1eeaee` |
| sha256 `…exe.original` | **identiek** — `71244646710b5dc7…eeaee` |

### De twee `HASP`-teksten zijn de transporttabel van de klassieke HASP-client — MEASURED

Beide staan in één doorlopende DLL-/API-naamtabel, op bestandsoffset `0x556f41c` en `0x55700dc`:

```
0x556f41c  UTUnRegister.KERNEL32.DLL.USER32.DLL.ADVAPI32.DLL.....HASPUT16.DLL.NETAPI32.DLL.Netbios
           .....wsock32.dll.............WSAStartup.....socket.....closesocket.....connec
0x55700dc  ......................................................HASPDOSDRV................
```

**INFERRED (sterk):** dit is de tabel met de transporten die de klassieke HASP-client achtereenvolgens
probeert:

| naam in de tabel | transport |
|---|---|
| `HASPDOSDRV` | de HASP-DOS-driver (16-bits real-mode) |
| `HASPUT16.DLL` | de 16-bits HASP-runtime |
| `NETAPI32.DLL` + `Netbios` | **NetHASP over NetBIOS** |
| `wsock32.dll` + `WSAStartup`/`socket`/`closesocket`/`connect` | **NetHASP over TCP/IP** |

Dat is dezelfde protocolfamilie die bij **Terminator Salvation** gemeten is (UDP-poort 475 =
HASP-licentiemanager, plus de AKS-USB-daemon). Bij AE is de **Windows**-variant van die client
gebruikt en die is **statisch meegecompileerd** — er is dus geen DLL om te vervangen.

### En daarnaast: directe USB-HID-opsomming — MEASURED

```
hid.dll (4):       HidP_GetCaps, HidD_GetPreparsedData, HidD_GetHidGuid, HidD_GetAttributes
setupapi.dll (4):  SetupDiGetClassDevsA, SetupDiEnumDeviceInterfaces,
                   SetupDiGetDeviceInterfaceDetailA, SetupDiDestroyDeviceInfoList
```

Die twee sets horen bij elkaar: HID-GUID ophalen, de interfaces opsommen, de details ophalen en dan
`HidD_GetAttributes` (dat VendorID/ProductID teruggeeft). **MAAR:** `VID_` komt **0 keer** voor in het
bestand en `USB` **1 keer** (in code, geen string). Er staat dus **geen hardgecodeerde VID/PID** in de
exe. **INFERRED:** de vergelijking gebeurt met waarden die elders vandaan komen (een bestand, het
register via `advapi32` `RegOpenKeyExA`/`RegQueryValueExA`, of een berekening). **Onbekend waar.**

**Open vraag die dit oproept:** is `hid.dll`/`setupapi.dll` de **dongle** (een HID-klasse dongle) of de
IO-kaart? Statisch is dat niet te scheiden. Dit is meetpunt 1 in §5.

---

## 2. Wat het spel vraagt

**MEASURED, en het antwoord is: het staat er niet.**

* **Geen** `hasp/`-map, **geen** antwoordtafel, **geen** `hhl_mem.dmp`, **geen** `nethasp.ini`,
  **geen** `.v2c`, **geen** opgeslagen donglegegevens — nergens in de hele spelmap.
* **Geen** bestand met een naam waarin `hasp`, `aladdin`, `sentinel` of `dongle` voorkomt, behalve
  `DATA/dongle.tga`.
* **Geen** tekst-, ini-, cfg- of logbestand in de spelmap dat `hasp`, `dongle`, `licen`, `Sentinel` of
  `Aladdin` noemt. (Geijkt: dezelfde zoekactie vindt die woorden wél in de stringtabel van de exe.)

### `DATA/dongle.tga` is het "dongle ontbreekt"-scherm — MEASURED

| | |
|---|---|
| grootte | 1.048.594 bytes |
| md5 / sha256 | `7b4457a8333cc728a979dd9b640b3575` / `d38e7ec2e44a0cef745ccdc0b5cd2305aa25eca2fa0588a43f481703c446b4da` |
| TGA-kop | type **2** (ongecomprimeerd true-color), **512×512**, **32 bpp** — 512·512·4 + 18 = 1.048.594 ✔ |

De verwijzing staat op bestandsoffset `0x466e08`, **midden in de TGA-lezer**, naast de foutteksten
`Pixel depth must be 24 or 32 bit`, `Must use a NO color palette`, `Only handling uncompressed`:

```
0x466e08  … ; dongle.tga .. DEFAULT . Pixel depth must be 24 or 32 bit . Must use a NO color
          palette . Only handling uncompressed …
```

**INFERRED (sterk):** `dongle.tga` wordt als **textuur** geladen en full-screen getekend zodra de
donglecontrole faalt. Dit is het zichtbare symptoom van de blokkade, niet de blokkade zelf.

### De "dehasped"-naam is misleidend — MEASURED

`aliens dehasped.exe` en `aliens dehasped.exe.original` zijn **byte-identiek** (zelfde md5 én sha256,
zelfde 15 imports, dezelfde `HASP`- en `dongle.tga`-verwijzingen). Er is dus **niets uit deze binary
verwijderd**. Ofwel de "dehasp" is nooit toegepast, ofwel hij was al aanwezig vóór de kopie. **De
pre-dehasp-versie staat niet in het archief.** Wie hierop voortbouwt moet dat weten: dit is *geen*
voorgepatchte binary.

### Wat de game zelfstart vertelt — MEASURED (klein, maar het is de officiële startregel)

`DATA/COMMANDLINE.txt`:

```
-noavi
-nooverload
-log
-win
-message
-full
```

`DATA/CONTROLS.txt` (179 bytes) is een bedieningslijstje (servicemenu `[5]`, menu `[9]`/`[0]`,
bevestigen `[6]`, start 1P/2P `[1]`/`[2]`, munten L-CTRL/L-SHIFT/L-ALT). Geen schakelaar die de
dongle uitzet.

De spelmap bevat ook `GVR_Tools/` (Global VR-gereedschap: `NetSet.exe`, `winkey.exe`,
`winkeyLight.exe`, `HideTaskBar.exe`, `winmsg.exe`, `finishMessage.exe`, `PQIHelper.exe`) en een
DivX-installer. **MEASURED: geen van die binaries importeert iets van HASP**; ze zijn hier niet de
route.

---

## 3. Wat we al hebben dat helpt

Eerlijk: **weinig, en het meeste is een route-*inzicht*, geen code.**

| wat | waar | dekt dit spel? |
|---|---|---|
| de route-analyse "NetHASP over sockets, dus de fix is een schone licentie-/daemon, geen gamedpatch" | `patches/Terminator/LICENTIE-NETHASP.md` | **deels** — AE gebruikt dezelfde protocolfamilie (NetBIOS/TCP), maar op Windows en statisch gelinkt |
| de protocolmeting van Terminator (AF_UNIX `/tmp/.aksusb`, UDP `:475`) | `LICENTIE-NETHASP.md` | **als hypothese** — bij AE niet gemeten; poort/protocol kunnen verschillen |
| de algemene les "statisch gelinkte HASP-aanroepen vragen een PLT/GOT- of code-koppeling" | `docs/hasp-layer.md` §4, `docs/loskoppelen-per-game.md` | **ja, maar zwaarder** — dit is een PE32, niet een ELF; de bestaande `ravengun-hasp.so` is nutteloos |
| de naamregel en `dongle_crypto_replay` | `hasp/dongle.h` r. 143–166 | **alleen als er ooit een API-koppeling komt** — er is geen client-DLL en geen antwoordtafel |
| de vendor-DLL-shim `hasp_windows_x64_*.dll` | `ravengun/hasp/out/` | **nee** — AE importeert geen DLL; het is HASP-klassiek, niet HASP-HL |
| de Linux-runtime `libhasp_linux_*.so` (zoals bij Jurassic Park / NERF) | spelmap Jurassic Park / NERF | **nee** — verkeerd platform én verkeerde API-generatie |

**Wat expliciet NIET overdraagt:**

* Terminator's antwoordtafel en `hhl_mem.dmp` (die horen bij een HASP-HL-dongle van een andere kast).
* De fileid-conventie `0xFFF0`/`0xFFF2`/`0xFFF4`/`0xFFF5` — dat is HASP-**HL**; AE's strings noemen de
  klassieke HASP/DOS/NetBIOS-stack, een andere API-generatie.
* De PE-drop-in-motor (`WINEDLLOVERRIDES`, `DllOverride = native`) — er is geen DLL om te overrulen.

---

## 4. Wat ontbreekt

1. **Welk transport werkelijk gebruikt wordt** — NetBIOS, TCP/IP (poort?), de DOS-driver, of de
   USB-HID-opsomming. Statisch niet te beslissen.
2. **De HASP4-uitdaging→antwoordantwoorden** (of het HASP4-geheugen). Er staat **niets** in het
   archief: geen tafel, geen dump, geen bestand. Dit is de kern van het probleem.
3. **Waar de HID-vergelijking haar VID/PID vandaan haalt** (register, bestand, berekening). Er staat
   geen letterlijke VID/PID in de exe.
4. **Of `hid.dll`/`setupapi.dll` de dongle of de IO-kaart betreft.**
5. **Een `-dehasp`-binary of een origineel om tegen te vergelijken** — we hebben alleen twee identieke
   kopieën.

**Kosten (de hoogste van de vier):**

* Punt 1 en 4 zijn **goedkoop**: één geïnstrumenteerde run (socket-waarnemer + HID-waarnemer), dezelfde
  techniek als `patches/Terminator/net-log.c`. Dat levert het protocol en de VID/PID.
* Punt 2 is **duur**: zonder antwoordtafel uit een echte kast moet óf een kast worden opgezocht, óf de
  controle statisch worden doorgerekend in een 85 MB statische binary (HASP4 gebruikt een geheime
  sleutel in de dongle; dat is precies wat we niet hebben). Dit is de enige van de vier games waar de
  data **niet** in het archief zit én er geen vendor-DLL is om te vervangen.

---

## 5. Voorgestelde test (NIET UITGEVOERD — de bench is bezet)

**Eén meting:** start `aliens dehasped.exe` (32-bits, onder Wine) één keer met een waarnemer die
**twee** dingen logt:

1. elke `connect` / `sendto` / `recvfrom` — protocol, adres, poort, lengte, en of er een antwoord komt;
2. elke `SetupDiEnumDeviceInterfaces` + `HidD_GetAttributes` — het **VID/PID** en het apparaatpad dat
   de game accepteert of afwijst.

**Wat de uitkomst ons vertelt:** de eerste log geeft het transport en dus het protocol dat een
schone licentie-/daemon moet spreken (of dat er juist géén socket aan te pas komt en het puur HID is).
De tweede log geeft de VID/PID die een HID-emulatie zou moeten presenteren.

**Positieve controle:** **dezelfde waarnemer op Terminator Salvation.** Daar is het socketgedrag
onafhankelijk gemeten: `connect(AF_UNIX /tmp/.aksusb)` → 2× `errno=2`, dan `sendto(165 bytes →
127.0.0.1:475)` en `sendto(165 bytes → 255.255.255.255:475)`, vier keer met 1 s ertussen, **geen
enkele `recvfrom`**, dan `Failed aldeen: 2` / `DongleInitialize() FAILED!` in het eigen logboek en
`exit(0)` op ~4 s. Als de waarnemer dat op Terminator exact reproduceert maar bij AE **HID-verkeer in
plaats van sockets** laat zien, dan is bewezen dat AE een andere route neemt — en dat is precies de
informatie die we nu missen.

**Wat een uitkomst zou weerleggen:** dezelfde socket-rijen bij AE als bij Terminator (zelfde poort,
zelfde 165-byte vraag) → dan is AE's route identiek aan Terminator's en is de bestaande
licentie-/daemon-aanpak direct overdraagbaar.

---

## 6. Bewijsvoering — hoe de scanners zijn geijkt

| scanner | ijkpunt | uitslag |
|---|---|---|
| PE-importlezer | `RSLauncher.exe` van TC5 moet `KERNEL32.dll` importeren | **PASS** — 19 DLL's, `KERNEL32.dll` gevonden |
| PE-exportlezer | onze shim `hasp_windows_x64_105293.dll` moet 25 uitvoeren op ordinalbasis 1 hebben | **PASS** — 25, basis 1, `@13 = hasp_login` |
| stringzoeker | `hasp_windows_x64_105293.dll` moet in `TimeCrisisGame-Win64-Shipping.exe` staan, een verzonnen naam niet | **PASS** — 1 treffer; controle → 0 |
| tekstzoeker in de spelmap | dezelfde zoekactie moet `hasp`/`dongle`/`licen`/`Sentinel`/`Aladdin` **wél** vinden als ze er zijn (in de exe-stringtabel gebeurt dat ook) | **PASS** — vindt 2× `HASP`, 1× `dongle`, 1× `dongle.tga` in de exe |

**Teruggetrokken tijdens dit werk:** een eerste PE-lezer las `SizeOfOptionalHeader` op de verkeerde
offset; de ijking gaf onmiddellijk FAIL en de uitslagen zijn pas ná reparatie gebruikt.

**Gereedschap (op de deck, herhaalbaar):** `/home/deck/hasp_work/hasp_measure.py`,
`hasp_measure2.py`, `pelib.py`, `hasp_measure3.py`, `hasp_measure5.py`; uitvoer in `d.out` … `h.out`.
Alles read-only.
