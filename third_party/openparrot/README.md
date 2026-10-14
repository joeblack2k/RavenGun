# OpenParrot: eigen lader en kern uit de openbare bron

Doel: zo min mogelijk binaries van TeknoParrot. De open lader (`OpenParrotLoader.exe`, `OpenParrotLoader64.exe`) en de open
kern die de Type X2-games gebruiken (`OpenParrot.dll`, `OpenParrot64.dll`) worden hier uit de openbare bron gebouwd in plaats
van uit de TeknoParrotUi-download te komen. De Wine-fix voor Type X2 zit als broncodewijziging in de lader.

- Bron: https://github.com/teknogods/OpenParrot, gepind op commit `fc8ba632e1f67c5c340ff88f30efca743e8d38f5` (11 september 2026, "Add APM3 test support"). Licentie GPL-3.0 (bestand `LICENSE` in de bron), RavenGun is GPL-3.0-or-later.
- De bron staat niet in deze repo. `scripts/build-openparrot.sh` haalt precies die commit op (`git fetch` van de commit, controle van de hash) in `~/.cache/ravengun/src/openparrot`, pakt hem met `git archive` uit en past onze patches toe. Niets uit de bron wordt buiten de build-container uitgevoerd (`premake5.exe`, `*.bat`, `appveyor.yml` blijven ongebruikt).
- Bouwen: `scripts/build-openparrot.sh` (rootless podman, geen netwerk tijdens het bouwen). Uitvoer: `~/.local/share/ravengun/openparrot/<commit>/{OpenParrotWin32,OpenParrotx64}/` met een `BUILDINFO.txt` (commit, hashes van patches en uitvoer, toolchain) en de verwijzing `~/.local/share/ravengun/openparrot/current`. Twee keer bouwen geeft byte-voor-byte dezelfde bestanden (gecontroleerd met sha256).

## Toolchain (vastgepind)

`packaging/containers/openparrot/Containerfile`: basis-image `docker.io/library/archlinux@sha256:2fd1ae548076c67bcd41098ddd44e799058010e8d55f085d453dbb890e706860` plus pakketten uit het Arch Linux Archive van 2026/10/11:
mingw-w64-gcc 16.2.0-2 (i686 en x86_64), mingw-w64-headers/crt/winpthreads 14.0.0-1, clang en lld 23.1.1-1, patch 2.8-1. `scripts/build-openparrot.sh --versions` toont ze.

Waarom geen premake/MSVC: de bron is een Visual Studio-oplossing (premake5.lua). Met MinGW-w64 en clang is dat wel te bouwen:
GCC voor het meeste, clang voor de bestanden met MSVC-SEH (`__try`) en MSVC-inline-assembler (`__asm { }`) die GCC niet kent
(`compile-one.sh` probeert eerst g++ en valt dan terug op clang). Gebouwd met C++17, `-O2`; de lader met `-O0` (de bron zet
`#pragma optimize("", off)`), alles statisch tegen libstdc++ en met de UCRT-importbibliotheken (Wine levert `ucrtbase` en de
`api-ms-win-crt-*`-stubs; gecontroleerd met Wine 11 uit `proton-11.0-2`).

## Wat wij wijzigen (`patches/`, GPL-3.0 net als de bron)

1. `0001-loader-no-context-floating-point.patch`: **de Wine-fix.** `OpenParrotLoader/src/main.cpp`, regel 874 van de upstream-bron
   (`mycontext.ContextFlags`): `0x00010000 + 1 + 2 + 4 + 8 + 0x10` (0x1001F) wordt `0x00010000 + 1 + 2 + 4 + 0x10` (0x10017), dus zonder
   `CONTEXT_FLOATING_POINT`. Dat ene `mycontext` gaat naar elke `GetThreadContext` en `SetThreadContext` van de lader (`RunTo` en
   `LoadHookDLL`). Reden: zie `docs/decisions.md`, "Eigen OpenParrot-lader en -kern": Wine zet bij `SetThreadContext` met
   `CONTEXT_FLOATING_POINT` zonder `CONTEXT_EXTENDED_REGISTERS` MXCSR op 0, waarna Mesa in de D3D9-init met `c00002b5` vastloopt (Type X2).
   Het is dezelfde wijziging als de bytepatch `OpenParrotLoader zonder CONTEXT_FLOATING_POINT` (`patches/_defaults/OpenParrot.patches.ini`),
   nu in de bron. Let op: de 64-bit lader heeft dezelfde regel en krijgt de wijziging dus ook; de bytepatch liet hem met rust (`TargetName`).
