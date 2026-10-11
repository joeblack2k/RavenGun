# Kan de community dit? — een eerlijk antwoord

*Advies van RavenGun, ronde 6 (2026-10-11). De vraag: kan iemand op een forum een nieuwe game
toevoegen met alleen een map `patches/<Game>/`? Alle cijfers hieronder zijn gemeten; de commando's
staan erbij.*

## Het korte antwoord

**Ja, dat kan — maar niet vandaag, en het vraagt twee generieke stukken engine, geen 35 losse
oplossingen.** Het mapcontract staat (`docs/dropins.md`) en is goed. Wat ontbreekt is de motor die
de patches in die mappen ook echt toepast.

De eerlijke stand van zaken in één tabel:

| Nodig voor een nieuwe game | Status |
|---|---|
| Een mapcontract dat per game alles bij elkaar houdt | **klaar** (`docs/dropins.md`) |
| Een lader | **klaar** — eigen build, 24 van de 35 games gebruiken hem al |
| Profiel, invoerindeling, resolutie, dongle, drop-ins in een map | **klaar** qua vorm |
| Bytepatches op de **lader en de kern** | **klaar** (`Kind = file`, met versiecontrole) |
| Bytepatches op de **game zelf** | **ontbreekt** |
| Geheugenpatches (`Kind = memory`) toepassen | **ontbreekt** — de motor bestaat niet |
| Een eigen kern voor de 21 TeknoParrot-games | **ontbreekt** |

---

## 1. Wat er wél staat (gemeten)

**De lader is van ons.** Voor 24 van de 35 games kiest de launcher
`openparrot/current/OpenParrotLoader*.exe`; alleen de 7 ElfLdr2-games gebruiken nog BudgieLoader en
TC5 staat expliciet op `Build = teknoparrot`. Narekenen: `python3 tools/verify-all.py`.

**Het bestandsformaat van de patches is er.** `patches/<Game>/patches.ini` kent per patch:

| Sleutel | Betekenis |
|---|---|
| `Enabled` | aan of uit (zonder deze is de patch uit) |
| `Kind` | `file` (zoek-en-vervang) of `memory` (op een adres in het proces) |
| `Category` | `loader`, `input`, `audio`, … (voor de sortering in de app) |
| `AppliedBy` | `launcher` of `core` — wie hem hoort toe te passen |
| `Target` + `TargetName` | bij `file`: de lader, de kern, of een pad in de runtime |
| `Find` + `Replace` + `Count` | bij `file`: de bytes, en hoe vaak ze moeten voorkomen |
| `Module` + `Offset` + `Action` + `Size` | bij `memory`: waar en wat |
| `Source` + `Reason` | waar de kennis vandaan komt en waarom |

En `Kind = file` werkt echt: de launcher controleert `Find` (moet precies `Count` keer voorkomen),
weigert als de bytes niet kloppen, patcht **een lokale kopie** en raakt de NAS nooit aan. Met
`[Game] ExeSha256` weigert hij ook als je de verkeerde spelversie hebt — precies de gewoonte die je
van een nette patchmotor wilt.

**De dongle is per game uit te drukken.** `dongle.json` met een `kind` en de gegevens erbij; de
emulator-binary zelf is niet game-specifiek. Zie `docs/dropins.md`.

---

## 2. Wat er niet staat — en dat is het probleem

### 2a. `Kind = memory` wordt door niemand toegepast

In `scripts/launch-direct.py` staat letterlijk:

```python
if p.kind != "file":
    entry(p, "overgeslagen", plek, "soort %s wordt niet door RavenGun toegepast%s" % (
        p.kind, " (AppliedBy = core: de OpenParrot-kern doet dit zelf)" if p.applied_by == "core" else ""))
    continue
```

En:

```
$ grep -rn 'jmp-returns-true' scripts/ src/ tools/
(geen uitvoer)
```

De geheugenpatches in `patches/GaiaAttack4/patches.ini`, `HauntedMuseum`, `HauntedMuseumII` en
`MusicGunGun2` zijn dus **documentatie van wat de OpenParrot-kern zelf doet** — niet iets wat onze
code uitvoert. Wie vandaag een `Kind = memory`-patch in een map zet, ziet niets gebeuren.

### 2b. Je kunt de game zelf niet patchen

`patch_target()` in dezelfde file:

```
patch '<naam>': Target <x> is niet de lader of de kern van deze game; alleen die twee kunnen
gepatcht worden
```

Alleen de **lader** en de **kern** mogen het doel van een `Kind = file`-patch zijn. De game zelf
niet. En juist de game is waar de meeste per-game aanpassingen horen.

### 2c. Er is geen eigen kern voor de 21 TeknoParrot-games

Gemeten (`docs/research/kern-abi.md`): `TeknoParrot64.dll` is 56 MB, onze `OpenParrot64.dll` is
1,9 MB. De koppeling tussen lader en kern is klein — `InitializeASI` plus `TP_InitializeApmTest` —
maar het verschil in grootte is **kennis**. Onze eigen kern noemt **nul** van de 35 games in deze
repo.

---

