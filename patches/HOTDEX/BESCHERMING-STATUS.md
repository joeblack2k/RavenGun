# The House of the Dead EX — beschermingsstatus

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, lader of sandbox gestart** (de bench is
bezet). Elke scanner is eerst geijkt — zie §5.

**Spel:** The House of the Dead EX (Sega Lindbergh, 32-bits Linux-ELF).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/The House of the Dead EX/`
**Hoort bij:** [`../ELF-SEGA-FAMILIE-20261014.md`](../ELF-SEGA-FAMILIE-20261014.md)

**Kernbevinding:** dezelfde `amDongle`-laag als Rambo (`Ver.0.12 Build:Feb 21 2008`) en, net als bij
Rambo, **al geforceerd tot "geslaagd"** — met de patch in de *wrapper* in plaats van in de methode.
Daarnaast is `libsegaapi.so` hier als **`.bak`** achtergelaten: de naam die de ELF als `DT_NEEDED` noemt,
bestaat niet meer.

---

## 1. De tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **The House of the Dead EX** | `amDongle`-dongle op poort-I/O, statisch in de ELF; poort **al geforceerd** (4/4 varianten) | 16-byte `AccessKey` → `amDongleChipInfo` → `0x0101`; dan 1 KiB `KeyCheckEncData` ontsleutelen en met `KeyCheckOrgData` vergelijken. Daarnaast `/dev/i2c/0` (`amEepromSmbusAccess`), `/dev/lbb`, `/dev/hdb3` en `amJvspReqKeyCodeInput`/`amJvspAckKeyCodeInput` | **nee** — nog geen eigen antwoord. TeknoParrot antwoordt met `libkswapapi.so` waarin `kswap_collect(){return 0;}` | een eigen `libkswapapi.so`; **`libsegaapi.so`** (staat hier alleen als `libsegaapi.so.bak`); `libcri_soundoutput_lindbergh_jr.so` en `librnalindbergh_jr.so` zijn er wél | statisch: lees de aanroepers van `amDongleDecryptEx` en `kswap_collect`; daarna één benchrun met een loggende `libkswapapi.so` |

---

## 2. Wat er gemeten is

**MEASURED** — `readelf`: `hodexRI.elf` (9.544.868 B) en `hodexRI_vga.elf` (9.544.868 B) zijn ELF32
i386 `EXEC`, 13 `DT_NEEDED` (zelfde lijst als Rambo), geen HASPE-marker, **374** UND-symbolen.
`hodextestR.elf`/`hodextestR_vga.elf` zijn 540.827 B en het testmenu.

**MEASURED** — **niet gestript**: `.symtab` aanwezig (17.893 `FUNC`) en 4 debug-secties. Daardoor zijn
de adressen bekend:

| symbool | adres |
|---|---|
| `amDongleInit` | `0x084ba886` |
| `amDongleExit` | `0x084b92a0` |
| `amDongleIsAvailable` | `0x084b9341` |
| `amDongleDecryptEx` | `0x084ba23b` |
| `amDongleEncryptEx` | `0x084ba019` |
| `amDongleChipInfo` | `0x084ba7cd` |
| `_Z25JvsMgr_CheckDongleDecryptv` | `0x08189614` (grootte **202**) |
| `amOsinfoCheckDongleRental` | `0x084bf5bf` |
| `AccessKey` | `0x0854449a` |
| `KeyCheckEncData` | `0x08784a60` (**1024** B) |
| `KeyCheckOrgData` | `0x08784660` (**1024** B) |
| `amDongleSeedRecv` / `amDongleSeedSend` | `0x0854447a` / `0x0854448a` |

De bronbestandsnaam `am_dongle.c` staat in de stringtabel; de bouwstempel is
`amDongle Ver.0.12 Build:Feb 21 2008 18:00:12`.

**MEASURED** — `strings -a -n 4 hodexRI.elf | grep -c -i -E '<patroon>'`: `dongle` 80 · `amDongle` 77 ·
`jvs` 168 · `ioctl` 12 · `/dev/` 12 · `protect` 6 · `region` 5 · **`hasp` 0 · `keychip` 0 ·
`nethasp` 0 · `HASPE` 0 · `segaapi` 0**.

**MEASURED** — de `amDongle`-woordenschat is identiek aan Rambo (39 functies, `amDongleBit_*`,
`SetIv`, `SetSeedRecv/Send`, `Encrypt/DecryptEx`, `Read/WriteEeprom`, `Read/WriteSram`, `ChipInfo`,
`UserInfo`). Kernteksten die hier extra staan: `amEepromSmbusAccess`,
`amJvspReqKeyCodeInput`/`amJvspAckKeyCodeInput`, `  REGION  `, `JAPANESE `,
`UserBackupData_DefaultTbl_JPN`/`_USA`, `UserEepData_DefaultTbl_JPN`/`_USA`,
`UserRankData_DefaultTbl_JPN`/`_USA`, `g_iName_Forbid_JPN`/`_USA`. **Dat is de regiomechanica van deze
titel: regiogebonden tabellen in de ELF, niet een `keychip.appboot.region`-vraag** (0 treffers).

**MEASURED** — de spelmap `elf/`: `game.sh`, `gamevga.sh`, `hodexRI.elf`, `hodexRI_vga.elf`,
`hodextestR.elf`, `hodextestR_vga.elf`, `libcri_soundoutput_lindbergh_jr.so` (14.010 B),
`librnalindbergh_jr.so` (15.153 B), **`libsegaapi.so.bak`** (67.784 B), lettertype-materiaal,
`logo.tga`, `vssver.scc`. **Geen `libkswapapi.so`.**

> **`libsegaapi.so.bak` is een beslissende afwijking.** De ELF noemt `libsegaapi.so` als `DT_NEEDED`;
> het bestand met die naam bestaat niet, en de enige kandidaat is met `.bak` uitgezet. Of dat een
> overblijfsel van een packer is of van een eerdere bewerker, is niet bepaald. **Er is geen
> `libkswapapi.so` in de map**, dus beide gastbibliotheken moeten van buiten komen.

**MEASURED** — `elf/game.sh` en `elf/gamevga.sh`:
```
game.sh:     export GLIBCXX_FORCE_NEW=1; LD_LIBRARY_PATH=…:/home/disk0/elf;
             pushd /home/disk0/elf; ./hodexRI.elf $1   (of ./hodextestR.elf $1 bij -t)
