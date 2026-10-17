# Transformers: Shadows Rising zonder TeknoParrot

Stand 2026-10-17. Alleen statische metingen en het plan van de launcher (`build/rg-direct --plan`); er is niets gestart.

## Wat TeknoParrot hier deed

- De lader (`OpenParrotLoader.exe`, eigen build) injecteert de gesloten `TeknoParrot.dll` in `Shell.exe` (de SEGA Game Shell, 32 bits). De kern
  zit **alleen in de shell**, niet in `Transformers2.exe` (`patches/TransformersShadowsRising/README.md`, `REPORT-io-board-and-loader.md` kop 1).
- De invoer loopt **niet** door de kern: `[Input] Route = hid-board` (SaeGunBoard via uinput voor de DirectInput-gun van
  `Transformers2.exe`, SaeIoBoard via uhid voor het IO-bord dat de shell zoekt). `--route tp-shm` werkt hier niet omdat de kern niet in het
  gameproces zit.
- Dongle: `Sega/shell/dk2win32.dll` (Dinkey) meldt `MainThreadDongleScan: failed stage 1`; gemeten dat het niet blokkeert. Deze dump is
  voor-gepatcht (`Shell.exe` forceert het IO-bord en slaat de `/k=`-checksum over; `Game.ini` heeft `SecurityDisabled=1` en `NoVerify=1`).
- Vensterkeuze, padherleiding en regio: niets gevonden dat de kern voor deze game doet; `REPORT-io-board-and-loader.md` (kop "Where our own loader stands") noemt het sterk vermoed dat de kern
  hier niets doet; dat is door deze kandidaat nog niet op de tv bevestigd.

## Vervanging

`[Loader] Build = ravengun` en `Core = ravengun`: de lader en kern komen uit onze bron (`third_party/openparrot`). De kern kent de game niet
(CRC `0x55f66578` zit niet in de tabel), dus de generieke route van patch 0003 draait: geen hooks, niets gepatcht. Plan gemeten: argv
`wine OpenParrotLoader.exe …\OpenParrot …\shell\Shell.exe`, `core_source = ravengun`. De `teknoparrot.ini` in de view schrijft de launcher zelf.

Verdere stap: `Core = none` (Wine start `Shell.exe` zelf). De plan-uitvoer klopt (`argv = [wine, Shell.exe]`), maar de launcher zet de werkmap dan op de
TeknoParrot-map (`direct.cpp`: `cwd = elf_like ? gamedir : tp`) in plaats van de map van de exe, terwijl de lader die normaal zelf kiest. De shell leest
relatieve bestanden (`Game.ini`, logboeken). Pas `Core = none` toe als de launcher bij `zonder_lader` de map van de exe (of `[Launch] WorkingDirectory`)
als werkmap neemt.

## Status

Kandidaat, klaar om te testen: `docs/research/losse/TransformersShadowsRising.game.ini` (alleen het `[Loader]`-blok verschilt).

TV-test: dubbelklik zoals nu, laden duurt ~2 minuten; verwacht dezelfde attract (PRESS START BUTTON, FREE PLAY), START met toets 1, richten en schieten.
Controle dat er niets van TeknoParrot meedraait: `build/rg-direct --plan TransformersShadowsRising` met de kandidaat moet `core_source = ravengun` en
een lader onder `…/openparrot/current/` tonen; in het logboek van de run staat `kern (ravengun)` in plaats van `kern (runtime)`.
Open (niet nieuw): de hendel is niet nagedaan.
