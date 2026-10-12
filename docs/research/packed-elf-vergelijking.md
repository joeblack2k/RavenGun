# Gepakte ELF's vergeleken: wat voor bestand is Halo (ELF64) echt?

*Read-only statische meting op de NAS-bestanden. Er is **niets** gestart: geen game, geen
BudgieLoader, geen Wine. Er is niet op de NAS geschreven. Ronde: `_packed-elf-*` (deze sessie).
Alle getallen komen uit de hieronder letterlijk genoemde commando's; **gemeten** en **afgeleid**
zijn apart gelabeld. `ELF-LADER-SPEC.md` is **niet** gewijzigd (één schrijver).*

Doel: het gat in de tabel "De twee klassen" van `ELF-LADER-SPEC.md` dichten. Daar stonden voor Halo
drie cellen op "niet geverifieerd"/"niet gemeten": het aantal structuur-stubs, de GOT-slots in het
bestand, en (impliciet) de dynsym-topologie. Daarnaast: twee nieuw gevonden gepakte games
(Target Terror Gold, Walking Dead) en twee controles (Big Buck Hunter Pro / -Home).

---

## 0. De drie dingen die de vraag beslissen (vooraan)

1. **De pakking van Halo is qua *klasse* niet anders dan 32-bits.** Dezelfde vier merkers zijn
   gemeten: de HASPE-marker, `SHNUM = 0` (in `game`), de GOT die in het bestand al met **absolute
   adressen uit een vreemd adresbeeld** is gevuld (hier `0x7fd0b…`, 64-bits), en een `dynsym`
   waarin de pakker een **naamloos blok aaneengesloten achter de originele tabel plakt** — precies
   bij `DT_HASH.nchain`. Voor Halo: `nchain = 220`, naamloze run **220..680 (461 stuks)**, tabel
   eindigt op **681**. Voor Galaga: `nchain = 4719`, run **4719..4976 (258)**, einde 4977. Dat is
   hetzelfde mechanisme, byte voor byte in dezelfde verhouding.
2. **De 64-bits pakking is op twee concrete punten wél wezenlijk anders**, en dat zijn precies de
   punten waar een eigen lader op zou stuklopen:
   * **`0xdeadbeef` is géén 64-bits merker.** Halo heeft er **24** (in `game`) en **2** (in
     `game_original`), tegen **357/577/442/807** bij 32-bits — daar is 100% van de structuurstubs
     vervangen. Bij Halo blijven **177 van de 202** 16-byte-stubs een **echte** reloc-index
     (1..177) pushen, dus luie binding is daar niet stukgemaakt.
   * **De structuurstub heeft bij 64-bits een andere vorm.** In `game_original` staat **geen enkele**
     8-byte-stub; in `game` staat een blok van **611 stubs van 8 byte** (`ff 25 <rel32> 66 90`) op
     `0x40d390..0x40e6a8`, waarvan de slots (één 64 KiB-venster rond `0x1690c20`) voor **611 van de
     617** doelen een absoluut `0x7fd0b…`-adres bevatten. De 32-bits gepakte games hebben **nul**
     zulke 8-byte-stubs; hun structuur-PLT is 16-byte met `push 0xdeadbeef`.
3. **De cel "24 structuur-stubs" is geverifieerd maar meet het verkeerde.** 24 = het aantal
   `68 ef be ad de` in `game` (en 2 in `game_original`) — dat klopt exact. Maar de *echte*
   structuurtabel van `game` heeft **611** stubs. Voor ELF64 onderschat de deadbeef-teller het
   aantal structuurslots dus met een factor ~25. Wie de lader op deadbeef laat beslissen, ziet Halo
   als "nauwelijks gepakt" terwijl 68% van de importnamen gewist is.

---

## 1. Vergelijkende tabel (alles gemeten)

Betekenis van de kolommen:
* **dynsym** = einde van de tabel volgens `DT_STRTAB − DT_SYMTAB` / `DT_SYMENT`; tussen haakjes
  `DT_HASH.nchain`. Beide zijn onafhankelijk geverifieerd met een plausibiliteitstoets per entry.
* **structuur-stubs** = aantal `68 ef be ad de` in de uitvoerbare segmenten, en tussen haakjes het
  aantal 16-byte-PLT-stubs (`ff 25 … 68 … e9 …`).
* **8-byte-stubs** = `ff 25 … 66 90` (de `.plt.sec`-vorm).
* **GOT-slots in het bestand** = de waarde die in het bestand staat **op de doelen van de
  structuurstubs** (dus niet een willekeurig venster). `eigen` = binnen het eigen beeld,
  `nul` = 0, `anders` = geen van beide.

