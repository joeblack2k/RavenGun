# Aliens Armageddon — beschermingsstatus (HASP/Aladdin-dongle)

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse
van de archiefkopie op de NAS (alleen gelezen). Er is **niets uitgevoerd** — geen spel, geen lader,
geen sandbox. Elk cijfer hieronder is MEASURED met een script waarvan de lezer eerst op een bekend
antwoord is geijkt (zie §6).

**Spel:** `Aliens Armageddon` (Raw Thrills, 2015) — native **32-bits Linux ELF**, geen Windows-kant.
**Werkmap in het archief:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Aliens Armageddon/`

---

## 1. Welke variant

**MEASURED** — het is de **Aladdin/HASP-klassieke familie met de `aksusbd`-daemon en NetHASP**,
**statisch in de game gelinkt**. Er is *geen* HASP-runtimebibliotheek in de spelmap en *geen*
`DT_NEEDED` naar iets van HASP.

`game` (en `game.original`):

| | |
|---|---|
| grootte | 9.149.556 bytes |
| sha256 `game` | `be7ebd1b81dc543881376eaba1c40fc2286b60d2821e9d543900ecf28af49acf` |
| sha256 `game.original` | `f5a35337e004f86fe4c24b37a7bedb457f9533f6d72cdc394b0da1f13fe91057` |
| md5 `game` | `06b9d6d4d25d72fa3dc3ce1650becf4d` |
| kop | `7f454c46 01 01 01 00 … 02 00 03 00` → ELF, class 1 (**32-bits**), ET_EXEC (2), EM_386 (3) |

**MEASURED** — `readelf -d game` geeft 13 `DT_NEEDED`-regels: `libGL.so.1`, `libGLU.so.1`,
`libglut.so.3`, `libpthread.so.0`, `librt.so.1`, `libesd.so.0`, `libz.so.1`, `/usr/lib/libcsv.so`,
`libgcc_s.so.1`, `libc.so.6`, `libdl.so.2`, `libstdc++.so.6`, `libm.so.6`. **Geen `libhasp_linux_*`.**

**MEASURED (geijkte scanners)** — dit is dezelfde doodlopende route als bij Halo/Terminator/Jurassic
Park/Galaga (`docs/hasp-layer.md` §0):

```
readelf --dyn-syms game  -> 1424 regels, 0 met hasp/aladdin/dongle
readelf -r game          -> 1419 regels, 0 met hasp/aladdin/dongle
```

De HASP-aanroepen zitten dus **in de eigen code van de game**; er is geen PLT-slot en geen
GOT-entry. `LD_PRELOAD` zet de emulator wél in het proces maar de aanroepen gaan er niet langs.

**MEASURED** — de dongle-teksten in `game`, met bestandsoffset en de ruwe tekst eromheen:

| patroon | aantal | eerste offset | context (ruw) |
|---|---|---|---|
| `hasp` | 84 | `0x6f8ad0` | `ReInitAladdinDongles: resetting hasp daemon` |
| `HASP` | 13 | `0x76e4c8` | `classic_check_local_port: invalid HASP ID, want: %u, found: %u` |
| `Dongle` | 6 | `0x6f8a0f` | `Unable to start dn: %d … DongleInitialize() FAILED!` |
| `dongle` | 9 | `0x6eb700` | `Writing country code to dongle FAILED! (ec=%i)` |
| `Aladdin` | 2 | `0x6f8a8e` | `ReInitAladdinDongles: called %d times` |
| `aksusb` | 2 | `0x6f88fd` | `/etc/init.d/aksusbd restart` |
| `nethasp.ini` | 1 | `0x8a0380` | (losse naam in een lege stringtabel) |
| `DongleInitialize` | 1 | `0x6f8a0f` | `DongleInitialize() FAILED!` |

Verder MEASURED in dezelfde stringtabel: `Too many loops when acquiring mutex for dngle: %i`,
`Bad inputs to SysGetCryptoSeed()`, `Stored c (%u) does not match data c (%u)`,
`Failed to close aldeen: %i`, `Failed to reinit aldeen: %i`, `Failed when releasing dh: %d`,
`Writing serial number to dongle FAILED! (ec=%i)`.

**INFERRED (sterk):** dit is dezelfde keten als **Terminator Salvation** — de AKS-USB-daemon
(`aksusbd`), de "aldeen"-laag, de NetHASP-route via `nethasp.ini`, en de klassieke HASP-ID-controle
(`classic_check_local_port`). De aanroepen zijn statisch, de dongle wordt via een daemon/socket
bereikt, niet via een gelinkte `hasp_*`-client.

### Wat `game` en `game.original` van elkaar verschillen — MEASURED

Slechts **4 bytes**:

| offset | `game` | `game.original` |
|---|---|---|
| `0x3c832` | `80 07` | `50 05` |
| `0x3c83c` | `38 04` | `00 03` |

De disassemblage eromheen (ruwe bytes): `c7 05 40 b5 90 08 <imm32>` — `mov dword ptr [0x90b540], imm32`:

* `[0x90b540]`: `game` = `0x780` = **1920**, `game.original` = `0x550` = **1360**
* `[0x90b544]`: `game` = `0x438` = **1080**, `game.original` = `0x300` = **768**

**MEASURED + INFERRED:** `game` is een **beeldresolutie-patch naar 1920×1080**; `game.original` is de
kastversie op 1360×768. **De dongle is NIET aangeraakt** — de aanroepen en de dongle-map staan er
nog volledig. De naam "dehasped" bestaat hier niet; er is niets aan de beveiliging gepatcht.

---

## 2. Wat het spel vraagt, en wat er geantwoord moet worden

Dit is de grote winst van deze game: **de antwoorden zitten in de spelmap zelf.** De map
`hasp/` is een vastgelegde uitdaging→antwoordtafel plus een geheugenbeeld.

### 2a. De antwoordtafel — MEASURED

```
map:  Aliens Armageddon/hasp/
```

| meting | waarde |
|---|---|
| bestanden | **14.674** (470.368 bytes totaal) |
| naamlengte-histogram (hextekens → aantal) | `{11: 1, 32: 12.006, 64: 2.667}` |
| bestandsgrootte-histogram | `{32: 14.671, 128: 1, 256: 1, 512: 1}` |
| niet-hex namen | precies 1: `hhl_mem.dmp` |
| identieke inhouden | 0 (14.674 distinct) |
| prefix-closure | elke 64-hex naam heeft zijn 32-hex voorvoegsel als bestaande naam: **2.667 van 2.667** |
| 32-hex namen mét 64-hex verlenging | 1.213 · zónder: 10.793 |

**Positieve controle (ijking):** dezelfde meting op `Terminator Salvation /hasp/` geeft
`{11: 1, 32: 3.736, 64: 1.676}`, `{32: 5.410, 128: 1, 256: 1, 512: 1}`, 0 duplicaten, en
prefix-closure 32 → 1.676 (óók alle). **De vorm is identiek.**

**INFERRED (volgt uit de gemeten vorm + de regel uit `patches/Terminator/dongle.json`):** de regel is
`naam = kleine hex van de eerste 32 bytes van de invoer (64 tekens)`, en als dat bestand niet bestaat
`van de eerste 16 bytes (32 tekens)`. Een invoer van 32 bytes levert dus zowel een 64-hex als een
32-hex bestand op (vandaar: élke 64-hex heeft een 32-hex tweeling), en een invoer van 16 bytes alleen
een 32-hex bestand. Dat verklaart alle drie de gemeten feiten.

De twee grote bestanden (`2a3ccf10ef8dff97539ed4b3cd113701` = 512 B,
`91a0dda258cc5df430a5678cc72cd603` = 256 B) zijn **geen** record-concatenaties: 508 resp. 254 van hun
bytes zijn niet-`00`/niet-`FF`, en de 128-bits blokken zijn niet identiek. Het zijn dus gewoon grotere
antwoorden in dezelfde tafel (hun naam is 32 hex, dus hun invoer was ≤ 16 bytes).

### 2b. Het geheugenbeeld `hhl_mem.dmp` — MEASURED, en het is decodeerbaar

128 bytes, sha256 `5f56e18a46a63474eacfc7f846396c6e695381aaeaa348dea5ff9f587ed07bc9`.

De indeling is **gemeten op Terminator Salvation** en daar **geijkt** (positieve controle), daarna
toegepast op Aliens Armageddon:

| veld | Terminator (controle) | Aliens Armageddon |
|---|---|---|
| `0xFFF0` = dump`[0x00:0x30]` (48 B) | `da367ea4…86b5ffffffffffffffff` | `47a214c1…f063cd46ffffffffffffffff` |
| `0xFFF2` = dump`[0x30:0x70]` (64 B) | alle `FF`, met `0x1c=03`, `0x3c..3f = 62 0a 00 00` | alle `00`, met `0x39=01`, `0x3a=04`, `0x3c=FA`, `0x3d=03` |
| staart `dump[0x70:0x80]` (16 B) | `WBPT\x01\x00\x00\x00\x10\x00\x00\x00\x00\x00(\xb1` | **byte-identiek hetzelfde** |
| CRC-controle `zlib.crc32(main[0:36])` vs `main[0x24:0x28]` | `dbcb86b5` == `dbcb86b5` → **MATCH** | `f063cd46` == `f063cd46` → **MATCH** |
| landcode `0xFFF2+0x1c` | **3** | **0** |
| serienummer `0xFFF2+0x3c` (le u32) | **2658** | **1018** |

**IJKING PASSED.** Voor Terminator komt er exact uit wat `patches/Terminator/dongle.json` als gemeten
vastlegt (landcode 3 = UK, serienummer 2658, CRC-regel). Diezelfde regel op AA's bestand geeft een
**kloppende CRC** en dus een geldige uitlezing. Conclusie:

* `0xFFF0` (**HASP_FILEID_MAIN**), **48 bytes** — met op offset `0x24` een `le` CRC32 over de eerste
  36 bytes. De CRC klopt, dus dit is het echte beeld en niet een halve kopie.
* `0xFFF2` (**HASP_FILEID_LICENSE**), **64 bytes** — landcode 0, serienummer 1018.
* De 16-byte staart is **voor beide games byte-identiek** → formaatmarkering, geen dongle-inhoud.
* **Let op het verschil met Terminator:** AA's licentieblok is nul waar Terminator's `FF` is. Volgens
  de gemeten notitie bij Terminator betekent `00` in die kenmerkwoorden "aan" (actief-laag). AA's
  dongle heeft die kenmerken dus **allemaal aan**, Terminator's allemaal uit. **Neem Terminator's
  waarden hier niet over.**

### 2c. Het dongle-scherm — MEASURED (cosmetisch, maar het verklaart de bestandsnaam)

`utils/splash/dongle-1024x768.cfg` (1.253 B, sha256 `47d17534a2ed3a22…`) is een **SUSE-bootsplash-
configuratie** (commentaar van `stepan@suse.de`), met `version=3`, `state=1`,
`jpeg=/pm/utils/splash/dongle-1024x768.jpg`. Het bijbehorende `dongle-1024x768.jpg` is 146.898 B,
sha256 `7765c1f8f1f87e03…`. **INFERRED:** dit is het opstartscherm van de kast; het pad `/pm/...`
bevestigt dat de spelwortel `/pm` hoort te zijn (dezelfde eis die bij de ElfLdr2-titels speelt).

---

## 3. Wat we al hebben dat helpt

**Dit is de gunstigste van de vier.** De hele keten is aanwezig of direct af te leiden.

| wat | waar | dekt dit spel? |
|---|---|---|
| de emulator met de antwoordtafel-logica | `ravengun/hasp/src/dongle.c` → `dongle_crypto_replay()`; regel in `dongle.h` r. 143–166 | **ja, één-op-één** — dezelfde naamregel (hex van 32, anders 16 bytes) |
| het `dongle.json`-formaat met `crypto_table` + `memory` | `patches/Terminator/dongle.json` | **ja** — het voorbeeld is precies deze vorm |
| de geheugenbestandsnamen (binaire fileid) | `dongle_fileid_name()`, `memory/00000000000000001111111111110000` (0xFFF0) en `…0010` (0xFFF2) | **ja** |
| het beeld zelf | `Aliens Armageddon/hasp/hhl_mem.dmp` | **ja — in de spelmap** |
| de antwoordtafel | `Aliens Armageddon/hasp/` (14.674 antwoorden) | **ja — in de spelmap** |
| de gemeten hit-rate van de regel | Terminator, 8 van 8 treffers (`dongle.json`) | **positieve controle** |
| de Windows/PE-route | `hasp-layer.md` §4 | **nee** — AA is native ELF |
| de vendor-DLL-shim `hasp_windows_x64_*.dll` (25 ordinalen) | `ravengun/hasp/out/` | **nee** — AA importeert geen DLL |

**Wat NIET overdraagbaar is:**

* **Terminator's donglewaarden** (landcode 3, serienummer 2658, `FF`-kenmerken). AA's zijn 0 / 1018 /
  `00`. Overnemen zou de verkeerde kast opleveren.
* **`haspid`, `features`, `keycaps`** — voor AA niet gemeten en voor deze route ook niet nodig; die
  horen bij de `hasp_get_info`-XML van een gelinkte client, en AA heeft die niet.
* **De Linux-kant van de laag is hier werkloos** (§1, 0 dynsyms / 0 verplaatsingen). Dit is dezelfde
  blokkade als bij Halo, Terminator, Jurassic Park en Galaga, en dus **geen nieuwe kostenpost**: het is
  de al geplande PLT/GOT-koppeling uit `docs/loskoppelen-per-game.md` (plthook + Capstone voor 64-bits;
  vaste GOT herschrijven voor 32-bits) — of de daemon-route hieronder.

---

## 4. Wat ontbreekt

1. **Het aangrijpingspunt.** De hasp-aanroepen zijn statisch. Zonder de PLT/GOT-koppeling wordt
   `dongle_crypto_replay` nooit bereikt. (Zelfde openstaande punt als de andere Linux-titels.)
2. **Of de daemon-route nodig is in plaats van de API-route.** AA heeft `aksusbd`,
   `/etc/init.d/aksusbd restart`, `nethasp.ini` en `SysGetCryptoSeed()` — dat wijst op een
   socket/daemon als echt protocol. Voor Terminator is die route al als de fix benoemd
   (`LICENTIE-NETHASP.md`): een schone licentie-/daemon die op AF_UNIX `/tmp/.aksusb` en/of UDP
   `:475` antwoordt. **Voor AA is het protocol nog niet gemeten.**
3. **De betekenis van de 16-byte staart** `WBPT\x01\x00\x00\x00\x10\x00\x00\x00\x00\x00(\xb1`
   (in beide games identiek) en of de game hem controleert. Onbekend.
4. **Of de nul-kenmerken in het licentieblok door AA's eigen controle worden geaccepteerd** — dit is
   exact de klasse fout die Terminator in run r1 in "Game must be registered" liet vallen. Niet te
   voorspellen zonder run.

**Kosten:** punt 1 is de enige echte bouwkostenpost en die is al begroot voor vier andere titels.
Punten 2–4 zijn metingen, geen bouwwerk. Alle *data* hebben we al.

---

## 5. Voorgestelde test (NIET UITGEVOERD — de bench is bezet)

**Eén meting:** laat `game` onder onze eigen lader lopen met de emulator in het proces en
`RAVENGUN_HASP_VERBOSE=1`, `RAVENGUN_HASP_DIR=<patches>`, `RAVENGUN_HASP_GAME=AliensArmageddon`,
met `crypto_table` op AA's `hasp/`-map. **Lees uit:** het aantal regels
`hasp_login` / `hasp_read (fileid=… offset=… lengte=…)` / `hasp_encrypt` / `hasp_decrypt`, en
daaruit de **treffers versus missers op de antwoordtafel** (de emulator logt de naam die hij zocht).
Dat ene getal — treffers/missers — zegt of onze uitlezing van het dongle-geheugen en de naamregel
samen correct zijn.

**Positieve controle:** dezelfde run met `RAVENGUN_HASP_GAME=Terminator` op
`Terminator Salvation /hasp/`, waar de regel onafhankelijk op **8 van 8** is gemeten en de attract
draait. Als dezelfde opzet daar 8/8 geeft en bij AA 0/n of n/n, dan is de meting betrouwbaar en is
het verschil echt.

**Een tweede, goedkopere meting zonder spelstart** (al gedaan, en herhaalbaar): de
`hhl_mem.dmp`-ontleding hierboven met de CRC-controle. Die is in deze ronde **geslaagd** en hoeft
alleen herhaald te worden als het bestand wijzigt.

**Wat een uitkomst zou weerleggen:** nul `hasp_*`-regels in het logboek terwijl de DLL wél in
`/proc/<pid>/maps` staat → dan zijn de aanroepen statisch (verwacht) en is punt 1 uit §4 de blokkade,
niet de data.

---

## 6. Bewijsvoering — hoe de scanners zijn geijkt

Elke scanner is eerst op een **bekend antwoord** getest; een lege uitkomst telt pas als bewijs als dat
is gelukt.

| scanner | ijkpunt | uitslag |
|---|---|---|
| PE-importlezer | `RSLauncher.exe` van TC5 moet `KERNEL32.dll` importeren | **PASS** — 19 DLL's, `KERNEL32.dll` gevonden |
| PE-exportlezer | onze eigen shim `hasp_windows_x64_105293.dll` moet 25 uitvoeren op ordinalbasis 1 hebben, met `@13 = hasp_login` | **PASS** — 25, basis 1, `@2/hasp_decrypt`, `@13/hasp_login`, `@14/hasp_logout`, `@15/hasp_read` |
| stringzoeker | het literaal `hasp_windows_x64_105293.dll` moet in `TimeCrisisGame-Win64-Shipping.exe` staan, en een verzonnen naam niet | **PASS** — 1 treffer om `0x16818d8`; controle `ZZZABSENTZZZ` → 0 |
| ELF-symbollezer | `readelf` moet op dezelfde binary wél de `DT_NEEDED`-regels vinden (13 stuks) | **PASS** |
| `hhl_mem.dmp`-ontleder | Terminator moet landcode 3, serienummer 2658 en een kloppende CRC geven | **PASS** |

**Teruggetrokken tijdens dit werk (eerlijkheidshalve):** een eerste versie van de PE-lezer las de
COFF-kop verkeerd (`SizeOfOptionalHeader` op offset 12 in plaats van 16). De ijking ving dat
onmiddellijk — C1 en C2 gaven FAIL. Pas ná de reparatie zijn de uitslagen in dit document gebruikt.

**Gereedschap (op de deck, herhaalbaar):**
`/home/deck/hasp_work/hasp_measure.py`, `hasp_measure2.py`, `pelib.py`, `hasp_measure3.py`,
`hasp_measure4.py`, `hasp_measure5.py`; uitvoer in `d.out` … `h.out`. Alles read-only.
