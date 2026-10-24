# AM-LAAG.md — de Sega-`am`-laag in Luigi's Mansion Arcade, en wat de GESLOTEN kern ermee doet

**Soort werk:** statische analyse op een geheugendump. Er is **geen run** gedaan en er is **niets**
geschreven buiten dit bestand. Alleen gelezen.

**Bronnen (allemaal read-only gelezen):**

| bron | wat |
|---|---|
| `/tmp/b0-geheugen-b/image-gesloten.bin` | geheugendump spelproces onder de **GESLOTEN** kern, 19.259.392 B, **offset = RVA** bij ImageBase `0x140000000`; sha256 `11bb710b7c71686b` (uit `samenvatting-gesloten.txt`) |
| `/tmp/b0-geheugen-b/maps-gesloten.txt` | `/proc/<pid>/maps` van diezelfde run |
| `/tmp/b0-geheugen-b/gesloten.log`, `onze.log`, `vergelijking.txt` | launch-direct-logboeken; kern-sha's |
| `.../Luigi's Mansion Arcade/exe/x64/VACUUM.exe` | spelbestand op de NAS, 3.436.544 B |
| `/tmp/vacuum-dump/` | eerdere per-sectie-dumps (`text.bin`, `capcom0.bin`, …) |

**Etiketten.** **[GEMETEN]** = direct uit bytes of uit de maps gelezen, met adres en bytes erbij.
**[AFGELEID]** = gevolgtrekking uit die metingen. Waar ik iets niet weet staat dat er letterlijk.

---

## 0. Samenvatting

> ### ⚠ EERST DIT — de dichte meting, en zij WEERLEGT mijn eerdere waarschuwing
>
> Ik heb het venster t=4,0 … 6,5 s op **beide** kernen dicht bemonsterd (elke 0,1 s), met per moment
> het percentage nul in `.text` (`0x140001000`, 3.496.500 B) en de 5 bytes op `0x1402b4170`.
>
> | t (s) | **ONZE kern** `fe2ecf8c…` `.text` %nul | prologue | **GESLOTEN kern** `f5c7532d…` `.text` %nul | prologue |
> |---|---|---|---|---|
> | 4,0–4,5 | **100,00** | `00 00 00 00 00` | **100,00** | `00 00 00 00 00` |
> | 4,6 | **75,28** (uitpakken begint) | `00 00 00 00 00` | 100,00 | `00 00 00 00 00` |
> | 4,7 | **11,20** (volledig uitgepakt) | `40 53 48 83 ec` (origineel) | 100,00 | `00 00 00 00 00` |
> | 4,8 | 11,25 | (leesfout) | 100,00 | `00 00 00 00 00` |
> | 5,0–8,0 | — | **proces weg op t=5,96** | 100,00 | `00 00 00 00 00` |
> | 9,0 | — | — | **64,16** (uitpakken begint) | `00 00 00 00 00` |
> | 10,0 | — | — | **11,20** | **`e9 df cd d3 ff` (omgezet)** |
> | 12,0 | — | — | 11,20 | `e9 df cd d3 ff` |
>
> **[GEMETEN]** — en de gevolgen zijn precies omgekeerd aan wat ik in de vorige versie schreef:
>
> 1. **De pakker WERKT onder onze kern.** `.text` bereikt onder onze kern **11,20 % nul op t=4,7 s** —
>    exact dezelfde eindtoestand die de gesloten kern op t=10,0 s bereikt. Het spel komt dus
>    **voorbij het uitpakken**.
> 2. Onder onze kern sterft het proces **op t=5,96 s, dus ~1,3 s NA een geslaagd uitpakken**, en de
>    17 prologues staan dan nog **origineel**.
> 3. Onder de gesloten kern gebeuren het uitpakken en het omzetten van de prologues **op hetzelfde
>    moment** (t=10,0 s: 11,20 % nul én `e9 …`).
>
> **Conclusie: de `am`-drop-in is wél de juiste route.** Het spel bereikt onder onze kern de
> uitgepakte toestand, en het enige gemeten verschil in het spelbeeld op dat punt is dat de 17
> prologues origineel zijn in plaats van `e9`. De pakker/`NtProtectVirtualMemory`-route is dus **niet**
> nodig. Mijn eerdere waarschuwing (dat het spel in de pakker zou sterven) is hiermee **weerlegd**.
> Het blijft **[AFGELEID]** dat die 17 prologues de dood *veroorzaken* — het is een sterke correlatie
> en het enige gemeten verschil, maar niet meer dan dat. Het beslissende experiment is de drop-in
> zelf: zet de 17 prologues om en kijk of het spel dan langer dan 6 s leeft.

1. De bewering **"17 functies met een gewijzigde prologue" klopt precies** — het zijn 17 van de 18
   prologues die naar één pagina springen: **`0x13fff0000`, 4096 B, anoniem, `rwxp`**. **[GEMETEN]**
2. Die 17 zijn `amDongle*` (8), `amHm*` (5), `amMaster*` (4). De namen komen **niet** uit een
   exporttabel maar uit **log-tag-strings** in `.rdata` die elk vanuit hun eigen functie worden
   aangeroepen. **[GEMETEN]**
3. De pagina is een **tweeslotige detour-tabel**: de prologue springt naar slot B
   (`jmp qword [rip+0]` → **een functie in de gesloten kern**), en slot A zet de originele prologue
   terug en springt naar het originele lichaam. Alle 18 doelen liggen in de kern-module
   `0x6ffff6b61000..0x6ffff6ddc000`. **[GEMETEN]**
4. **Alle 17 kern-implementaties geven `0` terug** (`xor eax,eax; add rsp,0x28; ret`), en de meeste
   loggen alleen een regel. Eén uitzondering: **`amDongleGetNetworkAddress` schrijft `0x1a8c0`
   (= 192.168.1.0) in de uitvoerpointer** en geeft 0 terug. **[GEMETEN]** Dat is het enige echte
   "antwoord van de kast" dat ik heb gevonden.
5. Het dongle-object is **leeg** (512 B nul) op alle vier de gemeten momenten. De waarden die de game
   zou lezen zijn er dus niet: de kern vult geen object, hij vervangt de aanroepen. **[GEMETEN]**

---

## 1. De 17 gewijzigde prologues — [GEMETEN]

**Methode.** Scan van de hele dump op `e9 rel32` waarvan het doel in `0x13fff0000..0x13fff1000`
valt (`doel = ImageBase + offset + 5 + rel32`).

**Resultaat: 18 treffers. 17 daarvan liggen in de `am`-laag in `.text`; 1 ligt elders.**

De pagina zelf, uit `maps-gesloten.txt` regel 99:

```
13fff0000-13fff1000 rwxp 00000000 00:00 0
```

Dus: **anoniem, 4096 B, read+write+execute, direct onder de ImageBase van het spel.** **[GEMETEN]**

