# Doel: 90% van de games native in RavenGun

Opdracht van de eigenaar (2026-10-14): minstens 90% van de titels speelt rechtstreeks vanuit onze app, zonder externe scripts of tools
(geen TeknoParrotUi, geen shellketen), met hooguit per-game data in `patches/<Game>/`; installeerbaar op een nieuwe SteamOS-pc.
Werkwijze: pass 1 = titels met een bekend recept (snel), pass 2 = titels die nieuw werk vragen. Een titel is **WORKING** na een
dubbelklik in de app en 5 minuten attract of spel op de tv, met bewijs in `~/.local/share/ravengun/bench-captures/<Game>/`.
Deze lijst is de enige werklijst; na elke titel bijgewerkt. Overgeslagen titels komen niet meer terug.

Stand: 41 van 68 titels WORKING (60%); doel 90% = 62 titels; sessiedoel (eigenaar, 2026-10-15): 50 speelbaar. Pass 1: 8, pass 2: 19, overgeslagen: 15.

## Doel eigenaar 2026-10-16: 95% start (zonder Raw Thrills)

"ga door totdat 95% van de games starten excluded Raw Thrills titels" (downloaden mag altijd). Telt mee: elke titel buiten
"overgeslagen" en buiten Raw Thrills (Halo, Jurassic Park, NERF, Target: Terror Gold, Terminator, Aliens Armageddon, Walking Dead,
Ghostbusters): 60 titels, 95% = 57. **Start** = dubbelklik in de app, en het spel komt in eigen beeld (logo, attract, titel of menu) op
de tv; zwart, een foutmelding of een crash telt niet.

Stand 2026-10-17 03:53: **52 van 54** starten (96%). Police Trainer 2 en Crossfire tellen als Raw Thrills (TeknoParrot-profiel
RawThrillsGUN, Raw Thrills-I/O via /dev/mjg en poort 0x37c); Block King Ball Shooter (Japanse tekst), Mighty Moto (4:3), Star Trek
Voyager (4:3) en Gunslinger Stratos 3 (Japanse tekst) zijn overgeslagen, dus 54 titels en 95% = 52 (51,3). Erbij sinds de vorige stand:
America's Army (native route aan), Lost Land Adventure (mfc100u), Transformers: Human Alliance, Robin Hood, Cooper's 9, Far Cry Paradise
Lost en Friction (alle vijf: Windowed = 1 in gamescope; met 0 bleven ze zwart), Infinity Blade FX (matrix-exports op de ordinalen van
het origineel, plus Windowed = 1), Silent Scope: Bone Eater (TeknoMacaw met DXVK, indeling KonamiAcio, ViewTtcAsTtf voor de
DynaFont-collecties), Men in Black (MIB.exe zonder kast-shell op onze kern, BepInEx-plugin tegen het zelf-afsluiten, GStreamer voor de
films), Aliens: Extermination (PeFillPages: Wine wiste de HASP-emulatie achter .text). Nog niet: The House of the Dead III en Virtua
Cop 3 (Chihiro; Cxbx-Reloaded onder Proton 11: de Xbox-hoofdthread wacht, twee andere threads vallen op MOVAPS met een stapel die niet op
16 bytes ligt, in code in het gespiegelde fysieke geheugen (0xF7xxxxxx, in beide spellen gelijk); onder GE-Proton10-34 start de
emulatie niet).

## Taken per familie

Opdracht van de eigenaar (2026-10-16): een taak per familie, Raw Thrills geparkeerd. Een fix werkt vaak voor de hele familie, dus
de taak is pas af als elke titel erin WORKING is of een gemeten reden heeft om te blijven staan. Per titel staat de stand in de
tabellen hieronder; deze lijst zegt alleen wat de taak is en waar hij staat. Werkvolgorde: T1 tot en met T9.

