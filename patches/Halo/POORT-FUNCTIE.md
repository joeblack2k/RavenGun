# DE POORT WORDT GEOPEND DOOR `0xdd57e0` -- en die functie wordt VIA EEN POINTER aangeroepen

*RavenGun, 2026-10-13. Bench-vrij. Dit is de aanroeper-pass op de schrijver van de poortbyte. Niets van
mij draait. Scanner vooraf gekalibreerd (22 verwijzingen naar het anker, verwacht 22).*

## 1. De functie die de poort ZET (begin op `0xdd57e0`)

```
dd57e0: push %rbp
dd57e1: push %rbx
dd57e2: mov  $0x2,%edi
dd57e7: sub  $0x8,%rsp
dd57eb: movl $0x1,0xccdde3(%rip)   # 0x1aa35d8       <-- een vlag op 1
dd57f5: call 0x40e4a0
dd57fa: call 0xde65c0
dd57ff: call 0xdcf340                                <-- de APPARAATBLOK-familie (0xdcf2xx-0xdcf9xx)!
        ...
dd58f3: lea  0x1aa37a0,%eax                          <-- let op: 0x1aa37A0, niet 0x1aa37c0
dd5903: movss 0x1139ba4(%rip),%xmm0                  <-- een constante float uit rodata
dd590b: movl $0x0,(%rax,%rbp,4)                      <-- dword nul op 0x1aa37a0 + speler*4
dd5912: lea  0x1aa37c0,%rax                          <-- DE POORTARRAY
dd5928: movb $0x1,(%rax,%rbp,1)                      <-- EN ZET DE POORTBYTE OP 1
```

## 2. Wat daarmee opgelost en bewezen is

1. **De poort wordt op `1` gezet** (`movb $0x1` op `0xdd5928`), met **stap 1** -- precies de stap die de
   lezers gebruiken (`cmpb $0x3,(%rax,%rdi,1)`). 1 ≤ 3, dus **deze functie is wat de poort opent** en de
   getter daarna de echte floats laat teruggeven.
2. **En de byte/dword-vraag is opgelost MET EVIDENTIE, niet met een aanname:** het zijn **twee
   verschillende arrays**. De dword-schrijfactie gaat naar **`0x1aa37a0`** (+ speler*4), de poortbyte naar
   **`0x1aa37c0`** (+ speler) -- ze liggen **0x20 uit elkaar**. Mijn eerdere "zelfde basis, dubbele stap"
   was dus een leesfout van mij, en die is nu rechtgezet: geen dubbele indeling, twee arrays.
3. **En deze functie hangt aan het apparaatblok:** hij roept **`0xdcf340`** aan, en dat adres ligt in
   dezelfde familie als de 45 verwijzingen naar `0x1a7cb20` (`0xdcf243`, `0xdcf390`, ...). De poort en
   het geweerblok zitten dus in **dezelfde subsystem-code** -- dat is de eerste keer dat die twee werelden
   met een echte verwijzing aan elkaar hangen.
4. Hij zet ook een **vlag op `0x1aa35d8` = 1** (naast de poortbyte) -- een tweede veld om in de gaten te
   houden.

## 3. En het belangrijkste negatief: NOBODY CALLS IT DIRECTLY

De scan op `call e8 rel32` naar `0xdd57e0` geeft **0 aanroepen**. Deze functie wordt dus **via een
pointer** aangeroepen -- een functietabel, een vtable of een callback-registratie. Dat verklaart ook de
eerdere vondst op `0xdd525f`: daar wordt de array als **eerste argument** aan `0x40d788` gegeven, wat op
een **registratie** lijkt.

**De volgende stap is dus bench-vrij en heel precies:** zoek in het datasegment de **8-byte waarde
`0xdd57e0`** (en de buur-adressen van deze functie). Waar die staat, staat de tabel waarin hij
geregistreerd is -- en die tabel vertelt **wat hem aanroept** en dus **welke toestand of welk menu de
poort hoort te openen**. Dat is de laatste schakel tussen "de poort bestaat" en "de poort hoort open te
gaan".

## 4. De stand van de drie open punten

| punt | stand |
|---|---|
| knopindices van het apparaatblok (`0x1a7cb20`, 45 refs) | **nog niet gelezen** |
| `0x48` tegenover `0x50` op de schakelaarbasis | **nog open** |
| ~~byte tegenover dword op de poortarray~~ | **OPGELOST met evidentie: twee arrays, 0x1aa37a0 (dword) en 0x1aa37c0 (byte), 0x20 uit elkaar** |

En de vondst van de poort-functie geeft meteen een **nieuw** open punt: **waar is de pointer naar
`0xdd57e0`**, en wat roept hem aan.
