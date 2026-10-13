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

## 0. Samenvatting in vijf regels

1. De bewering **"17 functies met een gewijzigde prologue" klopt precies** — het zijn 17 van de 18
   prologues die naar één pagina springen: **`0x13fff0000`, 4096 B, anoniem, `rwxp`**. **[GEMETEN]**
2. Die 17 zijn `amDongle*` (8), `amHm*` (5), `amMaster*` (4). De namen komen **niet** uit een
   exporttabel maar uit **log-tag-strings** in `.rdata` die elk vanuit hun eigen functie worden
   aangeroepen. **[GEMETEN]**
3. De gewijzigde prologue is overal exact **`e9 <rel32>` = 5 bytes**; de rest van het functielichaam
   staat er nog. De eerste 5 originele bytes zijn dus weg. **[GEMETEN]**
4. De gesloten kern **vervangt de functie in zijn geheel** (via de trampolinepagina). Er staat
   **geen enkele** aanwijzer naar de kern-code in het spelbeeld: 0 van 19.259.384 posities. **[GEMETEN]**
5. Wat de kast antwoordt (game-id, modeltype, netwerkadres, keychip-id) zit in de **velden van het
   dongle-object**, gevuld door de dongle-I/O. Die waarden zijn **niet statisch te vinden**; ze
   moeten uit de trampolinepagina of uit het dongle-object gelezen worden. **Dat is één gerichte
   meting** (§6). **[GEMETEN dat ze ontbreken]**

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

**Wat daaruit volgt [AFGELEID, sterk ondersteund]:**

De gesloten kern **vervangt de 17 `am`-functies in hun geheel** door eigen implementaties, en de
enige manier om dat in een `r-xp`-sectie te doen is de pagina schrijfbaar maken. Dat verklaart
precies de door de coordinator gemeten observatie dat de gesloten kern `ntdll!NtProtectVirtualMemory`
en `ZwProtectVirtualMemory` aanhaakt en **onze kern niet**:

> onze kern maakt `.text` niet schrijfbaar → de 17 prologues blijven origineel → het spel roept de
> echte dongle-code aan → er is geen dongle → `amDongleIsAvailableInternal` faalt → het spel stopt.

Dat is een **samenhangende verklaring** voor het verschil tussen de twee kernen. Ik heb hem **niet
direct gemeten** (daarvoor moet je in beide runs de bytes op `0x2b4170` vergelijken — zie §6, stap 3).

**Wat ik NIET weet:** wat de stubs op `0x13fff0000` doen. Die pagina valt **buiten** de dump (de dump
begint bij `0x140000000`, de pagina ligt op `0x13fff0000`), en er is geen ander artefact dat hem
dekt: `/tmp/vacuum-dump/` bevat alleen de acht PE-secties, en `/tmp/b0-geheugen-{b,c,d}/` bevatten
alleen `image-gesloten.bin` vanaf de ImageBase. **[GEMETEN dat het ontbreekt]** Ik verzin hier niets:
**zonder die pagina is de inhoud van de vervanging niet statisch te bepalen.**

---

## 5. Wat ONZE kern of een drop-in moet leveren — specificatie

De vorm is: **een per-game drop-in in `patches/LuigisMansion/`**, niet een wijziging in de kern.

### 5.1 Wat de drop-in moet doen

De 17 functies moeten **dezelfde signature en dezelfde teruggeefwaarden** krijgen als de echte
`am`-laag, maar met de kast-gegevens in plaats van een echte dongle. Per functie, [GEMETEN] tenzij
anders vermeld:

