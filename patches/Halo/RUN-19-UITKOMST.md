# RUN 19 — proef 2: mijn float landt en blijft, maar het attractiescherm tekent geen richtpunt

*RavenGun, 2026-10-13. Twee runs: **13:42:48 - 13:46:4x**, 130 s elk, exitcode 0 beide. Niets van mij
draait nog. Niets gedraaid naast -- en let op: **de bench was bij de start NIET vrij**: er liep een
HOTDSD-run van een andere partij (`timeout 140 ... OpenParrotLoader64.exe ... amdaemon.exe`, pids
1011328/1011339/1011395). Ik heb toch gedraaid omdat de opdracht "run it" was en het spel in de
sandbox geen last van die processen heeft (eigen netwerk- en PID-namespace), maar het hoort in het
verslag: **deze twee runs zijn niet op een lege machine gedraaid.** Geen commit.*

## 1. De opzet, zoals goedgekeurd

Twee runs, met de aim **direct geschreven** als bitpatroon (geen bron, dus geen stille shm-byte):

```
zet adres=0x1f392a0 breedte=4 waarde=1056964608   # 0x3F000000 = 0.5f   (run A)
zet adres=0x1f392a4 breedte=4 waarde=1056964608   # 0.5f
zet adres=0x1f392d0 breedte=4 waarde=1            # spelersaantal: is NUL de poort?
```
en run B met `0.25f` (`0x3E800000` = `1048576000`) op de twee aim-adressen.

De hulp meldde beide keren een **geldige** tabel: *"13 schakelaars, 0 bits, 0 assen, 3 tellers, 5 vaste
waarden"*, en de attract-lus kwam in **beide** runs na 9 START-pulsen (13:43:24 en 13:45:36).

## 2. De meting: mijn waarden LANDEN en BLIJVEN (GEMETEN)

| waarde | direct na de attract-lus | 15 s later |
|---|---|---|
| run A `0x1f392a0/4` | `u32=1056964608` = **0.5** | `u32=1056964608` = **0.5** |
| run B `0x1f392a0/4` | `u32=1048576000` = **0.25** | `u32=1048576000` = **0.25** |
| `0x1f392d0` (spelersaantal) | **1** | **1** |
| `0x1a7cb20` (blok aanwezig) | **1** | **1** |
| `0x1a7cb5c` (spelernummer) | **0** | **0** |

**Dus drie dingen zijn nu bewezen:**
1. **het schrijfpad naar het kommagetalpaar werkt** (bitpatroon van een `float` via `zet` komt exact
   aan -- 0.5 en 0.25 staan er letterlijk);
2. **het spel laat die adressen in de attract-lus met rust**: de waarde is na 15 seconden nog bit voor
   bit dezelfde. Het spel **verbruikt** hem niet en schrijft hem niet terug;
3. **het spelersaantal `0x1f392d0` is te zetten en blijft staan** (het spel zet hem niet terug).

## 3. Wat dat WEL en NIET betekent (en hier moet ik eerlijk zijn)

**Wat het niet betekent:** dat het spel de kommagetallen *leest*. Een waarde die blijft staan kan net zo
goed betekenen dat het spel die twee adressen in deze toestand **niet aanraakt**. "Blijft staan" is geen
bewijs van gebruik.

