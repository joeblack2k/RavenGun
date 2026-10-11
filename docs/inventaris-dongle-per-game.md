# Inventaris: dongle en patches per game

Automatisch gemaakt uit `patches/<Game>/game.ini` en `patches/<Game>/dongle.json` door
`scripts/gen-dongle-inventaris.py`. Niet met de hand bijwerken: pas de map van de game aan en draai het script opnieuw.

De kolom **Dongle** komt uit `patches/<Game>/dongle.json` (veld `kind`); het bestaan van dat bestand betekent "deze
game vraagt een dongle" (`docs/dropins.md`). Ontbreekt het, dan is de dongle `-`. De kolom **Bron** zegt waar de dongle
vandaan komt: de drop-in in de map van de game. De kolom **Status** komt uit `[Status] Status` in `game.ini`.

`[Dongle] Source` in `game.ini` kiest tussen de fysieke dongle van de eigenaar (`usb-passthrough`, de standaard) en de
drop-in (`drop-in`); de meegeleverde laag laat die keuze aan de eigenaar (zie `docs/per-game-patches.md`, "Dongle per game").
Staat in `dongle.json` `"enforced": false`, dan gaat de game zonder die dongle verder (hij valt terug op zijn eigen
standaardwaarde); dat staat er dan bij.

| Game | Type | Emulatieprofiel | Dongle | Bron | Status |
|---|---|---|---|---|---|
| **3DCosplayMahjong** | TeknoParrot | TaitoTypeXGeneric | - | - |  |
| **GaiaAttack4** | OpenParrot | GaiaAttack4 | - | - | parked |
| **GalagaAssault** | ElfLdr2 | RawThrillsGUN | - | - | parked |
| **Gashaaaan** | TeknoParrot | WartranTroopers | - | - | playable |
| **Gashaaaan2** | TeknoParrot | WartranTroopers | - | - | playable |
| **GhostBusters** | ElfLdr2 | RawThrillsGUN | - | - | parked |
| **GoGoStrike** | ElfLdr2 | RawThrillsGUN | - | - | playable |
| **Halo** | ElfLdr2 | WartranTroopers | hasp | drop-in (dongle.json + dropin/) | playable |
| **HauntedMuseum** | OpenParrot | HauntedMuseum | - | - | parked |
| **HauntedMuseumII** | OpenParrot | HauntedMuseum2 | - | - | parked |
| **HOTDSD** | TeknoParrot | ALLSHOTDSD | - | - | playable |
| **InfinityBladeFX** | TeknoParrot | InfinityBlade | - | - | parked |
| **JurassicPark** | ElfLdr2 | RawThrillsGUN | - | - | parked |
| **LethalEnforcers3** | TeknoParrot | WartranTroopers | - | - | parked |
| **LGI** | TeknoParrot | SegaJvsLetsGoIsland | region | drop-in (dongle.json) | playable |
| **LGI3D** | TeknoParrot | SegaJvsLetsGoIsland | region | drop-in (dongle.json) | playable |
| **LGS** | TeknoParrot | LGS | region | drop-in (dongle.json) | playable |
| **LuigisMansion** | TeknoParrot | LuigisMansion | - | - | playable |
| **MedaruNoGunman** | TeknoParrot | FarCry | - | - | parked |
| **MusicGunGun2** | OpenParrot | MusicGunGun2 | - | - | parked |
| **NERF** | TeknoParrot | WartranTroopers | - | - | playable |
| **OmatsuriYasanGansoTakoyaki** | TeknoParrot | WartranTroopers | - | - | parked |
| **OmatsuriYasanKingyoSukui** | TeknoParrot | WartranTroopers | - | - | parked |
| **Otomedius** | TeknoParrot | WartranTroopers | - | - | parked |
| **PacmanChomp** | ElfLdr2 | RawThrillsGUN | - | - | parked |
| **PigsMightFly** | TeknoParrot | WartranTroopers | - | - | playable |
| **RabbidsHollywood** | TeknoParrot | WartranTroopers | matrix | drop-in (dongle.json + dropin/) | parked |
| **RaccoonRampage** | TeknoParrot | WartranTroopers | - | - | playable |
| **RainbowBombergirl** | TeknoParrot | WartranTroopers | - | - | parked |
| **Rampage** | TeknoParrot | WartranTroopers | - | - | playable |
| **TankTankTank** | ElfLdr2 | DeadHeat | - | - |  |
| **TC5** | TeknoParrot | TimeCrisis5 | hasp | drop-in (dongle.json + dropin/) | playable |
| **Terminator** | ElfLdr2 | RawThrillsGUN | - | - | playable |
| **TombRaider** | TeknoParrot | WartranTroopers | - | - | playable |
| **TransformersShadowsRising** | TeknoParrot | TransformersShadowsRising | - | - | playable |

