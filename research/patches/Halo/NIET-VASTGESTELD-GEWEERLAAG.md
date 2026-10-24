# NIET VASTGESTELD: de adressen van de geweerlaag (leest dit als een waarschuwing, niet als een lezing)

*RavenGun, 2026-10-13. Dit bestand staat NAast `LEZING-GEWEERLAAG.md` en is bedoeld om te voorkomen dat
iemand op die lezing verder bouwt. **Niets van wat hieronder staat is weerlegd met een disassembler** --
het is weerlegd met twee andere instrumenten, en de herlezing zelf is nog niet gedaan.*

## Wat vervalt (en waarom)

| bewering uit de eerdere lezing | status nu | waarmee |
|---|---|---|
| "de aim is een paar `float` op **`0x1f392a0` + 8*speler**" | **NIET VASTGESTELD** | de game laat mijn geschreven `0.5` en `0.25` daar **15 s bit-identiek** staan (run 19), en de binary bevat **nergens** een 4- of 8-byte literaal met dat adres (code én data), terwijl het adres wel in het beeld ligt |
| "per speler een **apparaatblok** op `0x1a7cb20`, 32 blokken van `0x1348`, `+0` = aanwezig, **`+0x3c`** = spelernummer" | **NIET VASTGESTELD als adres** | idem: **geen enkele** verwijzing naar `0x1a7cb20` of `0x1a7cb5c` in de binary. (Wat WEL blijft: het schrijven op die adressen **landt en blijft staan** en het spel zet `+0` niet terug -- zie `RUN-17-UITKOMST.md`. Dat bewijst het schrijfpad, niet dat het spel die velden leest.) |
| "spelersaantal **`[0x1f392d0]`**" | **NIET VASTGESTELD als adres** | idem; wel gemeten: te zetten en het blijft staan (run 19) |
| "de knoppen zijn **overgangstellers** op blok+`0xc8` + 4*knop, `modulo=16`" | **NIET GETEST** | de bron (de shm-bytes van de daemon) staat stil, want de daemon zit in de **evdev-route** (`IO-brug: geen`) |

## Wat NIET vervalt (en waarom dat zo is)

* **De drie regelsoorten in de hulp blijven nuttig**, ongeacht de adressen: `zet` (een vaste waarde),
  `teller` (een bit uit het bordbeeld als overgangsteller) en `as ... soort=f32` (een as als kommagetal
  met eigen bereik). Ze zijn gebouwd, ze zitten in het artefact (`8199ef9b…`) en ze zijn additief naast de
  ingebouwde kastschakelaars -- dat is gemeten in run 17 en 19 (13 schakelaars + mijn regels, geldig).
* **Het schrijfpad werkt**: `zet` met een `float`-bitpatroon komt exact aan (0.5 en 0.25) en het spel
  schrijft het niet terug. Dat is een eigenschap van het gereedschap, niet van het adres.
* **De methode blijft**: adressen komen uit een lezing, en een lezing hoort met een **tweede instrument**
  te worden gecontroleerd voordat er een run op wordt gebouwd. Dat is precies wat hier misging.

## En de open escape

De zoekmethode (een literaal in de binary) kan **niet** uitsluiten dat de code de structuur via een
**basis + afstand** benadert. Dan staat in de code alleen de *offset* en niet het absolute adres, en dan
zouden mijn schrijfacties in een **kopie** van de structuur zijn beland in plaats van in het levende veld.
Daarom is de conclusie "niet vastgesteld" en niet "weerlegd".

## WAT DE HERLEZING NU AL HEEFT OPGELEVERD (nieuw, met de disassembler aan de praat)

**En eerst het instrument: `objdump -D` deed NIETS, en dat was geen bewijs.** Deze binary heeft **geen
section headers** (`ELF 64-bit ... no section header`) en beide `LOAD`-segmenten staan op **RWE**.
`objdump` loopt dus niets af en `--start-address` filtert alleen wat hij toch al liep -- uitvoer: alleen
de kopregel. **De oplossing, en die is herbruikbaar voor elke partij:**
```
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 --start-address=0x61e890 --stop-address=0x61e920 <game>
```
(`--adjust-vma=0x400000` klopt voor **beide** segmenten: bij allebei is `vaddr - offset = 0x400000`.)