| bestand | klasse | grootte (B) | Class | entry | SHNUM | NEEDED | dynsym (nchain) | structuur-stubs | 8-byte-stubs | GOT-slots in bestand | UND met naam / st_name=0 (binnen dynsym-einde) | JMPREL JUMP_SLOT | HASPE-marker |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Galaga Assault `game` | **gepakt** | 7.278.592 | ELF32 | `0x81150a7` | **0** | 10 | 4977 (4719) | **357** (357) | 0 | **356 × `0xb7…`, 1 × `0`** | 102 / 259 | 100 | **ja** |
| PacmanChomp `game` | **gepakt** | 6.852.608 | ELF32 | `0x8077f73` | **0** | 12 | 4234 (3986) | **442** (448) | 0 | 441 × `0xb7…`, 6 × eigen, 1 × `0` | 202 / 249 | 195 | **ja** |
| PacmanChomp `game_original` | gepakt, ongebonden | 4.144.655 | ELF32 | `0x835f8da` | 2 | 11 | 3924 (3740) | 285 (286) | 0 | **286 × eigen** (ongebonden) | 103 / 185 | 101 | ja |
| Jurassic Park `game` | **gepakt** | 17.694.720 | ELF32 | `0x813befc` | **0** | 16 | 19283 (18811) | **577** (578) | 0 | **576 × `0xb7…`, 1 × eigen (`0x813a0f6`), 1 × `0`** | 108 / 473 | 101 | **ja** |
| Jurassic Park `game_original` | gepakt, ongebonden | 17.690.127 | ELF32 | `0x904a8da` | 2 | 16 | 19283 (18811) | 577 (578) | 0 | **578 × eigen** (ongebonden) | 108 / 473 | 101 | ja |
| **Target Terror Gold `game`** | **ongepakt** (correctie!) | 1.197.165 | ELF32 | `0x804b1c0` | **36** | 7 | 209 (209) | **0** (185: 184 numeriek, 1 push 0) | 0 | 185 × eigen | **187 / 1** (alleen index 0) | 185 | **nee** (zie §3) |
| **Walking Dead `game`** | **gepakt** (nieuw) | 19.165.184 | ELF32 | `0x8055d70` | **0** | 18 | 1021 (320) | **807** (813) | 0 | 520 × `0xb7…`, 286 × anders, 6 × eigen, 1 × `0` | 114 / **702** | 106 | **ja** |
| **Halo `game`** | **gepakt** (ELF64) | **33.554.432** (`0x2000000`) | ELF64 | `0x414600` | **0** | 13 | **681** (220) | **24** (202, waarvan 177 **numeriek** 1..177) | **611** in blok `0x40d390..0x40e6a8` (618 patronen totaal) | **611 × `0x7fd0b…`, 1 × eigen, 5 × `0`** op de 8-byte-slots; op de 16-byte-slots 121 × `0x7f…`, 79 × eigen, 2 × anders | **219 / 462** | **178 (RELA)** | **ja** |
| **Halo `game_original`** | gepakt, ongebonden | 31.270.111 | ELF64 | `0x1f65240` | 2 | 13 | **681** (220) | **2** (180, waarvan 177 **numeriek** 1..177) | **0** | **178 × eigen** op de 16-byte-slots; 178 × eigen op de JMPREL-slots | **219 / 462** | **178 (RELA)** | ja |
| Big Buck Hunter Pro `game` | **ongepakt** (controle) | 51.469.664 | ELF32 | `0xb15d8b0` | 0 | 7 | 188 (188) | 0 (177: 176 numeriek, 1 push 0) | 0 | 177 × eigen | 179 / 1 | 177 | **nee** |
| Big Buck Hunter Pro Home `game` | **ongepakt** (controle) | 51.491.248 | ELF32 | `0xb162d00` | 0 | 7 | 197 (197) | 0 (185: 184 numeriek, 1 push 0) | 0 | 185 × eigen | 187 / 1 | 185 | **nee** |

**Wat de tabel onmiddellijk zegt**

* `SHNUM = 0` is **geen** pakkingsmerker: Big Buck Hunter Pro en -Home hebben ook `SHNUM = 0` en zijn
  aantoonbaar ongepakt (0 deadbeef, `nchain` == tabel-einde, alleen index 0 naamloos). De merker is
  de combinatie **HASPE-string + naamloos blok direct achter `DT_HASH.nchain`**.
* De verhouding naamloos/totaal is bij Halo **462/681 = 67,8%**. Bij 32-bits gepakt: Galaga 71,7%,
  Jurassic Park 81,4%, PacmanChomp 55,2%, Walking Dead 86,0%. Halo zit dus **midden in dezelfde
  bandbreedte** — de naamwissing is bij 64-bits niet anders.
* De volledige naamloze runs beginnen bij **alle vier** de gepakte families exact op `DT_HASH.nchain`
  (Galaga 4719, JP 18811, WalkingDead 320, Halo 220, PacmanChomp 3986) en bij **geen enkele**
  ongepakte game (Target Terror Gold 209 = nchain, BBH 188/197 = nchain: alleen index 0).
* **DT_JMPREL wijst bij álle gepakte games naar een tweede slottabel, niet naar de structuur-GOT.**
  Bij 32-bits waren dat ~100 relocaties rond `0x86f83f8` (Galaga) met de tweede PLT rond `0x8658b10`;
  nu ook Halo: 178 relocaties op `0x238dc30..0x238e1c8` (0 daarvan in de `DT_PLTGOT`-regio), met de
  bijbehorende tweede PLT op `0x1f5c600..0x1f5d130`. Bij de ongepakte games vallen de `r_offset`s
  juist **wel** met de PLT-GOT samen (185/185, 177/177, 185/185).

---

## 2. De letterlijke commando's en de ruwe getallen

Alles read-only, niets gestart. `NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot`.

### 2.1 Klasse, SHNUM, entry, NEEDED, HASPE

```bash
file "$NAS/Galaga Assault/game"
readelf -h "$NAS/Halo Fireteam/g7/halo/game"
readelf -h "$NAS/Halo Fireteam/g7/halo/game" | grep -iE 'Class|Type|Machine|Entry|Number of section|program headers'
readelf -d "$NAS/Halo Fireteam/g7/halo/game" | grep -iE 'PLTGOT|JMPREL|SYMTAB|STRTAB|HASH|NEEDED|PLTREL'
strings -a "$NAS/Halo Fireteam/g7/halo/game" | grep -i HASPE
```

