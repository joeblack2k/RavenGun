# The House of the Dead 4 Special — beschermingsstatus

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, lader of sandbox gestart** (de bench is
bezet). Elke scanner is eerst geijkt — zie §5.

**Spel:** The House of the Dead 4 Special (Sega Lindbergh, 32-bits Linux-ELF).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/The House of the Dead 4 Special/`
**Hoort bij:** [`../ELF-SEGA-FAMILIE-20261014.md`](../ELF-SEGA-FAMILIE-20261014.md)

**Kernbevinding:** dezelfde `amDongle`-laag als HOTD4 (`Ver.0.05 Build:Feb 27 2007`), in de ELF, met
een **ongepatcht** controlepad. Deze titel is technisch de **zwaarste van de vijf**, niet om de
bescherming maar om de levering: er zit **geen enkele `libsegaapi.so`- of `libkswapapi.so`-ELF** in de
spelmap, en de `libsegaapi.so` die er wél staat is **geen ELF maar 23 bytes tekst**.

---

## 1. De tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **The House of the Dead 4 Special** | `amDongle`-dongle op poort-I/O, statisch in de ELF; **controlecode lijkt ongepatcht** | als HOTD4: `AccessKey` → `amDongleChipInfo` → `0x0101`; dan 1 KiB `KeyCheckEncData` ontsleutelen en met `KeyCheckOrgData` vergelijken. Daarnaast `/dev/ttyS0`, `/dev/usb/tts/%d` (seriële randapparatuur) en `amEepromSmbusAccess` op `/dev/i2c/0` voor de backup-EEPROM | **nee** — nog geen eigen antwoord. TeknoParrot antwoordt met `libkswapapi.so` waarin `kswap_collect(){return 0;}` | een eigen `libkswapapi.so`; **een echt `libsegaapi.so`** (de meegeleverde is onbruikbaar); alle overige gastbibliotheken | eerst de 23-byte `drv/ssound/lib/libsegaapi.so` onderzoeken; daarna dezelfde statische stappen als HOTD4 (§4) |

---

## 2. Wat er gemeten is

**MEASURED** — `readelf -h -d`: `hod4M.elf` (6.584.844 B) is ELF32 i386 `EXEC`, entry `0x804d600`,
**32** sectiekoppen, dezelfde 13 `DT_NEEDED` als HOTD4 (inclusief `libsegaapi.so` en
`libkswapapi.so`), geen HASPE-marker, **gestript** (0 `.symtab`, 0 debug-secties), **318** UND-symbolen.
`hod4testM.elf` (350.736 B) is het testmenu: **31** sectiekoppen en **9** `DT_NEEDED`, waaronder
`libglut.so.3` — en géén `amDongle`.

**MEASURED** — de spelmap `elf/` bevat: `game`, `hod4M.elf`, `hod4testM.elf`, `XF86Config_dual`,
`frontend.env~`, `LucidaConsole_12.abc`/`.tga`, `P1.png`, `P2.png`, `logo.tga`. **Geen `libsegaapi.so`,
geen `libkswapapi.so`, geen `libposixtime`** — terwijl de ELF ze alle drie als `DT_NEEDED` noemt. Die
moeten dus van buiten komen.

**MEASURED** — in `drv/ssound/lib/` staat wél een `libsegaapi.so`, maar `file -b` zegt:
**`ASCII text, with no line terminators`, 23 bytes.** De echte bibliotheek staat ernaast als
`libsegaapi.so-1.03.0000` (65.036 B, ELF32 i386 DSO). Daar staan ook `libctdproxy.so`, `libpiaproxy.so`
en `libosuser.so` (elk met een `-1.03.0000`-tweeling). **INFERRED:** iemand heeft de naam
`libsegaapi.so` met een tekstbestandje bezet — dat is precies het soort spoor dat een eerdere ronde kan
hebben achtergelaten, en het is geen eigenschap van de cab.

**MEASURED** — `strings -a -n 4 hod4M.elf | grep -c -i -E '<patroon>'`: `dongle` 36 · `amDongle` 36 ·
`jvs` 7 · `ioctl` 3 · `/dev/` 23 · `region` 1 · **`hasp` 0 · `keychip` 0 · `nethasp` 0 · `HASPE` 0**.
De `amDongle`-woordenschat is identiek aan HOTD4 en Rambo, met de bouwstempel
`amDongle Ver.0.05 Build:Feb 27 2007 19:51:30`.

**MEASURED** — kernteksten die alleen hier opduiken:
`amEepromSmbusAccess: access error. (%s: access=%c, cmd=%02X, type=%08X)` en een tabelkop met de tekst
`  REGION  `. Apparaten in de stringtabel: `/dev/i2c/0`, `/dev/lbb`, `/dev/console`, `/dev/hdb3`,
`/dev/ttyS0`, `/dev/usb/tts/%d`.

**MEASURED** — het opstartscript `elf/game`:
```
/X11R6/bin/xinit $app_path/hod4M.elf $1 -- /X11R6/bin/X -s 0 -nolisten tcp -xf86config $app_path/XF86Config_dual
```
en bij `-t` hetzelfde met `hod4testM.elf`. `elf/frontend.env~` bevat alleen versienummers
(`NVIDIA_VER=8164`, `SSOUND_VER=final1.4`, `BASEBD_VER=0.19`). De uitgecommentarieerde regel
`# mount -t ext3 /dev/hdb5 /home/disk1` in `game` toont hoe de cab zijn schijven koppelde.

