# CORRECTIE — MIJN RIP-RELATIEVE SCANNER WAS FOUT, EN DAARMEE VERVALT MIJN EIGEN WEERLEGGING

> **Het was geen geluk dat de absolute scan en run 19 overeind bleven: het waren er TWEE die echt
> onafhankelijk van elkaar waren, terwijl de derde dat niet was — en juist die ene brak.**
>
> *En de uitkomst een uur later: met de herstelde scanner is de **lezer** van de aim gevonden, en die
> **bevestigt** de lezing — kommagetalpaar op `0x1f392a0`, stap 8 per speler, `-1.0f` = geen richtpunt —
> plus de **poort** die eraan hangt: `byte[0x1aa37c0 + speler]`, getoetst tegen 3. Zie `AIM-GELEZEN.md`.
> De adressen zijn dus niet vervallen: ze waren **aangeraakt maar onbewezen**, en nu bewezen.*


*RavenGun, 2026-10-13. Dringende correctie op `NIET-VASTGESTELD-GEWEERLAAG.md` en op mijn verslag van
de herlezing. Dit staat hier zodat niemand op de oude conclusie verder bouwt.*

## 1. De fout, precies

In mijn RIP-relatieve scanner las ik de `disp32` op **`i + len(pat) - 4`**, terwijl `len(pat)` **3** is
(de bytes `48 8d 05`) -- dus las ik op **`i - 1`**, één byte te vroeg. Daardoor klopte **geen enkele**
doelberekening, en vond de scanner **niets**, ook waar er wel iets staat. Het patroon **`(pat, 7)`** in
mijn eerdere script zette de **instructielengte** in de tuple, maar die 7 werd nooit gebruikt: ik gebruikte
`len(pat)`.

**Correct is:** de instructie is 7 bytes en de `disp32` staat op **`i + 3`**:
```
disp = struct.unpack_from("<i", d, i + 3)[0]
doel = (i + 0x400000) + 7 + disp
```

**En de absolute-literaal-scan was WEL goed** (`d.find(struct.pack("<I", adres))`) -- die conclusie blijft
dus staan: de adressen komen nergens als **getal** in de binary voor.

## 2. Wat er daardoor vervalt

Mijn uitspraak van de vorige ronde: *"geen enkele RIP-relatieve verwijzing naar die vijf adressen"* --
**die is ongeldig en trek ik in.** De weerlegging van mijn eigen lezing steunde op drie instrumenten; één
daarvan (de RIP-scan) was stuk. Er bleven er twee over, en nu blijkt de derde precies het
tegenovergestelde te zeggen.

## 3. De HERSTELDE meting (GEMETEN, met dezelfde binary)

```
ANKER 0x180ae80 (schakelaartafel)   ->  22 RIP-relatieve verwijzingen
0x1f392a0 (mijn aim x)              ->   3 RIP-relatieve verwijzingen   (0xdd5c7a, 0xdd5cb0, 0xdd6c7a)
0x1f392a4 (mijn aim y)              ->   0
0x1f392d0 (mijn spelersaantal)      ->   0
0x1a7cb20 (mijn blok 0)             ->  45 RIP-relatieve verwijzingen   (0xdcf243, 0xdcf390, 0xdcf3ed, ...)
```

**Dus: de adressen van mijn eerdere lezing WORDEN door de code aangeraakt** -- het apparaatblok
(`0x1a7cb20`) op 45 plaatsen en de aim-x (`0x1f392a0`) op 3. Wat blijft staan is dat het spel mijn
geschreven waarden in de attract-lus **niet aanraakt** (run 19) en dat de adressen niet als **getal** in de
binary staan (absolute scan) -- maar "de code kent ze niet" is **onjuist** gebleken.

**De status van de lezing is daarmee: WEER IN BEELD, niet weerlegd.** De waarschijnlijke verklaring voor
run 19 is niet "het adres is fout" maar "de lezer zit achter een voorwaarde" -- bijvoorbeeld een toestand
die alleen in een **spelmodus** waar is, of een lezer die alleen bij een gebeurtenis kijkt.

## 4. En de bevestigde anker-pass leverde meteen een familie accessors op

Rond `0x61c170` staat een rij kleine functies die elk een **vast adres** teruggeven, met een stap van
**0x48**:
```
61c1c0: lea ...,%rax  # 0x180ae80     <- de schakelaartafel zelf
61c1b0: lea ...,%rax  # 0x180aec8     (+0x48)
61c1a0: lea ...,%rax  # 0x180af10     (+0x90)
61c190: lea ...,%rax  # 0x180af58     (+0xd8)
61c180: lea ...,%rax  # 0x180afa0     (+0x120)
61c170: lea ...,%rax  # 0x180afe8     (+0x168)
```
en op `0x61c940` begint een echte functie die de tafel in **`rdi`** laadt
(`lea 0x11ee533(%rip),%rdi # 0x180ae80`) en registers bewaart -- dat is de **invoerlaag** die de
schakelaartafel opbouwt/bijwerkt. **Dat is de functie die de laderbouwer kan aangrijpen**, en die is nu
langs het bevestigde anker gevonden.

**Let op de stap 0x48 tegenover de 0x50 uit de getter:** er zijn dus twee indelingen op dezelfde basis --
de getter rekent met `slot*0x50` binnen een bordblok van `0x2200`, en deze accessors stappen met `0x48`.
Dat verschil moet worden uitgezocht voordat er iets geschreven wordt.

## 5. De volgende stap (bench-vrij, en nu heel gericht)

**Disassembleer de drie verwijzingen naar `0x1f392a0`** (`0xdd5c7a`, `0xdd5cb0`, `0xdd6c7a`) en de 45 naar
`0x1a7cb20` (`0xdcf243` en verder). Daar staat de **lezer** van de aim en van het apparaatblok, en die
vertelt in één keer: of het een kommagetalpaar is, welke voorwaarde eraan hangt (waarom run 19 niets
deed), en welke knopindexen het spel echt leest. Dat is de weg -- niet opnieuw een run.