2. `0002-mingw-w64-port.patch`: kleine aanpassingen zodat het met MinGW-w64 bouwt, zonder gedragswijziging: parameternamen die de klassenaam
   verbergen (`Hooking.Patterns.h`), `std::min` met expliciete typen, `injector` en MinHook accepteren functiewijzers en stringliteralen
   (MSVC liet die stilzwijgend naar `void*` gaan), `memory_pointer_tr` uit een functiewijzer, een statische `genericRetZero` in `FNFDrift.cpp` die
   botste met de globale, MS-inline-assembler met expliciete operandgrootte (`GRID.cpp`), en in `APM3.cpp` de `__try/__except` rond een debug-print (clang
   kent die voor i686 niet in een variadische functie) en de lambda als thread-startfunctie met `stdcall`.
3. `0003-unknown-game-generic-route.patch`: **een onbekende game wordt niet meer geweigerd.** `OpenParrot/src/Utility/GameDetect.cpp`, de
   `default:`-tak van de CRC-switch: daar stond een `MessageBoxA("Unsupported Executable, NEW CRC: ...")` gevolgd door `ExitProcess(0)`. Die twee zijn
   weg; de CRC gaat nog steeds als `Unsupported Executable` in `teknoparrot.ini`, zodat een verkeerde exe niet stil verdwijnt. Reden: onze eigen kern
   hoeft een game niet bij naam te kennen om hem te laten draaien. Van de 22 games die in de patches-repo nog op de gesloten kern staan, draaien er 15 al
   met alleen `wine <exe>` (gemeten, zie `patches/ZONDER-LADER.md` en `patches/EIGEN-KERN.md` in de patches-repo), en die horen niet op een foutmelding te
   stuiten. Wat er dan gebeurt: `currentGame` blijft `GameID::Global` en `X2Type` blijft `X2Type::None`, dus alleen de globale init-functies draaien en
   elke per-game tak slaat zichzelf over (`TypeX2Generic.cpp` begint met `if (GameDetect::X2Type == X2Type::None) return;`). Er wordt niets gehaakt en
   niets gepatcht. **Terugdraaien:** zet de `MessageBoxA` en `ExitProcess(0)` in die tak terug.