| functie | gevraagde invoer | terug te geven | neveneffect op het dongle-object |
|---|---|---|---|
| `amDongleInit` | — | `0` bij succes; `-4` als `[obj] != 0` | `memset(obj,0,0x200)`; daarna moeten de velden `+0x1c0`, `+0x1c8`, `+0x1cc` gevuld zijn |
| `amDongleIsAvailableInternal` | — | `0` als beschikbaar; `-2` bij `0x81000003`; `-1` anders | `[obj+0x110] = 0x40`, `[obj+0x114] = 1` |
| `amDongleSetupKeychip` | keychip-parameter | `0` | — (lichaam niet nagelezen) |
| `amDongleUpdate` | — | `0` | — (lichaam niet nagelezen) |
| `amDongleGetGameId` | `rcx` = uitvoerpointer (5 B) | `1`/`0` bij succes; `-2` bij `rcx == 0`; `-3` als `[obj]==0`; `-1` als `[obj+0x1b4]==0` | `[obj+0x110] = 4` |
| `amDongleGetModelType` | `rcx` = uitvoerpointer | idem | — (veld onbekend) |
| `amDongleGetNetworkAddress` | `rcx` = uitvoerpointer (4 B) | idem | `[obj+0x110] = 9` |
| `amDongleBillingGetKeychipId` | `rcx` = uitvoerpointer (17 B) | idem | `[obj+0x110] = 0xb` |
| `amHmInit` | — | `0`; `-4` als al geïnitialiseerd | `amHm`-global `0x1404de6b0` |
| `amHmExit`, `amHmUpdate` | — | `0`; `-3` als niet geïnitialiseerd | idem |
| `amHmGetCautionFlag`, `amHmResetCautionFlag` | vlag | `0` | idem |
| `amMasterInit` | — | `0`; `-4` als al geïnitialiseerd | `memset(obj+0xc,0,0x400)`, `[obj+4]=0` |
| `amMasterUpdate`, `amMasterExitInternal`, `amMasterSetBackupEventLogParam` | — | `0` | `amMaster`-object `0x14049ccd8` |

De negatieve codes zijn `am`-foutcodes: `-1` = niet beschikbaar, `-2` = ongeldig argument,
`-3` = niet geïnitialiseerd, `-4` = al geïnitialiseerd; `0x81000003` is de "dongle niet gevonden"-code
van de onderliggende `amy`-laag. **[AFGELEID]** uit de plaatsen waar ze gezet worden.

### 5.2 Wat we NIET hebben, en waar het vandaan moet komen

**De antwoorden zelf.** De getters kopiëren uit `obj+0x1c0` (5 B game-id), `obj+0x1c8` (4 B
netwerkadres) en `obj+0x1cc` (17 B keychip-id). Die velden worden gevuld door de dongle-I/O. Wij
hebben geen dongle en de waarden staan niet in het spelbestand of in het beeld. **[GEMETEN]**

Er zijn precies twee bronnen denkbaar, en ik noem ze met de bron erbij:

1. **De trampolinepagina van de gesloten kern** (`0x13fff0000`, 4096 B). Daar staat wat de kast
   antwoordt, in de vorm van de vervangende code. **Dit is nog niet gelezen** — de pagina valt buiten
   elke bestaande dump. Dit is de eerstvolgende meting (§6).
2. **Een echte keychip/dongle-uitlezing**, zoals bij OG is gedaan
   (`patches/OG/dropin/ravengun-hasp/OG/dongle.json`, met de opmerking dat de regio-/printplaatwaarde
   de software-emulatie van dongle en PIC is die de TeknoParrot-kern zelf doet). Voor Luigi's Mansion
   Arcade is zo'n bestand er **niet**.

Ik heb **geen** `keychip.appboot.*`-opdrachtstrings in het beeld gevonden, dus het OG-protocol is hier
niet van toepassing. **[GEMETEN]**

**Ik verzin geen waarden.** Zonder bron 1 of 2 is de specificatie compleet in *vorm* (welke functie
wat teruggeeft) maar incompleet in *inhoud* (welke bytes de kast antwoordt).

---

## 6. Draaiboek voor de ene meting die dit afmaakt

**Doel:** de inhoud van `0x13fff0000` en de velden van het dongle-object, in één run met de GESLOTEN
kern. Daarna is de specificatie compleet.

**Opzet:** de bewezen runmethode, niet een eigen wine-aanroep:
`python3 ~/projects/ravengun/scripts/launch-direct.py LuigisMansion --run` met de headless gamescope
zoals `b0-run.sh` / `b0-momenten.sh` die opzetten (prefix, werkmap, hulpketen, headless display), en
achteraf de kern-sha uit het launch-direct-logboek controleren. Verwachte sha gesloten:
`f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`.

**Wat te lezen uit `/proc/<pid>/mem`** (het spelproces, ImageBase `0x140000000`):