Ruw (Halo `game`): `ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), dynamically linked,
interpreter /lib64/ld-linux-x86-64.so.2, no section header`; `Class: ELF64`, `Type: EXEC`,
`Machine: Advanced Micro Devices X86-64`, `Entry point address: 0x414600`,
`Number of section headers: 0`, `Number of program headers: 9`.
HASPE: `PATCHED_WITH_HASPE_LINUX_LOADER_1.1` en `HASPE_LINUX_LOADER_1.1`, op bestands-offset
`0x65da` in de `DT_STRTAB`-stringtabel. Halo `game_original`: zelfde twee strings op `0x65da`,
`Number of section headers: 2`.
`readelf -d` op beide `game_original`-bestanden geeft `readelf: Error: no .dynamic section in the
dynamic segment` (de 2 sectiekoppen dekken de dynamic-sectie niet); de waarden hieronder komen uit
`PT_DYNAMIC` zelf.

`DT_PLTGOT` / `DT_JMPREL` / `DT_SYMTAB` / `DT_STRTAB` (`readelf -d`):

| bestand | PLTGOT | JMPREL | JMPREL-grootte | SYMTAB | STRTAB |
|---|---|---|---|---|---|
| Galaga Assault/game | `0x830d000` | `0x807f840` | 0x320 | `0x8051a0c` | `0x806511c` |
| PacmanChomp/game | `0x8255000` | `0x83614d0` | 0x618 | `0x804ed10` | `0x805f5b0` |
| PacmanChomp/orig | `0x8235e8c` | `0x8072018` | 0x328 | `0x804ea88` | `0x805dfc8` |
| JurassicPark/game | `0x8add000` | `0x81384d8` | 0x328 | `0x806e680` | `0x80b9bb0` |
| JurassicPark/orig | `0x8add000` | `0x81384d8` | 0x328 | `0x806e680` | `0x80b9bb0` |
| Target Terror Gold | `0x8144c50` | `0x804a03c` | 0x5c8 | `0x80487a8` | `0x80494b8` |
| Walking Dead | `0x8a27000` | `0x8051080` | 0x350 | `0x8048928` | `0x804c8f8` |
| **Halo/game** | **`0x1690b58`** | **`0x1f47c78`** | **0x10b0** | **`0x401990`** | **`0x405968`** |
| **Halo/orig** | **`0x1690b58`** | **`0x1f47c78`** | **0x10b0** | **`0x401990`** | **`0x405968`** |
| BBHPro | `0x80ec110` | `0x8049cbc` | 0x588 | `0x804866c` | `0x804922c` |
| BBHProHome | `0x80f0110` | `0x8049ee0` | 0x5c8 | `0x8048798` | `0x80493e8` |

Halo heeft `DT_PLTREL = 0x7` (RELA, 24-byte entries); alle 32-bits bestanden `DT_PLTREL = 0x11`
(REL, 8-byte). `DT_RELA`=`0x1f47b58`, `DT_RELASZ`=`0x120` (12 entries: 11 × `R_X86_64_COPY`,
1 × `R_X86_64_GLOB_DAT`; **nul** `R_X86_64_RELATIVE` — net als de 32-bits gepakte games, die nul
`R_386_RELATIVE`/`R_386_GLOB_DAT` hebben behalve een handvol `R_386_COPY`).

### 2.2 Het aantal sectiekoppen (`SHNUM`)

`readelf -h <bestand> | grep 'Number of section headers'`. Uitkomst: Galaga 0, PacmanChomp/game 0,
PacmanChomp/orig 2, JurassicPark/game 0, JurassicPark/orig 2, **Target Terror Gold 36**,
Walking Dead 0, **Halo/game 0**, **Halo/orig 2**, BBHPro 0, BBHProHome 0.

### 2.3 De PLT-vorm (letterlijke bytes)

De PLT is te vinden via `DT_JMPREL` → `r_offset` → het GOT-slot → de `ff 25`-stub die dat slot
gebruikt; en via `DT_PLTGOT`. Dump van de eerste stub, en van een gewone stub (`re/_packed-elf-meet2.py`,
dat de `ff 25`-patronen in de uitvoerbare segmenten zoekt en de bytes letterlijk print).

**Galaga Assault/game** — PLT0/stub-0 op `0x8080396`:
```
ff 25 08 d0 30 08 00 00 00 00 ff 25 0c d0 30 08 68 ef be ad de e9 e0 ff ff ff
```
gewone stub op `0x8080770` (doel `0x830d100`):
```
ff 25 00 d1 30 08 68 ef be ad de e9 10 fc ff ff
```
→ **elke** stub: `ff 25 <abs32 GOT> | 68 ef be ad de | e9 <rel32 naar PLT0>`.

**Jurassic Park/game** — `0x8139736`:
```
ff 25 08 d0 ad 08 00 00 00 00 ff 25 0c d0 ad 08 68 ef be ad de e9 e0 ff ff ff
```
**PacmanChomp/game** — `0x807293e`:
```
ff 25 94 5e 23 08 00 00 00 00 ff 25 98 5e 23 08 68 ef be ad de e9 e0 ff ff ff
```
**Walking Dead** — `0x8052a20`-blok, idem `68 ef be ad de` (807 stuks).
**Target Terror Gold** (ongepakt) — `0x804a622`:
```
ff 25 58 4c 14 08 00 00 00 00 ff 25 5c 4c 14 08 68 00 00 00 00 e9 e0 ff ff ff
```
→ hier pusht de stub een **echte byte-offset in de REL-tabel** (`0x00`, `0x08`, `0x10`, …), niet
`0xdeadbeef`.

