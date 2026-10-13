# EXITBESLUIT.md — waarom `VACUUM.exe` zichzelf met 32 beëindigt

**Soort werk:** statische analyse op de geheugendump onder de GESLOTEN kern. **Geen run gedaan.**
Alleen gelezen.

**Bron:** `/tmp/b0-geheugen-b/image-gesloten.bin`, 19.259.392 B, **offset = RVA** bij ImageBase
`0x140000000`, sha256 `11bb710b7c71686b`. Spelbestand read-only op de NAS (3.436.544 B).
Kern in die run: gesloten `f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`.

**Etiketten.** **[GEMETEN]** = direct uit bytes gelezen, met adres en bytes erbij.
**[AFGELEID]** = gevolgtrekking. Wat ik niet weet staat er letterlijk.

---

## 0. Antwoord in het kort

* De exit wordt **niet** vanuit gewone code aangeroepen, maar vanuit een **VM-entry**:
  `push imm32; call 0x140e7438b`, en `0x140e7438b` springt een **interpreter** in. **[GEMETEN]**
* Die interpreter is een **VM** (VMProtect-achtig): alle registers worden bewaard, er wordt een
  VM-frame van `0x180` bytes opgezet, er is een bytecode-aanwijzer (`rsi`), een rollende
  ontsleutelsleutel (`rbx`) en een virtuele instructieaanwijzer (`rdi`), en de handlers eindigen op
  `jmp` naar elkaar met `pushf`/`pop [rbp+…]` voor de vlaggen. **[GEMETEN]**
* De `imm32` vóór de aanroep **is een versleutelde aanwijzer naar de bytecode van die virtuele
  functie**. Voor deze ene familie is de ontsleuteling gevonden en **op alle 34 aanroepen
  geverifieerd**. **[GEMETEN]**
* De exit-tag `0xb63f49fe` ontsleutelt naar **`0x140e02403`** in `.capcom0`: dáár ligt de bytecode die
  de exit doet. **[GEMETEN]**
* **De beslissing zelf is niet statisch leesbaar.** Dat is geen onwil: de bytecode is versleuteld en
  de handlers zijn over het hele beeld verstrooid en verminkt. Hier houdt de statische analyse op.
  **[GEMETEN grens]**
* **Geen gemeten verband** met de `am`-laag (`AM-LAAG.md`). Dat de `am`-laag de oorzaak is, is een
  **hypothese**, geen meting. **[AFGELEID/onbekend]**

---

## 1. De gemeten keten vanaf het exitpunt

De hook op `ExitProcess`/`TerminateProcess`/… ving de aanroep met **terugkeeradres
`VACUUM.exe+0xf1374d`** (gemeten door de andere partij). Wat er in het geheugen op en rond dat adres
staat:

```
140f1373e  e8 48 0c f6 ff        call 0x140e7438b        ; vorige stub
140f13743  68 fe 49 3f b6        push 0xffffffffb63f49fe ; <-- de tag van DEZE stub
140f13748  e8 3e 0c f6 ff        call 0x140e7438b        ; <-- hier komt ExitProcess vandaan
140f1374d  68 be 4a 9f 8c        push 0xffffffff8c9f4abe ; volgende stub
140f13752  e8 34 0c f6 ff        call 0x140e7438b
```
**[GEMETEN]** — exact de bytes die de opdracht noemde, bevestigd in de dump.

De hulpfunctie:

```
140e7438b  41 55                 push r13
140e7438d  e9 1f c3 f7 ff        jmp  0x140df06b1
140df06b1  51                    push rcx
140df06b2  e9 f2 5f 04 00        jmp  0x140e366a9
```
**[GEMETEN]**

En dan vanaf `0x140e366a9` een lange keten die **alle registers bewaart** en de VM opzet:

