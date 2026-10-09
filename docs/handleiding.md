# Handleiding voor de speler

Dit is de gewone gebruiksaanwijzing van RavenGun. Er komt geen technische kennis aan te pas. Open de app met de snelkoppeling "RavenGun" op het bureaublad. Bovenaan staan acht tabbladen. De app onthoudt het tabblad waar je het laatst was.

## Een game starten

1. Open het tabblad Games.
2. Zoek de game op en druk op Spelen.
3. Staat er "Wacht op de Move: druk op de PS-knop.", druk dan op de PS-knop van de Move. De Move gaat aan en de app gaat verder.
4. Na even staat er "De game draait." Het beeld van de game komt op de tv. De lightgun wordt vanzelf aangezet.
5. Klaar met spelen? Sluit de game zoals gewoonlijk in de game zelf, of druk in de app op Stoppen.

Onder sommige games staat een kleine opmerking, bijvoorbeeld dat ze nog niet getest zijn of nog niet werken. Die games kun je wel proberen, maar het kan misgaan.

## Met twee spelers

Elke Move is een speler: de eerste Move is speler 1, de volgende die je koppelt speler 2. Elke speler heeft in de game een eigen pistool. De bol van speler 1 is cyaan (lichtblauw), die van speler 2 magenta (roze-paars), zodat de camera ze uit elkaar houdt. Speler 2 start in het spelmenu met toets 2 (de knop Start of Vierkant op zijn Move); Coin is voor iedereen toets 5.

Op de tabbladen Overzicht, Richten, Trillen, Knoppen en LED en camera staat bovenaan "Instellingen van: Speler 1 / Speler 2". Kies eerst de speler; alles wat je daarna verandert, geldt alleen voor de Move van die speler. Zo kan speler 2 bijvoorbeeld zachter trillen of een andere knoppenindeling hebben.

Weet je niet welke Move welke is? Ga naar Controllers en druk bij een Move op Knipperen: die Move knippert dan drie seconden wit en trilt even.

In TeknoParrot moet een game de pistolen van beide spelers kennen. Voor games die met `scripts/tp-add-game.py` zijn toegevoegd, staat speler 2 meteen goed ("RavenGun Virtual Lightgun P2"). Het pistool van speler 2 bestaat pas als speler 2 een Move heeft; start een game daarom pas nadat beide Moves gekoppeld zijn.

## Een controller koppelen

Een nieuwe Move (of een Navigation Controller) koppel je zelf, op het tabblad Controllers:

1. Druk op Nieuwe controller koppelen.
2. Sluit de controller met een USB-kabel aan op de pc. Gebruik een kabel die data doorgeeft; een los laadkabeltje werkt niet.
3. De app koppelt hem aan deze pc. Bluetooth start daarvoor even opnieuw; een Move die al aan staat, valt een paar seconden weg en komt vanzelf terug.
4. Staat er "Haal de USB-kabel eruit": doe dat.
5. Staat er "Druk nu kort op de PS-knop": druk op de ronde PS-knop. Als hij verbonden is staat er "Klaar", en hij staat in de lijst met zijn spelernummer.

Staat er dat de controller al gekoppeld is, dan kun je kiezen: Opnieuw koppelen, of gewoon de kabel eruit halen en op de PS-knop drukken.

De allereerste keer vraagt de app om eenmalig een hulpprogramma te installeren. Daarvoor is het beheerwachtwoord nodig: laat iemand die het weet het getoonde commando in een terminal uitvoeren, en druk daarna op Opnieuw proberen. Daarna kan de app voortaan zonder wachtwoord koppelen.

## Een controller verwijderen of een andere naam geven

Op het tabblad Controllers staat elke bekende controller met zijn naam, speler, batterij en wanneer hij het laatst gezien is.

- Naam: typ een nieuwe naam en druk op Naam bewaren.
- Speler: kies een ander spelernummer. Heeft een andere Move dat nummer, dan ruilen ze.
- Verwijderen: als je een Move wegdoet of als hij kapot is. De app vraagt of de instellingen bewaard moeten blijven. Kies "instellingen bewaren" als hij misschien terugkomt (bijvoorbeeld na een reparatie); hij krijgt ze dan terug als je hem opnieuw koppelt. Kies "wissen" als hij voorgoed weg is.
- Standaardinstellingen: wist de eigen instellingen van die controller; dan gelden weer dezelfde instellingen als voor een nieuwe controller.

Een Navigation Controller hoort bij een speler. Zijn knoppen kies je bij Knoppen van die speler (onderaan de lijst), bijvoorbeeld L1 voor herladen en L2 voor een granaat. Hij werkt alleen als de Move van die speler ook aan staat.

## Richten: de cursor goed krijgen

Houd de Move op het midden van het scherm en druk op de PS-knop of op de grote knop Centreren in het Overzicht. Dat moet je vaker doen, want een Move draait na een tijd langzaam weg. Dat is normaal.