## 3. Waarom dit toch goed nieuws is

De twee ontbrekende motordelen zijn **generiek**. Ze zijn één keer te bouwen en daarna werken ze voor
**elke** map die iemand maakt. Dat is precies wat je voor een community wilt: de bijdrage is data,
de motor is van ons.

En er zit een vereenvoudiging in die het veel kleiner maakt dan het lijkt:

**Een geheugenpatch op de hoofd-exe is een bestandspatch.** `Module = game.exe`, `Offset = 0x113E80`,
`Action = jmp-returns-true` betekent niets anders dan: schrijf op de bestandsplaats die met dat adres
overeenkomt de bytes voor "spring naar iets dat 1 teruggeeft". Voor een PE- of ELF-bestand is
`Module + Offset` via de sectietabel naar een bestandsplaats te rekenen. Dus:

> **Als de bestandspatch-motor ook de game-exe als doel accepteert, en we rekenen `Module + Offset`
> om naar een bestandsplaats, dan is het grootste deel van de `memory`-woordenschat meteen gedekt —
> zonder injectie, zonder procesgeheugen, zonder eigen kern.**

Dat is de kortste weg naar een werkende community-motor, en het gebruikt de motor die er al is.

**Wat daar niet mee lukt, eerlijk gezegd:**
- games waarvan de exe **gepakt** is (Pigs Might Fly, Luigi's Mansion — daar is de code pas in het
  geheugen leesbaar);
- patches in een **DLL die pas later geladen wordt** (die staat niet in het exe-bestand);
- patches die van de **uitvoeringstoestand** afhangen (iets wat pas na een bepaalde gebeurtenis
  aangepast moet worden);
- aanpassingen die geen bytes zijn maar **gedrag** (een nieuwe API onderscheppen, een nieuw
  nagebootst apparaat).

Voor die gevallen is er wél een echte geheugen-motor nodig: een DLL/`.so` die in het spelproces
geladen wordt en op `Module + Offset` schrijft. Dat is één component, geen 35.

---

## 4. Wat ik zou doen, in deze orde

De volgorde is niet willekeurig: elk stuk maakt het volgende stuk nuttig.

1. **Laat `Kind = file` de game-exe als doel accepteren** (op een kopie in de view, nooit op de NAS),
   met dezelfde `Find`/`Replace`/`Count`-controle en dezelfde `ExeSha256`-versiecontrole die er al
   zijn. *Klein, en het maakt per-game patchen vandaag mogelijk.*
2. **Reken `Module + Offset` om naar een bestandsplaats** voor de hoofd-exe, zodat de bestaande
   `memory`-patches in `patches.ini` als bestandspatch uitgevoerd kunnen worden. *Klein, en het maakt
   vier bestaande patchbestanden meteen echt.*
3. **Bouw de echte geheugen-motor** voor de gevallen uit §3 die niet in een bestand passen: één
   DLL/`.so` die in het proces geladen wordt en een lijst `(Module, Offset, Action, Size)` afwerkt.
   *Eén component; daarna werkt élke map.*
4. **Bouw de eigen kern** voor de TeknoParrot-families: `InitializeASI` + `TP_InitializeApmTest`
   exporteren, het gedeelde geheugen vullen volgens de bordindeling, `CreateFileA` onderscheppen voor
   de schijfletter-herleiding, en de patchmotor aanroepen. *Dit is het grootste stuk, en zonder dit
   blijven 21 games op TeknoParrot.*
5. **Schrijf de handleiding voor bijdragers** (`docs/een-game-toevoegen.md`) plus een
   `patches/_template/` en een controleprogramma dat zegt wat er in een map nog mist. *Dit is wat een
   forumlid nodig heeft.*

Punt 4 is het duurste en het minst modulair. Punten 1, 2, 3 en 5 zijn samen waarschijnlijk minder
werk dan punt 4, en ze maken het verschil tussen "de community kan meedoen" en "de community kan
niets".

---

## 5. De eerlijke conclusie

**Wat wel kan:** een forumlid voegt een game toe met één map — profiel, invoerindeling, dongle,
drop-ins en bytepatches. Zodra punt 1 en 2 hierboven klaar zijn, werkt dat voor elke game die in een
bestaande familie past en waarvan de exe niet gepakt is. Dat is de meerderheid van de lightgun-titels.

**Wat niet kan:** "elke game zonder TeknoParrot, alleen met een map". Voor 21 van de 35 games is de
gesloten kern de blokkade, en die kennis is één binary van 56 MB — niet per game te vervangen. Daar
is een eigen kern voor nodig, en dat is een project, geen patch.

**Wat nooit een map wordt:** een game die een nieuw soort ingreep vraagt, een nieuw nagebootst
apparaat, of een nieuwe lader. Dan is het engine-werk. Maar — en dat is de sleutel — **elke keer dat
zoiets één keer in de engine wordt opgelost, wordt het daarna een `Action` in de woordenschat, en dan
is het voor iedereen weer een map.** De motor groeit, de bijdragen blijven data.

Dat is het model dat werkt, en het is het model dat dit project al bijna heeft.