```
140e366a9  push rbp / push rbx / push r8 / push rax / push r14 / push r9 / pushf
           push r12 / push r10 / push r11 / push r15 / push rdx / push rsi / push rdi
140e36714  48 8b b4 24 90 00 00 00   mov  rsi,[rsp+0x90]   ; de gepushte tag (8 B boven de retaddr)
140e36721  81 f6 4b 65 51 6d         xor  esi,0x6d51654b
140e3672b  0f ce                     bswap esi
140e36739  c1 c6 02                  rol  esi,0x2
140e36747  81 c6 95 68 2e 6c         add  esi,0x6c2e6895
140e3675e  48 b8 00 00 00 00 01 00 00 00   movabs rax,0x100000000
140e3676f  48 03 f0                  add  rsi,rax          ; -> 0x140xxxxxx
140e36774  48 8b ec                  mov  rbp,rsp
140e36777  48 81 ec 80 01 00 00      sub  rsp,0x180
140e3678b  48 83 e4 f0               and  rsp,0xfffffffffffffff0
140e36796  48 8b de                  mov  rbx,rsi          ; sleutel = bytecode-aanwijzer
140e367b0  4c 8d 25 49 98 1c bf      lea  r12,[rip-0x40e367b7]  ; = 0x100000000
140e367c4  48 8d 3d f9 ff ff ff      lea  rdi,[rip-7]      ; rdi = RIP van deze lea (VIP-basis)
140e367d0  8b 06                     mov  eax,[rsi]        ; bytecode-dword lezen
140e367d2  48 83 c6 04               add  rsi,4
140e367dd  31 d8                     xor  eax,ebx          ; ontsleutelen met de sleutel
140e367df  e9 eb 3c 00 00            jmp  0x140dea4cf     ; naar de volgende handler
```
**[GEMETEN]**

**Dit is de kern van het antwoord op vraag 1:** de hulpfunctie **lost geen doeladres op en
ontsleutelt geen foutcode** — hij **berekent de bytecode-aanwijzer** uit de gepushte `imm32` en
begint die bytecode te interpreteren.

---

## 2. Het is een VM — het bewijs, met adressen

Zes onafhankelijke kenmerken, allemaal **[GEMETEN]**:

1. **Volledige registerredding bij binnenkomst.** `0x140e366a9`–`0x140e366f7` pusht 16 GPR's +
   `pushf` (17 pushes, `0x88` bytes). Dat is geen functieprologue van een compiler; dat is een
   VM-context opbouwen.
2. **VM-frame.** `mov rbp,rsp; sub rsp,0x180; and rsp,-16` op `0x140e36774`. `rbp` wordt daarna
   gebruikt als basis van een registerbestand: `[rbp+0x0]`, `[rbp+0x4]`, `[rbp+0x8]`, `[rbp+0x10]`.
   **[GEMETEN]** bijvoorbeeld:
   ```
   140df05c6  89 5c 25 08   mov dword [rbp+0x8],ebx
   140df05d6  89 4c 25 04   mov dword [rbp+0x4],ecx
   140df05da  89 54 25 00   mov dword [rbp+0x0],edx
   140df0628  48 89 44 25 00  mov qword [rbp+0x0],rax
   ```
3. **Bytecode-aanwijzer met vaste stapgrootte.** `rsi` wordt met `add rsi,4` / `sub rsi,4`
   verplaatst en met `mov eax,[rsi]` gelezen — een stroom van dwords. **[GEMETEN]**
   ```
   140df05df  48 81 ee 04 00 00 00   sub  rsi,0x4
   140df05e6  8b 06                  mov  eax,[rsi]
   140df05e8  33 c3                  xor  eax,ebx
   140df05f0  05 b3 47 a2 65         add  eax,0x65a247b3
   140df05f5  c1 c0 02               rol  eax,0x2
   ```
4. **Rollende ontsleutelsleutel.** De sleutel `ebx` wordt per handler bijgewerkt met
   `bswap`/`rol`/`ror`/`add`/`xor` met vaste constanten. En de klassieke
   VMProtect-vermomming `push rbx; xor [rsp],eax; pop rbx` (dat is een verhulde `xor ebx,eax`):
   ```
   140e7436b  53            push rbx
   140e7436c  48 f7 db      neg  rbx
   140e7436f  31 04 24      xor  dword [rsp],eax
   140e74379  5b            pop  rbx
   ```
   **[GEMETEN]**
5. **Vlag-emulatie.** `pushf` / `pop qword [rbp+0x0]` — de echte RFLAGS wordt in het registerbestand
   opgeslagen, wat je alleen doet als je vlaggen virtueel doorgeeft. **[GEMETEN]**
   ```
   140df065b  9c                  pushf
   140df0662  8f 44 25 00         pop  qword [rbp+0x0]
   140df0693  9c                  pushf
   140df069b  8f 44 25 00         pop  qword [rbp+0x0]
   ```
6. **Virtuele instructieaanwijzer in `rdi`.** Handlers rekenen de VIP bij met het ontsleutelde
   operand: `movsxd rax,eax; add rdi,rax; jmp <volgende handler>`. **[GEMETEN]**
   ```
   140e74350  48 63 c0      movsxd rax,eax
   140e74354  48 03 f8      add  rdi,rax
   140e74357  e9 06 c1 01 00  jmp 0x140e90462
   ```