Op het tabblad Richten kun je instellen hoe de cursor zich gedraagt. Elk schuifje werkt meteen: sleep het en kijk tegelijk naar de blauwe stip. Er staat altijd bij wat de waarde is.

- Vooruitkijken: de cursor loopt anders een beetje achter op je hand. Een hogere waarde maakt dat minder, maar te hoog laat de cursor doorschieten als je abrupt stopt.
- Rust bij stilhouden: lager is rustiger, maar de cursor volgt je hand dan wat trager.
- Reactiesnelheid bij snel richten: hoger betekent minder vertraging bij snelle bewegingen.
- Standaard herstellen zet deze drie terug op de waarden waarmee alles is gemeten.
- Wegdraaien tegengaan: laat deze op Kompas (aanbevolen) staan, tenzij je veel metaal of magneten bij de Move hebt. Dan is Zonder kompas beter.
- Leren van centreren: laat dit aan staan. De Move leert dan van elke keer dat je centreert hoe hij wegdraait.

Het kalibratiescherm op de tv open je met de knop op dit tabblad. Je bedient het daar met het pistool zelf.

## Trillen

Op het tabblad Trillen zet je het trillen aan of uit en kies je hoe sterk, hoe lang en hoe vaak de Move trilt. Wijzigingen werken meteen, ook terwijl je speelt. Met Test voel je het direct. Lukt de test niet, dan staat er waarom (meestal is de Move niet aan).

## Knoppen

Op het tabblad Knoppen kies je per knop wat die doet: herladen, granaat, centreren, een toets van het spelmenu (Coin, Start, Test, Service) of niets. Druk op een knop van de Move en de bijbehorende regel licht op, zodat je weet welke knop je bedoelt. De PS-knop (centreren) en de trekker (schieten) staan vast.

Je wijzigingen gelden pas na Opslaan. Dat werkt meteen, ook als een game draait. Met Ongedaan maken ga je terug, met Standaardindeling herstellen krijg je de indeling terug waarmee alles is getest (nog even op Opslaan drukken).

## LED en camera

De camera volgt de gloeiende bol bovenop de Move. Op dit tabblad kies je de kleur en zie je of de camera de bol ziet. Kies een kleur die niet in de kamer voorkomt, bijvoorbeeld geen rood bij een rode bank. De instellingen van de camera zelf (beeldformaat, belichting) hoef je meestal niet aan te raken.

## Wat betekenen de meldingen?

- Niet verbonden (bij de Move): de Move staat uit of is niet gekoppeld. Druk op de PS-knop. Werkt dat niet, koppel hem dan opnieuw op het tabblad Controllers.
- Batterij: bijna leeg (rood), laag (oranje), halfvol, goed, bijna vol of vol. Laadt: de Move ligt aan de lader. De Move meldt geen precies percentage, alleen deze stappen. Laad hem op voor een lange speelsessie.
- Navigation Controller: niet verbonden betekent dat de joystick niet gevonden is. Dat is geen probleem als je hem niet gebruikt.
- "Deze speler heeft nog geen Move" (bij de spelerkeuze): er is voor die speler nog geen Move gekoppeld. Wat je dan verandert, geldt voor nieuwe controllers.
- "Koppelen is niet gelukt" met een tekst eronder: lees de tekst. Verbindt hij niet via Bluetooth, laad hem dan eerst op en probeer het opnieuw.
- Camera: Ziet de bol betekent dat het richten kan werken. Ziet de bol niet: houd de Move voor de camera en let op andere lichtjes of de kleur van de bol. Camera werkt niet: controleer de USB-kabel van de camera.
- Lightgun: Klaar betekent dat een game de Move als pistool ziet. Uit betekent dat de games niets van de Move merken. Bij Spelen gaat hij vanzelf aan.
- "Wacht op de Move: druk op de PS-knop.": de game wil beginnen, maar de Move is nog niet gevonden. Druk op de PS-knop. Na twee minuten zonder Move geeft de app op.
- "Starten is mislukt." met een tekst eronder: lees de tekst, bijvoorbeeld "Geen Move gevonden binnen 2 minuten". Los het op en druk nog een keer op Spelen.
- "Er wordt al een game gestart of gestopt": wacht even, of druk op Stoppen.
- "Er draait al een game": druk eerst op Stoppen.
- "Centreren lukt nog niet": de Move is niet aan of niet verbonden.
- "Het programma op de achtergrond is niet bereikbaar" (rood bovenin): het programma ravengund draait niet. Sluit de app en open hem opnieuw met de snelkoppeling.

Het tabblad Info (technisch) is voor als je hulp zoekt. Daar staat ook de knop Diagnose exporteren, die een bestand maakt dat je kunt delen. Er staan geen camerabeelden in.
