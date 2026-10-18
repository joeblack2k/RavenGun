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

## 6. De seriele poorten (2026-10-18): "Memory Card Port Open Error!" en het geweerbord

**Oorzaak (gemeten aan PTW.exe, niets gestart).** Het spel opent twee seriele poorten via WSC32.dll (die intern `CreateFileA("COMx")` doet): poort 0 =
geheugenkaartlezer (`0x47e140` -> `0x47e0f0`: `SioReset(0,0x400,0x200)`, `SioFlow('N')`, `SioBaud(7)`), poort 1 = geweerbord (`0x47d560`, vanaf
`0x41e84e`: `SioReset(1,0x400,0x200)`, `SioBaud(6)`, `SioRxClear`, `SioFlow('N')`, `SioInfo('V')`, `SioMessage(1,hwnd,0x400,1)`). Faalt `SioReset(0)` (geen COM1
onder Wine), dan toont `0x41e87c` het berichtvenster "Memory Card Port Open Error!"; faalt poort 1, dan alleen `OutputDebugString("iolink setup fail..")`.
De IAT-slots van WSC32 (0x4b1364..0x4b1394) zijn per ordinaal: 17 Reset, 25 TxQue, 8 DTR, 3 CTS, 19 RTS, 29 Message, 1 Baud, 9 Flow, 12 Info, 20 RxClear, 11 Gets, 6 Done, 15 Puts.

**Geheugenkaart.** Bit-banging: `SioPuts`/`SioTxQue` (0x47f540), `SioRTS('S'/'C')`/`SioDTR('S')` als klok en data, `SioCTS` als invoer (0x47f070: bit is 1 als
CTS == 0). Twee blokken van 16 bytes (adres 0 en 0x10) die allebei nul zijn geven -100 (0x47e1d0), en dat behandelt `0x42b380` als "geen kaart, verder". Dus: open
slaagt, alles geeft 0, `SioCTS` geeft altijd 1 (actief) -> alle bits 0 -> "geen kaart". Meer is niet nodig.

**Geweerbord.** Pakketten [type][lengte][...]; lengtetabel `0x47dad8` + `0x47da40`; de vensterprocedure verwerkt ze bij bericht 0x400 (`0x41ecb4`), tabel `0x41f7a0`:
`F` (2 bytes) trekker, `X`/`U`/`x` (4 bytes: `px`, `255-py`) richting, `d` (3) herladen (= toets S, gebeurtenis 0x1adb1), `t` (3) antwoord op `T`/`+`.
Het spel stuurt 5 gelijke tekens: `T`/`+` (0x42b0b0) en `G`/`H`, `B`/`C`; het herhaalt `T`/`+` tot het `t`-antwoord er is (0x41ff40), dus dat antwoord is verplicht.
Het schot gebeurt bij het X-pakket na een `F` (0x41ef88). Munt en start komen NIET als pakket: er is geen munt-pakket, het spel kent alleen de toetsen uit de tabel op `0x41e889`
(space, F1/F2, Home/End/Ins/Del/PgUp/PgDn, `/`, S) en de muis (WM_MOUSEMOVE `0x41ea47`, WM_LBUTTONDOWN `0x41f2aa`). Kalibratie van de richting: `correctionV2.txt`
(`640 480 10  x1 y1 .. x4 y4  offX offY`, 13 getallen); wij leveren een rechthoek zodat byte -> scherm lineair is (afleiding in `ptt_board.h`).

**Wat we gebouwd hebben.** `tools/rg-ptt/rg_ptt_wsc32.c` + `ptt_board.h` (model, getest in `tests/ptt_board_unit.c`, in `tests/ptt_wk_test.sh`), `build-wsc32.sh`; de drop-ins staan in
`PullTheTrigger.dropin.ini` (`wsc32.dll`, `correctionV2.txt`). Geen COM-poort, geen CreateFile-hook: de DLL IS WSC32.

**Open.** (1) Niets onder Wine gedraaid: de DLL is alleen gebouwd en statisch gecontroleerd (export-ordinalen en `ret N`), de logica is native getest. (2) Munt/start via de shm hebben
geen pakket: de eigenaar kan die nu alleen via toetsen geven (spel start al op de trekker; of munt nodig is, is niet gemeten). (3) Of `d` een enkele of een dubbele stap van het herladen
is ("PULL BACK/RELEASE") en of de offsets 1,1 in `correctionV2.txt` de richting goed zetten: te meten met het logboek en de tv.
