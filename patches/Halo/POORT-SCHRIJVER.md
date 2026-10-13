# DE POORTBYTE WORDT DOOR HET SPEL ZELF GESCHREVEN — de proef is daarom NIET gedraaid

*RavenGun, 2026-10-13. Bench-vrij. Dit is de voorwaarde die de coördinator stelde voordat de proef
mocht draaien: **eerst uitzoeken of het spel die byte zelf schrijft.** Het antwoord is ja, en daarmee is
het vooraf zetten van die byte een **omzeiling** in plaats van een eerlijke ingreep. **De proef is dus
niet gedraaid** -- en dat is een uitkomst, geen omissie.*

## 0. De scanner is eerst gekalibreerd (bekend antwoord)

```
verwijzingen naar het anker 0x180ae80 : 22   (verwacht: 22)   -> de scanner vindt wat hij moet vinden
verwijzingen naar 0x1aa37c0          :  7
```

## 1. De zeven verwijzingen naar de poortbyte `0x1aa37c0`

| plek | wat er staat | oordeel |
|---|---|---|
| `0xdd525f` | `lea 0x1aa37c0,%rdi` + `mov %rbp,%rdx` + `xor %esi,%esi` + **`call 0x40d788`** | **de array wordt als EERSTE ARGUMENT aan een functie gegeven** -- een registratie/initialisatie |
| `0xdd5912` | `lea 0x1aa37c0,%rax` ... **`movl $0x0,(%rax,%rbp,4)`** (`0xdd590b`) | **ECHTE SCHRIJFACTIE: een dword nul op `[basis + speler*4]`** |
| `0xdd5950` | `lea 0x1aa37c0,%r13` + **`cmpb $0x3,0x0(%r13,%rbp,1)`** + `jg` | lezer (en ernaast wordt een float-paar geschreven) |
| `0xdd5bfa` | `cmpb $0x3,(%rax,%rdi,1)` + `jg` | lezer (de aim-getter van het tweede blok) |
| `0xdd5c6a` | `cmpb $0x3,(%rax,%rdi,1)` + `jg` | **de aim-getter die we al kenden** |
| `0xdd6c1a` | `cmpb $0x3,(%rcx,%rax,1)` + `jg` | lezer |
| `0xdd6c6a` | `cmpb $0x3,(%rcx,%rax,1)` + `jg` | lezer |

**Dus: het spel schrijft de byte** (`movl $0x0` op `0xdd590b`) **en geeft de array door aan een functie**
(`call 0x40d788` op `0xdd526b`). De lezers toetsen hem aan 3; de schrijver zet hem op nul.

## 2. Wat dat betekent voor de proef (en waarom ik hem NIET heb gedraaid)

De afspraak was: schrijft het spel die byte zelf, dan is die byte **bedoeld** om door iets gezet te worden
dat nu niet loopt -- en dan is onze taak **die weg laten lopen**, niet zijn uitkomst vooraf invullen.
Daarom is "zet de byte op 0 en kijk of het richtpunt komt" een **omzeiling** en geen eerlijke ingreep, en
**ik heb hem niet gedraaid.** Wat er in plaats daarvan moet gebeuren is bench-vrij en ligt voor de hand:

**Zoek wie de functie rond `0xdd590b` aanroept** (scan op `call` naar het begin van die functie) en
disassembleer de aanroeper. Als dat de **geweer-detectie** of de **kalibratie** is, dan is dat de weg die
moet lopen -- en dan weten we ook waarom hij in onze runs niet loopt (geen geweer, geen camera, of een
toestand die wij overslaan). Dat is dezelfde methode als waarmee de lezer is gevonden, en het kost geen
bench.

## 3. En een tweede dubbele indeling, die ik als OPEN noteer

De lezers vergelijken een **byte** op `[basis + speler]` (stap **1**), terwijl de schrijver een **dword**
op `[basis + speler*4]` zet (stap **4**). Dat kan alleen samenvallen voor speler 0. **Dat is dezelfde
soort dubbele indeling als de 0x48 tegenover 0x50**, en ik verzoen hem **niet** met een aanname:

* of er zijn twee arrays (een byte-array en een dword-array) die toevallig op hetzelfde adres beginnen;
* of een van mijn twee lezingen pakt het verkeerde veld;
* of de "speler"-index is in het ene geval een **byte-index** en in het andere een **dword-index**.

**Er wordt niets geschreven voordat dit is uitgezocht** -- en dat geldt nu voor twee vragen: de knopindices
van het apparaatblok (`0x1a7cb20`, 45 verwijzingen, nog niet gelezen) en deze twee dubbele indelingen.

## 4. De stand, eerlijk

* **Bewezen**: het aim-paar (`0x1f392a0`, stap 8, `-1.0f` = geen richtpunt), de poort (`cmpb $0x3` op
  `0x1aa37c0 + speler`) en dat het spel de poortarray **zelf** schrijft en doorgeeft.
* **Niet gedaan, met reden**: de proef met de vooraf gezette poortbyte -- dat zou een omzeiling zijn.
* **Open**: wie de schrijver aanroept (bench-vrij te vinden), de knopindices van het blok, en de twee
  dubbele indelingen (0x48/0x50 en byte/dword).