| # | functie-start (RVA) | prologue-bytes | sprongdoel | naam (uit de log-tag in het lichaam) |
|---|---|---|---|---|
| 1 | `0x002b3b70` | `e9 5f d4 d3 ff` | `0x13fff0fd4` | `amDongleBillingGetKeychipId` |
| 2 | `0x002b3d30` | `e9 9f d1 d3 ff` | `0x13fff0ed4` | `amDongleGetGameId` |
| 3 | `0x002b3ff0` | `e9 9f ce d3 ff` | `0x13fff0e94` | `amDongleGetModelType` |
| 4 | `0x002b40b0` | `e9 9f cd d3 ff` | `0x13fff0e54` | `amDongleGetNetworkAddress` |
| 5 | `0x002b4170` | `e9 df cd d3 ff` | `0x13fff0f54` | `amDongleInit` |
| 6 | `0x002b4270` | `e9 5e c9 d3 ff` | `0x13fff0bd3` | `amDongleIsAvailableInternal` |
| 7 | `0x002b4320` | `e9 6f cc d3 ff` | `0x13fff0f94` | `amDongleSetupKeychip` |
| 8 | `0x002b4740` | `e9 d4 c7 d3 ff` | `0x13fff0f19` | `amDongleUpdate` |
| 9 | `0x002b50f0` | `e9 a4 bb d3 ff` | `0x13fff0c99` | `amHmExit` |
| 10 | `0x002b5180` | `e9 94 bb d3 ff` | `0x13fff0d19` | `amHmGetCautionFlag` |
| 11 | `0x002b5200` | `e9 14 ba d3 ff` | `0x13fff0c19` | `amHmInit` |
| 12 | `0x002b55a0` | `e9 34 b7 d3 ff` | `0x13fff0cd9` | `amHmResetCautionFlag` |
| 13 | `0x002b56f0` | `e9 5f b5 d3 ff` | `0x13fff0c54` | `amHmUpdate` |
| 14 | `0x002b5860` | `e9 72 b5 d3 ff` | `0x13fff0dd7` | `amMasterExitInternal` (lichaam noemt ook `amMasterExitA`) |
| 15 | `0x002b5cb0` | `e9 5f b1 d3 ff` | `0x13fff0e14` | `amMasterInit` |
| 16 | `0x002b6050` | `e9 00 ad d3 ff` | `0x13fff0d55` | `amMasterSetBackupEventLogParam` |
| 17 | `0x002b6160` | `e9 34 ac d3 ff` | `0x13fff0d99` | `amMasterUpdate` |
| — | `0x0000485a` | `e9 2f c3 fe ff` | `0x13fff0b8e` | **geen `am`-functie** (ligt aan het begin van `.text`) |

**[GEMETEN]** De 18 sprongdoelen zijn 18 verschillende offsets in dezelfde pagina; de pagina is dus
een tabel met 18 stubs, niet één gedeelde stub.

**Let op, eerlijke grens:** de naamskoppeling in kolom 5 komt uit de log-tag die *binnen het
functiebereik* `[start, volgende start)` wordt aangeroepen. Voor de meeste rijen is dat eenduidig.
Voor rij 12 en 14 noemt het bereik twee tags (een gedeelde log-hulp); daar is de naam een
**beste toewijzing**, geen bewijs. De *adressen* en de *sprongdoelen* zijn hard.

---

## 2. Waar de namen vandaan komen — [GEMETEN]

De vraag was of de namen bij de adressen horen. Ze komen **niet** uit een exporttabel en **niet** uit
de kern. Ze zijn als volgt te vinden:

* De exportdirectory van het spel staat in het geheugen op RVA `0x0121cc00` (module
  `VACUUM_FINAL.exe`, `NumberOfNames = 57`, `Base = 1`). **Geen enkele naam begint met `am`** — het
  zijn libcurl-namen. **[GEMETEN]** (Dit bevestigt wat de coordinator zelf nagemeten had.)
* De `am`-namen staan als **losse strings in `.rdata`**, in de reeks `0x0036a648` … `0x0036b2b8`,
  naast hun build-regels, bijvoorbeeld:

```
0x0036a648  amDongle Build:Jul 21 2015 18:07:42 ($Rev: 4286 $)
0x0036a680  Error: amyDongleOpen() failed.\n
0x0036a6a0  amDongleInit
0x0036a6b0  Error: amyAsyncCreateObject() failed.\n
0x0036a6d8  Error: amyAsyncWaitForObject() %d.\n
0x0036a745  Keychip
0x0036a818  Error: precondition. %d\n
0x0036a850  Error: invalid arg.\n
0x0036a7c8  Error: amyDongleIsAvailable() failed %d.\n
0x0036a8b2  KeychipId
```
  **[GEMETEN]**
* **Elke `am`-functie verwijst met `lea rcx,[rip+…]` naar zijn eigen tag-string.** Dat is de
  naam→adres-route. Voorbeeld, `amDongleInit`:

```
1402b41c5  4c 8d 05 b4 64 0b 00   lea r8,[rip+0xb64b4]    # 0x14036a680  "Error: amyDongleOpen() failed."
1402b41cc  48 8d 0d cd 64 0b 00   lea rcx,[rip+0xb64cd]   # 0x14036a6a0  "amDongleInit"
1402b41d3  ba e8 01 00 00         mov edx,0x1e8            ; regelnummer in het logboek
1402b41d8  e8 e3 46 00 00         call 0x1402b88c0         ; amyLog-achtige hulp
```
  **[GEMETEN]**

**Conclusie.** De namen zijn log-tags, geen exports en geen symbolen. Een naam hoort bij een adres
omdat de functie zelf zijn eigen tag aanroept. Dat is een sterke maar indirecte koppeling: als een
functie zijn tag deelt met een buur, is de toewijzing onzeker (zie §1).

Er zijn **geen** `keychip.appboot.*`-opdrachtstrings in het beeld. **[GEMETEN]** Dat is een verschil
met `patches/OG/` (waar `amDongleSetupKeychip` wél met zulke opdrachten werkt). Voor Luigi's Mansion
Arcade is het dongleprotocol dus **niet** het OG-protocol; ik heb geen vervangend protocol gevonden.

---

## 3. Wat elke functie vraagt — [GEMETEN] uit de disassembly

Het **globale dongle-object** staat achter één aanwijzer: `[0x14049cc68]`. De `amMaster`-variant
heeft zijn eigen object op `[0x14049ccd8]`.

### 3.1 `amDongleInit` (lichaam vanaf `0x002b4182`)

```
1402b4182  48 8b 1d df 8a 1e 00   mov rbx,[0x14049cc68]        ; dongle-object
1402b4189  83 3b 00               cmp dword [rbx],0
1402b418c  74 0b                  je  0x1402b4199
1402b418e  b8 fc ff ff ff         mov eax,0xfffffffc           ; -4: al geïnitialiseerd
1402b4199  33 d2                  xor edx,edx
1402b419b  41 b8 00 02 00 00      mov r8d,0x200
1402b41a1  48 8b cb               mov rcx,rbx
1402b41a4  e8 27 bd 05 00         call 0x14030fed0             ; memset(obj, 0, 0x200)
1402b41a9  48 8d 4b 08            lea rcx,[rbx+8]
1402b41ad  e8 de 7a 00 00         call 0x1402bbc90             ; "amyDongleOpen"
1402b41b4  85 c0                  test eax,eax
1402b41b6  79 31                  jns 0x1402b41e9
1402b41b8..  log "amDongleInit" + "Error: amyDongleOpen() failed." regel 0x1e8
1402b41e9  48 8b 0d 78 8a 1e 00   mov rcx,[0x14049cc68]
1402b41f0  48 83 c1 10            add rcx,0x10
1402b41f4  e8 a7 69 00 00         call 0x1402baba0             ; "amyAsyncCreateObject"
1402b41f9  8b d8                  mov ebx,eax
1402b41fb  85 c0                  test eax,eax
1402b41fd  79 3d                  jns 0x1402b423c
1402b41ff..  log "Error: amyAsyncCreateObject() failed." regel 0x1ef
```

