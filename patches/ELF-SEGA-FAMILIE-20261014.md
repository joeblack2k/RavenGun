# De vijf Sega-ELF-titels: zit de bescherming in de ELF of in de Windows-lader?

**Datum:** 2026-10-14 · **Lane:** licentie/hardware-emulatie · **Methode:** statische analyse van de
archiefkopie op de NAS. **Alles alleen gelezen; er is geen spel, lader of sandbox gestart** (de bench is
bezet). Elke scanner is eerst op een bekend antwoord geijkt — zie §7.

**Titels:** Rambo, The House of the Dead 4, The House of the Dead 4 Special, The House of the Dead EX,
Too Spicy (Apache).
**Werkmap:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/<titel>/`

**Labels:** **MEASURED** = uit de bestanden gelezen, commando erbij · **INFERRED** = gevolgtrekking ·
**NIET BEPAALD** = niet gemeten.

---

## 1. Het antwoord in één alinea

**De controle zit ín de ELF.** In alle vijf de titels is de `amDongle`-laag statisch in het spelbeeld
gelinkt: de bit-banged bus, de `AccessKey`, de chipinfocheck op `0x0101`, het sleutelbeheer
(`SetIv`, `SetSeedRecv/Send`) en het coderingspaar `amDongleEncrypt/Decrypt`. De eigenlijke poort is
`CJvsMgr::CheckDongleDecrypt`: die trekt acht willekeurige 16-bits woorden, laat de dongle een blok van
**1024 bytes** (`KeyCheckEncData`) ontsleutelen en vergelijkt de uitkomst met **`KeyCheckOrgData`**, ook
1024 bytes, met `strncmp(..., 0x400)`. **Beide blokken zitten ín de ELF.** Wat de Windows-lader doet is
iets heel anders en heel kleins: hij levert een **vervangende `libkswapapi.so`** waarvan de hele
`kswap_collect` uit `xor %eax,%eax; ret` bestaat — dus `return 0;`. Er worden **geen ELF-bytes** door de
lader gepatcht. Voor onze eigen route betekent dat: de bescherming is *niet* iets wat alleen in de
Windows-lader leeft, maar het antwoord erop is **twee instructies en een `.so`** — zie §5.

Daar komt één extra meting bij die het kostenbeeld scherper maakt: **Rambo (alle vier varianten) en
The House of the Dead EX hebben hun eigen dongle-controle al geforceerd** (`movl $0x1,0x8(%eax); ret`),
terwijl HOTD4, HOTD4 Special en Too Spicy het originele controlecode nog intact hebben — zie §4.

---

## 2. De gevraagde tabel

| Game | bescherming | wat het vraagt | antwoorden wij het? | wat ontbreekt | voorgestelde proef |
|---|---|---|---|---|---|
| **Rambo** | `amDongle`-dongle in de ELF, poort-I/O; **check in deze dump al geforceerd** (4/4 varianten) | 16-byte `AccessKey` naar de dongle, chipinfo `0x0101`, daarna 1 KiB `KeyCheckEncData` ontsleutelen en met `KeyCheckOrgData` vergelijken | **nee** — nog geen eigen antwoord. TP antwoordt met `kswap_collect(){return 0;}` | een eigen `libkswapapi.so` (`kswap_collect`, `collect_pf`, `__kswap_collect`) + de gastbibliotheken die de cab leverde (`libsegaapi.so`, `libposixtime.so.1`, libGL/libGLU, libX11) | één statische stap: lees `amDongleInit`@`0x082c4746` naast `HOTD4`'s ongepatchte `JvsMgr_CheckDongleDecrypt` en bepaal of `kswap_collect` überhaupt door de check wordt aangeroepen. Daarna één benchrun met een loggende `libkswapapi.so` |
| **The House of the Dead 4** | `amDongle`-dongle in de ELF; **controlecode lijkt ongepatcht** | als Rambo, met `amDongle Ver.0.05`; vraagt daarnaast `libsegaapi.so`, `libkswapapi.so`, `libposixtime.so.2.4`, `libstdc++.so.5` | **nee** | idem + `libposixtime.so.2.4` en `libstdc++.so.5` (staan deels in `ElfLdr2/libs/`) | idem Rambo, op `hod4M.elf` (gestript: werken via de PLT-stub van `kswap_collect`) |
| **The House of the Dead 4 Special** | als HOTD4, **ongepatcht**; eigen `drv/ssound/lib/` met een `libsegaapi.so` die géén ELF is (23 bytes tekst) | als HOTD4 + `/dev/ttyS0`, `/dev/usb/tts/%d`, `amEepromSmbusAccess` | **nee** | dezelfde bibliotheken, plus een echt `libsegaapi.so` (de meegeleverde is beschadigd/vervangen) | idem; eerst de 23-byte `libsegaapi.so` onderzoeken — dat kan een eerdere worker de das omgedaan hebben |
| **The House of the Dead EX** | `amDongle` + **check al geforceerd** (ook 4/4 varianten, `Ver.0.12 Build:Feb 21 2008`) | als Rambo; `libsegaapi.so` staat als `libsegaapi.so.bak` in `elf/` (dus weggehaald) | **nee** | `libsegaapi.so` + `libkswapapi.so` + `libcri_soundoutput_lindbergh_jr.so`/`librnalindbergh_jr.so` zijn er wel | idem Rambo |
| **Too Spicy (Apache)** | `amDongle` + **ongepatcht** (gestript) | als Rambo, `Ver.0.12 Build:Feb 14 2007`; `/dev/ttyS0`, `/dev/ttyS1` | **nee** | `libsegaapi.so` én `libkswapapi.so` ontbreken **volledig** in `elf/` | idem; `apacheM.elf` is de duurste van de vijf (geen namen), dus na Rambo |

---

## 3. Wat er gemeten is, per laag

### 3.1 De klasse: ongepakt, met echte importnamen

**MEASURED** — `readelf -h -d <elf>`:

| ELF | klasse | entry | sectiekoppen | NEEDED | HASPE |
|---|---|---|---|---|---|
| `Rambo/elf/ramboM.elf` | ELF32 i386 EXEC | `0x804f2c0` | 39 | 13 | nee |
| `The House of the Dead 4/elf/hod4M.elf` | ELF32 i386 EXEC | `0x804cf90` | 272 | 13 | nee |
| `HOTD4/elf/hod4M_offscreen.elf`, `hod4Mn.elf` | ELF32 i386 EXEC | `0x804cf70` | 282 | 13 | nee |
| `HOTD4 Special/elf/hod4M.elf` | ELF32 i386 EXEC | `0x804d600` | 32 | 13 | nee |
| `HOTD EX/elf/hodexRI.elf` | ELF32 i386 EXEC | — | — | 13 | nee |
| `Too Spicy/elf/apacheM.elf` | ELF32 i386 EXEC | — | — | 13 | nee |

Dat is de **ongepakte klasse** uit [`ELF-LADER-SPEC.md`](ELF-LADER-SPEC.md) §"De twee klassen": echte
sectiekoppen, geen `PUSH 0xdeadbeef`-PLT, geen `PATCHED_WITH_HASPE_LINUX_LOADER`. De namen van de
imports zijn er: **402** UND-symbolen (Rambo), 306 (HOTD4), 318 (HOTD4 Special), 374 (HOTD EX), 386
(Too Spicy). Dit is de **goedkope** familie, niet de gepakte.

`DT_NEEDED` is bij alle vijf: `libsegaapi.so`, `libkswapapi.so`, `libX11.so.6`, `libXext.so.6`,
`libGL.so.1`, `libGLU.so.1`, `libm.so.6`, `libpthread.so.0`, `libposixtime*`, `libstdc++.so.*`,
`libgcc_s.so.1`, `libc.so.6`, `libdl.so.2`. De testmenu-ELF's (`hod4testM.elf`, `hodextestR.elf`,
`apachetestM.elf`) vragen daarentegen **alleen** GL/glut en hebben géén `amDongle` — dat zijn
gereedschappen, geen spellen.

### 3.2 `libsegaapi.so` is **niet** de bescherming — het is audio

**MEASURED** — `nm -D --defined-only "HOTD4/elf/libsegaapi-1.01.0001"` (75.055 B, ELF32 i386 DSO):
**309** uitvoeren, alles `SEGAAPI_*` buffer-/afspeel-/volumefuncties, EAX- en
`CLSID_/IID_/DEVICE_`-namen van de Sound Blaster Live-laag plus `libsega_app_init`/`_fini`. **Nul**
keychip-, dongle-, regio- of beveiligingsfuncties. De hypothese "de bescherming zit in `libsegaapi`"
is daarmee weerlegd, en dat is meetbaar, niet een mening.

### 3.3 `amDongle` zit in de ELF, met naam en adres

**MEASURED** — Rambo en HOTD EX zijn **niet gestript** (`.symtab` aanwezig; 20.024 symbolen /
15.490 `FUNC` bij Rambo; 8 respectievelijk 4 debug-secties). Daar staan de adressen:

| symbool | Rambo `ramboM.elf` | HOTD EX `hodexRI.elf` | grootte |
|---|---|---|---|
| `amDongleInit` | `0x082c4746` | `0x084ba886` | 583 |
| `amDongleExit` | `0x082c3160` | `0x084b92a0` | 161 |
| `amDongleIsAvailable` | `0x082c3201` | `0x084b9341` | 29 |
| `amDongleIsDevelop` | `0x082c321e` | `0x084b935e` | 29 |
| `amDongleSendEx` | `0x082c3680` | `0x084b97c0` | — |
| `amDongleDecryptEx` | `0x082c40fb` | `0x084ba23b` | — |
| `amDongleEncryptEx` | `0x082c3ed9` | `0x084ba019` | — |
| `amDongleReadEepromEx` | `0x082c449f` | `0x084ba5df` | — |
| `amDongleChipInfo` | `0x082c468d` | `0x084ba7cd` | 185 |
| `_ZN7CJvsMgr18CheckDongleDecryptEv` | `0x080e3e94` | — (alleen de wrapper) | 235 / 15 |
| `_Z25JvsMgr_CheckDongleDecryptv` | `0x080e4262` | `0x08189614` | 21 / 202 |
| `amOsinfoCheckDongleRental` | `0x082c947f` | `0x084bf5bf` | — |

Verder de objecten `amDongleVersion`, `amDongleSeedRecv`, `amDongleSeedSend`, `amDongleContext`,
`amDongleDebugLevel`, `AccessKey`, `KeyCheckOrgData`, `KeyCheckEncData` — en de bronbestandsnaam
**`am_dongle.c`** staat in de stringtabel. Bij HOTD4, HOTD4 Special en Too Spicy is het beeld
**gestript** (0 `.symtab`, 0 debug-secties); daar draagt de identificatie op de identieke
stringwoordenschat (hieronder), niet op symbolen.

### 3.4 De dongle spreekt **poort-I/O**, niet `/dev/i2c/0`

**MEASURED** — `objdump -d --start-address=0x082c4746 --stop-address=0x082c4900 ramboM.elf`. De eerste
tientallen instructies van `amDongleInit`:

```
82c481b:  movzwl 0xacadf68,%ecx      ; poortbasis 1
82c4822:  lea    0x2c(%ecx),%edx
82c4825:  in     (%dx),%eax          ; register lezen
82c4826:  out    %al,$0x80           ; I/O-vertraging (klassieke ISA-delaypoort)
82c482d:  or     $0x8,%ah
82c4830:  and    $0xcf,%ah
82c4833:  out    %eax,(%dx)          ; register schrijven
82c4836:  movzwl 0xacadf6a,%edx
82c483d:  add    $0x28,%edx
82c4840:  in     (%dx),%eax          ; tweede register
82c4848:  or     $0x38000000,%eax
82c484d:  out    %eax,(%dx)
82c4850:  mov    %ecx,%edx
82c4852:  in     (%dx),%eax
82c485a:  or     $0x70800,%eax
82c485f:  out    %eax,(%dx)
82c4862:  movl   $0x83a57f6,0x8(%esp) ; = AccessKey (16 bytes)
82c486a:  movl   $0x10,0x4(%esp)
82c4872:  movl   $0x10,(%esp)
82c4879:  call   82c3680 <amDongleSendEx>
82c487e:  test   %eax,%eax
82c4880:  js     82c48ee             ; foutpad
82c4888:  call   82c468d <amDongleChipInfo>
82c488d:  test   %eax,%eax
82c488f:  js     82c48ee
82c48d8:  cmp    $0x101,%ax          ; chipinfocheck: 0x0101
```

**Dit is het protocol:** poortbasis uit `[0xacadf68]` en `[0xacadf6a]`, bit-banged via registers op
`+0x2c`, `+0x28` en `+0x00`, met `out %al,$0x80` als vertraging — de klassieke I2C-over-PCI-poorten.
Eerst wordt de **`AccessKey`** (16 bytes, `0x083a57f6`) gestuurd, dan `amDongleChipInfo`, dan de
chipinfo vergeleken met **`0x0101`**, en bij succes gaat de vlag in `0x0acad9a4` op 1
(`amDongleIsAvailable`). De string `amDongleInit: chip exists.` (27 bytes) wordt dan via `fwrite`
weggeschreven; `amDongleInit: enter develop mode.` en `amDongleInit: Rsv06` zijn de twee andere
logpaden.

> **Correctie op een voor de hand liggende lezing.** De strings `/dev/i2c/0`, `/dev/lbb`,
> `/dev/console`, `/dev/hdb3`, `/dev/input/js%d`, `/dev/ttyS0`, `/dev/ttyS1`, `/dev/usb/tts/%d`
> zitten wél in de bestanden (MEASURED: `strings -a -n 4 | grep '^/dev/'`), maar **`/dev/i2c/0` hoort
> bij `amEepromSmbusAccess`** — het SMBus-pad naar de backup-EEPROM, niet naar de dongle. Dat blijkt
> uit HOTD EX, waar letterlijk `amEepromSmbusAccess` en `amEepromSmbusAccess: access error. (%s:
> access=%c, cmd=%02X, type=%08X)` naast de donglesymbolen staan. Wie `/dev/i2c/0` als "de dongle"
> leest, bouwt het verkeerde antwoord. **INFERRED**, maar met de disassembly hierboven als grond.

### 3.5 De poort zelf: `CJvsMgr::CheckDongleDecrypt` — wat het vergelijkt

**MEASURED** — de (in Rambo nog aanwezige, onbereikbare) originele body op `0x080e3eaa`:

```
80e3ee3:  movl $0x10000,(%esp)        ; KfRandomI(0x10000) — 8 willekeurige 16-bits woorden
80e3eea:  call 8217144 <KfRandomI>
80e3eef:  mov  %ax,(%esi,%ebx,2)
80e3ef6:  cmp  $0x7,%ebx              ; lus 0..7
80e3efb:  movl $0x400,0x10(%esp)      ; lengte = 1024
80e3f03:  mov  %edi,0xc(%esp)         ; uitvoerbuffer
80e3f07:  movl $0x8415280,0x8(%esp)   ; = KeyCheckEncData (1024 B)
80e3f0f:  mov  %esi,0x4(%esp)
80e3f13:  mov  %esi,(%esp)
80e3f16:  call 82c40fb <amDongleDecryptEx>
80e3f1b:  test %eax,%eax
80e3f1d:  jne  80e3f73                ; fout -> vlag 0
80e3f27:  movl $0x8415680,0x4(%esp)   ; = KeyCheckOrgData (1024 B)
80e3f2f:  mov  %edi,(%esp)
80e3f32:  call 804e82c <strncmp@plt>
80e3f3c:  sete %al                    ; gelijk -> 1
80e3f42:  mov  %eax,0x8(%edx)         ; resultaat in het object
```

`readelf -sW` bevestigt de twee blokken: **`KeyCheckEncData` @`0x08415280`, 1024 bytes** en
**`KeyCheckOrgData` @`0x08415680`, 1024 bytes** (precies 0x400 uit elkaar). De check is dus een
**known-answer-test**: het spel stuurt zijn eigen 1 KiB cijfertekst met een willekeurige sleutel naar de
dongle, en vergelijkt het antwoord met de 1 KiB die het óók al in huis heeft.

**Wat dat betekent (INFERRED, sterk):** het geheim zit in de *transformatie* van de dongle, niet in de
verwachte bytes — die staan immers in het bestand. Een eigen antwoord hoeft dus niet de dongle te zijn;
het moet één functie zijn (`amDongleDecryptEx`) die het juiste resultaat teruggeeft. Of dat mag, hangt
aan één vraag die hieronder bij §6 open blijft: of de aanroepende code nog iets anders met dezelfde
sleutel doet.

### 3.6 De uitgebreide amDongle-woordenschat (identiek bij alle vijf)

**MEASURED** — `strings | grep -i dongle | sort -u`:
`amDongleInit` · `amDongleExit` · `amDongleUpdate` · `amDongleIsAvailable` · `amDongleIsDevelop` ·
`amDongleSetWorkaround` (`: Invalid workaround keyword, %s.`) · `amDongleSend` / `SendEx` ·
`amDongleReceive` / `ReceiveEx` · `amDongleSetIv` · `amDongleSetSeedRecv` · `amDongleSetSeedSend` ·
`amDongleEncrypt` / `EncryptEx` · `amDongleDecrypt` / `DecryptEx` · `amDongleReadEeprom` / `WriteEeprom`
(+`Ex`) · `amDongleReadSram` / `WriteSram` (+`Ex`) · `amDongleChipInfo` · `amDongleUserInfo`
(+`UserInfoEx`) · `amDongleBit_GetData` · `amDongleBit_SetData_and_Strobe` · `amDongleBit_SetBusy` ·
`amDongleBit_ResetBusy` · `amDongleBit_ResetStrobe` · `amDongleBit_Wait0` · `amDongleBit_Wait1` ·
`amOsinfoCheckDongleRental`.

Bouwstempels: `amDongle Ver.0.12 Build:Feb 21 2008 18:00:12` (Rambo, HOTD EX) ·
`Ver.0.05 Build:Feb 27 2007 19:51:30` (HOTD4, HOTD4 Special) ·
`Ver.0.12 Build:Feb 14 2007 17:39:06` (Too Spicy).

**Let op wat er níet staat:** `keychip` = **0** treffers in alle vijf, `keychip.appboot.region` = 0,
`hasp`/`aladdin`/`nethasp` = 0. Dit is dus **niet** de Ring-familie-route uit
[`REGIO-PER-GAME.md`](REGIO-PER-GAME.md) (LGI/LGI3D/LGS/OG lezen `keychip.appboot.region`) en ook geen
HASP/Aladdin-titel. De regio komt hier via `CConfigMgr::GetRegion` / `ConfigMgr_GetRegion` plus de
tabellen `UserBackupData_DefaultTbl_JPN/USA`, `UserEepData_DefaultTbl_JPN/USA`,
`UserRankData_DefaultTbl_JPN/USA` — regio's worden dus uit de backup-EEPROM/defaulttabellen gelezen.
In HOTD4 Special, HOTD EX en Too Spicy staat daarnaast een tabelkop met de tekst `REGION`.

---

## 4. Rambo en HOTD EX hebben hun controle **al geforceerd**; de andere drie niet

Dit is de scherpste meting van deze ronde en hij verandert de kosten per titel.

**MEASURED** — `objdump -d` op de kop van `_ZN7CJvsMgr18CheckDongleDecryptEv`:

| ELF | adres | grootte volgens `.symtab` | eerste bytes |
|---|---|---|---|
| `Rambo/elf/ramboM.elf` | `0x080e3e94` | 235 | `push ebp; mov esp,ebp; mov 0x8(%ebp),%eax; **movl $0x1,0x8(%eax)**; pop ebp; ret` |
| `Rambo/elf/ramboM_VGA.elf` | `0x080e3e94` | 235 | idem |
| `Rambo/elf/ramboD.elf` | `0x081658fc` | **15** | idem, direct gevolgd door `nop` en de volgende functie |
| `Rambo/elf/ramboR.elf` | `0x080f47f6` | **15** | idem |
| `HOTD EX/elf/hodexRI.elf` | `0x08189614` (de **wrapper**) | 202 | `mov 0xa9dfebc,%eax; **movl $0x1,0x8(%eax)**; ret` |

Bij `ramboM.elf` en `ramboM_VGA.elf` staat de **originele body er nog achter**, onbereikbaar: op
`0x080e3ea3` begint objdump midden in een instructie (`inc %esp`) en even later volgt weer echte code
(`movl $0x20,(%esp); call IzHeapAlloc`). Dat is het beeld van een **bytes-patch die de functiekop
overschrijft**; bij `ramboD.elf` en `ramboR.elf` is de symbolgrootte precies 15, dus daar is de check
bij het bouwen al weggehaald of de symbooltabel is opnieuw opgebouwd.

**MEASURED, symboolonafhankelijk** — ik heb op de byte-sequentie gezocht in plaats van op symbolen
(dus ook bruikbaar in de gestripte bestanden), `c7 40 08 01 00 00 00 5d c3` = "zet vlag op 1 en keer
terug":

| ELF | force-true | force-false (`...00...`) |
|---|---|---|
| `ramboM.elf`, `ramboM_VGA.elf` | 4 | 4 |
| `ramboD.elf` | 4 | 12 |
| `ramboR.elf` | 4 | 4 |
| `hod4M.elf` (HOTD4) | **0** | 2 |
| `hod4Mn.elf` | **0** | 2 |
| `hod4M.elf` (HOTD4 Special) | **0** | 2 |
| `hodexRI.elf` | 0 | 10 |
| `apacheM.elf`, `apacheM_VGA.elf` | **0** | 8 |

**INFERRED:** Rambo (4/4 varianten) en HOTD EX dragen een geforceerde dongle-controle; **HOTD4,
HOTD4 Special en Too Spicy lijken het originele controlepad nog intact te hebben**. Voor HOTD EX is
dat extra opvallend omdat de patch daar in de *wrapper* zit, niet in de methode.

**Wat er nog moet worden nagekeken voordat dit hard is:** de force-true-sequentie is streng
(eindigt op `pop ebp; ret`). Een patch die de vlag zet en daarna nog iets doet, wordt door mijn
signatuur gemist. De **beslissende, statische** vervolgstap is daarom: zoek in de gestripte
bestanden de PLT-stub van `kswap_collect` en kijk welke functie hem aanroept, en disassembleer in
`hod4M.elf` de aanroeper van `amDongleDecryptEx` (via de `DT_JMPREL`-relocatie van die naam, zie
`ELF-LADER-SPEC.md` §"Waar de gewiste namen precies zitten" voor de methode). Dat kan zonder iets te
starten.

---

## 5. Wat de Windows-lader doet, en waarom dat zo weinig is

**MEASURED** — de spelbestanden dragen zelf een `libkswapapi.so`; TeknoParrot levert een **andere**:

| bestand | grootte | gebouwd met | uitvoeren |
|---|---|---|---|
| `Rambo/elf/libkswapapi.so` (de cab-versie) | 5.816 | `GCC 3.4.3 (MontaVista …2005-10-21)`, met debug-info | `kswap_collect`, `__kswap_collect`, `collect_pf` |
| `ElfLdr2/libs/libkswapapi.so` (TeknoParrot) | 14.816 | `GCC 13.3.1 20240913 (Red Hat)`, bron `kswap.c` | **alleen** `kswap_collect` |

**MEASURED** — ik heb die TeknoParrot-versie uit
`cores/TeknoParrotElfLdr2Core_1.0.0.1445.zip` gehaald en gedemonteerd:

```
00001150 <kswap_collect>:
    1150:  31 c0    xor %eax,%eax
    1152:  c3       ret