gamevga.sh:  idem, maar `gdbserver 127.0.0.1:6666 hodexRI_vga.elf $1` bij normaal bedrijf
```
De VGA-variant wordt dus onder `gdbserver` gestart — een debugspoor, geen bescherming.

**MEASURED** — profiel: `HOTDEX.xml`, `EmulatorType=ElfLdr2`, `ExecutableName=hodexRI.elf`, test
`hodextestR.elf`, `DongleRegion`/`PcbRegion` aanwezig.

**MEASURED** — varianten (md5): `hodexRI.elf` `69a85f6f…` ≠ `hodexRI_vga.elf` `511702db…`;
`hodextestR.elf` `834e46dc…` ≠ `hodextestR_vga.elf` `0e58a373…`. Alle vier zijn dus eigen builds.

---

## 3. De poort is hier ook al geforceerd

**MEASURED** — `objdump -d --start-address=0x08189614 --stop-address=0x08189660 hodexRI.elf`:

```
08189614 <_Z25JvsMgr_CheckDongleDecryptv>:
 8189614:  a1 bc fe 9d 0a        mov    0xa9dfebc,%eax
 8189619:  c7 40 08 01 00 00 00  movl   $0x1,0x8(%eax)     <-- vlag geforceerd op 1
 8189620:  c3                    ret
 8189621:  04 24                 add    $0x24,%al          <-- hier begint de ORIGINELE body,
 8189623:  6a 00                 push   $0x0                  onbereikbaar, midden in een instructie
 8189625:  6a 20                 push   $0x20
 8189627:  e8 e2 eb 1b 00        call   834820e <IzHeapAlloc>