**Halo `game`** — `.plt` op `0x40d226`:
```
ff 25 3c 39 28 01 0f 1f 40 00 | ff 25 3a 39 28 01 68 ef be ad de e9 e0 ff ff ff |
ff 25 32 39 28 01 68 ef be ad de e9 d0 ff ff ff | ff 25 2a 39 28 01 68 ef be ad de e9 c0 ff ff ff
```
(22 zulke deadbeef-stubs op `0x40d230..0x40d38f`.)
En de `.plt.sec`-vorm op `0x40d3c0`:
```
ff 25 92 38 28 01 66 90 | ff 25 92 38 28 01 66 90 | ff 25 92 38 28 01 66 90
```
**Halo `game` — de tweede PLT op `0x1f5c600`** (dit is de PLT van `DT_JMPREL`):
```
ff 25 2a 16 43 00 68 00 00 00 00 e9 e0 ff ff ff | ff 25 22 16 43 00 68 01 00 00 00 e9 d0 ff ff ff
```
→ pushes `0, 1, 2, …` = **echte reloc-indexen** (stap 24 = `DT_SYMENT` van RELA).
**Halo `game_original`** — `0x1f5c5f6`:
```
ff 25 6c 45 73 ff 0f 1f 40 00 | ff 25 2a 16 43 00 68 00 00 00 00 e9 e0 ff ff ff |
ff 25 22 16 43 00 68 01 00 00 00 e9 d0 ff ff ff
```
→ dezelfde 177 numerieke stubs; **nul** 8-byte-`.plt.sec`-stubs in het hele bestand.

Stubclassificatie per bestand (`re/_packed-elf-meet3.py`):

| bestand | 16-byte stubs | deadbeef | numeriek | push 0 | 8-byte stubs | bereik 16-byte |
|---|---|---|---|---|---|---|
| Galaga | 357 | 357 | 0 | 0 | 0 | `0x80803a0..0x80819e0` |
| PacmanChomp/game | 448 | 442 | 5 | 1 | 0 | `0x8076350..0x8077f40` |
| PacmanChomp/orig | 286 | 285 | 0 | 1 | 0 | `0x8072948..0x8073b18` |
| JurassicPark/game | 578 | 577 | 0 | 1 | 0 | `0x8139740..0x813bb50` |
| JurassicPark/orig | 578 | 577 | 0 | 1 | 0 | `0x8139740..0x813bb50` |
| Target Terror Gold | 185 | **0** | 184 | 1 | 0 | `0x804a62c..0x804b1ac` |
| Walking Dead | 813 | 807 | 5 | 1 | 0 | `0x8052a20..0x8055ce0` |
| **Halo/game** | 202 | **24** | **177** | 1 | **611 in `0x40d390..0x40e6a8`** (618 patronen totaal) | `0x40d230..0x1f5d130` |
| **Halo/orig** | 180 | **2** | **177** | 1 | **0** | `0x1f5c600..0x1f5d130` |
| BBHPro | 177 | 0 | 176 | 1 | 0 | `0x804a26c..0x804ad6c` |
| BBHProHome | 185 | 0 | 184 | 1 | 0 | `0x804a4d0..0x804b050` |

De 24 deadbeef-stubs van Halo `game` zitten op `0x40d230, 0x40d240, …` (22 stuks, aaneengesloten)
plus `0x1f5cb80` en `0x1f5d0c0`. De 2 van `game_original` op `0x1f5cb80` en `0x1f5d0c0` — **precies
dezelfde twee adressen**.

### 2.4 De GOT in het bestand

Gemeten **op de doelen van de stubs** (niet op een willekeurig venster), plus een ruim venster vanaf
`DT_PLTGOT` als controle (`re/_packed-elf-meet2.py`).

* **32-bits gepakt, `game`**: de structuurslots bevatten absolute adressen in `0xb7xxxxxx`
  (Galaga `356 × 0xb7…` + `1 × 0`; JP `576 × 0xb7…` + `1 × 0x0813a0f6` (eigen beeld) + `1 × 0`;
  PacmanChomp `441 × 0xb7…` + `6 × eigen` + `1 × 0`; Walking Dead `520 × 0xb7…` + `286 × anders` +
  `6 × eigen` + `1 × 0`).
  Eerste 24 rijen Galaga vanaf `DT_PLTGOT`: `0x0 0x0 0x0 0xb750bf40 0xb7689e60 0xb7689ef0
  0xb7509230 …`; de adresrun loopt over de rijen **3..359**.
* **32-bits gepakt, `game_original`**: **ongebonden** — PacmanChomp `286 × eigen`,
  JP `578 × eigen` (alleen `stub+6`-waarden in `0x08…`).
* **Halo/game**: eerste rijen vanaf `DT_PLTGOT = 0x1690b58`: `0x0 0x0 0x0 0x7fd0b3f8ac30
  0x7fd0b624b940 0x7fd0b3f89950 …` → dezelfde opbouw als 32-bits: `GOT[0..2]` leeg/gereserveerd en
  daarna **absolute adressen uit een vreemd proces**. De 8-byte-stubs wijzen naar één doorlopend
  slotbereik (`0x1690c20`, 592 van de 617 doelafstanden zijn exact 8 byte); **611 van de 617 slots**
  bevatten `0x7f…`, 5 nul, 1 eigen beeld.
  De slots van `DT_JMPREL` (`0x238dc30..0x238e1c8`, 178 stuks) bevatten in `game`
  `99 × 0x7f… + 79 × eigen`, en in `game_original` **`178 × eigen`**.
