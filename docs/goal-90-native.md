# Doel: 90% van de games native in RavenGun

Opdracht van de eigenaar (2026-10-14): minstens 90% van de titels speelt rechtstreeks vanuit onze app, zonder externe scripts of tools
(geen TeknoParrotUi, geen shellketen), met hooguit per-game data in `patches/<Game>/`; installeerbaar op een nieuwe SteamOS-pc.
Werkwijze: pass 1 = titels met een bekend recept (snel), pass 2 = titels die nieuw werk vragen. Een titel is **WORKING** na een
dubbelklik in de app en 5 minuten attract of spel op de tv, met bewijs in `~/.local/share/ravengun/bench-captures/<Game>/`.
Deze lijst is de enige werklijst; na elke titel bijgewerkt. Overgeslagen titels komen niet meer terug.

Stand: 12 van 81 titels WORKING (14%); doel 90% = 73 titels. Pass 1: 31, pass 2: 38, overgeslagen: 1.

## WORKING (12)

| Titel | Platform | Volgende stap |
|---|---|---|
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

## pass 1 (31)

| Titel | Platform | Volgende stap |
|---|---|---|
| Action Deka | Konami PC Based | profiel + direct, Konami PC |
| After Dark | UNIS PC Based | profiel + direct, UNIS (recept Raccoon Rampage) |
| Aliens Armageddon | RAW Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Aliens: Extermination | Global VR PC Based | profiel + direct, Global VR |
| America's Army Arcade | Global VR PC Based | native (Global VR Linux, ElfLdr2) |
| Big Buck Hunter Pro | Raw Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Big Buck Hunter Pro Home | Raw Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Block King Ball Shooter | Type-X PC Based | profiel + direct, Type-X |
| Bug Busters | EM-Teck PC Based | profiel + direct |
| Coopers's 9 | KONAMI PC Based | profiel + direct, Konami PC |
| Crossfire: Maximum Paintball | Linux PC Based | native (Linux, recept NERF/JP) |
| Drakons: Realm Keepers | Adrenaline Amusements PC-Based | profiel + direct, Adrenaline (recept Rampage) |
| Dream Raiders | SEGA RingWide | profiel + direct, SEGA RingWide (recept Let's Go Island) |
| Elevator Action Invasion | UNIS PC Based | profiel + direct, UNIS (recept Raccoon Rampage) |
| Far Cry Paradise Lost | Global VR PC Based | profiel + direct (indeling FarCryPipe bestaat) |
| Friction | Unique PC Based | profiel + direct, OpenParrot |
| Gashaaaan Refill!!!! | Konami PC Based | spelbestanden ontbreken lokaal: map zoeken/koppelen, dan dubbelklik |
| Gashaaaan!!!! | Konami PC Based | dubbelklik, 5 min (route bestaat) |
| Go Go Strike | PAOKAI Linux PC Based | spelbestanden ontbreken lokaal: map zoeken/koppelen, dan dubbelklik |
| Mars Sortie | UDC PC Based | profiel + direct |
| Mighty Moto | UNIS PC Based (Unity, 32-bit) with a local SmartFoxServer 1.6.6 (Java) | profiel + direct (Unity 32-bit) |
| Night Hunter - After Dark, Chapter 2 | UNIS PC Based | profiel + direct, UNIS (recept Raccoon Rampage) |
| Operation G.H.O.S.T. | SEGA RingWide | direct, SEGA RingWide (recept Let's Go Island) |
| Police Trainer 2 | Phantom Systems PC Based | native (Linux, ElfLdr2) |
| Pull The Trigger | COVIELSA PC-Based | profiel + direct |
| Robin Hood | ICE PC Based | profiel + direct |
| SEGA Golden Gun | SEGA RingWide | profiel + direct, SEGA RingWide (recept Let's Go Island) |
| Target: Terror Gold | Raw Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Walking Dead | RAW Thrills Linux PC Based | native (Raw Thrills Linux, recept NERF/JP) |
| Wartran Troopers | Konami PC Based | profiel + direct (indeling WartranTroopers bestaat) |
| Wild West Shootout | Coastal PC-Based | profiel + direct |

## pass 2 (38)

| Titel | Platform | Volgende stap |
|---|---|---|
| Elevator Action Death Parade | Taito Type X2 | Taito Type X2 / OpenParrot |
| Gaia Attack 4 | Taito Type X2 | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Galaga Assault | RAW Thrills Linux PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Ghost Squad Evolution | SEGA Lindbergh Red | Lindbergh-route bouwen |
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
| Mario Kart: Arcade GP DX | Namco ES3A | PRIORITEIT eigenaar: starten, gamepad als stuur, netwerk (ALL.Net zoals TeknoParrot), webcam |
| Medaru No Gunman | SEGA RingWide | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Men in Black | Windows PC | geparkeerd: twee versies, onderzoek ATTRACT-RUN.md |
| Metal Gear Arcade | Konami PC-Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Music GunGun 2 | Taito Type X2 | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Omatsuri Yasan - Ganso Takoyaki | CAVE PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Omatsuri Yasan - Kingyo Sukui | CAVE PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Otomedius Excellent | Konami PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Pacman Chomp-Mania | RAW Thrills Linux PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Point Blank X | Namco ES4 | Namco ES4 |
| Primeval Hunt | SEGA Lindbergh Red | Lindbergh-route bouwen |
| Rainbow Bombergirl | KONAMI PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Rambo | SEGA Lindbergh Red EX | Lindbergh-route bouwen |
| Silent Hill: The Arcade | Konami PC Based | geparkeerd; zie patches/<Game>/game.ini [Status] |
| Silent Scope: Bone Eater | Konami PC-Based | Konami TeknoMacaw |
| Star Trek Voyager | Team Play PC Based | Lindbergh-route bouwen |
| The House of the Dead 4 Special | SEGA Lindbergh Yellow | Lindbergh-route bouwen |
| The House of the Dead EX | SEGA Lindbergh Red | Lindbergh-route bouwen |
| The House of the Dead III | SEGA Chihiro | SEGA Chihiro (Xbox-emulatie) |
| Time Crisis 5 | Namco ES3X | invoer komt niet aan (onderzoek loopt) |
| Too Spicy | SEGA Lindbergh Red | Lindbergh-route bouwen |
| Transformers: Human Alliance | SEGA RingEdge 2 | SEGA RingEdge 2 |
| Transformers: Shadows Rising | Windows PC | bevriest ~1 min na START: SHELL_MSG_CHALLENGE (onderzoek loopt) |
| Virtua Cop 3 | SEGA Chihiro | SEGA Chihiro (Xbox-emulatie) |

## overgeslagen (1)

| Titel | Platform | Volgende stap |
|---|---|---|
| Pigs Might Fly | SEGA PC-Based | eigenaar: niet spelen (staand beeld) |

Geen spel (mappen in de bibliotheek, niet meegeteld): TNXK0O~C, _ia_work, _meta-quarantaine-2026-10-13, dump, findings.
