# Overdracht van de wiki (2026-10-13, 12:55)

Van: de loader-sessie. Aan: de partij die de wiki nu maakt. Ik laat de wiki hierna los; alles wat ik had, ligt in deze map.

## Wat hier ligt

| Pad | Wat het is |
|---|---|
| `concept/` | Mijn twaalf pagina's, in het Engels, precies zoals ik ze achterliet (bestandsnaam = paginanaam op GitHub) |
| `gereedschap/wiki-check.py` | De controle: pagina's en links, de opbouw per spelpagina, wat er niet in hoort, en geen negatieve scoperegel |
| `gereedschap/test_wiki_check.py` | De test van die controle (11 tests, groen op 12:53) |

De controle draait zo: `python3 -I gereedschap/wiki-check.py [MAP]` (zonder MAP: `concept/`). De test: `python3 -I gereedschap/test_wiki_check.py`.

De controle en zijn test stonden in de loader-repo (`scripts/`, `tests/`, twee ctests). Ze waren daar nog niet gecommit; ik heb ze **hierheen verplaatst** en de twee ctests uit `CMakeLists.txt` gehaald, zodat er niet twee kopieën uit elkaar gaan lopen. De loader-suite hangt daarmee niet meer aan de wiki.

De oude werkmap `~/projects/ravengun.wiki/` staat er nog (dezelfde twaalf bestanden, `git init` op branch `master`, remote `https://github.com/joeblack2k/ravengun.wiki.git`, **geen enkele commit, niets gepusht**). Gebruik hem of gooi hem weg; ik raak hem niet meer aan.

## Wat af is

- **De algemene pagina's**, inhoudelijk compleet: `Home.md`, `Loader-Architecture.md`, `Official-Start-Scripts.md`, `Running-on-Linux.md`, plus `_Sidebar.md` en `_Footer.md`. Wat erin staat over de lader (routes, het plan, drop-ins, de twee starts, argumenten, omgevingsvariabelen, de startscriptlezer, schijfletters, de identiteitsregel) komt uit de loader-kant en is gemeten of uit onze eigen bron gelezen.
- **De controle**, met de canonieke tien koppen en de regel tegen negatieve scoperegels er al in.

## Wat half is

- **De drie besluiten van de coördinator zijn in de pagina's nog NIET verwerkt.** De controle is daarom nu rood op `concept/` (39 meldingen): dat is de werklijst, geen defect.
  1. Canonieke opbouw: mijn zes spelpagina's hebben nog de oude koppen (`8. Linux notes`, `9. Status`, `10. Open questions`). Het moet worden: 1 Platform and cabinet · 2 What the game needs · 3 The official start script, or why there is none · 4 The route we use · 5 Input · 6 Timing · 7 What we saw on screen · 8 Current state and open questions · 9 What we tried and what did not work · 10 What is Linux-specific, and what differs on Windows.
  2. Positieve scoperegel: elke spelpagina eindigt nog met "Nothing on this page concerns the hardware security or licensing layers…", `Home.md` heeft een sectie "What this wiki does not cover" en `_Footer.md` noemt het ook. Vervangen door: "This page covers the game and the loader: what you need to run this title and where it gets stuck."
  3. Labels: ik gebruikte MEASURED / READ / DERIVED / NOT MEASURED. Besloten is MEASURED / INFERRED, en "assumption" waar het een aanname is. De legenda staat in `Home.md` en in `_Footer.md`.
- **Per spelpagina:**

| Pagina | Stand | Gebaseerd op de digest van |
|---|---|---|
| `Transformers-Shadows-Rising.md` | Inhoud compleet, oude opbouw | 12:13 (ongewijzigd sindsdien) |
| `Luigis-Mansion.md` | Inhoud compleet voor wat ik heb overgenomen (zie hieronder), oude opbouw | 12:10 |
| `Terminator-Salvation.md` | Inhoud compleet voor wat ik heb overgenomen, oude opbouw | 12:15 |
| `Halo-Fireteam-Raven.md` | Oude opbouw; de digest is om **12:46 herschreven** (nu met §9 en een nieuwe §10) en dat is nog niet verwerkt | een oudere versie |
| `House-of-the-Dead-Scarlet-Dawn.md` | **Skelet** met alleen feiten van de loader-kant; de digest (466 regels) is niet omgezet | — |
| `Rabbids-Hollywood.md` | **Skelet**; de digest kwam pas om 12:46 binnen (563 regels) en is niet omgezet | — |

## Wiki-mechanica (gemeten 12:52)

