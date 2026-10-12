# BudgieLoader uitgepakt: wat er in de gepakte ElfLdr2-lader zit

*Statische meting, ronde "fase 1", 2026-10-12. **Er is geen game gestart en BudgieLoader is niet
gestart**; er is uitsluitend in bestanden gelezen, gerekend en gedisassembleerd. Alles op de Deck,
read-only; op de NAS is niets geschreven.*

Aanduiding per bewering: **gemeten** = komt uit een commando dat hieronder staat; **afgeleid** =
interpretatie van die metingen. Waar ik iets niet weet, staat dat er met zoveel woorden.

> **Wat dit bestand toevoegt aan `patches/ELF-LADER-SPEC.md`.** De spec zegt: "De PE van de lader
> zelf openbreken is geen uitweg: die is zelf gepakt (secties `.brr0`–`.brr4`, gecomprimeerde
> strings)." Dat laatste is **juist**, maar de conclusie "geen uitweg" is te sterk. Hieronder staat
> per onderdeel wat er wél leesbaar is (importtabel, exporttabel, IAT, TLS-map, manifest,
> hint/naam-records), wat aantoonbaar dicht zit, en wat een eigen lader precies moet nabouwen.

---

## 1. De bestanden en hun maten

```bash
R="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
file "$R/BudgieLoader.exe" "$R/x64/BudgieLoader_x64.exe"
md5sum "$R/BudgieLoader.exe" "$R/x64/BudgieLoader_x64.exe"
cat "$R/.version"
```

| | `BudgieLoader.exe` | `x64/BudgieLoader_x64.exe` |
|---|---|---|
| klasse | PE32 i386, console | PE32+ x86-64, console |
| grootte | **11.152.552** B | **11.067.048** B |
| md5 | `688b59d7ec5768aed20c7a66a562b218` | `5755209dd2a630d396ba664c3744ac7f` |
| secties | **13** | **15** |
| ImageBase | `0x00400000` | `0x100400000` |
| EntryPoint (RVA) | `0x01014d27` | `0x00c2fea2` |
| SizeOfImage | `0x01295000` | `0x0133c000` |
| TimeDateStamp | **0** | **0** |
| Major/MinorLinkerVersion | **2 / 36** | **2 / 45** |
| Characteristics | `0x032f` | `0x022f` |
| Subsystem / DllCharacteristics | 3 (CUI) / `0x8000` | 3 (CUI) / `0x8000` |

`.version` = `TeknoParrotElfLdr2Core_1.0.0.1434`.

**Gemeten, geen enkele pakker-signatuur.** In beide bestanden is gezocht naar `UPX0/UPX1/UPX2/UPX!`,
`.MPRESS1/2`, `MPRESS`, `ASPack`, `.adata`, `Themida`, `VMProtect`, `.vmp0/1`, `FSG!`, `PECompact`,
`Petite`, `Enigma`, `aPLib`, `LZMA`, `NSIS`, `Nullsoft`, `Inno Setup`, `py2exe`, `_MEIPASS` en
`MZ`/`PE\0\0`. Uitkomst: **geen enkele treffer**. `BRR` (hoofdletters) komt in het 32-bits bestand
**2×** voor, midden in hoog-entropische rommel (context gecontroleerd op `0x567505` en `0xa57f15`) en
in het 64-bits bestand **0×**; dat zijn toevalstreffers, geen marker. De enige `PE\0\0` in het
bestand is de eigen PE-kop. **De pakker is dus niet op naam geïdentificeerd.**

**Afgeleid:** de linker-versies `2.36` en `2.45` zijn de klassieke waarden die **GNU ld** in een
PE-kop zet (binutils 2.36 resp. 2.45); MSVC schrijft daar 12–14. De laders zijn dus met GNU ld/MinGW
gelinkt, en `TimeDateStamp = 0` plus `RELOCS_STRIPPED` (bit 0x0001 in `Characteristics`) zijn
bewerkingssporen van de na-bewerking.

**Waarschuwing bij elke meting hieronder:** de 32-bits `hints.dat` is een **levend bestand**; zie §7.

---

## 2. Sectietabel, volledig en met entropie

```bash
python3 re/bw_sec2.py   "$R/BudgieLoader.exe" "$R/x64/BudgieLoader_x64.exe"   # ruwe koppen
objdump -h "$R/BudgieLoader.exe"; objdump -f "$R/BudgieLoader.exe"
python3 re/bw_pe.py     "$R/BudgieLoader.exe"                                # entropie per sectie
```

Shannon-entropie per sectie is berekend met `bw_pe.py` over de rauwe bytes (`SizeOfRawData` bij
`PointerToRawData`). Let op: `objdump -h` drukt **absolute VMA's** (ImageBase meegerekend) af; de
tabel hieronder geeft de RVA's zoals ze in de sectiekop staan (geverifieerd met `bw_sec2.py`).

### 2.1 32-bits, 13 secties

| # | naam | VirtSize | VirtAddr (RVA) | RawSize | RawPtr | kenmerken | entropie |
|---|---|---|---|---|---|---|---|
| 0 | `.text` | `0x0015be84` | `0x00001000` | **0** | **0** | CODE,IDATA,EXEC,READ | — |
| 1 | `.data` | `0x0003a2f4` | `0x0015d000` | **0** | **0** | IDATA,READ,WRITE | — |
| 2 | `.eh_fram` | `0x0004c958` | `0x00198000` | **0** | **0** | IDATA,READ,WRITE | — |
| 3 | `.bss` | `0x00004f30` | `0x001e5000` | **0** | **0** | UDATA,BSS,READ,WRITE | — |
| 4 | `.edata` | `0x000013d9` | `0x001ea000` | **0** | **0** | IDATA,READ | — |
| 5 | `.idata` | `0x00002c34` | `0x001ec000` | **0** | **0** | IDATA,READ,WRITE | — |
| 6 | `.brr1` | `0x00000818` | `0x001ef000` | **0** | **0** | IDATA,READ | — |
| 7 | `.brr0` | `0x00010750` | `0x001f0000` | **0** | **0** | IDATA,READ | — |
| 8 | `.tls` | `0x00002000` | `0x00201000` | **0** | **0** | IDATA,READ,WRITE | — |
| 9 | `.brr2` | `0x005f063d` | `0x00203000` | **0** | **0** | **CODE,EXEC,READ** | — |
| 10 | `.brr3` | `0x00001680` | `0x007f4000` | `0x00001800` | `0x00000400` | IDATA,READ,WRITE | **2,3544** |
| 11 | `.brr4` | `0x00a9da76` | `0x007f6000` | `0x00a9dc00` | `0x00001c00` | **CODE,EXEC,READ** | **7,9714** |
| 12 | `.rsrc` | `0x00000807` | `0x01294000` | `0x00000a00` | `0x00a9f800` | IDATA,READ | **4,2431** |

Entropie van het hele bestand: **7,9707** bits/byte.

* **Gemeten: 10 van de 13 secties hebben `SizeOfRawData = 0` én `PointerToRawData = 0`.** Hun
  virtuele ruimte is samen **8.317.874 B** (7,93 MiB) en staat dus **niet** in het bestand.
* **Gemeten: `.brr4` is 11.134.464 B rauw = 99,8 % van het bestand**, van `0x1c00` tot `0xaa0200`.
* **Gemeten: `.rsrc` ligt volledig bínnen het rauwe bereik van `.brr4`.** `.rsrc`-rauw begint op
  `0xa9f800` en is 2.560 B; `.brr4`-rauw loopt tot `0xaa0200`. Het manifestsectietje is dus in de
  staart van `.brr4` geplakt.