```

De symbolgrootte van de wrapper is nog **202** terwijl de body 13 bytes is — hetzelfde beeld als bij
`ramboM.elf`: de functiekop is overschreven en de originele code staat er nog achter.

**MEASURED, symboolonafhankelijk:** de force-true-sequentie `c7 40 08 01 00 00 00 5d c3` komt in
`hodexRI.elf` **0×** voor — omdat de patch hier in de wrapper zit en niet op een objectveld met
`pop ebp; ret` erachter. De "force-false"-variant komt 10× voor. **Dit is de belangrijkste les van deze
titel:** een negatieve uitkomst op één byte-signatuur bewijst niets; je moet de functie zelf lezen.
Voor Rambo, waar de patch in de methode zit, gaf dezelfde zoeker wél 4 treffers.

**INFERRED (sterk):** de dongle-controle van HOTD EX is geneutraliseerd. Dat betekent dat het
`amDongle`-protocol hier **niet** de dichte deur is — de vraag wordt eerder: wat vraagt deze titel
verder, en is dat zonder dongle te halen?

---

## 4. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

**Statisch, zonder bench:**

1. Zoek de **PLT-stub van `kswap_collect`** in `hodexRI.elf` en zijn aanroepers. Omdat deze ELF **niet
   gestript** is, kan dat hier volledig met namen — dat maakt HOTD EX de **goedkoopste proef van de
   vijf** voor deze vraag. **IJking:** de aanroep van `amDongleDecryptEx` moet in de onbereikbare body
   van de wrapper terug te vinden zijn (vergelijk `ramboM.elf` `0x080e3f16`).
2. Bepaal wat er met `libsegaapi.so.bak` is gebeurd: is het dezelfde bibliotheek als HOTD4's
   `libsegaapi-1.01.0001` (75.055 B) of een andere build (67.784 B)? `md5sum` + `nm -D` op beide.
3. Zoek uit of `libcri_soundoutput_lindbergh_jr.so` en `librnalindbergh_jr.so` (CRI-audio) door de game
   geladen worden — die zijn er wél en staan in geen enkele `DT_NEEDED`-lijst.

**Dan één benchrun** (pas op een vrije bench): `hodexRI.elf` onder onze eigen lader met een loggende
`libkswapapi.so` + `open()`-logger.

**Positieve controle:** de logger moet `/dev/input/js%d` en `/dev/console` laten zien.
**Wat de hypothese zou weerleggen:** start het spel door, dan is de dongle-laag niet afgedwongen — wat
hier, gezien de patch, het meest waarschijnlijke antwoord is.

---

## 5. Bewijsvoering — ijking

| scanner | ijkpunt | uitslag |
|---|---|---|
| `readelf -sW` | `hodexRI.elf` moet `.symtab` hebben (anders is de adreslijst onbruikbaar) | **PASS** — 17.893 `FUNC` |
| volledige-symboollezer | `amDongleInit` moet als `FUNC` met niet-nul adres verschijnen | **PASS** — `0x084ba886` |
| `objdump`-bereik | het opgegeven bereik moet een leesbare functiekop geven | **PASS** — de wrapper op `0x08189614` |
| byte-signatuurzoeker | de signatuur moet in `ramboM.elf` 4× gevonden worden — en dat deed hij — voordat een 0 hier als "niet gepatcht" gelezen wordt | **PASS**, en juist daarom is hier **niet** "niet gepatcht" geconcludeerd maar de functie zelf gelezen |
| stringzoeker | `dongle` 80× terwijl `hasp` 0× | **PASS** |
| `md5sum` | de vier varianten moeten onderling verschillen | **PASS** |

**Gereedschap (op de deck, read-only):** `/home/deck/rgwork/s3_elfscan.sh`, `s4_segaapi.sh`,
`s5_amd.sh`, `s6_donglecode.sh`, `s7_step2inv.sh`, `s13_patchcheck.sh`. Er is niets op de NAS
geschreven.