- `gh repo view joeblack2k/ravengun` geeft `visibility: PRIVATE`, `hasWikiEnabled: false`, standaardbranch `master`.
- `git ls-remote https://github.com/joeblack2k/ravengun.wiki.git` geeft `Repository not found`. **De wiki bestaat dus nog niet.**
- Een GitHub-wiki is een eigen git-repository, `<repo>.wiki.git`. Hij ontstaat pas nadat (a) de wiki in de repo-instellingen is aangezet en (b) de **eerste pagina in de browser** is gemaakt. Daarvoor valt er niets te klonen of te pushen. Punt (b) heb ik niet zelf kunnen meten; het is hoe GitHub het documenteert.
- Inloggen: `gh` is ingelogd als `joeblack2k`, protocol https, scopes `gist`, `read:org`, `repo`. Voor een push naar de wiki zijn geen extra gegevens nodig.
- Bestandsnamen: `Naam-Met-Streepjes.md` wordt de pagina "Naam Met Streepjes"; een link is `[tekst](Naam-Met-Streepjes)` zonder `.md`. `Home.md` is de startpagina, `_Sidebar.md` en `_Footer.md` staan op elke pagina. De controle eist letters, cijfers en streepjes.
- **Twee beslissingen van de eigenaar staan nog open** en ik heb er niets aan veranderd: of de repo (of alleen de wiki) publiek wordt, en het aanzetten van de wiki. Een wiki van een privérepo is niet publiek. Pushen is publiceren; ik heb niets gepusht.

## Wat ik in de digests vond (voor wie ze omzet)

Fouten en verouderde beweringen:

1. **Halo §3** zegt dat `rmdir /S /Q Y:\` letterlijk in `utils/run_game.sh` staat. Dat klopt niet: het is een Windows-opdracht en hij staat in het `game.bat` van **Luigi's Mansion** (onder `if not exist Y:\SDCF`). In het Halo-script staan wel `reboot -f` en `rm -rf` van de gebruikersmap, en in `go.sh` het vergrendelen van accounts, `swapoff` en netwerkinstellingen (door mij gelezen met de startscriptlezer van de launcher).
2. **Rabbids §3** zegt dat een `game.bat` in de verzameling "begint met" die `rmdir`. Het is voorwaardelijk, zie punt 1.
3. **Transformers §4** zegt dat onze lader profielen met twee uitvoerbare bestanden weigert. Dat is sinds vandaag niet meer zo: `launch-direct.py` start er twee (plan-sleutel `starts`). De vergelijking met Scarlet Dawn in die alinea klopt dus niet meer.
4. **Halo §10.1** noemt "een eigen lader plus de runtime-kern"; §10.8 zegt "geen eigen ELF-lader". Gemeten is het tweede: Halo start met de programmalader van het systeem.
5. **Halo §5 en §8** zeggen dat onze hulp de trekkerbits en de asbytes niet schrijft "en dat is code". De hulp is intussen tabelgestuurd en kan het; wat ontbreekt zijn de **bestemmingsadressen in het spel**. Die zijn niet gemeten.
6. **Luigi-digest**: de tekst is dubbel gecodeerd (`Â§` in plaats van `§`, en de Japanse commentaarregels in `game.bat` zijn onleesbaar). Zeven regels. In mijn pagina heb ik de commentaarregels weggelaten.
7. **Luigi §4 tegenover mijn eigen meting**: de digest zegt dat een `dosdevices`-koppeling die vóór de wineserver is gemaakt daarna weg is. In een aparte testprefix bleef hij bij mij staan (11:16). Beide zijn gemeten, de oorzaak van het verschil niet. Mijn pagina zet beide naast elkaar.

Paden en namen van deze ene machine die eruit moeten (de controle vangt ze): `/home/deck/...` (Rabbids §8.1, Terminator §2), het sharepad in de logregel van Transformers §2 en §10.5, en "de eigenaar" als persoon.

Wat ik bewust **niet** heb overgenomen, omdat het volgens punt 6 van de opdracht niet in de publieke wiki hoort. Het is aan de coördinator om te zeggen waar de grens precies ligt; ik heb hem ruim genomen:

- **Luigi §5** in zijn geheel, en de regels in §6, §8 en §9 die erop leunen. Mijn pagina zegt alleen: "The remaining difference is in how the game's own start-up code interacts with the core."
- **Terminator**: de bestandsnamen in §1, §2, §3 en §6 waar de zoekwoorden op aanslaan, en de verklaring in §8 van het patroon van kindprocessen. Mijn pagina beschrijft het patroon (aantal, tempo, levensduur) en zegt dat de oorzaak niet gemeten is. Let op: de coördinator vroeg om hier kritisch te kiezen wat blijft; mijn pagina is de karigste variant.
- **Halo §1 en §2**: dat `game` een aangepast bestand is naast een origineel, en waarom de bestandstest één fout meldt. Mijn pagina zegt alleen dat de test op onze kopie één bestand afkeurt en dat dat niet fataal is.
- **Scarlet Dawn §5**: de kolommen en regels over de platformlaag (regels 9, 270, 293, 307, 332 en 406 van de digest).

De controle kent de woorden uit de lijst van de coördinator nog niet allemaal: `matrix` en een los `licen…` staan er niet in (alleen `licence key`).

## Los hiervan, maar het raakt de wiki

**Bijgewerkt 13:4x: de map heet nu `patches/_WIKI/`** (een map die met `_` begint is voor de scans geen spel; de twee tests zijn weer groen). Hieronder de oude toelichting. `patches/WIKI/` stond in de patches-repo tussen de spelmappen. Twee tests van de loader (`ravengun_ownprofile_py`, `ravengun_gamesettings_tests`) zien die map als een spel zonder `game.ini` en zijn daardoor rood. De digests horen eerder hier, in deze map, of in een map die met `_` begint.
