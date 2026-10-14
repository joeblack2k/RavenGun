# The House of the Dead 4 — beschermingsstatus

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, lader of sandbox gestart** (de bench is
bezet). Elke scanner is eerst geijkt — zie §5.

**Spel:** The House of the Dead 4 (Sega Lindbergh, 32-bits Linux-ELF).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/The House of the Dead 4/`
**Hoort bij:** [`../ELF-SEGA-FAMILIE-20261014.md`](../ELF-SEGA-FAMILIE-20261014.md)

**Kernbevinding:** de bescherming zit **in de ELF** (de `amDongle`-laag, statisch gelinkt, hier
`Ver.0.05 Build:Feb 27 2007`), en anders dan bij Rambo en HOTD EX **lijkt het controlepad hier nog
intact** — de force-true-signatuur komt in deze bestanden niet voor. De Windows-lader draagt alleen een
vervangende `libkswapapi.so` bij met `kswap_collect(){return 0;}`.

---

## 1. De tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **The House of the Dead 4** | `amDongle`-dongle op **poort-I/O**, statisch in de ELF; poort `CJvsMgr::CheckDongleDecrypt`, **controlecode lijkt ongepatcht** | 16-byte `AccessKey` → `amDongleChipInfo` → chipinfo `0x0101`; dan 1 KiB `KeyCheckEncData` ontsleutelen met acht willekeurige 16-bits woorden en vergelijken met `KeyCheckOrgData` (`strncmp`, `0x400`) | **nee** — nog geen eigen antwoord. TeknoParrot antwoordt met `libkswapapi.so` waarin `kswap_collect(){return 0;}` | een eigen `libkswapapi.so` (`kswap_collect`, `collect_pf`), `libkswapapi.so` en `libsegaapi.so` voor de gast (de game levert ze **niet** zelf), plus `libposixtime.so.2.4` en `libstdc++.so.5` | statisch: zoek de PLT-stub van `kswap_collect` en zijn aanroepers (§4). Daarna één benchrun met een loggende `libkswapapi.so` |

---

## 2. Wat er gemeten is

**MEASURED** — `readelf -h -d`: `hod4M.elf` is ELF32 i386 `EXEC`, entry `0x804cf90`, **272**
sectiekoppen, 13 `DT_NEEDED`: `libsegaapi.so`, `libkswapapi.so`, `libX11.so.6`, `libXext.so.6`,
`libGL.so.1`, `libGLU.so.1`, `libm.so.6`, `libpthread.so.0`, `libposixtime.so.2.4`, `libstdc++.so.5`,
`libgcc_s.so.1`, `libc.so.6`, `libdl.so.2`. `hod4M_offscreen.elf` en `hod4Mn.elf`: **282**
sectiekoppen, entry `0x804cf70`. Geen HASPE-marker. **Ongepakte** klasse
([`../ELF-LADER-SPEC.md`](../ELF-LADER-SPEC.md)), **306** UND-symbolen.

**MEASURED** — **gestript**: 0 debug-secties en **geen** `.symtab`. De identificatie van de
beschermingslaag loopt hier dus via de stringwoordenschat, niet via symbolen.

**MEASURED** — `strings -a -n 4 hod4M.elf | grep -c -i -E '<patroon>'`: `dongle` 36 · `amDongle` 36 ·
`jvs` 7 · `ioctl` 3 · `/dev/` 21 · `region` 1 · **`hasp` 0 · `keychip` 0 · `nethasp` 0 · `HASPE` 0 ·
`protect` 0 · `licen` 0**.

**MEASURED** — de volledige `amDongle`-woordenschat is identiek aan die van Rambo (39 functies),
inclusief `amDongleBit_SetData_and_Strobe`, `amDongleSetIv`, `amDongleSetSeedRecv/Send`,
`amDongleEncrypt/DecryptEx`, `amDongleRead/WriteEeprom`, `amDongleRead/WriteSram`, `amDongleChipInfo`,
`amDongleUserInfo` en de bouwstempel `amDongle Ver.0.05 Build:Feb 27 2007 19:51:30`. De logregels
noemen `amDongleInit: chip exists.`, `amDongleInit: enter develop mode.`, `amDongleInit: Rsv06`.

**MEASURED** — welke bestanden de game **niet** meelevert: in `elf/` staan alleen de ELF's, een
testmenu, `game`, wat TGA/PNG-materiaal, `libposixtime.so`/`.so.2.4` en **`libsegaapi-1.01.0001`**
(75.055 B). `libsegaapi.so` is audio (309 `SEGAAPI_*`-uitvoeren, **geen** beveiligingsfunctie — zie
[`../ELF-SEGA-FAMILIE-20261014.md`](../ELF-SEGA-FAMILIE-20261014.md) §3.2). **`libkswapapi.so` staat
er niet**, terwijl de ELF hem als `DT_NEEDED` noemt: die moet dus van buiten komen (de cab leverde hem,
TeknoParrot zet zijn stub in `ElfLdr2/libs/`).

**MEASURED** — de poort in de ELF: `strings` bevat `/dev/i2c/0`, `/dev/lbb`, `/dev/console`,
`/dev/hdb3`, `/dev/input/js%d`. Het **`/dev/i2c/0`-pad hoort bij `amEepromSmbusAccess`** (de
backup-EEPROM), niet bij de dongle: de dongle loopt bij deze familie over **poort-I/O** (`in`/`out`
met `out %al,$0x80` als vertraging), zoals bij Rambo gedemonteerd is.

**MEASURED** — het opstartscript `elf/game`:
```
app_path=$(pwd)
if [ "$1" != "-t" ]; then /X11R6/bin/xinit $app_path/hod4M.elf $1 -- /X11R6/bin/X -s 0 -nolisten tcp
else                       /X11R6/bin/xinit $app_path/hod4testM.elf $1 -- /X11R6/bin/X -s 0 -nolisten tcp ; fi
```
Het testmenu (`hod4testM.elf`, 262.540 B, 31 sectiekoppen) vraagt alleen GL/glut en heeft **geen**
`amDongle`.

**MEASURED** — profielen: `HOTD4.xml` (`EmulatorType=Lindbergh`, test `hod4testM.elf`) en
`HOTD4ELF2.xml` (`EmulatorType=ElfLdr2`, `ExecutableName=hod4M.elf`), allebei
`DongleRegion=JAPAN` / `PcbRegion=JAPAN`.

**MEASURED** — varianten (md5): `hod4M_offscreen.elf` = `hod4Ms.elf` (`4f714e2c4ab996fdf8e82767c89e5c40`);
`hod4M.elf` (`03640802…`), `hod4Mn.elf` (`9c372490…`), `hod4Mn_HD.elf`, `hod4Mn_VGA.elf`,
`hod4Ms_HD.elf`, `hod4Ms_VGA.elf` zijn alle verschillend. Wie `_HD` of `_VGA` wil, doet eigen werk.

---

## 3. Is de controle hier nog intact?

**MEASURED** — de symboolonafhankelijke byte-zoektocht (`s13_patchcheck.sh`):

| ELF | force-true (`c7 40 08 01 00 00 00 5d c3`) | force-false (`c7 40 08 00 00 00 00 5d c3`) |
|---|---|---|
| `hod4M.elf` | **0** | 2 |
| `hod4Mn.elf` | **0** | 2 |
| `ramboM.elf` (controle, bekend geforceerd) | **4** | 4 |
| `apacheM.elf` | **0** | 8 |

**INFERRED (matig sterk):** de dongle-controle van HOTD4 is **niet** op dezelfde manier geforceerd als
bij Rambo. Dat maakt HOTD4 juist **interessant**: hier staat het originele controlepad vermoedelijk nog
in het bestand, terwijl Rambo het al kwijt is. Wie de poort wil lezen om hem na te bouwen, kan dat het
beste hier of bij Too Spicy doen.

**Wat dit nog niet bewijst:** de gebruikte signatuur is streng (hij eindigt op `pop ebp; ret`). Een
patch die de vlag zet en daarna nog iets doet, wordt gemist. En omdat deze ELF gestript is, is er geen
symbolgrootte om tegen te controleren. De vervolgstap in §4 sluit dat gat.

---

## 4. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

**Statisch, zonder bench:**

1. Zoek de **PLT-stub van `kswap_collect`** in `hod4M.elf` (de UND-import is er; de `DT_JMPREL`-index
   wijst de stub aan) en daarna zijn aanroepers in `.text`. **IJking:** doe dezelfde zoektocht in
   `ramboM.elf`, waar de symbolen nog namen hebben — daar moet dezelfde structuur tevoorschijn komen.
2. Lees de **aanroeper van `amDongleDecryptEx`** in `hod4Mn.elf`: leg vast wat er met de acht
   willekeurige woorden gebeurt ná de `strncmp`-vergelijking, en of het resultaat ergens anders nog
   wordt gebruikt. (In `ramboM.elf` staat die body nog, op `0x080e3ee3`–`0x080e3f6e` — dat is het
   referentiebeeld.)
3. Bepaal welke van de twee — de dongle-check of `kswap_collect` — de dichte deur is. Zolang dat niet
   duidelijk is, is een antwoord op de verkeerde laag mogelijk.

**Dan één benchrun** (pas op een vrije bench): `hod4M.elf` onder onze eigen lader met een **loggende**
`libkswapapi.so` en een `LD_PRELOAD`-logger op `open()`.

**Positieve controle:** de logger moet de `/dev/input/js%d`- en `/dev/console`-openingen van de
JVS-laag laten zien. **Wat de hypothese zou weerleggen:** start het spel zonder dongle door, dan is de
dongle-laag **niet afgedwongen**.

---

## 5. Bewijsvoering — ijking

| scanner | ijkpunt | uitslag |
|---|---|---|
| `readelf -sW` | een gestripte ELF moet 0 `.symtab` geven, en dat mag niet als "geen bescherming" gelezen worden | **PASS** — 0, en daarom is overgestapt op strings + byte-signaturen |
| stringzoeker | `dongle` moet 36× gevonden worden terwijl `hasp` 0× is | **PASS** |
| stringzoeker, negatieve controle | `zzqqxx_not_present` in hetzelfde bestand | **PASS** — 0 |
| `nm -D`-uitvoerlezer | de meegeleverde `libsegaapi-1.01.0001` moet uitvoeren tonen | **PASS** — 309, en alle 309 zijn audio |
| byte-signatuurzoeker | de signatuur moet in `ramboM.elf` 4× gevonden worden (bekend uit `objdump`) voordat een 0 in `hod4M.elf` iets betekent | **PASS** |

**Gereedschap (op de deck, read-only):** `/home/deck/rgwork/s3_elfscan.sh`, `s4_segaapi.sh`,
`s5_amd.sh`, `s6_donglecode.sh`, `s7_step2inv.sh`, `s12_amdcode.sh`, `s13_patchcheck.sh`. Er is niets op
de NAS geschreven.
