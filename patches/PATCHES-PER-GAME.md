# Patches per game — welke patch nodig is, en of die er is

*Wat dit bestand is:* één kaart over **alle 36 games** in `patches/` met per game (1) **welke patch
nodig is**, (2) **of die er is** met het bewijs (bestandsnaam + wat erin staat), en (3) **wat een
start zonder TeknoParrot blokkeert**. Het is de tegenhanger van `patches/STAND.md`: dat bestand kijkt
naar de **volledigheid van de map** (game.ini, drop-in, secties, dongle), dit bestand naar de
**vraag "wat moet er gepatcht worden, en is dat gebeurd"**.

*Ronde:* 26 (2026-10-11). *Gemaakt door:* de patches-agent, op de Deck.

*Dit bestand is met de hand geschreven, niet gegenereerd.* De repo-`README.md` zet de zes kaarten
onder één kop ("Ze worden gegenereerd"); dat geldt voor `patches/STAND.md` en `patches/STARTTEST.md`
(die komen uit `patches/stand-gen.py` en `patches/starttest-gen.py`) en voor de tabel van
`patches/REGIO-PER-GAME.md`, maar **niet** voor dit bestand: de kolom *welke patch nodig is* en de
onderbouwing per game zijn oordelen op grond van meerdere bronnen, en die zijn niet uit de mappen af
te leiden. Elke regel noemt daarom zijn bron, zodat wie het er niet mee eens is precies kan zien waar
het vandaan komt. De **tellingen** onderaan zijn wél allemaal met een commando na te rekenen (zie
[Hoe deze kaart is gemaakt](#hoe-deze-kaart-is-gemaakt)).

---

## De vier soorten patch

| Waarde | Wat het betekent | Waar het werk zit |
|---|---|---|
| **`geen`** | Deze game heeft geen patch nodig: hij start en draait zoals hij is, en er ontbreekt niets in zijn map. | — |
| **`drop-in`** | Er moeten **bestanden in de view van de game** komen: onze donglebibliotheek, de GL-fix, de inline-hook, of de gegevens ernaast. Dat is `patches/<Game>/dropin/` + `dropin.ini`, met **een eigen sectie per bestand** (`docs/dropins.md`). | deze repo (`ravengun-patches`) |
| **`patches.ini`** | Er moet een **bytepatch** in de map van de game: `Kind = file` op de lader, de kern of de game zelf, of `Kind = memory` op de hoofdmodule. De motor is `scripts/patchfile.py` (loader-repo). | deze repo (`ravengun-patches`) |
| **`eigen kern`** | De game loopt op de **gesloten `TeknoParrot(64).dll`**; om hem zonder TeknoParrot te draaien is een eigen kern nodig die `InitializeASI` exporteert en de hooks van dat `EmulationProfile` implementeert. | **de loader-repo** (centrale arbeid, niet per game) — deze repo kan dit niet leveren |

**Combinaties komen voor.** Een game kan bijvoorbeeld een drop-in **en** een eigen kern nodig hebben.
De kolom *Patch nodig* noemt dan beide; de kolom *Er?* zegt per onderdeel of het er is.

**Wat "eigen kern" niet is:** een `patches.ini`-regel. De bytepatches die de kern moet toepassen
staan voor vier games al in hun map; de **motor** die ze toepast is de kern. Voor de andere games is
er nog geen enkele patch bekend, omdat de byte-adressen in de gesloten kern niet statisch te lezen
zijn (`findings/12_aanpassingen_en_koppelingen.md` §6).

---

## Hoe deze kaart is gemaakt

Alles is **gemeten**, niet overgenomen. De commando's, allemaal op de Deck:

```bash
# 1. de volledigheid van de mappen (hetzelfde script als STAND.md)
cd ~/projects/ravengun-patches && python3 patches/stand-gen.py

# 2. de lader, de kern, de drop-in en de patches per game
cd ~/projects/ravengun
python3 tools/verify-all.py                 # de kolom Patches + "drop-ins die NIET aankomen"
python3 tools/verify-all.py --md /tmp/per-game-stand.md
python3 tools/verify-all.py GaiaAttack4 HauntedMuseum HauntedMuseumII MusicGunGun2

# 3. wat de patchmotor met de patches van één game doet (controleren, schrijft niets)
python3 scripts/patchfile.py --game <Game> --game-exe <exe> --loader <lader> --core <kern>
python3 scripts/patchfile.py --game <Game> --game-exe <exe> --take-over-core

# 4. de dongle van één game
python3 scripts/hasp.py   --game <Game> --json     # ok: true = compleet
python3 scripts/matrix.py --game <Game>
python3 scripts/region.py --game <Game>

# 5. de testsuite
python3 -m unittest discover -s tests 2>&1 | tail -4
```

Daarnaast gelezen: `patches/STAND.md`, `patches/STARTTEST.md`, `patches/<Game>/game.ini`,
`patches/<Game>/README.md`, `patches/<Game>/patches.ini`, `patches/<Game>/dropin/`, en in de
loader-repo `docs/per-game-patches.md`, `docs/loskoppelen-per-game.md`, `docs/dropins.md`,
`docs/geen-dongle-in-de-loader.md`, `docs/patchmotor.md`, `docs/region-layer.md`,
`docs/matrix-layer.md`, `docs/hasp-layer.md`, `docs/elfldr2-route.md`, `docs/todo.md`. Op de NAS:
`findings/<Game>.md` per game en `findings/WAT-ELK-SPEL-NODIG-HEEFT.md`,
`findings/12_aanpassingen_en_koppelingen.md`, `findings/MATRIX-dongle-API.md`,
`findings/INVENTARIS-dongle-per-game.md`.

Voor de vraag **"zijn de patches van de vier games met een eigen kern compleet"** is de bron van de
kern zelf gelezen: `TypeX2Generic.cpp` uit de OpenParrot-bron waarmee onze eigen kern gebouwd is
(`~/.cache/ravengun/build/openparrot/fc8ba632…/src/OpenParrot/src/Functions/Games/TypeX2/TypeX2Generic.cpp`,
GPL-3.0, commit `fc8ba632e1f67c5c340ff88f30efca743e8d38f5`).

### De verificatie van deze ronde (uitvoer letterlijk)

Drie keer gedraaid, en de derde keer wijkt af. Dat is geen fout in deze repo maar een **koppeling
tussen de twee repo's**; zie [De testsuite van de loader kijkt naar deze repo](#de-testsuite-van-de-loader-kijkt-naar-deze-repo)
hieronder.

**1 en 2 — `python3 tools/verify-all.py`, exitcode 0** (identiek in beide runs, en ook in de derde):

```
games:                       36
lader van ons:               25
kern van ons:                4
kern nog TeknoParrot:        22
lader nog BudgieLoader:      7
met dongle.json:             13
  waarvan script ontbreekt:  0
met dropin/:                 15
  drop-ins die NIET aankomen:0
plan mislukt:                3
  !! 3DCosplayMahjong             rc=4
  !! HOTDSD                       rc=4
  !! TankTankTank                 rc=4
```

**1 en 2 — `python3 -m unittest discover -s tests`, exitcode 0:**

```
Ran 240 tests in 55.979s

OK (skipped=21)
```

**3 — dezelfde testsuite, ná de twee toegevoegde patches van HauntedMuseumII, exitcode 1:**

```
FAIL: test_repo_tree (test_gamesettings.PatchPolicyTest.test_repo_tree)
  File "/home/deck/projects/ravengun/tests/test_gamesettings.py", line 261, in test_repo_tree
    self.assertEqual(len(mem), 10)
AssertionError: 12 != 10

Ran 240 tests in 56.550s

FAILED (failures=1, skipped=21)
```

De drie games met `rc=4` zijn de drie die niet met de directe start te starten zijn: `HOTDSD` heeft
twee exe's en `3DCosplayMahjong` en `TankTankTank` staan niet op de NAS. Dat is dezelfde 36 − 3 = 33
die `patches/STARTTEST.md` als **START** meet (33 getest, 33 START, 0 viel uit, 0 niet gestart).

---

## De testsuite van de loader kijkt naar deze repo

Dit is een **harde koppeling** die je moet weten voordat je een patch toevoegt: de testsuite van de
loader-repo (`~/projects/ravengun`) leest de **meegeleverde `patches/`** en heeft de inhoud ervan in
getallen vastgelegd. `tests/test_gamesettings.py`, test `PatchPolicyTest.test_repo_tree`, doet dit:

```python
mem = [(n, p) for n in ("HauntedMuseum", "HauntedMuseumII", "GaiaAttack4", "MusicGunGun2")
       for p in gs.load_game(n, shipped=shipped, user=...).patches if p.kind == "memory"]
self.assertEqual(len(mem), 12)
...
versions = sorted(p.version for n, p in mem if n == "HauntedMuseumII")
self.assertEqual(versions, ["2100", "2100", "2100", "2101J", "2101J", "2101J"])
```

Toen deze kaart werd gemaakt stond die test nog op **10** memory-patches en op `Find` leeg bij alle.
De twee `MakeNOP`-patches van HauntedMuseumII en de gemeten `Find` maken dat **12** en **acht met een
`Find`**; de test is daarom in de loader-repo bijgewerkt (commit `4f18989`, *"De memory-patches van de
vier Type X2-games hebben nu een Find"*), samen met `docs/per-game-patches.md` en
`docs/patchmotor.md`, die dezelfde getallen noemden. De suite is daarna weer groen: **240 tests,
`OK (skipped=21)`**.

**Wat dat betekent:**
1. **`verify-all.py` blijft groen** (exitcode 0, "drop-ins die NIET aankomen: 0").
2. **Wie een memory-patch toevoegt, weghaalt of van een `Find` voorziet, moet die test bijwerken.**
   Een test die het **aantal** patch-secties en de `Find`-loze gevallen vastlegt, breekt bij elke
   uitbreiding. Dat is een bewuste keuze — hij bewaakt dat de meegeleverde patches niet stilletjes
   veranderen — maar het is handwerk.

---

## Wat er in deze ronde is veranderd

De patches-repo liep **tijdens** dit werk door: andere agents hebben hun ronde afgemaakt terwijl deze
kaart werd gemaakt. Alles hieronder is in dit bestand al verwerkt, en het is de reden dat de tellingen
afwijken van wat er in een eerdere versie van deze kaart stond.

| Wat | Voor | Na | Bron |
|---|---|---|---|
| `README.md` per game | 31 van 36 | **36 van 36** | 3DCosplayMahjong, HOTDSD, RainbowBombergirl, TankTankTank en TransformersShadowsRising kregen er een |
| `[Status] Status` | 34 van 36 | **36 van 36** | 3DCosplayMahjong en TankTankTank kregen `untested` met de reden erbij |
| `Size` bij `jmp-returns-true` | **ontbrak bij alle 6** | **`Size = 6` bij alle 6** | de vier `patches.ini`-bestanden |
| `patches.ini` van HauntedMuseumII | 5 secties | **7 secties** | twee `MakeNOP`-patches toegevoegd: `0x1391` (`Size = 1`) en `0x1105` (`Size = 6`) |
| de motor op de memory-patches | 6 geweigerd | **0 geweigerd** | hermeting met `--take-over-core` |
| TC5 `dropin.ini` | 3 DLL-secties zonder `Env` | **`Env = RAVENGUN_HASP_DIR=…;RAVENGUN_HASP_GAME=TC5` op alle drie** | bewezen dat de DLL in het **spelproces** komt (`grep -i hasp /proc/<pid>/maps`) |
| TC5's view | `TC5/Binaries/Win64` was een symlink naar de NAS | **gematerialiseerd** (echte mappen met symlinks) | zonder dat kwam er geen enkel drop-in-bestand aan |
| `STAND.md`, `STARTTEST.md` | — | opnieuw gegenereerd | `stand-gen.py`, `starttest-gen.py` |
| `Find` bij de memory-patches | **0 van de 12** | **10 van de 14** — gemeten op de echte exe's | ronde 33 en 39, zie [De Find is gemeten](#de-find-is-gemeten) |
| de testsuite van de loader | `FAILED (failures=1)`, 240 tests | **`OK (skipped=21)`**, 240 tests | commit `4f18989` in de loader-repo |

**Wat dat aan de conclusies verandert:** de vier `patches.ini`-bestanden zijn een stuk dichter bij
compleet dan aan het begin van deze ronde — het `MakeJMP`/`MakeNOP`-deel is nu volledig (12
memory-patches, allemaal geaccepteerd, alle adressen in `.text`). Wat er nog ontbreekt zijn de
**hooks** en de **`WriteMemory`-waarden**, en die vallen deels buiten het formaat. Zie
[Ontbrekende patches](#ontbrekende-patches-die-er-wel-zouden-moeten-zijn).

---

## Hoe je deze kaart vernieuwt

1. **De mappen en de drop-ins.** `cd ~/projects/ravengun-patches && python3 patches/stand-gen.py`
   werkt `patches/STAND.md` bij. `cd ~/projects/ravengun && python3 tools/verify-all.py` geeft de
   kolom `patches_aan,patches_uit,patches_geweigerd` en de regel `drop-ins die NIET aankomen`; die
   twee moeten in de tabel hieronder terugkomen. **Verandert een van die getallen, dan is deze kaart
   verouderd.**
2. **De patches zelf.** Voor elke game met een `patches.ini`:
   `python3 scripts/patchfile.py --game <Game> --game-exe <exe> --loader <lader> --core <kern>`
   (het pad van de lader en de kern komen uit `python3 scripts/launch-direct.py <Game> --plan`).
   Wil je zien of de patches kloppen als **onze eigen kern** ze zou overnemen, zet dan in een kopie
   van de ini `Enabled = true` naast `AppliedBy = core` en gebruik `--take-over-core`; de motor meldt
   dan per patch `gecontroleerd`, `overgeslagen` of `geweigerd` met de reden.
3. **De dongles.** `python3 scripts/hasp.py --game <Game> --json` (let op `ok` en `errors`),
   `python3 scripts/matrix.py --game <Game>`, `python3 scripts/region.py --game <Game>`.
4. **De blokkades.** `patches/<Game>/game.ini` `[Status] Note`, `patches/<Game>/README.md`, en
   `findings/<Game>.md` op de NAS. `patches/STARTTEST.md` opnieuw maken met
   `bash /home/deck/starttest3.sh 22 && python3 patches/starttest-gen.py`.
5. **De tests.** `python3 -m unittest discover -s tests 2>&1 | tail -4` hoort `OK` te geven (240
   tests, `skipped=21`). **Let op:** die suite leest de `patches/` van deze repo en heeft de inhoud
   in harde getallen vastgelegd (`tests/test_gamesettings.py` regel 261 en 268). Voeg je een
   memory-patch toe of haal je er een weg, werk die twee regels dan bij — zie
   [De testsuite van de loader kijkt naar deze repo](#de-testsuite-van-de-loader-kijkt-naar-deze-repo).
6. **Werk de tellingen onderaan bij** en, als een game van soort verandert, de regel in de tabel en de
   onderbouwing eronder.

---

## De tabel — alle 36 games

**Kolommen.** *Patch nodig* = de soort patch die deze game nodig heeft (zie de vier soorten
hierboven). *Er?* = is die er, met het aantal bestanden/secties. *Bewijs* = het bestand en wat erin
staat. *Status* = `[Status] Status` uit `game.ini`. *STARTTEST* = het resultaat in
`patches/STARTTEST.md` (33 games zijn daar gemeten; de andere drie zijn niet te starten).

| # | Game | Patch nodig | Er? | Bewijs (bestand + wat erin staat) | Status | STARTTEST | Wat een start zonder TeknoParrot blokkeert |
|---|---|---|---|---|---|---|---|
| 1 | **3DCosplayMahjong** | `geen` (niet vast te stellen) | n.v.t. | alleen `game.ini` + `README.md`; geen `patches.ini`, geen `dropin/`, geen `dongle.json`. De README zegt hetzelfde en noemt de drie dingen die ontbreken: de dump, een gamemap/startmeting en de bestandsscan | untested | niet getest | Het spel staat **niet op de NAS**: `--plan` rc=4 "game niet gevonden". Niets te patchen tot de dump er is; er is ook **geen bestandsevidence** over een dongle — alleen het profiel noemt er geen. |
| 2 | **GaiaAttack4** | `eigen kern` + `patches.ini` | ja, **onvolledig** | `patches.ini`: 3 secties. 2× `Kind = memory`, `AppliedBy = core`, `Enabled = false`, `Size` gezet: `game.exe+0x113E80` `jmp-returns-true` `Size = 6`, `game.exe+0x1156C8` `nop` `Size = 4`. Plus de gedeelde lader-patch `Enabled = true`. verify-all: **0 aan / 2 uit / 1 geweigerd** | parked | START | Zwart beeld na de JVS-functiecontrole (`handle_syscall_fault code=c0000005`); de Wine-MXCSR-bug is met `RG_LOADER_FPFIX` omzeild en zit nu in de bron van onze eigen lader. |
| 3 | **GalagaAssault** | `drop-in` | **ja (6/6)** | `dropin.ini` met 6 secties: `librt-glfix.so` (`Preload`), `libravengun-hasphook.so` (`Preload`, `Audit = false`), `libravengun-hasphook-x64.so` (sectie **zonder** `Preload`), `ravengun-hasp.so`, `ravengun-hasp/GalagaAssault/dongle.json` (hasp, fileid 65520, 112 B, **geconstrueerde** image) + `memory/…1111111111110000`. `hasp.py --json`: `ok: true` | parked | START | De gesloten `BudgieLoader` en de **gepakte ELF**: GL via `dlsym`, 357 vaste GOT-slots in het bestand gevuld met 0xb7xxxxxx-adressen; van de imports zijn er 258 zonder naam en pusht elke PLT-stub 0xdeadbeef → `GL_MAX_TEXTURE_SIZE > 2047` nodig. Daarnaast mist de dump de lettertypen `images/fonts/zoinks*.tga` en `AlSeana*.tga`; crash op EIP `0x0808957D`. |
| 4 | **Gashaaaan** | `eigen kern` | **nee** | alleen `game.ini` + `README.md`; geen `patches.ini`, geen `dropin/` | playable | START | De gesloten `TeknoParrot.dll`. Met `TP_REMOTETHREAD` vastlopen op de loader-lock of exitcode `0xDEADC0DE`; **zonder** enige `TP_*`-injectievariabele start hij en is hij speelbaar. |
| 5 | **Gashaaaan2** | `eigen kern` | **nee** | als Gashaaaan | playable | START | De gesloten kern **plus** de `Patreon`-activering in de Wine-prefix (`needs_activation: true`). |
| 6 | **GhostBusters** | `drop-in` | **nee (0/0)** | `dropin/dropin.ini` bestaat maar heeft **0 secties en 0 bestanden** (alleen commentaar). De README beschrijft het recept: de `pm`-map is géén drop-in maar een map met koppelingen in de view. | parked | START | De gesloten `BudgieLoader` en `GAME PATH CHECK FAILED` — opgelost met `[Identify] Path = pm/game`; daarna komt de game niet in beeld. De ELF bevat `DongleInitialize() FAILED!`, `Game dongle initialization failed!`, `ReInitAladdinDongles`, `hasp_login`, `nethasp.ini`, dus een HASP-dongle is waarschijnlijk, maar **welk bestand ontbreekt is niet gemeten**. |
| 7 | **GoGoStrike** | `drop-in` (de eigen `LD_PRELOAD`-kastnabootser, 64-bits) | **nee** | geen `dropin/`; de README zegt het expliciet: *"Er is voor deze game geen drop-in"*. `docs/loskoppelen-per-game.md` zet bij deze game als kortste route "eigen `LD_PRELOAD` (64-bit familie)" en in de drop-in-kaart "ja: eigen `LD_PRELOAD` (64-bit)". `STAND.md` noemt de map "compleet; geen drop-in nodig" — dat gaat over de **map**, niet over de loskoppeling | playable | START | De gesloten `BudgieLoader` en de `Patreon`-activering. De game zelf werkt (bowling met de Move als trackball). |
| 8 | **HOTDSD** | `geen` (wel een eigen startketen) | n.v.t. | alleen `game.ini` + `README.md`; geen `patches.ini`, geen `dropin/`, geen `dongle.json`. `STAND.md` telt de ontbrekende `dongle.json` van het soort `amdaemon` als gat | playable | niet getest | **Twee exe's**: `Executable2 = amdaemon.exe` met `LaunchSecondExecutableFirst = true`; `launch-direct.py` start geen profielen met twee exe's (rc=4). Speelbaar via TeknoParrotUi (`[Launch] Mode = tpui`); `minime` is de open vervanging van `amdaemon`. De directe proef met twee exe's is gedaan en **teruggedraaid**: de game startte tot de attract maar **zonder invoer** (die komt alleen via de `TeknoParrotPipe` van TeknoParrotUi). |
| 9 | **Halo** | `drop-in` | **ja (7/7)** | `dropin.ini` met 7 secties: `ravengun-hasp-x64.so` en `libravengun-hasphook-x64.so` (beide `Preload = true`, `Audit = false`), `libravengun-hasphook.so` (sectie **zonder** `Preload`), `dongle.json` (hasp, `haspid 748675309`, features, keycaps, 2 fileids) + `memory/…0100`, `…0101`, `fff4`. `hasp.py --json`: `ok: true` | playable | START | De gesloten `BudgieLoader_x64`. De game **loopt** en zet zijn venster, maar er is nog geen getekend beeld: ElfLdr2 herkent de game niet (`Graphics API: Unknown`) en installeert zijn grafische brug dus niet. De HASP-aanroepen zijn statisch gelinkt; de hook mag daar niet in `LD_AUDIT` (dan sterft de lader). |
| 10 | **HauntedMuseum** | `eigen kern` + `patches.ini` | ja, **onvolledig** | `patches.ini`: 6 secties. 5× `Kind = memory`, `AppliedBy = core`, `Enabled = false`: `Game.exe+0x9E410` en `+0x9E3B0` `jmp-returns-true` `Size = 6`, `+0x9F778` `nop` `Size = 4`, en sinds ronde 39 de twee healthbar-waarden `+0x2AACFC` en `+0x2AACE0` (`replace-bytes`, 4 en 8 bytes), allebei met een gemeten `Find`. Plus de gedeelde lader-patch. verify-all: **0 aan / 5 uit / 1 geweigerd** | parked | START | Zwart beeld na de JVS-functiecontrole (zelfde als GaiaAttack4). |
| 11 | **HauntedMuseumII** | `eigen kern` + `patches.ini` | ja, **onvolledig** | `patches.ini`: 7 secties. 6× `Kind = memory`, `AppliedBy = core`, `Enabled = false`: versie `2100` `0x9D970` `jmp-returns-true` `Size = 6`, `0x1391` `nop` `Size = 1` (crash-NOP) en `0x9FD68` `nop` `Size = 4`; versie `2101J` `0x9B920` `jmp-returns-true` `Size = 6`, `0x1105` `nop` `Size = 6` (firmware-foutmelding) en `0x9DA08` `nop` `Size = 4`. Plus de gedeelde lader-patch. verify-all: **0 aan / 6 uit / 1 geweigerd** | parked | START | Zwart beeld; de profielversies `2100` en `2101J` zijn in het profiel niet te onderscheiden, dus beide sets staan er. |
| 12 | **InfinityBladeFX** | `drop-in` + `eigen kern` | drop-in **ja (3/3)**, kern **nee** | `dropin.ini` met 3 secties: `matrix32.dll` (onze eigen build, `DllOverride = native`), `matrix/matrix.ini` (aantal 2; poort bewust niet gezet), `dongle.json` (matrix, `Init_MatrixAPI`→0, `Dongle_Count`→2, `Dongle_ReadSerNr`→69420). `matrix.py`: *"in orde"* | parked | START | De gesloten kern en de UE3-start: `Kaboom: InitLib() failed`, daarna wit of zwart beeld. Onze DLL is in de proef **niet aangeroepen** (de game stopt eerder), dus de dongle is hier niet de bewezen blokkade. |
| 13 | **JurassicPark** | `drop-in` | **ja (6/6)** | `dropin.ini` met 6 secties: `librt-glfix.so`, `libravengun-hasphook.so` (`Audit = false`), `libravengun-hasphook-x64.so` (sectie zonder `Preload`), `ravengun-hasp.so`, `dongle.json` (hasp, fileid 65522, 112 B, **geconstrueerde** image) + `memory/…1111111111110010`. `hasp.py --json`: `ok: true` | parked | START | De gesloten `BudgieLoader` en de **gepakte ELF**: de GL-aanroepen komen met `stdcall`-opruiming terug, de GOT-sloten hebben vaste dump-adressen en de PLT-stubs `push 0xdeadbeef`; `GL_MAX_TEXTURE_SIZE > 2047` nodig. De donglecontrole wordt nooit bereikt. |
| 14 | **LGI** | `drop-in` + `eigen kern` | drop-in **ja (1/1)**, kern **nee** | `dropin.ini` met 1 sectie: `ravengun-hasp/LGI/dongle.json` (region, `DongleRegion = JAPAN`, `PcbRegion = JAPAN`, **`enforced: false`**). `region.py`: *"de waarde is vastgelegd maar niet afgedwongen: de game valt terug op zijn eigen DefaultRegion en start door"* | playable | START | De gesloten kern — maar de game **speelt al** via de directe start. De regio blokkeert niet. |
| 15 | **LGI3D** | `drop-in` + `eigen kern` | drop-in **ja (1/1)**, kern **nee** | `dongle.json` (region, USA/USA, `enforced: false`) | playable | START | Als LGI. |
| 16 | **LGS** | `drop-in` + `eigen kern` | drop-in **ja (1/1)**, kern **nee** | `dongle.json` (region, USA/USA, `enforced: false`; leest de regio met `amDongleGetRegion` uit `keychip.appboot.region`) | playable | START | De gesloten kern en de JVS-dienst op poort 40100 (die met `TP_REMOTETHREAD=off` verdwijnt). |
| 17 | **LethalEnforcers3** | `eigen kern` (+ een afhankelijkheid) | **nee** | alleen `game.ini` + `README.md`; geen `patches.ini`, geen `dropin/` | parked | START | **Geen patchprobleem:** de game crasht op het openen van een `.m1v`-film omdat Wine geen 32-bits GStreamer (MPEG-1) heeft. Daarnaast de gesloten kern. |
| 18 | **LuigisMansion** | `eigen kern` | **nee** | alleen `game.ini` + `README.md` | playable | START | De gesloten kern en de **gepakte** `VACUUM.exe` (Capcom-packer); de kern blokkeert COM1 (`BLOCKING LUIGI COM PORT`) en neemt de I/O over — dat patroon moet nagebouwd worden. |
| 19 | **MedaruNoGunman** | `eigen kern` (+ een keychip-beschrijving) | **nee** | geen `dropin/`. De README legt uit waarom er géén `dongle.json` is: de game vraagt een **keychip** (Lindbergh/Ring), geen regio-dongle, en er is geen regiowaarde om te antwoorden. `region.py` meldt de beschrijving daarom als **ontbrekend** | parked | START | De gesloten kern (FarCry) en de keychip/SRAM: bij elke verse start `3001 RAM DATA IS BAD` of `3003 ROM HAS CHANGED` (`SBTF_sram.bin` blijft 0 bytes). |
| 20 | **MusicGunGun2** | `eigen kern` + `patches.ini` | ja, **onvolledig** | `patches.ini`: 2 secties. 1× `Kind = memory`, `AppliedBy = core`, `Enabled = false`: `Game.exe+0x137C70` `jmp-returns-true` `Size = 6`, `Category = loader`. verify-all: **0 aan / 1 uit / 1 geweigerd** | parked | START | De game toont zijn **eigen D3D-foutvenster** "Direct3D-initialisatie mislukt" (ook met DXVK) en opent de JVS-pipe niet. |
| 21 | **NERF** | `eigen kern` (de dongle loopt al via een plugin) | n.v.t. | **geen** `dropin/` en **geen** `dongle.json`, met reden: de dongle zit in **managed code** (`UDongleControl` in `Assembly-CSharp.dll`, P/Invoke naar `hasp_linux_23714`), en TeknoParrot levert daarvoor `BepInEx/plugins/NERFTPPlugin.dll`. Een HASP-emulator heeft hier niets te onderscheppen. | playable | START | De gesloten kern en de `Patreon`-activering. De game is speelbaar. |
| 22 | **OG** | `drop-in` + `eigen kern` | drop-in **ja (1/1)**; de beschrijving is met de juiste gamemap goedgekeurd | `dropin.ini` met 1 sectie: `ravengun-hasp/OG/dongle.json` (region, JAPAN/JAPAN, `enforced: false` met `enforced_basis` = *"GEMETEN (ronde 34): de game start en blijft draaien zonder enige regiobron"*). `region.py` geeft `errors: []` — sinds ronde 43 lost het gereedschap de gamemap op zoals de launcher (via de view `views/OG/gs2.exe`), dus zonder `RAVENGUN_GAME_DIR` | **untested** | **START** | De game is nooit echt gespeeld; de regiowaarden komen uit het TeknoParrot-sjabloon, niet uit een meting. Let op: `game.ini` zegt `untested`, maar `STARTTEST.md` meet hem als **START** — die twee zijn niet gelijk. |
| 23 | **OmatsuriYasanGansoTakoyaki** | `eigen kern` | **nee** | alleen `game.ini` + `README.md` | parked | START | Wit venster, de game maakt geen D3D-apparaat; de **kern** doet bij de start een online activeringsaanvraag (`teknoparrot.com/vmp/activation.php`). Een eigen kern laat die weg. |
| 24 | **OmatsuriYasanKingyoSukui** | `eigen kern` | **nee** | alleen `game.ini` + `README.md` | parked | START | Exitcode 8 nadat de game alle schijfletters op `rqrdlist.rqr` en `campaign*.bmp` heeft doorzocht — dat is de **schijfletter-herleiding** van de kern (`CreateFileA` → `.\TeknoParrot\<LETTER>\`), geen patch op de game. |
| 25 | **Otomedius** | `eigen kern` (+ een ACIO-responder) | **nee** | alleen `game.ini` + `README.md` | parked | START | Wit venster; de game zoekt een **ACIO-micom op COM2** (`COM Port 2 Open Error`) en komt niet verder. `libacio.dll`/`HIdGet.exe` zijn I/O, geen dongle. |
| 26 | **PacmanChomp** | `drop-in` | **ja (1/1)** | `dropin.ini` met 1 sectie: `librt-glfix.so` (`Preload = true`; `Audit` blijft hier bewust op zijn standaard `true`, want deze game heeft de audit-route juist nodig) | parked | START | De gesloten `BudgieLoader`: de lader ontleedt de ELF, herkent het spel, zet de grafische brug en de videomodus, en **crasht daarna** (`BudgieLoader.exe.stackdump` staat in de view). Zelfde klasse als Jurassic Park. |
| 27 | **PigsMightFly** | `eigen kern` | **nee** | alleen `game.ini` + `README.md` | playable | START | De gesloten kern en de **packer** op `Shell.exe`: injectie vanaf het startpunt geeft *"Unhandled illegal instruction at address 0044A5F4"*. Draait in een eigen prefix waarin `C:\Sega` naar de game wijst. |
| 28 | **RabbidsHollywood** | `drop-in` | **ja (3/3)** | `dropin.ini` met 3 secties: `matrix64.dll` (`To = Game_Data/Plugins`, `DllOverride = native`), `matrix/matrix.ini` (poort 85, aantal 2), `dongle.json` (matrix, **`enforced: true`**). `matrix.py`: *"in orde"*. In het spel gemeten: `views/RabbidsHollywood/matrix.log` met `Init_MatrixAPI -> 0`, `Dongle_Count -> 2`, `Dongle_ReadSerNr -> 69420`; met de drop-in leefde de game 132–150 s (zonder: ~57 s) | parked | START | De **eigen** Matrix-donglecontrole van de game (`SBK.Matrix.MatrixManager`, `**** DONGLE NOT FOUND` + `UnityEngine.Application.Quit`). Dat is de enige game waar de dongle bewezen **blokkeert**. |
| 29 | **RaccoonRampage** | `eigen kern` | **nee** | alleen `game.ini` + `README.md` | playable | START | De gesloten kern; de game meldt *"guns are not calibrated"* en het vizier stond vast linksboven. Dat is opgelost via het testmenu (kalibratie in de game-opslag). |
| 30 | **RainbowBombergirl** | `eigen kern` (+ een `region`-beschrijving) | **nee** | alleen `game.ini` + `README.md`; geen `patches.ini`, geen `dropin/`, **geen `dongle.json`**. De README zegt dat een `dongle.json` van het soort `region` met de waarde `EXPORT` verwacht wordt, of de vastlegging dat de regio niet afgedwongen wordt zoals bij de Let's Go-games | parked | START | Na munt en start blijft de game bij `SYSTEM ENTRY` / de **PASELI**-melding hangen. `General/Region = EXPORT` is volgens `region.py` geen dongle maar een **kerninstelling** (de gesloten kern handelt hem af); of die regio-check de game tegenhoudt is **niet** gemeten. De parkeerreden is het aanraakscherm, niet de regio. |
| 31 | **Rampage** | `drop-in` | **ja (3/3)** | `dropin.ini` met 3 secties: `matrix64.dll`, `matrix/matrix.ini` (poort 85, aantal 2), `dongle.json` (matrix, **`enforced: false`**). `matrix.py`: *"in orde"*. Gemeten: alleen `Init_MatrixAPI` wordt aangeroepen; `Dongle_Count`/`Dongle_ReadSerNr` **niet** — precies wat de IL voorspelt | playable | START | **Geen blokkade:** de game start en speelt (joystick-/aanvalsspel, geen lightgun-game). De filmpjes vragen `gst-libav` (AAC). |
| 32 | **TC5** | `drop-in` | ja (4/4), maar **onvolledig** | `dropin.ini` met 4 secties: `hasp_windows_x64_105293.dll`, `hasp_windows_x64_106993.dll`, `hasp_windows_x64.dll` (alle drie `DllOverride = native`, elk met `Env = RAVENGUN_HASP_DIR=ravengun-hasp;RAVENGUN_HASP_GAME=TC5`) en `dongle.json` (hasp, **`memory` is leeg**). `hasp.py --json`: **`ok: false`** — *"geen 'memory'-items: de dongle-image van deze game is nog niet vastgelegd"*. Bewezen dat onze DLL **in de game** komt (`grep -i hasp /proc/<pid>/maps`); de view `TC5/Binaries/Win64` moest daarvoor gematerialiseerd worden — het enige van de 15 drop-in-games waar dat speelt | playable | START | De **runtime-lader** (`[Loader] Build = teknoparrot`) omdat `bngrw.dll` en `iDmacDrv64.dll` nog niet in onze eigen OpenParrot-build zitten, plus de gesloten kern. De dongle-image ontbreekt; of de game de dongle echt uitleest is niet gemeten. |
| 33 | **TankTankTank** | `geen` (niet vast te stellen) | n.v.t. | alleen `game.ini` + `README.md`; geen `patches.ini`, geen `dropin/`, geen `dongle.json`. De README noemt het profiel een **leenprofiel** (`EmulationProfile = DeadHeat` is een raceprofiel) en zegt dat de indeling niet gemeten is | untested | niet getest | Het spel staat **niet op de NAS**: `--plan` rc=4. Geen bestandsevidence. |
| 34 | **Terminator** | `drop-in` | ja (3/3), maar **inert** | `dropin.ini` met 3 secties: `ravengun-hasp.so` (sectie **zonder** `Preload`), `dongle.json` (hasp, fileid 65524 **aanname**, 128 B, byte-voor-byte de echte `hhl_mem.dmp` uit de spelmap) + `memory/…1111111111110100`. `hasp.py --json`: `ok: true`. **Geen inline-hook**: `game` heeft 0 `hasp_`-symbolen in zijn dynsym | playable | START | De gesloten `BudgieLoader` en de dongle: native stopt de game met `DongleInitialize() FAILED!` (exitcode 255). De drop-in wordt wel uitgerold maar **spreekt de emulator niet aan**, want er valt niets aan te haken. |
| 35 | **TombRaider** | `drop-in` | **ja (3/3)** | `dropin.ini` met 3 secties: `matrix64.dll`, `matrix/matrix.ini` (poort 85), `dongle.json` (matrix, **`enforced: false`**). `matrix.py`: *"in orde"*. Gemeten: `matrix.log` toont de volledige controle (`Init_MatrixAPI`→0, `Dongle_Count`→2, `Dongle_ReadSerNr`→69420) | playable | START | **Geen blokkade meer:** lightgun, herladen, munt, start en trillen werken. De oorspronkelijke blokkade was het USB-I/O-bord, niet de dongle. |
| 36 | **TransformersShadowsRising** | `eigen kern` (+ een `sega-scan`-donglelaag) | **nee** | alleen `game.ini` + `README.md`; geen `patches.ini`, geen `dropin/`, **geen `dongle.json`** (`STAND.md` telt die als gat: `sega-scan` verwacht). De README noemt ook de **eigen `Shell.exe`-drop-in** als ontbrekend | playable | START | De gesloten kern (alleen in `Shell.exe`, niet in `Transformers2.exe`). De donglescan (`Sega/shell/dk2win64.dll`, Dinkey Pro) faalt in fase 1 (`Dongle - 261011.log`: `MainThreadDongleScan: failed stage 1`) maar de game **speelt door**. Voor dit soort bestaat nog geen `kind` in `docs/dropins.md` (open vraag 9). |

---

## Tellingen

| | aantal |
|---|---|
| games in `patches/` | **36** |
| met `patches.ini` | **4** (GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2) |
| ... memory-patches daarin | **14** (2 + 5 + 6 + 1), allemaal geaccepteerd door de motor, **0 geweigerd** |
| ... waarvan compleet t.o.v. de kernbron | **0** — de API-hooks, de functie-hooks en de D3D9-compatibiliteit ontbreken nog; die passen niet in het formaat van de motor |
| ... waarvan met een `Find` (versiecontrole) | **10** van de 14 — de tien waarvan de bytes op onze dump gemeten zijn; zie [De Find is gemeten](#de-find-is-gemeten) |
| met `dropin/` | **15** (15 `dropin.ini`-bestanden; `STAND.md` zegt "43 secties in de **14** `dropin.ini`-bestanden" — het vijftiende is dat van GhostBusters en heeft 0 secties) |
| ... waarvan elk bestand een sectie heeft | **15** (43 bestanden, 43 secties) |
| ... waarvan de drop-in leeg is | **1** (GhostBusters) |
| drop-ins die niet aankomen (`verify-all.py`) | **0** |
| met `dongle.json` | **13** |
| dongle verwacht maar nog geen `dongle.json` (telling van `STAND.md`) | **4** (GhostBusters hasp, HOTDSD amdaemon, NERF hasp, TransformersShadowsRising sega-scan) |
| ... en twee games met een gedocumenteerd gat dat die telling níet meet | **2** (RainbowBombergirl `region`, MedaruNoGunman keychip) |
| dongle aanwezig maar onvolledig | **1** (TC5: geen geheugenimage) |
| dongle-beschrijving die een gereedschap niet kon controleren | **0** (OG was de laatste; sinds ronde 43 lost `region.py` de gamemap op zoals de launcher en is dat oordeel groen) |
| met `[Status] Status` | **36** |
| met `README.md` | **36** |
| direct te starten (`--plan` rc=0) | **33** |
| START in `STARTTEST.md` | **33** |
| patch nodig: `geen` | **3** (3DCosplayMahjong, HOTDSD, TankTankTank) |
| patch nodig: `drop-in` | **16** (14 compleet, 2 ontbreken: GhostBusters, GoGoStrike) |
| patch nodig: `patches.ini` | **4** (alle vier onvolledig) |
| patch nodig: `eigen kern` | **22** games met de gesloten kern; **4** games hebben onze eigen kern al |

**De soorten overlappen** en tellen dus niet op tot 36. De 22 games met een gesloten kern zijn
`kern nog TeknoParrot` uit `verify-all.py`; **9** daarvan hebben óók een drop-in nodig
(InfinityBladeFX, LGI, LGI3D, LGS, OG, RabbidsHollywood, Rampage, TC5, TombRaider). De vier games met
onze eigen kern (GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2) staan niet in die 22. De
partitie die wél sluit is: **3** zonder patch + **16** met een drop-in + **4** met een eigen kern +
**13** met alleen een gesloten kern = **36**.

---

## Wat de zonder-lader-meting aan de kolom *eigen kern* verandert

`patches/ZONDER-LADER.md` (ronde 34) meet iets wat deze kaart nog niet wist: elke game is gestart met
**alleen `wine <exe>`** — geen lader en geen kern. Van de **22 games met de gesloten kern blijven er
15 draaien**:

> InfinityBladeFX, LGI, LGI3D, LGS, NERF, OG, OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui,
> RabbidsHollywood, RaccoonRampage, RainbowBombergirl, Rampage, TC5, TombRaider,
> TransformersShadowsRising

Voor die vijftien is de gesloten `TeknoParrot(64).dll` dus **niet nodig om te blijven draaien**. Wat
er in de gewone start verder bij komt is besturing, het JVS-bord, de dongle en de regio — en dat is
precies wat onze eigen lader en de drop-ins al leveren.

**Zeven vielen uit:** Gashaaaan, Gashaaaan2, LethalEnforcers3, Luigi's Mansion, MedaruNoGunman,
Otomedius en Pigs Might Fly. Voor die zeven is er wél iets structureels aan de hand. Twee
voorbehouden die `ZONDER-LADER.md` zelf noemt: het venster is 20 seconden, en bij een **gepakte** exe
kan de meting het spoor verliezen (Luigi's Mansion en Pigs Might Fly zijn gepakt; Pigs Might Fly gaf
in de ene run START en in de andere viel hij uit). De drie games die die proef niet kon plannen
(3DCosplayMahjong, HOTDSD, TankTankTank) staan er helemaal niet in.

**Sinds ronde 41 is herkennen geen voorwaarde meer om te draaien.** Onze eigen kern weigert een
onbekende executable niet meer (OpenParrot-patch `0003-unknown-game-generic-route.patch`); hij blijft
dan op `GameID::Global` / `X2Type::None` en doet niets behalve bestaan. Gemeten: **LGI** (een game die
de kern niet herkent) start en blijft draaien op onze eigen lader en kern, met de JVS-pipe actief.
Wat er in die generieke route ontbreekt zijn de **per-game hooks** -- de invoervertaling van de
gesloten kern -- dus of zo'n game ook speelbaar is, is niet gemeten. Zie
`patches/EIGEN-KERN.md`, "Sinds ronde 41 weigert onze kern een onbekende game niet meer".

**Wat dat betekent voor deze kaart.** De kolom *Patch nodig: `eigen kern`* zegt dat de gesloten kern
**in de startketen zit**, niet dat die kern onmisbaar is. Voor de vijftien hierboven hoeft er dus geen
`EmulationProfile` met per-game hooks nagebouwd te worden om ze te laten *draaien*; het werk zit daar
in besturing en I/O. Voor de zeven die uitvielen blijft de vraag open. **En dit is niet gemeten met
onze eigen lader zónder kern** — dat kan de directe start niet — dus dat het zonder kern zou kunnen
is een gevolgtrekking, geen meting.

---

## Per game: de onderbouwing

### A. De vier games met een eigen kern en een `patches.ini`

Deze vier games zijn de enige met `Type = OpenParrot`: lader **en** kern zijn van ons
(`~/.local/share/ravengun/openparrot/current/`). Hun `patches.ini` is **documentatie van wat de
OpenParrot-kern zelf doet**: elke **memory-patch** heeft `AppliedBy = core` en `Enabled = false`, dus
de motor past hem niet toe — de kern doet het bij het draaien. (De enige patch die aan staat is de
**gedeelde** lader-patch; zie hieronder.)

**Het bewijs dat de motor niets toepast en waarom.** `python3 tools/verify-all.py <Game>` geeft voor
alle vier `0 aan / n uit / 1 geweigerd`. Die **ene geweigerde** is de **gedeelde** patch
`OpenParrotLoader zonder CONTEXT_FLOATING_POINT` uit `patches/_defaults/OpenParrot.patches.ini`
(`Enabled = true`). De motor weigert hem met:

```
patch 'OpenParrotLoader zonder CONTEXT_FLOATING_POINT': Find komt 0 keer voor, Count zegt 1 (verkeerde spelversie?)
```

Dat is **geen fout en geen probleem**: onze eigen lader heeft die fix **in de bron**
(`third_party/openparrot/patches/0001-loader-no-context-floating-point.patch`), dus de bytes
`C7 05 C0 9B 42 00 1F 00 01 00` staan er niet meer in. De `Note` in `_defaults/OpenParrot.patches.ini`
zegt hetzelfde, en de launcher zet de patch voor onze eigen build uit (`own_build()` in
`scripts/launch-direct.py`), waardoor `--plan` gewoon rc=0 geeft. `verify-all.py` roept de motor
**buiten** de launcher om aan en ziet die uitzondering dus niet.

**Zijn die patches compleet? Nee.** Vergeleken met de bron van de kern
(`TypeX2Generic.cpp`, `Games/TypeX2/`) staat in de vier `patches.ini`-bestanden **alleen** het
`MakeJMP`/`MakeNOP`-deel: samen **12 memory-patches**. Wat er per game nog ontbreekt staat in de
sectie [Ontbrekende patches](#ontbrekende-patches-die-er-wel-zouden-moeten-zijn) hieronder, met
regelnummers. In het kort: de **API-hooks** (`GetKeyboardState`, `CreateWindowExA`), de
**functie-hooks** van MusicGunGun2 en de D3D9-compatibiliteitsfunctie van MusicGunGun2. De twee
`WriteMemory`-waarden van HauntedMuseum stonden hier ook, en die zijn in ronde 39 **toegevoegd** --
dat kon wel, want het zijn vaste bytes op een vaste plek.

**De patches zijn wél allemaal toepasbaar.** Zet je de memory-patches in het geheugen aan
(`Enabled = true`) en laat je de motor ze overnemen (`--take-over-core`), dan accepteert hij ze
allemaal — **0 geweigerd** — en rekent hij elk adres goed om naar de bestandsplaats:

```
GAME GaiaAttack4
  [gecontroleerd] Gun-schrijfacties uit (0x113E80)   op game.exe+0x113e80 (bestand 0x113280, .text)  [51833d18b8f3 -> b801000000c3]
  [gecontroleerd] Volumeschrijfactie uit (0x1156C8)  op game.exe+0x1156c8 (bestand 0x114ac8, .text)  [66895120 -> 90909090]
  summary: toegepast 2, overgeslagen 1, geweigerd 0

GAME HauntedMuseumII
  [gecontroleerd] Gun-schrijfacties uit, versie 2100 (0x9D970)   op game.exe+0x9d970 (bestand 0x9cd70, .text)  [83ec34a1fce2 -> b801000000c3]
  [gecontroleerd] Crash-NOP, versie 2100 (0x1391)                op game.exe+0x1391 (bestand 0x791, .text); geen Find: die byte is hier al 0x90
  [gecontroleerd] Volumeschrijfactie uit, versie 2100 (0x9FD68)  op game.exe+0x9fd68 (bestand 0x9f168, .text)  [66895124 -> 90909090]
  [gecontroleerd] Gun-schrijfacties uit, versie 2101J (0x9B920)  op game.exe+0x9b920 (bestand 0x9ad20, .text); geen Find: andere build
  [gecontroleerd] Firmware-foutmelding uit, versie 2101J (0x1105) op game.exe+0x1105 (bestand 0x505, .text); geen Find: andere build
  [gecontroleerd] Volumeschrijfactie uit, versie 2101J (0x9DA08) op game.exe+0x9da08 (bestand 0x9ce08, .text); geen Find: andere build
  summary: toegepast 6, overgeslagen 1, geweigerd 0
```

Dat is in deze ronde rechtgezet: **zes `jmp-returns-true`-patches misten `Size`** (de motor weigert
die met *"Action = jmp-returns-true vraagt Size"*) en **twee `MakeNOP`-patches van HauntedMuseumII
ontbraken** (`0x1391` en `0x1105`). Beide zijn nu toegevoegd. Zie
[Wat er in deze ronde is veranderd](#wat-er-in-deze-ronde-is-veranderd).

**De `Find` is nu gemeten.** In ronde 33 zijn de originele bytes op alle twaalf de adressen uit de
echte exe's gelezen (module-offset naar bestandsplaats via `addrmap`, sectie `.text`) en staan ze in de
acht patches die bij onze dump horen; de motor meldt ze daarom als `gecontroleerd` in plaats van
*"geen Find, dus niet te controleren"*. Zie [De Find is gemeten](#de-find-is-gemeten). Bij de vier
zonder `Find` zou hij niets zeggen: de 2101J-set hoort bij een **andere build** en de crash-NOP van
`0x1391` staat in onze dump al op `0x90`.

### B. Games met een drop-in

**Compleet (14 games, 43 bestanden = 43 secties).** GalagaAssault (6), Halo (7), InfinityBladeFX (3),
JurassicPark (6), LGI (1), LGI3D (1), LGS (1), OG (1), PacmanChomp (1), RabbidsHollywood (3),
Rampage (3), TC5 (4), Terminator (3), TombRaider (3). `verify-all.py` meldt voor alle 15 drop-in-mappen
samen **0 drop-ins die niet aankomen**, en `patches/STAND.md` meldt **0 bestanden zonder sectie**. De
dest van elk bestand is met `launch-direct.py <Game> --plan` nagerekend; de fout die er eerder in zat
(het pad stond dubbel in `To` én in de sectienaam) is er niet meer.

**Drie kwalificaties die je bij de "ja" moet lezen:**

1. **Terminator is inert.** De bestanden komen aan, maar er is geen inline-hook en `game` heeft
   **0 `hasp_`-symbolen** in zijn dynsym: er is geen statisch gelinkte HASP-client om aan te haken.
   De drop-in is een vastgelegde donglebeschrijving, geen werkende emulatie.
2. **TC5 is onvolledig.** De drie DLL's komen aan — en sinds de reparatie van deze ronde is dat ook
   **bewezen**: `grep -i hasp /proc/<pid>/maps` toont `hasp_windows_x64_105293.dll` in het
   **spelproces** (niet in de lader). Twee dingen waren daarvoor nodig: `Env = RAVENGUN_HASP_DIR=…;
   RAVENGUN_HASP_GAME=TC5` bij elk van de drie DLL-secties (zonder die wist de DLL niet wélke dongle
   hij moest laden) en een **gematerialiseerde view** (`TC5/Binaries/Win64` was een symlink naar de
   NAS; de drop-in-motor weigert dan te schrijven). Maar `dongle.json` heeft nog steeds
   `"memory": []` en `hasp.py` meldt de dongle als onvolledig. Of de game de dongle ook echt
   *uitleest* is niet gemeten (de aanroepen komen niet in het logboek). Tot dat vastligt, emuleert de
   gesloten kern de dongle van TC5 — dat is het laatste stuk dat daar nog aan TeknoParrot hangt.
3. **OG's beschrijving klopt, en sinds ronde 43 vindt het gereedschap de exe ook.** `region.py`
   probeert nu eerst dezelfde resolver als de launcher (`ownprofile.locate_game`) en vindt OG via de
   view `views/OG/gs2.exe`; het oordeel is `ok: true` met `errors: []` en de merktekens `amDongleOpen`
   2x, `amDongleSetupKeychip`, `amDongleSendAndRecvEx`, `amDongle Ver.`, `keychip.appboot.region` en
   `REGION` 5x. De waarden zelf komen nog steeds uit het TeknoParrot-sjabloon en
   `findings/11_hardware_patches_dongles.md`; `enforced` is in ronde 34 gemeten (de game start en
   blijft draaien zonder enige regiobron).

**Ontbrekend (2 games):**

* **GhostBusters** — `dropin/dropin.ini` heeft 0 secties en 0 bestanden. Dat is met opzet voor de
  `pm`-map (die is de dump van de eigenaar, 2,7 GB, en hoort niet in deze repo), maar er staat ook
  **niets** klaar voor het geval de dongle toch nodig blijkt. De `dropin.ini` beschrijft in
  commentaar de vorm die zo'n bestand moet krijgen (`[pm/ravengun-voorbeeld.so]`, `To = .`,
  `Preload = true`).
* **GoGoStrike** — geen `dropin/` en de README zegt expliciet dat er geen is. Wat deze game nodig
  heeft is de eigen `LD_PRELOAD`-kastnabootser voor de 64-bits ElfLdr2-familie; die bestaat nog niet
  (werksoort 3, centraal).

### C. Games waar geen patch nodig is

* **3DCosplayMahjong** en **TankTankTank** — het spel staat niet op de NAS, dus er valt niets te
  patchen en niets te meten. `--plan` stopt met rc=4. Alles wat over deze twee te zeggen is, is
  "onbekend"; hun `[Status]` staat sinds deze ronde op **`untested`** met die reden erbij, en hun
  README zegt hetzelfde. Er is ook **geen bestandsevidence** over een dongle: alleen het **profiel**
  noemt er geen, en de bestandsscan die bij de andere games de dongle aanwijst heeft deze twee nooit
  gehad.
* **HOTDSD** — speelbaar via TeknoParrotUi; de blokkade is de **startketen met twee exe's**, niet een
  patch. `amdaemon.exe` moet eerst (5 s wachten, `DAEMON_WAIT_SECONDS` uit `StartApp.bat`) en
  `launch-direct.py` start geen profielen met twee exe's. De directe proef is gedaan en
  teruggedraaid: de game startte tot de attract maar **zonder invoer**.

### D. Games waar de eigen kern de patch is

Deze games hebben **geen** `patches.ini` nodig en er is ook geen patch voor bekend; wat er in de weg
staat is de gesloten kern (en soms iets anders). Per game:

* **Gashaaaan / Gashaaaan2** — speelbaar; zonder enige `TP_*`-injectievariabele neemt de lader zijn
  standaardroute en start de game. Gashaaaan2 heeft daarnaast de `Patreon`-activering.
* **LethalEnforcers3** — geen patchprobleem: 32-bits GStreamer/MPEG-1 ontbreekt.
* **LuigisMansion** — speelbaar; de exe is gepakt en de kern blokkeert COM1.
* **MedaruNoGunman** — de keychip/SRAM: `3001 RAM DATA IS BAD` bij elke verse start.
* **NERF** — speelbaar; de dongle zit in managed code en TeknoParrot levert daar een BepInEx-plugin
  voor. Er is hier dus **niets** voor een HASP-drop-in.
* **OmatsuriYasanGansoTakoyaki / OmatsuriYasanKingyoSukui** — respectievelijk een wit venster met een
  **kern**-activeringsaanvraag, en exitcode 8 na het doorzoeken van alle schijfletters (de
  schijfletter-herleiding van de kern).
* **Otomedius** — een ACIO-micom op COM2; vraagt een responder op een pty, geen patch.
* **PigsMightFly** — speelbaar; de exe is gepakt (injectie vanaf het startpunt faalt).
* **RaccoonRampage** — speelbaar na kalibratie in het testmenu.
* **RainbowBombergirl** — `SYSTEM ENTRY`/PASELI; `Region = EXPORT` is een kerninstelling.
* **TransformersShadowsRising** — speelbaar; de Dinkey-donglescan faalt in fase 1 maar blokkeert niet.
  Voor dit soort bestaat nog geen `kind` in `docs/dropins.md`.

---

## De Find is gemeten

*Ronde 33.* De motor kon tot ronde 32 niet controleren of je de juiste spelversie voor je had: geen
enkele memory-patch had een `Find`, dus hij meldde *"geen Find, dus niet te controleren"*. Dat is nu
voor **acht van de twaalf** opgelost. De bytes komen niet uit een bron maar uit de exe's zelf:

```bash
cd ~/projects/ravengun
# 1. welk adres hoort bij welke plek in het bestand, en welke bytes staan daar?
python3 - <<'PY'
import sys; sys.path.insert(0, "scripts")
import addrmap
m = addrmap.open_module("/home/deck/mnt/spidernas_emulation/Teknoparrot/Haunted Museum II/game.exe")
print(hex(m.offset_for(0x9FD68)), m.region_of(0x9FD68))
PY
# 2. en de motor print dezelfde bytes zelf bij het overnemen van de kern
python3 scripts/patchfile.py --game HauntedMuseumII --game-exe <exe> --take-over-core
```

De motor print bij elke gecontroleerde patch `[<huidige bytes> -> <vervanging>]`; die "huidige bytes"
zijn de `Find`. De regel is: **de `Find` is precies zo lang als de patch schrijft** (`Size`), dus bij
`jmp-returns-true` zes bytes en bij `nop` evenveel als `Size`.

| Game | Patch | `Find` |
|---|---|---|
| GaiaAttack4 | Gun-schrijfacties uit (0x113E80) | `51 83 3D 18 B8 F3` |
| GaiaAttack4 | Volumeschrijfactie uit (0x1156C8) | `66 89 51 20` |
| HauntedMuseum | Gun-schrijfacties uit (0x9E410) | `56 68 14 B4 D8 00` |
| HauntedMuseum | Gun-schrijfacties uit (0x9E3B0) | `83 EC 18 A1 54 30` |
| HauntedMuseum | Volumeschrijfactie uit (0x9F778) | `66 89 51 20` |
| HauntedMuseumII | Gun-schrijfacties uit, 2100 (0x9D970) | `83 EC 34 A1 FC E2` |
| HauntedMuseumII | Volumeschrijfactie uit, 2100 (0x9FD68) | `66 89 51 24` |
| MusicGunGun2 | JMP naar ReturnsTrue (0x137C70) | `55 8B EC 81 EC 78` |

**Waarom de andere vier geen `Find` hebben.** Dat is geen vergetelheid:

* **De 2101J-set van HauntedMuseumII** (`0x9B920`, `0x1105`, `0x9DA08`) hoort bij de **JP 1.01**-build.
  Onze dump is de **USA 1.00**-build: de CRC die de kern zelf over de eerste `0x400` bytes rekent is
  `0x5a7fa241` en dat is in `GameDetect.cpp` `GameID::HauntedMuseum2100`, niet `0x42d54499`
  (`GameID::HauntedMuseum2101J`). De kern kiest op grond daarvan de 2100-tak en past de 2101J-patches
  nooit toe. Een `Find` zou daar alleen zeggen wat er toevallig op dat adres staat, niet dat het adres
  goed is — en bij `0x9B920` staat `CC` (`int3`), wat juist verdacht is.
* **De crash-NOP van 0x1391** (2100): de byte daar is in onze dump **al `0x90`**, dus de patch doet op
  deze build niets en een `Find = 90` zou triviaal waar zijn.

De meting achter de versieset staat in `patches/EIGEN-KERN.md` (gegenereerd met
`patches/kern-herkenning.py`), samen met de volledige tabel van welke games onze eigen kern herkent.

---

## Ontbrekende patches die er wel zouden moeten zijn

Dit is de kern van deze kaart. Alles hieronder is **gemeten**, met de bron erbij.

### 1. De vier games met een eigen kern: `patches.ini` is incompleet

Bron: `TypeX2Generic.cpp` van de OpenParrot-bron waarmee onze eigen kern gebouwd is (commit
`fc8ba632…`). Regelnummers zijn die van dat bestand.

| Game | Wat de kern doet en in `patches.ini` **niet** staat | Regel | In `patches.ini` te zetten? |
|---|---|---|---|
| **GaiaAttack4** | `MH_CreateHookApi(user32!GetKeyboardState, …)` — *Disable Native Keyboard* | 2236 | **nee** — een API-hook, niet in de woordenschat |
| | `MH_CreateHookApi(user32!CreateWindowExA, …)` (alleen als **niet** windowed) | 2235 | **nee** — API-hook |
| | `CreateDirectoryA(".\\OpenParrot\\GA4")` + volume uit `.\OpenParrot\Settings.ini` | 2218, 2231 | nee — kernwerk |
| **HauntedMuseum** | `GetKeyboardState`-hook | 2137 | nee |
| | `CreateWindowExA`-hook (niet-windowed) | 2136 | nee |
| | `WriteMemory<float>(0x2AACFC, ResHeight − 61.0)` — 2D-healthbars op de juiste hoogte | 2147 | **toegevoegd in ronde 39** als `Healthbar-hoogte (0x2AACFC)`: `replace-bytes`, `Size = 4`, `Find = 00 C0 30 44` (gemeten: de exe heeft er **707.0** = 768 − 61) en `Replace = 00 C0 24 44` (659.0 = 720 − 61, onze 1280×720). Een vaste patch legt die resolutie vast; de `Note` zegt dat |
| | `WriteMemory<double>(0x2AACE0, ResHeight − 61.0)` | 2148 | **toegevoegd in ronde 39** als `Healthbar-hoogte, dubbel (0x2AACE0)`: `Size = 8`, `Find = 00 00 00 00 00 18 86 40` (707.0) en `Replace = 00 00 00 00 00 98 84 40` (659.0) |
| | `CreateDirectoryA(".\\OpenParrot\\HM")`, volume uit `Settings.ini` | 2119, 2132 | nee |
| **HauntedMuseumII** | voor **2100**: `GetKeyboardState`- en `CreateWindowExA`-hook | 2172-2173 | nee |
| | ~~voor **2100**: `MakeNOP(0x1391, 1)`~~ — **in deze ronde toegevoegd** als `Crash-NOP, versie 2100` | 2178 | **gedaan** |
| | voor **2101J**: `GetKeyboardState`- en `CreateWindowExA`-hook | 2204-2205 | nee |
| | ~~voor **2101J**: `MakeNOP(0x1105, 6)`~~ — **in deze ronde toegevoegd** als `Firmware-foutmelding uit, versie 2101J` | 2210 | **gedaan** |
| | `CreateDirectoryA(".\\OpenParrot\\HM2")`, volume uit `Settings.ini` | 2155, 2168 | nee |
| **MusicGunGun2** | `MH_CreateHook(0x1ADA90, MusicGunGun2VolumeSetup)` — volume | 2105 | nee — functie-hook |
| | `MH_CreateHook(0x1143B0, MusicGunGun2strncpy)` — string/naam-fix | 2106 | nee |
| | `MH_CreateHook(0x12E70, MusicGunGun2MultiHeadFixHook)` — **multi-head/multi-monitor** | 2107 | nee |
| | `MH_CreateHookApi(user32!CreateWindowExA, …)` (niet-windowed) | 2109 | nee |
| | `InstallMusicGunGun2AndroidD3D9Compatibility()` | 2091 | **nee** — hij doet niets zonder `ANDROID_ALSA_SERVER` (Android x86-brug) en zou een `MakeCALL` naar een functie in de kern zelf zijn; zie open punt 7 |
| | `CreateDirectoryA` + volume uit `.\OpenParrot\Settings.ini` | 2102 | nee |

**De twee die er het duidelijkst in hoorden** waren de `MakeNOP`-patches van HauntedMuseumII
(`game.exe+0x1391`, `Size = 1` en `game.exe+0x1105`, `Size = 6`). Die zijn **in deze ronde
toegevoegd** — ze passen letterlijk in het formaat dat de vier games al gebruiken, en de tweede is
functioneel: hij onderdrukt de **firmware-foutmelding** die juist ontstaat doordat het I/O-bord
ontbreekt.

**En wat er in géén geval in `patches.ini` kan:** de API-hooks (`GetKeyboardState`,
`CreateWindowExA`), de functie-hooks van MusicGunGun2 en de D3D9-compatibiliteitsfunctie. De
woordenschat van de motor is `nop`, `jmp-returns-true`, `replace-bytes` en `write-bytes`; een hook op
een API of een gamefunctie valt daarbuiten. Dat hoort bij de kern (of bij de runtime-route met een
hook-DLL). **Eerlijk gezegd:** van de vier games is dus een deel van de kernaanpassingen principieel
niet in deze repo te vangen — en dat deel is nu precies wat er nog ontbreekt.

### 2. ~~De zes `jmp-returns-true`-patches missen `Size`~~ — opgelost in deze ronde

Toen ik begon, misten **alle zes** de `jmp-returns-true`-patches een `Size`, en de motor weigerde ze
daardoor zodra ze aan stonden:

```
[geweigerd] Gun-schrijfacties uit (0x113E80)   patch '…': Action = jmp-returns-true vraagt Size
```

Dat waren: GaiaAttack4 `game.exe+0x113E80`, HauntedMuseum `Game.exe+0x9E410` en `+0x9E3B0`,
HauntedMuseumII `game.exe+0x9D970` (2100) en `+0x9B920` (2101J), MusicGunGun2 `Game.exe+0x137C70`.
**In deze ronde zijn ze alle zes van `Size = 6` voorzien** (de motor zet op die plaats
`b8 01 00 00 00 c3` — `mov eax,1 ; ret`, 6 bytes, `RETURNS_TRUE` in `scripts/patchfile.py` regel 66 —
en vult de rest met `0x90`). De hermeting geeft **0 geweigerd** op alle vier de games.

**De `Find` is nu gemeten** (ronde 33): acht van de twaalf hebben er een, en die bytes komen van de
echte exe's. Zie [De Find is gemeten](#de-find-is-gemeten) voor de tabel en de methode. Wat er nog
steeds niet is, is een `Find` voor de vier die niet bij onze dump horen — en daar zou hij ook niets
zeggen.

### 3. Games waar een `Kind = memory`-patch een controle zou kunnen overslaan, maar er geen is

De opdracht vraagt hier expliciet naar. De eerlijke stand:

| Game | Welke controle blokkeert | Is het adres bekend? | Wat er dan in de patch zou moeten staan |
|---|---|---|---|
| **Terminator** | De Aladdin/HASP-dongle: native `DongleInitialize() FAILED!`, exitcode 255. De drop-in is inert want `game` heeft 0 `hasp_`-symbolen | **nee** — geen bron noemt de functie of het adres | Een `jmp-returns-true` op de dongle-initfunctie van `game` (of een `nop` op de tak die naar de fout leidt). **Zonder gemeten adres is dit niet te schrijven.** |
| **GhostBusters** | De ELF bevat `DongleInitialize() FAILED!`, `Game dongle initialization failed!`, `ReInitAladdinDongles`, `hasp_login`, `nethasp.ini`; na de `pm`-fix komt de game niet in beeld en is niet gemeten wat er dan blokkeert | **nee** | Idem. Bovendien is niet eens zeker dat de dongle de blokkade is (het kan ook de OpenGL-context zijn). |
| **MedaruNoGunman** | De keychip/SRAM: `3001 RAM DATA IS BAD` / `3003 ROM HAS CHANGED` (`SBTF_sram.bin` blijft 0 bytes) | **nee** | Een patch die de SRAM-controle overslaat, of de kern die de juiste SRAM vult. De README noemt de **keychip** als soort die nog geen `kind` heeft. |
| **Otomedius** | `COM Port 2 Open Error` (ACIO-micom) | **nee** | Een patch die de COM2-open-fout negeert, of (beter) een ACIO-responder op een pty. |
| **RainbowBombergirl** | `SYSTEM ENTRY` / de PASELI-melding | **nee** | Een patch op de betalings-/netwerkcontrole. `region.py` zegt dat `Region = EXPORT` een **kerninstelling** is, geen dongle. |
| **OmatsuriYasanGansoTakoyaki** | Wit venster + een **online activeringsaanvraag van de kern** (`teknoparrot.com/vmp/activation.php`) | n.v.t. | **Geen patch nodig:** die aanvraag zit in **TeknoParrot's** binary. Een eigen kern laat hem gewoon weg (`docs/research/loskoppelen-teknoparrot.md`). |
| **OmatsuriYasanKingyoSukui** | Exitcode 8 na het doorzoeken van alle schijfletters op `rqrdlist.rqr` en `campaign*.bmp` | n.v.t. | **Geen patch:** dit is de **schijfletter-herleiding** van de kern (`CreateFileA` → `.\TeknoParrot\<LETTER>\`), een gedeeld stuk in de kern. |

**Conclusie:** er zijn games waar een `Kind = memory`-patch denkbaar is (Terminator, GhostBusters,
MedaruNoGunman, Otomedius, RainbowBombergirl), maar **voor geen daarvan is het adres in enige bron
gemeten**. Wat er in zo'n patch moet staan is dus niet te zeggen zonder eerst de executable te
analyseren. En er is een **beleidstegenstelling** die dit blokkeert (zie hieronder).

### 4. Beschrijvingen die ontbreken of niet kloppen

**Ontbrekende `dongle.json` (4, volgens `STAND.md`):** GhostBusters (hasp), HOTDSD (amdaemon), NERF
(hasp), TransformersShadowsRising (sega-scan). **En twee die die telling niet meeneemt maar die de
README van de game zelf als gat noemt:** RainbowBombergirl (`region`, waarde `EXPORT`) en
MedaruNoGunman (keychip). Dat is een verschil van inzicht tussen `patches/STAND.md` (dat
`EXPECTED_DONGLE` in `stand-gen.py` gebruikt) en de README's; wie de kaart vernieuwt, moet kiezen
welke van de twee leidend is.

* **GhostBusters** — `hasp` verwacht (de ELF noemt HASP/Aladdin), maar **welk bestand** ontbreekt is
  niet gemeten. Er staat ook geen enkel bestand klaar in `dropin/`.
* **HOTDSD** — `amdaemon` verwacht; wat er nodig is, is óf een `dongle.json` van dat soort óf de
  expliciete vastlegging dat deze game geen dongle heeft en alleen de **startketen** nodig heeft.
* **NERF** — `hasp` verwacht volgens `STAND.md`, maar de README legt uit dat een HASP-drop-in hier
  **niets te doen heeft**: de dongle zit in managed code en TeknoParrot levert daar een BepInEx-plugin
  voor. Hier is de goede uitkomst dus waarschijnlijk "geen `dongle.json`", niet "vul hem in".
* **TransformersShadowsRising** — `sega-scan` verwacht, en dat `kind` bestaat nog niet in
  `docs/dropins.md`. Eerst de beslissing (open vraag 9), dan het bestand. De README noemt ook de
  **eigen `Shell.exe`-drop-in** als ontbrekend.
* **RainbowBombergirl** — `region` met `EXPORT`; de README wil óf dat bestand, óf de vastlegging dat
  de regio niet afgedwongen wordt (zoals bij LGI/LGI3D/LGS, waar `enforced: false` **gemeten** is).
  Voor deze game is dat niet gemeten.
* **MedaruNoGunman** — `region.py` meldt: *"vraagt een regio-dongle, maar de beschrijving in zijn
  drop-in ontbreekt"*. De README legt uit dat het een **keychip** is en dat er geen regiowaarde te
  meten is. Wat er nodig is: een eigen `kind` voor de keychip in `docs/dropins.md`, de regiowaarde
  meten (testmenu of een log van `keychip.appboot.region`), en dan pas een `dongle.json`.
* **OG** — `region.py` meldt: *"de executable van OG noemt geen enkele amDongle- of regioleesroute;
  klopt dongle.json wel?"*. Wat er nodig is: de game een keer starten en `enforced` **meten**, en
  `enforced_basis` in `dongle.json` vervangen. De waarden (JAPAN/JAPAN) komen nu uit het
  TeknoParrot-sjabloon.
* **TC5** — de **dongle-image** ontbreekt (`"memory": []`). Wat er nodig is: de fileids vastleggen
  die de game opvraagt (`hasp_read`) en de bytes die hij terug verwacht; zonder de fysieke dongle kan
  dat alleen door de antwoorden van de bestaande emulatie af te vangen. De README noemt drie routes
  die **niet** werkten (verbose-vlag, `[Env]` in de gebruikerslaag, `atime` op `relatime`) en één die
  wel zou werken: de Windows-DLL naar een **logbestand** in de view laten schrijven in plaats van naar
  `stderr` (`hasp/src/hasp_preload_win.c` heeft nu geen logging).

### 5. Een beleidstegenstelling die het werk in de weg zit

`docs/per-game-patches.md` zegt op regel 355-369 dat de categorieën `dongle`, `region`, `keychip`,
`integrity` en `activation` **bestaan** en dat ze per game in `patches/<Game>/patches.ini` horen
("gewone patches zoals elke andere"). Dat is ook zo in de code: `scripts/gamesettings.py` regel 51
heeft ze in `PATCH_CATEGORIES`, en `scripts/patchfile.py` weigert ze niet.

Maar **dezelfde pagina** zegt op regel 398-399: *"Grenzen (projectafspraak, ook voor meegeleverde
patches van anderen): geen patches op dongle-, keychip-, regio-, integriteits- of
abonnementscontroles, en niets op uitgepakte beschermde code."* En `patches/RabbidsHollywood/README.md`
zegt nog: *"De dongle-patch staat niet in `patches.ini`: dat formaat heeft er bewust geen categorie
voor"* — dat is achterhaald, want die categorie bestaat nu.

**Gevolg:** het formaat staat een dongle- of regiopatch toe, de beleidsteksten spreken elkaar tegen, en
**in geen enkele game staat zo'n patch**. Wie er een wil schrijven, moet eerst weten welk van de twee
regels geldt. Dat is een vraag voor de eigenaar, niet iets wat deze kaart kan beslissen.

---

## Wat ik niet heb kunnen vaststellen

1. **Of een bytepatch de geparkeerde games verder helpt.** Voor Terminator, GhostBusters,
   MedaruNoGunman, Otomedius en RainbowBombergirl is geen adres gemeten, dus er is geen patch te
   schrijven. Ik heb dat niet zelf uitgezocht (dat vraagt disassemblage van de exe per game).
2. ~~Of de vier `patches.ini`-bestanden de juiste `Find`-bytes kunnen krijgen.~~ **Opgelost in ronde
   33:** de bytes zijn op de echte exe's gemeten en staan bij acht van de twaalf patches. Bij de andere
   vier zou een `Find` niets zeggen (andere build, of de byte staat er al).
3. ~~Of OG's `dongle.json` klopt.~~ **Opgelost in ronde 38** (de beschrijving klopt) **en in
   ronde 43** (`region.py` vindt de exe nu zelf, via dezelfde resolver als de launcher: oordeel groen
   met `errors: []`, zonder `RAVENGUN_GAME_DIR`). Wat blijft: de waarden komen uit het
   TeknoParrot-sjabloon, en de game is nooit gespeeld (de start is gemeten, de speelbaarheid niet).
4. **Welk donglebestand GhostBusters mist.** De lader meldt `GAME PATH CHECK FAILED` en de ELF noemt
   HASP/Aladdin, maar de dump heeft alleen een splash-afbeelding. Dat is een open vraag in
   `docs/loskoppelen-per-game.md` (open vraag 3) en ik heb hem niet kunnen beantwoorden.
5. **Of de runtime-route (een DLL die pas later geladen wordt) al aan de launcher gekoppeld is.** De
   generator `pf.runtime_list()` en de motor `memory/src/patchmem.c` bestaan en zijn getest, maar in
   `scripts/launch-direct.py` staat geen aanroep van `runtime_list`/`patchmem`. `docs/patchmotor.md`
   noemt die koppeling expliciet als overdracht. Ik heb dat niet geverifieerd in de C++-kant.
6. **Wat `untested` betekent, is nergens vastgelegd.** De loader-doc noemt de drie waarden
   (`playable | parked | untested`, `docs/per-game-patches.md` regel 132) zonder ze te definiëren.
   Voor OG is dat nu het enige dat nog wringt: de **gegevens** zijn rechtgezet (de Note zegt dat de
   game start en blijft draaien maar nooit gespeeld is, en `enforced = false` is gemeten in plaats van
   aangenomen), maar het **woord** `untested` kan "nooit gestart" of "nooit gespeeld" betekenen en
   `STAND.md` neemt het letterlijk over in zijn kolom. Dat is een vraag voor de eigenaar van de
   loader-doc, niet iets wat deze kaart kan beslissen.
7. ~~Wat `InstallMusicGunGun2AndroidD3D9Compatibility()` doet.~~ **Opgelost in ronde 40:** de functie
   staat in `MusicGunGun2InputMisc.cpp` regel 56 en doet **niets** zonder `ANDROID_ALSA_SERVER` in de
   omgeving — ze is voor de Android x86-brug, niet voor onze start. Wat ze dan zou doen is
   `injector::MakeCALL(base + 0x11F3B, …)`: zes bytes (`8B 42 40 FF D0`, `mov eax,[edx+40h]` /
   `call eax`) vervangen door een **call naar een functie in de kern zelf**. Dat past niet in
   `patches.ini`: de motor kan bytes schrijven, maar het doeladres wordt pas bij het laden berekend.
   Er valt hier dus niets toe te voegen, en op onze platform is de functie bovendien dood.
8. ~~Ik heb de testsuite niet groen gekregen.~~ **Opgelost in ronde 33:** `test_repo_tree` is in de
   loader-repo bijgewerkt (commit `4f18989`) en `python3 -m unittest discover -s tests` eindigt weer op
   `OK (skipped=21)`, 240 tests. Zie
   [De testsuite van de loader kijkt naar deze repo](#de-testsuite-van-de-loader-kijkt-naar-deze-repo).