**1. De getter op `0x61e890` is nu een echt gelezen functie, en hij BEVESTIGT de schakelaartafel:**
```
61e890: test %edi,%edi
61e892: js     0x61ee68            ; negatief signaal -> andere tak
61e898: cmp    $0x1ff,%edi
61e89e: jle    0x61e930            ; <= 511 -> andere tak
61e8a4: lea    -0x239(%rdi),%edx   ; signaal - 569
61e8b1: and    $0x1f,%edx          ; & 31   -> de SCHAKELAAR binnen een bord (32 vakken!)
61e8b4: lea    0x11ec5c5(%rip),%rsi  # 0x180ae80     <-- TABLE_BASE
61e8bb: sar    $0x5,%eax           ; >> 5   -> het BORD
61e8be: lea    (%rdx,%rdx,4),%rdx  ; * 5
61e8cb: shl    $0x4,%rdx           ; * 16   -> * 0x50 per schakelaar
61e8c7: shl    $0xd,%rax           ; bord * 0x2000
61e8cf: shl    $0x9,%rcx           ; bord * 0x200
61e8d3: add    %rcx,%rax           ; bord * 0x2200
61e8d6: add    %rax,%rdx           ; + schakelaar*0x50  -> het adres van de ingang
```
**Dat is precies het model van onze hulp**: `0x180ae80`, `0x50` per schakelaar, `0x2200` per bord, en
**32 vakken per bord** (niet 13). Het schakelaardeel van de hulp staat dus op een met de disassembler
**bevestigde** basis.

**2. En er is een echte LEZER van die tafel** (aanroep op `0x49ebc2`):
```
49ebbf: mov  (%r14),%edi        ; het signaal, uit een reeks (r14 += 4)
49ebc2: call 0x61e890
49ebc7: test %rax,%rax
49ebca: je   0x49ebd7
49ebcc: mov  0x4(%rax),%eax     ; de game LEEST +4 van de ingang
49ebcf: test %eax,%eax
49ebd1: jne  0x49ee58           ; niet nul -> er is iets met die schakelaar
```
Dat is de "ingedrukt"-semantiek die de hulp schrijft, nu van de lezerskant bevestigd.

**3. En de adressen van de geweerlaag staan er nog steeds niet in -- nu met een DERDE methode:**
```
geen absolute literaal   (run-19-controle, 4 en 8 byte, code én data)
geen RIP-relatieve verwijzing (lea/mov/movss naar 0x1f392a0, 0x1f392a4, 0x1f392d0, 0x1a7cb20, 0x1a7cb5c)
en het spel laat mijn geschreven waarden 15 s ongemoeid (run 19)
```
**Drie onafhankelijke instrumenten** zeggen hetzelfde: **die adressen worden door de code niet
aangeraakt.** De lezing van het aim-paar en het apparaatblok is daarmee **niet vastgesteld** -- en wie
verder wil, moet bij de **geweerfuncties** beginnen die de binary zelf noemt (`GunCalProc`,
`GunCameraTestProc`, `GunInfo_NumAttachedGuns`/`AttachedSerials`, `QRSetupProc`) en hun dataverwijzingen
volgen met de disassembler die nu werkt -- niet bij mijn adressen.


1. **Werkt de disassembler op deze binary?** Mijn eerste poging (`objdump -D` op `0x61e890` en op twee
   aanroepplekken) gaf **geen uitvoer** -- dat is een instrumentfout, geen bewijs. Eerst dat oplossen.
2. **De 10 aanroepplekken van de getter `0x61e890`** (gevonden met een `e8 rel32`-scan:
   `0x49ebc2`, `0x49ee38`, `0x49ee60`, `0x49ee83`, ...) -- daaruit volgt hoe de invoerlaag wordt
   gebruikt en of er een **basis** uit een globaal wordt geladen.
3. **Bestaat er een tabel of een basis?** Als het een vaste tabel is, hoort er een literaal in de binary
   te staan -- en die is er voor deze vijf adressen **niet**. Als het een basis is, is de volgende vraag
   **waar die basis vandaan komt**, en dat is de zin die de laderbouwer nodig heeft: dan moet de
   IO-brug of de hulp die **basis** vullen, niet een absoluut adres.