* **Controle (ongepakt)**: Target Terror Gold 185 × eigen, BBHPro 177 × eigen, BBHProHome 185 × eigen
  — dus geen enkel vreemd absoluut adres, en `DT_JMPREL` valt volledig binnen `[DT_PLTGOT,+0x4000)`.

### 2.5 UND-symbolen bij meerdere bovengrenzen (12.000 / 20.000 / 40.000)

Commando-equivalent: `re/_packed-elf-meet2.py` loopt `dynsym` af tot de opgegeven bovengrens en telt
per entry `st_shndx == 0` met `st_name == 0` (naamloos) of `st_name != 0` (met naam).

| bestand | cap 12.000 | cap 20.000 | cap 40.000 | waar eindigt de tabel echt? |
|---|---|---|---|---|
| Galaga | 105 / 500 | 555 / 505 | 1392 / 505 | 4977 (naamloos verzadigt op 505, **met naam groeit mee**) |
| PacmanChomp/game | 310 / 694 | 882 / 710 | 1940 / 717 | 4234 (**groeit mee**) |
| PacmanChomp/orig | 262 / 388 | 877 / 405 | 1975 / 414 | 3924 (**groeit mee**) |
| JurassicPark/game | 8 / 1 | **108 / 473** | **108 / 473** | 19283 — **verzadigt**, enige bestand dat dat doet |
| JurassicPark/orig | 8 / 1 | **108 / 473** | **108 / 473** | 19283 — idem |
| Target Terror Gold (ongepakt) | 932 / 18 | 1386 / 31 | 2768 / 47 | 209 (**groeit mee**, puur ruis erna) |
| Walking Dead | 1117 / 1770 | 1970 / 1785 | 4282 / 1817 | 1021 (**groeit mee**) |
| **Halo/game** | 1410 / 2564 | 2713 / 2572 | 5778 / 2597 | 681 (**groeit mee**) |
| **Halo/orig** | 167 / 2548 | **167 / 2548** | 168 / 2548 | 681 (vrijwel verzadigd) |
| BBHPro (ongepakt) | 281 / 5 | 281 / 5 | 408 / 120 | 188 (groeit bij 40.000) |
| BBHProHome (ongepakt) | 299 / 6 | 299 / 6 | 366 / 51 | 197 (groeit bij 40.000) |

**Ja, het eerder bij PacmanChomp geziene effect herhaalt zich, en niet alleen daar.** Zodra de
bovengrens voorbij het einde van `dynsym` komt, leest de scanner willekeurige bestandsbytes als
symbolen; die hebben meestal `st_shndx = 0`, dus ze tellen als "UND met naam" mee. Bij Galaga,
PacmanChomp (beide), Walking Dead en **Halo/game** groeit het aantal met naam dus mee met de
bovengrens; bij Target Terror Gold en BBHProHome (beide **ongepakt**!) gebeurt dat ook. De
cap-methode is dus voor bijna alle bestanden onbruikbaar, en de getallen uit de eerdere
PacmanChomp-meting zijn inderdaad ruis.

**De methode die wél werkt** (en die de SPEC-cijfers 357/577/442/361 reproduceert): het einde van de
tabel is `(DT_STRTAB − DT_SYMTAB) / DT_SYMENT`, en dat is onafhankelijk bevestigd met een
plausibiliteitstoets per entry (eerste entry die faalt). Beide vallen samen:

| bestand | `DT_STRTAB−DT_SYMTAB` | / SYMENT | eerste niet-plausibele index | `DT_HASH.nchain` | UND met naam / naamloos **binnen het echte einde** |
|---|---|---|---|---|---|
| Galaga | 79.632 | 4977 | 4977 | 4719 | **102 / 259 = 361** (SPEC: 99/258 = 357) |
| PacmanChomp/game | 67.744 | 4234 | 4234 | 3986 | 202 / 249 = 451 |
| PacmanChomp/orig | 62.784 | 3924 | 3924 | 3740 | 103 / 185 = 288 |
| JurassicPark/game | 308.528 | 19283 | 19283 | 18811 | **108 / 473 = 581** (SPEC: 105/472 = 577) |
| JurassicPark/orig | 308.528 | 19283 | 19283 | 18811 | 108 / 473 = 581 |
| Target Terror Gold | 3.344 | 209 | 209 | 209 | 187 / 1 |
| Walking Dead | 16.336 | 1021 | 1021 | 320 | 114 / **702** = 816 |
| **Halo/game** | **16.344** | **681** | **681** | **220** | **219 / 462 = 681** |
| **Halo/orig** | **16.344** | **681** | **681** | **220** | **219 / 462 = 681** |
| BBHPro | 3.008 | 188 | 188 | 188 | 179 / 1 |
| BBHProHome | 3.152 | 197 | 197 | 197 | 187 / 1 |

Het verschil met de SPEC-getallen is precies de drie `NOTYPE`-symbolen
(`_Jv_RegisterClasses`, `_ITM_deregisterTMCloneTable`, `_ITM_registerTMCloneTable`) en het
nulsymbool: 99 + 3 = 102 en 258 + 1 = 259.