**Vraagt:** een dongle die te openen is (`amyDongleOpen`) en een async-object dat te maken is.
**Antwoordt:** `0` bij succes; `-4` als al geïnitialiseerd; de foutcode van de dongle-open anders.
**[GEMETEN]**

### 3.2 `amDongleIsAvailableInternal` (lichaam vanaf `0x002b4290`)

```
1402b4290  40 53                  push rbx
1402b4292  48 83 ec 20            sub  rsp,0x20
1402b4296  48 8b 0d cb 89 1e 00   mov  rcx,[0x14049cc68]
1402b429d  4c 8d 41 10            lea  r8,[rcx+0x10]
1402b42a1  48 8d 91 e8 01 00 00   lea  rdx,[rcx+0x1e8]
1402b42a8  48 83 c1 08            add  rcx,0x8
1402b42ac  e8 2f 79 00 00         call 0x1402bbbe0             ; "amyDongleIsAvailable"
1402b42b1  8b d8                  mov  ebx,eax
1402b42b3  85 c0                  test eax,eax
1402b42b5  79 26                  jns  0x1402b42dd
1402b42b7..  log "amDongleIsAvailableInternal" + "Error: amyDongleIsAvailable() failed %d." regel 0x3b5
1402b42dd  48 8b 05 84 89 1e 00   mov  rax,[0x14049cc68]
1402b42e4  c7 80 10 01 00 00 40 00 00 00   mov dword [rax+0x110],0x40
1402b42ee  c7 80 14 01 00 00 01 00 00 00   mov dword [rax+0x114],0x1
1402b42f8  81 fb 03 00 00 81      cmp  ebx,0x81000003
1402b42fe  74 0a                  je   0x1402b430a
1402b4300  f7 db                  neg  ebx
1402b4302  1b c0                  sbb  eax,eax                  ; 0 als ebx==0, anders -1
1402b4304  48 83 c4 20            add  rsp,0x20
1402b4308  5b                     pop  rbx
1402b4309  c3                     ret
1402b430a  b8 fe ff ff ff         mov  eax,0xfffffffe           ; -2 bij 0x81000003
```

**Dit is de poort.** Bij succes zet hij de statusvelden `[obj+0x110] = 0x40` en `[obj+0x114] = 1` en
geeft `0` terug; bij fout `-1`, en speciaal `-2` als de dongle-foutcode `0x81000003` is.
**[GEMETEN]**

### 3.3 De vier "vragers" — één gemeenschappelijk sjabloon

`amDongleGetGameId`, `amDongleGetModelType`, `amDongleGetNetworkAddress` en
`amDongleBillingGetKeychipId` hebben alle vier dezelfde vorm (hier `amDongleGetGameId`):

```
1402b3d3f  41 83 38 00            cmp  dword [r8],0            ; r8 = dongle-object
1402b3d43  75 07                  jne  0x1402b3d4c
1402b3d45  bb fd ff ff ff         mov  ebx,0xfffffffd          ; -3: niet geïnitialiseerd
1402b3d4c  41 83 b8 b4 01 00 00 00  cmp dword [r8+0x1b4],0
1402b3d54  75 05                  jne  0x1402b3d5b
1402b3d56  83 cb ff               or   ebx,0xffffffff          ; -1: dongle niet beschikbaar
1402b3d5b  41 83 b8 10 01 00 00 00  cmp dword [r8+0x110],0
1402b3d63  74 31                  je   0x1402b3d96
1402b3d65  bb 01 00 00 00         mov  ebx,1
...
1402b3d96  48 85 c9               test rcx,rcx                 ; rcx = uitvoerpointer
1402b3d99  75 2c                  jne  0x1402b3dc7
1402b3d9b..  log "amDongleGetGameId" + "Error: invalid arg." regel 0x517
1402b3dbc  b8 fe ff ff ff         mov  eax,0xfffffffe          ; -2: null-uitvoer
1402b3dc7  49 81 c0 c0 01 00 00   add  r8,0x1c0
1402b3dce  ba 05 00 00 00         mov  edx,0x5
1402b3dd3  e8 d0 92 05 00         call 0x14030d0a8             ; memcpy(rcx, obj+0x1c0, 5)
1402b3dd8  83 fb 01               cmp  ebx,0x1
1402b3ddb  75 15                  jne  0x1402b3df2
1402b3ddd  48 8b 05 84 8e 1e 00   mov  rax,[0x14049cc68]
1402b3de4  48 c7 80 10 01 00 00 04 00 00 00   mov qword [rax+0x110],0x4
```

Per functie verschilt alleen **welk veld** en **hoeveel bytes** gekopieerd worden:

| functie | bronveld | lengte | status na succes |
|---|---|---|---|
| `amDongleGetGameId` (`0x2b3d30`) | `obj+0x1c0` | 5 B | `[obj+0x110] = 4` |
| `amDongleGetNetworkAddress` (`0x2b40b0`) | `obj+0x1c8` (`mov eax,[r8+0x1c8]; mov [rcx],eax`) | 4 B | `[obj+0x110] = 9` |
| `amDongleBillingGetKeychipId` (`0x2b3b70`) | `obj+0x1cc` (`add r8,0x1cc; mov edx,0x11`) | 17 B | `[obj+0x110] = 0xb` |
| `amDongleGetModelType` (`0x2b3ff0`) | zelfde sjabloon, veld niet meer leesbaar (zie onder) | — | — |

**[GEMETEN]** voor de eerste drie; voor `amDongleGetModelType` is het lichaam vanaf `0x2b3ff5`
onleesbaar geworden omdat de 5-byte jmp midden in de originele prologue viel. Ik **weet niet** welk
veld die functie kopieert.

**Belangrijk:** de antwoorden komen uit **velden van het dongle-object** (`+0x1c0`, `+0x1c8`,
`+0x1cc`), die gevuld worden door de dongle-I/O in `amDongleInit`/`amDongleUpdate`. De *waarden*
staan dus niet in de code. **[GEMETEN]**

### 3.4 `amHm*` (5 van de 17)

`amHmInit` (`0x2b5200`, lichaam vanaf `0x2b5205`): roept `0x1402b8a10` (module-registratie), kijkt
naar een global op `0x1404de6b0` (al geïnitialiseerd → `-4`), en roept dan `0x1402b5490`
(`amHmLoadHwInfo`-gebied) en `0x1402b5410` (`amHmLoadCpuTempThreshold`-gebied) aan; bij fout geeft
hij die foutcode terug. **[GEMETEN]**

`amHmUpdate` (`0x2b56f0`): controleert een global, geeft `-3` als niet geïnitialiseerd, roept anders
`0x1402bbd70`. **[GEMETEN]**

De overige drie (`amHmExit`, `amHmGetCautionFlag`, `amHmResetCautionFlag`) hebben dezelfde vorm:
statuscontrole → `-3`/`-4`, anders een kleine bewerking op de `amHm`-global en `0`. **[AFGELEID]**
uit de aanroepen in dezelfde regio; niet alle lichamen zijn regel voor regel nagelezen.