| Taak | Familie | Titels | Stand |
|---|---|---|---|
| T1 | Taito Type X2 | Gaia Attack 4, Elevator Action Death Parade, Haunted Museum, Haunted Museum II | 3 van 4 WORKING (JVS-herverbinden, 0x22-kanalen, borden TaitoGun/TaitoEADP met Taito-commando's, zaaigoed, ViewRealDirs); EADP geparkeerd: PhysX 2.8.1-kern ontbreekt (NxCreatePhysicsSDK = NULL; oude PhysX-systeemsoftware van NVIDIA nodig) |
| T2 | Windows D3D9 zonder Present | Far Cry Paradise Lost, Robin Hood, Friction | bezig: Far Cry gemeten (berichtenlus loopt, spel wacht op I/O of dongle, geen D3D9-hang); Robin Hood en Friction volgen |
| T3 | SEGA Lindbergh | The House of the Dead 4, The House of the Dead 4 Special | open |
| T4 | Namco ES3X | Time Crisis 5, Lost Land Adventure | open |
| T5 | Werkende titels: open punten | Mario Kart DX (camera), Ghost Squad Evolution (wisselvallig), Point Blank X (attract-film), runtime-keuze zonder daemonherstart | open |
| T6 | Linux native, nog niet geprobeerd | America's Army Arcade, Police Trainer 2, Crossfire: Maximum Paintball | open |
| T7 | Windows met bescherming of crash | Aliens: Extermination, Block King Ball Shooter, Cooper's 9 | Cooper's 9 start (Windowed = 1); Block King overgeslagen (Japanse tekst); Aliens: Extermination start (PeFillPages) |
| T8 | Overige platforms | Luigi's Mansion Arcade, Mighty Moto, Silent Scope: Bone Eater, Infinity Blade FX, Men in Black, Gunslinger Stratos 3, Transformers: Human Alliance, Star Trek Voyager | Transformers en Infinity Blade FX starten; Gunslinger Stratos 3 overgeslagen (Japanse tekst); Mighty Moto overgeslagen (4:3); Silent Scope: Bone Eater start; Star Trek Voyager overgeslagen (4:3); Men in Black start; rest open |
| T9 | SEGA Chihiro | The House of the Dead III, Virtua Cop 3 | alleen meten of een passthrough-lader (Cxbx-achtig, x86 native) haalbaar is |
| T10 | Eén RavenGun-map | (structuur) | open: alles wat RavenGun op schijf zet zo veel mogelijk in één map, zoals de User-map van Dolphin |
| geparkeerd | Raw Thrills / ICE | Walking Dead, Ghostbusters, Aliens Armageddon | eigenaar 2026-10-16: parkeren |

## WORKING (41)

| Titel | Platform | Volgende stap |
|---|---|---|
| Bug Busters | EM-Teck PC Based | Gst32 (films); schermvullend via gamescope + Windowed = 1 (2026-10-16) |
| Drakons: Realm Keepers | Adrenaline Amusements PC-Based |  |
| Dream Raiders | SEGA RingWide | View = off (vanuit de view SOUND FUNCTION NOT FOUND) en gamescope (2026-10-16) |
| Elevator Action Invasion | UNIS PC Based |  |
| Gaia Attack 4 | Taito Type X2 | JVS-pipe met herverbinden, bord TaitoGun (START op P1 omhoog/omlaag), gamescope + Windowed = 1, geweerkalibratie als zaaigoed (2026-10-16) |
| Haunted Museum | Taito Type X2 | recept Gaia Attack 4; bord TaitoGun beantwoordt de Taito-commando's (0x05 aan node 0); kalibratie als zaaigoed (2026-10-16) |
| Haunted Museum II | Taito Type X2 | recept Gaia Attack 4 + ViewRealDirs = data (Proton: gelinkte map = reparse point, SOUND ERROR); kalibratie P1+P2, datum en 1 munt per credit als zaaigoed (2026-10-16) |
| Gashaaaan!!!! | Konami PC Based | schermvullend via gamescope; BACKUP DATA ERROR opgelost door de schrijflaag (NVRAM bleef niet bewaard, 2026-10-16) |
| Ghost Squad Evolution | SEGA Lindbergh Red | Lindbergh-route; richtvenster nog niet gekalibreerd |
| HALO Fireteam Raven | RAW Thrills Linux PC Based | native sinds 2026-10-15 (eigen lader, IRTrack-geweren via tools/rt-irtrack, geen TeknoParrot-lader); geluid via de FMOD-wrapper (native FMOD 1.10 als drop-in, STEREO of LPCM 5.1) |
| House of the Dead: Scarlet Dawn | SEGA ALLS |  |
| Jurassic Park Arcade | RAW Thrills Linux PC Based |  |
| Let's Go Island 3D: Lost on the Island of Tropics | SEGA RingEdge |  |
| Let's Go Island: Lost on the Island of Tropics | SEGA RingWide |  |
| Let's Go Jungle Lost on the Island of Spice | SEGA Lindbergh Red | Lindbergh-route, stackalign, doorgiftefix 0003 |
| Let's Go Jungle Special | SEGA Lindbergh Yellow | ride-versie: START voor de ride-reset; patches 0003 en 0004 |
| Let's Go Safari | SEGA RingWide | Windowed = 1 in gamescope (met 0 wit beeld sinds de lokale view, 2026-10-16) |
| Lethal Enforcers 3 | Konami PC Based | Gst32 (films) |
| Mario Kart: Arcade GP DX | Namco ES3A | open: camera (het spel vraagt hem niet), Banapass-kaarten |
| Mars Sortie | UDC PC Based | DXVK; ViewCopies (GameRecords lokaal); beloningsscherm: trekker 5 s vasthouden |
| Music GunGun 2 | Taito Type X2 | HD-build van de dump via ViewLinks; JVS-bord van de daemon |
| NERF Arcade | Raw Thrills PC Based |  |
| Night Hunter - After Dark, Chapter 2 | UNIS PC Based | zonder DXVK (AVPro-video), BepInEx NHAD2, gst-extra, gamescope |
| Operation G.H.O.S.T. | SEGA RingWide | Gst32, native XACT 3.3, .wmv via [Launch] Registry; beeld 4:3 (spel kiest 1024x768) |
| Point Blank X | Namco ES4 | tp-shm PointBlankX; eenmalig testmenu: taal en-US |
| Primeval Hunt | SEGA Lindbergh Red | Lindbergh-route; aanraakscherm bewust afwezig |
| Pull The Trigger | COVIELSA PC-Based |  |
| Rabbids Hollywood Arcade | ADRENALINE PC Based |  |
| Raccoon Rampage | UNIS PC Based |  |
| Rambo | SEGA Lindbergh Red EX | Lindbergh-route |
| Rampage | ADRENALINE PC Based |  |
| SEGA Golden Gun | SEGA RingWide | recept Let's Go Island; gamescope schermvullend; regio EXPORT (Engels) |
| Target: Terror Gold | Raw Thrills Linux PC Based | native (eigen bordemulator tt_emu.so); virtuele gun met USB-pad; richten bij 4:3 zonder pillarbox-correctie |
| Terminator Salvation | RAW Thrills Linux PC Based |  |
| The House of the Dead EX | SEGA Lindbergh Red | Lindbergh-route met stackalign |
| Tomb Raider Arcade | ADRENALINE PC Based |  |
| Too Spicy | SEGA Lindbergh Red | Lindbergh-route met stackalign |
| Wild West Shootout | Coastal PC-Based | DXVK, winhttp=n,b |
| After Dark | UNIS PC Based | DXVK + native d3dx9_41, Gst32 + AsyncReader, rg-compat (DirectInput); open: geluid hapert (eigenaar, wined3d-run) |
| Transformers: Shadows Rising | Windows PC | hid-board (SaeGunBoard + SaeIoBoard); bevriezing van 10-14 niet teruggezien; hendel niet nagedaan |
| Silent Hill: The Arcade | Konami PC Based | widescreen-patch (16:9) onderzoeken; e-AMUSEMENT PASS (kaart + netwerkdienst) emuleren; START werkt pas na een munt |

## pass 1 (8)

| Titel | Platform | Volgende stap |
|---|---|---|
| Aliens Armageddon | RAW Thrills Linux PC Based | 2026-10-15 NATIVE route werkt (dongle-hook, RIO-brug fork-bestendig, rg-glslfix, PTRACER): attract, kalibratie, spel; GEBLOKKEERD: 16 .g6-modellen ontbreken in de NAS-map (brute*, buddy*, bullet, burn_*, cams, camtest2, capella, c_interior*) |
| Aliens: Extermination | Global VR PC Based | 2026-10-17 start: PeFillPages (HASP-emulatie achter .text, door Wine gewist), gamescope 1920x1080; opstartscherm meldt nog "no GVRIO card" en "no Aliens guns"; invoer niet gemeten |
| America's Army Arcade | Global VR PC Based | native (Global VR Linux, ElfLdr2) |
| Crossfire: Maximum Paintball | Linux PC Based | native (Linux, recept NERF/JP) |
| Friction | Unique PC Based | 2026-10-15: onder gamescope draait UpdateWindow eindeloos (zwart); zonder gamescope laadschermen met 60 fps, daarna zwart met 7 fps |
| Police Trainer 2 | Phantom Systems PC Based | native (Linux, ElfLdr2) |
| Walking Dead | RAW Thrills Linux PC Based | 2026-10-15 native: slottabel 806/806, RG_ELF_MAIN, libcaca-stub, dongle-hook; "Welcome to Walking Dead"; GEBLOKKEERD: dongle-antwoordtafel (hasp/, hhl_mem.dmp) ontbreekt in de dump |

## pass 2 (19)

| Titel | Platform | Volgende stap |
|---|---|---|
| Coopers's 9 | KONAMI PC Based | getest 2026-10-14: draait (game.exe), geen zichtbaar venster; diagnose loopt |
| Elevator Action Death Parade | Taito Type X2 | 2026-10-16: JVS werkt (bord TaitoEADP), logo, dan crash: NxCreatePhysicsSDK(2.8.0) geeft NULL, de PhysX 2.8.1-kern (oude PhysX-systeemsoftware van NVIDIA) ontbreekt; 2.8.4 van Infinity Blade FX werkt niet; ook met DXVK. Eigenaar: PhysX 2.8.1 aanleveren of downloaden? |
| Far Cry Paradise Lost | Global VR PC Based | 2026-10-16 (meetdraad): hoofddraad in zijn berichtenlus (PeekMessage), werkdraden wachten; geen D3D9-hang maar spellogica die op dongle of USB-IO wacht (gesloten kern TeknoParrot.dll) |
| Ghostbusters | ICE/RT Linux PC Based | 2026-10-15 native: dongle-hook, RIO-brug; "Welcome to Ghostbusters"; GEBLOKKEERD: dongle-antwoordtafel (hasp/, hhl_mem.dmp) ontbreekt in de dump |
| Infinity Blade FX | ADRENALINE PC Based | 2026-10-16 start: "No Dongle!" weg met matrix-exports op de ordinalen van het origineel, wit beeld weg met Windowed = 1 |
| Lost Land Adventure | Namco ES3X | Namco ES3X (zelfde familie als TC5) |
| Luigi's Mansion Arcade | SEGA Nu 1.1 | invoer in het spelproces schrijven (rg-pe-hook, RVA 0x17E50); munt/test zoeken |
| Men in Black | Windows PC | 2026-10-17 start: MIB.exe rechtstreeks op onze kern (DXVK, gamescope), BepInEx-plugin RavenGun.MenInBlack onderdrukt Application.Quit (geen kast-shell), GStreamer voor de MP4-attract; invoer nog niet gemeten |
| Robin Hood | ICE PC Based | 2026-10-15 (meetdraad): D3D9 1280x720, hoofddraad in zijn berichtenlus, geen Present; 57% CPU in /proc-lezingen (GetThreadTimes) |
| Silent Scope: Bone Eater | Konami PC-Based | 2026-10-17 start: TeknoMacaw64 + DXVK, regio Asia (Engels), attract met ranglijst; ViewTtcAsTtf (Wine's GDI+ neemt geen .ttc aan); invoer KonamiAcio nog niet gemeten |
| The House of the Dead 4 | SEGA Lindbergh Red | draait (stackalign): attract, munt, START, intro; tijdens het spel wit beeld (shaders uitgesloten), dan GAME OVER |
| The House of the Dead 4 Special | SEGA Lindbergh Yellow | draait (stackalign, /var/tmp): ride-reset en 5 min attract; munt+START starten geen spel (ride-signalen) |
| The House of the Dead III | SEGA Chihiro | SEGA Chihiro (Xbox-emulatie) |
| Time Crisis 5 | Namco ES3X | invoer komt niet aan (onderzoek loopt) |
| Transformers: Human Alliance | SEGA RingEdge 2 | SEGA RingEdge 2 |
| Virtua Cop 3 | SEGA Chihiro | SEGA Chihiro (Xbox-emulatie) |

## overgeslagen (19)

| Titel | Platform | Volgende stap |
|---|---|---|
| Pigs Might Fly | SEGA PC-Based | eigenaar: niet spelen (staand beeld) |
| Gashaaaan Refill!!!! | Konami PC Based | eigenaar 2026-10-15: spelbestanden niet op de NAS; weghalen en overslaan (verborgen in de app) |
| Go Go Strike | PAOKAI Linux PC Based | eigenaar 2026-10-15: spelbestanden niet op de NAS; weghalen en overslaan (verborgen in de app) |
| Action Deka | Konami PC Based | skip-regel eigenaar 2026-10-15: Japanse tekst en 4:3 (gemeten); map naar emulation/skipped |
| Metal Gear Arcade | Konami PC-Based | skip-regel eigenaar 2026-10-15: Japanse tekst (README: Shift-JIS, regio J); map naar emulation/skipped |
| Medaru No Gunman | SEGA RingWide | skip-regel eigenaar 2026-10-15: 4:3 (venster 1024x768 gemeten), alleen Japan; map naar emulation/skipped |
| Omatsuri Yasan - Ganso Takoyaki | CAVE PC Based | skip-regel eigenaar 2026-10-15: 4:3 (venster 1024x768 gemeten), alleen Japan; map naar emulation/skipped |
| Omatsuri Yasan - Kingyo Sukui | CAVE PC Based | skip-regel eigenaar 2026-10-15: 4:3 (venster 1024x768 gemeten), alleen Japan; map naar emulation/skipped |
| Wartran Troopers | Konami PC Based | skip-regel eigenaar 2026-10-15: 4:3 (twee schermen van 640x480 gemeten in de DXVK-log, TP-profiel met bezel-optie); map naar emulation/skipped (eerst geweigerd door de NAS, later gelukt) |
| Otomedius Excellent | Konami PC Based | skip-regel eigenaar 2026-10-15: Japanse tekst en 4:3 (gemeten: regiocode GGG:J, Japanse foutmelding, 4:3-beeld); map naar emulation/skipped |
| Rainbow Bombergirl | KONAMI PC Based | skip-regel eigenaar 2026-10-15: Japanse tekst (gemeten: ボンバーガール レインボー, PASELI; staand beeld); map naar emulation/skipped |
| Pacman Chomp-Mania | RAW Thrills Linux PC Based | eigenaar 2026-10-15: overgeslagen en weg (geen spelbestanden; profiel verwijderd) |
| Big Buck Hunter Pro | Raw Thrills Linux PC Based | eigenaar 2026-10-15: niet zijn genre; map naar emulation/skipped |
| Big Buck Hunter Pro Home | Raw Thrills Linux PC Based | eigenaar 2026-10-15: niet zijn genre; map naar emulation/skipped |
| Galaga Assault | RAW Thrills Linux PC Based | eigenaar 2026-10-15: overgeslagen en weg (geen spelbestanden; profiel verwijderd) |
| Block King Ball Shooter | Type-X PC Based | skip-regel eigenaar 2026-10-16: Japanse tekst (gemeten); profiel weg, map naar emulation/skipped |
| Mighty Moto | UNIS PC Based | skip-regel eigenaar 2026-10-17: 4:3 (gemeten in de eigen output_log.txt van de kast: desktop 1024x768; geen resolutie-optie); daarbij HASP SRM-envelope met volledig versleutelde code (.text entropie 8,0, "Unable to access HASP SRM Run-time Environment (H0033)"); profiel weg, map naar emulation/skipped |
| Star Trek Voyager | Team Play PC Based | skip-regel eigenaar 2026-10-17: 4:3 (gemeten in de code van trek: venster glutInitWindowSize(600, 450), Glide-doel 640x480, volledig scherm zonder eigen modus; geen breedbeeld- of aspect-optie); map naar emulation/skipped |
| Gunslinger Stratos 3 | Taito Type X4 | skip-regel eigenaar 2026-10-17: Japanse tekst (gemeten in de eigen attract-film gs3_pv1.wmv en de standaard-tutorial; alleen 6 van de 21 tutorialfilms hebben een _en-versie); geen lightgun-spel. Daarvoor gemeten: herkenning vond eerst Cooper's 9 (Size ontbrak), native xaudio2_7, FastIO-pipe, daarna 60 fps zwart zonder assets; profiel weg, map naar emulation/skipped |