**Halo's cel is nu ingevuld**: `dynsym` = **681** entries (twee onafhankelijke bevestigingen: de
STRTAB-SYMTAB-deling én de eerste niet-plausibele index, beide exact 681), waarvan **alle 681
`st_shndx = 0`**: **219 met naam** (index 1..219) en **462 met `st_name = 0`** — waarvan het
aaneengesloten blok **220..680 (461 stuks)**, exact beginnend op `DT_HASH.nchain = 220`, plus index 0
(het nulsymbool).

### 2.6 `DT_JMPREL`: naar de PLT-GOT of naar een tweede slottabel?

| bestand | JUMP_SLOTs | `r_offset`-bereik | in `[DT_PLTGOT,+0x4000)` | `r_offset`s die ook `ff 25`-doel zijn | tweede slottabel? |
|---|---|---|---|---|---|
| Galaga | 100 | `0x86f83f8..0x86f8588` | **0** | **0 van 100** | **ja** — 2e tabel, geen stub wijst erheen via `ff 25` |
| PacmanChomp/game | 195 | `0x825518c..0x868e058` | 1 | 1 van 195 | **ja** (1 relocatie toevallig in de GOT) |
| PacmanChomp/orig | 101 | `0x8235ed0..0x83fc588` | 1 | 1 van 101 | **ja** |
| JurassicPark/game | 101 | `0x8add278..0x90e7588` | 1 | 1 van 101 | **ja** |
| JurassicPark/orig | 101 | `0x8add278..0x90e7588` | 1 | 1 van 101 | **ja** |
| Target Terror Gold | 185 | `0x8144c5c..0x8144f3c` | **185** | **185 van 185** | nee |
| Walking Dead | 106 | `0x8a27360..0x924e588` | 6 | 6 van 106 | **ja** |
| **Halo/game** | **178** | **`0x238dc30..0x238e1c8`** | **0** | **178 van 178** | **ja** — 2e tabel, en hier wijzen de stubs er wél naartoe (2e PLT `0x1f5c600..0x1f5d130`) |
| **Halo/orig** | **178** | **`0x238dc30..0x238e1c8`** | **0** | **178 van 178** | idem |
| BBHPro | 177 | `0x80ec11c..0x80ec3dc` | **177** | 177 van 177 | nee (aaneengesloten, 0 gaten) |
| BBHProHome | 185 | `0x80f011c..0x80f03fc` | **185** | 185 van 185 | nee |

De `r_offset`s van de 32-bits gepakte games zijn aaneengesloten met stap 4 (1-2 gaten) en liggen
ver van `DT_PLTGOT`; die van de ongepakte games liggen er juist middenin en zijn perfect
aaneengesloten. Halo gedraagt zich bij de JMPREL-tabel als een **ongepakt** bestand (178 van 178
`r_offset`s zijn een `ff 25`-doel, en er staat een echte 2e PLT met echte indices), maar de
slottabel ligt 12,8 MB van `DT_PLTGOT` vandaan.

### 2.7 `game` versus `game_original`

* **Jurassic Park**: `game` en `game_original` hebben **identieke** `DT_SYMTAB` (`0x806e680`),
  `DT_STRTAB` (`0x80b9bb0`), `DT_PLTGOT`, `DT_JMPREL`, 578 stubs, 577 deadbeef, 108/473 UND. Verschil:
  `game` heeft de structuur-GOT **gebonden** (576 × `0xb7…`), `game_original` **ongebonden**
  (578 × eigen), en `SHNUM` 0 tegen 2, entry `0x813befc` tegen `0x904a8da`. Zoals de SPEC al zegt:
  `game_original` bewaart de ongebonden GOT, niet de namen.
* **PacmanChomp**: zelfde verhaal, maar de bestanden zijn **niet** verder identiek — `game` heeft
  `DT_SYMTAB 0x804ed10` / 4234 entries / 448 stubs / 12 NEEDED, `game_original` `0x804ea88` /
  3924 / 286 / 11 NEEDED. Het zijn dus **twee verschillende builds** of twee verschillende
  patchrondes, niet één bestand in twee fasen.
* **Halo**: `DT_SYMTAB`, `DT_STRTAB`, `DT_PLTGOT`, `DT_JMPREL`, `DT_STRSZ` (`0xca0`), `DT_HASH`,
  681 dynsym-entries, 219/462 UND-splitsing en 178 JMPREL-relocaties zijn **identiek** in beide
  bestanden. Verschillen: entry (`0x414600` vs `0x1f65240`), `SHNUM` (0 vs 2), PT_LOAD-offsets
  (segment 2 op `0x1261000` vs `0x1061890`, vaddr `0x1661000` vs `0x1661890`), de GOT
  (gebonden vs ongebonden), **24 vs 2 deadbeef**, en **611 vs 0 8-byte-stubs**. Het paar is dus het
  dichtst bij "zelfde build, twee fasen" van alle vier.

---

## 3. Correcties op de uitgangspunten van deze opdracht

1. **Target Terror Gold is NIET gepakt.** De eerder gevonden "HASPE-treffer" is de string
   `GetHASPError` — een **dongle-symbool** (samen met `hardlock2hasp_struct`,
   `HaspConvertLptAddrToLptNum`, `LptNumToLptAddr`), **niet** de pakker. De ladermarker
   `PATCHED_WITH_HASPE_LINUX_LOADER_1.1` komt in dit bestand **niet** voor. Het bestand heeft
   `SHNUM = 36`, `niet gestript`, `with debug_info`, `DT_HASH.nchain = 209` = het einde van `dynsym`,
   alleen index 0 naamloos, en een PLT die echte reloc-offsets pusht. Het is dus een **ongepakt**
   lid van de familie. (De HASP-symbolen laten we met rust — "niet van de lader".)
