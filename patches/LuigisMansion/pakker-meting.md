# Luigi's Mansion: de pakker in `VACUUM.exe` gemeten

Gemeten 2026-10-12 door RavenGun, alleen lezen op de NAS. Bron:
`Luigi's Mansion Arcade/exe/x64/VACUUM.exe` (3.436.544 B). Narekenen: `re/_vacuum-pe2.py`.

## Wat er in het bestand staat

Het is een **x64 PE32+** met `ImageBase = 0x140000000` en het entry point op RVA `0xf23125`
(VA `0x140f23125`). En dan het belangrijkste:

| sectie | VA | virtuele omvang | **bytes in het bestand** | rechten |
|---|---|---|---|---|
| `.text` | `0x00001000` | 3.496.500 | **0** | R-X |
| `.rdata` | `0x00357000` | 1.329.995 | **0** | R-- |
| `.data` | `0x0049c000` | 9.083.712 | **0** | RW- |
| `.pdata` | `0x00d46000` | 172.140 | **0** | R-- |
| `.capcom0` | `0x00d71000` | 1.719.331 | **0** | R-X |
| `.capcom1` | `0x00f15000` | 3.434.464 | **3.434.496** (offset `0x400`) | R-X |
| `.reloc` | `0x0125c000` | 24 | 512 | R-- |
| `.rsrc` | `0x0125d000` | 233 | 512 | R-- |

**Vijf secties hebben een groot virtueel bereik en nul bytes in het bestand.** De enige sectie met echte
data is **`.capcom1`**, met een **entropie van 7,9553** -- dat is de ontpakker. En het **entry point valt
binnen `.capcom1`**: het programma begint dus in de stub, niet in het spel.

De datadir bevestigt het: `import` (RVA `0x10980d8`), `export` (`0x121cc00`), `exception`
(`0x122eb70`), `iat` (`0x120d000`) en `loadconfig` (`0x122eb00`) wijzen allemaal naar bereiken waarvan de
bytes **niet in het bestand** staan. Er is dus ook geen bruikbare import-tabel om statisch uit te lezen.

## Wat dat verklaart

Wie op het entry point pauzeert, injecteert of schrijft, **breekt het ontpakken** -- en krijgt dan
"illegal instruction"-achtige symptomen zonder dat er iets mis is met het spel. Dat is precies wat de
eerdere notitie beschreef.

## Een correctie op de eerdere notitie

In `packages/games-status.md` staat bij Pigs Might Fly: *"injectie vanaf het startpunt geeft 'Unhandled
illegal instruction at address 0044A5F4' (zoals Luigi's Mansion)"*.

**Dat adres bestaat niet in dit image.** Met `ImageBase = 0x140000000` is `0x0044A5F4` een 32-bits
adres, en op de bijbehorende plek in het bestand staat hoogentropische data (`ab ea 5f 3b df 56 31 53
…`). Het adres komt vrijwel zeker uit **Pigs Might Fly's 32-bits `Shell.exe`**, en de notitie heeft het
naar Luigi's Mansion gegeneraliseerd -- "zoals Luigi's Mansion" ging over de **soort** fout, niet over
hetzelfde adres. Wie dit leest: zoek in `VACUUM.exe` niet naar `0x0044A5F4`.

## De weg vooruit

1. **Het beeld dumpen uit het geheugen**, precies zoals bij BudgieLoader is gedaan: het spel met de
   werkende route starten en dan `/proc/<pid>/mem` uitlezen terwijl het draait. Dat geeft de ontpakte
   `.text`, de echte import-tabel, de echte relocaties en het echte **OEP** (original entry point).
2. Twee valkuilen uit die eerdere meting: `systemd-logind` doodt de procesgroep als de ssh-sessie sluit
   (draai als systemd-user-unit), en `ptrace_scope=1` is geen probleem zolang het dumpende script de
   **ouder** is van het spel.
3. Daarna is het gewone laderwerk: het OEP als startpunt, de echte imports oplossen, en de I/O via het
   gedeelde geheugen `TeknoParrot_JvsState` (`inputProtocol shared-luigi-mansion`, bits op offset 8, assen
   op 12..15).

Het OEP is ook zonder dump te vinden (hardware-breakpoint op de eerste schrijfactie in `.text`, of de
laatste sprong van de stub volgen), maar de dump geeft méér: de imports en relocaties die het spel
werkelijk gebruikt.


## De twee helften: invoer is al van ons, de lader nog niet

Bij het natrekken van de I/O-kant bleek iets belangrijks: **de invoerhelft bestaat al in dit project.**

* `packaging/tp-shm-profiles.json` heeft een volledig profiel **`LuigisMansion`** met het protocol
  `shared-luigi-mansion`: stuurwoord van 32 bits op offset 8 (start bit 0/3, vacuümpknop = Strobulb op
  bit 1/4, vacuümhendel op bit 2/5, test 8, select switch P1 9, service 10, screen-out 16/17) en vier
  asbytes op 12..15 (P1Y, P1X, P2Y, P2X), kalibratie X 3..252 en Y 6..250, links/boven = 0.
* `scripts/tp-shm-swap.sh` laat onze daemon de **enige schrijver** van dat invoerblok zijn: TeknoParrotUi
  start nog wel de lader, maar zijn eigen invoer bereikt de game niet meer.

Dus de stand is: **onze invoer, hun lader.** Wat ontbreekt is precies de laderhelft, en dat is de gepakte
`VACUUM.exe`.

## En wat het eigen onderzoek over de moeilijkheid zegt

In `docs/research/zonder-teknoparrot.md` staat Luigi's Mansion beoordeeld als **"moeite: zeer hoog"**,
met het advies **"laatste in de rij, bij voorkeur TP onder Wine blijven proberen"**. De reden die daar
staat: een eigen **Sega Nu-platformlaag** met `oxGetHwInfo`, het I/O-bord `LMAIO.bin` via COM1, de drives
`X:` en `Y:`, keychip en netwerk. Ook: geen enkel open-sourceproject dekt deze game (Segatools noemt hem
niet).

**Dat is geen reden om het niet te doen, maar wel om te weten waar we aan beginnen.** Het is het meest
waarschijnlijke van de zes om veel tijd te kosten, en het is het enige van de zes dat al speelt.

De I/O-kant is bovendien niet het probleem dat overblijft: `game.bat` flasht firmware naar het bord via
COM1 (`sendFlash COM1 LMAIO.bin`), en uit de bevindingen blijkt die flash **niet nodig** te zijn -- onder
TeknoParrot speelt de game met alleen het gedeelde geheugen. De open vraag uit `teknoparrot-io.md` ("wat
doet de game op COM1 als er geen bord is") is dus de vraag die onze lader moet beantwoorden: het spel
moet niet stuklopen op een afwezige poort.