* **Gemeten: het bestand eindigt met een Authenticode-handtekening, geen onbekende overlay.** De
  `SECURITY`-datadir wijst naar **bestandsoffset `0xaa0200`** (let op: dit veld is een
  bestandsoffset, geen RVA), grootte **10.920**, en `0xaa0200 + 10920 = 0xaa2ca8 =` exact de
  bestandsgrootte. De bytes beginnen met `a8 2a 00 00 | 00 02 | 02 00` = `WIN_CERTIFICATE`,
  `dwLength = 0x2aa8`, revisie `0x0200`, type `0x0002` (PKCS#7 SignedData), gevolgd door een
  DER-`SEQUENCE` `30 82 2a 9a`. Entropie 7,6185. Dit is ondertekening, geen payload.
* Het manifest in `.rsrc` is leesbaar: `requestedExecutionLevel level="asInvoker"` en de
  compatibiliteits-ID's voor Vista t/m Windows 10.

### 2.2 64-bits, 15 secties

| # | naam | VirtSize | VirtAddr (RVA) | RawSize | RawPtr | kenmerken |
|---|---|---|---|---|---|---|
| 0 | `.text` | `0x001727e0` | `0x00001000` | **0** | **0** | **CODE,IDATA,EXEC,READ,WRITE** (`0xe0000020`) |
| 1 | `.data` | `0x0004c8d0` | `0x00174000` | **0** | **0** | IDATA,READ,WRITE |
| 2 | `.rdata` | `0x00000170` | `0x001c1000` | **0** | **0** | IDATA,READ |
| 3 | `.pdata` | `0x0000f0d8` | `0x001c2000` | **0** | **0** | IDATA,READ |
| 4 | `.xdata` | `0x00018ff0` | `0x001d2000` | **0** | **0** | IDATA,READ |
| 5 | `.bss` | `0x000058e0` | `0x001eb000` | **0** | **0** | UDATA,BSS,READ,WRITE |
| 6 | `.edata` | `0x000013fd` | `0x001f1000` | **0** | **0** | IDATA,READ |
| 7 | `.idata` | `0x00003cac` | `0x001f3000` | **0** | **0** | IDATA,READ |
| 8 | `.brr1` | `0x00000818` | `0x001f7000` | **0** | **0** | IDATA,READ |
| 9 | `.brr0` | `0x00004384` | `0x001f8000` | **0** | **0** | IDATA,READ |
| 10 | `.tls` | `0x00002000` | `0x001fd000` | **0** | **0** | IDATA,READ,WRITE |
| 11 | `.brr2` | `0x006b02c4` | `0x001ff000` | **0** | **0** | **CODE,EXEC,READ** |
| 12 | `.brr3` | `0x00001db0` | `0x008b0000` | `0x00001e00` | `0x00000400` | IDATA,READ,WRITE |
| 13 | `.brr4` | `0x00a88684` | `0x008b2000` | `0x00a88800` | `0x00002200` | CODE,IDATA,EXEC,READ,`0x68000060` |
| 14 | `.rsrc` | `0x00000807` | `0x0133b000` | `0x00000a00` | `0x00a8aa00` | IDATA,READ |

* **Gemeten: 12 van de 15 secties zonder bestandsbytes**, samen **9.079.505 B** (8,66 MiB).
* **Gemeten: de secties die erbij zijn t.o.v. 32-bits** zijn `.rdata`, `.pdata`, `.xdata` (de
  klassieke x64-unwind/SEH-secties); de 32-bits lader heeft daarvoor in de plaats `.eh_fram`
  (DWARF). De `.brr0`–`.brr4`-familie is in beide identiek aanwezig, met `.brr1` in beide exact
  `0x818` bytes — dat is dus een vaste kleine sectie, geen toeval.
* **Gemeten: de 64-bits `.text` is `0xe0000020` = `CODE|IDATA|EXEC|READ|WRITE`, dus
  schrijfbaar én uitvoerbaar.** Dat is precies wat een sectie nodig heeft die pas ná het starten
  gevuld wordt. De 32-bits `.text` is `0x60500060` = `ALIGN_16BYTES|CODE|IDATA|EXEC|READ` (niet
  schrijfbaar).
* **Gemeten: de 64-bits `.brr4` heeft `0x68000060`** = `CODE|IDATA|EXEC|READ|NOT_PAGED`
  (`objdump` waarschuwt daar ook over). De 32-bits `.brr4` heeft `0x60000020` = `CODE|EXEC|READ`.
* **Gemeten: ook de 64-bits lader eindigt exact op zijn handtekening**: `SECURITY` =
  bestandsoffset `0xa8b400`, grootte 10.920, `0xa8b400 + 10920 = 0xa8dea8 =` bestandsgrootte.

**Afgeleid (en dit is de kern van §2):** de secties zonder bestandsbytes zijn **decomprimatie- of
ontsmettingsdoelen**: de exporttabel verwijst naar RVA's die in `.text` en `.data` liggen (§5), en
die secties hebben nul bytes in het bestand. De code die de exporttabel belooft, bestaat dus pas
nadat iets `.brr4` heeft uitgeschreven naar `.text`, `.data`, `.eh_fram`, `.edata`, `.idata`,
`.brr0`–`.brr2` en `.tls`.

---

## 3. Waar zit wat: een kaart van het bestand

```bash
python3 re/bw_map.py    "$R/BudgieLoader.exe"     # entropie + printbaarheid per 64 KiB
python3 re/bw_final.py                            # chi-kwadraatklassen per 64 KiB
python3 re/bw_stat.py   "$R/BudgieLoader.exe"     # comprimeerbaarheid, periodiciteit, stride
```

De chi-kwadraattoets hieronder meet hoe ver de bytehistogram van uniform afligt (255 vrijheids­
graden; **255 = precies uniform**). Kalibratie op dezelfde machine, dezelfde code:

| referentie | n | entropie | chi² | zlib-ratio |
|---|---|---|---|---|
| `msys-2.0.dll` (echte x86-code) | 3.362.559 | 6,2664 | **44.594.688** | 0,4528 |
| `zlib(msys-2.0.dll)` | 1.248.364 | 7,9969 | 5.456 | — |
| `lzma(msys-2.0.dll)` | 999.712 | 7,9998 | 261 | — |
| `RC4(stroomcijfer)` | 3.000.000 | 7,9999 | 270 | — |

Tegen die lat ligt het bestand zo:

| zone (32-bits) | n | entropie | chi² | zlib | lzma |
|---|---|---|---|---|---|
| kop `0x0`–`0x1d00` | 7.424 | 2,72 | — | **0,2873** | 0,2866 |
| **RAND `0x40000`–`0x5c0000`** | 5.767.168 | **8,0000** | **236** | **1,0003** | **1,0001** |
| STRUCT `0x5e0000`–`0x660000` | 524.288 | 7,8471 | 92.968 | 0,9687 | 0,9522 |
| hele bestand | 11.152.552 | 7,9707 | 566.154 | — | — |

Verdeling in klassen over alle 64 KiB-vensters:

| klasse | 32-bits | 64-bits |
|---|---|---|
| puur random (chi² < 600) | 6.291.456 B (**56,4 %**) | 5.832.704 B (52,7 %) |
| licht gemengd (600–3.000) | 131.072 B (1,2 %) | 262.144 B (2,4 %) |
| gemengd (3.000–20.000) | 3.157.160 B (28,3 %) | 1.245.184 B (11,3 %) |
| gestructureerd (> 20.000) | 1.572.864 B (14,1 %) | 3.727.016 B (33,7 %) |
| mediaan chi² per venster | 288 | 307 |
| **maximum** | **161.145** (de PE-kop) | **2.122.784** (zie §3.2) |

### 3.1 Wat de getallen wel en niet zeggen

* **Gemeten: de "RAND"-zone van 5,77 MB is statistisch niet te onderscheiden van een stroomcijfer
  of van een ideale coder.** chi² = 236 (uniform verwacht 255), entropie 8,0000, conditionele
  entropie H(X|X−1) = 7,9918 (RC4: 7,9842; lzma: 7,9523), en **incomprimeerbaar** (zlib 1,0003).
  Periodiek zoeken naar een herhalende sleutel met periode 1 t/m 1024 (chi² per restklasse) geeft
  overal 177–328, dus **geen XOR met een vaste periode**. Een stride-toets met stap 1–128 op de
  STRUCT-zone laat chi² exact als 1/k meeschalen — dus **geen interleaving met een vaste stap**.
  **Eerlijk: "versleuteld" en "gecomprimeerd met een bereikcoder" zijn met deze middelen niet van
  elkaar te scheiden.** Wat wél vaststaat is dat het geen platte code en geen platte data is.
* **Gemeten: de STRUCT-zone is een mengsel van ongeveer 21 % code-statistiek en 79 % ruis.** De
  chi² van echte x86-code is 2.132.771 per 256 KiB; de STRUCT-zone haalt 92.968 per 256 KiB;
  √(92.968 / 2.132.771) = **0,209**. Die 21 % klopt met de 7,55 % "laag-entropie-eilanden" die een
  fijnere scan (vensters van 256 B, drempel 6,5) vindt, plus de ruimere definitie.
* **Gemeten: geen enkele eenvoudige omkeerbare bewerking haalt de code terug.** Geprobeerd op
  256 KiB STRUCT: prefix-som, XOR/met +/− met de positie, XOR met de vorige byte (1/2/4/8/16 en
  65536 terug), bitreverse, nibble-swap, `not`. De hoogste chi² die overblijft is 92.968 — precies
  de oorspronkelijke waarde (`not` en de bitpermutaties veranderen de histogramvorm niet); alle
  andere maken het *platter*, niet scherper. **Afgeleid: de 79 % "ruis" is geen transparante
  bytebewerking van de 21 % code.**
* **Gemeten: in het 32-bits bestand is er nergens een blok met code-entropie.** Het "beste" venster
  buiten de kop haalt chi² ≈ 31.322 (op `0x710000`) — een factor 68 onder echte code.

### 3.2 Het enige echte gestructureerde gebied: een datatabel in de 64-bits lader

```bash
python3 re/bw_tab.py     # recordstructuur
python3 re/bw_x64.py     # het gebied zelf uitlezen
```

**Gemeten:** `x64/BudgieLoader_x64.exe`, bestandsoffset **`0x4a0000`–`0x530000`** (589.824 B,
RVA `0xd4fe00`–`0xddfe00`, midden in `.brr4`):
* chi² over het hele gebied **18.237.406**, entropie 5,5330, printbaar aandeel 0,14;
* **geen enkele printbare reeks van 6 tekens of meer** in het hele gebied;
* bytehistogram: `0x00` komt 197.387× voor (33,5 %), daarna `0x08`, `0x10`, `0x18`, `0x20`;
* **98.304 records van 6 bytes** (589.824 / 6, rest 0): elk record = `<u32 adres><u16 lengte>`.
  * Het adres loopt van `0x8c353a00` tot `0xb346a600`, is **altijd een veelvoud van 0x100**
    (98.304/98.304 gecontroleerd), en is **strikt stijgend** (alle 98.303 opeenvolgende paren);
  * de lengte is **altijd een veelvoud van `0x800`** (98.304/98.304 gecontroleerd), van 2.048 tot
    47.104, met 2.048 als meest voorkomende waarde (53.796×);
  * de som van alle lengtes is **354.600 KiB** (≈ 345,8 MiB) over een adresbereik van
    655.453.184 B (625 MiB) — dus geen sluitende kaart maar een gedeeltelijke;
  * **dit is géén code.** Mijn eerste indruk ("576 KB onversleutelde code") was fout; de patronen
    `55 48 89 e5`, `48 89 5c 24`, `48 83 ec` komen **0×** voor.

**Wat het wél is, is niet vastgesteld.** Het is een gesorteerde tabel van (adres, omvang) met
adressen net onder het klassieke 32-bits `0xb7…`-gebied. Ik noem het bewust geen "geheugenkaart" —
daarvoor ontbreekt de sleutel tot de semantiek. In het 32-bits bestand bestaat geen vergelijkbaar
gebied; dat is een **gemeten asymmetrie tussen de twee laders**.

---

## 4. Het startpad: entrypoint en TLS

```bash
python3 re/bw_tls.py                                       # datadir, TLS-map, callbacks
objdump -D -b pei-i386 -m i386 --start-address=0x1414d27 \
        --stop-address=0x1414db0 "$R/BudgieLoader.exe"
```

(Let op: `objdump` neemt **absolute** adressen; `0x1414d27 = 0x400000 + 0x1014d27`.)

### 4.1 32-bits: entrypoint midden in `.brr4`, geen TLS-callbacks

**Gemeten:** EP RVA `0x1014d27` → bestandsoffset `0x820927`, in `.brr4`. De eerste 25 instructie­
bytes ontleden **coherent**, maar zijn duidelijk bewerkt (equivalent-vervanging, verborgen
adresrekenwerk):

```
1414d27: e8 e5 15 f3 ff        call   0x1346311        ; niet $+5 — geen pop volgt
1414d2c: 81 ef 04 00 00 00     sub    $0x4,%edi
1414d32: ba 88 b6 35 46        mov    $0x4635b688,%edx
1414d37: 8b 94 17 78 49 ca b9  mov    -0x4635b688(%edi,%edx,1),%edx   ; == mov edx,[edi]
1414d3e: 33 d3                 xor    %ebx,%edx
1414d40: b9 39 25 09 4d        mov    $0x4d092539,%ecx
1414d45: 8d 94 0a 1f 03 52 9e  lea    -0x61adfce1(%edx,%ecx,1),%edx
1414d4c: 8b c1                 mov    %ecx,%eax
1414d4e: f7 da                 neg    %edx
1414d50: fe c9                 dec    %cl
1414d52: 42                    inc    %edx
1414d53: f7 d8                 neg    %eax
1414d55: f7 d1                 not    %ecx
1414d57: 8d 8c 89 84 23 ae b2  lea    -0x4d51dc7c(%ecx,%ecx,4),%ecx
1414d5e: f7 da                 neg    %edx
1414d60: e9 38 a9 1a 00        jmp    0x15bf69d        ; RVA 0x11bf69d, ook in .brr4
```

`mov edx,0x4635b688` gevolgd door `mov edx,[edi+edx-0x4635b688]` is een verhulde `mov edx,[edi]`:
een klassieke "obfuscator met equivalente instructies". **De 32 bytes vóór het entrypoint ontleden
als onzin** (`43 61 70 72 60 45 53 f0 7c 60 e1 a0 …`), en vanaf `0x1414d65` — ná de sprong — staat
weer onzin. **Afgeleid:** het entrypoint ligt midden in een ondoorzichtige regio, met dode
"kandidaat-stubs" ernaast.

**Gemeten: de 32-bits TLS-map heeft GEEN callbacks.**
`TLS` rva `0x102e0e4` (bestand `0x839ce4`), `StartRawData=0xbf4668`, `EndRawData=0xbf5667`
(4.096 B, RVA `0xbf4668` — **in `.brr4`**), `AddressOfIndex=0x601018` (in `.tls`),
**`AddressOfCallBacks = 0x0`**, `ZeroFill=0`, `Characteristics=0x300000`.

### 4.2 64-bits: entrypoint onzin, maar TWEE TLS-callbacks

**Gemeten:** EP RVA `0xc2fea2` → bestand `0x3800a2`, in `.brr4`. De eerste bytes `e8 89 a2 f6 ff`
(`call`) worden onmiddellijk gevolgd door onontleedbare bytes; er is geen coherente stub zoals bij
32-bits.

**Gemeten: de 64-bits TLS-map wél.**
`TLS` rva `0xd34f78` (bestand `0x485178`), `StartRawData=0x100cb0d80`, `EndRawData=0x100cb1d7f`,
`AddressOfIndex=0x1005fd030` (in `.tls`), **`AddressOfCallBacks=0x101134fa0`**, en de lijst bevat:

| callback | absolute VMA | RVA | waar die RVA ligt |
|---|---|---|---|
| 0 | `0x101133198` | **`0xd33198`** | in `.brr4` (bestand `0x483398`) |
| 1 | `0x100403330` | **`0x3330`** | in `.text` — **een sectie zonder bestandsbytes** |

**Afgeleid, en dit is een echt aanknopingspunt:** de 64-bits lader start via **TLS-callback 0**, en
de oorspronkelijke programmatuur had een **eigen TLS-callback op RVA `0x3330`** die pas ná het
uitpakken in `.text` kan bestaan. Wie de 64-bits lader wil narekenen moet dus de TLS-keten
afwikkelen, niet alleen naar `AddressOfEntryPoint` springen. De 32-bits lader heeft die route niet.

---

## 5. De importtabel: volledig leesbaar, 403 en 423 invoeren

```bash
objdump -p "$R/BudgieLoader.exe"       | grep -A40 'DLL Name'
python3 re/bw_final2.py                # tellen per DLL + IAT-omvang
python3 re/bw_iat.py                   # IAT-waarden tegen de hint/naam-records
```

**Gemeten: de import-descriptors, de ILT, de hint/naam-records en de IAT staan allemaal
onversleuteld in `.brr4` en `.brr3`.** Alleen de *plaats* is verstrooid: de hint/naam-records van
één DLL liggen niet aaneengesloten maar verspreid over de hele 10,9 MB. Voor `msys-2.0.dll` lopen de
RVA's van `0x7f8912` tot `0x1291a00` — **57,0 % van het hele beeld** — en ze zijn **niet oplopend**.
Dat is gemeten voor alle zeven/negen DLL's (behalve `VERSION.dll` in 32-bits, die toevallig wél
oplopend is).

