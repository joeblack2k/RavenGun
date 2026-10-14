# Doel: 90% van de games native in RavenGun

Opdracht van de eigenaar (2026-10-14): minstens 90% van de titels speelt rechtstreeks vanuit onze app, zonder externe scripts of tools
(geen TeknoParrotUi, geen shellketen), met hooguit per-game data in `patches/<Game>/`; installeerbaar op een nieuwe SteamOS-pc.
Werkwijze: pass 1 = titels met een bekend recept (snel), pass 2 = titels die nieuw werk vragen. Een titel is **WORKING** na een
dubbelklik in de app en 5 minuten attract of spel op de tv, met bewijs in `~/.local/share/ravengun/bench-captures/<Game>/`.
Deze lijst is de enige werklijst; na elke titel bijgewerkt. Overgeslagen titels komen niet meer terug.

Stand: 16 van 81 titels WORKING (19%); doel 90% = 73 titels. Pass 1: 14, pass 2: 51, overgeslagen: 1.

## WORKING (16)

| Titel | Platform | Volgende stap |
|---|---|---|
| Drakons: Realm Keepers | Adrenaline Amusements PC-Based |  |
| Dream Raiders | SEGA RingWide |  |
| Elevator Action Invasion | UNIS PC Based |  |
| Gashaaaan!!!! | Konami PC Based |  |
| HALO Fireteam Raven | RAW Thrills Linux PC Based |  |
| House of the Dead: Scarlet Dawn | SEGA ALLS |  |
| Jurassic Park Arcade | RAW Thrills Linux PC Based |  |
| Let's Go Island 3D: Lost on the Island of Tropics | SEGA RingEdge |  |
| Let's Go Island: Lost on the Island of Tropics | SEGA RingWide |  |
| Let's Go Safari | SEGA RingWide |  |
| NERF Arcade | Raw Thrills PC Based |  |
| Rabbids Hollywood Arcade | ADRENALINE PC Based |  |
| Raccoon Rampage | UNIS PC Based |  |
| Rampage | ADRENALINE PC Based |  |
| Terminator Salvation | RAW Thrills Linux PC Based |  |
| Tomb Raider Arcade | ADRENALINE PC Based |  |

## pass 1 (14)

| Titel | Platform | Volgende stap |
|---|---|---|
| Aliens Armageddon | RAW Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Aliens: Extermination | Global VR PC Based | profiel + direct, Global VR |
| America's Army Arcade | Global VR PC Based | native (Global VR Linux, ElfLdr2) |
| Big Buck Hunter Pro | Raw Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Big Buck Hunter Pro Home | Raw Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Crossfire: Maximum Paintball | Linux PC Based | native (Linux, recept NERF/JP) |
| Friction | Unique PC Based | getest 2026-10-14: "Shader UI_VS is not supported" (HLSL-compilatie), ook met DXVK; echte d3dx9 nodig (zoals Medaru) |
| Gashaaaan Refill!!!! | Konami PC Based | spelbestanden ontbreken lokaal: map zoeken/koppelen, dan dubbelklik |
| Go Go Strike | PAOKAI Linux PC Based | spelbestanden ontbreken lokaal: map zoeken/koppelen, dan dubbelklik |
| Mighty Moto | UNIS PC Based (Unity, 32-bit) with a local SmartFoxServer 1.6.6 (Java) | profiel + direct (Unity 32-bit) |
| Police Trainer 2 | Phantom Systems PC Based | native (Linux, ElfLdr2) |
| Target: Terror Gold | Raw Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Walking Dead | RAW Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Wild West Shootout | Coastal PC-Based | profiel + direct |

## pass 2 (51)

