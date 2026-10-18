# Infinity Blade FX zonder TeknoParrot

Stand 2026-10-17. Alleen statische metingen (objdump op `SwordGame.exe` en `KaboomComDll.dll` van de NAS, alleen gelezen) en
`build/rg-direct --plan`; er is niets gestart.

## Wat TeknoParrot hier deed

1. **De kern in het gameproces** (`OpenParrotLoader.exe` + `TeknoParrot.dll`, EmulationProfile `InfinityBlade`; UI-kant: `TC5Pipe`, dezelfde
   gedeelde-geheugenindeling als Time Crisis 5: munt 0x08, aanraking/trekker, X en Y als bytes). Wat de kern met die waarden doet is gesloten; gemeten
   zijn alleen de gevolgen: de munt (`CKaboomCom`) komt niet aan, de rest (UE3, DXVK, Matrix) start zonder kern ook (`patches/ZONDER-LADER.md`: START en
   leeft 70 s).
2. **De TPFIX-bestanden in de map van de game** (`Binaries/Win32/readme.txt`): TeknoParrot's eigen `dinput8.dll` en `TPFIX.ini`; `TPFIX.ini` bevat
   `[DLL] path=…\TeknoParrot.dll` en laadt dus de gesloten kern (in de view staat zo'n kopie met het pad naar de runtime). Een tweede, verborgen
   TeknoParrot-onderdeel. TeknoParrot's `matrix32.dll` in dezelfde map is al door onze Matrix-drop-in vervangen.
3. **Het Kaboom-bord** (munten, toetsenblok, kaartjes): `SwordGame.exe` heeft `CKaboomCom` ingebakken en praat via kernel32 met `\\.\COM9`
   (`push 9 / call 0x233ece0` op 0x1cdc7b2). Zonder bord: `Kaboom: InitLib() failed`, munt 0/4 credits. De kern deed dit nagebootst.

## Vervanging

- `[Loader] Build = ravengun`, `Core = ravengun`: eigen lader en generieke kern (CRC `0x90d5e6f8` onbekend, dus geen hooks).
- `[Wine] DllOverrides = dinput8=b`: Wine's eigen dinput8 in plaats van TPFIX (laadt dan nooit `TeknoParrot.dll`). Gemeten in het plan:
  `WINEDLLOVERRIDES=dinput8=b;matrix32=n;d3d8=n;…`.
- **`rg-kaboom.dll`** (`tools/rg-kaboom/`, gebouwd met `tools/rg-kaboom/build.sh`, 32 bits, alleen kernel32 en user32): drop-in met
  `Preload = true`, geladen door de lader (`TP_PRELOAD_DLL`). Legt de kernel32-ingangen van de hoofdmodule om (CreateFileW, ReadFile, WriteFile,
  CloseHandle, Get/SetCommState, SetCommMask, SetCommTimeouts, PurgeComm, ClearCommError) en bootst voor `\\.\COM9` het bord na. Protocol (gemeten in
  `KaboomComDll.dll` en in de ingebakken kopie in de exe; de commandonummers, magics en controlesom zijn gelijk):
  pakket `magic u16, controlesom u16 (som van de bytes vanaf lengte), lengte u16, commando u16, lading`; host-magic 0xF10E (of 0x9A44), bord-magic 0xB00F;
  commando 6 (18 bytes, toetsenblok op 12..15), 8/9 (kaartjes te geven), 0xA (uitgegeven), 0xB (reset), 0xC (muntteller, 2 × u16), 0xD (reset muntteller).
  De muntteller telt op bij de stijgende flank van bit 0x08 in het woord op offset 8 van `TeknoParrot_JvsState` (indeling `TimeCrisis5`, `coin1`).
- Tests: `tests/kaboom_unit.c` via `tests/kaboom_test.sh` (pakketten, controlesom, munt-flank, kapotte pakketten, deellezen) en de DLL-bouw.

## TouchFX / PQMTClient.dll (tv-meting 2026-10-17: wit scherm)

Met `rg-kaboom.dll` slaagt Kaboom (`InitLib() succeeded`), maar het scherm blijft wit en `Launch.log` stopt 29 s na
`TouchFX Error: ConnectServer() failed`. Statisch gemeten waarom:

- "TouchFX" is de PQ-Labs-multitouchclient `PQMTClient.dll` (map van de game, 16 exports). De game (`SwordGame.exe` 0x1cde140) doet, allemaal cdecl:
  `SetOnReceivePointFrame(0x1cde110, 0)`, `SetOnGetDeviceInfo(0x1cdc760, 0)`, `ConnectServer("<ip>" op 0x2c383e4, 0x5433)`; 0 = geslaagd, anders
  `ConnectServer() failed`. Dan `SendRequest(&zero_struct{type=1})` en `SendThreshold(10)`, ook 0 = geslaagd.
- De echte client opent een TCP-verbinding met een PQ-server (poort 0x5433 = 21555, ip uit de game); die server is er niet. De kern loste dat op door de client
  of de verbinding over te nemen (de kern is gepakt; alleen het gevolg is gezien).
- De aanraking komt via de callback `(frame_id, time_stamp, aantal, const TouchPoint*, ctx)` (cdecl). `TouchPoint` is 16 bytes `{gebeurtenis, x, y, rest}`;
  de game bewaart van het eerste punt `u16 gebeurtenis (0 neer, 1 beweegt, 2 op)`, `u16 frame`, `x`, `y` in een globale (0x31e58a0) en gebruikt x/y als
  muispositie in de client-rechthoek van het venster (zonder aanraking: `GetCursorPos` + `ScreenToClient`).

Vervanging: `tools/rg-pqmt/` bouwt een eigen `PQMTClient.dll` (zelfde 16 exports met dezelfde ordinalen, alleen kernel32 en user32). ConnectServer, SendRequest
en SendThreshold slagen; een draadje geeft de aanraking door uit het gedeelde geheugen: trekker (`p1.trigger`, 0x10) = vinger neer, `p1.x`/`p1.y` (offset 12/16)
naar pixels van de client-rechthoek van het grootste zichtbare venster (of `RG_PQMT_W`/`_H`); buiten beeld (as <= 1 of >= 254) = vinger op. De logica
(`pqmt_logic.h`) is native getest (`tests/pqmt_unit.c`, `tests/pqmt_test.sh`), de exports en de imports van de DLL ook. Sectie `[PQMTClient.dll]` in
`InfinityBladeFX.dropin.ini` (`DllOverride = native`, logboek `rg-pqmt.log` in de view).

Niet bewezen: of dit de enige reden van het witte scherm is (de volgende fout zou na de TouchFX-regel in `Launch.log` staan); of de game zijn aanraakpad
gebruikt (er zijn twee globale vlaggen, 0x3103d74 en 0x307725c, die kiezen tussen de aanraking en de muis); de schaal van de coordinaten (aangenomen:
pixels van de client-rechthoek).

## Niet bewezen

- Of het nagebootste bord in de exe voldoet: de commandonummers 6, 8-0xD staan in de exe-kopie met dezelfde antwoordlengtes, maar ze zijn niet in een draaiende
  game gezien. `RG_KABOOM_LOG=rg-kaboom.log` laat per pakket zien wat er gebeurt; ontbreekt het logboek dan is de DLL niet geladen.
- Wat de kern voor aanraking/zwaard deed. Zonder kern werkt de muis (de daemon in muismodus) alleen als UE3 hem als aanraking leest. De cursor/crosshair
  (`P1.png`, `Crosshair/Display`) tekende de kern; die is dus weg.
- De hook staat alleen in de IAT van de hoofdmodule; roept een andere module de seriele functies aan, dan wordt dat niet onderschept.

## Status en TV-test

Kandidaat: `docs/research/losse/InfinityBladeFX.game.ini` plus de sectie uit `docs/research/losse/InfinityBladeFX.dropin.ini` (nog niet in
`patches/InfinityBladeFX/dropin/dropin.ini`: tot de wissel zou de DLL anders ook naast de TeknoParrot-kern laden).

Test: de twee bestanden in de gebruikerslaag zetten (of `game.ini`/`dropin.ini` vervangen), dubbelklik. Verwacht: UE3-opstartscherm, attract; in
`views/InfinityBladeFX/Binaries/Win32/rg-kaboom.log` de regels `geladen: N kernel32-ingangen omgelegd` (N > 0), `CreateFileW COM9`, `pakket … beantwoord`;
munt (toets 5) geeft `munt: teller nu 1` en `1/4 CREDITS` in plaats van `0/4`. In het logboek van de run mag `TeknoParrot.dll` niet meer voorkomen.

## Wit beeld op de kandidaat: de opdrachtregel van de kern ontbrak (2026-10-18)

Gemeten in de logboeken van de game (`SwordGame/Logs/Launch*.log`, in de schrijflaag en in de view; niets gestart):

| Runs | `Init: Command line:` | `Shader platform (RHI):` | Beeld |
|---|---|---|---|
| 9 met de kern van TeknoParrot (11-17 okt) | `-Fullscreen -simmobileinput`, bij Windowed = 1 `-ResX=1280 -ResY=853 -Windowed -simmobileinput` | `PC-D3D-SM3` (D3D9) | Fullscreen: wit; Windowed: attract |
| 2 op de kandidaat (17 okt 17:46, 18 okt 11:23) | leeg | `PC-D3D-SM4` + `Found D3D10 adapter 0: AMD Radeon RX 6950 XT (RADV NAVI21)` | wit, geluid wel |

De gesloten kern gaf de game dus altijd een opdrachtregel mee; de generieke kern geeft er geen. Zonder `-Windowed` start UE3 exclusief
schermvullend (in gamescope bleef dat al wit), en zonder `-simmobileinput` kiest de game de D3D10-weg.
Statisch (SwordGame.exe, imagebase 0x400000, alleen gelezen): de RHI-keuze staat op VA 0x73e4d8..0x73e593 en is de enige aanroeper van zowel de
D3D10-RHI (0x1c9b8c0, alleen vanaf 0x73e58c) als de D3D9-RHI (0x1cb9600, alleen vanaf 0x73e593). `ParseParam` (0x638090) test op een heel woord
(na de naam moet een spatie, tab of einde volgen), dus `-simmobileinput` is NIET `-simmobile`. D3D10 wordt gekozen als het vlagwoord
`ds:0x31af960` gezet is (cmp op 0x73e54d, `jne` op 0x73e553, bytes `75 2e`) of `-d3d10`/`-sm4` meekomt; dat woord wordt nergens met een absoluut adres
beschreven (een enkele verwijzing in het hele bestand), het komt dus uit een tabel of struct (vermoedelijk `AllowD3D10=True` in
`SwordGame/Config/SwordEngine.ini` r. 768, sectie SystemSettings). Welk deel van het verschil (SM3/SM4 of Windowed) het witte beeld veroorzaakt is niet
gescheiden gemeten: de oplossing zet beide terug.

Oplossing: `[Launch] Arguments = -ResX=1280 -ResY=853 -Windowed -simmobileinput` (de OpenParrot-lader krijgt alles als een parameter,
`spel_argumenten`). Alternatief voor proef 2: `RG_KABOOM_PATCH=0x33e553:752e:9090` (rg-kaboom.dll) maakt van de `jne` twee nop's, zodat zonder
`-d3d10`/`-sm4` altijd D3D9 gekozen wordt.

### Correctie na tv-ronde 40 (2026-10-18 14:10)

De kandidaat met `Arguments = -ResX=1280 -ResY=853 -Windowed -simmobileinput` WERKT (attract, munt 0/4 -> 2/4, geluid). Het echte logboek van die run staat NIET in de view
(`views/InfinityBladeFX/SwordGame/Logs/Launch.log` is daar nog een koppeling naar het oude NAS-bestand van 11 okt, met `-Fullscreen -simmobileinput` en SM3) maar in de
schrijflaag: `~/.local/share/ravengun/writelayer/InfinityBladeFX/home_deck_mnt_spidernas_emulation_Teknoparrot/upper/Infinity Blade FX/SwordGame/Logs/Launch.log`. Daar staat:
`Init: Command line: -ResX=1280 -ResY=853 -Windowed -simmobileinput`, `Shader platform (RHI): PC-D3D-SM4`, `Kaboom: InitLib() succeeded`. De launcher
heeft de argumenten dus niet vervangen. SM4 (D3D10 via DXVK) is NIET de oorzaak van het witte beeld; de oorzaak was het ontbreken van `-Windowed` (exclusief
schermvullend in gamescope). De SM3/SM4-redenering hierboven is daarmee weerlegd; de `RG_KABOOM_PATCH` (alt-variant) is niet nodig. Open: `ViewRealDirs = SwordGame/Logs`
maakt de logmap niet echt lokaal (Launch.log blijft een koppeling; de game schrijft via de schrijflaag), dus lees dat logboek daar.