| DLL | 32-bits invoeren | 64-bits invoeren |
|---|---|---|
| `msys-2.0.dll` | **277** | **245 + 1** (twee descriptoren) |
| `ADVAPI32.dll` | 5 | 5 |
| `GDI32.dll` | 8 | 8 |
| `KERNEL32.dll` | **41** | **72** |
| `USER32.dll` | 66 | 65 |
| `VERSION.dll` | 3 | 3 |
| `WINMM.DLL` | 3 | 3 |
| `WS2_32.dll` | — | **21** |
| **totaal** | **403 in 7 DLL's** | **423 in 9 DLL's** |

De eerdere meting in de spec ("alleen `ADVAPI32/GDI32/KERNEL32/USER32/VERSION/WINMM` plus
`msys-2.0.dll`, en in x64 ook `WS2_32.dll`") is dus **bevestigd**, en nu met aantallen.

**Gemeten: er IS een dynamische-resolutie-route.** `KERNEL32.dll` levert `LoadLibraryA`,
`LoadLibraryExA`, `GetProcAddress`, `FreeLibrary`, `SetDllDirectoryW` en `GetModuleHandleA/W`,
`GetModuleHandleExW`. Dat is precies de gereedschapskist van een lader die gastbibliotheken zelf
moet openen en symbolen zelf moet opzoeken. Verder valt op:
* `USER32`: `ChangeDisplaySettingsA`, `ChangeDisplaySettingsExA`, `EnumDisplaySettingsA`,
  `EnumDisplayMonitors`, `GetSystemMetrics`, `MonitorFromWindow` — het volledige scherm­modus-pad;
* `GDI32`: `ChoosePixelFormat`, `DescribePixelFormat`, `GetPixelFormat`, `SetPixelFormat`,
  `SwapBuffers` — de WGL-pixelformaatroute;
* `USER32`: `PeekMessageA/W`, `SendMessageA`, `SetCapture`, `GetKeyState`, `ToAscii` — de
  event-lus; `VERSION`: de versiequery's; `WINMM`: `timeGetTime`/`timeBeginPeriod`.

### 5.1 De IAT in `.brr3`

**Gemeten:** IAT-datadir 32-bits = RVA `0x7f4000`, grootte 1.640 B = **410 slots van 4 bytes**, en
`0x7f4000` is exact het begin van sectie **`.brr3`**. 64-bits: RVA `0x8b0000`, grootte 3.456 B =
**432 slots van 8 bytes**, exact het begin van `.brr3`.

**Gemeten: elke IAT-slot bevat een RVA naar een hint/naam-record in `.brr4`.** De eerste acht
32-bits waarden zijn `0xe52fa8, 0x113dc56, 0x827034, 0xf92e64, 0xff0e68, 0xddbbfe, 0x10e5ee4,
0x127e342` — volledig verstrooid. De eerste is te controleren: `0xe52fa8` hoort bij het
hint/naam-record waarvan de ASCIIZ-naam op `0xe52faa` begint (2 bytes `Hint` vooraf), en daar staat
letterlijk **`__cxa_atexit`**. Dit is dus een normale *ongebonden* IAT die de Windows-loader zelf
zou vullen; er is niets geheimzinnigs aan.

**Gemeten: `.brr3` is groter dan de IAT.** 32-bits virtueel `0x1680`, rauw `0x1800`, IAT gebruikt
1.640 B; de rest (1.072 B virtueel, ~1.300 B rauw) is nog niet geïdentificeerd. 64-bits: virtueel
`0x1db0`, rauw `0x1e00`, IAT 3.456 B.

---

## 6. De exporttabel: 173 en 174 namen, en de volledige groepering

```bash
python3 re/bw_final.py            # de groepering hieronder
python3 re/bw_dirs.sh             # dumpt de volledige lijst naar /home/deck/bw/exports32.txt
```

**Dit is het API-oppervlak dat een eigen lader moet leveren.** De DLL-naam in de exportmap is
`BudgieLoader.exe` respectievelijk `BudgieLoader_x64.exe`, ordinal base 1, en **alle 173/174 namen
hebben een eigen naam** (geen ordinal-only). De adressen in de exporttabel liggen allemaal in
`.text`/`.data` — dus in secties **zonder bestandsbytes** (§2), wat nog eens bevestigt dat die
secties na het uitpakken gevuld worden.

### 6.1 Groepering

| groep | 32-bits | 64-bits | namen |
|---|---|---|---|
| **eigen lader-API** | 6 | 6 | `GetElfBase`, `GetElfSymbol`, `GetMainInterfaceIP`, `GetMainInterfaceSubnetMask`, `TpRefreshMainInterface`, `get_info` |
| **data-exports (variabelen)** | 3 | 3 | `AmdPowerXpressRequestHighPerformance` (.data `0x15d020`), `NvOptimusEnablement` (.data `0x15d024`), `api` (.data `0x15d028`) |
| **glibc-locale** | 3 | 3 | `_Z13__ctype_b_locv` (`__ctype_b_loc`), `_Z19__ctype_tolower_locv`, `_Z19__ctype_toupper_locv` |
| **freeglut-API** | **161** | **161** | alle `glut…` (zie 6.2), waarvan **31 met `Ucall`-achtervoegsel** |
| **overig** | 0 | **1** | `ElfPreReserveCallback` — **alleen in de 64-bits lader** |
| totaal | 173 | 174 | |

De adressen van de eigen API (32-bits): `get_info` `0x4280`, `GetElfBase` `0x4430`, `GetElfSymbol`
`0x4540`, `TpRefreshMainInterface` `0x4860`, `GetMainInterfaceIP` `0x4940`,
`GetMainInterfaceSubnetMask` `0x4a10`; de `__ctype_*` op `0x2b4c0`/`0x2b4e0`/`0x2b4f0`; de
freeglut-adressen van `0x30930` (`glutIdleFunc`) tot `0x3e9d0` (`glutSetWindowData`).

### 6.2 De freeglut-groep, in het kort

**Gemeten:** de volledige freeglut-API is aanwezig, inclusief de niet-standaard uitbreidingen die
freeglut zelf niet heeft. Naast het gewone werk (`glutInit`, `glutCreateWindow`, `glutMainLoop`,
`glutSwapBuffers`, `glutSolid*`/`glutWire*`, `glutBitmap*`, `glutStroke*`, `glutFullScreen`,
`glutGameMode*`) staan er:
* **31 `*Ucall`-varianten**, bijvoorbeeld `glutDisplayFuncUcall`, `glutKeyboardFuncUcall`,
  `glutMouseFuncUcall`, `glutTimerFuncUcall`, `glutCloseFuncUcall`, `glutWindowStatusFuncUcall`;
  per callback bestaat dus een gewone én een "Ucall"-versie;
* **callback-achtige uitbreidingen die freeglut niet kent**: `glutPositionFunc`,
  `glutWindowStatusFunc`, `glutAppStatusFunc`, `glutInitContextFunc`, `glutEntryFunc`,
  `glutButtonBoxFunc`, `glutDialsFunc`, `glutTabletMotionFunc`, `glutTabletButtonFunc`,
  `glutMultiEntryFunc`, `glutMultiButtonFunc`, `glutMultiMotionFunc`, `glutMultiPassiveFunc`,
  `glutMouseWheelFunc`, `glutKeyboardUpFunc`, `glutSpecialUpFunc`, `glutWMCloseFunc`,
  `glutOverlayDisplayFunc`;
* `glutSetVertexAttribCoord3`, `glutSetVertexAttribNormal`, `glutSetVertexAttribTexCoord2`,
  `glutSetupVideoResizing`, `glutVideoResize`, `glutVideoPan`, `glutVideoResizeGet`,
  `glutStrokeLengthf`, `glutStrokeWidthf`, `glutGetWindowData`/`glutSetWindowData`,
  `glutGetProcAddress`, `glutLeaveMainLoop`, `glutMainLoopEvent`;
* `glutExit` en `glutFullScreenToggle`.

**Afgeleid:** dit is geen freeglut maar een *eigen* GLUT-implementatie met de freeglut-naamgeving
erbovenop — precies wat je doet als je `libglut.so.3` (dat de games vragen en dat **niet** in
`ElfLdr2/libs/` staat, §8) binnen de lader zelf wilt leveren.

---

## 7. `hints.dat`: structuur, groei, en wat de sleutel niet is

```bash
cp -p "$R/hints.dat" /tmp/h32; cp -p "$R/x64/hints.dat" /tmp/h64   # eerst een kopie!
python3 re/bw_hint2.py
python3 re/bw_hintkey.py
```

### 7.1 Maten — en een waarschuwing

| bestand | grootte | record | records | mtime | sha256 (afgekapt) |
|---|---|---|---|---|---|
| `ElfLdr2/hints.dat` | **2.994.888** B | 12 | **249.574** | **2026-10-12 18:25:18** | `c55574902a80a554…` |
| `ElfLdr2/x64/hints.dat` | **7.325.952** B | 16 | **457.872** | 2026-10-12 01:38:47 | `d6343aa5453a5ea3…` |

**Gemeten: de 32-bits `hints.dat` groeit.** De spec noteerde 235.414 records; vandaag zijn het er
**249.574**, dus **+14.160 records = +169.920 B**, en de mtime is van vandaag 18:25. De 64-bits
`hints.dat` staat met 457.872 records nog precies op de oude waarde. **Afgeleid:** de lader schrijft
`hints.dat` bij tijdens het laden. **Dit bestand is dus geen statische tabel en mag nooit als
"de" waarheid worden ingelezen** — het is een cache die per ElfLdr2-sessie aangroeit. (Ik heb voor
alle metingen een `cp -p`-kopie gemaakt en op de kopie gerekend.)

### 7.2 De recordstructuur, precies

**Gemeten (32-bits, 12-byte records):** het record is **(u32 A, u32 B, u32 C)** en het is zinvol om
(A,B) als één **64-bits sleutel** te lezen en C als **gastadres**:

* **13 distincte sleutels** in het hele bestand. De grootste vier:
  `c8ec53491776d9fc` (112.458 records), `349bc9ac1e12f6f5` (49.580), `5057251d3f1c979c` (27.168),
  `5f94d2ff8385ea59` (22.268);
* **C is in 100,0 % van de 249.574 records een gastadres in `0x080780c3`–`0x0896d7e7`** — het
  `0x08xxxxxx`-beeld waar de gepakte games wonen. Geen enkele waarde ≥ `0x80000000`, geen enkele
  < `0x1000`;
* het bestand bestaat uit **1.596 aaneengesloten runs** van records met dezelfde sleutel, en
  **alle 1.596 runs hebben strikt oplopende, unieke adressen** (1.596/1.596);
* per sleutel: 1603, 662, 420, 319, 137, 143, 114, 63, 57, 32, 30, 11 en **2** unieke adressen
  (samen **3.593 distincte (sleutel,adres)-paren** op 249.574 records);
* de eerste adressen van de dertien sleutels liggen allemaal in `0x0813bb9d`–`0x0829316c`, dus in
  **één smalle strook van ~1,4 MB** — terwijl de bereiken per sleutel juist over de hele 9,4 MB
  heen lopen en elkaar zwaar overlappen (`0x80780c3`–`0x896d7e7`). **Afgeleid: de sleutels
  partitioneren de adressen níet ruimtelijk maar naar herkomst.**

**Gemeten (64-bits, 16-byte records):** het record is **(u64 sleutel, u64 adres)**, met het hoge
halfwoord van het adres **altijd 0** (want het beeld staat op `0x400000`):

* **15 distincte sleutels**; grootste `b29f8faf5eed6d90` (214.560 records);
* adressen `0x0040e709`–`0x01130e7d` (**9.539 distincte paren**);
* **720 runs, alle 720 oplopend en uniek**;
* **exact 48 keer hetzelfde patroon**: de eerste adressen van alle vijftien sleutels komen elk
  precies **48×** voor, en 720/15 = 48. Het 64-bits bestand is dus **48 identieke snapshots** achter
  elkaar; 457.872 / 9.539 = 48,0.

**Gemeten, 32-bits:** de eerste adressen komen **101 of 102 keer** voor (niet 48), en
249.574 / 3.593 = 69,5 — de verhoudingen lopen per sleutel uiteen (112.458 / 1.603 = 70,2 maar
202 / 2 = 101). **Afgeleid:** er worden per sessie alleen snapshots toegevoegd voor de sleutels die
die sessie gebruikte.

### 7.3 Is dit de sleutel tot de gewiste importnamen? — nee, en wel hierom

De hoop was dat `hints.dat` een naam→adres-tabel is. **Gemeten: dat is het niet.** Een
naam→adres-tabel zou honderden tot duizenden distincte sleutels hebben; er zijn er **13 (32-bits)
en 15 (64-bits)**.

**Gemeten: de sleutel is deterministisch maar niet herleid.** Dezelfde sleutel komt 101
respectievelijk 48 keer terug, dus het is geen toevalswaarde, maar een vaste functie van iets.
Ik heb **1.463 toetsen** gedaan: 11 hashfuncties (md5[:8], md5[-8:], sha1[:8], sha256[:8], FNV-1a-64,
FNV-1-64, djb2-64, sdbm-64, crc32‖lengte, lengte‖crc32, crc32‖crc32-van-1-byte-korter) × 133
kandidaten (de namen `libc.so.6`, `libm.so.6`, `libpthread.so.0`, `libGL.so.1`, `libGLU.so.1`,
`libglut.so.3`, `libSDL-1.2.so.0`, `libfmod.so.7`, `game`, `BudgieLoader.exe`, … én de volledige
inhoud van alle 96 bestanden in `libs/` en `x64/libs/`). **Nul treffers.** De sleutel is dus een
64-bits waarde waarvan de herkomst niet is vastgesteld.

**Wat `hints.dat` wél oplevert, gemeten:** per sleutel een **gesorteerde lijst van gastadressen in
het spelbeeld**, die per sessie opnieuw wordt toegevoegd. Daarmee is een *groepering* van adressen
gegeven (13 respectievelijk 15 groepen), maar **geen naam en geen bibliotheekidentiteit**. Voor de
gepakte games uit `ELF-LADER-SPEC.md` — waarvan 72–82 % van de importnamen gewist is en de
slot→naam-tabel niet in het bestand zit — is dit dus **geen oplossing, maar wél een extra
beperking**: het aantal groepen (13/15) is een harde bovengrens aan het aantal te onderscheiden
herkomsten.

**En één toets die een specifieke hoop wegneemt:** de "tweede PLT" `0x08658b10` uit de spec komt in
`hints.dat` **0×** voor (en in `0x8658000`–`0x8659000` ook 0×). De hintadressen zijn dus niet die
slottabel.

---

## 8. De nagebootste bibliotheken: `libs/`, `x64/libs/`, `env/`

```bash
ls -la "$R/libs" "$R/x64/libs" "$R/env/dev"
for f in "$R"/libs/*; do file -b "$f"; done            # en idem voor x64/libs
bash re/bw_libs2.sh                                    # klasse + aantal dyn. symbolen per bestand
ldconfig -p | grep -E 'libglut|libXrandr|libiw|libSDL-1.2|libesd|libfmod'
```

**Gemeten:** `libs/` bevat **77 bestanden: 55 echte 32-bits Linux-ELF's en 22 PE32-DLL's**.
`x64/libs/` bevat **19 bestanden: 13 echte 64-bits Linux-ELF's en 6 PE32+-DLL's**. Het is dus géén
map met "nagebootste" bibliotheken maar een **elleboogmap met echte derde-partij­bibliotheken van
beide soorten**, met `.so`-namen die de games vragen.

Voorbeelden van de ELF's in `libs/` (naam, grootte, klasse, aantal gedefinieerde dynamische
symbolen, SONAME): `libGLU.so.1` 671.072, `libtheora.so.0` 123.492, `libPhysXLoader.so.1` 40.832,
`libPhysXCore.so` 2.883.308, `libNxCharacter.so.1` 48.524, `libNxCooking.so` 462.476,
`liballeg.so.4.0` 761.380, `libNL.so.1.6` 72.392, `libstdc++.so.5` 4.274.059,
`libstdc++.so.6.0.7/9/10/14/15/16` (zes versies!), `libcsv.so` 10.266, `libfmod.so.7` 1.555.296,
`libfmodstudio.so.7` 2.078.616, `libavcodec.so.51` 8.449.052, `libboost_*` (negen bestanden),
`libcrypto.so.1.0.0`, `libssl.so.1.0.0`, `libsqlite3.so.0`,
`libpng12.so.0`, `libpng15.so.15` (FreeBSD-klasse!), `libz.so.6` (FreeBSD-klasse),
`libkswapapi.so`, `libarcaderegistry.so.1`, `libaoss.so.0.0.0`, `libalsatoss.so.0.0.0`,
`libvorbis*`, `libogg.so.0`, `libgsm.so.1`, `libmp3lame.so.0`, `libfaac.so.0`, `libfaad.so.0`,
`libx264.so.68`, `libxvidcore.so.4`, `libpostproc.so.0`, `libpq.so.3` (PostgreSQL!),
`libnsl.so.1`, `libresolv.so.2`, `libcrypt.so.1`.

