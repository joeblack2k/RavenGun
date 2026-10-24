# Too Spicy (Apache) — beschermingsstatus

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, lader of sandbox gestart** (de bench is
bezet). Elke scanner is eerst geijkt — zie §5.

**Spel:** Too Spicy / Apache (Sega Lindbergh, 32-bits Linux-ELF).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Too Spicy/`
**Hoort bij:** [`../ELF-SEGA-FAMILIE-20261014.md`](../ELF-SEGA-FAMILIE-20261014.md)

**Kernbevinding:** dezelfde `amDongle`-laag (`Ver.0.12 Build:Feb 14 2007`), in de ELF, met een
**ongepatcht** controlepad. Deze titel is de **duurste van de vijf om te lezen** (volledig gestript,
geen symbolen) en mist **beide** gastbibliotheken (`libsegaapi.so` én `libkswapapi.so`) volledig.

---

## 1. De tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **Too Spicy (Apache)** | `amDongle`-dongle op poort-I/O, statisch in de ELF; **controlecode lijkt ongepatcht** | als de andere vier: `AccessKey` → `amDongleChipInfo` → `0x0101`; dan 1 KiB `KeyCheckEncData` ontsleutelen en met `KeyCheckOrgData` vergelijken. Daarnaast `/dev/console`, `/dev/lbb`, `/dev/hdb3`, `/dev/ttyS0`, `/dev/ttyS1` | **nee** — nog geen eigen antwoord. TeknoParrot antwoordt met `libkswapapi.so` waarin `kswap_collect(){return 0;}` | een eigen `libkswapapi.so` **en** een `libsegaapi.so` (geen van beide staat in `elf/`); alle overige gastbibliotheken (OpenAL staat wél in `drv/openal/`) | **laatst van de vijf**: zonder symbolen is elke statische stap duurder. Begin bij Rambo en HOTD EX, gebruik de daar gevonden adressen als patroon, en pas die hier via de `DT_JMPREL`-route toe |

---

## 2. Wat er gemeten is

**MEASURED** — `readelf`: `apacheM.elf` (6.517.812 B) en `apacheM_VGA.elf` zijn ELF32 i386 `EXEC`,
13 `DT_NEEDED` (zelfde lijst als Rambo), geen HASPE-marker, **386** UND-symbolen, en
**volledig gestript**: 0 `.symtab`, 0 debug-secties. `apachetestM.elf` (337.768 B) is het testmenu en
heeft géén `amDongle`.

**MEASURED** — `strings -a -n 4 apacheM.elf | grep -c -i -E '<patroon>'`: `dongle` 37 · `amDongle` 37 ·
`jvs` 10 · `ioctl` 9 · `/dev/` 15 · `region` 2 · **`hasp` 0 · `keychip` 0 · `nethasp` 0 ·
`HASPE` 0 · `segaapi` 0**.

**MEASURED** — de `amDongle`-woordenschat is identiek aan de andere vier, met de bouwstempel
`amDongle Ver.0.12 Build:Feb 14 2007 17:39:06`, en met één regel die alleen hier voorkomt:
`amDongleSetWorkaround: Invalid workaround keyword, %s.` — er is dus een **workaround-mechanisme** in
deze dongleversie. Wat het doet is niet bepaald; het is een concreet aanknopingspunt.

**MEASURED** — de spelmap `elf/`: `game`, `apacheM.elf`, `apacheM_HD.elf`, `apacheM_VGA.elf`,
`apachetestM.elf`, `apachetestM_HD.elf`, `apachetestM_VGA.elf`, `buffer.ini`, `logo.tga`,
lettertype-materiaal. **Geen enkele `.so`.** De game vraagt dus `libsegaapi.so` **en**
`libkswapapi.so`, en levert geen van beide. In `drv/` staan `nvidia/8769` (het oude driverpakket) en
`openal/` met `libopenal.so.0`, `libalut.so.0` — die zijn er dus wel.

**MEASURED** — `elf/game`:
```
if [ "$1" = "-t" ]; then /X11R6/bin/xinit /home/disk0/elf/apachetestM.elf $1 -- /X11R6/bin/X -s 0 -nolisten tcp
else                     /X11R6/bin/xinit /home/disk0/elf/apacheM.elf     $1 -- /X11R6/bin/X -s 0 -nolisten tcp ; fi
```
`elf/buffer.ini` is een renderbufferbeschrijving (`DisplayBuffer`, `GameBuffer`, `HalfBuffer`,
`QuadBuffer`) — beeldinstelling, geen bescherming.

**MEASURED** — profielen: `2Spicy.xml` (`EmulatorType=Lindbergh`) en `2SpicyElf2.xml`
(`EmulatorType=ElfLdr2`), beide `ExecutableName=apacheM_HD.elf;apacheM.elf` (dus met terugval),
test `apachetestM.elf`, `DongleRegion`/`PcbRegion` aanwezig.

**MEASURED** — varianten (md5): `apacheM.elf` = `apacheM_HD.elf` (`e796e6672c00dde996eb3f1146d694fc`);
`apacheM_VGA.elf` is anders (`b98e989bc126b9f6a64a0c6b31556867`). Bij het testmenu idem:
`apachetestM.elf` = `apachetestM_HD.elf` (`6e8a6ebf…`), `_VGA` anders (`f0e6f237…`).

---

## 3. Is de controle hier nog intact?

**MEASURED** — de symboolonafhankelijke byte-zoektocht:

| ELF | force-true (`c7 40 08 01 00 00 00 5d c3`) | force-false (`…00…`) |
|---|---|---|
| `apacheM.elf` | **0** | 8 |
| `apacheM_VGA.elf` | **0** | 8 |
| `ramboM.elf` (controle, bekend geforceerd) | **4** | 4 |

**INFERRED (matig sterk):** het originele controlepad lijkt hier nog aanwezig. Omdat deze ELF geen
symbolen heeft, is de signatuurzoektocht hier **zwakker** dan bij HOTD4: er is geen functiegrootte om
tegen te controleren en geen naam om op te zoeken. De vervolgstap in §4 is daarom verplicht voordat dit
hard genoemd mag worden.

**Waarom deze titel als laatste:** voor de andere vier is er minstens één ELF met namen (Rambo, HOTD EX)
of een kleinere, beter leesbare ELF (HOTD4 Special). Wie de methode op Too Spicy wil toepassen, moet
hem eerst elders hebben geijkt — anders is een leeg resultaat niet te onderscheiden van een fout
gereedschap.

---

## 4. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

**Statisch, zonder bench, en pas ná Rambo/HOTD EX:**

1. Zoek de **PLT-stub van `kswap_collect`** in `apacheM.elf`. Omdat er geen symbolen zijn: gebruik de
   `DT_JMPREL`-lijst (`readelf -r`) om de `R_386_JMP_SLOT` van de `kswap_collect`-index te vinden, en
   zoek daarna in `.text` naar de `call` naar dat adres. **IJking verplicht:** dezelfde methode moet in
   `ramboM.elf` dezelfde structuur opleveren, waar de naam bekend is.
2. Lees de aanroeper van `amDongleDecryptEx` op dezelfde manier (via de `DT_JMPREL`-index van die naam).
   Het referentiebeeld staat in `ramboM.elf` op `0x080e3ee3`–`0x080e3f6e` en in `hodexRI.elf` (niet
   gestript, dus met namen).
3. Onderzoek `amDongleSetWorkaround` — welke trefwoorden accepteert deze versie, en is dat een
   ontwikkelaarsstand die het spel zelf kan zetten?
4. Bepaal de twee ontbrekende bibliotheken: is `libsegaapi.so` hier dezelfde als HOTD4's
   1.01.0001 (75.055 B) of de 1.03.0000-versie uit HOTD4 Special (65.036 B)?

**Dan één benchrun** (pas op een vrije bench): `apacheM.elf` met een loggende `libkswapapi.so` +
`open()`-logger.

**Positieve controle:** de logger moet `/dev/console`, `/dev/lbb` of `/dev/ttyS0` laten zien.
**Wat de hypothese zou weerleggen:** start het spel door zonder dongle, dan is de dongle-laag niet
afgedwongen.

---

## 5. Bewijsvoering — ijking

| scanner | ijkpunt | uitslag |
|---|---|---|
| `readelf -sW` | een gestripte ELF moet 0 `.symtab` geven — en dat mag **niet** als "geen bescherming" gelezen worden | **PASS** — 0, dus overgestapt op strings + `DT_JMPREL` |
| `readelf -r` | de `kswap_collect`-index moet in de `JMPREL`-lijst voorkomen (anders is de aanpak in §4 onmogelijk) | **PASS** — `kswap_collect` staat als UND-import en heeft dus een `R_386_JMP_SLOT` |
| stringzoeker | `dongle` 37× terwijl `hasp` 0× | **PASS** |
| stringzoeker, negatieve controle | `zzqqxx_not_present` in hetzelfde bestand | **PASS** — 0 |
| byte-signatuurzoeker | de signatuur moet in `ramboM.elf` 4× gevonden worden — en dat deed hij — voordat een 0 hier iets betekent | **PASS** |
| `md5sum` | `apacheM.elf` en `apacheM_HD.elf` moeten identiek zijn, `_VGA` niet | **PASS** |

**Gereedschap (op de deck, read-only):** `/home/deck/rgwork/s3_elfscan.sh`, `s4_segaapi.sh`,
`s5_amd.sh`, `s6_donglecode.sh`, `s7_step2inv.sh`, `s13_patchcheck.sh`. Er is niets op de NAS
geschreven.
