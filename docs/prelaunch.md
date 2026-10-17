# Startscherm en poort vóór een lightgun-game (2026-10-17)

Doel (de eigenaar): een game start pas als de speler echt klaar is. Is de PS Move gekozen en ingesteld, dan eerst controleren of hij
aan staat ("Please turn on your PS Move controller"), ook in Steam Game Mode (gamescope) waar het hoofdvenster niet te zien is. Daarna
een korte kalibratie: eerst de Move van speler 1 (blauw), dan, als die aan staat, die van speler 2 (rood), en dan pas de game.

Geïmplementeerd en automatisch getest (`ravengun_readiness_tests`, `ravengun_games_tests`, `ravengund_command_tests`,
`ravengun_app_tests`, `ravengun_app_widget_tests`). Nog niet op de tv en niet in Game Mode geprobeerd.

## Eén poort, in de engine

De beslissing staat op één plek: `src/ravengun/games/readiness.*` (zuiver, geen I/O, geen klok). Elke startweg gaat door de engine
(`game_start`: dubbelklik en Play in de app, Big Picture, `ravengun --play`, `ravengunctl game start`, oude snelkoppelingen), en
`GameService::wait_lightgun` wacht daar met deze beslissing. Het startscherm van de app vraagt dezelfde beslissing op met IPC
`game_ready` en toont hem; daarna wacht `game_start` niet meer (de Move is al aan), maar als de Move tussendoor uitgaat, wacht de engine
alsnog en toont de app dezelfde melding.

