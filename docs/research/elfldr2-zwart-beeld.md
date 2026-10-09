# ElfLdr2 32-bit: zwart beeld op volledig scherm (onderzoek, 2026-10-09)

Aanduiding per bewering: [bron] = uit bron gelezen (URL of lokaal bestand erbij), [afgeleid] = afgeleid of onzeker.

## Kort oordeel

Er is online geen enkele melding gevonden die precies dit probleem beschrijft (zwart beeld op volledig scherm bij 32-bit ElfLdr2/BudgieLoader onder Wine/Linux of Windows). Het meest concrete nieuws komt uit de eigen TPUI-binary: TPUI 2.0.0.130 Linux heeft een Gamescope-functie ("Fullscreen game scaling"), en die slaat ElfLdr2-profielen standaard over.

## Vraag 1: is het probleem bekend, en wat zijn de oplossingen?

Niet gevonden: een issue, forumpost of video over zwart beeld op volledig scherm bij Jurassic Park, Terminator Salvation, Aliens Armageddon of Big Buck met ElfLdr2. Gezocht in teknogods/TeknoParrot, TeknoParrotUI en teknoparrot-issues via de GitHub-zoek-API en websearch.

Wel gelezen:
- [bron] TeknoParrot issue 83 "ELFLDR2 / Budgie Loader issue with jurrasic Park Arcade" (https://github.com/teknogods/TeknoParrot/issues/83): Windows 10, geen geluid ("sound init failed"), crashes bij missiekeuze. Geen woord over beeld, volledig scherm of oplossing; onbeantwoord door onderhouders.
- [bron] teknoparrot-issues 189 (Aliens Armageddon crasht in attract, gesloten) en TeknoParrot issue 51 (Terminator Salvation kan gameplay niet ingaan, open): andere problemen dan het onze.
- [bron] WineHQ-bug 52808 (https://list.winehq.org/pipermail/wine-bugs/2022-May/574480.html): volledig scherm schaalt niet goed in oudere apps onder Wayland; door de reporter later als Wayland-specifiek heringedeeld.
- [bron] WineHQ-bug 42761 (https://www.winehq.org/pipermail/wine-bugs/2017-July/473157.html): een reactie meldt dat Xwayland geen xrandr-modewissel ondersteunt, zodat oude games hun verwachte resolutie niet kunnen instellen.
- [bron] akitaonrails/distrobox-gaming issue 7 (https://github.com/akitaonrails/distrobox-gaming/issues/7): wie de Windows-UI onder Wine draait, kreeg games die exclusief volledig scherm gebruiken en een verkeerde resolutie (3804x2085) kozen; oplossing was een Explorer-virtueel bureaublad van 1920x1080. Ook: UseEGL=N (GLX) nodig voor een zwarte UI. Raw Thrills wordt er niet genoemd. De auteur heeft de opzet geparkeerd ("eindeloze afstemming per game").
- [afgeleid] Onze game maakt na de GLEW-probe nooit een GL-context op het hoofdvenster. Dat past bij een game die vanuit de ELF (freeglut: `glutFullScreen` staat als import/string in BudgieLoader.exe, lokaal gelezen) een modewissel of "fullscreen"-pad volgt dat onder Xwayland mislukt (geen xrandr). Dit is een hypothese, niet bevestigd.

## Vraag 2: kan de ELF native draaien, zonder Wine?

- [bron, lokaal] De map `Native/` in de TPUI-distributie bevat alleen `SDL3/` (libSDL3.so voor linux-x64 plus win-dll's). Geen ELF-loader of game-runtime.
- [bron, lokaal] ElfLdr2 bestaat uit `BudgieLoader.exe`, `BudgieLoader_x64.exe`, `TeknoParrot.dll`, msys-dll's en een `env/dev`-map: een Windows-PE-loader. Er is geen Linux-uitvoerbaar bestand.
- [bron, lokaal] Het profielveld `<LinuxOk>` staat in de GameProfiles alleen op true bij SR3, TroubleWitches, Tetris TGM3 en WackyRaces; niet bij Halo, JurassicPark, Terminator, AliensArmageddon. In onze gebruikersprofielen staat Halo op `LinuxOk=false`, en dat spel draait toch (onder Wine). [afgeleid] LinuxOk lijkt dus een compatibiliteitsvlag voor de UI, geen "native"-schakelaar.
- [bron] akitaonrails issue 7: de native Linux-net8-build zonder loaders start geen enkele game; "Cannot find loader: ./ElfLdr2/BudgieLoader.exe" bij Cruis'n Blast.
- Conclusie [afgeleid]: native draaien van deze ELF-games is geen route. Niet gevonden: een document dat dit tegenspreekt.

## Vraag 3: Gamescope

Lokaal vastgesteld [bron, strings uit /home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrotUi]:
- TPUI 2.0.0.130 heeft per game een instelling "Fullscreen game scaling" (profielveld `FullscreenScalingMode`; waarden o.a. Disabled en "Automatic fullscreen fit", experimenteel) en `GamescopeGameWindowCompatibility` (Default/RequireWindowed). Er is een knop "Test Gamescope".
- Omgevingsvariabelen: `TP_GAMESCOPE=1` (afdwingen), `TP_NO_GAMESCOPE=1` (uit), `TP_GAMESCOPE_PATH`, `TP_GAMESCOPE_BACKEND` (ook "auto"), `TP_GAMESCOPE_ALLOW_NESTED=1`.
- Meldingstekst: "External emulator profile - automatic wrapping skipped (not explicitly requested via TP_GAMESCOPE=1 or an explicit per-game Automatic fullscreen fit setting)". [afgeleid] Zonder expliciete keuze wordt dus niet gewrapt; waarschijnlijk geldt dit voor ons nu. Of ElfLdr2 als "external emulator" telt is niet vastgesteld.
- Tekst: de functie is experimenteel en "verified with controlled test clients only so far - not yet with a real TeknoParrot game's input/JVS pipeline or lightgun/pointer input". Ook een regel over "Exclusive fullscreen Create3DDevice failure under nested Gamescope".
- [bron, lokaal] `/usr/bin/gamescope` bestaat, versie 3.16.30. Let op: `XDG_SESSION_TYPE` in mijn shell was "tty", dus de Wayland-sessie is hier niet getest.
- Online niets gevonden dat TPUI-Gamescope documenteert (wiki/handleiding niet gevonden via zoekmachine).
- [afgeleid] Gamescope als genest compositor geeft de game een eigen scherm van vaste grootte; een modewissel (xrandr/ChangeDisplaySettings) heeft dan geen effect op de monitor en de game ziet een venster dat gamescope schaalt. Terminator werkt al in venster (~1360x768), dus schalen in gamescope is waarschijnlijk de kortste weg naar een schermvullend beeld. Jurassic Park in venster crasht (0x58000), dus daar helpt dit niet zonder die crash op te lossen.

## Gerangschikte vervolgstappen

1. Terminator Salvation: venstermodus laten schalen met Gamescope. Zet `FullscreenScalingMode` van het profiel op "Automatic fullscreen fit" (of start met `TP_GAMESCOPE=1`), houd Windowed=1 en eventueel Use Custom Resolution 1360x768 (of 1920x1080 als dat werkt), en gebruik "Test Gamescope". Handmatige variant: `gamescope -w 1360 -h 768 -W 1920 -H 1080 -f -- <wine-commando>`. Verwacht effect: schermvullend beeld voor Terminator en andere games die in venster werken. Moeite: laag. Risico: laag tot middel (experimenteel; lichtgun-/pointerinput kan verkeerd uitlijnen door schaling; een tweede compositor kan de Move-/evdev-injectie beïnvloeden, wat bij ons via /dev/input loopt en dus waarschijnlijk niet geraakt wordt [afgeleid]).
2. Wine virtual desktop als proef: `wine explorer /desktop=Game,1920x1080 BudgieLoader.exe ...` (of zonder Gamescope), met Windowed=0. Verwacht effect: de game krijgt een vast 1920x1080-"scherm" zonder echte modewissel, wat het GL-contextpad kan herstellen. Moeite: laag (via wine-runner-wrapper of aparte `.env`). Risico: laag; kan de Wine-kant van de JVS-brug niet verstoren, maar bij het maken van een tweede desktop kan het venster-id veranderen [afgeleid]. Niet gevonden: bewijs dat dit voor ElfLdr2 werkt.
3. Resolutie-experiment voor Jurassic Park en Terminator op volledig scherm: gebruik CustomResolution met 1360x768 (de standaardwaarde in het profiel) in plaats van 1920x1080 en kijk of de game dan wel een GL-context op het hoofdvenster maakt. Verwacht effect: onzeker; als de game een bepaalde modus verwacht, lost dit het op. Moeite: zeer laag. Risico: zeer laag. [afgeleid] De TV-resolutie wordt dan niet gewijzigd (Xwayland), dus combineer met stap 1 of 2 voor schaling.
4. WINEDEBUG-vergelijking tussen Terminator-venster (werkt) en Terminator-fullscreen (zwart): vergelijk waar de GL-aanroepen beginnen (`+wgl,+opengl,+win,+x11drv,+display`). Verwacht effect: verduidelijkt of de fout een mislukte modewissel (`ChangeDisplaySettingsExA`, `EnumDisplaySettingsA`) of een venster-/focusprobleem is. Moeite: middel. Risico: geen.
5. Wine-versie variëren (andere Kron4ek-build of Wine 10.x): WineHQ-bug 58488 beschrijft een zwart-beeld-regressie rond client-surfaces voor OpenGL in Wine 10.12 (https://list.winehq.org/hyperkitty/list/wine-bugs@list.winehq.org/thread/CRHSYHZ7DLMHLMMBZEUFEZS5JREUEPL5/). [afgeleid] Wine 11 (WoW64) kan een andere regressie hebben; Halo werkt echter in dezelfde Wine, dus zeker niet algemeen. Moeite: middel (download nodig, dus buiten deze opdracht). Risico: laag.
6. Een X11-sessie of `--backend sdl/wayland` in gamescope proberen (uit TPUI-tekst `--backend`): [afgeleid] alleen als 1 en 2 falen.

## Niet gevonden

- Elke vermelding van ElfLdr2-zwart-beeld op volledig scherm, op Windows of Linux.
- Documentatie van BudgieLoader-argumenten of -omgevingsvariabelen voor resolutie/venster (de binary is gepakt/versleuteld: alleen glut-namen en Win32-import-namen zichtbaar).
- Een TeknoParrot-wiki- of handleidingpagina over Linux/Steam Deck/ElfLdr2/Raw Thrills; de TeknoParrot-site en wiki zijn niet via de zoekmachine bereikbaar gebleken. De Sinden-wikipagina gaf 403.
- Bevestiging dat de TPUI-Gamescope-functie ElfLdr2-profielen ondersteunt of overslaat; alleen de binary-tekst "External emulator profile" is gelezen.
- Verklaring voor de access violation in Jurassic Park-venstermodus (0x58000, exit 255).