PE's in `libs/`: `TPGfx.dll` 1.167.872, `cg.dll`/`cgGL.dll` (NVIDIA Cg), `openal32.dll` 845.045,
`openal32gsevo.dll` 109.144, `regal32.dll` 4.657.664, `SDL.dll`, `SDL_image/mixer/net/ttf.dll`,
`lua5.1.dll`, `tcl83.dll`, `zlib1.dll`, `libfreetype-6.dll`, `portaudio_x86.dll`,
`libasound.dll`, `libWebcam.dll`, `bnusCore_win.dll`, `nuSound2_win.dll`, `OpenSndGaelco.dll`,
`Opensegaapi.dll`.

`x64/libs/`: `libGLU.so.1` 453.352, `libblas.so.3`, `libc.so.6` 2.030.928, `libdl.so.2`,
`libgcc_s.so.1`, `libgfortran.so.4`, `liblapack.so.3` 6.936.320, `libm.so.6`, `libpthread.so.0`,
`libquadmath.so.0`, `librt.so.1`, `libstdc++.so.6.0.25`, `libudev.so.1`, plus de PE's `SDL2.dll`,
`SDL2_mixer.dll`, `fmod64.dll`, `fmodstudio64.dll`, `tpgfx_x64.dll`, `vpx.dll`.

