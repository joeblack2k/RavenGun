# Rambo — beschermingsstatus

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, lader of sandbox gestart** (de bench is
bezet). Elke scanner is eerst geijkt — zie §5.

**Spel:** Rambo (Sega Lindbergh, 32-bits Linux-ELF).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Rambo/`
**Hoort bij:** [`../ELF-SEGA-FAMILIE-20261014.md`](../ELF-SEGA-FAMILIE-20261014.md)

**Kernbevinding:** de bescherming zit **in de ELF** (de `amDongle`-laag, statisch gelinkt), en de
controle is in **al vier** de Rambo-ELF's **al geforceerd** tot "geslaagd". De Windows-lader draagt aan
deze titel alleen een vervangende `libkswapapi.so` bij waarvan `kswap_collect` uit
`xor %eax,%eax; ret` bestaat.

---

## 1. De tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **Rambo** | `amDongle`-dongle op **poort-I/O**, statisch in de ELF; poort `CJvsMgr::CheckDongleDecrypt` **in deze dump al geforceerd** (4/4 varianten) | 16-byte `AccessKey` naar de dongle → `amDongleChipInfo` → chipinfo `0x0101`; daarna acht willekeurige 16-bits woorden waarmee de dongle 1 KiB `KeyCheckEncData` moet ontsleutelen tot `KeyCheckOrgData` | **nee** — nog geen eigen antwoord. TeknoParrot antwoordt met `libkswapapi.so` waarin `kswap_collect(){return 0;}` | een eigen `libkswapapi.so` (`kswap_collect`, `__kswap_collect`, `collect_pf`) en de gastbibliotheken die de cab leverde (`libsegaapi.so`, `libposixtime.so.1`, libGL/libGLU, libX11) | statisch: lees de ongepatchte poort in `hod4Mn.elf`/`apacheM.elf` en zoek de aanroepers van `kswap_collect`. Daarna één benchrun met een loggende `libkswapapi.so` (§4) |

---

## 2. Wat er gemeten is

**MEASURED** — `readelf -h -d Rambo/elf/ramboM.elf`: ELF32 i386 `EXEC`, entry `0x804f2c0`,
**39 sectiekoppen**, **13** `DT_NEEDED` (`libsegaapi.so`, `libkswapapi.so`, `libXxf86vm`, `libX11`,
`libXext`, `libGL`, `libGLU`, `libm`, `libpthread`, `libposixtime.so.1`, `libstdc++.so.6`,
`libgcc_s.so.1`, `libc`), geen HASPE-marker. Dat is de **ongepakte** klasse uit
[`../ELF-LADER-SPEC.md`](../ELF-LADER-SPEC.md) — echte importnamen (**402** UND-symbolen), geen
`0xdeadbeef`-PLT.

**MEASURED** — niet gestript: `.symtab` met **20.024** symbolen waarvan **15.490** `FUNC`, en 8
debug-secties. Daardoor zijn de adressen bekend:

| symbool | adres | grootte |
|---|---|---|
| `amDongleInit` | `0x082c4746` | 583 |
| `amDongleExit` | `0x082c3160` | 161 |
| `amDongleIsAvailable` | `0x082c3201` | 29 |
| `amDongleSendEx` | `0x082c3680` | — |
| `amDongleDecryptEx` | `0x082c40fb` | — |
| `amDongleEncryptEx` | `0x082c3ed9` | — |
| `amDongleChipInfo` | `0x082c468d` | 185 |
| `_ZN7CJvsMgr18CheckDongleDecryptEv` | `0x080e3e94` | 235 (maar zie §3) |
| `_Z25JvsMgr_CheckDongleDecryptv` | `0x080e4262` | 21 |
| `amOsinfoCheckDongleRental` | `0x082c947f` | — |
| `AccessKey` (object, 16 B) | `0x083a57f6` | 16 |
| `KeyCheckEncData` (object) | `0x08415280` | **1024** |
| `KeyCheckOrgData` (object) | `0x08415680` | **1024** |

De bronbestandsnaam **`am_dongle.c`** staat in de stringtabel; de bouwstempel is
`amDongle Ver.0.12 Build:Feb 21 2008 18:00:12`.

**MEASURED** — `objdump -d --start-address=0x082c4746 --stop-address=0x082c4900`: `amDongleInit` opent
**geen enkel `/dev`-knooppunt**. Het leest twee poortbases (`[0xacadf68]`, `[0xacadf6a]`) en doet
`in`/`out` op `+0x2c`, `+0x28` en `+0x00`, met `out %al,$0x80` als I/O-vertraging — bit-banged over
PCI-poorten. Daarna `amDongleSendEx(0x10, 0x10, AccessKey)`, dan `amDongleChipInfo`, dan
`cmp $0x101,%ax` en bij succes de vlag in `0x0acad9a4` op 1.

> **Valkuil:** de strings `/dev/i2c/0`, `/dev/lbb`, `/dev/console` en `/dev/input/js%d` zitten wél in
> het bestand, maar de dongle loopt niet via `/dev/i2c/0` — dat pad hoort bij `amEepromSmbusAccess`
> (de backup-EEPROM). Zie het familietabblad §3.4.

**MEASURED** — `strings -a -n 4 ramboM.elf | grep -c -i -E '<patroon>'`: `dongle` 81 · `amDongle` 77 ·
`jvs` 159 · `ioctl` 12 · `region` 5 · `protect` 1 · `licen` 1 · **`hasp` 0 · `keychip` 0 ·
`nethasp` 0 · `HASPE` 0**. Dit is dus géén HASP/Aladdin-titel en géén Ring-keychip-titel.

**MEASURED** — de spelbestanden dragen zelf een `libkswapapi.so` (5.816 B, `GCC 3.4.3 (MontaVista
2005-10-21)`, met debug-info) die **`kswap_collect`, `__kswap_collect` en `collect_pf`** uitvoert;
`kswap_collect` staat als UND-import in de ELF.

**MEASURED** — het opstartscript is `elf/game.sh`:
`export LD_LIBRARY_PATH=…:/home/disk0/elf/; pushd /home/disk0/elf; /home/disk0/elf/ramboM.elf $1`.
Niets over bescherming.

**MEASURED** — profielen: `Rambo.xml` (`EmulatorType=Lindbergh`, `ExecutableName=ramboD.elf`) en
`RamboElf2.xml` (`EmulatorType=ElfLdr2`, `ExecutableName=ramboM.elf`), allebei met
`DongleRegion=JAPAN` en `PcbRegion=JAPAN` (keuzes JAPAN | USA | EXPORT).

**MEASURED** — de varianten (md5): `ramboM.elf` = `ramboM_HD.elf` (`954c6dde6ca3aaa530f8c67277632216`);
`ramboM_VGA.elf` (`3e3be01c62eb37c7a94f7be1cf335ab0`), `ramboD.elf` (`8a4e169e…`) en `ramboR.elf`
(`807354a6…`) zijn andere builds. `ramboM_HD.elf` is dus gratis; `_VGA` vraagt eigen werk.

---

## 3. De poort is hier al geforceerd

**MEASURED** — `objdump -d` op de kop van `_ZN7CJvsMgr18CheckDongleDecryptEv`:

| ELF | adres | `.symtab`-grootte | bytes |
|---|---|---|---|
| `ramboM.elf` | `0x080e3e94` | 235 | `55 89 e5 8b 45 08 **c7 40 08 01 00 00 00** 5d c3` — daarna begint objdump midden in een instructie (`inc %esp`) en volgt de **originele body** weer (`call IzHeapAlloc`, `KfRandomI`, `amDongleDecryptEx`, `strncmp(…,0x400)`) |
| `ramboM_VGA.elf` | `0x080e3e94` | 235 | identiek |
| `ramboD.elf` | `0x081658fc` | **15** | dezelfde 15 bytes, direct gevolgd door `nop` en de volgende functie |
| `ramboR.elf` | `0x080f47f6` | **15** | idem |

**MEASURED, symboolonafhankelijk** (`s13_patchcheck.sh`): de byte-sequentie
`c7 40 08 01 00 00 00 5d c3` ("zet de vlag op 1 en keer terug") komt **4×** voor in elk van de vier
Rambo-ELF's. In `hod4M.elf`, `hod4M.elf` (Special) en `apacheM.elf` komt hij **0×** voor, terwijl de
"zet de vlag op 0"-variant daar wél staat.

**INFERRED (sterk):** de dongle-controle van Rambo is in deze dump geneutraliseerd — bij `ramboM.elf`
en `ramboM_VGA.elf` door de functiekop te overschrijven met `return true` (de originele body staat er
nog onbereikbaar achter), bij `ramboD.elf` en `ramboR.elf` is de functie nog maar 15 bytes.
**Wat dit nog niet bewijst:** of het een crack is of een bouwvariant van Sega, en of de dongle-laag
elders (de `amDongleRead/WriteEeprom`-route) ook nog wordt gebruikt. Daarvoor is de vervolgstap in §4
nodig.

---

## 4. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

**Statisch, zonder bench:**

1. Zoek in `ramboM.elf` de PLT-stub van `kswap_collect` en zijn aanroepers (daar is de naam bekend —
   dat is de **ijking** van de methode). Herhaal daarna op de gestripte `apacheM.elf` en `hod4Mn.elf`.
   Vraag: wordt `kswap_collect` door de dongle-poort gebruikt, of ergens anders?
2. Lees de **ongepatchte** poort: disassembleer in `hod4Mn.elf` of `apacheM.elf` de aanroeper van
   `amDongleDecryptEx` (via de `DT_JMPREL`-index van die naam) en leg vast wat er met de willekeurige
   sleutel gebeurt **ná** de vergelijking.
3. Ontwerp het antwoord: een eigen `libkswapapi.so` met dezelfde drie namen als de cab-versie van
   Rambo (`kswap_collect`, `__kswap_collect`, `collect_pf`).

**Dan één benchrun** (pas op een vrije bench): `ramboM.elf` onder onze eigen lader met een **loggende**
`libkswapapi.so` (logt argumenten en terugkeerwaarde van `kswap_collect`) en een `LD_PRELOAD`-logger op
`open()`.

**Positieve controle:** dezelfde logger moet de `/dev/input/js%d`- en `/dev/console`-openingen van de
JVS-laag laten zien; ziet de logger die niet, dan logt hij niet en is de uitkomst waardeloos.
**Wat de hypothese zou weerleggen:** start het spel zonder dongle en met `kswap_collect(){return 0;}`
gewoon door, dan is de dongle-laag voor Rambo **niet afgedwongen** en blijft alleen de
bibliothekenlijst over.

---

## 5. Bewijsvoering — ijking

| scanner | ijkpunt | uitslag |
|---|---|---|
| `readelf -sW` | `ramboM.elf` moet `.symtab` hebben, een gestripte ELF niet | **PASS** — 20.024 symbolen tegen 132 in `hod4testM.elf` |
| volledige-symboollezer | `amDongleDecryptEx` moet als `FUNC` met een niet-nul adres verschijnen | **PASS** — `0x082c40fb` |
| stringzoeker | `hasp` moet **0** zijn terwijl `dongle` **81** is (dus de zoeker werkt en de afwezigheid is echt) | **PASS** |
| byte-signatuurzoeker | de force-true-sequentie moet in `ramboM.elf` gevonden worden nadat ik hem daar met `objdump` had gezien | **PASS** — 4× |
| `objdump`-bereik | het opgegeven adresbereik moet de functiekop bevatten | **PASS** — `amDongleInit` op `0x082c4746` |

**Gereedschap (op de deck, read-only):** `/home/deck/rgwork/s3_elfscan.sh`, `s4_segaapi.sh`,
`s5_amd.sh`, `s6_donglecode.sh`, `s7_step2inv.sh`, `s12_amdcode.sh`, `s13_patchcheck.sh`; uitvoer in de
bijbehorende `.out`-bestanden. Er is niets op de NAS geschreven.
