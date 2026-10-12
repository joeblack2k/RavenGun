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
