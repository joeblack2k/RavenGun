# Nachtdienst 19 oktober 2026 — korte stand voor Jeroen

Alles hieronder is gecommit en gepusht op `integratie-installatie-app`; de hele proefserie is groen
(94 van 94) en de machine is schoon achtergelaten (geen spel, geen AMAuthd, de daemon draait,
muisgeweer op speler 1).

## Wat er vannacht af is

1. **Samenvoegingen.** De nagekeken takken `claude/pass2-review3` (poortbaar/installatie/native) en
   `claude/pass2-review7` (OLED-proef, AL_GOED-regressie, absolute lijstpaden in de motor) zijn
   samengevoegd; alles bouwt en de proeven zijn groen.
2. **Poortbare map bewezen.** `dist/RavenGun` is ververst met een gewone run (gebruikersmap bleef
   staan) en de SDR-proef is herhaald: met `~/.local/share/ravengun`, `~/.config/ravengun` en
   `~/.local/opt/ravengun` weggelegd start de daemon uit de map, draait SDR langer dan twee minuten
   attract en komt alles daarna terug. Foto en statusbestand staan in
   `bench-captures/SDR/20261019-portable-isolation-proof.*`.
3. **Robin Hood: het I/O-bord doorgrond.** De veiligheidscontrole was al om; vannacht is het
   bordprotocol gevonden en gedecodeerd. De haken loggen nu ook de aanroeper, en daarmee is de
   bordhulpfunctie van het spel te vinden (0x4902a0-0x4907xx). De lezer van het antwoord
   (0x490357-0x4904a0) eist: **byte 0 moet een van {0x00, 0xFF, 0x94..0x98, 0x23, 0x24, 0x29, 0x30}
   zijn en byte 6 is de som van de eerste zes bytes** (dezelfde kaderregel als de opdrachtkaders).
   Onze echo had twee fouten (verkeerde eerste byte, en de tweede lezing gaf byte 0..5 in plaats van
   1..6); die zijn gerepareerd, het spel leest nu (462 keer in 30 s, eerst 1 byte dan 6) en elk
   frame-paar staat in het logboek. Alle tien geldige codes zijn geprobeerd: het spel blijft op
   `BOOTING . / I/O ... CHECKING` staan, dus de **lading** van het antwoord moet de bordtoestand
   bevatten. Zie `docs/research/wartran/README.md`.
4. **Mario Kart: de ALL.Net-authenticatie werkt.** Het spel vraagt bij het opstarten
   `CoCreateInstance(CLSID {4603BB03-...}, CLSCTX_LOCAL_SERVER, IID {045A5150-...})` en kreeg altijd
   `CLASS_E_CLASSNOTREG`. De klasse zit in `AMCUS/AMAuthd.exe` en wordt pas vindbaar als
   `AMCUS/iauthdll.dll` geregistreerd is (`regsvr32`, eenmalig per prefix). Het profiel start
   AMAuthd nu zelf als tweede exe (zoals de kast in `init_w10.ps1`), en daarmee geeft die aanroep
   **hr 0x00000000**. Het beeld blijft daarna nog zwart, dus dit is een noodzakelijke stap die
   werkt, niet de laatste. Zie `docs/research/namco/README.md`.
5. **Opgeruimd** uit de reviewrondes: de OLED-proef gebruikt de gebouwde app; de geheugen-motor
   laadt de haak-DLL ook zonder patchlijst; `rg_rhio` heeft een slot om zijn bordtoestand, meldt de
   resterende bytes in `ClearCommError`, leest de omgevingsstand een keer en heeft een bouwscript;
   de meetknoppen staan niet meer in het standaardprofiel van Mario Kart; de TC5-noten zeggen alleen
   wat op onze eigen kern gemeten is (titelscherm en Unreal-splash met INSERT 1 CREDIT(S), 77 s);
   de Wartran-README zegt negen geheugenpatches.

## Wat er nog open staat (met de volgende stap erbij)