### 3.5 `amMaster*` (4 van de 17)

`amMasterInit` (`0x2b5cb0`, lichaam vanaf `0x2b5cb5`):

```
1402b5cc2  48 8b 1d 0f 70 1e 00   mov rbx,[0x14049ccd8]        ; amMaster-object
1402b5cc9  83 3b 00               cmp dword [rbx],0
1402b5ccc  74 0b                  je  0x1402b5cd9
1402b5cce  b8 fc ff ff ff         mov eax,0xfffffffc           ; -4
1402b5cd9  33 c0                  xor eax,eax
1402b5cdb  48 8d 4b 0c            lea rcx,[rbx+0xc]
1402b5cdf  33 d2                  xor edx,edx
1402b5ce1  41 b8 00 04 00 00      mov r8d,0x400
1402b5ce7  48 89 43 04            mov [rbx+0x4],rax
1402b5ceb  e8 e0 a1 05 00         call 0x14030fed0             ; memset(obj+0xc, 0, 0x400)
1402b5cf0  48 8d 4b 0c            lea rcx,[rbx+0xc]
1402b5cf4  e8 17 7f 00 00         call 0x1402bdc10
1402b5cf9  85 c0                  test eax,eax
1402b5cfb  79 2d                  jns 0x1402b5d2a
1402b5cfd..  log "amMasterInit" regel 0x92
1402b5d2a  ...                    call 0x1402be450
```

`amMasterUpdate` (`0x2b6160`), `amMasterExitInternal` (`0x2b5860`, lichaam noemt ook
`amMasterExitA`) en `amMasterSetBackupEventLogParam` (`0x2b6050`) hebben dezelfde vorm.
**[GEMETEN]** voor `amMasterInit`; **[AFGELEID]** voor de andere drie.

---

### 3.6 Wie ze aanroept — [GEMETEN]

Scan van het hele beeld op `call rel32` naar elk van de 17 startadressen:

| functie | aanroeper(s) (RVA) |
|---|---|
| `amDongleInit` | `0x0004d2df` |
| `amDongleSetupKeychip` | `0x0004d2f7` |
| `amDongleBillingGetKeychipId` | `0x0004d30d` |
| `amDongleUpdate` | `0x0004d314`, `0x0004e7a9` |
| `amDongleGetNetworkAddress` | `0x0004d33f` |
| `amDongleGetGameId` | `0x0004d371` |
| `amDongleGetModelType` | `0x0004d38a` |
| `amDongleIsAvailableInternal` | `0x000ab8b9`, `0x001349d8` |
| `amHmInit` | `0x0004d5ac` |
| `amHmGetCautionFlag` | `0x0004d5cd` |
| `amHmExit` | `0x0004e57b` |
| `amHmUpdate` | `0x0004e75d` |
| `amHmResetCautionFlag` | `0x000abc74` |
| `amMasterInit` | `0x0004d9cd` |
| `amMasterSetBackupEventLogParam` | `0x0004dc97` |
| `amMasterExitInternal` | `0x0004e6cd` |
| `amMasterUpdate` | `0x0004e735` |

**Alle 17 worden direct aangeroepen; geen enkele via een functiepointertabel** — ik heb voor elk van
de 17 naar 8-byte aanwijzers in het hele beeld gezocht: **0 treffers**. **[GEMETEN]**

Er zijn twee clusters: een **opzetroutine** op `0x0004d250` (0x4d2df … 0x4dc97) en een
**update/afsluitblok** op `0x0004e57b` … `0x0004e7a9`. Daarbuiten staan twee losse vragers van
`amDongleIsAvailableInternal` op `0x000ab8b9` en `0x001349d8`. **[GEMETEN]**

**Het foutpad van de opzetroutine — [GEMETEN]:**

```
14004d2df  e8 8c 6e 26 00            call 0x1402b4170          ; amDongleInit
14004d2e4  48 8b 0b                  mov  rcx,[rbx]
14004d2e7  89 41 04                  mov  dword [rcx+0x4],eax  ; status bewaren
14004d2ea  48 8b 03                  mov  rax,[rbx]
14004d2ed  83 78 04 00               cmp  dword [rax+0x4],0
14004d2f1  0f 85 a3 00 00 00         jne  0x14004d39a        ; <-- FOUTPAD
14004d2f7  e8 24 70 26 00            call 0x1402b4320          ; amDongleSetupKeychip
14004d2fc  83 f8 02                  cmp  eax,0x2
14004d2ff  74 f6                     je   0x14004d2f7          ; lus zolang eax == 2
14004d301  48 8b 0b                  mov  rcx,[rbx]
14004d306  48 81 c1 95 00 00 00      add  rcx,0x95
14004d30d  e8 5e 68 26 00            call 0x1402b3b70          ; amDongleBillingGetKeychipId
14004d312  8b f8                     mov  edi,eax
14004d314  e8 27 74 26 00            call 0x1402b4740          ; amDongleUpdate
14004d319  83 ff 01                  cmp  edi,0x1
14004d31c  74 e3                     je   0x14004d301          ; lus zolang edi == 1
14004d31e  85 ff                     test edi,edi
14004d320  75 78                     jne  0x14004d39a        ; <-- FOUTPAD
```

Dus: **geeft `amDongleInit` iets anders dan 0 terug, of eindigt de keychip/update-lus met
`edi != 0`, dan springt de game naar `0x0004d39a`.**

**Wat daar staat — en dit is een belangrijke negatieve uitkomst — [GEMETEN]:**

```
14004d39a  48 8b 74 24 40      mov  rsi,[rsp+0x40]
14004d39f  48 8b c3            mov  rax,rbx
14004d3a2  48 8b 5c 24 30      mov  rbx,[rsp+0x30]
14004d3a7  48 83 c4 20         add  rsp,0x20
14004d3ab  5f                  pop  rdi
14004d3ac  c3                  ret
```

`0x0004d39a` is **niet** een fout- of afsluitpad: het is de **gewone epilogue** van de functie. De
routine geeft de dongle-structuur terug met de status in `[obj+0x4]`, en de *aanroeper* beslist
verder. **Een mislukte dongle-controle beëindigt het spel dus niet van zichzelf.** **[GEMETEN]**

Dat is een waarschuwing tegen een te makkelijke causaliteit: "`amDongleIsAvailableInternal` faalt →
exit 32" is **niet** aangetoond, en het ziet er niet naar uit dat de `am`-laag zelf de exit neemt.

**Wat ik verder niet gevolgd heb:** de aanroeper van `0x0004d250`, en waar de vlag `edi` uit
`0x001349d8` naartoe leidt.

**Zijspoor dat wél opvalt [GEMETEN]:** de functie direct ná dit blok (`0x0004d3b0`) opent een
bestand met een **relatief pad**:

