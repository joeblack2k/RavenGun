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