4. `0004-drive-letter-redirect.patch`: **generieke schijfletter-herleiding.** `OpenParrot/src/Functions/Ring_amLib/amJvs.cpp`: de al bestaande
   `CreateFileA/W`-haken herleiden nu ook een pad dat begint met `<L>:\` (of `<L>:/`) naar `.\TeknoParrot\<L>\<rest>` (cwd-relatief, zoals de gesloten
   kern en `docs/loskoppelen-per-game.md`), en er komen `CreateDirectoryA/W`-haken bij die hetzelfde doen. `C:` (prefix) en `Z:` (Wine-root) worden nooit
   herleid. Reden: Gashaaaan (profiel WartranTroopers) zoekt zijn ROM/NVRAM/RAW op `D:\`/`F:\`/`E:\` (uit `BALLS.INI`); zonder die schijven crasht zijn
   recursieve `mkdir -p` in `util.dll` op een NULL-pointer (0xC0000005, exitcode 5 na ~5 s, getraced). Met `d:`/`e:`/`f:` in de prefix leeft hij 20 s en
   schrijft hij NVRAM; deze patch doet dat in de kern in plaats van in de prefix. De crash zit in `CreateDirectoryA`, niet in `CreateFileA`, dus die haak
   moet mee. Generiek, geen per-game lijst. **Terugdraaien:** verwijder het patchbestand en herbouw.
5. `0005-luigismansion-detect.patch`: **Luigi's Mansion Arcade wordt herkend (alleen detectie).** Nieuwe `GameID::LuigisMansion` en de
   detectie-case voor de genormaliseerde CRC `0xd3dc9603` van `VACUUM.exe` in `GameDetect.cpp` (alleen in de 64-bit kern). Er hangt verder geen
   gedrag aan; het nut is dat de game niet meer in de `default:`-tak valt en de kern dus geen `[Error] Unsupported Executable` meer in de
   `teknoparrot.ini` in de werkmap schrijft. **Terugdraaien:** verwijder het patchbestand en herbouw.
   **Vervallen op 2026-10-12: de COM-blokkade.** Tot die datum heette de patch `0005-luigismansion-com-block.patch` en weigerde hij in
   `Hook_CreateFileA/W` (`amJvs.cpp`) voor deze game elke naam die met `COM` begint (`INVALID_HANDLE_VALUE` + `ERROR_FILE_NOT_FOUND`). De reden:
   (1) De waarneming waarop de blokkade rustte klopt: de gesloten kern heeft voor deze game een COM-tak. De string "BLOCKING LUIGI COM PORT" staat
   één keer in het ontsleutelde geheugenbeeld van de gesloten 64-bit kern (`docs/research/region-mechanisme.md`, hoofdstuk 2). Een tussentijdse
   conclusie "niet terug te vinden" was fout, want daar was in het gepakte DLL-bestand gezocht.
   (2) Voor deze game maakt de blokkade op onze kern geen meetbaar verschil: in een A/B op de echte game met COM1 aan een pty is het resultaat met
   en zonder identiek (exitcode 32, dezelfde levensduur, nul eigen databestanden) en raakt het spel de poort in beide gevallen nooit aan (nul blokken
   op de pty, nul COM-namen in alle 77-86 `CreateFileW`-regels). Een kernloze meting laat hetzelfde zien (`docs/research/luigismansion-kernloze-meting.md`).
   (3) De blokkade zet dus een deur dicht die de game hier niet gebruikt: hij beschermt niets en zou een latere route waarin we de game juist een
   poort willen geven stil saboteren. Kortom: de premisse is waar, de patch doet hier niets, en hij zet wel een deur dicht. Komt het spel op onze
   kern tot zijn I/O-init, dan is dit het eerste om opnieuw te meten.
   Build zonder de blokkade: `OpenParrot64.dll` sha256 `a9cd0e837f3e4a9934997d3ee6c3adf0f0376e754b0d890acba041907a203cfd`, `OpenParrot.dll`
   `dbf8bb2014861f59fe504f434fdbe2acf2fb07606552f2f3c9ce624f6d2ac0e6`; de laders zijn byte-gelijk gebleven. De build met de blokkade was
   `fe2ecf8c2c8844bd7abb3af616271a36364028453c2885fc92d61c9ffd155794` (64-bit).
6. `0006-init-trace.patch`: **opt-in trace van de init van de kern.** Met `RG_INIT_TRACE=<bestand>` schrijft de kern een regel aan
   het begin en eind van elke stap (DllMain, `Main_DoInit`, `RunMain`, elke init-functie). Zonder die variabele gebeurt er niets.
   Meting: `docs/research/luigismansion-initializeasi-trace.md`. **Terugdraaien:** verwijder het patchbestand en herbouw.
7. `0007-exit-trace.patch`: **opt-in exit-trace en overzicht van wat er in het proces is aangeraakt.** Nieuw bestand
   `OpenParrot/src/Utility/RgExitTrace.cpp`, twee regels in `dllmain.cpp` en `Utils.h`. Alleen actief met `RG_INIT_TRACE`; schrijft in
   hetzelfde bestand. Haakt `kernel32!ExitProcess`, `kernel32!TerminateProcess`, `ntdll!RtlExitUserProcess` en
   `ntdll!NtTerminateProcess` en logt per aanroep de volle code, het terugkeeradres als module+offset met de sectie, de bytes ervoor en
   erop, de argumentregisters, de stapel erboven en een stackwalk. Per haak een installatieregel en een zelftest, zodat een lege meting
   eerst bewijst dat hij gezocht heeft. Daarnaast een momentopname die alleen leest (direct na de init, bij de beëindiging): staan de
   haken er nog, hoe vol is elke sectie van de hoofdmodule, en een overzicht in een eigen bestand met de eerste 16 bytes van elke
   geëxporteerde functie van elke geladen module en de import-adres-tabel van de hoofdmodule. Opties: `RG_EXIT_PROBE=<rva,rva>`
   (32 bytes op die RVA's), `RG_EXIT_DUMP=1` (het beeld van de hoofdmodule), `RG_EXIT_PROTECT=1` (meekijken op
   `NtProtectVirtualMemory`, ongewijzigd doorgegeven). Het bronbestand hangt alleen van `<windows.h>` en MinHook af en is daarom ook
   als losse DLL te bouwen: doel `exittrace64` (niet in de standaarddoelen) geeft `OpenParrotx64/RgExitTrace64.dll`, die de lader via
   `TP_PRELOAD_DLL` laadt in een proces waar onze kern niet in zit; die maakt zijn momentopnamen bij de 1e, 5e, 20e en 60e draad van
   het spel. De overzichten leest `tools/exit-trace-overzicht.py` (`aangeraakt`: tegen het DLL-bestand op schijf; `verschil`: twee
   runs naast elkaar). Meting: `docs/research/luigismansion-exit-trace.md`. **Terugdraaien:** verwijder het patchbestand en herbouw.
8. `0008-parked-entry-hold-suspended.patch`: **de parked-entry-point-route zonder race.** `OpenParrotLoader/src/main.cpp`: met
   `TP_ENTRYPOINT_REMOTETHREAD_MS` zet de lader na de wachttijd de hoofdthread stil (`SuspendThread`, en hij controleert dat EIP/RIP
   op het startpunt staat), schrijft de originele 20 bytes terug en laadt **dan** pas de kern en de drop-ins via een remote thread.
   Met `TP_LOADER_MANAGED_INIT` vergelijkt hij daarna het startpunt met het origineel: heeft de kern het zelf gehaakt, dan geen eigen
   trampoline (en dus geen tweede `InitializeASI`). De extra `SuspendThread` voor de trampoline is weg; de `ResumeThread` aan het eind
   blijft in balans. Reden (gemeten op Mario Kart DX, `patches/MarioKartDX/NETWERK-INVOER-CAMERA.md` 5c): upstream bleef de
   hoofdthread op `EB FE` draaien terwijl de kern laadde. De gesloten kern 1.0.0.3761 negeert `TP_LOADER_MANAGED_INIT` en schrijft in
   DllMain byte voor byte `E9 <rel32>` op het startpunt; de draaiende thread voerde `E9 FE 06 00 00` uit en sprong midden in een
   instructie (#UD op 0x8C753D). De kern bewaarde bovendien `EB FE` als "originele" code. Nieuwe logregels: `Primary thread suspended
   at the entry point; original entry bytes restored before loading the core.` en `Core hooked the entry point itself (..);
   skipping the loader-managed initializer.` **Terugdraaien:** verwijder het patchbestand en herbouw.
9. Hulpbestanden (geen wijziging van bronbestanden): `mingw-compat.h` (standaardheaders en SAL-macro's die MSVC stilzwijgend meebrengt),
   `shim.sh` (hoofdletter- en backslash-includes: `<Shlwapi.h>`, `Utility\Hooking.Patterns.h`; en de meegeleverde Microsoft-DirectX-SDK-headers worden vervangen
   door de MinGW-w64-headers), `win64init.S` (GNU-vertaling van `win64init.asm`), `openparrot32.def` (zelfde exportnamen als de MSVC-build, ook `_PrepareSafeInit@4`),
   `compile-one.sh`, `build-in-container.sh`.

## Wat wel en niet gebouwd is

Gebouwd: `OpenParrotLoader.exe` (32-bit), `OpenParrotLoader64.exe`, `OpenParrot.dll` (32-bit), `OpenParrot64.dll`.

Niet gebouwd: `OpenParrotKonamiLoader.exe`, `iDmacDrv`(`iDmacDrv32/64.dll`) en `OpenBanapass` (`bngrw.dll`): niet gevraagd. Spellen die die
bibliotheken naast de kern verwachten (Nesica en ES3X, Namco/Banapass) horen dus voorlopig bij de TeknoParrot-build.
In de kern ontbreken twee bronbestanden: `Games/Other/GHA.cpp` (gebruikt `IGameExplorer` uit `<gameux.h>`, niet in MinGW-w64) en, alleen in de 64-bit
kern, `Games/ES3X/WMMT6.cpp` (gebruikt `DnsQueryEx`/`DNS_QUERY_REQUEST`, niet in de MinGW-w64-headers). Dat zijn losse spelmodules; de Type X2-games (`Games/TypeX2/`) zitten er wel in.

Dongle-, keychip- en integriteitscode: de OpenParrot-bron heeft in sommige spelmodules zelf hooks op dongles of HASP (`Games/Other/CrazySpeed.cpp`,
`Games/ES3X/WMMT5*.cpp`, `WMMT6*.cpp`, `StarWars.cpp`, `Games/ES3B/PokkenTournament.cpp`, `Games/Nesica/NesicaGeneric.cpp`, `Games/APM3/APM3.cpp` en in
`OpenParrotKonamiLoader`). Die staan er zoals upstream en zijn door ons niet gewijzigd of uitgebreid; de Type X2-modules (`Games/TypeX2/`) hebben zulke
onderdelen niet. RavenGun roept ze niet zelf aan.

## Verschillen met de TeknoParrot-builds (versie 2.0.0.130, `OpenParrotWin32/` en `OpenParrotx64/`)

Statisch vergeleken, zie ook `docs/decisions.md`:

- Dezelfde bron als de meegeleverde lader: de omgevingsvariabelen die de lader leest zijn gelijk (`TP_REMOTETHREAD`, `TP_POSTSTART_REMOTETHREAD_MS`,
  `TP_ENTRYPOINT_REMOTETHREAD_MS`, `TP_GAME_WORKING_DIRECTORY`, `TP_PRELOAD_DLL`, `TP_LOADER_MANAGED_INIT`, en alleen in de 64-bit lader `TP_DIRECTHOOK`, `TP_APM_TEST_INIT`); bijna alle
  meldingen uit de bron staan ook in de meegeleverde lader (de rest is `#if` van de andere architectuur). De opdrachtregel is gelijk (`<kern zonder .dll> <exe> [parameters]`, daarnaast `--apm-test` in de 64-bit lader); zonder argumenten geeft elke build dezelfde gebruiksmelding.