```

**Dat is `return 0;` — twee instructies, het hele antwoord van de Windows-kant op deze familie.** Geen
ELF-patch, geen hook in het spelbeeld, geen keychip-emulatie: één vervangend `.so`-bestand op het
zoekpad. In `ElfLdr2/libs/` staat voor deze familie verder `libarcaderegistry.so.1` (uit `arcadekv.c`),
maar **geen van de vijf titels noemt `arcade_reg` of `arcaderegistry`** (MEASURED: 0 treffers per ELF;
ijking op de TP-bibliotheek zelf geeft 134 treffers) — die hebben ze dus niet nodig.

**MEASURED, de Windows-kant is verder leeg:** ik heb `SegaApi.dll` (9,7 MB), `Opensegaapi.dll`
(150.016 B), `BudgieLoader.exe` (11 MB), `TeknoParrotUi.dll`, `TeknoParrotUi.Common.dll` en
`ParrotPatcher.exe` op dezelfde merktekens doorzocht. **Geen enkele treffer op `amDongle`,
`DongleRegion`, `PcbRegion` of `keychip`** die geen ruis is. De treffers die er waren, zijn alle
weerlegd: `VkSwapchainCreate*` bevat "kswap", `IXI2Client`/`uni2C7C` bevat "i2c",
`DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDTVDONGLE` bevat "dongle", `@lBBl@` bevat "lbb". Wie hier
"de lader emuleert de dongle" leest, leest ruis. (De ijking: dezelfde scanner vindt in die bestanden
wél `SEGAAPI_` (31×) en de PE-DOS-stub, dus een leeg resultaat is betekenisvol.)

De twee routes die TeknoParrot per titel aanhoudt, zijn **profielvarianten, geen twee laders**:

| titel | profiel | `EmulatorType` | `ExecutableName` |
|---|---|---|---|
| Rambo | `Rambo.xml` / `RamboElf2.xml` | `Lindbergh` / `ElfLdr2` | `ramboD.elf` / `ramboM.elf` |
| HOTD4 | `HOTD4.xml` / `HOTD4ELF2.xml` | `Lindbergh` / `ElfLdr2` | `hod4M.elf` (test `hod4testM.elf`) |
| HOTD4 Special | `HOTD4SP.xml` / `HOTD4SPELF2.xml` | `Lindbergh` / `ElfLdr2` | `hod4M.elf` |
| HOTD EX | `HOTDEX.xml` | `ElfLdr2` | `hodexRI.elf` (test `hodextestR.elf`) |
| Too Spicy | `2Spicy.xml` / `2SpicyElf2.xml` | `Lindbergh` / `ElfLdr2` | `apacheM_HD.elf;apacheM.elf` |

Allebei dragen `DongleRegion` en `PcbRegion`, standaard **JAPAN** (keuzes JAPAN | USA | EXPORT). Dat is
de regiokeuze van het profiel, niet een dongle-antwoord: de dongle-vraag wordt door de `.so` hierboven
afgedaan en de regiowaarden komen uit de `..._DefaultTbl_JPN/USA`-tabellen in de ELF.

De opstartscripts zeggen niets over bescherming: `Rambo/elf/game.sh` is
`export LD_LIBRARY_PATH=…; pushd /home/disk0/elf; /home/disk0/elf/ramboM.elf $1`,
`HOTD4/elf/game` is `xinit $app_path/hod4M.elf $1 -- X …` met `hod4testM.elf` bij `-t`.

---

## 6. De varianten zijn deels duplicaten (MEASURED, md5)

| titel | zelfde md5 | verschillend |
|---|---|---|
| Rambo | `ramboM.elf` = `ramboM_HD.elf` (`954c6dde…`) | `ramboM_VGA.elf` (`3e3be01c…`), `ramboD.elf`, `ramboR.elf` |
| HOTD4 | `hod4M_offscreen.elf` = `hod4Ms.elf` (`4f714e2c…`) | `hod4M.elf`, `hod4Mn.elf`, `hod4Mn_HD/VGA`, `hod4Ms_HD/VGA` |
| HOTD4 Special | — | 1 spel-ELF, 1 testmenu |
| HOTD EX | — | `hodexRI.elf` ≠ `hodexRI_vga.elf`; `hodextestR.elf` ≠ `..._vga.elf` |
| Too Spicy | `apacheM.elf` = `apacheM_HD.elf` (`e796e667…`); `apachetestM.elf` = `apachetestM_HD.elf` | `apacheM_VGA.elf`, `apachetestM_VGA.elf` |

**Praktisch:** wie `ramboM.elf` aanpakt, heeft `ramboM_HD.elf` gratis; `_VGA` is een **andere build**
(andere md5, en bij Rambo ook een andere entry-inhoud), dus die vraagt eigen werk.

---

## 7. Bewijsvoering — hoe de scanners zijn geijkt

Verplicht volgens de werkwijze: een leeg resultaat is pas iets waard na een ijking op iets waarvan je
weet dat het er is.

| scanner | ijkpunt | uitslag |
|---|---|---|
| stringzoeker (`strings -a -n 4 \| grep -c -i`) | `hasp` in `Jurassic Park/game` (bekend HASP-titel) | **PASS** — 136 |
| stringzoeker, negatieve controle | `zzqqxx_not_present` in hetzelfde bestand | **PASS** — 0 |
| `strings`-dekking | `GLIBC` in `libsegaapi-1.01.0001` | **PASS** — 38 |
| `nm -D`-uitvoerlezer | `libc.so.6` moet uitvoeren hebben | **PASS** — maar 0 op deze machine: `nm` leest hier `/lib/i386-linux-gnu/libc.so.6` niet (64-bits host). Daarom is de ijking verlegd naar `libsegaapi-1.01.0001` → **309 uitvoeren**, plausibel voor een audiobibliotheek |
| volledige-symbooltabel-lezer | `ramboM.elf` moet `.symtab` hebben, een gestripte ELF niet | **PASS** — 20.024 symbolen tegen 132 in `hod4testM.elf` |
| PE-merktekenzoeker | moet `SEGAAPI_` (31×) en de PE-DOS-stub in `SegaApi.dll` vinden | **PASS**; de "treffers" op kswap/i2c/dongle/lbb zijn daarna alle als substrings van andere namen weerlegd |
| byte-signatuurzoeker | de force-true-sequentie moet in `ramboM.elf` gevonden worden nadat ik hem daar met `objdump` had gezien | **PASS** — 4× |

**Gereedschap (op de deck, herhaalbaar, alles read-only):** `/home/deck/rgwork/s1_inventory.sh` t/m
`s13_patchcheck.sh`, met uitvoer in `/home/deck/rgwork/s1.out` … `s13.out`. Er is **niets op de NAS
geschreven** en er is geen spel, lader of sandbox gestart.

---

## 8. Wat open blijft

1. **Wordt `kswap_collect` door de poort aangeroepen, en zo ja waar?** In de originele body van
   `CheckDongleDecrypt` staat geen aanroep naar `kswap_collect`; de functie wordt dus elders gebruikt.
   Wie dat weet, weet of TP's `return 0`-stub de dongle-check ontwijkt of iets anders stillegt.
   **Statisch oplosbaar** via de PLT-stub en zijn aanroepers.
2. **Is de geforceerde vlag in Rambo/HOTD EX een patch van een crack, of een bouwvariant van Sega?**
   Het patroon (kop overschreven, originele body blijft staan, symbolgrootte nog 235) wijst op een
   patch; `ramboD.elf`/`ramboR.elf` met grootte 15 wijzen op "schoon gebouwd". Dit is een gat in het
   bewijs, geen conclusie. **Statisch oplosbaar** door de originele bytes van de kop in de
   `game_original`-achtige varianten te vergelijken — die zijn er voor Rambo niet, dus het blijft bij
   vier van de vijf.
3. **Heeft het spel nog een tweede poort?** `amOsinfoCheckDongleRental` (huurfacturatie) en de
   `amDongleRead/WriteEeprom`-route zijn niet uitgelezen. De EEPROM wordt wél beschreven, dus een
   statisch donglebeeld kan te weinig zijn — precies het punt dat
   `patches/LostLandAdventure/BESCHERMING-STATUS.md` voor de HASP-laag al maakt.
4. **De `23`-bytes `libsegaapi.so` in `HOTD4 Special/drv/ssound/lib/`** is geen ELF maar ASCII-tekst
   (MEASURED, `file -b`). Iemand heeft daar iets neergezet. Dat is een spoor voor een eerdere ronde,
   geen spel-eigenschap.
5. **Er is niets gestart.** Of een titel met een eigen `libkswapapi.so` en zonder dongle doorstart, is
   daarmee **niet** gemeten — dat mag pas op een vrije bench.

---

## 9. Voorgestelde proef (NIET UITGEVOERD — de bench is bezet)

**Eerst statisch en zonder bench, in deze volgorde:**

1. **`kswap_collect`-aanroepers vinden.** Pak de `DT_JMPREL`-relocatie van `kswap_collect` in
   `apacheM.elf` (ongepatcht, gestript, kleinste van de vijf) en zoek in `.text` naar de `call` naar
   die PLT-stub. Herhaal voor `hod4M.elf`. **IJking:** de methode moet in `ramboM.elf` de stub vinden
   die de UND-import `kswap_collect` aanspreekt — daar is de naam bekend.
2. **De poort in een ongepatcht bestand lezen.** Disassembleer in `hod4Mn.elf` of `apacheM.elf` de
   aanroeper van `amDongleDecryptEx` (via de `JMPREL`-index van die naam) en leg de twee
   randvoorwaarden vast: wat er met de willekeurige sleutel gebeurt ná de vergelijking, en of het
   resultaat ergens anders nog wordt gebruikt.
3. **Het antwoord ontwerpen.** Een eigen `libkswapapi.so` met `kswap_collect`, `__kswap_collect` en
   `collect_pf` (dezelfde drie namen als de cab-versie van Rambo), plus — als de check inderdaad
   onontkoombaar is — een `amDongleDecryptEx`-antwoord dat `KeyCheckOrgData` teruggeeft.

**Dan één benchrun** (pas als de bench vrij is): `ramboM.elf` onder onze eigen lader met een
**loggende** `libkswapapi.so` (logt argumenten en terugkeerwaarde van `kswap_collect`) en een
`LD_PRELOAD`-logger op `open()`. Twee vragen in één run: wordt `kswap_collect` aangeroepen, en sterft
het spel als de dongle ontbreekt?

**Positieve controle:** `libc.so.6`-`open`-logging moet de `/dev/input/js%d`- en `/dev/console`-openingen
van de JVS-laag laten zien; als de logger die niet ziet, logt hij niet en is de uitkomst waardeloos.
**Wat de hypothese zou weerleggen:** als het spel zonder dongle en met `kswap_collect(){return 0;}`
gewoon doorstart, dan is de dongle-laag voor deze vijf **niet afgedwongen** en is er geen eigen
dongle-antwoord nodig — dan blijft alleen de bibliothekenlijst over.