```
14004d409  4c 8d 0d 60 f6 38 00   lea  r9,[rip+0x38f660]   # 0x1403dca70  "../../Ram/"
14004d410  4c 8d 05 69 f6 38 00   lea  r8,[rip+0x38f669]   # 0x1403dca80  "%sEEPROM_0x%08x.bin"
14004d417  48 8d 4c 24 30         lea  rcx,[rsp+0x30]
14004d41c  ba 00 01 00 00         mov  edx,0x100
14004d425  e8 e2 d9 2b 00         call 0x14030ae0c
14004d42a  48 8d 15 e3 b4 38 00   lea  rdx,[rip+0x38b4e3]   # 0x1403d8914  "rb"
14004d436  e8 0d 81 2c 00         call 0x140315548          ; fopen-achtig
```

Dat is een **EEPROM-bestand** dat met een pad van twee niveaus omhoog gelezen wordt. Als de werkmap
onder onze kern anders is, is dit een concrete plek waar het mis kan gaan. **[GEMETEN]** dat dit pad
bestaat; **niet gemeten** dat het onder onze kern faalt.

**De tweede vrager — [GEMETEN]:**

```
1401349cf  4c 39 35 da 23 3c 00      cmp  qword [0x1404f6db0],r14   ; r14 = 0
1401349d6  74 0f                     je   0x1401349e7
1401349d8  e8 93 f8 17 00            call 0x1402b4270               ; amDongleIsAvailableInternal
1401349dd  40 0f b6 ff               movzx edi,dil
1401349e1  85 c0                     test eax,eax
1401349e3  41 0f 44 fe               cmove edi,r14d                 ; eax == 0  ->  edi = 0
1401349e7  48 63 75 1c               movsxd rsi,[rbp+0x1c]
1401349fa  83 fe 07                  cmp  esi,0x7
1401349fd  0f 87 1c 0b 00 00         ja   0x14013551f
140134a03  4c 8d 2d f6 b5 ec ff      lea  r13,[rip-0x134a0a]        ; = 0x140000000
140134a0a  41 8b 84 b5 50 55 13 00   mov  eax,[r13+rsi*4+0x135550]  ; sprongtabel op RVA 0x135550
140134a12  49 03 c5                  add  rax,r13
140134a15  ff e0                     jmp  rax                       ; 8 takken
```

`amDongleIsAvailableInternal` wordt hier gebruikt om een vlag `edi` te zetten: **`edi` wordt op 0
gezet als de dongle beschikbaar is**. Daarna volgt een gewone compilerswitch over `[rbp+0x1c]`
(8 takken, tabel op RVA `0x135550`) — geen VM. **[GEMETEN]** Waar `edi` daarna toe leidt, heb ik
**niet** gevolgd.

---

## 4. Hoe de GESLOTEN kern ze afvangt — [GEMETEN] + [AFGELEID]

**Wat gemeten is:**

1. **De prologue is exact 5 bytes `e9 <rel32>` en springt naar `0x13fff0000 + offset`.** De pagina is
   4096 B, anoniem, `rwxp`, en ligt 64 KiB onder de ImageBase van het spel. **[GEMETEN]**
2. **Er staat geen enkele aanwijzer naar de kern in het spelbeeld.** Ik heb alle 19.259.384 posities
   als 8-byte woord gelezen en getoetst aan het uitvoerbare bereik van de kern
   `0x6ffff6b61000..0x6ffff6ddc000` (uit `maps-gesloten.txt`): **0 treffers**. **[GEMETEN]**
3. De enige wijziging in het spelbeeld is dus die 18 prologues. **[GEMETEN]**
4. De 2510 bytes verschil tussen `.capcom1` in het geheugen en in het bestand liggen **allemaal** in
   de IAT (`0x0120d000..0x0120dda5`): in het bestand staan RVA's, in het geheugen opgeloste
   `0x6ffff…`-adressen. Dat is normale IAT-binding, **geen kern-patch**. **[GEMETEN]**
5. `.text` is gemapt als **`r-xp`** (`140001000-140357000 r-xp`), dus **niet schrijfbaar**. Om een
   prologue in `.text` te overschrijven moet een pagina eerst schrijfbaar gemaakt worden.
   **[GEMETEN]**
6. **De controle, in één run per kern — [GEMETEN]:**
   * **GESLOTEN kern** (`f5c7532d…`, sha uit het launch-direct-log): op t=10, 12, 16, 20 en 25 s staat
     op alle acht gecontroleerde prologues **`e9 df cd d3 ff`** c.s. — precies dezelfde bytes als in
     de statische dump. En op t=2, 3, 4 en 5 s staat er **`00 00 00 00 00`**: `.text` is dan nog niet
     uitgepakt, en de pagina is nog niet gemapt (EIO).
   * **ONZE kern** (`fe2ecf8c…`, sha uit het launch-direct-log): op **alle** bemonsterde momenten
     (t=0,1 / 0,2 / 0,3 / 0,5 / 0,8 / 1,2 / 2,0 / 4,0 s) staat op alle acht prologues
     **`00 00 00 00 00`**, is de trampolinepagina **nooit** leesbaar, en het proces is weg op
     **t=5,72 s**.
   De causale keten van hieronder is daarmee **gemeten voor zover hij strekt**: onze kern zet de 17
   prologues niet om. Wat er **niet** uit volgt, is dat dat de dood veroorzaakt — zie de waarschuwing
   in §0 en §6.

**Wat daaruit volgt [AFGELEID, sterk ondersteund]:**

De gesloten kern **vervangt de 17 `am`-functies in hun geheel** door eigen implementaties, en de
enige manier om dat in een `r-xp`-sectie te doen is de pagina schrijfbaar maken. Dat verklaart
precies de door de coordinator gemeten observatie dat de gesloten kern `ntdll!NtProtectVirtualMemory`
en `ZwProtectVirtualMemory` aanhaakt en **onze kern niet**:

> onze kern maakt `.text` niet schrijfbaar → de 17 prologues blijven origineel → het spel roept de
> echte dongle-code aan → er is geen dongle → `amDongleIsAvailableInternal` faalt → het spel stopt.

Dat is een **samenhangende verklaring** voor het verschil tussen de twee kernen. Ik heb hem **niet
direct gemeten** (daarvoor moet je in beide runs de bytes op `0x2b4170` vergelijken — zie §6, stap 3).

**Wat de pagina bevat — [GEMETEN] in `/tmp/lm-meet-gesloten/tramp-10.0.bin` (4096 B, 610 niet-nul):**

De pagina is een tabel van **tweeslotige detours**. Per functie:

```
slot A  (bijv. 0x13fff0f40):  <herhaalde originele prologue> ; ff 25 00 00 00 00 <qword>
                                                                 ^ jmp qword ptr [rip+0]
        het qword = het ORIGINELE lichaam   (bijv. 0x1402b4176)
slot B  (bijv. 0x13fff0f54):  ff 25 00 00 00 00 <qword>
        het qword = de KERN-implementatie   (bijv. 0x6ffff6cfe1f0)
```

De prologue van de game springt naar **slot B**, dus naar de kern. Slot A is de terugweg: de kern
roept die aan als hij het originele gedrag wil, en slot A zet de weggeoverschreven prologue
gedeeltelijk terug en springt naar het originele lichaam. Voorbeeld, `amHmInit`
(`0x13fff0c00` = slot A, `0x13fff0c19` = slot B):

