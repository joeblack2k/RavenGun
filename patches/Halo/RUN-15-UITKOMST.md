# RUN 15 — de service-schakelaar in de attract-lus: mijn controle greep naar een OUDE logregel

*RavenGun, 2026-10-13. Uitgevoerd **12:52:43 - 12:55:4x** (180 s, beeld op de tv, exitcode 0). Niets
draaide ernaast, de Move was uit, de kern staat op `f5c7532d6e638485`, opgeruimd met alleen mijn eigen
pids. Geen commit.*

## 1. WAT ER MISGING, EN HET IS MIJN FOUT

Mijn fase-1-controle was `grep -q 'sound bank: attract' "$G"` op **het hele logboek van de game** -- en
dat bestand **loopt door over alle runs**. Het vond dus de `attract`-regel van **run 12/13** en meldde
"de attract-bank staat erin na 1 START-puls (t=13s)". Dat was een regel van **een vorige run**: in de
nieuwe regels van deze run komt de attract-bank **niet** voor (de laatste nieuwe regel is
`Network initialization completed` om 12:53:15). Het spel stond dus op het **kalibratiescherm**, en de
service-pulsen vielen daar -- **opnieuw op het verkeerde scherm**.

**De correctie is één regel:** elke controle moet op **alleen de nieuwe regels** kijken, dus
`tail -n +$((REGELS_VOOR+1)) "$G" | grep -q 'sound bank: attract'`, en niet op het hele bestand. Dat is
dezelfde valkuil als een `pgrep` op een algemene naam: je meet iets dat er al was.

## 2. WAT ER WEL GEMETEN IS (en dat is bruikbaar)

**De indrukken kwamen allemaal aan** (uit de meldingen van de hulp, en de tafelmeting van buiten het
proces bevestigt het):

| bit | schakelaar | aantal | wat de tafel deed |
|---|---|---|---|
| 6 | start (k=0) | 1 | `[6]` = 1 |
| 1 | **service (k=3)** | **3** | `[0]`=1, `[6]` liep op tot 3 |
| 4 | volume op (k=1) | 3 | `[0]`=1, `[6]` tot 3 |
| 5 | volume neer (k=2) | 3 | `[0]`=1, `[6]` tot 3 |
| 0 | test (k=4) | **0** | -- (zoals bedoeld) |

**En het scherm deed niets**: de foto's zijn 11,8-11,9 kB (egaal, de opstart) voor de eerste
service-pulsen en daarna **1,466 MB = het kalibratiescherm**, en het logboek kreeg **geen enkele regel**
tijdens de service- en volume-pulsen.

**Conclusie: ook deze keer is de service-schakelaar niet in de attract-lus getest** -- de indruk komt
aantoonbaar aan, maar het spel was nog niet zo ver.

## 3. WAT DE VOLGENDE RUN MOET DOEN (twee kleine correcties, verder identiek)

1. **De controle op de nieuwe regels** (`tail -n +$((REGELS_VOOR+1))`), zodat "de attract-bank is er"
   echt over **deze** run gaat.
2. **START blijven herhalen** tot die nieuwe regel er staat -- in run 11 duurde dat 25 s na de FileTest
   (11:56:03 -> 11:56:32), in run 13 zo'n 30-40 s. De lus moet dus **niet** na één puls stoppen (dat
   deed hij nu door de fout in punt 1) maar doorgaan met een bovengrens van ~60 s, en **stoppen en
   melden** als de attract er dan nog niet is.
3. Daarna pas: service (bit 1 -> k=3, ~700-800 ms, drie keer) met een foto per stap en de tafelmeting
   ernaast; dan zo nodig de volumetoetsen (bit 4/5) als menunavigatie.
4. Verwachting ongewijzigd: een service-/diagnosescherm met de geweren, de camera's en de
   online-inrichting. En als het daar ook niets doet: **lezen** (de bewaarde webcam-volgorde en de
   `AdjValStore`-familie onder `pmuser/adj/save/`), geen run.

## 4. De identiteitsregel

```
== play-native Halo 20261013-125243
script: …/scripts/play-native.sh
  de versie uit de REPO (commit 4f6a70a; met wijzigingen in scripts/ die niet in die commit zitten); sha256 c19370b6d9d2ef09…
  gegevens: …/packaging/native-games.json sha256 06db1453b689aaa4…
  hulp: …/rg-paths.sh sha256 8ca01e47…   hulp: …/rg-identiteit.sh sha256 be4c411f…
```

Ongewijzigd ten opzichte van run 14: dezelfde commit met **ongewijzigde wijzigingen in `scripts/`**,
dezelfde gegevens en dezelfde launcher. Er is dus niets aan de opzet veranderd tussen die twee runs --
alleen mijn eigen logcontrole was fout.
