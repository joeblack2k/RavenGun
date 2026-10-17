# Pull The Trigger zonder TeknoParrot: wat TeknoParrot doet, wat wij vervangen, wat nog open is

Stand 2026-10-17. Alleen statisch onderzocht (NAS alleen gelezen); er is niets gestart. Bouw: `tools/rg-ptt/`; tests: `tests/ptt_wk_test.sh`.

## 1. Wat TeknoParrot voor dit spel doet (wat te bewijzen valt)

`PTW.exe` (32-bit, 835.584 bytes) is met **WibuKey** ingepakt (4 secties `.wkcrypt`, stub in `.text` op VA 0x4d2000). Geen WIBU-doos aan de kast:
het spel start dus alleen als de kern de doos nabootst. De kern is gepakt (`.teknoGo`), dus wat hij precies hookt is statisch niet te lezen; de rest
is wel met zekerheid vast te stellen uit het spelbestand:

| Onderdeel | Bewijs |
|---|---|
| WibuKey-stub: laadt `WKWIN32.DLL`, opent de doos met firmcode **250010** en usercode **13** ("Program needs WIBU-BOX with 250010:13."), laat de doos **een blok van 280 bytes** ontsleutelen (`WkbCrypt2`, commando 0x102) | stub `0x4d2540`/`0x4d24a0`/`0x4d28b0`; gegevens `0x4d4000..`; tekst in `.data` van de stub |
| Dat blok (RVA 0xbde00, 280 = 14 x 20 bytes) is **de importtabel van het spel**. De stub loopt er zelf doorheen (`0x4d2070`): `LoadLibraryA(naam)` + `GetProcAddress` en schrijft de IAT. PE-importmap wijst alleen naar de eigen 11 imports van de stub | `0x4d2070..0x4d21d0`, `[0x4d40f0]=0xbde00`, `[0x4d40f8]=0x118` |
| De **code van het spel is niet versleuteld** (entropie per bladzijde normaal, geen enkele bladzijde > 7,2) en roept de stub **nooit** aan (0 `call`/`jmp` naar 0x4d2000-0x4d3000, 0 verwijzingen naar zijn IAT) | `.wkcrypt` code 175 bladzijden gemeten |
| `PTFS.exe` verschilt 1.076 bytes van `PTW.exe`, allemaal in de PE-kop, in een code-bladzijde en in `.data`; de ciphertekst van het blok is **gelijk**. Dus PTFS = schermvullende variant, dezelfde bescherming | `cmp -l` |
| Invoer: seriele poort via **WSC32.dll** (klasse "WSJComm", `SioReset:COM%d`, `Port Open Success`, rij pakketten met een eerste byte 0x20..0x77 en lengtetabel op 0x47dad8). Het geweerbord zit dus aan een COM-poort, geen DirectInput/HID/USB | imports `WSC32.dll` (13 ordinalen), code op 0x47d5xx-0x47f5xx |
| Overige DLL's: `ijl11.dll` (JPEG), `OPENGL32`, `GLU32`, `WS2_32`, `IMM32`, `WINMM`, `ole32` — alle standaard of in de spelmap | herbouwde importtabel |

TeknoParrot's profiel `RawThrillsGUN` schrijft in het gedeelde geheugen Control-bits op offset 8 (test, service, munt, start, trekker,
granaat, herladen, per speler) en vier assen op 12/16/20/24 (`Pipes/RawThrillsGUN.cs`). Hoe de gesloten kern die in de serie-pakketten van het
spel omzet is niet statisch te zien.

## 2. Wat we hebben gebouwd: de doos zonder doos

De importtabel hoeft niet ontsleuteld te worden: alles wat erin staat is ook los in het bestand te vinden.

* De opzoektabellen (OFT, 13 groepen) liggen direct achter het blok (0xbdf18..), de IAT-groepen (FT) staan op 0xb1000 met dezelfde inhoud, de
  DLL-namen staan achter de hint/naam-blokken. De linker zette de groepen in volgorde van de naam (hoofdletters voor kleine letters):
  ADVAPI32, GDI32, GLU32, IMM32, KERNEL32, OPENGL32, SHELL32, USER32, WINMM, WS2_32, WSC32, ijl11, ole32.
* 13 DLL's + de nulafsluiting = **14 descriptors = 280 bytes**, precies de grootte van het versleutelde blok.
* Onafhankelijke controles: de code van het spel roept `SioReset` aan via 0x4b1364 en `SioBaud` via 0x4b137c; dat zijn de FT van WSC32 (0xb1364)
  en het zevende slot daarvan (ordinaal 1 = SioBaud). Ordinalen van `WS2_32` (115 = WSAStartup, 23 = socket, ...) en `ijl11` (2, 3, 4 =
  ijlInit/ijlFree/ijlRead) komen overeen met de echte exporttabellen.