**Gemeten: niets daarvan is een dummy.** Alle onderzochte ELF's hebben een normale `SONAME`, een
normale `DT_NEEDED`-lijst en honderden tot duizenden gedefinieerde dynamische symbolen
(`libstdc++.so.6.0.25` 5.411, `liblapack.so.3` 1.869, `libm.so.6` 1.025, `libpthread.so.0` 253,
`libquadmath.so.0` 94, `libudev.so.1` 95, `librt.so.1` 52). Sommige zijn niet gestript en hebben
`with debug_info`. Er is in deze map dus **geen enkele "nagebootste" bibliotheek aangetroffen**; de
nabootsing zit in de lader zelf (de 161 freeglut-exports).

**Gemeten: wat de games vragen en wat er níet is** (`ldconfig -p` erbij voor de host):

| gevraagd bestand | in `libs/` | in `x64/libs/` | op de host |
|---|---|---|---|
| `libglut.so.3` | **nee** | **nee** | ja |
| `libSDL-1.2.so.0` | **nee** | **nee** | **nee** |
| `libXrandr.so.2` | **nee** | **nee** | ja |
| `libesd.so.0` | **nee** | **nee** | **nee** |
| `libfmod.so.10` | **nee** | **nee** | **nee** |
| `libfmodstudio.so.10` | **nee** | **nee** | **nee** |
| `libiw.so.30` | **nee** | **nee** | ja |

