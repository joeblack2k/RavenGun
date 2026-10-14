# Time Crisis 5 op de directe route in C++ (rg-direct): invoer komt niet aan

Datum: 2026-10-14. Route: dubbelklik in RavenGun -> ravengund -> systemd-run -> `rg-direct --run` -> Wine -> OpenParrotLoader64 ->
TimeCrisisGame (tp-shm, indeling `TimeCrisis5`, pipehelper `rg-jvsbridge.exe` spiegelt `/dev/shm/ravengun_JvsState` naar
`TeknoParrot_JvsState`, 64 bytes). Bench: muismodus (Steam Controller Puck als lightgun van speler 1) en `ravengunctl output-button`.

## Gemeten

| Wat | Uitkomst |
|---|---|
| Start per dubbelklik | ja: de game laadt, attract met Bink-logo en "INSERT 1 CREDIT(S)" |
| Munt (toets 5, de daemon geeft Shift+T) | ja: SELECT A GAME MODE, daarna de instructie |
| Bordbeeld van de daemon | ja: trekker 0x10, gunknop 0x20, linker pedaal 0x40, rechter pedaal 0x80 staan in byte 8 terwijl ze worden vastgehouden (gemeten met `od` op `/dev/shm/ravengun_JvsState`) |
| Spiegel naar de game | actief volgens het logboek ("shm mirror active ... [created fresh]") |
| Pedalen in de game | NEE: "Step on the left pedal to move!" en "Hold down the right pedal to attack!" reageren niet, de positie blijft R |
| Trekker in de game | NEE: de munitie blijft 9 (achter dekking kan schieten ook niet; niet los te meten zonder pedaal) |

De instructie liep door tot het eerste echte deel (op tijd, niet door onze invoer).

## Nog niet gedaan

- Dezelfde proef op de TeknoParrotUi-route (`--tpui`, daar schreef de daemon in het gedeelde geheugen van TeknoParrotUi): de
  oude notitie zegt dat richten, trekker en linker pedaal daar werkten. Als dat zo is, zit het verschil in de spiegel of in de
  plek waar de kern van deze versie de pedalen leest.
- Lezen wat de kern (TeknoParrot64.dll, 1.0.0.3761) voor TC5 uit `TeknoParrot_JvsState` haalt.

Status blijft `playable` (via TeknoParrotUi); niet `working` op de directe route.

Bijvangst van deze proef (opgelost in de loader, commit 0e467e3): met een Move EN een muis op dezelfde speler wiste elk
Move-rapport een vastgehouden muisknop. De knoppen tellen nu per bron als OR.