* **Robin Hood**: welke bytes van het antwoord moeten de bordtoestand zijn? De aanroeper van
  0x4902a0 nakijken (wat doet hij met de bewaarde waarde) en/of de daemon zijn bordbeeld
  (`TeknoParrot_JvsState`, indeling WartranTroopers) in het antwoord laten meelopen. Voor **Coopers
  9** (zelfde familie) geldt vermoedelijk hetzelfde protocol: dezelfde 7-byte kaders, dezelfde
  somregel, dezelfde lijst geldige codes — dat is daar de eerste zaak om te proberen. Coopers 9 valt
  nu om in zijn eigen vroege start (`0x4010d0` -> page fault op `0x4fd8aed0`); de `load.exe`-tekst op
  VA 0x512424 is geen route (geen `CreateProcess`-import) en de haken van onze kern zijn niet een
  voor een uit te zetten zonder de bron van de kern in huis.
* **Mario Kart**: welke van de 29 methodes van IAuth roept het spel aan, en wat antwoordt de echte
  AMAuthd daarop? Het antwoordlogboek van rg-mfcam laat de methodeaanroepen zien; `AMConfig.ini`
  kent `amdcfg-authType` = `ALL.NET` | `OFFLINE` | `NBLINE` (een OFFLINE-kopie is nog niet
  geprobeerd). Daarna blijft de ShareK-poortwachter (`obj+0x394`, `obj+0`) staan.
* **Geparkeerd met notitie**: AfterDark (dongle-controle), PigsMightFly (PS Move nodig),
  LostLandAdventure (WAJVGetStatus-lus), de nog niet geteste profielen (HOTD4SP, MenInBlack,
  SegaGameShell, 3DCosplayMahjong, AliensArmageddon, TNXK0O~C) en de geheugen-haken op
  `CreateFileMappingA`/`OpenFileMappingA`/`MapViewOfFile` voor Robin Hood (die lieten de DLL niet
  meer laden).

## Laatste twee blokken (05:25-06:05)

* **Robin Hood, bordtoestand in het antwoord.** `rg_rhio` vult de bytes 1-5 van zijn antwoord nu uit
  **ons eigen gedeelde geheugen**: de mapping `TeknoParrot_JvsState` die de daemon al schrijft
  (indeling WartranTroopers; besturing op offset 8, richtbytes op 12..19). In het logboek staat
  `bordbeeld: mapping TeknoParrot_JvsState gekoppeld` en bijvoorbeeld
  `opdracht 25 c7 66 1b 23 00 90 -> antwoord 94 00 80 80 00 00 94` (byte 1 = besturing, byte 2/3 =
  richtas van speler 1 op het midden 0x80, byte 6 = de som). Met `RG_RHIO_BORD=0` valt hij terug op
  de echo. **Het spel blijft nog op `BOOTING . / I/O ... CHECKING` staan.** Wel belangrijk: de lezer
  op 0x490357-0x4904a0 vergelijkt *alleen* byte 0 (de lijst geldige codes) en byte 6 (de som) --
  onze kaders worden dus geaccepteerd, en de I/O-controle hangt aan iets anders. Twee sporen staan in
  `docs/research/wartran/README.md`: de aanroeper van de hulpfunctie volgen (het echte functiebegin
  ligt vóór 0x4902a0), en de daemon een munt/start laten aanbieden terwijl de controle loopt.
* **Mario Kart, `authType=OFFLINE`.** Een kopie van `AMCUS/AMConfig.ini` in de view met
  `amdcfg-authType=OFFLINE` (de NAS-original is niet aangeraakt) is geprobeerd: de IAuth-aanroep
  geeft nog steeds `hr 0` en het beeld blijft zwart, dus OFFLINE maakt voor deze hobbel geen verschil.
  De methodeaanroepen van de echte AMAuthd zijn van buiten niet te zien; onze eigen proef liet wel
  zien dat het spel direct na het aanmaken **slot 9** (vtable+0x24) aanroept en daarna netjes afsluit
  (met de echte server blijft het draaien). Een doorgeefobject dat logt en doorgeeft vraagt de
  ariteiten van de 32 methodes uit `AMCUS/iauthdll.dll` -- dat is de volgende stap.