```
13fff0c00  48 83 ec 28              sub  rsp,0x28
13fff0c03  48 8d 0d 25 9d 37 00     lea  rcx,[0x14036a930]      ; "amHwMonitor"
13fff0c0a  ff 25 00 00 00 00        jmp  qword ptr [rip+0]      ; -> 0x1402b520b (origineel lichaam)
13fff0c11  0b 52 2b 40 01 00 00 00  (qword 0x1402b520b)
13fff0c19  ff 25 00 00 00 00        jmp  qword ptr [rip+0]      ; <- hier komt de game binnen
13fff0c1f  90 05 d0 f6 ff 6f 00 00  (qword 0x6ffff6d00590 = KERN)
```
**[GEMETEN]**

**De 18 doelen in de kern — [GEMETEN] uit de pagina, en hun code is [GEMETEN] gelezen via
`/proc/<pid>/mem` in `/tmp/lm-meet-kern/`** (de kern-DLL is zelf ook gepakt — zijn `.text` heeft
`SizeOfRawData = 0` — dus uit het bestand lezen geeft rommel; dat heb ik geprobeerd en het klopte
niet):

| # | spel-functie | spel-adres | slot B | **kern-implementatie** |
|---|---|---|---|---|
| 1 | `amDongleBillingGetKeychipId` | `0x1402b3b70` | `0x13fff0fd4` | `0x6ffff6cfdd80` |
| 2 | `amDongleGetGameId` | `0x1402b3d30` | `0x13fff0ed4` | `0x6ffff6cfe040` |
| 3 | `amDongleGetModelType` | `0x1402b3ff0` | `0x13fff0e94` | `0x6ffff6cfe0a0` |
| 4 | `amDongleGetNetworkAddress` | `0x1402b40b0` | `0x13fff0e54` | `0x6ffff6cfe0e0` |
| 5 | `amDongleInit` | `0x1402b4170` | `0x13fff0f54` | `0x6ffff6cfe1f0` |
| 6 | `amDongleIsAvailableInternal` | `0x1402b4270` | `0x13fff0bd3` | `0x6ffff6c97d50` |
| 7 | `amDongleSetupKeychip` | `0x1402b4320` | `0x13fff0f94` | `0x6ffff6cfe2a0` |
| 8 | `amDongleUpdate` | `0x1402b4740` | `0x13fff0f19` | `0x6ffff6cfe3e0` |
| 9 | `amHmExit` | `0x1402b50f0` | `0x13fff0c99` | `0x6ffff6d00520` |
| 10 | `amHmGetCautionFlag` | `0x1402b5180` | `0x13fff0d19` | `0x6ffff6d00550` |
| 11 | `amHmInit` | `0x1402b5200` | `0x13fff0c19` | `0x6ffff6d00590` |
| 12 | `amHmResetCautionFlag` | `0x1402b55a0` | `0x13fff0cd9` | `0x6ffff6d005c0` |
| 13 | `amHmUpdate` | `0x1402b56f0` | `0x13fff0c54` | `0x6ffff6caff50` |
| 14 | `amMasterExitInternal` | `0x1402b5860` | `0x13fff0dd7` | `0x6ffff6d062e0` |
| 15 | `amMasterInit` | `0x1402b5cb0` | `0x13fff0e14` | `0x6ffff6d063d0` |
| 16 | `amMasterSetBackupEventLogParam` | `0x1402b6050` | `0x13fff0d55` | `0x6ffff6d06320` |
| 17 | `amMasterUpdate` | `0x1402b6160` | `0x13fff0d99` | `0x6ffff6cc2130` |
| — | (geen `am`) | `0x1400485a` | `0x13fff0b8e` | `0x6ffff6c979a0` |

**Wat de kern-implementaties doen — [GEMETEN]:**

Drie zijn een pure "return 0"-stub, zonder enige log:

```
amHmUpdate                     (0x6ffff6caff50)  89 4c 24 08 33 c0 c3
                              = mov [rsp+8],ecx ; xor eax,eax ; ret
amMasterUpdate                 (0x6ffff6cc2130)  88 4c 24 08 33 c0 c3
                              = mov [rsp+8],cl  ; xor eax,eax ; ret
amMasterSetBackupEventLogParam (0x6ffff6d06320)  44 89 4c 24 20 44 89 44 24 18 89 54 24 10
                                                 89 4c 24 08 33 c0 c3
                              = argumenten bewaren ; xor eax,eax ; ret
```

De andere volgen één patroon: **argumenten bewaren → `lea rcx,[<string in de kern>]` → een
log-aanroep → `xor eax,eax; add rsp,0x28; ret`**. Voorbeelden met de gemeten bytes:

```
amDongleGetGameId   (0x6ffff6cfe040)  89 54 24 10 48 89 4c 24 08 48 83 ec 28
                                      48 8d 0d cc e2 12 00   ; lea rcx,[0x6ffff6e2c320]
                                      e8 72 2d 4e 02         ; log
                                      ...  xor eax,eax ; add rsp,0x28 ; ret   -> 0
amDongleGetModelType(0x6ffff6cfe0a0)  48 8d 0d ec e1 12 00   ; lea rcx,[0x6ffff6e2c2a0]
                                      e9 f9 87 3c 02         ; jmp (staart)
                                      ...  xor eax,eax ; add rsp,0x28 ; ret   -> 0
amDongleInit        (0x6ffff6cfe1f0)  48 83 ec 28 48 8d 0d 5d e0 12 00 e9 51 51 54 02
                                      ...  xor eax,eax ; add rsp,0x28 ; ret   -> 0
```

**De enige uitzondering, en dat is een echt antwoord — [GEMETEN]:**

```
amDongleGetNetworkAddress (0x6ffff6cfe0e0)
   6ffff6cfe0e0  89 54 24 10              mov  [rsp+0x10],edx
   6ffff6cfe0e4  48 89 4c 24 08           mov  [rsp+0x8],rcx
   6ffff6cfe0e9  48 83 ec 28              sub  rsp,0x28
   6ffff6cfe0ed  0f b6 05 0d dd 28 00     movzx eax,BYTE PTR [rip+0x28dd0d]   ; 0x6ffff6f8be01
   ...
                 mov  DWORD PTR [rax],0x1a8c0     ; <-- schrijft 192.168.1.0
                 xor  eax,eax ; add rsp,0x28 ; ret   -> 0
```

`0x1a8c0` little-endian is `c0 a8 01 00` = **192.168.1.0**. De kern schrijft dus een **vast**
netwerkadres in de uitvoerpointer van de game en geeft 0 terug. **[GEMETEN]**

**De log-strings in de kern heb ik NIET gelezen.** Hun adressen zijn gemeten
(`0x6ffff6e2c320`, `0x6ffff6e2c2a0`, `0x6ffff6e2c338`, `0x6ffff6e2c254`, …) maar die liggen in
`.rdata` van de kern-module, en die heb ik niet gedumpt. **[GEMETEN dat ze er zijn; onbekend wat er
staat]**

**Het dongle-object is leeg — [GEMETEN]:**

`[0x14049cc68]` = `0x1404de050` op t=12, 16, 20 en 25 s, en de **512 bytes op dat adres zijn
allemaal nul**, inclusief `+0x1c0` (game-id), `+0x1c8` (netwerkadres), `+0x1cc` (keychip-id),
`+0x110`, `+0x114` en `+0x1b4`. Het `amMaster`-object op `0x1404de700` en de drie `amHm`-globalen
zijn ook nul. **[GEMETEN]** De kern vult dus **geen** object; hij vervangt de aanroepen.

