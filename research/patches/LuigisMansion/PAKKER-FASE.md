# PAKKER-FASE.md — waarom het uitpakken onder onze kern anders loopt

**Soort werk:** metingen op de bench met de bewezen opzet (`lm-meet.sh`, `lm-pakker.sh`). Geen wijziging
aan de kern behalve het inzetten van onze DLL met terugzetten achteraf.

**Bronnen.** Alle paden staan op de Deck.

| bron | wat |
|---|---|
| `/tmp/lm-dicht-onze/meting-onze.txt` | **ONZE** kern `fe2ecf8c…`, dichte bemonstering t=4,0…6,5 s (elke 0,1 s) van `.text`-nulpercentage + prologue |
| `/tmp/lm-dicht-gesloten/meting-huidig.txt` | **GESLOTEN** kern `f5c7532d…`, zelfde venster + 7/8/9/10/12 s |
| `/tmp/lm-pak-onze/run.log` | ONZE kern, `WINEDEBUG=+virtual`, 22.829 regels |
| `/tmp/lm-pak-gesloten/run.log` | GESLOTEN kern, `WINEDEBUG=+virtual`, 54.281 regels |
| `/tmp/lm-meet-gesloten/tramp-10.0.bin` | de trampolinepagina onder de gesloten kern |

**Etiketten.** **[GEMETEN]** = direct uit een log of een geheugenlezing. **[AFGELEID]** = gevolgtrekking.

---

## 0. Uitkomst in het kort

1. **De pakker loopt onder ONZE kern en maakt zichzelf af.** `.text` gaat van 100 % nul naar
   **11,20 % nul op t=4,7 s**, en het spel doet daarna nog precies de afsluitende
   `NtProtectVirtualMemory`-aanroepen die de secties op hun eindrechten zetten. **[GEMETEN]**
2. **De geheugenbescherming is NIET het verschil.** Beide kernen doen exact dezelfde aanroepen op
   `.text` en `.capcom0` — eerst `0x40` (RWX), dan `0x20` (RX) — en er staat in geen van beide logs
   één `err:virtual` of `warn:virtual`. **[GEMETEN]**
3. **Het verschil zit in de TIJD en in de prologues.** De gesloten kern schrijft `.text` pas tussen
   t=8 s en t=10 s en zet de 17 prologues op datzelfde moment om; onze kern schrijft `.text` al op
   t=4,6–4,7 s en zet de prologues **nooit** om, en sterft op **t=5,96 s**. **[GEMETEN]**
4. **De am-route is dus NIET uitgesloten.** Zie §5 en de correctie in §7.

---

## 1. De tijdlijn van het uitpakken — [GEMETEN]

Per moment het percentage nul in `.text` (`0x140001000`, 3.496.500 B) en de 5 bytes op
`0x1402b4170` (de prologue van `amDongleInit`).

| t (s) | ONZE kern `fe2ecf8c…` `.text` | prologue | GESLOTEN kern `f5c7532d…` `.text` | prologue |
|---|---|---|---|---|
| 4,0–4,5 | 100,00 % nul | `00 00 00 00 00` | 100,00 % nul | `00 00 00 00 00` |
| 4,6 | **75,28 % nul** | `00 00 00 00 00` | 100,00 % nul | `00 00 00 00 00` |
| 4,7 | **11,20 % nul** | `40 53 48 83 ec` (origineel) | 100,00 % nul | `00 00 00 00 00` |
| 4,8 | 11,25 % nul | (leesfout) | 100,00 % nul | `00 00 00 00 00` |
| 5,0–8,0 | — | **proces weg op t=5,96** | 100,00 % nul | `00 00 00 00 00` |
| 9,0 | — | — | **64,16 % nul** | `00 00 00 00 00` |
| 10,0 | — | — | **11,20 % nul** | **`e9 df cd d3 ff`** |
| 12,0 | — | — | 11,20 % nul | `e9 df cd d3 ff` |

`11,20 % nul` is de eindtoestand van `.text`; de gesloten kern haalt precies dezelfde waarde, en de
baseline van 300 s ook (`B0-VIJFMIN`/`b0-baseline-gesloten`: `.text 11.2% nul`). **[GEMETEN]**