- Exports kern: 32-bit `InitLinux`, `InitializeASI`, `_PrepareSafeInit@4`; 64-bit `InitializeASI`, `PrepareSafeInit`: gelijk. Onze 32-bit kern exporteert er `PrepareSafeInit@4` bij (de `dllexport` van de bron), onschadelijk. Laadadres gelijk (0x10000000 en 0x180000000); Wine plaatst een DLL desnoods elders, dat doet de meegeleverde build ook.
- Imports: onze builds gebruiken de UCRT (`api-ms-win-crt-*`) en `libstdc++` statisch; de TeknoParrot-builds hebben de MSVC-runtime statisch erin (`/MT`). Onze 32-bit kern importeert daarnaast `ntdll`; `d3d9` en `d3dx9_43` importeert hij net als de meegeleverde.
- Grootte: lader 1,3 MB (32-bit) en 1,2 MB (64-bit) tegen 173 en 334 kB, kern 2,0 en 1,9 MB tegen 970 kB en 1,2 MB (statische C++-bibliotheek, `-O2`).
- Versie: de lader meldt in beide builds `0.0.0.0`. De kern heeft in de bron `1.0.0.0` (`OpenParrot.rc`); de meegeleverde build is `1.0.0.787` (de CI van upstream telt het buildnummer erbij). De bron-commit is dus nieuwer dan of gelijk aan wat TeknoParrot 2.0.0.130 meelevert; welke CI-build exact bij 787 hoort is niet te zien.
- De Wine-fix: de meegeleverde lader heeft de store `mov [mycontext], 0x1001F` (`C7 05 .. .. .. .. 1F 00 01 00`), 32-bit op bestandsoffset 0x7f13 en 64-bit op 0xf4c2; in onze builds staat op die plek `0x10017` (32-bit 0x2f25, 64-bit 0x6e70).