2. **Walking Dead is wél gepakt** en is het grootste 32-bits geval: 807 deadbeef-stubs, 0
   sectiekoppen, ladermarker aanwezig, en een naamloos blok van **701** entries (het hoogste
   naamloos-percentage van allemaal: 86%).
3. **`SHNUM = 0` alleen bewijst niets.** Big Buck Hunter Pro en -Home hebben `SHNUM = 0` én geen
   HASPE-marker, geen deadbeef, `nchain` == tabel-einde, en een perfect normale PLT/GOT. Ze zijn
   dus een goed controlepunt: `SHNUM = 0` is een gevolg van strippen, niet van pakken.

---

## 4. Wat dit betekent voor onze eigen lader

**Is de 64-bits pakking wezenlijk anders? Gedeeltelijk — en het antwoord is niet "ja" of "nee".**

* **In klasse niet.** Halo is geen "native" ELF die toevallig ook een HASPE-string draagt. Van de
  vier merkers van de gepakte familie zijn er vier aanwezig: ladermarker, `SHNUM = 0`,
  GOT-in-het-bestand met **absolute adressen uit een vreemde machine** (`0x7fd0b…`, en op de
  JMPREL-slots `99 × 0x7f…`), en een `dynsym` waarin de pakker **461 naamloze UND-entries**
  aaneengesloten achter de originele tabel plakt, exact op `DT_HASH.nchain`. Ook `DT_JMPREL` wijst
  naar een **tweede** slottabel buiten de `DT_PLTGOT`-regio, met een eigen tweede PLT — hetzelfde
  patroon als Galaga's `0x086f83f8`/`0x8658b10`. Voor de lader betekent dat: **de resolutietabel is
  ook bij Halo niet in het bestand**, want 462 van de 681 imports (67,8%) hebben geen naam. Dat is
  dezelfde blokkade als bij de 32-bits gepakte games, met dezelfde orde van grootte.
* **In mechaniek wél, op twee punten die een lader moet weten.**
  1. **`push 0xdeadbeef` is geen 64-bits merker.** Alleen 24 van de 202 16-byte-stubs van `game`
     (en 2 van de 180 van `game_original`) pushen deadbeef; **177 stubs pushen een echte
     reloc-index (1..177)**, en alle 178 `R_X86_64_JUMP_SLOT`-relocaties hebben een **naam**. Een
     lader die "gepakt" detecteert aan deadbeef, of die aanneemt dat luie binding onmogelijk is,
     oordeelt hier **verkeerd**. Deadbeef is bij 64-bits een *restverschijnsel*, niet het mechanisme.
  2. **De structuurstub heeft bij 64-bits een andere vorm.** In `game` staat een blok van **611
     stubs van 8 byte** (`ff 25 <rel32>; 66 90`) op `0x40d390..0x40e6a8`, met de slots in één
     doorlopend bereik rond `0x1690c20` dat **611 absolute `0x7fd0b…`-adressen** bevat. De 32-bits
     gepakte games hebben **nul** van die 8-byte-stubs. En `game_original` heeft ze **ook niet** —
     daar staat niets van die tabel. Voor de lader betekent dat de 64-bits structuurtabel niet met
     dezelfde scanner te vinden is als de 32-bits (16-byte, deadbeef, `push`+`jmp plt0`), en dat
     `game` en `game_original` **structureel** verschillen, niet alleen in bindingsfase.

**Wat dat concreet betekent voor fase 3** (de offline dumper die per game één JSON schrijft met
stubadres, GOT-slotadres en waarde, `DT_JMPREL` als `(r_offset, symboolindex, naam, type)`, en
`dynsym` per UND-index):

1. **De assertie `aantal stubs == naamloos + met naam` uit de SPEC klopt niet voor Halo.** Voor
   Galaga klopt hij (361 UND vs 357 slots + 4 niet-PLT-symbolen), voor Halo niet: 202 16-byte-stubs
   (waarvan 177 mét naam, 24 deadbeef, 1 push-0) plus **611 8-byte-stubs zonder enige relocatie**,
   tegen 681 UND-symbolen waarvan 462 naamloos. Voor ELF64 moet de dumper de structuurtabel dus
   **apart** detecteren (8-byte `ff 25 … 66 90`-patroon met een doorlopend slotbereik), niet via
   deadbeef en niet via het aantal UND-symbolen.
2. **De dumper moet per game beide tabellen vastleggen**, ook waar ze samenvallen: de
   structuurtabel (stubs → slots → waarde in het bestand) en de `DT_JMPREL`-tabel
   (`r_offset`, symboolindex, naam, type). Bij Halo wijzen ze naar **verschillende** bereiken
   (`0x1690c20` vs `0x238dc30`); bij Galaga net zo (`0x830d008` vs `0x86f83f8`); bij een ongepakt
   bestand vallen ze samen. De dumper moet die twee gevallen van elkaar kunnen onderscheiden — dat
   is één bit aan informatie die nu in de SPEC ontbreekt.