**De prologue op t=4,7 s is de ORIGINELE** (`40 53 48 83 ec` = `push rbx; sub rsp,0x20`, exact de
bytes die de trampolinepagina in slot A herhaalt). Dat is het directe bewijs dat het uitpakken klaar
is en dat de prologue **nog niet** is omgezet. **[GEMETEN]**

---

## 2. De geheugenbescherming: identiek op beide kernen — [GEMETEN]

`WINEDEBUG=+virtual`, gefilterd op de secties van het spelbeeld:

```
ONZE kern (fe2ecf8c…):
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140001000 00355a34 00000040
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140001000 00355a34 00000020
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140d71000 001a3c23 00000040
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140d71000 001a3c23 00000020

GESLOTEN kern (f5c7532d…):
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140001000 00355a34 00000040
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140001000 00355a34 00000020
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140d71000 001a3c23 00000040
  0104:trace:virtual:NtProtectVirtualMemory 0xffffffffffffffff 0x140d71000 001a3c23 00000020
```

**Byte voor byte dezelfde vier aanroepen**: `.text` en `.capcom0` worden eerst `0x40`
(`PAGE_EXECUTE_READWRITE`, dus schrijfbaar gemaakt om te ontpakken) en daarna `0x20`
(`PAGE_EXECUTE_READ`, de eindrechten). **[GEMETEN]**

Ook:
* **Geen enkele `err:virtual` of `warn:virtual`** in beide logs. De aanroepen slagen dus. **[GEMETEN]**
* De **laatste** virtuele regels van ONZE run zijn precies die afsluitende reeks:
  ```
  0104:trace:virtual:NtProtectVirtualMemory ... 0x140d71000 001a3c23 00000020   ; .capcom0 -> RX
  0104:trace:virtual:NtProtectVirtualMemory ... 0x140357000 00144b4b 00000002   ; .rdata   -> R
  0104:trace:virtual:NtProtectVirtualMemory ... 0x140001000 00355a34 00000020   ; .text    -> RX
  0104:trace:virtual:NtProtectVirtualMemory ... 0x140d46000 0002a06c 00000002   ; .pdata   -> R
  0104:trace:virtual:NtProtectVirtualMemory ... 0x140da2000 00005c58 00000004   ; eiland   -> RW
  ```
  Dat is de **eindpass** van de ontpakker: hij zet alle secties op hun eindrechten. Daarna sterft het
  proces. **[GEMETEN]**
* De eerste aanroepen op het spelbeeld zijn bij beide kernen **identiek op twee adressen na**
  (`0x14012e890` c.s., in `.capcom1`). **[GEMETEN]**

**Conclusie voor spoor 1 (de geheugenbescherming):** dit is **niet** het verschil. Onze kern maakt
`.text` net zo goed schrijfbaar als de gesloten kern, doet dat met dezelfde argumenten, krijgt geen
fout, en zet de rechten daarna net zo goed terug. De aanroepen zijn niet het probleem.

---

## 3. Wat er dan wél verschilt — [GEMETEN]

Alleen twee dingen, en ze hangen samen:

1. **De timing.** De gesloten kern schrijft `.text` pas tussen t=8 s en t=10 s; onze kern al op
   t=4,6–4,7 s. **[GEMETEN]**
2. **De prologues.** De gesloten kern zet de 17 prologues om **op hetzelfde moment** waarop zijn
   `.text` klaar is (t=10,0 s: 11,20 % nul én `e9 df cd d3 ff`). Onze kern zet ze **nooit** om, ook
   niet nadat `.text` op t=4,7 s klaar is. **[GEMETEN]**

---

## 4. Hypothese: de kern poort de ontpakker — [AFGELEID]

De `NtProtectVirtualMemory`-haak van de gesloten kern zou precies dit kunnen doen: **de laatste
`NtProtectVirtualMemory(.text, …, PAGE_EXECUTE_READ)` tegenhouden** — de aanroep waarmee de ontpakker
zegt "ik ben klaar, de code mag lopen" — tot de kern zijn 17 prologue-patches heeft aangebracht.

Wat deze hypothese dekt:

* waarom de gesloten kern **later** klaar is met `.text` (t=8–10 s) terwijl de ontpakker identiek is:
  de eindpass wordt tegengehouden;
