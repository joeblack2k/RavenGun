# CAVE-SYNTHESE — what the findings archive holds for Terminator Salvation's licence check

**Question answered:** does the findings archive on `Z:\Teknoparrot\findings` already contain what
Terminator Salvation needs to get past its licence/hardware check — and if so, what exactly, and is it
enough?

**Sources.** The archive is mounted **read-only** at `Z:\Teknoparrot\findings`; nothing there was
created, changed or deleted. Today's measurements come from the deck dossiers
`~/projects/ravengun-patches/patches/Terminator/` (`DONGLE-ROUTE.md`, `LICENTIE-NETHASP.md`,
`BESCHERMINGSLAAG-PTRACE.md`, `KOPPELING-EN-IO.md`, `KETEN-EN-METHODE.md`, and the drop-in under
`dropin/`). **Nothing was started** for this document: no game, loader, emulator, wine or container, and
no process was killed.

**Labels.** **MEASURED** = a command with its raw output, or a quoted file path with its line content.
**INFERRED** = a conclusion drawn from measurements. Where the archive and today disagree, the side with
evidence is named.

**A note on the moving target.** `DONGLE-ROUTE.md` grew while this synthesis was being written (638 lines
/ 41,786 B at 18:10 → 1,181 lines / 76,858 B at 18:42, **MEASURED** `wc -l`, `ls -la`). Both revisions
were read. Where the newer revision corrects the older one, the correction is used and flagged.

---

## 0. Bottom line

**No. The archive does not contain Terminator's licence/dongle data.** There is no filled-in dongle
description for Terminator, no captured request/response table, no memory image and no key. The words
`0xFFF2`, `65522`, `0xFFF0`, `65520` and `hhl_mem` occur **nowhere** in the archive (**MEASURED**,
`grep -r` over the whole tree → 0 hits). The only file IDs the archive ever names for a Raw Thrills HASP
title — `65524 = 0xFFF4` and `65525 = 0xFFF5` — belong to **Halo Fireteam**, and today's measurement
shows Terminator uses neither (**MEASURED**, `DONGLE-ROUTE.md` §3.1).

What the archive *does* contain is three things of real value, and one thing that is actively misleading:

1. a complete, verified **format** for a dongle (both the drop-in directory and the RGD1 container),
   with **one** fully filled-in example: **Halo Fireteam**;
2. the **complete HASP API contract** (ten functions, handle `0xE6F`, `hasp_get_size` → 0, the XML
   descriptor, and the fileid→filename rule), derived from `halo.so` and from the opened core;
3. an explicit, honest **"not investigated"** marker on exactly the thing we need — Terminator's
   `hasp/` folder (`dongles/README.md` §5, `LOSKOPPELEN.md` §"Niet bewezen");
4. the misleading part: three mutually inconsistent statements about whether Terminator has a dongle at
   all, one of which ("Terminator has no protection field, so it only needs input plus patches") is
   quoted back as a hard answer.

---

## 1. What the RGD format is — and what a dongle description contains

There are **two layers**, and they are described in two different documents. The `.rgd` file is only the
outer one.

### 1.1 Layer A — the drop-in directory (`dongle.json` + `memory/`)

**MEASURED**, `dongles/README.md:15-25`:

```
dongles/
  README.md              <- dit bestand
  schema.json            <- het formaat, machineleesbaar
  _template/             <- leeg voorbeeld om te kopiëren
    dongle.json
  <spelnaam>/
    dongle.json          <- de beschrijving van de dongle
    memory/              <- de geheugenbestanden (alleen bij het HASP-type)
      <bestandsnaam>     <- de naam is de 32-bits binaire fileid
```

**What is mandatory — the machine-readable answer.** **MEASURED**, `dongles/schema.json:6`,
`:12-16`, `:72-85`:

```json
"required": ["game", "type"],
...
"type": { "type": "string", "enum": ["hasp-hl", "region-pcb", "sega-scan"], ... },
...
"allOf": [
  { "if": { "properties": { "type": { "const": "hasp-hl" } } },
    "then": { "required": ["haspid", "memory"] } },
  { "if": { "properties": { "type": { "const": "region-pcb" } } },
    "then": { "required": ["region", "pcb"] } },
  { "if": { "properties": { "type": { "const": "sega-scan" } } },
    "then": { "required": ["stages"] } }
]
```

So: `game` + `type` always; for `type: "hasp-hl"` additionally **`haspid` and `memory`**; each `memory`
entry requires `name`, `fileid`, `size`, `file` (`schema.json:38`). `features`, `keycaps`, `port`,
`notes` are optional. The prose table agrees — **MEASURED**, `dongles/README.md:142-153`:

```
| Veld | Verplicht | Betekenis |
| `game` | ja | de naam van het spel, alleen voor de leesbaarheid |
| `type` | ja | `hasp-hl`, `region-pcb` of `sega-scan` |
| `haspid` | bij `hasp-hl` | het dongle-nummer dat in de XML komt |
| `features` | bij `hasp-hl` | de kenmerken die de game mag opvragen |
| `keycaps` | nee | de capabilities, komen in de uitgebreide XML |
| `memory` | bij `hasp-hl` | de geheugenbestanden met fileid, grootte en pad |
| `port` | nee | het poorttype en adres (`196608` = `0x30000`) |
```

### 1.2 The rule that makes the whole format work: the filename *is* the file ID

**MEASURED**, `dongles/README.md:57-71`:

> **De bestandsnaam is de kern van het hele formaat.** De fileid wordt omgezet naar **32
> tekens, één per bit, hoogste bit eerst**. Dus fileid `65524` = `0x0000FFF4` wordt:
> ```
> 00000000000000001111111111110100
> ```
> Dat is letterlijk de bestandsnaam. De loader hoeft dus geen tabel te hebben: hij rekent de
> naam uit de fileid uit en opent het bestand.

The same rule is derived independently, from disassembly of `halo.so`'s `hasp_read`, in
`17_hasp_dongle.md:124-146` — **MEASURED**:

```asm
 ; loop i = 0 .. 0x1f (32 tekens)
 mov eax, 0x1f
 sub eax, [rbp-0x44] ; 31 - i
 mov edx, [rbp-0x58] ; <-- de fileid
 shr edx, cl ; fileid >> (31-i)
 and eax, 1
 add eax, 0x30 ; -> '0' of '1'
 ...
 ; daarna: fopen(naam, "rb")
```