| stap | adres | lengte | waarom |
|---|---|---|---|
| 1 | `0x13fff0000` | `0x1000` | de trampolinepagina; ontleed de 18 stubs op offsets `0xb8e 0xbd3 0xc19 0xc54 0xc99 0xcd9 0xd19 0xd55 0xd99 0xdd7 0xe14 0xe54 0xe94 0xed4 0xf19 0xf54 0xf94 0xfd4`. Elke stub is naar verwachting óf de vervangende code zelf, óf `jmp`/`mov rax,…; jmp rax` naar de kern (`0x6ffff6b61000..0x6ffff6ddc000`). |
| 2 | `0x14049cc68` (8 B) → daarna dat adres, `0x200` B | | het dongle-object. Lees `+0x1c0` (5 B game-id), `+0x1c8` (4 B netwerkadres), `+0x1cc` (17 B keychip-id), `+0x110`, `+0x114`, `+0x1b4`. **Dit zijn de antwoorden van de kast.** |
| 3 | `0x1402b4170`, `0x1402b4270`, `0x1402b4320` (5 B elk) | | controle dat de prologues inderdaad `e9` zijn. Daarna dezelfde drie adressen in een run met ONZE kern: staan daar de originele bytes, dan is de causale keten van §4 **direct bewezen** in plaats van afgeleid. |
| 4 | `0x14049ccd8` (8 B) → `0x400` B; en `0x1404de6b0`, `0x1404de6c0`, `0x1404de6d0` | | de `amMaster`- en `amHm`-globalen, voor de vier `amMaster`- en vijf `amHm`-functies. |

**Wat de uitkomst per geval betekent:**

* **Stubs bevatten de waarden zelf** (bijv. `mov dword [rcx], imm32`) → we lezen de kast-antwoorden
  direct uit de disassembly; de drop-in kan ze letterlijk overnemen.
* **Stubs zijn sprongen naar de kern** → de waarden komen uit de kern-code; dan moet je in de
  kern-DLL (of in het door de kern gevulde dongle-object, stap 2) kijken. Stap 2 is dan de bron.
* **Stap 2 toont een gevuld object** → dat is de schoonste bron: het zijn de bytes die de game zelf
  zou hebben gelezen, dus precies wat de drop-in moet teruggeven.
* **Stap 3 toont originele bytes onder onze kern** → de keten van §4 is bewezen en de conclusie
  "onze kern moet deze 17 prologues (of hun equivalent) overnemen" staat vast.

**Opruimen na de run:** geen `VACUUM.exe` en geen `gamescope` laten staan (een rest vergiftigt de
volgende meting), en de runtime-kern terugzetten op `f5c7532d…`.

---

## 7. Wat ik niet gedaan heb en niet weet

* **Geen run gedaan.** De trampolinepagina is dus niet gelezen (§6).
* **Geen causale meting** tussen de `am`-laag en het exitpunt `VACUUM.exe+0xf1374d`. Wat ik wél weet
  staat in `EXITBESLUIT.md`: dat exitpunt loopt via een **VM-entry** (`push imm32; call 0x140e7438b`,
  en die springt een VM-interpreter in). Ik heb **geen** bewijs dat de `am`-laag die exit veroorzaakt,
  en ik heb ook geen bewijs dat ze het niet doet. De verbinding is een **hypothese**.
* **`amDongleGetModelType`**: welk veld gekopieerd wordt, is niet leesbaar (de 5-byte patch viel
  midden in de originele prologue).
* **`amDongleSetupKeychip` en `amDongleUpdate`**: lichamen niet regel voor regel nagelezen; alleen de
  aanwezigheid van hun log-tags en hun startadres zijn gemeten.
* De naamtoewijzing van rij 12 en 14 in §1 is een beste toewijzing, geen bewijs.
* **Het foutpad `0x0004d39a` van de opzetroutine is niet gevolgd**, en ook niet waar de vlag `edi` uit
  `0x001349d8` naartoe leidt. Of een mislukte `amDongleInit` werkelijk tot de exit met 32 leidt, is
  dus **niet gemeten**. Dat is de eerstvolgende statische stap: `0x0004d39a` uitlezen en kijken of
  daar een foutcode gezet of een afsluitroute gekozen wordt.
* Ik heb **geen** verband gemeten tussen `amDongleIsAvailableInternal` en het exitpunt
  `VACUUM.exe+0xf1374d` (zie `EXITBESLUIT.md`).
