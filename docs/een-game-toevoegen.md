# Een game toevoegen — handleiding voor bijdragers

*Voor wie een nieuwe game in RavenGun wil zetten. Je hebt geen C++ nodig en je raakt geen bestanden
van anderen aan: alles wat je doet staat in één map, `patches/<JouwGame>/`.*

Lees eerst `docs/dropins.md` (het contract) en `docs/community-model.md` (wat de motor vandaag wel
en niet kan). Deze handleiding is de volgorde van werken.

---

## Voordat je begint: past deze game in een familie die de motor al kent?

Dit is de vraag die bepaalt of het een middag werk is of een project. Kijk in
`GameProfiles/<JouwGame>.xml` naar `EmulationProfile`:

| Uitkomst | Wat het betekent |
|---|---|
| een profiel dat al in `patches/` voorkomt (`WartranTroopers`, `RawThrillsGUN`, …) | **goed nieuws** — de bordindeling bestaat al |
| `EmulatorType` is `OpenParrot` | **het beste** — die games draaien al volledig op eigen lader én eigen kern |
| een profiel dat nergens voorkomt | waarschijnlijk engine-werk; zie de laatste sectie |

---

## Stap 1 — Zet de game in de app en laat hem herkennen

Zet de game in een gamemap en laat de app hem vinden. Noteer:

- het pad van de executable;
- de **grootte** in bytes en de **sha256** (`sha256sum <exe>`);
- of het 32- of 64-bits is;
- de **versie** van de game (staat vaak in het bestand zelf: `strings` of de eigenschappen).

Die vier dingen gaan straks in `[Identify]` en in de `Reason` van je patches. Zonder de versie weet
een volgende persoon niet of jouw patch nog geldt.

## Stap 2 — Kopieer de template

```bash
cp -r patches/_template patches/<JouwGame>
```

## Stap 3 — Vul `game.ini` in

Volg de commentaren in het bestand. Het `[Config]`-blok komt uit
`GameProfiles/<JouwGame>.xml`; importeer het met `scripts/import-tp-profiles.py` in plaats van het
met de hand over te typen.

**Laat `[Dongle]` weg.** De schakelaar is voor de eigenaar; de *soort* dongle staat in
`dongle.json`.

## Stap 4 — Vraagt de game een dongle?

Zoek in de map van de game naar de sporen: `hasp_*`, `matrix32.dll`/`matrix64.dll`, een
`DongleRegion`/`PcbRegion` in het profiel, een tweede executable (`amdaemon.exe`).

- **Ja** → maak `dongle.json` met het juiste `kind` (`hasp`, `matrix`, `region`, `amdaemon`) en zet
  de gegevens erbij. Zie `docs/dropins.md`.
- **Nee** → laat `dongle.json` weg. Dat is de betekenis van "geen dongle".

**Denk aan de afweging uit `docs/community-model.md`:** vraagt de game alleen *óf* de dongle er is,
dan is een patch op het beslispunt genoeg en is `dongle.json` misschien overbodig. Verbruikt hij
echte gegevens (geheugenimages), dan is emulatie nodig.

## Stap 5 — De patches

Zoek uit wat de game tegenhoudt en schrijf dat op in `patches.ini`.

**Wat vandaag werkt:** `Kind = file` met `Target = loader` of `Target = core` — zoeken en vervangen
in de lader of de kern, met `Find`/`Replace`/`Count`. De launcher controleert of `Find` precies
`Count` keer voorkomt en weigert anders, dus je kunt je niet vergissen zonder het te merken.

**Wat vandaag nog niet werkt:** `Kind = memory`, en `Kind = file` met de **game zelf** als doel. De
motor slaat die over. Schrijf ze toch op — met `Source` en `Reason` — want dan is het werk klaar
zodra de motor het kan (zie `docs/community-model.md`, punten 1 tot 3). Maar verwacht er nu nog geen
effect van.

**Zet er altijd bij:**
- `Source` — waar de kennis vandaan komt (een openbare bron, een eigen meting, een forum);
- `Reason` — waarom de aanpassing nodig is, in het Nederlands.

## Stap 6 — Drop-ins

Heb je een bestand van ons nodig in de view van de game (een DLL, een `.so`)? Zet het in `dropin/` en
beschrijf het in `dropin/dropin.ini`. **Een drop-in toevoegen is een bestand plus één sectie — geen
codewijziging.**

Zet hier nooit een bestand van TeknoParrot zelf neer; alleen wat wij bouwen.

## Stap 7 — Testen

```bash
build/rg-direct --plan <JouwGame>     # wat gaat er gebeuren? (start niets)
# echt starten: dubbelklik in de app (de daemon start de game zelf)
```

`--plan` laat zien welke lader, welke kern, welke patches en welke dongle gekozen worden. Draait er
iets mis, dan weigert hij met een duidelijke melding in plaats van halverwege te stoppen.

**Schrijf op wat je meet**, niet wat je verwacht: welke toetsen werken, of het vizier klopt, hoe lang
de game blijft draaien. Een schermafdruk met `import -window root` is bewijs.

## Stap 8 — Schrijf de README

Vervang de template-README door wat je hebt gemeten: wat de game nodig heeft, wat je hebt geprobeerd,
wat niet werkte, en wat er nog open is. **Dat is voor de volgende persoon meer waard dan de
instellingen zelf** — die staan immers al in de bestanden.

Zet je uitgebreidere bevindingen in `findings/<JouwGame>.md`.

---

## Controlelijst

- [ ] `game.ini` met `[Game]`, `[Identify]` (grootte + sha256) en `[Config]`
- [ ] `[Status]` met `Note` (Nederlands) en `NoteEN` (Engels), en `Findings`
- [ ] `dongle.json` alleen als de game een dongle vraagt, met het juiste `kind`
- [ ] `patches.ini` met per patch `Enabled`, `Kind`, `Category`, `Source` en `Reason`
- [ ] `dropin/` + `dropin.ini` alleen als er bestanden van ons nodig zijn
- [ ] `README.md` met je eigen metingen
- [ ] `build/rg-direct --plan <JouwGame>` geeft geen weigering ("verdict": "ok")
- [ ] `python3 -m unittest discover -s tests -q` blijft groen

---

## Wanneer je moet stoppen en om engine-werk moet vragen

Doe dat als één van deze waar is — dan is het geen map maar een stuk motor, en dat is nuttig werk om
te melden in plaats van om in te bijten:

- de game vraagt een **nieuw soort ingreep** die geen van de bestaande `Action`-soorten dekt;
- de game heeft een **nieuw nagebootst apparaat** nodig (een bord, een kaartlezer, een camera);
- de game heeft een **nieuwe lader** nodig (een nieuwe verpakkings- of beveiligingsvorm);
- de exe is **gepakt**, zodat de code pas in het geheugen leesbaar is;
- de game is van een familie waarvoor **nog geen eigen kern** bestaat (zie
  `docs/community-model.md`).

**Elke keer dat zoiets één keer in de motor wordt opgelost, wordt het daarna een `Action` in de
woordenschat — en dan is het voor iedereen weer een map.** Dat is de bedoeling van dit ontwerp.