The archive ships the real thing: **MEASURED**, `dongles/halo-fireteam/memory/` contains three files of
112 bytes each — `00000000000000001111111111110100` (= 0xFFF4), `00000000000000001111111111110101`
(= 0xFFF5) and `fff4`; the first two differ, the first and `fff4` are byte-identical (hex dump: both
begin `2C 03 AB 63 2A 91 92 3F ED 45 8B A1 4E 92 2E B7 …`). The archive's own advice on which one to
use — **MEASURED**, `dongles/README.md:67-71`: *"Neem de lange naam — dat is degene die de API
opzoekt."*

### 1.3 The one fully filled-in example — Halo Fireteam

**MEASURED**, `dongles/halo-fireteam/dongle.json`:

```json
{
  "game": "Halo Fireteam",
  "type": "hasp-hl",
  "haspid": 748675309,
  "features": [ 0, 60 ],
  "keycaps": [ "hasphl", "newintf", "hasp4", "aes", "sentinelhl" ],
  "memory": [
    { "name": "read/write", "fileid": 65524, "size": 112,
      "file": "memory/00000000000000001111111111110100" },
    { "name": "read-only", "fileid": 65525, "size": 112,
      "file": "memory/00000000000000001111111111110101" }
  ],
  "port": { "type": "USB", "address": 196608 },
  "notes": "De echte dongle uit de spelmap (g7/halo). Beide fileids delen dezelfde 112-byte image in deze distributie."
}
```

**Is this a game we have already made run?** Yes. **MEASURED**, `INVENTARIS-dongle-per-game.md:15` and
`:90`: `| **Halo** | ElfLdr2 | WartranTroopers | - | playable |` and `**Halo** — Werkt volledig:
lightgun, knoppen, trillen.` Halo is the same family as Terminator (Raw Thrills Linux PC, ElfLdr2) and
is the archive's worked example throughout (`16_halo_fireteam.md`).

**One caveat, and it is ours not the archive's.** Today's dossier records that Halo's *own drop-in in our
repo* is inert, because Halo's `hasp_*` calls are statically linked too (**MEASURED**,
`DONGLE-ROUTE.md` §6: the Halo drop-in's `notes` say *"de hasp-aanroepen van deze game zijn statisch
gelinkt, dus de emulator wordt geladen maar nog niet aangesproken"*). Halo runs on **TeknoParrot's**
`halo.so` route (`16_halo_fireteam.md:13-21`), not on our drop-in. So "working example" here means: a
complete, real, byte-verified dongle description — not "our name-based drop-in made it work".

### 1.4 Layer B — the RGD1 container

`RGD1-formaat.md` describes a single-file container that wraps layer A. **MEASURED**,
`RGD1-formaat.md:39-48` (the fixed 64-byte header, all little-endian):

| Positie | Grootte | Veld | Waarde |
|---|---|---|---|
| `0x00` | 4 | **magic** | **`52 47 44 31`** = de ASCII-tekens **`RGD1`** |
| `0x04` | 2 | **formaatversie** | **`1`** |
| `0x06` | 2 | **vlaggen** | bit 0 = **1** als de inhoud versleuteld is, **0** als hij plat is |
| `0x08` | 4 | **lengte van de platte inhoud** | in bytes, vóór versleuteling |
| `0x0C` | 4 | **lengte van de opgeslagen inhoud** | in bytes, zoals het in het bestand staat |
| `0x10` | 32 | **SHA-256 van de platte inhoud** | de controlesom |
| `0x30` | **16** | **zout** | voor de sleutelafleiding; willekeurig per bestand |

Payload: UTF-8 JSON `{"dongle": {...}, "memory": {"<filename>": "<hex>"}}` (`RGD1-formaat.md:60-77`).
Key derivation: `PBKDF2-HMAC-SHA256(password, salt, 200000, 32)`; keystream
`SHA-256(key || n as 64-bit LE)`; encryption is XOR (`RGD1-formaat.md:89`, `:111-123`).

**Purpose — and the spec is unusually honest about it. MEASURED**, `RGD1-formaat.md:18-19`:
*"De sleutel **reist mee met de tool**. Wie de tool heeft, heeft de sleutel. **De versleuteling geeft
dus geen geheimhouding.**"* Its stated value is integrity (the SHA-256), separation, one fixed
structure, and one file instead of a directory (`RGD1-formaat.md:23-28`).

### 1.5 I verified the shipped container — and found a real defect in the archive

**MEASURED**, my own run on the read-only file (Python from the bundled runtime; output verbatim):

```
$ python rgd1.py info Z:\...\dongles\halo-fireteam.rgd
{ "versie": 1, "vlaggen": 1, "payload_lengte": 1322, "body_lengte": 1322,
  "digest": "51be53301201052bc0934c33d5653bf03f6b276efea076f19eead09f118d0964",
  "zout": "5a0ff5a2c19929a6182a95a374a139fe" }
```

An independent parse of the same bytes agrees exactly: length 1386, magic `RGD1`, version 1, flags 1,
plain 1322, stored 1322, salt `5A0FF5A2C19929A6182A95A374A139FE` (**MEASURED**, `[System.IO.File]` +
`BitConverter` on the same file). The salt and the 1,386-byte size are **the ones printed in the spec's
own test-vector table** (`RGD1-formaat.md:262`), so the shipped file is the file that table describes.

**MEASURED**, unpacking it:

```
$ python rgd1.py unpack ...\halo-fireteam.rgd <tmp>
ValueError: controlesom klopt niet -- beschadigd of verkeerd wachtwoord

# with the spec's password instead of the tool's default:
FAIL pw=b'RavenGun dongle container v1' -> controlesom klopt niet ...
OK   pw=b'1234-5678-90'  game='Halo Fireteam'  files=3  img_len=112
     img_sha256=3932b3aa7b0d062607e27919859247b9b5647afd6c12aab0067b4aa21d57a80b
     fileids: [('read/write', 65524, 112), ('read-only', 65525, 112)]
```

**So the container is genuinely openable and round-trips** — the 112-byte image hash is exactly the one
in the spec's table (`RGD1-formaat.md:265`), and the key/keystream test vector reproduces bit for bit
(`key = 2da613ed…b51d`, `blok(0) = 0ed9375e…f9b8`, `blok(1) = 7797183d…2efb`, all **MEASURED**).

**But: the archive contradicts itself on the password.** `RGD1-formaat.md:236` publishes the password as
`1234-5678-90`; `dongles/src/rgd1.py:21` hardcodes `STANDAARD_WACHTWOORD = b'RavenGun dongle container
v1'` as the default for both `pack` and `unpack`, and the CLI has no password argument
(`rgd1.py:172-177`). The shipped `.rgd` was made with the **spec's** password, so **the reference tool
cannot open its own shipped example with its default settings.** That is a defect in the archive, not a
disagreement with today — but it matters, because it means "use `rgd1.py`" is not a working instruction
as written.