`tools/rg-ptt/rg_ptt_imports.py` rekent die tabel uit het spelbestand uit (`ptt_imports.h` bevat hem plus de bestaande ciphertekst).
`tools/rg-ptt/rg_ptt_wk.c` is onze `WkWin32.dll` (freestanding, kernel32 only, ordinalen 20/33/34/35/37/45/46/47/72 zoals de stub ze zoekt):
`WkbGetVersion2` meldt versie 3.x, `WkbAccess2`/`WkbOpen2` geven een handvat, `WkbCrypt2` zet bij het versleutelde blok van 280 bytes de platte
tabel terug (bij andere inhoud of grootte: niets wijzigen, wel "gelukt" melden). Er wordt **geen sleutel of algoritme** nagebootst en geen
dongle-inhoud verzonnen. Het logboek `rg-wkwin32.log` in de map van de exe bewijst dat de DLL geladen is en wat hij beantwoordde.

Plan (met `rg-direct --plan`, kandidaat-`game.ini` + drop-in in een tijdelijke patches-map): geen lader, geen kern, argv =
`gamescope ... wine-prefix PTW.exe`, drop-in `WkWin32.dll` -> `views/PullTheTrigger/WkWin32.dll`.

## 3. Status en wat ontbreekt: de invoer

* **Wibu/start: kandidaat, klaar voor een tv-proef.** Onbewezen onder Wine (niet gestart); de redenering volgt de stub instructie voor instructie.
* **Invoer: geblokkeerd.** Zonder TeknoParrot leest niemand `TeknoParrot_JvsState`. Het spel verwacht een geweerbord op een COM-poort via
  WSC32 (`SioReset(poort, 0x400, 0x200)`, `SioFlow('N')`, `SioBaud(poort, 7)`, daarna `SioGets`/`SioPuts` met een eigen pakketformaat en
  RTS/DTR/CTS-stuurlijnen). Dat pakketprotocol is niet in kaart gebracht (de betekenis van de pakketten 0x20-0x77, de assen en de
  knoppen). Twee wegen: (a) een eigen `wsc32.dll` (28 exports `Sio*`) die dit bord uit het gedeelde geheugen nabootst — vraagt het hele
  protocol; (b) de ptylink/IO-brug van de daemon (COM-pty) met een bordmodule. Beide vragen eerst de protocolanalyse (klasse op 0x47d800-0x47ff00,
  pakkettypen via tabel 0x47dad8, antwoorden op 0x47da40 en de dispatch bij 0x47d8e0). Het is ook nog niet gemeten wat het spel doet als de poort
  er niet is ("Port Open Error"-tak): dat is precies wat de tv-proef toont.

## 4. De tv-proef (aan de lead)

1. In `patches/PullTheTrigger/`: `dropin/WkWin32.dll` (staat er al, inert) en de regels uit `docs/research/losse/PullTheTrigger.dropin.ini` in
   `dropin/dropin.ini`; `game.ini` voor de proef vervangen door `docs/research/losse/PullTheTrigger.game.ini` (alleen `[Loader] Core = none` is nieuw).
2. Dubbelklik. Verwacht zonder TP: geen `OpenParrotLoader`/`TeknoParrot.dll` in het proces; `views/PullTheTrigger/rg-wkwin32.log` met "geladen",
   "WkbOpen2 firm:user geaccepteerd: 250010:13", "WkbCrypt2: importtabel (280 bytes) vervangen door de platte tekst".
3. Mogelijke uitkomsten: (a) logboek ontbreekt: de DLL is niet geladen (Wine pakt de NAS-versie: dan de view controleren); (b) logboek compleet maar
   crash: importtabel klopt niet (kijk welke DLL de stub meldt); (c) venster/attract zonder invoer: Wibu-deel gelukt, dan komt de protocolanalyse.
4. Terugdraaien: oude `game.ini` terug en `dropin/` weg; de TeknoParrot-route blijft ongewijzigd.

## 5. Onzeker / niet vastgesteld

* Of de kern naast Wibu ook de seriele aanroepen hookt of alleen de WSC32-functies vervangt (niet te zien in de gepakte kern).
* Of er verderop in het spel nog een WibuKey- of ander controlepunt zit (de code roept de stub niet aan; andere controles zijn niet gezocht).
* Of Wine `LoadLibraryA("WKWIN32.dll")` de DLL uit de view pakt (zou moeten: map van de exe eerst).
* `COM`-nummer van het geweerbord en de baudcode 7 (WSC32-codering) zijn niet vastgesteld.