3. **`(DT_STRTAB − DT_SYMTAB) / DT_SYMENT` is de betrouwbare tabelgrens**, niet `DT_HASH.nchain`
   (die is bij elke gepakte game te klein: 4719 vs 4977, 320 vs 1021, 220 vs 681) en niet de
   cap-scan (die loopt door in de volgende data). De dumper moet deze deling als primaire grens
   nemen, met de plausibiliteitstoets als bevestiging, en `nchain` alleen als "waar begint het
   naamloze blok".
4. **De 64-bits route hoeft niet anders gebouwd te worden, maar wel anders gecontroleerd.** Dat
   Halo volgens `packaging/native-games.json` native met beeld start, is met deze meting **niet**
   te rijmen met "gewoon `ld.so`": 462 van de 681 imports hebben geen naam, 99 van de 178
   JMPREL-slots bevatten een adres (`0x7fd0b…`) dat op deze machine niet bestaat, en de
   structuurslots doen dat voor 611 van de 617. Een start "met beeld" moet dus via ElfLdr2's eigen
   resolutiepad (of via zijn ingebakken adressen) gaan. **Gemeten is dat niet** — er is niets
   gestart — maar de twee beweringen (`ELF-LADER-SPEC.md`: gepakt; `native-games.json`: native met
   beeld) zijn met deze getallen alleen te verzoenen als "gepakt bestand, gestart door de lader",
   niet als "gepakt bestand, gestart door de kernel".

---

## 5. Wat open blijft

1. **Herkomst en fase van `game` vs `game_original` bij Halo.** De twee bestanden delen `DT_SYMTAB`,
   `DT_STRTAB`, `DT_HASH`, `DT_PLTGOT`, `DT_JMPREL`, 681 dynsym-entries en 178 relocaties, maar
   `game` heeft 611 8-byte-stubs die `game_original` **volledig mist**. Of die 611 stubs door de
   pakker zijn toegevoegd, of dat `game` een **procesdump** is van een ándere build dan
   `game_original`, is met read-only statische data niet te beslissen. Wat er wél voor pleit dat het
   een dump is: de grootte is **exact 33.554.432 = `0x2000000`** (32 MiB rond), `SHNUM = 0`, en de
   GOT bevat levensechte `0x7fd0b…`-adressen. (Dit is **afgeleid**, niet gemeten.)
2. **De naam `Ill11lll11ll11l`** staat als UND-symbool op index 6 in Halo's `dynsym` (beide
   bestanden) — een verhulde naam die niet op de HASPE-marker lijkt. Of dat een tweede beschermer
   is, is niet onderzocht en valt buiten de lader. (Bewust niet uitgezocht: geen
   integriteits-/donglewerk.)
3. **Waarom Walking Dead 286 van zijn 807 structuurslots niet met een `0xb7…`-adres vult** (520 wel,
   286 "anders", 6 eigen). Dat kan een tweede tabel, een andere adresklasse of half gebonden GOT
   zijn; niet uitgezocht.
4. **De 5 resterende "8-byte-stubs" in Halo/game** (buiten het blok van 611, o.a. op `0xd407c8`) en
   de 4-5 afwijkende doeladressen (`0x167d120`, `0x1821250`, `0x1821258`, `0x18219e0`, `0x1e93460`)
   zijn vermoedelijk code die toevallig op het patroon lijkt; **niet** per stuk geverifieerd.
5. **Of de ingebakken GOT-waarden door ElfLdr2 gebruikt of overschreven worden**, en hoe hij aan de
   naam van een naamloos slot komt — ongewijzigd open (staat ook zo in de SPEC). Deze meting voegt
   daar alleen aan toe dát de 64-bits structuurtabel 611 slots heeft en dat de namen daarvan
   nergens in het bestand staan.
6. **De semantiek van de tweede PLT/tabel bij Halo** (twee PLT's die naar twee verschillende
   slotbereiken wijzen, 12,8 MB uit elkaar) is beschreven maar niet verklaard.

---

## 6. Reproduceerbaarheid

Scripts staan in de Windows-werkmap onder `re/` en zijn op de Deck geüpload als `/home/deck/_packed-elf-*.py`:

| script | wat het meet |
|---|---|
| `re/_packed-elf-a.sh` | bestaan, grootte, `file` |
| `re/_packed-elf-b.sh` | `file`, `readelf -h/-d/-S/-r/-l`, `strings \| grep -i HASPE`, `nm -D` |
| `re/_packed-elf-meet.py` | eerste versie (had een bug op het segmenteinde) |
| `re/_packed-elf-meet2.py` | PLT-dumps, GOT-venster, UND bij 12k/20k/40k, JMPREL vs PLTGOT |
| `re/_packed-elf-meet3.py` | stubclassificatie 16/8 byte, GOT op de stubdoelen, symidx-ondergrens |
| `re/_packed-elf-meet4.py` | dynsym-grens (streng én soepel), 8-byte-stubclusters |
| `re/_packed-elf-meet5.py` | waar het naamloze blok begint t.o.v. `DT_HASH.nchain` |
| `re/_packed-elf-meet6.py` | fijne structuur van Halo's 8-byte-stubtabel (doelen, gaten, waarden) |

Uitvoer: `/home/deck/_packed-elf-b.out`, `_packed-elf-meet{,2,3,4,5}.out`; kopieën in
`re/_b.out`, `re/_meet.out`, `re/_meet2.out`, `re/_meet3.out`, `re/_meet4.out`.

Deze run heeft **geen** game, lader of Wine gestart, **niets** naar de NAS geschreven, en
`ELF-LADER-SPEC.md` niet aangeraakt.