(Er staat wél `SDL.dll` — de **Windows**-versie — maar geen `libSDL-1.2.so.0`; en `libfmod.so.7` /
`libfmodstudio.so.7`, niet de `.10`-versies.)

**Gemeten: `env/dev/`** bevat vier gewone bestanden, geen apparaatknopen:
`fd` (40 B), `stderr` (44 B), `stdin` (44 B), `stdout` (44 B), alle van 15 dec 2022. Ze zijn te
klein om een echte `/proc/self/fd`-inhoud te zijn. **Afgeleid:** een nagebootste minimale
`/dev`-omgeving die de gast ziet als hij `/dev/...` opent.

---

## 9. Kan het statisch uitgepakt worden? En zo niet, wat is dan de goedkoopste weg?

**Antwoord: statisch uitpakken is niet gelukt, en met de middelen hier is er ook geen aanwijzing dat
het zonder uitvoering gaat lukken.** Dat is geen vaag "moeilijk", het is meetbaar:

1. **De grote massa is aantoonbaar incomprimeerbaar** (§3.1): 5,77 MB met zlib 1,0003 en lzma
   1,0001, chi² 236. Dit is de kern van "geen statische uitpak".
2. **Er is geen enkel herkenbaar archief- of stroomformaat.** Gemeten magics: `PK\x03\x04` 0×,
   `7z` 0×, `Rar!` 0×, `MSCF` (CAB) 0×, `BZh` 0×, `\xfd7zXZ` 0×, zstd 0×, `\x1f\x8b\x08` 0×,
   `\xd0\xcf\x11\xe0` 0×. De enige treffers zijn `PE\0\0` (de eigen kop) en één toevallige
   `5d 00 00` op `0xa4fc07`.
3. **Geen vaste periode, geen vaste stap, geen omkeerbare bytebewerking** (§3.1) — dus ook geen
   "even de XOR-sleutel zoeken".
4. **Geen TLS-callback in 32-bits en een entrypoint dat midden in de rommel ligt** (§4): er is geen
   plek in het bestand waar de decompressor als *leesbare code* begint, behalve de eerste ~25
   bytes bij het entrypoint, en die springen direct door naar weer rommel.

