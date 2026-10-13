# RUN 18 + de bench-vrije vondst: de daemon staat in de EVDEV-route, niet in de IO-brug

*RavenGun, 2026-10-13. Run 18 liep **13:37:43 - 13:41:0x** (150 s, exitcode 0); daarna een bench-vrije
nameting zonder spel. Niets van mij draait nog (muiskoppeling uit, uitvoer ontwapend, mijn pids
opgeruimd). Geen commit.*

## 1. RUN 18: PROEF 1 IS **NIET GETEST** -- mijn eigen fout in het koppelen

Ik wilde de eerste muis uit `ravengunctl mice` koppelen, maar mijn ontleding pakte de **kopregel** van die
uitvoer in plaats van een muis:

```
gekozen muis-id: Muizen:
ravengunctl: daemon meldt invalid_argument: onbekende muis 'Muizen:' (zie 'mice' voor de beschikbare muizen)
```

Er is dus **geen bron gekoppeld** en de voorwaarde van proef 1 ("met een gekoppelde bron") is nooit
vervuld. **Alles wat run 18 over de aim zegt, geldt dus voor de situatie ZONDER koppeling** -- het is geen
weerlegging van mijn voorspelling. (De tweede fout in dezelfde uitvoer: `test-pattern point` drukt de
positie van **vóór** de wijziging af, dus "aim 0.800 bij punt 0.5" is de vorige stand, niet de nieuwe.)

Wat run 18 verder wel liet zien: de attract-lus kwam er in **deze** run (`13:38:18`, na 9 pulsen), de
tabel werd weer geldig gemeld (13 schakelaars + mijn regels), en `shm[8..11]` en `shm[12..15]` bleven
**allemaal nul** -- maar dat is **mijn eigen bestand** (`/rg/jvs` = `start.jvs`), dat ik zelf met de
START-puls beschrijf. Dat het daar nul is, zegt alleen dat de **daemon** dat bestand niet schrijft.

## 2. DE BENCH-VRIJE NAMETING: DE DAEMON ZEGT HET ZELF (GEMETEN)

Zonder spel, alleen de daemon en de shm-bestanden:

```
$ ravengunctl status
IO-brug:      geen (invoer via de virtuele apparaten, TeknoParrotUi leest ze: route evdev)
Uitvoerklok:  250 Hz; ... 0 positie-events
```

En het bestand dat de hulp als **standaard** noemt bestaat wel, maar blijft leeg:

```
/dev/shm/TeknoParrot_JvsState   64 bytes, 12 okt 10:18
  VOOR           b8-11=[0,0,0,0]  b12-15=[0,0,0,0]  b16-19=[0,0,0,0]
  na point 0.5   b8-11=[0,0,0,0]  b12-15=[0,0,0,0]  b16-19=[0,0,0,0]
  na point 0.9   b8-11=[0,0,0,0]  b12-15=[0,0,0,0]  b16-19=[0,0,0,0]
  na trigger tap b8-11=[0,0,0,0]  b12-15=[0,0,0,0]  b16-19=[0,0,0,0]
```

(`output-button trigger tap` weigerde zelfs zonder wapenen: *"uitvoer is niet gewapend: gebruik eerst
'arm'"* -- dus die meting deed niets.)

**Conclusie: de daemon staat in de `evdev`-route. Hij publiceert NIETS in de JVS-indeling**, niet de
knoppen (offset 8) en niet de aim (offset 12..19). De virtuele apparaten zijn zijn enige uitgang, en die
schrijft hij wél (de "3 positie-events, 18 knop-events" uit run 18). Het 64-byte bestand
`/dev/shm/TeknoParrot_JvsState` is een **restant van 12 oktober** dat niemand meer vult.

## 3. WAT DAT BETEKENT VOOR RUN 17 (mijn eerdere uitleg was te smal)

In run 17 schreef ik: *"de muismodus van de daemon stuurt het evdev-apparaat en publiceert niet in de
tp-shm-indeling"*. Dat is **juist**, maar de oorzaak is niet de muismodus -- het is de **routekeuze van de
daemon**: `IO-brug: geen`. Daarmee is ook de vraag van de laderbouwer scherper: **voor Halo kan de hulp de
aim niet van het evdev-apparaat halen**, want run 9 heeft gemeten dat het spel **geen enkel**
`/dev/input`-apparaat opent en **SDL niet laadt**. Van de twee routes die hij noemt, blijft er dus één
over: **de daemon moet de IO-brug (tp-shm) aanzetten**, en dan lezen mijn `as`- en `teller`-regels de
bordindeling zoals bedoeld. Dat is zijn mechanisme en zijn beslissing; ik meld het met de meting erbij.

## 4. PROEF 2 KAN BETER DAN GEPLAND, EN HEEFT DE DAEMON NIET NODIG

De tweede goedgekeurde proef (`[0x1f392d0]` = spelersaantal) kan **zonder** de daemon, want mijn
`zet`-regel schrijft rechtstreeks. En ik kan hem **strenger** maken: in plaats van te wachten op een
aim-bron die niet komt, schrijf ik de **aim-kommagetallen direct** -- de bitpatronen van `float`:

```
zet adres=0x1f392d0 breedte=4 waarde=1            # spelersaantal: is NUL de poort?
zet adres=0x1f392a0 breedte=4 waarde=1056964608   # 0x3F000000 = 0.5f  (aim x)
zet adres=0x1f392a4 breedte=4 waarde=1056964608   # 0.5f (aim y)
```

**Waarom 1 en niet 2 voor het spelersaantal:** de vraag is binair -- *is nul de poort?* -- en 1 is het
kleinste antwoord dat daarop ja of nee geeft. Met 2 zou ik een **tweede variabele** toevoegen (het spel
zou dan om een tweede geweerblok kunnen vragen, dat ik niet gezet heb) en dan weet ik bij een negatief
niet welke van de twee oorzaken het was.

**En waarom `zet` in plaats van `as` voor de aim:** de `as`-regel heeft een **bron** nodig (de shm-byte)
en die bron staat stil zolang de daemon in de evdev-route zit. Met `zet` schrijf ik het bitpatroon van een
`float` direct, dus de vraag "leest het spel onze kommagetallen, en laat het spelersaantal dat toe?"
wordt dan **niet** vertroebeld door een stille bron. Twee runs met verschillende waarden (0.5 en dan
0.25) laten zien of het richtpunt ook echt **beweegt** in plaats van alleen te bestaan.

**Verwachting vooraf:** met spelersaantal 1 en een geschreven `0.5f` verlaat de aim `-1,-1` (het spel
leest onze kommagetallen) en verschijnt er een richtpunt in beeld. Zo niet, dan is het spelersaantal niet
de poort, of de adressen van het kommagetalpaar zijn niet de aim van dit spel -- beide zijn dan winst.