| Titel | Platform | Volgende stap |
|---|---|---|
| Action Deka | Konami PC Based | getest 2026-10-14: lader exitcode 5 direct na laden; diagnose loopt |
| After Dark | UNIS PC Based | getest 2026-10-14: MFC 9.0 in de gedeelde prefix lost "Failed to run the process" op; nu een venster "Initialization Error" |
| Block King Ball Shooter | Type-X PC Based | getest 2026-10-14: lader exitcode 1; diagnose loopt |
| Bug Busters | EM-Teck PC Based | getest 2026-10-14 (2x, ook met view en lege video-map): munt+START tot MODE SELECT, daarna exitcode 5; oorzaak open |
| Coopers's 9 | KONAMI PC Based | getest 2026-10-14: draait (game.exe), geen zichtbaar venster; diagnose loopt |
| Elevator Action Death Parade | Taito Type X2 | Taito Type X2 / OpenParrot |
| Far Cry Paradise Lost | Global VR PC Based | getest 2026-10-14: draait (Farcry_R.exe), beeld zwart; diagnose loopt |
| Gaia Attack 4 | Taito Type X2 | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Galaga Assault | RAW Thrills Linux PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Ghost Squad Evolution | SEGA Lindbergh Red | Lindbergh-route klaar (profiel GSEVO): laag bouwen, dubbelklik, 5 min (docs/lindbergh.md) |
| Ghostbusters | ICE/RT Linux PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Gunslinger Stratos 3 | Taito Type X4 | Taito Type X4 |
| Haunted Museum | Taito Type X2 | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Haunted Museum II | Taito Type X2 | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Infinity Blade FX | ADRENALINE PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Let's Go Jungle Lost on the Island of Spice |  | geen profiel in de bibliotheek: koppelen (recept Let's Go Island) |
| Let's Go Jungle Special |  | geen profiel in de bibliotheek: koppelen (recept Let's Go Island) |
| Lethal Enforcers 3 | Konami PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Lost Land Adventure | Namco ES3X | Namco ES3X (zelfde familie als TC5) |
| Luigi's Mansion Arcade | SEGA Nu 1.1 | invoer in het spelproces schrijven (rg-pe-hook, RVA 0x17E50); munt/test zoeken |
| Mario Kart: Arcade GP DX | Namco ES3A | PRIORITEIT eigenaar. Gemeten: stack 16M, standaardroute: toegangsfout op 0x00835E60 in de spelcode na init (race bij geparkeerd startpunt opgelost); RemoteThread ook niet. Agent onderzoekt |
| Mars Sortie | UDC PC Based | getest 2026-10-14: Shooter.exe (Unity, BepInEx, Sentinel dog_windows) draait, beeld zwart: ook met gamescope en met DXVK-prefix; diagnose loopt |
| Medaru No Gunman | SEGA RingWide | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Men in Black | Windows PC | geparkeerd: twee versies, onderzoek ATTRACT-RUN.md |
| Metal Gear Arcade | Konami PC-Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Music GunGun 2 | Taito Type X2 | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Night Hunter - After Dark, Chapter 2 | UNIS PC Based | getest 2026-10-14: blijft op Initializing... (qumo2_en.exe draait); diagnose loopt |
| Omatsuri Yasan - Ganso Takoyaki | CAVE PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Omatsuri Yasan - Kingyo Sukui | CAVE PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Operation G.H.O.S.T. | SEGA RingWide | getest 2026-10-14: crash: Exception EXCEPTION_ACCESS_VIOLATION-dialoog (gs2.exe); diagnose loopt |
| Otomedius Excellent | Konami PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Pacman Chomp-Mania | RAW Thrills Linux PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Point Blank X | Namco ES4 | Namco ES4 |
| Pull The Trigger | COVIELSA PC-Based | getest 2026-10-14: PTW.exe draait, geen beeld (achter de app of zwart); diagnose loopt |
| Rainbow Bombergirl | KONAMI PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Robin Hood | ICE PC Based | getest 2026-10-14: game.exe draait, geen beeld (achter de app of zwart); diagnose loopt |
| SEGA Golden Gun | SEGA RingWide | getest 2026-10-14: lader exitcode 5 (toegang geweigerd) direct na laden; diagnose loopt |
| Primeval Hunt | SEGA Lindbergh Red | Lindbergh-route klaar (profiel Primevil): laag bouwen, dubbelklik, 5 min (docs/lindbergh.md) |
| Rambo | SEGA Lindbergh Red EX | Lindbergh-route klaar (ramboM.elf, libCg): laag bouwen, dubbelklik, 5 min (docs/lindbergh.md) |
| Silent Hill: The Arcade | Konami PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Silent Scope: Bone Eater | Konami PC-Based | Konami TeknoMacaw |
| Star Trek Voyager | Team Play PC Based | geen Lindbergh-game (Team Play-pc, trek.e4 onbekend voor lindbergh-loader): eigen native route nodig |
| The House of the Dead 4 Special | SEGA Lindbergh Yellow | EERSTE Lindbergh-titel: laag bouwen (scripts/build-lindbergh-loader.sh), dubbelklik, 5 min (docs/lindbergh.md); map "The House of the Dead 4" is Rev C = profiel HOTD4 |
| The House of the Dead EX | SEGA Lindbergh Red | Lindbergh-route klaar (libCg): laag bouwen, dubbelklik, 5 min (docs/lindbergh.md) |
| The House of the Dead III | SEGA Chihiro | SEGA Chihiro (Xbox-emulatie) |
| Time Crisis 5 | Namco ES3X | invoer komt niet aan (onderzoek loopt) |
| Too Spicy | SEGA Lindbergh Red | Lindbergh-route klaar (profiel 2Spicy, libCg): laag bouwen, dubbelklik, 5 min (docs/lindbergh.md) |
| Transformers: Human Alliance | SEGA RingEdge 2 | SEGA RingEdge 2 |
| Transformers: Shadows Rising | Windows PC | bevriest ~1 min na START: SHELL_MSG_CHALLENGE (onderzoek loopt) |
| Virtua Cop 3 | SEGA Chihiro | SEGA Chihiro (Xbox-emulatie) |
| Wartran Troopers | Konami PC Based | getest 2026-10-14: zwart beeld (wined3d en DXVK d3d9), game.exe draait; diagnose loopt |

## overgeslagen (1)

| Titel | Platform | Volgende stap |
|---|---|---|
| Pigs Might Fly | SEGA PC-Based | eigenaar: niet spelen (staand beeld) |