Gemeten onder Wine (proton-11.0-2, `tools/openparrot-mxcsr-check.sh`): een klein 32-bit programma dat zijn MXCSR leest nadat de lader de context van de hoofdthread
heeft gezet, krijgt met de lader van TeknoParrot `0x00000000` (alle SSE-uitzonderingen ongemaskeerd: het Type X2-probleem), met de eigen lader `0x00001f80` (standaard).
Bij de 64-bit lader is het `0x1f80` in beide builds (0x1001F bevat daar `CONTEXT_AMD64` niet), de wijziging verandert daar dus niets.
Rooktests onder Wine: alle vier de binaries starten (zonder argumenten dezelfde gebruiksmelding als de TeknoParrot-builds); de 32-bit lader laadt met de eigen kern een
onschuldig programma (`hostname.exe`) tot "Have fun :)". Géén game gestart.

## Gebruik

`[Loader] Build = ravengun` in `patches/_defaults/<Type>.ini` of `patches/<Game>/game.ini` (of de gebruikerslaag) laat `scripts/launch-direct.py` de eigen lader (en bij `EmulatorType = OpenParrot` de eigen kern) kiezen; standaard blijft het de lader uit de TeknoParrot-runtime.
Zie `docs/per-game-patches.md` (sectie `[Loader]`).

## Bijwerken

Nieuwe commit: `OPENPARROT_COMMIT` in `scripts/build-openparrot.sh` aanpassen, patches opnieuw genereren als ze niet meer passen (`patch -p1` meldt het), THIRD_PARTY.md en dit bestand bijwerken, bouwen, daarna de smoke-tests (lader zonder argumenten, lader plus kern op `hostname.exe` onder Wine) en een Type X2-game op de tv.
