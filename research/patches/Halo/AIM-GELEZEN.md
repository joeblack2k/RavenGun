# DE AIM IS GELEZEN -- accessor, setter, en de POORT (GEMETEN met de disassembler)

*RavenGun, 2026-10-13. Bench-vrij. Dit vervangt de twijfel: de lezer van `0x1f392a0` is gevonden, en
daarmee is de lezing **bevestigd** -- en de reden dat run 19 niets deed, is gevonden. Niets van mij draait.*

## 1. De GETTER van de aim (op `0xdd5c60`)

```
dd5c60: test %rsi,%rsi
dd5c63: je   0xdd5c94                 ; geen uit-pointer -> terug
dd5c65: test %rdx,%rdx
dd5c68: je   0xdd5c94
dd5c6a: lea  0xccdb4f(%rip),%rax  # 0x1aa37c0      <-- EEN BYTE PER SPELER
dd5c71: movslq %edi,%rdi
dd5c74: cmpb $0x3,(%rax,%rdi,1)                     <-- DE POORT: byte[speler] vergelijken met 3
dd5c78: jg   0xdd5ca0                               ; GROTER DAN 3 -> geen richtpunt
dd5c7a: lea  0x116361f(%rip),%rax # 0x1f392a0      <-- HET AIM-PAAR
dd5c81: movss (%rax,%rdi,8),%xmm0                   ; x = float[speler*8 + 0]
dd5c86: movss %xmm0,(%rsi)                          ; *out_x = x
dd5c8a: movss 0x4(%rax,%rdi,8),%xmm0                ; y = float[speler*8 + 4]
dd5c90: movss %xmm0,(%rdx)                          ; *out_y = y
dd5c94: repz ret
dd5ca0: movl $0xbf800000,(%rdx)                     ; -1.0f
dd5ca6: movl $0xbf800000,(%rsi)                     ; -1.0f   <-- "geen richtpunt"
dd5cac: ret
```

## 2. De SETTER (op `0xdd5cb0`)

```
dd5cb0: lea  0x11635e9(%rip),%rax # 0x1f392a0
dd5cb7: movslq %edi,%rdi
dd5cba: movss %xmm0,(%rax,%rdi,8)     ; schrijf x voor deze speler
dd5cbf: movss %xmm1,0x4(%rax,%rdi,8)  ; schrijf y
dd5cc5: ret
```
En op `0xdd5cd0` staat hetzelfde setterpatroon voor **`0x1f392e0`** (= `0x1f392a0 + 0x40` = index **8** bij
een stap van 8) -- dus de reeks loopt door voor meer dan 8 spelers/doelen.

## 3. Wat daarmee BEWEZEN is

1. **Het is een kommagetalpaar**: `movss` twee keer, stap **8** per speler (x op `+0`, y op `+4`), precies
   zoals mijn lezing zei. **De lezing is dus bevestigd, niet weerlegd** -- de eerdere intrekking van de
   RIP-scan heeft dit weer boven water gehaald.
2. **`-1.0f` is inderdaad de "geen richtpunt"-waarde** (`0xbf800000`), en die wordt geschreven als de
   poort dicht staat.
3. **En de POORT is nu bekend: `byte[0x1aa37c0 + speler]`, vergeleken met `3`; is die groter dan 3, dan
   geeft de getter `-1/-1` terug ongeacht wat er in het paar staat.**

## 4. Waarom run 19 niets deed -- de waarschijnlijke verklaring (AFGELEID, en meteen toetsbaar)

In run 19 schreef ik **de twee floats** en het spelersaantal, maar **niet `byte[0x1aa37c0 + 0]`**. Als die
byte daar **groter dan 3** was, gaf de getter netjes `-1/-1` terug -- en dan ziet niemand op het scherm
iets, terwijl mijn waarden in het geheugen blijven staan. **Dat klopt precies met wat ik zag**: de floats
bleven bit-identiek staan (de getter leest ze, hij schrijft ze niet terug) en het scherm veranderde niet.

**De volgende proef is daarmee één regel:** zet `byte[0x1aa37c0 + speler]` op een waarde **≤ 3** (0 is de
veilige keuze) en schrijf daarna het aim-paar. **Verwachting vooraf:** dan komt de getter bij de floats uit
in plaats van bij de `-1`-tak, en verschijnt er een richtpunt -- en met twee verschillende waarden moet te
zien zijn dat het ook **beweegt**. Dat is een run van twee minuten, en de eerste keer dat we de poort
meenemen in plaats van eromheen te werken.

## 5. Wat dit NIET oplost (en wat de opdracht eiste: niet schrijven voordat dit is uitgezocht)

* **De knopindices** van het apparaatblok (`0x1a7cb20`, 45 verwijzingen) heb ik nog **niet** gelezen --
  dat is de volgende pass, en daaruit volgt welke knop de trekker is.
* **De twee stappen op dezelfde basis (0x48 tegenover 0x50)** zijn nog **niet** verzoend. Ik noteer het
  als open, niet als opgelost: de getter rekent `slot*0x50` in een bordblok van `0x2200`, de accessors
  bij `0x61c170` stappen `0x48`. **Er wordt niets geschreven voordat dat is uitgezocht.**
* **Wat `byte[0x1aa37c0+speler]` precies betekent** (een toestand per speler: actief? gekalibreerd?
  aangesloten?) is niet gelezen -- alleen dat de getter hem tegen 3 toetst. Dat is de volgende vraag, en
  het antwoord bepaalt of "≤ 3 zetten" een eerlijke ingreep is of een omzeiling.
* En de **IO-brug-vraag** blijft bij de laderbouwer: de daemon zit in de evdev-route, dus de shm-kant is
  stil. Deze vondst verandert daar niets aan, maar maakt wel duidelijk **wat** er doorgegeven moet worden
  (het paar op `0x1f392a0`, plus de poortbyte per speler).