---

## 5. Wat de drop-in moet doen — specificatie

**Vorm:** een per-game drop-in in `patches/LuigisMansion/dropin/` met een `dropin.ini`, **niet** in
de lader. De hasphook-specialist bouwt het mechanisme dat de 17 prologues omzet; hieronder staan de
**gegevens** die het mechanisme moet leveren.

**Wat de drop-in in essentie moet doen:** de 17 spel-functies hun prologue laten overschrijven met
`e9 <rel32>` naar een eigen stub, precies zoals de gesloten kern doet — en die stub moet **dezelfde
teruggeefwaarde en hetzelfde neveneffect** hebben als de kern-implementatie hieronder.

### 5.1 Per functie: adres, kern-doel, wat de vervanger doet, wat de drop-in moet teruggeven

Alles in deze tabel is **[GEMETEN]** behalve waar "AFGELEID" staat.

| spel-functie | spel-adres | kern-doel | wat de kern doet (gemeten bytes) | terug | neveneffect |
|---|---|---|---|---|---|
| `amDongleInit` | `0x1402b4170` | `0x6ffff6cfe1f0` | `sub rsp,0x28; lea rcx,[0x6ffff6e2c254]; jmp <log>` → `xor eax,eax; ret` | **0** | — |
| `amDongleIsAvailableInternal` | `0x1402b4270` | `0x6ffff6c97d50` | `sub rsp,0x38; lea rcx,[…]; jmp <log>` → `ret` (waarde niet met zekerheid gelezen) | **0** (AFGELEID) | — |
| `amDongleSetupKeychip` | `0x1402b4320` | `0x6ffff6cfe2a0` | `sub rsp,0x28; lea rcx,[…]; call <log>`; `33 c0 c3` op +0x191 | **0** | — |
| `amDongleUpdate` | `0x1402b4740` | `0x6ffff6cfe3e0` | `sub rsp,0x28; lea rcx,[…]; jmp` → `xor eax,eax; ret` | **0** | — |
| `amDongleGetGameId` | `0x1402b3d30` | `0x6ffff6cfe040` | log → `xor eax,eax; add rsp,0x28; ret` | **0** | **schrijft niets** in `rcx` |
| `amDongleGetModelType` | `0x1402b3ff0` | `0x6ffff6cfe0a0` | log → `xor eax,eax; ret` | **0** | **schrijft niets** |
| `amDongleGetNetworkAddress` | `0x1402b40b0` | `0x6ffff6cfe0e0` | `mov dword [rax],0x1a8c0` → `xor eax,eax; ret` | **0** | **`*(uint32*)rcx = 192.168.1.0`** (`c0 a8 01 00`) |
| `amDongleBillingGetKeychipId` | `0x1402b3b70` | `0x6ffff6cfdd80` | stack-cookie, `lea rcx,[0x6ffff6e2c338]`, `call <log>` | **0** (AFGELEID) | **schrijft niets** |
| `amHmInit` | `0x1402b5200` | `0x6ffff6d00590` | log → `xor eax,eax; ret` | **0** | — |
| `amHmExit` | `0x1402b50f0` | `0x6ffff6d00520` | log → `xor eax,eax; ret` | **0** | — |
| `amHmUpdate` | `0x1402b56f0` | `0x6ffff6caff50` | `mov [rsp+8],ecx; xor eax,eax; ret` | **0** | — |
| `amHmGetCautionFlag` | `0x1402b5180` | `0x6ffff6d00550` | log → `xor eax,eax; ret` | **0** | **schrijft niets** |
| `amHmResetCautionFlag` | `0x1402b55a0` | `0x6ffff6d005c0` | log → `xor eax,eax; ret` | **0** | — |
| `amMasterInit` | `0x1402b5cb0` | `0x6ffff6d063d0` | log → `xor eax,eax; ret` | **0** | — |
| `amMasterUpdate` | `0x1402b6160` | `0x6ffff6cc2130` | `mov [rsp+8],cl; xor eax,eax; ret` | **0** | — |
| `amMasterExitInternal` | `0x1402b5860` | `0x6ffff6d062e0` | log → `xor eax,eax; ret` | **0** | — |
| `amMasterSetBackupEventLogParam` | `0x1402b6050` | `0x6ffff6d06320` | argumenten bewaren; `xor eax,eax; ret` | **0** | — |

**Samengevat, en dit is het hele antwoord:** de kast-antwoorden bestaan uit **"geslaagd" (0)** plus
**één vast netwerkadres 192.168.1.0**. Er is **geen** game-id, **geen** modeltype en **geen**
keychip-id gemeten — de kern schrijft die niet en het dongle-object blijft leeg.

### 5.2 Hoe je dat antwoord levert zonder iets te verzinnen

1. **Zet de prologue om** naar een stub met dezelfde semantiek als de kern: argumenten bewaren
   (de eerste twee args worden op `[rsp+8]` en `[rsp+0x10]` gezet, net als de kern doet — dat is
   zichtbaar in élke kern-implementatie) en dan `eax = 0` en `ret`.
2. **Alleen voor `amDongleGetNetworkAddress`** ook `*(uint32_t*)rcx = 0x1a8c0` schrijven.
   Dat is de enige waarde die de kast blijkbaar geeft, en hij is **letterlijk gemeten**.
3. **Voor de andere 16 niets in de uitvoerpointer schrijven.** De kern doet dat ook niet, en de game
   leest die buffers dus niet (of accepteert de oude inhoud).
4. **Geen waarden verzinnen.** Er is geen bron voor een game-id, modeltype of keychip-id: niet in het
   spelbestand, niet in het beeld, niet in het dongle-object, en de kern schrijft ze niet. Als er
   later een echte keychip-uitlezing komt (zoals bij OG in
   `patches/OG/dropin/ravengun-hasp/OG/dongle.json`), is dat de plek om ze in te vullen — maar tot
   dat er is, is "0 terug, niets schrijven, behalve 192.168.1.0" de volledige, gemeten specificatie.

### 5.3 De uitkomst van de dichte meting, en wat die betekent

Zie §0: de dichte bemonstering **weerlegt** mijn eerdere waarschuwing. Onder onze kern bereikt `.text`
**11,20 % nul op t=4,7 s** — dezelfde volledig uitgepakte toestand als de gesloten kern op t=10,0 s —
en het spel sterft pas **~1,3 s later**. Het spel komt dus voorbij de pakker, en het enige gemeten
verschil in het spelbeeld op dat moment is dat de 17 prologues **origineel** zijn in plaats van `e9`.
**Deze drop-in is dus de juiste route.** Wat nog [AFGELEID] is: dat die 17 prologues ook werkelijk de
dood *veroorzaken*. Het beslissende experiment is de drop-in zelf: zet de prologues om en kijk of het
spel dan langer dan 6 s leeft. De drempel is scherp — **de gesloten kern haalt 300 s, onze kern
5,96 s** — dus elke uitkomst boven ~10 s is al een treffer.