## Per game: de dongle in de map van de game

* **3DCosplayMahjong** — -
* **GaiaAttack4** — -
* **GalagaAssault** — -
* **Gashaaaan** — -
* **Gashaaaan2** — -
* **GhostBusters** — -
* **GoGoStrike** — -
* **Halo** — hasp (dongle/ met 2 geheugenbestanden, dropin/)
* **HauntedMuseum** — -
* **HauntedMuseumII** — -
* **HOTDSD** — -
* **InfinityBladeFX** — -
* **JurassicPark** — -
* **LethalEnforcers3** — -
* **LGI** — region (niet afgedwongen, enforced = false)
* **LGI3D** — region (niet afgedwongen, enforced = false)
* **LGS** — region (niet afgedwongen, enforced = false)
* **LuigisMansion** — -
* **MedaruNoGunman** — -
* **MusicGunGun2** — -
* **NERF** — -
* **OmatsuriYasanGansoTakoyaki** — -
* **OmatsuriYasanKingyoSukui** — -
* **Otomedius** — -
* **PacmanChomp** — -
* **PigsMightFly** — -
* **RabbidsHollywood** — matrix (dropin/)
* **RaccoonRampage** — -
* **RainbowBombergirl** — -
* **Rampage** — -
* **TankTankTank** — -
* **TC5** — hasp (dropin/)
* **Terminator** — -
* **TombRaider** — -
* **TransformersShadowsRising** — -

## Per game: de statusnotitie