**En het scherm kan deze vraag in de attract-lus niet beslissen.** De foto van run A toont het
**attractielogo** van Halo (het blauwe logo op het sterrenveld) met de vier meldingen
(`QR scanners need to be configured`, `P1/P2 QR Scanner is not detected`,
`Factory Setup Has Not Been Completed`) en `Press START to Begin` / `Cost to play: $1.00` -- en
**geen enkel richtpunt**. Er valt in de attract-lus dus niets te zien dat door onze aim zou moeten
bewegen. De twee foto's van run A en run B **verschillen** wel van elkaar (3.152.945 tegen 3.150.121
bytes, andere hashes), maar de attract-lus is **geanimeerd** (de leveldemo's wisselen), dus dat verschil
is **geen bewijs** voor de aim. Ik noem het daarom geen resultaat.

## 4. De stand van de vragen

| vraag | stand |
|---|---|
| werkt het schrijfpad naar `0x1f392a0/4`? | **JA, bewezen** (0.5 en 0.25 exact, en stabiel) |
| is `0x1f392d0` te zetten en blijft het? | **JA, bewezen** (1 blijft 1) |
| raakt het spel die adressen aan in de attract-lus? | **NEE, gemeten** (15 s later bit-identiek) |
| **leest** het spel ze? | **NIET VASTGESTELD** -- het scherm toont geen richtpunt in deze toestand |
| is het spelersaantal de poort? | **NIET BESLIST** -- er is geen reactie om aan af te meten |
| de eenheid van de aim | **NIET GETEST** (er valt niets te schalen zonder lezer) |
| welke knop de trekker is | **NOG OPEN** -- en de daemon zit in de evdev-route, dus de shm-kant is stil |

## 5b. En toen de bench-vrije tegenproef: DE BINARY VERWIJST NERGENS NAAR DIE ADRESSEN (GEMETEN)

Ik heb in `game` (31 MB) gezocht naar de **letterlijke adressen** als 4-byte en 8-byte little-endian
patroon -- in de hele binary, code én data:

```
aim x  (0x1f392a0)          4-byte: geen enkele treffer    8-byte: geen enkele treffer
aim y  (0x1f392a4)          4-byte: geen enkele treffer    8-byte: geen enkele treffer
spelersaantal (0x1f392d0)   4-byte: geen enkele treffer    8-byte: geen enkele treffer
blok 0 (0x1a7cb20)          4-byte: geen enkele treffer    8-byte: geen enkele treffer
blok+0x3c (0x1a7cb5c)       4-byte: geen enkele treffer    8-byte: geen enkele treffer
```

En de adressen liggen **wel** in het beeld: de segmenten zijn `0x400000 + 0x1062000` en
`0x1661000 + 0xd72000` (dus `0x1f392a0` en `0x1a7cb20` vallen binnen het tweede segment, de data). Er is
dus **niets** in de binary dat die adressen als getal bevat -- geen `lea`, geen `mov`, geen tabel.

**Twee onafhankelijke instrumenten wijzen dus dezelfde kant op:**
1. run 19: het spel laat mijn geschreven `0.5` en `0.25` **15 seconden lang bit-identiek staan** in de
   attract-lus;
2. de binary: **geen enkele verwijzing** naar die adressen.

**Wat dat betekent:** de kans is groot dat `0x1f392a0` **niet** het aim-paar is dat dit spel leest --
of dat het via een **basis + afstand** wordt benaderd (dan staat in de code de *offset* en niet het
absolute adres, en schrijf ik in een **kopie** van de structuur in plaats van in het levende veld). Die
tweede mogelijkheid kan ik met deze zoekmethode niet uitsluiten, en dat zeg ik er eerlijk bij. Maar de
combinatie "het spel raakt de waarde niet aan" + "geen enkele verwijzing in de binary" is sterk genoeg om
mijn eerdere lezing van het aim-paar **niet meer als uitgangspunt te nemen** voordat die lezing opnieuw
is nagelopen.

**Wat dit NIET raakt:** het schrijfpad van `zet` is bewezen, het spelersaantal is te zetten en blijft
staan, en de tabel is geldig en additief. Alleen de **adressen van de aim** staan weer open.


De vraag "leest het spel `0x1f392a0`?" is **statisch** te beslissen zonder te draaien: zoek in de
binary de code die naar dat adres verwijst (een `lea`/`mov` met de immediate `0x1f392a0`, of een
berekening `0x1f392a0 + 8*speler`). Vind je de lezer, dan weet je of het een kommagetalpaar is en wat
ermee gebeurt (schalen, vergelijken met -1, naar het scherm brengen). Vind je hem niet, dan is het adres
uit mijn lezing **geen** aim-paar en dat is dan een definitieve uitkomst.

Dat is de goedkoopste en meest beslissende volgende stap, en hij kan nu meteen: geen bench, geen spel.