* waarom de prologues in de gesloten kern op **exact hetzelfde moment** verschijnen als de voltooide
  `.text`: ze worden in dat venster aangebracht;
* waarom de gesloten kern de 300 s haalt en onze kern 5,96 s: zonder de poort loopt het spel meteen
  door naar de uitgepakte code en roept de `am`-functies aan die nog origineel zijn.

**Niet bewezen.** Deze log bevat geen tijdstempels en `+virtual` logt **geen retourwaarden** (alleen
het binnengaan van de functie), dus ik kan de poort niet direct zien. Het is een verklaring die alle
metingen dekt, niet meer dan dat.

---

## 5. Wat dit betekent voor de am-route — [GEMETEN] + [AFGELEID]

**De am-route is NIET uitgesloten, en dit document weerlegt de eerdere lezing dat hij dat wel was.**
De feiten:

* Het spel bereikt onder onze kern de **volledig uitgepakte toestand** (11,20 % nul, t=4,7 s) — het
  is dus niet zo dat het in de pakker sterft. **[GEMETEN]**
* Het sterft **1,3 s later**, met de 17 prologues nog origineel. **[GEMETEN]**
* Het enige gemeten verschil in het spelbeeld op dat moment is dat de 17 prologues origineel zijn in
  plaats van `e9`. **[GEMETEN]**
* Dat die prologues de dood **veroorzaken** is **[AFGELEID]** — het is een sterke correlatie en het
  enige gemeten verschil, maar geen causaliteit.

**Het beslissende experiment is de drop-in zelf:** zet de 17 prologues om (args bewaren, `eax = 0`,
`ret`, en voor `amDongleGetNetworkAddress` ook `*(uint32*)rcx = 0x1a8c0`) en kijk of het spel langer
dan 6 s leeft. De drempel is scherp: **5,96 s tegen 300 s**.

---

## 6. Wat niet gemeten is

* **`+virtual` logt geen retourwaarden.** "Welke aanroep mislukt" is met dit kanaal dus niet te
  beantwoorden; daar is `+relay` (gefilterd) of een ander kanaal voor nodig. De bekende valkuil is
  dat `+relay` het proces vertraagt en de levensduur verandert.
* **Geen tijdstempels.** De volgorde van de aanroepen is bekend, het moment niet. Een tijdlijn van
  de virtuele aanroepen zou de poort-hypothese van §4 direct kunnen bevestigen of weerleggen.
* **De aanroepen van de KERN zelf** (de gesloten kern die zijn eigen patches schrijft) zijn niet te
  onderscheiden van die van het spel in deze log — hetzelfde kanaal, geen module-informatie.
* **De `am`-laag is niet dynamisch gevolgd.** Of het spel onder onze kern `amDongleInit` werkelijk
  aanroept voordat het sterft, is niet gemeten. Een breekpunt of logpunt op `0x1402b4170` in onze run
  zou dat in één meting laten zien, en zou de causaliteit van §5 van AFGELEID naar GEMETEN tillen.

---

## 7. Correctie-record (belangrijk voor wie dit later leest)

Er heeft even een verkeerde lezing rondgegaan: **"onder onze kern is `.text` nooit ontpakt en wordt de
am-laag nooit bereikt, dus de am-route is af"**. Die lezing kwam uit
**`/tmp/lm-meet-onze/meting-onze.txt`**, een eerdere vlucht waarin alleen **t=0,1 … 4,0 s** bemonsterd
werd — dus **vóór** het uitpakken. Daar hoort ook "proces weg op t=5,72 s" bij.

Het juiste bestand is **`/tmp/lm-dicht-onze/meting-onze.txt`** (t=4,0 … 6,5 s, elke 0,1 s, **met**
het `.text`-nulpercentage). Dat laat zien dat `.text` op t=4,6 s begint te vullen en op t=4,7 s klaar
is (11,20 % nul), en dat het proces op t=5,96 s sterft.

**Conclusie: trek het am-spoor niet af.** Wie het later opnieuw opent, moet met
`/tmp/lm-dicht-onze/meting-onze.txt` werken en niet met `/tmp/lm-meet-onze/meting-onze.txt`.