**MEASURED** — profielen: `HOTD4SP.xml` (`EmulatorType=Lindbergh`) en `HOTD4SPELF2.xml`
(`EmulatorType=ElfLdr2`), beide `ExecutableName=hod4M.elf`, test `hod4testM.elf`,
`DongleRegion`/`PcbRegion` aanwezig.

---

## 3. Is de controle hier nog intact?

**MEASURED** — de symboolonafhankelijke byte-zoektocht:

| ELF | force-true (`c7 40 08 01 00 00 00 5d c3`) | force-false (`…00…`) |
|---|---|---|
| `hod4M.elf` (Special) | **0** | 2 |
| `ramboM.elf` (controle, bekend geforceerd) | **4** | 4 |

**INFERRED (matig sterk):** ook hier lijkt het originele controlepad nog aanwezig, net als bij HOTD4 en
Too Spicy. Dat maakt deze titel geschikt om de poort aan te lezen. **Wat het niet bewijst:** de
signatuur is streng en de ELF is gestript; zie §4.

**Eén waarschuwing die losstaat van de bescherming:** de `libsegaapi.so` in `drv/ssound/lib/` is geen
ELF. Zolang dat zo is, is elke conclusie over "de game mist alleen nog een bibliotheek" voorbarig —
eerst moet duidelijk zijn wat daar staat en wie dat heeft neergezet.

---

## 4. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

**Statisch, zonder bench:**

1. **Lees de 23 bytes** van `drv/ssound/lib/libsegaapi.so` (`cat`, `xxd`). Als het een tekstbestandje
   met een aanwijzing is, is dat een spoor voor een eerdere ronde. **Dit kost één commando en is mijn
   eerste aanbeveling.**
2. Zoek de **PLT-stub van `kswap_collect`** in `hod4M.elf` en zijn aanroepers. **IJking:** dezelfde
   zoektocht in `ramboM.elf`, waar de namen er nog zijn.
3. Lees de aanroeper van `amDongleDecryptEx`; in `ramboM.elf` staat de referentiebody op
   `0x080e3ee3`–`0x080e3f6e`.
4. Vergelijk `libsegaapi.so-1.03.0000` (65.036 B, hier aanwezig) met de HOTD4-versie
   `libsegaapi-1.01.0001` (75.055 B): is de eerste een echte Sega-build van de audiolaag, of een
   verminkte? Dat bepaalt of de game hem kan laden.

**Dan één benchrun** (pas op een vrije bench): `hod4M.elf` met dezelfde opzet als HOTD4.

**Positieve controle:** de `open()`-logger moet `/dev/ttyS0` of `/dev/usb/tts/%d` **of** `/dev/i2c/0`
laten zien — deze titel opent meer apparaten dan HOTD4, dus de logger heeft hier meer om te bewijzen.
**Wat de hypothese zou weerleggen:** start het spel zonder dongle door, dan is de dongle-laag niet
afgedwongen.

---

## 5. Bewijsvoering — ijking

| scanner | ijkpunt | uitslag |
|---|---|---|
| `file -b` | een echte DSO moet als `ELF 32-bit LSB shared object` gemeld worden | **PASS** op `libsegaapi.so-1.03.0000`; en de 23-byte `libsegaapi.so` wordt als `ASCII text` gemeld — dat is dus een **echte** afwijking, geen scannerfout |
| `readelf -h` | het testmenu moet minder `DT_NEEDED` hebben dan het spel (het is een gereedschap) | **PASS** — 9 tegen 13 |
| stringzoeker | `dongle` 36× terwijl `hasp` 0× | **PASS** |
| byte-signatuurzoeker | de signatuur moet in `ramboM.elf` 4× gevonden worden voordat een 0 hier iets betekent | **PASS** |
| `md5sum` | de varianten moeten onderscheiden kunnen worden (anders is de variantentabel zinloos) | **PASS** — `hod4M.elf` `74baffc7…`, `hod4testM.elf` `398b5f4f…` |

**Gereedschap (op de deck, read-only):** `/home/deck/rgwork/s3_elfscan.sh`, `s4_segaapi.sh`,
`s6_donglecode.sh`, `s7_step2inv.sh`, `s13_patchcheck.sh`. Er is niets op de NAS geschreven.
