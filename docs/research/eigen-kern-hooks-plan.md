# Eigen kern: per-game hooks — plan en volgorde

Stand 2026-10-12. Vervolg op de terreinkaart `patches/EIGEN-KERN.md` (patches-repo) en het
besluit in `docs/decisions.md` (2026-10-12). Dit stuk gaat alleen over de **volgorde en de
afweging**, niet over het schrijven van hooks zelf — dat wacht op de prioriteit van de eigenaar
én de tv met lightgun.

## Waar we staan

- Patch `0003` (loader-repo `f3ff5d0`) maakt dat een onbekende game niet meer omvalt: hij draait op
  de **generieke route** (`GameID::Global`, geen per-game hooks). `current/` is herbouwd met 0003
  (Win32-sha256 `258aa9fc…`). De vier naam-herkende games geven nog steeds START, LGI start ook.
- Onze kern herkent **4 van de 36** games (GaiaAttack4, HauntedMuseum, HauntedMuseumII,
  MusicGunGun2) — precies die met een profiel dat óók in de openbare OpenParrot-bron zit.

## De kern van de afweging

"Start en blijft draaien" ≠ "speelbaar". De overige **32** games gebruiken `EmulationProfile`s die
in de **gesloten** TeknoParrot-kern zitten, niet in de openbare bron. Die overnemen = TeknoParrots
per-game invoer-/JVS-vertaling opnieuw bouwen, per game, en op de tv verifiëren. En: alle 32 spelen
vandaag al op de gesloten kern. Blind omzetten ruilt een werkende game in voor een die alleen start.
Daarom staat `Type = OpenParrot` niet standaard in hun `game.ini`.

## Volgorde (goedkoopst eerst)

1. **De vier al-herkende games op de tv nalopen.** Spelen GaiaAttack4, HauntedMuseum,
   HauntedMuseumII en MusicGunGun2 vólledig op onze kern (niet alleen starten)? Zo ja: `Type =
   OpenParrot` voor díe vier. Geen reverse-engineering nodig — de hooks zitten al in de
   openbare bron. Dit is de enige echte winst die geen RE vraagt; hier beginnen.
2. **Eén gesloten profiel als eerste bewijs.** Pas na stap 1: het eenvoudigste gesloten profiel
   kiezen (de eigenaar beslist welke game), de `case <CRC>` in `GameDetect.cpp` zetten en de hooks
   van dat profiel schrijven. Verifiëren op de tv. Dit is de echte test of hook-overname haalbaar is.
3. **Opschalen alleen als stap 1-2 kloppen.** De grote groepen (`WartranTroopers` 13 games,
   `SegaJvsLetsGoIsland` 2) pas aanpakken als één profiel bewezen werkt.

## Wat hier NIET bij hoort

- De 8 ElfLdr2-games (Jurassic Park, Pac-Man, Galaga, Ghostbusters, …) zijn ELF's zonder PE-header;
  hun profiel is van BudgieLoader, niet van de kern. Die vragen een eigen LD_PRELOAD-kastnabootser,
  geen `GameDetect.cpp`-case. Apart spoor, nu geparkeerd (packer-/GOT-werk, buiten scope).
- Dongle, keychip, integriteit, abonnement: ongewijzigd, niets aan toevoegen.

## Waarom nu nog geen code

Elke stap hierboven vraagt verificatie op de tv met een lightgun (speelbaarheid is niet via de CLI te
meten) en een prioriteitskeuze van de eigenaar (welke game eerst, en of een werkende game het risico
waard is). Daarom is er in de nacht bewust **geen** `case` geschreven en **geen** `Type` omgezet: de
route is bewezen, de vervanging wacht op die twee dingen.

## Meting 2026-10-12: welke games hebben überhaupt een kern-hook nodig?

De eigenaar vroeg te beginnen met de hooks (voorkeur andere sessie: `SegaJvsLetsGoIsland` / LGI).
Vóór het schrijven heb ik per game de **invoerroute** nagekeken (`[Input] Route` in elke `game.ini`)
en de broncode van onze kern. Dat verandert de keuze. De route bepaalt of de invoer überhaupt
door de kern gaat:

| Route | Games | Gaat de invoer door de kern? |
|---|---|---|
| `jvs` (pipe/pty) | LGI, LGI3D, LGS | **Nee.** De game praat JVS over `\\.\pipe\TeknoParrot_JVS`; de RavenGun-daemon ís het JVS-bord (`jvs-boards.json`). De kern raakt dit nooit aan. |
| `tp-shm` | WartranTroopers-familie (13), LuigisMansion, FarCry, InfinityBlade, TC5, SegaJvsGoldenGun, + de al-herkende Type X2-games | In-proces via de mapping `TeknoParrot_JvsState`. Hier ligt de vraag. |
| `evdev` | HOTDSD, JurassicPark | Nee (directe Linux-invoer). |
| `unity-tcp` | Rabbids, TombRaider | Nee (eigen Unity-plugin over TCP). |
| `hid-board` | Transformers | Nee (SaeGunBoard). |

**LGI is dus géén goed eerste doel.** De invoer van LGI/LGI3D/LGS loopt buiten de kern om. Gemeten:
`proef-eigen-kern.py LGI --plan` met `Type = OpenParrot` kiest onze lader+kern, en de `[Input]`-route
blijft ongewijzigd `jvs`/pipe — de kern wisselt alleen de lader/kern-binaries, niet de invoer. De
andere sessie had al gemeten dat LGI op de generieke route start en blijft leven met antwoordende
JVS-pipe, en de findings (`findings/LetsGoIsland.md`) noemen LGI **speelbaar** via precies die externe
route. Een `case 0xd400a3f5 → GameID::LetsGoIsland` met hooks zou voor LGI dus niets zinnigs doen; het
enige dat de gesloten kern voor LGI deed is een onvoorwaardelijke `CreateWindowExW`-hook — en dat is
juist de *bug* die vensterstand laat crashen. **Gevolg:** LGI/LGI3D/LGS kunnen zonder enige hook naar
`Type = OpenParrot` (zodra de eigenaar het op de tv bevestigt) — dat is drie games van de gesloten
kern af, met nul regels hook-code.

**De échte hook-vraag zit bij `tp-shm`.** Alle `tp-shm`-games starten en blijven leven op onze kern
(70 s gemeten, `proef-lang.out`), dus booten is het probleem niet. Het gaat puur om de invoer, en
die hangt af van **wie de mapping `TeknoParrot_JvsState` leest**:
- Leest de **game** die mapping zelf (bij naam)? Dan is er geen kern-hook nodig en werken de
  `tp-shm`-games net als LGI al op de generieke route — dan is "zoveel mogelijk zelf doen" grotendeels
  al bereikt via de externe shm/pipe-brug.
- Of hookt de **gesloten kern** de I/O-calls van de game en bedient die vanuit de shm? Dan moet onze
  kern dat per game nabouwen — dát is het echte werk.

Dit is niet headless te beslissen (invoer meten vraagt de tv) en de andere sessie heeft de
`tp-shm`-route gebouwd, dus die weet het antwoord. Zodra dat vaststaat is het juiste eerste doel een
**enkel `tp-shm`-spel** (voorstel: `LuigisMansion`, `MedaruNoGunman`/FarCry of `InfinityBladeFX` —
één game, eigen profiel), niet LGI en niet meteen de 13 WartranTroopers.