**De goedkoopste weg om het tóch te doen, in volgorde van kosten:**

* **Weg A — de lader één keer laten lopen en zijn eigen geheugen dumpen.** Dit is verreweg het
  goedkoopst en het is *niet* wat deze opdracht toestaat (het starten van de lader is verboden; de
  bench is van een andere agent). Nodig: BudgieLoader één keer starten onder Wine met een game of
  zonder, en op het moment dat het uitpakken klaar is het procesbeeld van `0x400000`+ naar schijf
  schrijven (`/proc/<pid>/mem` of een minidump). Dat levert `.text`, `.data`, `.eh_fram`,
  `.edata`, `.idata`, `.brr0`–`.brr2` en `.tls` **kant-en-klaar** op — precies de 8,32 MB die in
  het bestand ontbreken. Randvoorwaarde: één game tegelijk en de afspraak dat de bench-agent
  eigenaar is van die runs.
* **Weg B — de decompressiestub in een eigen harness uitvoeren.** Haalbaar in principe: het
  entrypoint is bekend (`0x1414d27`), de code is 32-bits i386, en het bestand heeft
  `RELOCS_STRIPPED` met een vaste ImageBase, dus een harness kan het `.brr4`-blok op `0x7f6000`
  mappen, `.text`–`.brr2` als nul-gevulde regio's op hun RVA's aanmaken, en vanaf het entrypoint
  beginnen. **Maar:** (a) de stub is zelf bewerkt en springt naar `0x15bf69d`, dus een emulator of
  een echte i386-uitvoering is nodig, niet alleen een disassembler; (b) de stub leest ongetwijfeld
  `GetProcAddress`/`LoadLibrary`-resultaten en `fs:`-TLS, dus de harness moet een minimale
  Win32-omgeving leveren; (c) in de 64-bits lader moet bovendien **TLS-callback 0 op RVA
  `0xd33198`** draaien (§4.2). Dit is een echt project van dagen, niet een script van een uur.
* **Weg C — een instrumenterende emulator (QEMU-user met een PE-loader, of Unicorn) op het
  entrypoint.** Duurder dan B, geen voordeel.
* **Weg D — geheugenforensica op een bestaande dump.** Er is in het verleden al een proceskaart
  gemeten (`docs/rt-glfix.md`); als daar een volledige dump of een `/proc/<pid>/maps`+`mem`-paar
  van een ElfLdr2-run bestaat, is dat een derde weg naar dezelfde 8,32 MB. **Niet onderzocht in
  deze ronde** — ik heb geen dump gezocht.

**Wat ik in elk geval niet heb gedaan en niet kon doen:** de pakker op naam identificeren via
literatuur. `web_search` is in deze sessie niet beschikbaar (de zoeksleutel ontbreekt:
*"DeepSeek search has no API key"*), dus de vergelijking met gepubliceerde pakkers blijft beperkt
tot de byte-signaturen uit §1. **Dat is een echte beperking van deze ronde.**

---

## 10. Wat een eigen lader exact moet nabouwen

Alles hieronder is afgeleid uit wat hier gemeten is plus wat al in `ELF-LADER-SPEC.md` en
`elfldr2-injectie.md` stond; per punt staat waar het vandaan komt.

**(A) De PE-kant — het eigen proces**

1. **PE laden met vaste ImageBase.** `RELOCS_STRIPPED` staat in `Characteristics` van beide
   bestanden, dus een eigen lader moet op `0x00400000` (32-bits) respectievelijk `0x100400000`
   (64-bits) laden en mag niet herbaseren.
2. **Twaalf (32-bits: tien) secties als virtuele ruimte aanmaken en met nul vullen**, want
   `SizeOfRawData = 0`: 32-bits `.text` `0x1000`, `.data` `0x15d000`, `.eh_fram` `0x198000`,
   `.bss` `0x1e5000`, `.edata` `0x1ea000`, `.idata` `0x1ec000`, `.brr1` `0x1ef000`, `.brr0`
   `0x1f0000`, `.tls` `0x201000`, `.brr2` `0x203000`; 64-bits idem plus `.rdata` `0x1c1000`,
   `.pdata` `0x1c2000`, `.xdata` `0x1d2000`.
3. **`.brr3` (de IAT), `.brr4` en `.rsrc` uit het bestand mappen** — dat zijn de enige secties met
   bytes.
4. **`.rsrc` overlapt `.brr4`**: `.rsrc`-rauw (`0xa9f800`, 2.560 B) ligt volledig binnen het
   `.brr4`-bereik. Wie de secties naïef kopieert, kopieert die 2.560 bytes twee keer.
5. **De Authenticode-handtekening (10.920 B) overslaan als code** — het is de `SECURITY`-datadir,
   een bestandsoffset, geen RVA.
6. **De TLS-map opzetten.** 32-bits: `StartRawData=0xbf4668`, `AddressOfIndex=0x601018`,
   **geen `AddressOfCallBacks`**. 64-bits: `StartRawData=0x100cb0d80`, `AddressOfIndex=0x1005fd030`,
   **`AddressOfCallBacks` met twee callbacks**: `0xd33198` (in `.brr4`) en `0x3330` (in `.text`).
   De Windows-loader draait TLS-callbacks vóór het entrypoint; een eigen lader moet dat ook doen,
   en callback 1 is pas geldig nadat `.text` gevuld is.
7. **De IAT vullen.** 410 slots (32-bits) / 432 slots (64-bits) in `.brr3`, elk met een RVA naar een
   hint/naam-record; de naam begint op `RVA + 2`. Op te lossen tegen de **403** respectievelijk
   **423** importinvoeren uit 7 respectievelijk 9 DLL's (§5). De lader mag daar zijn eigen
   `LoadLibraryA`/`GetProcAddress` voor gebruiken (die zitten in zijn eigen importtabel).
8. **Uitpakken/ontsmetten van `.brr4` naar de tien/twaalf lege secties.** Dit is het enige
   onderdeel waarvan ik de methode **niet** ken (§9).

**(B) De exportkant — het contract met de gast**

9. **Precies deze 173 (32-bits) / 174 (64-bits) exportnamen leveren**, want de gast-ELF lost zijn
   importen op tegen de exporttabel van de lader (zoals `ELF-LADER-SPEC.md` al afleidde):
   * de **6 eigen functies**: `GetElfBase`, `GetElfSymbol`, `GetMainInterfaceIP`,
     `GetMainInterfaceSubnetMask`, `TpRefreshMainInterface`, `get_info`;
   * de **3 data-exports**: `AmdPowerXpressRequestHighPerformance`, `NvOptimusEnablement`, `api`;
   * de **3 `__ctype_*`-functies** (`__ctype_b_loc`, `__ctype_tolower_loc`, `__ctype_toupper_loc`),
     gemeten als C++-gemangelde namen `_Z13__ctype_b_locv`, `_Z19__ctype_tolower_locv`,
     `_Z19__ctype_toupper_locv`;
   * de **161 freeglut-namen**, waarvan 31 `*Ucall` (§6.2) — inclusief `glutFullScreen`,
     `glutGameMode*`, `glutSetupVideoResizing` en `glutGetProcAddress`;
   * **`ElfPreReserveCallback`** in de 64-bits variant.
10. **`libglut.so.3` dus niet als bestand leveren** maar als deze 161 exports — des te meer omdat
    het bestand in `libs/` ontbreekt (§8).

**(C) De gastkant — het ELF-werk**

11. De `PT_LOAD`-segmenten op hun `p_vaddr` mappen (niet-PIE, vast beeld op `0x08048000`).
12. `R_386_JMP_SLOT`, `R_386_GLOB_DAT` en `R_386_RELATIVE` toepassen met naam-lookup.
13. `DT_INIT` en `INIT_ARRAY` draaien, dan naar `e_entry` springen.
14. **De twee soorten games apart behandelen** (uit `ELF-LADER-SPEC.md`): de *ongepakte* leden
    (GhostBusters, Terminator, GoGoStrike) zijn standaardwerk; de *HASPE-gepakte* leden
    (GalagaAssault, JurassicPark, PacmanChomp, vermoedelijk Halo) hebben `SHNUM = 0`, een PLT die
    `0xdeadbeef` pusht, een GOT die al absolute `0xb7xxxxxx`-adressen bevat, en 72–82 % naamloze
    UND-symbolen. Daar is naamresolutie **niet** uit het bestand te halen.
