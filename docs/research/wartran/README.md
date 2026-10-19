
## 2026-10-19 02:45: de controle is om — het spel start door naar zijn eigen BOOTING-scherm

Twee dingen waren nodig:

**1. De patch moet vóór de eerste code van het spel landen.** De motor patchte in een
achtergrondthread (`entry_win.c`, `CreateThread` in `DllMain`). Gemeten: de bytes stonden goed in het
geheugen (`game.exe+0x91496` = `90 90`), maar de vlag van het spel stond al op 2 — de controle was al
geweest. Daarom doet `DllMain` nu **eerst een synchrone ronde op de hoofdmodule**
(`GetModuleHandle(NULL)`; géén `CreateToolhelp32Snapshot`, want die wacht in DllMain op de
loader-lock) en pas daarna de thread voor alle andere modules.

**2. De uitslag moest 0 worden.** De functie zet in elke foutafhandeling dezelfde staart
(0x491517 → 0x491522: `movl $0x2,0x1a6be80`), dus ook met de CRC-tak gedempt bleef de vlag 2. De
gebiedslees (0x57b350) lukt zonder dongle niet → `test %eax,%eax` → foutpad → die staart. Met
0x9150b (`je` → `jmp`, altijd naar de gebiedscontrole) en 0x9159c (`jge` → `jmp`, zodat
`mov %ebx,0x1a6be80` wordt overgeslagen) loopt de functie naar 0x491587 (`movl $0,0x1a6be80`) en geeft
1 terug.

De volledige lijst staat in `patches/RobinHood/dropin/patchmem.txt` (8 regels). **Valkuil van die
avond:** de RVA is het adres *min* de image base 0x400000, dus 0x4913e6 is RVA **0x913e6**; met
0x13e6 klopt de `verwacht`-controle niet en past de motor die regel niet toe (en stopt hij bij de
eerste regel die niet klopt).

**Resultaat** (gemeten; NAS ongemoeid, md5 `6d5b7bcf5211271559ab63403d5d11f2` voor en na): het spel
gaat voorbij `ERROR / SECURITY CHECK BAD` en staat daarna in zijn eigen scherm
`BOOTING . / I/O ... CHECKING`. In het proces: vlag 0x1a6be80 = 0, code 0x1a6be74 = 0, gebied
0x68411c = 0 (gelezen uit `/proc/<pid>/mem`).

**Nieuwe blokkade: het I/O-bord.** Daar blijft het spel staan (13+ minuten, geen time-out, geen
foutmelding). De exe zoekt zijn bord via de Windows-apparatenlijst:
`SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailA`,
`SetupDiGetDeviceRegistryPropertyA`, met de apparaatnamen `\\.\USBPDCOM` en (voor de dongle)
`\\.\FEnteDev` / `\\.\HARDLOCK.VXD`. Onze eigen kern vult die opsomming niet, dus het spel vindt geen
bord. **Volgende stap:** de apparaatinterface-GUID uit de exe halen en met een drop-in (zoals
rg-wajvio/rg-mfcam) een bord aanbieden dat op de `DeviceIoControl`-verzoeken antwoordt — of de
opsomming zó beantwoorden dat het spel denkt dat het bord er is. Met de gesloten kern was dit spel
speelbaar, dus die kern deed hier iets.
