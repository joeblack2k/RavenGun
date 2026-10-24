# RUN 16 — de attract-lus WEL gehaald, service doet daar niets, en de volumetoetsen blijken munten

*RavenGun, 2026-10-13. Uitgevoerd **12:59:47 - 13:03:0x** (180 s, beeld op de tv, exitcode 0). Mijn
eigen processen zijn opgeruimd; de runtime-kern staat op `f5c7532d6e638485`. Geen commit.*

## 1. De correctie WERKTE, en dat is nu een echte meting

Mijn nieuwe controle kijkt alleen naar de regels **na** het gemarkeerde punt (`NIEUW()`), en daarmee is de
attract-lus voor het eerst **aantoonbaar in deze run** gehaald:

```
13:00:00  FileTest Reported 1 errd files at startup!        (na 1 START-puls)
13:00:23  Network initialization completed
13:00:25  Failed to load FMOD sound bank: ATTRACT           <-- DE NIEUWE REGEL, t=38 s
13:00:49  Failed to load FMOD sound bank: level1
13:01:56  Failed to load FMOD sound bank: level2
```
De attract-bank kwam na **9 START-pulsen** (t=38 s, 13:00:25) -- in dezelfde orde als run 11 (25 s) en
run 13 (30-40 s). En de tegencontrole die in run 15 ontbrak, is er nu impliciet: dezelfde grep op de
nieuwe regels had toen **niets** gevonden.

## 2. SERVICE IN DE ATTRACT-LUS: de indruk komt aan, het spel reageert niet

| tijd | wat | tafel bord 0 k=3 | nieuwe regels |
|---|---|---|---|
| 13:00:25 | (attract-lus bereikt) | `00000000 …` | 17 |
| 13:00:30 | SERVICE-puls 1 | `00000001 …` | **17** |
| 13:00:36 | SERVICE-puls 2 | `00000001 … [6]=2` | **17** |
| 13:00:42 | SERVICE-puls 3 | `00000001 … [6]=3` | **17** |

De hulp meldde drie keer `indruk bit 1 -> bord 0 schakelaar 3` en de teller liep op -- **de indruk komt
dus aan** -- maar het logboek kreeg **geen enkele regel** en het scherm veranderde niet zichtbaar.
**Conclusie: de service-schakelaar (bit 1 -> k=3) opent in de attract-lus geen menu** (althans niet met
drie pulsen van 700-800 ms). Dat is nu een **schone** meting, in tegenstelling tot run 14 en 15.

## 3. EN EEN VERRASSING: DE VOLUMETOETSEN ZIJN MUNTEN

Na de volumepulsen (bit 4 -> k=1, bit 5 -> k=2) sprong het aantal nieuwe regels van **18 naar 73**, met
tientallen regels als:
```
13:01:00  WARNING: Detected more coin pulses than safely allowed on cashbox 0 coin 4!
13:01:00  time: 97935665, pressed: 1
13:01:00  time: 97935648, pressed: 1
...
```
**De game telt onze "volume"-indrukken als MUNTEN op `cashbox 0 coin 4`.** Onze hulp noemt k=1 en k=2
"volume omhoog/omlaag"; in deze game zijn dat **muntkanalen**. Dat is een correctie op onze eigen
tabelindeling, en het verklaart ook waarom de munt eerder zo makkelijk werkte (bit 2 -> k=5): er zijn
meer muntkanalen dan wij dachten.
En er is een **nieuw instrument** bijgekomen: de game logt bij munten zelf `time: <n>, pressed: 0|1` --
dat is een directe uitlezing van wat het spel aan invoer ziet.

## 4. Wat dit betekent voor de meldingen op het scherm

Het servicemenu is met deze schakelaar **niet** te openen (bit 1 -> k=3, drie pulsen, attract-lus). De
volgende kandidaten, in deze orde:
1. **Andere schakelaarindices** voor service/diagnose: k=3 was onze aanname; met de correctie van punt 3
   (k=1/k=2 zijn munten) is de hele tabel opnieuw verdacht. Een **veilige** manier om dat te toetsen is
   `RG_RT_ALLTYPES=1` (alle schakelaars als "aanwezig" markeren) of het een voor een zetten met een foto
   per stap -- en dan kijken of er een menu verschijnt.
2. **Lezen**, zoals afgesproken: de bewaarde webcam-volgorde en de `AdjValStore`-familie onder
   `pmuser/adj/save/`. Geen run.
3. En als er ooit een menu verschijnt: **niets zetten** voordat ik het gemeld heb.

## 5. De identiteitsregel, en een huisregel die zichtbaar werkte

```
== play-native Halo 20261013-125947
script: …/scripts/play-native.sh
  de versie uit de REPO (commit 4f6a70a; met wijzigingen in scripts/ die niet in die commit zitten); sha256 c241ff400648aee4…
  gegevens: …/packaging/native-games.json sha256 06db1453b689aaa4…
  hulp: …/rg-paths.sh sha256 8ca01e47…   hulp: …/rg-identiteit.sh sha256 be4c411f…
```
De launcher-sha is **veranderd** (`c241ff400648aee4` tegen `c19370b6d9d2ef09` in run 15) -- er is dus
weer aan `scripts/` gewerkt; de gegevens zijn gelijk gebleven.

**En de huisregel van vandaag werkte precies zoals bedoeld:** tijdens mijn opruiming bleek er een run van
**een andere partij** te zijn begonnen (gamescope met `OpenParrotLoader64.exe` en
`Hodzero-Win64-Shipping.exe` -- HOTDSD). Mijn `pkill`-patronen waren specifiek (`/pm/g7/halo/game` en
`play-native.sh`), dus **ik heb hun gamescope en lader met rust gelaten**; ze staan nog in de lijst en ik
heb er niets aan gedaan. Wie een algemene naam had gebruikt, had hun run omgelegd.

## 6. Wat run 16 NIET oplevert

* Geen servicemenu, en dus nog geen weg om de QR- en fabrieksmeldingen weg te halen.
* Geen uitspraak over de **aim** en de **trekker** (die velden schrijft de hulp nog niet; dat is code).
* Geen geluid (FMOD 1.10 ontbreekt).
* En de volumetoetsen zijn **niet** als volume getest -- ze blijken munten, dus die vraag is nog open.