15. **`libs/` en `x64/libs/` als symboolbron lezen, niet als gemapte DSO's.** Dat is eerder al
    gemeten (`elfldr2-injectie.md` §2: geen enkele `.so` uit die map staat in de proceskaart), en
    het verklaart waarom er 55 echte ELF's in een *Windows*-lader staan.
16. **`libSDL-1.2.so.0`, `libesd.so.0`, `libfmod.so.10`, `libfmodstudio.so.10` nergens leveren**
    (ze staan niet in `libs/`, niet in `x64/libs/` en niet op de host); `libglut.so.3` komt uit de
    eigen exports; `libXrandr.so.2` en `libiw.so.30` staan wel op de host.
17. **`env/dev/{fd,stdin,stdout,stderr}`** aanbieden als de gast `/dev/...` opent.

**(D) De cache**

18. **`hints.dat` lezen én bijwerken** zoals de echte lader doet: een **append-only** bestand van
    ((u64 sleutel, gastadres))-records, 12 bytes in 32-bits en 16 in 64-bits, per sleutel gesorteerd
    en uniek, één snapshot per sessie per gebruikte sleutel. Een eigen lader die dit bestand
    *schrijft* zonder de semantiek van de sleutel te kennen, kan het bestaande bestand beschadigen.
    **Aanbevolen: alleen lezen, en op een kopie.** De indeling is meetkundig volledig bekend, de
    betekenis van de sleutel niet.

---

## 11. Wat open blijft

1. **De pakker heeft geen naam.** Geen enkele bekende signatuur, geen marker, geen
   archiefformaat-magic. Webzoeken was in deze sessie niet beschikbaar, dus vergelijking met
   gepubliceerde pakkers is beperkt tot byte-signaturen.
2. **Wat de 5,77 MB "RAND" is** — versleuteld of gecomprimeerd met een bereikcoder — is met
   entropie, chi², periodiciteit, stride en comprimeerbaarheid **niet te scheiden**. Daarvoor is een
   decompressor of een dump nodig.
3. **Het uitpakmechanisme zelf** is niet gevonden: geen leesbare decompressiestub behalve ~25
   instructiebytes bij het 32-bits entrypoint, die direct wegspringen naar `0x15bf69d`.
4. **Wat het 64-bits gebied `0x4a0000`–`0x530000` voorstelt.** Vastgemeten: 98.304 records van
   (256-uitgelijnd stijgend adres, lengte als veelvoud van 0x800), geen strings. Semantiek onbekend.
5. **Waar de 64-bits TLS-callback `0xd33198` heen gaat** en wat callback `0x3330` doet — dat kan pas
   na het uitpakken, want `0x3330` ligt in een sectie zonder bytes.
6. **De herkomst van de 64-bits sleutels in `hints.dat`.** 1.463 hash-toetsen, 0 treffers. De
   sleutel is deterministisch maar niet herleid, en dus is er nog geen naam of bibliotheek aan een
   groep te hangen.
7. **Wat `.brr3` na de IAT nog bevat** (32-bits ~1.072 B virtueel, 64-bits ~4.000 B virtueel).
8. **Waarom de twee laders zo verschillend bewerkt zijn** (32-bits nergens code-entropie, 64-bits
   een grote leesbare datatabel en twee TLS-callbacks). Dat is gemeten, niet verklaard.
9. **Of `hints.dat` echt alleen door ElfLdr2 wordt geschreven** en of een andere agent/bench-run
   vandaag aan de groei heeft bijgedragen. De mtime van vandaag 18:25 en de +14.160 records sinds de
   spec zijn gemeten; de veroorzaker niet.
10. **De drie eerdere open punten uit de spec blijven open** en zijn door deze ronde niet geraakt:
    of ElfLdr2 de ingebakken `0xb7xxxxxx`-GOT-waarden gebruikt of overschrijft; hoe hij aan de naam
    van een naamloos slot komt (mijn meting maakt dit **niet** makkelijker); en of de gast-ABI op
    echte hostbibliotheken past.

---

## 12. Hoe je alles narekent

Alle scripts staan in de Windows-werkmap onder `re\`; ze zijn op de Deck gezet en read-only
gedraaid. Uploaden en draaien gaat via `re\rcput.ps1` en `re\rc.ps1` (nooit complexe shell inline in
`-Cmd`; altijd een `.sh`-bestand). De tussenresultaten staan in `~/bw/` op de Deck.

| script | wat het meet |
|---|---|
| `bw_pe.py` | PE-koppen, sectietabel, entropie per sectie, entrypoint-bytes, signaturenzoektocht |
| `bw_sec2.py` | ruwe sectiekoppen in hex (onafhankelijke controle) |
| `bw_dirs.sh` | `objdump -p` beide laders, volledige exportlijst, tekenreeksentellingen |
| `bw_map.py` | entropie + printbaarheid per 64 KiB, `MZ`-voorkomens, 4 KiB-eilanden |
| `bw_code.py` | e8/e9-doelanalyse, eilandstatistiek |
| `bw_deep.py` | laag-entropie-eilanden, datadir-inhoud, `.brr3` als tabel |
| `bw_stat.py` | comprimeerbaarheid, periodiciteit (p = 1…1024), stride (k = 1…128), chi² per 4 KiB |
| `bw_tr.py` | dertien omkeerbare bytebewerkingen tegen de codebias |
| `bw_zones.py` | kalibratie tegen `msys-2.0.dll`, zlib, lzma, RC4; verstrooiing van de hint/naamtabel |
| `bw_final.py` / `bw_final2.py` | segmentatie in klassen, exportgroepering, importtellingen |
| `bw_x64.py` / `bw_tab.py` | het gestructureerde gebied `0x4a0000` en zijn recordstructuur |
| `bw_tls.py` | datadir, TLS-map, callbacks, IAT-omvang |
| `bw_iat.py` | IAT-waarden tegen de hint/naam-records |
| `bw_hints.py` / `bw_hint2.py` | `hints.dat`: veldhistogrammen, sleutels, runs, snapshots |
| `bw_hintkey.py` | de 1.463 hash-toetsen tegen 133 kandidaten |
| `bw_libs2.sh` | per bibliotheek klasse, aantal dynamische symbolen, SONAME, ontbrekende vragen |
| `bw_brr.sh` / `bw_tail.sh` | markerzoektocht en de certificaatgrens |

De belangrijkste losse commando's:

```bash
R="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"

# secties en entrypoints (let op: objdump drukt absolute VMA's af)
objdump -h "$R/BudgieLoader.exe"
objdump -f "$R/BudgieLoader.exe"

# de stub zelf (absolute adressen!)
objdump -D -b pei-i386 -m i386 --start-address=0x1414d27 --stop-address=0x1414db0 "$R/BudgieLoader.exe"
objdump -D -b pei-x86-64 -m i386:x86-64 --start-address=0x10102fea2 --stop-address=0x101030000 \
        "$R/x64/BudgieLoader_x64.exe"

# de leesbare structuren
objdump -p "$R/BudgieLoader.exe" | sed -n '60,520p'
python3 re/bw_tls.py          # TLS-map en callbacks
python3 re/bw_final.py        # exportgroepering

# het levende bestand: altijd eerst een kopie
cp -p "$R/hints.dat" /tmp/h32
python3 re/bw_hint2.py
```

**Hardebeperkingen die in deze ronde zijn gerespecteerd:** geen game gestart, BudgieLoader niet
gestart, niets op de NAS geschreven, geen `git commit/push/checkout/stash`, niets geïnstalleerd.