- **Soort game.** Een lightgun-game heeft in de indeling van zijn invoerroute een aim-paar `pN.x` en `pN.y` (`tp-shm-profiles.json`, de
  analoge kanalen van `jvs-boards.json`, de assen van `hid-boards.json`), of gaat via de virtuele gun (`evdev`) of de Unity-plugin
  (`unity-tcp`). Het aantal guns is het hoogste N met een aim-paar (evdev en Unity zonder indeling: 4). Zonder aim-paar is het een
  pad-game (Mario Kart DX: stuur, gas, rem; Let's Go Safari: alleen `p1.x`): geen wachten en geen kalibratie. Zonder `[Input]` of met een
  onbekende indeling: een lightgun voor elke speler (liever wachten dan blind starten, het oude gedrag). `[Input] GunPlayers = 0..4` in
  `game.ini` gaat voor (0 = pad-game); nog nergens gezet.
- **Speler 1** is verplicht. Heeft hij een Move (een niet-verwijderde Move met spelernummer 1 in `config.json`), dan wacht de start op
  precies die Move; een andere Move of een gamepad telt niet. Een verbonden muis als lightgun van speler 1 telt wel (de muismodus van de
  bench). Zonder Move van speler 1 geldt het oude gedrag: een willekeurige verbonden Move, muis of gamepad van een speler.
- **Speler 2..N** (N = guns in de game) **wacht niet**. Een toegewezen Move die uit staat komt in `optional_off`; het startscherm geeft
  hem `kJoinMs` (10 s) om mee te doen. Afwijking van de opdracht ("P2 ook wachten als hij een Move heeft"), bewust: een twee-gun-game
  (Halo, House of the Dead) alleen spelen terwijl de tweede Move in de la ligt, zou anders nooit starten.
- **Time-out:** de engine wacht hoogstens `kMoveWaitMs` (2 minuten) en eindigt dan in `failed` ("Geen PS Move aangezet binnen 2
  minuten"). Het startscherm zelf wacht zonder time-out; afbreken kan altijd (B, Escape, Stop, of Steam "Exit game").
- **Wachten** is een lus van 200 ms in de worker van de start (een verbinding van een Move is geen gebeurtenis waar de worker op kan
  wachten); stoppen breekt hem binnen 200 ms af. De app vraagt `game_ready` elke 500 ms zolang het scherm wacht.

IPC (`docs/architecture.md`, "IPC"):

- `game_ready` (`profile`): `{"lightgun","gun_players","source","ready","waiting":[..],"waiting_any","optional_off":[..],
  "calibrate":[..],"players":[{"player","move_assigned","move_connected","mouse","mouse_connected"}]}`. CLI: `ravengunctl game ready
  PROFIEL`.
- `games`/`game_start`: de status heeft `waiting_players` (op wiens Move de start wacht) en bij `waiting_move` de tekst "Zet de PS Move
  van speler 1 aan". De app maakt daar "Please turn on your PS Move controller" van.
- `set_led` met `temporary: true` (alleen live: niet bewaard, de trackertint blijft) en `restore: true` (de kleur van vóór de eerste
  tijdelijke kleur terug). Een gewone `set_led` bewaart de kleur en verandert de tint van de camera; daarom niet die.

## Het startscherm (`src/app/widgets/PreLaunchWindow.*`, logica in `src/app/core/PreLaunch.*`)

Schermvullend en zwart (OLED), te bedienen met alleen de Move; een gamepad (A, B, Start), toetsenbord (Enter, Escape, S) en muis werken
ook. Stappen, elk alleen als het nodig is:

1. **Wachten:** "Please turn on your PS Move controller" (of "Player 2: ..."), tot de Move van speler 1 aan is.
2. **Kalibreren** per speler met een verbonden Move: de Moves krijgen tijdelijk hun kleur (speler 1 blauw, 2 rood, 3 groen, 4 geel),
   het scherm toont een doel in die kleur en het vizier van die speler (`watch` met `player`). Richten en de trekker overhalen (of PS)
   = IPC `recenter` van die speler; MOVE slaat de speler over. Daarna "Player 1 is ready" en de volgende. Na de kalibratie krijgen de
   Moves hun eigen kleur terug.
3. **Meedoen:** staat de Move van speler 2 (of 3, 4) uit en heeft de game die gun, dan 10 s "Player 2: turn on your PS Move to join";
   zet hij hem aan, dan kalibreert hij ook. De trekker van speler 1 start meteen.
4. **Haak: de uitleg van de knoppen** (zie onder).
5. **Starten:** `GameLauncher::play` (IPC `game_start`).

Waarom kalibreren altijd aangeboden wordt en niet "alleen als het lang geleden is": de yaw van de Move verloopt en het midden gaat
verloren bij een herstart van de engine of een nieuwe verbinding; een tijdstempel in de app weet dat niet. Eén trekker kost twee
seconden, MOVE slaat over, en **Settings > Display > Before a lightgun game starts** zet de kalibratie helemaal uit (het wachten op de
Move blijft). Is er niets te wachten of te kalibreren (pad-game, muismodus, kalibratie uit en de Move al aan), dan verschijnt er geen
scherm en start de game meteen.

### Startwegen

- **App:** dubbelklik, Play en Big Picture gaan door `PlayWithPreLaunch` (één ingang). Het scherm verdwijnt zodra de game draait. Big
  Picture negeert zijn eigen invoer zolang het startscherm erover ligt.
- **`ravengun --play PROFIEL`** (Steam en snelkoppelingen): alleen het startscherm, geen hoofdvenster, geen scan. Het proces blijft
  bestaan (onzichtbaar) zolang de game draait, want Steam volgt het proces, en stopt als de game stopt (exitcode 0; 1 = mislukt,
  2 = afgebroken). SIGTERM (Steam "Exit game") stopt eerst de game (`game_stop`, hoogstens 3 s) en dan het proces: de game draait in
  de eigen unit `ravengun-tpui`, buiten het procesbeheer van Steam.
- **Steam:** rechtsklik op een game > **Add to Steam...** maakt een eigen niet-Steam-game met `ravengun --play PROFIEL`, de naam en het
  icoon van de game (geen afbeeldingen). Het bijwerken herkent het item aan programma plus opties; het item "RavenGun" (Big Picture)
  neemt nooit een game-item over, en een eigen niet-Steam-game met dezelfde naam blijft ongemoeid.
- **Bureaublad:** een snelkoppeling met `Exec=<bin>/ravengun --play PROFIEL` geeft hetzelfde. Een oude snelkoppeling met
  `ravengunctl game start` werkt nog (de engine wacht), maar zonder scherm.

## Haak: de uitleg van de knoppen (`ControlsSetup`)

Tussen de kalibratie en de start: `PreLaunchWindow::RunControlsSetupStep()` toont de widget `src/app/widgets/ControlsSetup.{h,cpp}`
(docs/per-game-patches.md, "Bediening per game") als `ControlsSetup::needsSetup(profile)` waar is: een lightgun-game met knoppen om te
kiezen waarvan de speler de keuze nog niet bevestigde (`Confirmed = true` in [Controls] van de gebruikerslaag). De widget
vult het startscherm; het startscherm zelf negeert zolang zijn invoer. `finished(true)` = verder naar de start, `finished(false)` = de
speler breekt af (geen start, terug). `needsSetup` telt mee voor de keuze of er een scherm komt.

## Open

- Gamescope: of Steam Game Mode het venster van de game toont terwijl `ravengun --play` (de app van Steam) onzichtbaar wacht. De game
  start buiten de procesboom van Steam (systemd-run); dat geldt nu ook al voor Big Picture. Op de tv nagaan.
- Twee Moves tegelijk op het startscherm zijn alleen automatisch getest.