**Two smaller internal inconsistencies in the same layer, both MEASURED:**

* `dongles/_template/dongle.json` declares `fileid: 65525` for `read-only` but points it at
  `memory/00000000000000001111111111110100`, which encodes `0xFFF4 = 65524`. That breaks the
  filename-is-the-fileid rule the same document states. The real example
  (`dongles/halo-fireteam/dongle.json`) gets it right (`…11110101`). **Use the example, not the
  template.**
* `dongles/README.md:200-202` warns that two haspids are in circulation (`744811821` short form,
  `748675309` extended form) and says "zet in `dongle.json` welke bij jouw spel hoort" — but the archive
  never says which game each belongs to.

---

## 2. What the archive says Terminator needs — quoted, file by file, with the author's own confidence

The archive says **three different things**. All three are quoted; the evidence side is named.

### Position A — "Terminator needs HASP; the `hasp/` folder is the dongle" (an assumption)

**MEASURED**, `WAT-ELK-SPEL-NODIG-HEEFT.md:16-24` (a letter from the TeknoParrot team):

```
**Ik heb in alle spelbestanden gezocht naar HASP-artefacten. Dit is alles wat er is:**

Jurassic Park         utils/pmrtdata/libhasp_linux_*.so      (acht versies)
Terminator Salvation  hasp/
Time Crisis 5         hasp_windows_x64_105293.dll
NERF Arcade           Plugins/x86_64/libhasp_linux_23714.so
Halo Fireteam         00000000000000001111111111110100       (de dongle-image)
```

and `WAT-ELK-SPEL-NODIG-HEEFT.md:35` and `:117`:

```
| **Terminator Salvation** | een `hasp`-map |
...
| **Terminator Salvation** | **ja**, voor de dongle |
```

