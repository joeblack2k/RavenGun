# Laatste ronde: alle games naar Working

Stand 2026-10-17 (gegenereerd uit de patches; geluid en invoer gemeten in de speelproeven van 17/18-10, zonder TP-map, in muismodus: munt = `key 5`, start = `key 1`, schot = trigger op het midden).

Overgeslagen (skipregel, niet 16:9 of 4:3): Silent Scope, Elevator Action Death Parade, America's Army Arcade.

Lat van de eigenaar (2026-10-17): een game is **Working** als hij start via dubbelklik (zonder TeknoParrot), **geluid** heeft,
de **invoer** werkt met muis en PS Move, en **alle game-specifieke instellingen** in Properties > Options staan (en ook iets doen
op de route zonder TP). Ghostbusters en Walking Dead doen weer mee.

Kolommen: *zonder TP* = het meegeleverde plan noemt geen TP-bestand (`rg-direct --tp-refs`); *kandidaat* = met de kandidaat uit
het onderzoek wel; *beeld* = zonder TP-map eigen beeld op de tv; *opties* = aantal instellingen in het optiemenu (`options.json` +
`[ConfigType]`); *status* = `[Status]` in `game.ini` nu.

| Game | Route | Zonder TP | Kandidaat | Beeld zonder TP | Geluid | Invoer (muis/Move) | Opties | Status |
|---|---|---|---|---|---|---|---|---|
| 2Spicy | native | ja |  | ja | ja | ja (munt, start, tutorial) | 1 | working |
| AfterDark | direct | nee | nee | nog meten | nog meten | nog meten | 9 | working |
| AfterDark2 | direct | nee | nee | nee (dongle) | nog meten | nog meten | 9 | working |
| AliensArmageddon | native | ja |  | nog meten | nog meten | nog meten | 6 | untested |
| AliensExtermination | direct | nee | ja | ja (attract, films) | ja | munt ja na fix (4 credits nodig) | 20 | working |
| BugBusters | direct | nee | ja | ja (attract) | ja | ja (munt, start, schieten, zone 1) | 7 | working |
| Coopers9 | direct | nee | nee | nog meten | nog meten | nog meten | 13 | untested |
| CrossfirePaintball | direct | nee | nee | nog meten | nog meten | nog meten | 11 | working |
| DrakonsRealmKeepers | direct | nee | nee | nog meten | nog meten | nog meten | 8 | working |
| ElevatorActionInvasion | direct | nee | ja | ja (attract) | ja | ja (start) | 16 | working |
| FarCryParadiseLost | direct | nee | nee | ja | ja | ja (munt, start, kaart) | 16 | working |
| Friction | direct | ja |  | ja | ja | munt ja, start? | 10 | working |
| GaiaAttack4 | direct | ja |  | ja | ja | ja (munt, start, gebied/missie) | 32 | working |
| Gashaaaan | direct | nee | ja | ja (titel) | ja | ja (munt, start, koers) | 27 | working |
| GG | direct | nee | ja | ja (attract, Engels) | ja | munt ja (4 credits nodig) | 10 | working |
| GSEVO | native | ja |  | ja | ja | ja (munt, start, kostuum) | 1 | working |
| Halo | native | ja |  | ja | nee op 50 s (FMOD-wrapper, nagaan) | ? (startscherm na 60 s) | 16 | working |
| HauntedMuseum | direct | ja |  | ja | ja | ja (munt, start) | 28 | working |
| HauntedMuseumII | direct | ja |  | ja | ja | ja (munt, start) | 28 | working |
| HOTD4 | native | ja |  | ja (intro, HUD-tekst vaag) | ja (stereo; 5.1 als optie) | ja (munt, start) | 29 | untested |
| HOTD4SP | native | ja |  | ja | ja | ja (start; grijze textures) | 18 | untested |
| HOTDEX | native | ja |  | ja | ja (ALSA->Pulse) | ja (munt, start) | 12 | working |
| HOTDSD | direct | nee | ja | nee (ALLS-keychip) | nog meten | nog meten | 40 | working |
| InfinityBladeFX | direct | nee | ja | nee (wit) | ja | - | 10 | playable |
| JurassicPark | native | ja |  | ja (attract) | ja (OSS->Pulse) | ja (start, missie) | 21 | working |
| LethalEnforcers3 | direct | nee | ja | nee (zwart; venstermodus in proef) | ja | ja (bord, munt, start, trekker in het log) | 26 | working |
| LGI | direct | nee | ja | nee (stopt ~37 s) | nog meten | nog meten | 24 | working |
| LGI3D | direct | nee | ja | nee (stopt) | nog meten | nog meten | 34 | working |
| LGJ | native | ja |  | ja | ja | ja (munt, start, rit) | 18 | working |
| LGJS | native | ja |  | ja | ja | ja (start, rit) | 13 | working |
| LGS | direct | nee | ja | ja (attract, Engels) | ja | ja (rit) | 20 | working |
| LostLandAdventure | direct | nee | nee | nog meten | nog meten | nog meten | 11 | untested |
| LuigisMansion | direct | ja |  | ja | ja | nee (munt komt niet aan) | 23 | playable |
| MarioKartDX | direct | ja |  | nee (zwart; F:-paden zoals TP in proef) | ja | - | 45 | working |
| MarsSortie | direct | ja |  | ja | ja | ja (start, level) | 14 | working |
| MenInBlack | direct | ja |  | ja | ja | ja (start, PLAYER 1 READY, spel) | 0 | untested |
| MusicGunGun2 | direct | ja |  | ja | ja | ja (munt, start) | 12 | working |
| NERF | native | ja |  | ja | ja | ja (start, schieten) | 15 | working |
| OG | direct | nee | ja | ja (Presented by SEGA) | ja | ja (munt, start) | 35 | working |
| PointBlankX | direct | ja |  | ja | ja | munt ja (3 credits nodig) | 10 | working |
| Primevil | native | ja |  | ja | ja (ALSA->Pulse) | ja (start, wapen, gebied) | 1 | working |
| PullTheTrigger | direct | nee | nee | nog meten | nog meten | nog meten | 10 | working |
| RabbidsHollywood | unity | ja |  | ja | ja | munt ja (4 credits nodig) | 25 | working |
| RaccoonRampage | direct | nee | ja | ja (attract, banner kalibratie) | ja | ja (munt, start) | 31 | working |
| Rambo | native | ja |  | ja (initialiseert >150 s) | ja | ? (opstart te lang voor de meting) | 25 | working |
| Rampage | direct | ja |  | ja (attract) | ja | ? (start onduidelijk) | 6 | working |
| RobinHood | direct | nee | nee | nog meten | nog meten | nog meten | 5 | working |
| SDR | direct | nee | ja | nee (stopt) | - | - | 9 | working |
| SilentHill | direct | ja |  | ja | ja | ja (start) | 19 | working |
| TargetTerrorGold | native | ja |  | ja (LOADING >80 s) | ja (OSS->Pulse, DEEPBIND) | ? | 12 | working |
| TC5 | direct | nee | ja | ja (attract) | ja | munt ja (start = pedaal) | 48 | playable |
| Terminator | direct | nee | nee | nog meten | nog meten | nog meten | 26 | working |
| TombRaider | unity | ja |  | ja | ja | munt ja (4 credits nodig) | 18 | working |
| Transformers | direct | nee | ja | nee (0949 keychip) | nog meten | nog meten | 11 | working |
| TransformersShadowsRising | direct | ja |  | ja (attract, missie) | ja | ja (start, ROLL OUT) | 31 | working |
| WildWestShootout | direct | ja |  | ja | ja | ja (start, kaart, level) | 10 | working |

## Hoe er gemeten wordt

- Beeld: proefscript met de TP-map hernoemd; kandidaat alleen tijdens de eigen test in de gebruikerslaag; foto's op 45/90/150 s.
- Geluid: `pactl list sink-inputs` tijdens het spel: een stroom van het spelproces, kanalen en volume (en horen op de tv).
- Invoer: muismodus (`ravengunctl mouse-gun`), munt `ravengunctl key 5`, start `key 1`, schieten `test-pattern point X Y` +
  `output-button trigger`; foto's ervoor en erna (credits, menu, treffer). De Move gaat door dezelfde virtuele lightgun.
- Opties: elke optie één keer omzetten en kijken of de game het merkt (bv. regio, venster, geluid); opties die alleen de
  TeknoParrot-kern las, krijgen op de route zonder TP een eigen uitwerking of verdwijnen uit het menu.

Grenzen: zie `docs/decisions.md` (laatste secties): beveiliging nabootsen mag voor de eigen games; keychip-/dongle-cryptografie
wordt niet verder uitgebreid (HOTDSD en Transformers blijven daarop staan).