**Grenzen van wat leesbaar is.**

* **Leesbaar:** de handlers zelf (het zijn gewone x86-instructies, verstrooid over het hele beeld:
  `0x140df05c0`, `0x140dea4cf`, `0x140e366a9`, `0x140e74327`, `0x140e90462`, `0x140f0dbf7`, …), de
  VM-entry, en de **relatie tag → bytecode-start** (§3).
* **Niet leesbaar:** de **bytecode** (versleuteld; per instructie met een rollende sleutel), en
  daarmee de **vergelijking of aanroep die de beslissing neemt**. Er is geen tabel met handlers die
  je kunt uitlezen: elke handler eindigt op `jmp` naar een ander adres, en de handlers staan niet in
  een tabel. **[GEMETEN]**

**Eerlijke conclusie voor vraag 3:** ja, dit is een VM, en de beslissing die tot de exit leidt ligt
**in de bytecode**, niet in leesbare x86. Statische analyse van *die* beslissing is hier niet
mogelijk zonder de bytecode te ontsleutelen (dat zou een eigen implementatie van de VM-interpreter
vergen, of een dynamische trace).

**Overige bevestiging dat de VM niet de gewone code is:** het gebied rond `0xf13580` bevat wél echte,
zij het verminkte x86 (een `std::string`-achtige kopieerfunctie: SSO-controle
`cmp qword [rbx+0x18],0x10` op `0x140f13597`, `mov byte [rax+rdi],0` op `0x140f136a8`), met
tussengevoegde junk en **opaque sprongen** `e9 00 00 00 00` (`jmp +0`) op `0x140f1358c`,
`0x140f1359c`, `0x140f135aa`, `0x140f135c5`, `0x140f13622`, `0x140f1366d`, `0x140f13689`,
`0x140f13692`, `0x140f136a0`, `0x140f136db`. **[GEMETEN]** De functie eindigt op `ret` op
`0x140f136e0`; daarna `lea rcx,[0x14038c8c0]` = **`"string too long"`**, `call 0x1402e9da4`
(throw-hulp), `int3`, `jmp 0x140004992` (dat is `cc`-padding). **[GEMETEN]**

**De stubs staan NIET in een tabel.** Ik heb het hele beeld afgezocht:
* **0** absolute 8-byte aanwijzers naar een van de 26 stub-adressen;
* de enige `rel32`-verwijzingen naar `0xf13600..0xf14100` zijn junk (`call 0x140f13773`, dat is geen
  instructiegrens) of opaque sprongen binnen dezelfde functie.
**[GEMETEN]** De stubs worden dus **berekend** bereikt — precies wat je verwacht als de VIP (`rdi`)
ze aanwijst.

**De bytes tussen de stubs zijn geen code.** Tussen `0xf13752` en `0xf13777` staat
`1e 0e a7 d1 e4 ad 39 0b …` — dat bevat `push ds`/`push cs` (ongeldig in 64-bit) en
`shl esp,1` / `leave`, wat de stapel zou slopen. Er kan dus geen sprake zijn van "valt door" van de
ene stub in de andere. **[GEMETEN]**

---

## 3. De `imm32` is een versleutelde bytecode-aanwijzer — [GEMETEN]

Uit de code op `0x140e36714`–`0x140e3676f` volgt de ontsleuteling
`T(x) = ((bswap32(x ^ 0x6d51654b) rol 2) + 0x6c2e6895) & 0xffffffff`, en dan `+ 0x100000000`
om er een `0x140xxxxxx`-aanwijzer van te maken.

**Verificatie:** van de 34 `push imm32; call 0x140e7438b`-plaatsen in het beeld decodeert
**34 van 34** naar een adres **binnen het spelbeeld**. De kans dat dat per toeval gebeurt is ongeveer
`0,0045^34` — dat is geen toeval. **[GEMETEN]**

Voorbeeldwaarden:

| aanroep op RVA | tag | bytecode-aanwijzer | sectie |
|---|---|---|---|
| `0x00f13743` (de exit) | `0xb63f49fe` | `0x140e02403` | `.capcom0` |
| `0x00f136f3` | `0xa0264bfe` | `0x140e847cb` | `.capcom0` |
| `0x00f13739` | `0xf65a4fbe` | `0x140d69704` | `.pdata` |
| `0x00f1374d` | `0x8c9f4abe` | `0x140eda41c` | `.capcom0` |

**[GEMETEN]**