* **3DCosplayMahjong** — -
* **GaiaAttack4** — Start nog niet: geen beeld. De Wine-crash (MXCSR=0 door de lader) is omzeild (RG_LOADER_FPFIX), de game loopt de JVS-ketting door maar blijft zwart (g
* **GalagaAssault** — Geparkeerd. Een van drie Raw Thrills-games (met Jurassic Park, Pac-Man Chomp-Mania en Galaga Assault) die de lader van TeknoParrot onder Wine nog niet
* **Gashaaaan** — Speelbaar: munt (toets 5), start (toets 1), vizier en trekker (de bal) werken; eerste start toont 'BACKUP DATA ERROR': eenmaal TEST (toets 0) herstelt
* **Gashaaaan2** — Speelbaar na eenmalige instelling in het testmenu (klok, scherm, free play; staat nu in de game-opslag). Daarna start, vizier en trekker (de bal) werk
* **GhostBusters** — Start nog niet: de loader meldt 'GAME PATH CHECK FAILED' (de dump heeft geen donglebestand zoals Halo en Terminator). Geparkeerd.
* **GoGoStrike** — Werkt: bowling met de Move als trackball (richting en zwaai bepalen de worp), start (toets 1) en trekker (klik). Het beeld is 4:3 met zwarte randen (g
* **Halo** — Werkt volledig: lightgun, knoppen, trillen.
* **HauntedMuseum** — Start nog niet: geen beeld. De Wine-crash (MXCSR=0 door de lader) is omzeild (RG_LOADER_FPFIX), de game loopt de JVS-ketting door maar blijft zwart (g
* **HauntedMuseumII** — Start nog niet: geen beeld. De Wine-crash (MXCSR=0 door de lader) is omzeild (RG_LOADER_FPFIX), de game loopt de JVS-ketting door maar blijft zwart (g
* **HOTDSD** — Speelbaar via TeknoParrotUi (twee programma's: AM Daemon en de game): start (toets 1), vizier (gemeten op hoeken en midden), trekker (linkerknop) en s
* **InfinityBladeFX** — Start nog niet: UE3 start (Wine Mono en een eerste-start-dialoog voor TeknoParrot.dll nodig), daarna wit of zwart beeld; log stopt bij 'Kaboom: InitLi
* **JurassicPark** — Geparkeerd. Een van drie Raw Thrills-games (met Jurassic Park, Pac-Man Chomp-Mania en Galaga Assault) die de lader van TeknoParrot onder Wine nog niet
* **LethalEnforcers3** — Start nog niet: de game crasht bij het openen van een .m1v-film; Wine mist 32-bit GStreamer (lib32-gstreamer en MPEG-1-decoder), niet beschikbaar in d
* **LGI** — Speelbaar: start tot de attract, start (toets 1) en spelen. Het vizier volgt de aim (gemeten op een raster), de trekker schiet. Volledig scherm, geen 
* **LGI3D** — Speelbaar: start tot de attract, 3D-kalibratie (rood-cyaan beeld), start (toets 1) en spelen. Het vizier volgt de aim (gemeten op een raster in de tus
* **LGS** — Speelbaar: de game start tot de attract en het rijden (cursuskeuze en start met de knoppen). Rood, blauw, geel en groen zijn de toetsen A, S, D en F, 
* **LuigisMansion** — Werkt: titel, attract (free play), start (toets 1), stofzuiger richten en zuigen met de trekker; testmenu met toets 0, select switch op aux1. Laden du
* **MedaruNoGunman** — Start nog niet zonder fout: beeld, munt (toets 5), herladen, trekker en aim werken, maar de game meldt bij elke verse start 3001 RAM DATA IS BAD of 30
* **MusicGunGun2** — Start nog niet: de Wine-crash is omzeild (RG_LOADER_FPFIX), maar de game toont eigen foutvenster 'Direct3D-initialisatie mislukt' (ook met DXVK) en op
* **NERF** — Speelbaar: free play, start (toets 1), richten en trekker (score loopt op). Draait met DXVK in een eigen Wine-prefix; de BepInEx-plugin laadt. Niet ge
* **OmatsuriYasanGansoTakoyaki** — Start nog niet: wit venster (1024x768), de game maakt geen D3D-apparaat; de kern doet bij de start een online activeringsaanvraag. Geparkeerd.
* **OmatsuriYasanKingyoSukui** — Start nog niet: de game stopt met exitcode 8 nadat hij alle schijfletters heeft doorzocht op rqrdlist.rqr en campaign*.bmp. Geparkeerd.
* **Otomedius** — Start nog niet: wit venster; de game zoekt een ACIO-micom op COM2 ('COM Port 2 Open Error') en komt niet verder. Geparkeerd.
* **PacmanChomp** — Geparkeerd. Een van drie Raw Thrills-games (met Jurassic Park, Pac-Man Chomp-Mania en Galaga Assault) die de lader van TeknoParrot onder Wine nog niet
* **PigsMightFly** — Speelbaar: start (toets 1) en trekker (de hendel die het varken lanceert) werken; staand beeld (portret) in het midden van de tv. Testmenu: TEST (toet
* **RabbidsHollywood** — Start en reageert op de Move (munt, start, wereldkeuze), maar stopt na ongeveer een minuut zichzelf: de eigen dongle-controle van de game (SBK.Matrix,
* **RaccoonRampage** — Speelbaar: richten, trekker (ook = start) en munt (toets 5) werken; testmenu met TEST (toets 0), service (F3) en F4. De kalibratie is eenmalig gedaan 
* **RainbowBombergirl** — Start niet verder dan het begin van de game: de game start tot de attract (twee schermen: staand hoofdscherm rechts, aanraakscherm links onder), het t
* **Rampage** — Speelbaar, maar geen lightgungame: een joystick- en aanvalsspel voor drie spelers. Speler 1: omhoog = trekker, omlaag = herladen, links = granaat, rec
* **TankTankTank** — -
* **TC5** — Speelbaar via de IO-brug (tp-shm): richten, trekker en linker pedaal (middenknop) werken; munt = Move-knop voor toets 5 (de daemon geeft de game daarv
* **Terminator** — Werkt beeldvullend (via gamescope). Richten en schieten werken goed, ook via de IO-brug (tp-shm, 'play-halo.sh --route tp-shm').
* **TombRaider** — Werkt zonder TeknoParrot: lightgun, herladen, munt en start, trillen bij schieten. Nog niet met de echte Move-knoppen gespeeld.
* **TransformersShadowsRising** — Speelbaar met de Move: de daemon speelt het gun-bord van de game na (route hid-board). Vizier nagemeten op de tv (2026-10-11): binnen 1,5% van het sch
