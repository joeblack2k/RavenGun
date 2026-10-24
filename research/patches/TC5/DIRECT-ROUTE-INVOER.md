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

## Vervolg 2026-10-14 19:35

- Onderzoek (open TeknoParrotUI-bron): TC5 heeft GEEN named pipe (PipeFactory geeft null), alleen TC5Pipe naar het gedeelde
  geheugen; onze indeling `TimeCrisis5` is byte voor byte gelijk aan TC5Pipe.cs. Prefix en lader gelijk aan de oude route.
- Getest: het bronbestand bij de start op nul (rg-direct, commit c305efa). Bytes 8..19 daarna schoon (`00 00 00 00 82 00 00 00 7f`),
  maar de game reageert nog steeds niet op pedalen of trekker (tc2224.png). Die hypothese is weerlegd.
- Open: of de kern de bytes in Wine ziet (een probe in de prefix die TeknoParrot_JvsState leest), de HASP-drop-in (sinds 11-10, de
  oude werkende route had hem niet), pipehelper.exe in plaats van rg-jvsbridge.exe. Pass 2 (docs/goal-90-native.md).

## Vervolg 2026-10-15

- **HASP-drop-in uit** (de drie `hasp_windows_x64*.dll`-secties tijdelijk uit `dropin.ini`, in de view weer links naar de originele
  runtime van 788 KB): de game start, munt werkt, maar het linkerpedaal (positie blijft R), de trekker (munitie blijft 9) en het
  schieten op SKIP doen niets. De drop-in is dus niet de oorzaak; hij staat weer aan.
- **Injectie bij het entry point** (`[Loader] RemoteThread = off`, `PostStartMs =` leeg): de kern laadt ("Resolved target
  kernel32.dll procedure"), maar nu komt zelfs de munt (Shift+T) niet meer aan. Teruggezet naar `PostStartMs = 3000`.
- De kern `TeknoParrot64.dll` is gepakt (code in een versleutelde sectie `.teknoGo`); hoe hij de USIO-invoer (WinUsb) voor TC5
  invult, is alleen in een draaiend proces te zien. Volgende stap: een geheugendump van het spelproces, of de TeknoParrotUi-route
  ter vergelijking (die schreef eerder wel invoer door).