**Gevolg 1:** dezelfde hulpfunctie wordt vanaf tientallen plaatsen met een andere `imm32`
aangeroepen omdat **elke virtuele functie zijn eigen bytecode-start heeft**. Het is dus geen
foutcode-doorgeefluik en geen dispatch-tabel met opcodes.

**Gevolg 2:** de bytecode ligt **verspreid over het hele beeld**, ook in `.pdata`. Dat verklaart
waarom `.pdata` in het geheugen **geen geldige exceptiedirectory** is: 14345 `RUNTIME_FUNCTION`-
entries met onzinnige bereiken (bijvoorbeeld `[0x0, 0x81600002)`) en entropie **7,87 bits/byte**.
**[GEMETEN]** Er is dus **geen functietabel** voor de uitgepakte code; daarom heb ik in `AM-LAAG.md`
functiegrenzen via aanroepdoelen en log-tags bepaald.

**Gevolg 3:** dezelfde ontsleuteling werkt **alleen** voor de familie rond `0x140e7438b`. Voor de
andere families (`0x140dee933` met 118 aanroepen, `0x140f1ea77` met 56, `0x140ee0724` met 44,
`0x140e0d80e` met 39, …) decodeert `T` naar **0 van N** adressen binnen het beeld. Elke familie heeft
dus **eigen sleutelconstanten**. **[GEMETEN]** Ik heb die andere constanten **niet** gevonden.

Zij-opmerking: de laagste 6 bits van de tag zijn **constant per familie** (`& 0x3f == 0x3e` voor
`0x140e7438b`, `0x1b` voor `0x140dee933`, `0x0c` voor `0x140dc9a32`). Dat is een extra aanwijzing dat
de tag een *versleutelde aanwijzer met behouden laag bits* is en geen hash. **[GEMETEN]**

---

## 4. Waar de beslissing valt — het eerlijke antwoord

* **Waar:** in de bytecode op **`0x140e02403`** (`.capcom0`). **[GEMETEN]**
* **Welke vergelijking of aanroep:** **niet vast te stellen** met statische analyse. De bytecode is
  versleuteld en wordt per instructie met een rollende sleutel ontsleuteld; de handlers zijn
  verstrooid en verminkt. **[GEMETEN grens]**
* Wat **wel** vaststaat over de vorm: de aanroep gebeurt met `push imm32; call 0x140e7438b` waar de
  tag een bytecode-start is. Er is dus geen "`if (x == 32) ExitProcess(32)`" in leesbare code om aan
  te wijzen.
* **Waar de `32` vandaan komt:** **niet gemeten.** De `imm32` op `0xf13743` is de bytecode-aanwijzer
  `0xb63f49fe → 0x140e02403`; die waarde is **niet** de exitcode. De exitcode wordt binnen de
  bytecode gezet (of doorgegeven aan de `ExitProcess`-aanroep die de bytecode doet).
  **[GEMETEN dat de tag de exitcode niet is; onbekend waar de 32 vandaan komt]**

---

## 5. Draaiboek voor de dynamische meting (vraag 4)

**Waarom:** de beslissing zit in de bytecode. Een breekpunt op de VM-entry of op het terugkeeradres
geeft de *toestand* op het moment van de exit, en dat is genoeg om te bepalen welke tak genomen is
zonder de VM te hoeven naschrijven.

**Opzet.** De bewezen runmethode, niet een eigen wine-aanroep:
`python3 ~/projects/ravengun/scripts/launch-direct.py LuigisMansion --run` met de headless gamescope
zoals `b0-run.sh` / `b0-momenten.sh` die opzetten; achteraf de kern-sha uit het launch-direct-logboek
controleren. Twee runs: één met de gesloten kern (`f5c7532d…`), één met onze kern.

**Breekpunten en wat je wilt zien.**

| # | breekpunt | registers / stapelwoorden | wat de uitkomst betekent |
|---|---|---|---|
| 1 | `0x140f13748` (de `call` vóór het exitpunt) | `[rsp]` = terugkeeradres, `[rsp+8]` = de tag `0xb63f49fe`; `rdi` (VIP), `rsi` (bytecode-aanwijzer), `rbx` (sleutel), `rbp` (registerbestand) | bevestigt dat de exit via deze stub loopt en geeft de VM-toestand. `rsi` hoort rond `0x140e02403` te liggen. |
| 2 | `0x140e7438b` (de VM-entry) | idem, plus de 17 bewaarde registers op de stapel | idem; bruikbaar als 1 te vroeg vuurt. |
| 3 | **`0x140e02403`** (de bytecode van de exit) | — | als hier met een **hardware**-breekpunt niets vuurt, is de bytecode versleuteld en wordt dit adres alleen als *sleutelbasis* gebruikt; dan is de conclusie dat de beslissing elders in de bytecode valt. |
| 4 | `ExitProcess` (hook, bestaat al) | terugkeeradres + **`rcx` = de exitcode** | `rcx` lezen geeft de 32 hard terug, en de keten erboven (terugkeeradres `0xf1374d`) bevestigt de route. |