**De am-route is met een meting NIET uitgesloten.** Zie `PAKKER-FASE.md` voor het volledige bewijs:
de pakker loopt onder onze kern en maakt zichzelf af (`.text` 11,20 % nul op t=4,7 s, en daarna nog de
afsluitende `NtProtectVirtualMemory`-aanroepen die de secties op hun eindrechten zetten), en de
geheugenbescherming is **identiek** op beide kernen (`.text` en `.capcom0` allebei `0x40` → `0x20`,
geen enkele fout). Er is dus geen pakker-blokkade die de am-route overbodig zou maken.
**Let op:** er circuleert een verkeerde lezing die uit `/tmp/lm-meet-onze/meting-onze.txt` komt — dat
is een venster van t=0,1…4,0 s, dus **vóór** het uitpakken. Het juiste bestand is
`/tmp/lm-dicht-onze/meting-onze.txt`. Zie `PAKKER-FASE.md` §7.

---

## 6. Wat er nu gemeten is, en met welke bestanden

De meting is gedaan met de bewezen opzet (`lm-meet.sh` = de opzet van `b0-momenten.sh`: bench-check,
headless gamescope, `scripts/launch-direct.py LuigisMansion --run`, kern-sha uit het log,
kern-terugzetten, opruimen). **Per bestand welk van de twee runs het was:**

| bestand | run | inhoud |
|---|---|---|
| `/tmp/lm-meet-gesloten/tramp-10.0.bin` | **GESLOTEN kern** `f5c7532d…` | de trampolinepagina `0x13fff0000`, 4096 B, 610 niet-nul — de 18 detours |
| `/tmp/lm-meet-gesloten/meting-huidig.txt` | GESLOTEN kern | alle momenten; pagina leesbaar vanaf t=10 s (EIO daarvoor) |
| `/tmp/lm-meet-kern/kern-<naam>-12.0.bin` | **GESLOTEN kern** | 0xC0 B van elk van de 18 kern-implementaties, via `/proc/<pid>/mem` |
| `/tmp/lm-meet-kern/meting-huidig.txt` | GESLOTEN kern | idem, met de prologues op `e9 …` |
| `/tmp/lm-meet-gesloten/dongle-*.bin`, `amMaster-*.bin` | GESLOTEN kern | de objecten op t=12/16/20/25 s — **allemaal nul** |
| `/tmp/lm-meet-onze/meting-onze.txt` | **ONZE kern** `fe2ecf8c…` (sha uit het log) | de prologues op **`00 00 00 00 00`** op t=0,1 … 4,0 s; pagina nooit leesbaar; proces weg op **t=5,72 s** |

De kern stond na elke run terug op `f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`,
er is geen `VACUUM.exe` en geen `gamescope` blijven staan. **[GEMETEN]**

### De ene meting die nog moet

De vraag is nu niet meer *wat* de antwoorden zijn (die heb ik), maar **of de `am`-laag onder onze kern
ooit bereikt wordt**. Concreet:

1. **ONZE kern, dicht bemonsterd van t=4,0 s tot het einde** (bijvoorbeeld elke 0,1 s), en per moment:
   het percentage nul in `.text` (0x140001000, 3496500 B) plus de 5 bytes op `0x1402b4170`.
   * Wordt `.text` ooit gevuld → het spel haalt het uitpakken, en dan is de `am`-laag wél relevant.
   * Blijft `.text` nul tot de dood → het spel sterft **in de pakker**, vóór de `am`-laag, en dan is
     een `am`-drop-in niet de oplossing.
2. **Dezelfde reeks op de GESLOTEN kern** als ijkpunt (die heb ik deels: nul op t=2…5 s, uitgepakt
   op t=10 s).
3. Optioneel en goedkoop: de log-strings in de kern lezen op `0x6ffff6e2c254`, `0x6ffff6e2c2a0`,
   `0x6ffff6e2c320`, `0x6ffff6e2c338` (`.rdata` van de kern-module), zodat de drop-in dezelfde
   logregels kan produceren.

**Wat de uitkomst per geval betekent:**

* **`.text` wordt gevuld onder onze kern, en dan volgt de dood** → het uitpakken lukt; de dood komt
  daarna. Dan is de volgende verdachte de `am`-laag of de anti-tamper ná het uitpakken, en is de
  drop-in van §5 de juiste route.
* **`.text` blijft nul** → de dood zit in de pakker-fase. Dan moet het werk naar de
  `NtProtectVirtualMemory`-route (waarom de gesloten kern die aanhaakt en onze kern niet), en niet
  naar de `am`-laag.
* **De prologues staan onder onze kern ooit op `e9`** → onze kern patcht ze wél, en dan is de
  hypothese in §4 onjuist. Dan moet de oorzaak ergens anders gezocht worden.

---

## 7. Wat ik niet gedaan heb en niet weet

* **De log-strings in de kern** (`0x6ffff6e2c254`, `0x6ffff6e2c2a0`, `0x6ffff6e2c320`,
  `0x6ffff6e2c338`, …) zijn niet gelezen: die liggen in `.rdata` van de kern-module en dat heb ik niet
  gedumpt. De adressen zijn wel gemeten.
* **Van één kern-implementatie is de teruggeefwaarde AFGELEID en niet direct gelezen**:
  `amDongleIsAvailableInternal` (`0x6ffff6c97d50`). In een venster van 1024 B komt noch `33 c0 c3`
  noch `33 c0 48 83 c4` voor, dus zijn `ret` ligt verder dan 1024 B van het begin. **Bij de andere 16
  staat `xor eax,eax` letterlijk vóór de `ret`** — [GEMETEN], met de offset in het venster:
  * `33 c0 c3` (`xor eax,eax; ret`): `amDongleBillingGetKeychipId` +0x17c, `amDongleGetGameId` +0x54,
    `amDongleGetModelType` +0x391, `amDongleGetNetworkAddress` +0x351, `amDongleInit` +0x241,
    `amDongleSetupKeychip` +0x191, `amDongleUpdate` +0x51, `amHmUpdate` +0x04,
    `amMasterExitInternal` +0x52, `amMasterSetBackupEventLogParam` +0x12, `amMasterUpdate` +0x04;
  * `33 c0 48 83 c4 …` (`xor eax,eax; add rsp,imm8; ret`): `amHmExit` +0x29, `amHmGetCautionFlag`
    +0x32, `amHmInit` +0x29, `amHmResetCautionFlag` +0x2a, `amMasterInit` +0x29.
* **Ik heb onder onze kern niet bemonsterd tussen t=4,0 s en de dood op t=5,72 s.** Of `.text` in die
  laatste 1,7 s nog gevuld is geraakt, weet ik dus niet. Dat is precies de meting van §6.
* **Het foutpad `0x0004d39a` van de opzetroutine is de gewone epilogue** (gemeten), dus een mislukte
  dongle-controle beëindigt het spel daar niet. Waar de aanroeper van `0x0004d250` daarna naartoe
  gaat, en waar de vlag `edi` uit `0x001349d8` naartoe leidt, heb ik **niet** gevolgd.
* **Geen verband gemeten tussen de `am`-laag en het exitpunt `VACUUM.exe+0xf1374d`.** Zie
  `EXITBESLUIT.md`: dat exitpunt loopt via een VM-entry. De verbinding is een hypothese.
* De naamtoewijzing van rij 12 en 14 in §1 is een beste toewijzing, geen bewijs.
* **Ik heb geen drop-in gebouwd** — alleen de specificatie in §5. Dat is bewust: het mechanisme wordt
  door de hasphook-specialist gebouwd.