Same claim, same shape, in `MATRIX-dongle-API.md:159` (`| **HASP** | tien `hasp_*`-functies | Halo,
Jurassic Park, Terminator, TC5, NERF |`), in `17_hasp_dongle.md:232-234` (*"de dongle-inhoud die we al
hebben (de 112-byte blob hierboven) is voldoende om Halo te laten starten. Voor andere games is de
dongle-data te vinden in dezelfde bestanden in de game-map"*), and in `INVENTARIS-dongle-per-game.md:87`
(*"de dump heeft geen donglebestand zoals Halo en Terminator"*).

**How the archive itself labels this: as a directory listing, not as a measurement.** There is no
command, no file count from a tool, no file IDs, no haspid, no captured data. The one concrete,
checkable claim it does make is about the entries — **MEASURED**, `readfirst.md:47`:

```
| **Terminator Salvation** | Raw Thrills Linux PC | `RawThrillsGUN` | game · t4-patch (ELF) · irgun/firmware/prom_9b.mcs · irgun/firmware/prom_9e.mcs (Xilinx FPGA) · irgun/gunrec_update · irgun/fprog · hasp/* (32-byte dongle-entries) | 17_hasp_dongle.md, 18_irgun_transformers.md |
```

That "32-byte dongle-entries" claim is the one part of Position A that today's measurement **confirms**
(see §4).

### Position B — "the `hasp/` folder is not investigated, and it is a different storage pattern" (the honest one)

**MEASURED**, `dongles/README.md:213-216`:

> **Waar je de gegevens vandaan haalt:** de dongle-bestanden staan in de spelmap zelf. Zoek
> naar bestanden met een lange naam van nullen en enen (dat is de binaire fileid), of naar een
> map die `hasp` heet. Bij Halo staan ze in `g7/halo/`; bij Terminator in `hasp/` (10.826
> bestanden, genoemd naar hash).

**MEASURED**, `dongles/README.md:228-231` (§5, "Wat er nog niet is"):

> * **Terminators `hasp/`-map met 10.826 bestanden** is nog niet uitgezocht. De namen zijn
>   hashes (32 en 64 tekens), wat suggereert dat daar per aanroep een bestand wordt opgezocht.
>   Dat is een **ander opslagpatroon** dan het fileid-formaat, en het is de moeite waard om
>   na te gaan of Terminator dezelfde HASP-API gebruikt of iets eigens.

**MEASURED**, `LOSKOPPELEN.md:232` and `:294-295`:

```
| **Terminator Salvation** | ElfLdr2 → eigen starter | RawThrillsGUN | nodig | **hasp** (10.826 bestanden, nog uit te zoeken) | nee |
...
* **Terminators `hasp/`-map met 10.826 hash-bestanden** — een ander opslagpatroon dan het
  fileid-formaat; nog niet uitgezocht of dat dezelfde HASP-API is of iets eigens
```

**This is the archive's strongest contribution and it is a negative one:** it explicitly refuses to
answer, and it names the exact open question ("same HASP API, or something of its own?") that today's
measurement then answers — it *is* the same HASP-HL API, reached by a statically linked client, and the
file IDs are `0xFFF2`/`0xFFF0`, **not** the `hasp/` folder (see §4).

### Position C — "Terminator has no protection at all" (stated as a hard answer, and wrong)

**MEASURED**, `antwoorden_ronde4.md:67-88`:

```
## 18. Terminator, Transformers, Go Go Strike, Raccoon Rampage — ja, met één uitzondering
...
| **Terminator** | `RawThrillsGUN` | ElfLdr2 | — | **geen** |
...
* **Terminator** — ja, alleen invoer plus aanpassingen. Geen enkel beschermingsveld.
...
**Eén voorbehoud:** mijn uitlezing pakt alleen velden met een waarde. Staat er een leeg
`<DongleRegion></DongleRegion>` in een profiel, dan zie ik dat niet.
```

The same conclusion, derived from the profile dump, in `11_hardware_patches_dongles.md:1165-1191`: the
per-game table for `Terminator` lists **seven** fields — `Custom Resolution Height 768`,
`Custom Resolution Width 1360`, `ForceReticle 1`, `Input API RawInput`, two sensitivities, `Use Custom
Resolution 0`, `Use Relative Input 0`, `Windowed 1` — and **not one dongle field**. And
`INVENTARIS-dongle-per-game.md:39`/`:77`:

```
| **Terminator** | ElfLdr2 | RawThrillsGUN | - | playable |
...
* **Terminator** — -
```

**What Position C actually proves, and what it does not.** The profile dump is correct *about the
profile*: there is no `DongleRegion`, `PcbRegion`, `Patreon` or region field for Terminator, so the
**TeknoParrot loader** needs no dongle configuration for it. It does **not** prove the game has no
licence check — and today's measurement shows it has a large one. **Evidence side: today**
(`DONGLE-ROUTE.md` §1.4/§2: a statically linked Aladdin client, `hasp_login` reached from `main`, two
transports, measured file IDs). The archive's own author even flagged the limit of the method in the
caveat quoted above, and the two positions were never reconciled.

### Position D — the failure mode, as the archive recorded it

**MEASURED**, `Terminator.md:23-24` (identical text in `games/Terminator/Terminator.md:23-24`):

```
## Native op Linux (experiment, geparkeerd)
- Native: laadt, maar stopt direct met exit 255 ("DongleInitialize() FAILED"): de game heeft hiervoor iets uit de TeknoParrot-runtime nodig. Via TeknoParrot plus gamescope werkt hij wel.
```

Note what is **not** in the archive: no log, no command, no exit-code source, no timestamp, no build
hash. It is a one-line note, labelled as an experiment that was parked. The same note is echoed into the
live profile and is flagged there as outdated by our own later work (**MEASURED**,
`KOPPELING-EN-IO.md:318`: *"De note in het profiel is verouderd ("stopt direct met exit 255"): dat is
van vóór mijn werk."*).

### The one part of the HASP contract the archive *did* measure — and it is Halo's, not Terminator's

**MEASURED**, `17_hasp_dongle.md:40-54` (from `halo.so`, `hasp_login_scope @ 0x5316`):

```asm
 cmp qword [rbp-0x20], 0 ; is de handle-pointer NULL?
 je 0x533a
 mov rax, [rbp-0x20]
 mov dword [rax], 0xe6f ; <-- schrijf de dongle-handle 0xE6F
0x533a:
 mov eax, 0 ; <-- return HASP_STATUS_OK
```

**MEASURED**, `17_hasp_dongle.md:70-79` — the XML `hasp_get_info` returns from `halo.so` at file offset
`0x10C90`:

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<hasp_info>
 <hasp id="744811821" type="HASP-HL">
 <feature id="0" />
 <feature id="60" />
 </hasp>
</hasp_info>
```

and the extended descriptor at `0x10A18` (`17_hasp_dongle.md:85-122`) with `haspid 748675309`, keycaps
`hasphl/newintf/hasp4/aes/sentinelhl`, and two memory infos `65524`/`65525` of size 112. **All of this is
Halo's `halo.so`.** The archive never claims it is Terminator's, but a reader skimming `17_hasp_dongle.md`
for "the HASP answer" will find Halo's numbers and nothing that says "this is Halo only".

The core side adds three more haspids — **MEASURED**, `13_opengestelde_cores.md:209-215`:

```
### hasp_dongle (40)
- `./hasp`
- `/hasp/`
- `<haspid>133701337</haspid>`
- `<haspid>1663570977</haspid>`
- `<haspid>287339817</haspid>`
```

**None of the five haspids in the archive (`744811821`, `748675309`, `133701337`, `1663570977`,
`287339817`) is attributed to any game.** They are the core's per-game templates, and which one — if any
— is Terminator's is not stated anywhere in the archive. **MEASURED** (read of all five sites).

### The keyword hit list (de-duplicated)

The archive repeats every technical document inside each `games/<Spel>/techniek/` folder, so a raw grep
returns each top-level hit many times. De-duplicated to the top level, the files that mention
`Terminator` / `HASP` / `hasp` / `NetHASP` / `nethasp` / `aksusb` / `Hardlock` / `DongleInitialize`
(**MEASURED**, PowerShell `Select-String` per file, counts):

```
17_hasp_dongle.md 81 · WAT-ELK-SPEL-NODIG-HEEFT.md 33 · binary_settings.md 24 ·
13_opengestelde_cores.md 20 · 14_openstellen.md 19 · 16_halo_fireteam.md 18 ·
LOSKOPPELEN.md 15 · nvram_inventory.md 13 · MATRIX-dongle-API.md 8 · readfirst.md 8 ·
vragen.txt 7 · antwoorden_ronde4.md 6 · antwoorden_ronde2.md 5 · Terminator.md 5 ·
INVENTARIS-dongle-per-game.md 4 · 10_target_dossiers.md 4 ·
antwoorden_zonder_teknoparrot.md 4 · JurassicPark.md 4 · OPDRACHT-voor-de-loader-bouwer.md 3 ·
11_hardware_patches_dongles.md 3 · 05_target_games.md 3 · 00_OVERZICHT.md 3 ·
antwoorden_ronde11.md 2 · 18_irgun_transformers.md 2 · 15_game_share.md 2 ·
06_lightgun_boards.md 2 · antwoorden.md 2 · README.md 2 · then 1 each in
09_how_teknoparrot_runs_lightgun_games.md, antwoorden_ronde5/7/9/23.md, config_inventory.md,
GhostBusters.md, NERF.md, antwoorden_diepere_pass.md
```

Inside `dongles/`: `README.md` 40, `src/dongle.c` 41, `src/dongle.h` 22, `src/selftest.c` 20,
`src/README.md` 13, `schema.json` 5, `halo-fireteam/dongle.json` 4, `_template/dongle.json` 4,
`RGD1-formaat.md` 2. Inside `QA/`: `vragen.txt` 7, `antwoorden_ronde4.md` 6, `antwoorden_ronde2.md` 5,
`antwoorden_zonder_teknoparrot.md` 4, and 1–2 each in `antwoorden.md`,
`antwoorden_diepere_pass.md`, `antwoorden_ronde5/7/9/11/23.md`. Inside `data/`: only the decrypted-core
string harvest (`decrypted_harvest.json`, the same 40 `hasp_dongle` strings as
`13_opengestelde_cores.md`) and Terminator *titles*, no dongle data. `loaders/`: only binary string
matches (a `TeknoBudgie - Terminator Salvation` window-title literal in
`loaders/PipeHelper/HookedWindows.txt:39`, and `hasp` fragments inside `BudgieLoader` dumps,
`NERFTPPlugin.dll`, `TPGfx.dll`, `Shell.exe`) — no data.

**`aksusb`, `Hardlock` and `DongleInitialize`: exactly one hit each in the whole archive**, and all three
are the same line — `Terminator.md:24` (and its `games/Terminator/` copy). **MEASURED**:
`grep -r 'aksusb|Hardlock|DongleInitialize'` over the whole tree returns 2 lines, both that sentence.

---

## 3. Does the archive contain Terminator's actual dongle data? — No. Cleanly.

Every place such data could hide was checked. **MEASURED**, all of the following:

| check | command / evidence | result |
|---|---|---|
| a Terminator dongle directory | `ls dongles/` | **absent** — only `halo-fireteam/`, `lets-go-safari/`, `src/`, `_template/`, `_voorbeelden/` |
| an `.rgd` for Terminator | `Get-ChildItem -Recurse -Filter *.rgd` | **one file in the whole archive**: `dongles/halo-fireteam.rgd` (1,386 B) |
| a `hasp/` directory anywhere in the archive | `Get-ChildItem -Recurse -Directory \| ? Name -match hasp` | **0 hits** |
| the 32/64-hex-named memory files | `Get-ChildItem -Recurse -File` with a `^[0-9a-f]{32}$`/`{64}$` name filter | **2 hits, both Halo**: `dongles/halo-fireteam/memory/00000000000000001111111111110100` and `…11110101`, 112 B each |
| Terminator's measured file IDs | `grep -r '0xFFF2\|0xfff2\|fff2\|65522\|65520\|0xFFF0\|hhl_mem'` | **0 hits** |
| any HASP field in the machine-readable per-game data | `Select-String data/*.json -Pattern hasp` | **0 hits** |
| the per-game dongle inventory | `INVENTARIS-dongle-per-game.md:77` | `* **Terminator** — -` |
| a captured answer table / key for Terminator | no candidate file exists | **absent** |

**What the archive has instead is a pointer.** It repeatedly says the data is "in the game folder"
(`dongles/README.md:213-216`, `17_hasp_dongle.md:232-234`) — i.e. in `Terminator Salvation/hasp/` on the
NAS share, which is a *different* mount (`Z:\Teknoparrot\findings` is the findings archive; the game
itself is on the share). **The archive points at the data; it does not contain it.**

**One thing that looks like Terminator dongle data but is not the archive's:** our own deck drop-in
holds a byte-for-byte copy of the game's `hhl_mem.dmp` (**MEASURED**,
`dropin/ravengun-hasp/Terminator/dongle.json`, `notes`: *"Terminator Salvation/hasp/hhl_mem.dmp is een
HASP-HL-geheugendump van 128 bytes … sha256
759668aeac17ccae66385b365bbe9e19504cde93b4374fd966a8edf1d04a8a8e"*, declared as
`fileid 65524, size 128`). That is **ours**, dated 2026-10-11, and today's dossier refutes its central
assumption twice: the file ID is wrong (`0xFFF2`/`0xFFF0` measured) and the "no statically linked HASP
client to hook" inference is wrong (§4.4 below).

---

## 4. Reconciliation with today's measurements

Today's facts, as recorded on the deck (**MEASURED**, `DONGLE-ROUTE.md` and `LICENTIE-NETHASP.md`):
`game` is an unpacked 32-bit `ET_EXEC` (7,764,284 B, SHA-256 `798dec07…b206a`, entry `0x08057240`, 29
section headers); it has **0** `hasp_` dynamic symbols out of 1,019, and **45** `hasp` strings; the
Aladdin client is **statically linked**; there are two transports — `open("/dev/Hardlock", O_RDWR)` +
`ioctl(fd, 0x02FED112, buf)` at two sites (`0x083c6b18`, `0x083d3d8b`), and an `AF_UNIX` socket whose
`sun_path` is built from three immediates into `/tmp/.aksusb`; the game reads/writes file ID **`0xFFF2`**
and reads **`0xFFF0`**; the `hasp/` folder holds **5,413 entries** — `5410 × 32 B` + `hhl_mem.dmp`
(128 B) + one 256 B + one 512 B file, names 3,734 × 32 hex and 1,676 × 64 hex, entropy 7.9960 bits/byte;
and the licence check sits at **init step 1**.

### 4.1 Where the archive and today AGREE

| # | point | archive | today |
|---|---|---|---|
| A1 | the blocker is the dongle init, and it fails | `Terminator.md:24` — `DongleInitialize() FAILED` | the game's own `T4User/log/syslog_bb3.txt`: `Failed aldeen: 2` / `DongleInitialize() FAILED!` (`LICENTIE-NETHASP.md:27-29`) |
| A2 | the `hasp/` folder holds **32-byte** entries | `readfirst.md:47` — `hasp/* (32-byte dongle-entries)` | 5,410 files of exactly 32 B (`DONGLE-ROUTE.md` §3.3) |
| A3 | the `hasp/` names are **32- and 64-character** hash-like names | `dongles/README.md:229` — *"De namen zijn hashes (32 en 64 tekens)"* | 3,734 × 32 hex + 1,676 × 64 hex; the 64-char names are the 32-char names with 32 more hex chars appended (§3.3) |
| A4 | Halo is the worked example and Terminator is explicitly not | `dongles/README.md` §5; `LOSKOPPELEN.md:294` | Halo's drop-in is inert too; Terminator is "the best of the group in which to build the address-keyed route" (`DONGLE-ROUTE.md` §6) |
| A5 | the HASP API shape (handle `0xE6F`, `hasp_get_size`→0, XML descriptor, fileid→filename) | `17_hasp_dongle.md` §1–§2, from `halo.so` | the game calls `hasp_login` (feature `0xFFFF0000`), `hasp_read`/`hasp_write`/`hasp_encrypt`/`hasp_decrypt`; the file IDs are readable from its own call sites (§3.1) |
| A6 | the check is **early** in startup | `Terminator.md:24` — "stopt direct" | init **step 1** of 51, `main`'s third call, before `FbInit`/`WndInit`/`VidInit`/`DrawInit`/`FsInit`/`PhysXInit` (§4 of the newer revision) |

### 4.2 Where the archive and today DISAGREE — with the evidence side named

**D1 — "Terminator has no protection" vs a statically linked HASP client with two transports.**
Archive: `antwoorden_ronde4.md:81` — *"Terminator — ja, alleen invoer plus aanpassingen. Geen enkel
beschermingsveld."*; `INVENTARIS-dongle-per-game.md:77` — dongle `-`.
Today: `DONGLE-ROUTE.md` §1.4/§2 — a static Aladdin client inside `game`, `hasp_login` reachable from
`main`, `/dev/Hardlock` + `ioctl(0x02FED112)`, `AF_UNIX /tmp/.aksusb`.
**Evidence side: today.** The archive's claim is a correct reading of the *loader profile* and an
incorrect conclusion about the *game*. Note that the archive's author flagged the limit of his own
method (`antwoorden_ronde4.md:88`).

**D2 — the `hasp/` folder as "the dongle data" vs the game asking HASP-HL for `0xFFF2`/`0xFFF0`.**
Archive: `WAT-ELK-SPEL-NODIG-HEEFT.md:117` — *"**ja**, voor de dongle"*; `INVENTARIS:87` treats `hasp/`
as "een donglebestand".
Today: the game's licence route is the HASP-HL API with file IDs `0xFFF2` (read **and** write) and
`0xFFF0` (read); `0xFFF4` — the ID the archive's only example uses — **never appears in `game`**; and
`game` contains **no** filesystem path literal containing `hasp`.
**Evidence side: today** (call-site immediates, `readelf -r`, `strings`). The archive's Position B is
the one that survives: it only claims a different storage pattern and an open question.

**D3 — the file count: 10,826 vs 5,413.**
Archive: `dongles/README.md:215` and `:228` — *"10.826 bestanden"*; `LOSKOPPELEN.md:232` and `:294` —
*"10.826 bestanden"* / *"10.826 hash-bestanden"*.
Today: **5,413 entries** (`DONGLE-ROUTE.md` §3.3, from `05-haspdir.txt` / `10-haspdir2.txt`, a 5,416-line
directory listing).
**Note the arithmetic: 10,826 = 2 × 5,413, exactly.** That is unlikely to be a coincidence; the most
plausible reading is that the archive counted each entry twice (two listings, or a `hasp/` counted in two
views of the same share). **INFERRED.** The archive gives no command and no listing for its number;
today's number is reproducible from a listing. **Evidence side: today.**

**D4 — the exit code and the attribution of the failure.**
Archive: `Terminator.md:24` — *"stopt direct met exit 255 ("DongleInitialize() FAILED")"* — i.e. 255 is
attributed to the dongle init.
Today: the dongle failure writes its own message and the game **continues**; the run ends in **`exit 0`
after 4 s** (`LICENTIE-NETHASP.md:8`, `:30`; `DONGLE-ROUTE.md` newer §4, `RESULT.txt`:
`exitcode : 0`, `levensduur : 4 s`, `aanroep-regels : 0`). Separately, today's **255 was measured to come
from the game's own ptrace/Yama protection layer**, not the dongle: *"kind 1 `PTRACE_TRACEME`; kind 2
`PTRACE_ATTACH(ouder) → EPERM`; kind exit 1; ouder SIGKILL + -1 → 255"*, **MEASURED** by strace
(`BESCHERMINGSLAAG-PTRACE.md:286`), and after that layer was lifted the same title produced `exit 0`
(`:349`, `:371`).
**Evidence side: today** (strace of two runs, the guest's own syslog, and the init-order disassembly).
The archive's line is unsourced and, on the evidence, conflates two different failures.

**D5 — "stopt direct" vs "the failure does not abort the chain".**
Archive: "stopt direct" (stops immediately).
Today: `main` is a flat chain of 51 `call; test; js <own-failure-block>` steps in which *"each failure
block prints its own message, sets a flag and **jumps on to the next step** (so the chain never
aborts)"*; `DongleInitialize` is step 1, and the observed exit is a **draw-time skip** after step 50
(`0x080c2a49` switch → `jne 0x080c319d` past `glutDisplayFunc`/`glutMainLoop`). **MEASURED**
(`DONGLE-ROUTE.md` newer §4, with `26-names.txt`).
**Evidence side: today.** The archive is right that the check is early and wrong about the mechanism.

**D6 — bitness: the archive contradicts itself, and one side contradicts today.**
`Terminator.md:1` calls it a *"32-bit Linux-ELF via ElfLdr2/BudgieLoader"*, but
`antwoorden_zonder_teknoparrot.md:55-57` puts Terminator in the **64-bit** family:

```
| | **64-bits familie** | **32-bits familie** |
| spellen | Halo, Terminator, GhostBusters, Go Go Strike | Jurassic Park, Pacman Chomp-Mania, Galaga Assault |
```

Today: *"`ELF 32-bit LSB executable, Intel i386 … stripped`"*, `ET_EXEC` (**MEASURED**,
`DONGLE-ROUTE.md` §1.1).
**Evidence side: today**, which also confirms the archive's own title line. So `Terminator.md:1` is
right, `antwoorden_zonder_teknoparrot.md:57` is wrong, and the archive never noticed.

**D7 — "playable" is a TeknoParrot result, not licence knowledge.**
`INVENTARIS-dongle-per-game.md:39`/`:113` and `Terminator.md:6` record Terminator as working
image-filling via TeknoParrot. That is consistent with today (a different stack), but it must not be read
as "we know the licence data": the archive's own §5 says the opposite. **MEASURED** (both quoted).

### 4.3 What the reconciliation implies about **when** in startup the check happens

**The dongle init is the first thing after the supervisor, and the exit is a separate, much later
event.** **MEASURED**, `DONGLE-ROUTE.md` newer §4:

* `main` at `0x080c22d0`; its **step 0** is the supervisor (`0x080c22ec`, message `Faileed Dbgg init`);
* **step 1 is the dongle**: `0x080c232c  e8 1f 9c fe ff  call 0x080abf50` (`DongleInitialize`), failure
  block `0x080c2cdb` → `DongleInitialize() FAILED!`;
* framebuffer and window are steps **7** and **8**, video/draw are **12**/**13**, filesystem **14**,
  PhysX **22**, `gameInit` **26** — all **after** the dongle;
* the last things before the observed exit are `glutDisplayFunc` (`0x080c2a5e`) and `glutMainLoop`
  (`0x080c2a63`), **outside** the `call;js` chain, after step 50;
* the failure block **does not abort**: it prints, sets a flag, and falls through to the next step.

So the answer to "when": **the check runs at dongle-initialize time — `main`'s third call, before any
window, framebuffer, video, filesystem or PhysX initialisation — and its consequence (the game skipping
`glutMainLoop` and exiting) is deferred to draw time, roughly 4 seconds later.** The 4-second delay is
the licence client's own retry pacing, not a late gate: **MEASURED**, `LICENTIE-NETHASP.md:14-22` — at
±0.03 s the game `connect`s to `/tmp/.aksusb` (twice, `errno=2`), then sends a 165-byte datagram to
`127.0.0.1:475` and to `255.255.255.255:475`, **four times, 1 s apart (0, 1, 2, 3 s), with no
`recvfrom` ever answering**, then `exit(0)` at ~4 s.

**Two practical consequences, both INFERRED from the above:**

1. Any answering layer must be in place **before `main`'s third call**. An `LD_PRELOAD` constructor or a
   loader-side GOT override is early enough; a daemon that is started by a script *after* the guest has
   begun is not.
2. The archive's framing ("the game needs something from the TeknoParrot runtime, then it works") points
   at the right step for the wrong reason: TeknoParrot's `halo.so`-style preload works because it is
   loaded before the guest, not because it patches anything after the fact.

### 4.4 Stale or wrong — including two of **our own** notes

The brief asks specifically for this, because our own notes have already contradicted our own
measurements twice today. Both sides are listed.

**In the archive (against today):**

1. `dongles/README.md:215`, `:228`; `LOSKOPPELEN.md:232`, `:294` — the **10,826** file count. Today:
   5,413. Exact 2× factor; no command given for the archive's number. **D3.**
2. `dongles/README.md:229-231` — the suggestion that the `hasp/` folder might be Terminator's licence
   data ("ander opslagpatroon … of iets eigens"). Today: the licence data is the HASP-HL container for
   `0xFFF2`/`0xFFF0`; the folder's producer and consumer are both unmeasured and `game` has no
   `hasp`-containing path literal. The observation survives; the implication does not. **D2.**
3. `antwoorden_ronde4.md:74`, `:81` — *"Terminator … **geen** [beschermingsveld] … alleen invoer plus
   aanpassingen."* Wrong as a conclusion about the game. **D1.**
4. `11_hardware_patches_dongles.md:1181-1191` — no dongle row for Terminator. Correct for the profile,
   misleading as a statement about the title.
5. `Terminator.md:24` — *"stopt direct met exit 255 ("DongleInitialize() FAILED")"*. Wrong on both
   halves: it does not stop at the dongle (it continues 50 steps), and the 255 was measured to be the
   ptrace/Yama protection layer, with the dongle failure ending in `exit 0` after ~4 s. **D4/D5.**
6. `INVENTARIS-dongle-per-game.md:87` (GhostBusters) — *"de dump heeft geen donglebestand zoals Halo en
   Terminator"*. Treats `hasp/` as "a dongle file" for Terminator; that is the Position-A assumption,
   not a measurement.
7. `antwoorden_zonder_teknoparrot.md:57` — Terminator in the 64-bit family. Today: 32-bit. The archive's
   own `Terminator.md:1` says 32-bit. **D6.**
8. `INVENTARIS-dongle-per-game.md:39`/`:113`, `Terminator.md:6` — "playable". True for the TeknoParrot
   stack, not evidence about the licence. **D7.**
9. `dongles/_template/dongle.json` — `fileid: 65525` pointing at the `0xFFF4` filename. Breaks the
   archive's own naming rule; the real example is correct.
10. `RGD1-formaat.md:236` vs `dongles/src/rgd1.py:21` — the published password and the tool's hardcoded
    default differ, so the tool cannot open the archive's own shipped `.rgd`. **MEASURED** in §1.5.
11. `17_hasp_dongle.md:70-122` — a reader skimming for "the HASP answer" finds **Halo's** `haspid`,
    features and file IDs. Nothing in that document marks them as Halo-only.

**In our own deck notes (stale against today — these are the ones that have burned us):**

12. `dropin/ravengun-hasp/Terminator/dongle.json` and `dropin/dropin.ini` — *"er is dus geen statisch
    gelinkte HASP-client om aan te haken"*. **Refuted by today's own measurement**: 0 dynamic `hasp_`
    symbols means no *name* to hook, not no client — the client is statically linked
    (`DONGLE-ROUTE.md` §1.4: *"The correct reading is therefore **not** 'the game has no HASP client'
    but: the HASP client is statically linked into `game`"*). The same file declares `fileid 65524`
    (wrong; measured `0xFFF2`/`0xFFF0`) and a 128-byte image.
13. `DRAAIBOEK-BENCH.md:130` — success criterion *"exactly the §2 lines, then exit 255 with
    `DongleInitialize() FAILED!`"*, and `:299` *"exitcode 255 ← van de GAST, niet van ons"*. Today's
    measured success/failure signature is **`exit 0` after 4 s**; the 255 belongs to the protection
    layer. **A run judged by that bench book would be scored wrong.**
14. `KOPPELING-EN-IO.md:277` already flags the profile note as outdated (*"De note in het profiel is
    verouderd ('stopt direct met exit 255'): dat is van vóór mijn werk"*) — good, but the same stale note
    is still what the archive and the profile say.
15. `DONGLE-ROUTE.md` is **itself** a moving target: the 638-line revision I first read nominated
    `0x081c6a50` as "the game's dongle wrapper" and listed `hasp_login` callers at `0x081c6b1d`,
    `0x081c6c63`, `0x081c6cdf`. The 1,181-line revision corrects this: `0x081c6a50` is **dead code**,
    only `0x081c6b1d` is inside it, and the reachable wrapper is `0x081c6b70` via
    `main → 0x080abf50 → 0x081c6d20 → 0x081c6b70 → hasp_login`. **MEASURED** (both revisions read;
    the correction is in the newer §3). Anyone quoting the earlier revision has the wrong address.

---

## 5. What is still missing — concrete, with a falsifiable check for each

None of the following is in the archive. Each has a check that can come out the other way.

**M1 — the *content* the game expects at `0xFFF2`.** We know the offsets and lengths (1 byte at
`0xFFF2+0x1C`; 4 bytes at `0xFFF2+0x3C`, read and written) but not the values.
*Check:* hook `hasp_read` at `0x083bad30` and log `(fileid, offset, length)` **and the buffer contents
the game compares** — or, cheaper, drive `0xFFF2+0x1C` through all 256 values in 256 runs and find the
value at which the draw-time switch at `0x080c2a49` stops skipping `glutMainLoop`.
*Falsified if:* no single value changes the outcome, or the switch is not the licence flag.

**M2 — Terminator's `haspid`, `features` and `keycaps`.** Not in the archive (its five haspids are
unattributed; `744811821`/`748675309` are Halo's; `133701337`/`1663570977`/`287339817` are the core's
templates). And today's static scan found `hasp_get_info` (`0x083ba8a0`) has **0** `E8` callers.
*Check:* inline-hook `hasp_get_info` and run once. If it never fires, the XML descriptor is **not**
needed for this title and `haspid` is a non-issue.
*Falsified if:* it fires, in which case the expected XML must be captured.

**M3 — the byte-exact HASP-HL XML dialect for the `aksusb` socket.** The format strings and the
`v2c_req`/`v2c_reply`/`c2v_req`/`c2v_reply`/`xlm_api_login_request` names are measured, but no
request/response pair exists anywhere in the archive.
*Check:* observe one TeknoParrot run with `net-log.c` on the AF_UNIX socket and capture one pair;
success = a byte-identical reply replays and the game's dongle flag (state `+0x10`,
`0x087c4220`) flips.
*Falsified if:* the reply is not deterministic across two captures (nonce/session-bound).

**M4 — the classic `/dev/Hardlock` `ioctl(0x02FED112)` record layout.** Today's reading is partial and
**INFERRED**: an `int32` count at `buf+0`, then 8-byte records (16-bit id at `+0`, 16-bit value at
`+4`), ids above `0xFFFF` skipped.
*Check:* the dossier's own §5 instrument — override `open@GLIBC_2.0` (GOT slot `0x08599700`), log
`(path, return address)`, and require the `/dev/Hardlock` line's return address to be exactly
`0x083c6b18` or `0x083d3d8b`, with `/dev/urandom` as the calibrated positive control.
*Falsified if:* the log is empty **and** `/dev/urandom` is absent (instrument broken), or the return
address is different (wrong file hash).

**M5 — which transport is actually used, and in what order.** Both are `E8`-reachable statically; only a
run settles it. This is an explicit gap in today's dossier too.
*Check:* M4's instrument, then move the shim to `connect` (slot `0x08599834`) and look for `AF_UNIX` +
`/tmp/.aksusb`.
*Falsified if:* the `/dev/Hardlock` line is present, in which case the classic port is the first branch.

**M6 — the AES key material for `hasp_encrypt`/`hasp_decrypt`.** The unstripped twin holds
`aladdinPassHL`, `aladdinPassSeedHL`, `gRockeySeed`, `DongleGetRand`; the per-title key is not
established, and the archive's `dongle.json` contains no key material at all (**MEASURED**, schema has no
key field).
*Check:* hook `0x083bbd60`/`0x083bbce0`, log input/output pairs; success = the same input yields the same
output across two runs **and** the game's subsequent `hasp_read` comparison passes.
*Falsified if:* the output differs per run (then it is a challenge/response and M3's capture is
required instead).

**M7 — whether anything reads the `hasp/` folder.** The archive says "not investigated"
(`dongles/README.md:228`); today says the producer and consumer are both unmeasured and `game` has no
`hasp`-containing path literal.
*Check:* hook `open()` and require a return address inside the guest `.text`
(`0x08057240`–`0x084fa438`) with a path containing `hasp`, with `/dev/urandom` as the calibrated control.
*Falsified if:* such a line appears — then the folder *is* a consumer, and the 5,410 records become a
first-class lead.

**M8 — whether TeknoParrot's own per-game HASP store holds Terminator's answer.** The archive documents
the core's store as a single `hasp.bin` with **named** file IDs `HASP_FILEID_MAIN` /
`HASP_FILEID_LICENSE` (`17_hasp_dongle.md:187-201`, `13_opengestelde_cores.md:237-238`), while the game
asks for numeric `0xFFF2`/`0xFFF0`.
*Check:* locate the core's dongle data for `Terminator` (the string `[HASP DONGLE]: Found HASP data for
%s, reading...` means it is keyed by name) and compare the stored bytes at the offsets the game reads.
*Falsified if:* no Terminator entry exists in the core's store (then the core answers from a template,
and M1's value search is the only route).

**M9 — whether `t4-patch` participates in the decisive check.** It carries its own static HASP classic
client and two `/dev/Hardlock` strings; `game` has no reference to `t4-patch`; the loader profile loads
`game`.
*Check:* run the loader with a file-open log and see whether `t4-patch` is ever executed (an `execve` or
a `dlopen`), or whether its two `/dev/Hardlock` offsets (`0xfb4f5`, `0xfdd0e`) are ever reached.
*Falsified if:* `t4-patch` is never started, in which case it can be ignored entirely.

**M10 — a second protection family (Feitian Rockey).** The twin exports `AllocRockey`, `FindRockey`,
`gRockeySeed`, `rockey_ioctl`. Not in the archive at all.
*Check:* hook the Rockey entry points and run once; a run with zero hits closes it.
*Falsified if:* any of them is called before the draw-time skip.

---

## 6. Could not determine

1. **Whether any file in the archive is derived from a Terminator dongle.** No candidate exists: the
   only dongle artefacts are Halo's (`dongles/halo-fireteam/`, `dongles/halo-fireteam.rgd`), the only
   haspids are unattributed, and `0xFFF2`/`0xFFF0`/`hhl_mem` have 0 hits. I cannot rule out that a
   Terminator-derived number sits unlabelled inside the core's string harvest
   (`data/decrypted_harvest.json`), but nothing there is identified as Terminator's.
2. **What the archive's "10,826" counted.** No command, no listing and no tool output accompanies the
   number anywhere in the archive. The exact 2× relation to today's 5,413 is suggestive and nothing
   more.
3. **Whether the archive's "Terminator playable" run ever exercised the licence check.** The archive
   contains no logs from that run; its own §5 says the `hasp/` folder was never investigated. So I
   cannot tell whether TeknoParrot passed the check, bypassed it, or the check never fired on that stack.
4. **Which of the archive's three Terminator positions its author actually holds.** They come from
   different documents (a letter, a dossier, and a Q&A round) and the archive never reconciles them.
   Position B is the one that survives contact with today's measurements; whether its author knew that
   is not determinable from the text.
5. **Whether the RGD1 container is meant to ship to users or only to distribute dongles between
   machines.** `RGD1-formaat.md:137-155` describes an end-user password prompt; `§1` and `§9` say the
   key travels with the tool and gives no secrecy. Both readings are supported; the archive does not say
   which was intended.
6. **Whether the 5,410 `hasp/` records are a challenge→response table.** The archive never says they
   are; today's `LICENTIE-NETHASP.md` calls them *"een **vastgelegde uitdaging-antwoordtabel**"*
   (a recorded challenge/response table) while `DONGLE-ROUTE.md` §3.3/§7 says the producer and consumer
   are unmeasured and the contents are hash/key-like (entropy 7.9960). Those two of our own documents
   disagree with each other; which is right is not determined by either measurement on record.
7. **Whether the archive's Halo example transfers to Terminator at all.** The Halo image is 112 bytes;
   Terminator's candidate image (`hhl_mem.dmp`) is 128 bytes with a 16-byte suspected tool trailer.
   Whether the correct Terminator image is 112 bytes is **INFERRED, not measured** (`DONGLE-ROUTE.md`
   §3.2), and the archive has no Terminator image to compare against.
8. **Whether the two transports are alternatives or sequential.** Both are present and `E8`-reachable;
   the last hop into `connect("/tmp/.aksusb")` is INFERRED in today's dossier (`API_HL_CSTACK
   0x083d6a50` has 0 `E8` callers and no plain pointer anywhere in the file). The archive says nothing
   about it. Only a run settles it (M4/M5).

---

**Where this file went.** Written to
`~/projects/ravengun-patches/patches/Terminator/CAVE-SYNTHESE.md` on the deck — i.e. next to the other
Terminator dossiers (`DONGLE-ROUTE.md`, `LICENTIE-NETHASP.md`, `BESCHERMINGSLAAG-PTRACE.md`,
`KOPPELING-EN-IO.md`, `KETEN-EN-METHODE.md`). No copy was placed in the findings archive: it is
read-only, and nothing there was touched. The only other file created for this work is a scratch script
under the Windows `%TEMP%` (`rgd_pw.py`), used to test the two RGD passwords in §1.5.