**Wat je per uitkomst concludeert:**

* **`rsi` bij het breekpunt ligt in de buurt van `0x140e02403`** → de ontsleuteling van §3 klopt ook
  dynamisch, en de exit komt uit die ene virtuele functie.
* **Verschilt de tag of het terugkeeradres tussen de twee kernen** → dan neemt het spel onder onze
  kern een **andere** route (een eerdere fout) en is de exit bij `0xf1374d` een *gevolg*, niet de
  oorspronkelijke beslissing. Dat is het scenario dat de `am`-hypothese zou ondersteunen.
* **Is de route identiek onder beide kernen** → dan ligt het verschil **vóór** dit punt, en moet je
  verder terug kijken (richting de `am`-laag, zie `AM-LAAG.md` §6).
* **Vuurt breekpunt 3 niet** → de bytecode is versleuteld; ga dan verder met een **trace van de
  handlers** (log elk `jmp`-doel) en reconstrueer de uitgevoerde handlerreeks. Dat is meer werk maar
  het is de enige route zonder de VM na te schrijven.

**Opruimen na de runs:** geen `VACUUM.exe` en geen `gamescope` laten staan; runtime-kern terugzetten
op `f5c7532d…`.

---

## 6. Bijvangst over het bestand (nuttig voor iedereen die hierna komt)

Allemaal **[GEMETEN]** uit de sectorkoppen van `VACUUM.exe` en uit de dump:

* Het bestand is **volledig gepakt**. `SizeOfRawData = 0` en `PointerToRawData = 0` voor `.text`,
  `.rdata`, `.data`, `.pdata` **en `.capcom0`**. Het enige stuk echte inhoud is **`.capcom1`**
  (`rawsize 0x346800`, `rawptr 0x400`) plus `.reloc` en `.rsrc`.
* Daarom is "de bytes op RVA `0xf1374d` in het BESTAND" niet op te vragen: die RVA valt in
  `.capcom0`, dat geen bestandsdata heeft. Lees je het bestand op dezelfde *offset*
  (`0x1a274d`), dan lees je de gepakte blob — vandaar "hoog-entropisch". **[GEMETEN]**
* `.capcom1` is de sectie die het **adres van het entry point** bevat (`AddressOfEntryPoint
  0xf23125`, bytes `eb 08 …` = `jmp short +8`) en de **IAT** (`rva 0x0120d000`, `size 0xdb0`) en de
  **exportdirectory** (`rva 0x0121cc00`, 57 libcurl-namen). **[GEMETEN]**
* Het geheugen vs. het bestand voor `.capcom1`: **2510 bytes verschillend**, en die liggen
  **allemaal** in de IAT (`0x0120d000..0x0120dda5`) — in het bestand staan RVA's, in het geheugen
  opgeloste adressen. Dat is normale binding. **[GEMETEN]**
* `.capcom0` is gemapt als **`r-xp`**, `.data` en `.capcom1` als **`rwxp`**. **[GEMETEN]**
* Entropie van de secties in het geheugen: `.text` 6,45 / `.rdata` 5,56 / `.data` 0,18 /
  `.pdata` 7,87 / `.capcom0` 7,15 / `.capcom1` 7,96 bits/byte. **[GEMETEN]**

---

## 7. Wat ik niet gedaan heb en niet weet

* **Geen run gedaan**, dus geen dynamische bevestiging van §3 en §5.
* **De beslissing in de bytecode is niet gelezen.** Dat is de eerlijke grens van deze analyse.
* **Waar de exitcode 32 vandaan komt** is niet gemeten.
* **Geen gemeten verband tussen de `am`-laag en dit exitpunt.** Zie `AM-LAAG.md`: daar staat wél
  gemeten dat 17 `am`-functies in dit beeld een gewijzigde prologue hebben die naar een
  trampolinepagina van de gesloten kern springt. Of het spel zonder die vervanging bij `0xf1374d`
  uitkomt, is **niet gemeten** — het is de hypothese die §5, breekpunten 1 en 4, kan bevestigen of
  weerleggen.
