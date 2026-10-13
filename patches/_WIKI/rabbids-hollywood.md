# Rabbids Hollywood Arcade (Adrenaline PC Based) — publieke digest

Dit is een technisch verslag van wat wij van dit spel hebben gemeten en geleerd, geschreven voor
iemand die dit project **niet** kent en er een eigen front-end of emulator op wil bouwen. Per bewering
staat of hij **GEMETEN** is (met cijfers, tijdstippen, paden, offsets) of **AFGELEID**.

Alles hier is op een Steam Deck (SteamOS) onder Wine/Proton gemeten. De spelbestanden staan op een NAS
en zijn daar **alleen gelezen**.

| | |
|---|---|
| Spel | Rabbids Hollywood Arcade |
| Platform | **Adrenaline Amusements, "PC Based"** — een gewone Windows-pc in de kast, geen embedded arcadeplatform |
| Jaar | 2018 |
| Engine | **Unity 2017.2, 64-bits Mono** |
| Soort | lightgun-shooter; het spel toont "Insert credits" per speler en het protocol draagt vier spelers — **AFGELEID** voor de kast zelf, en wij hebben alleen speler 1 uitgeoefend |
| Map op onze share | `Teknoparrot/Rabbids Hollywood ` — **let op de spatie aan het eind van de mapnaam** |
| Uitvoerbaar bestand | `Game.exe`, 650.752 B |
| Route die wij gebruiken | **`unity-tcp`**: de game is de TCP-server, onze daemon is de client. Geen eigen lader, geen injectie |
| Onze stand | **de lat is gehaald**: 608 s (10 min 8 s) op onze eigen route, munt en start aangeslagen, een echt level gespeeld (zie §8) |
| Niet af | trekker en trilling van een **echte Move**, het vizier **visueel** bevestigen, speler 2-4, en de spelstatus die niet bewaard blijft (zie §9) |

**Leeswijzer bij de markeringen.** **GEMETEN** = afgelezen uit een bestand, een logboek of een
procesmeting, met het bewijs erbij. **AFGELEID** = een gevolgtrekking uit die metingen; bruikbaar, maar
geen bewijs. Getallen staan er altijd bij.

**Deze pagina gaat over het spel en de lader:** wat je nodig hebt om deze titel te laten draaien.

---

## 1. Platform en kast

**Het platform (GEMETEN).** Anders dan de Sega-titels in deze verzameling is dit een **pc-titel**: de
kast van Adrenaline Amusements draait een gewone Windows-installatie en het spel is een Unity-programma.
Het TeknoParrot-profiel zegt hetzelfde: `EmulatorType: TeknoParrot`, `EmulationProfile: WartranTroopers`,
`ExecutableName: Game.exe`, `Is64Bit: true`. **GEMETEN** uit het profiel; het `WartranTroopers`-profiel
is een generieke indeling voor shooters met vier spelers, wat past bij de vier assenparen in het
draadprotocol (§5).

**Het spel (GEMETEN):**

| bestand | grootte | rol |
|---|---|---|
| `Game.exe` | 650.752 B | het spel; precies de naam die het profiel noemt |
| `UnityPlayer.dll` | 23 MB | de Unity-runtime, 64-bits |
| `Game_Data/` | 1,9 GB | de Unity-assets (`Managed`, `Plugins`, `Resources`, `StreamingAssets`, `Mono`, `GI`) |
| `UnityPlayer_Win32_x64*.pdb`, `WindowsPlayer_x64_Master*.pdb` | — | **debug-symbolen zitten in de map**; handig voor wie de runtime wil volgen |
| `Firmware_Shooter.hex`, `libpng15.dll` | — | kastfirmware en een losse DLL die de game meelevert |

**De kast en het beeld (GEMETEN uit het spelgedrag, AFGELEID voor de kast zelf).** De kast heeft
**lichtgeweren**; de game leest zijn richtpunt als een muispositie in het spelvenster — dat is precies
waarom het draadprotocol schermpixels gebruikt en waarom de Y-oorsprong onderaan ligt (§5). De tv is
1920×1080; dat is ook de resolutie waarin wij het spel draaien. Op het scherm staat "Insert credits"
**per speler**, en munten worden in **kwarten** geteld ("1/4 credits"). De kast zelf hebben wij nooit
gezien.

**De taal (GEMETEN).** Het spel is meertalig en heeft een instelbare taal; wij zetten hem op Engels en
het titelscherm kwam in het Engels in beeld (§7).

---

## 2. Wat het spel nodig heeft

Wij hebben de map op volledigheid nagerekend. **GEMETEN**:

| controle | uitkomst |
|---|---|
| uitvoerbaar bestand | `Game.exe`, 650.752 B — precies wat het profiel noemt |
| **lege bestanden** | **0 van de 1.788** |
| omvang | **2,0 GB** (waarvan `Game_Data` alleen al 1,9 GB) |
| Unity-structuur | `Game_Data` met `Managed`, `Plugins`, `Resources`, `StreamingAssets`, `Mono`, `GI`; `UnityPlayer.dll` (23 MB) |
| aanwezige BepInEx-route | `BepInEx/{core,plugins,config,cache,patchers}`, `doorstop_config.ini`, `.doorstop_version`, `winhttp.dll` |
| gereedschap naast het spel | `GameLauncher.exe`, `RabbidsHollywood_Operator.exe` + `.conf` + `README.txt` |

De map is dus **structureel compleet**: geen enkel bestand is leeg en niets ontbreekt voor een start.
Er is geen losse assetmap of schijfletter nodig zoals bij sommige andere titels: alles staat onder de
spelmap.

**Wat wij er zelf bijzetten, en waar (GEMETEN).** Wij schrijven **niets** in de map op de NAS. In plaats
daarvan maken wij een **lokale view** met alleen koppelingen:

```
~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/     <- lokale map met symlinks
    <alle bestanden van de NAS, niets gekopieerd>
    Game_Data/                       <- een ECHTE map met per onderdeel een koppeling
    BepInEx/                         <- onze eigen BepInEx 5.4.21.0, niet die van de NAS
    winhttp.dll, doorstop_config.ini <- de doorstop van BepInEx
    BepInEx/plugins/RavenGun.RabbidsHollywood.dll   <- onze plugin in het spel
```

* Kleine configuratiebestanden (`*.json`, `*.conf`, `*.cfg`, kleiner dan 64 KiB) en lege vlagbestanden
  (`Game.run`, `ForceReboot`) worden **lokaal gekopieerd** in plaats van gekoppeld, omdat het spel erin
  schrijft. **GEMETEN**: in een run van 10 minuten kwamen die schrijfacties inderdaad allemaal lokaal in
  de view terecht (mtime's tijdens de run) en bleef de NAS onaangeroerd.
* De BepInEx die **op de NAS in de spelmap zit** en `teknoparrot.ini` worden **niet** in de view gezet:
  wij gebruiken onze eigen, schone BepInEx. **GEMETEN** in het aanmaakscript.

---

## 3. Het officiele startscript (of waarom het er niet is)

**Er is voor deze titel geen `game.bat`.** De map bevat geen enkel `.bat`- of `.cmd`-bestand.
**GEMETEN** uit de bestandslijst van de map. Wat er in de plaats staat, zijn twee Windows-programma's:

* `GameLauncher.exe` — een startprogramma in de map;
* `RabbidsHollywood_Operator.exe` + `RabbidsHollywood_Operator.conf` + `RabbidsHollywood_Operator.exe
  README.txt` — een bedienings-/operatorgereedschap met een configuratiebestand en een leesmij.

Wat die twee precies doen is **AFGELEID** (wij hebben ze niet uitgevoerd en niet ontleed); hun namen en
de leesmij erbij maken duidelijk dat het kastgereedschap is en niet de spellus.

**WAARSCHUWING — wij voeren dit soort scripts en programma's NOOIT uit.** Dat is geen voorzichtigheid
voor de vorm: in deze zelfde verzameling zit een titel waarvan het officiele `game.bat` begint met
`rmdir /S /Q Y:\`, dus met **het wissen van een hele schijf** als de verwachte datamap ontbreekt. Op een
echte kast is dat een dedicated schijf; op een gewone machine kan `Y:` van jou zijn.

Onze werkwijze is daarom: **lees** het startgereedschap, neem alleen de delen over die je nodig hebt
(paden, argumenten, omgevingsvariabelen, volgorde), en start het spel daarna zelf. Voor deze titel viel
er uit het startgereedschap niets over te nemen dat wij nodig hadden: `Game.exe` start direct, en de
twee problemen die je zonder nadenken zou willen oplossen met een startscript — de mapnaam met de
spatie en de BepInEx-route — lossen wij op met de view (§2 en §10.2).

---

## 4. De route die wij gebruiken

**Geen eigen lader, geen injectie.** Dit is de eenvoudigste route in ons project en tegelijk de reden
dat deze titel als eerste de lat haalde: het spel wordt **rechtstreeks door Proton** gestart, en het
enige dat wij in het spelproces krijgen is een **BepInEx-plugin** via de doorstop.

De keten, met de exacte bestanden (**GEMETEN** in de scripts):

```
scripts/play-halo.sh --profile RabbidsHollywood
   └─ ziet het profiel in packaging/aagames-games.json en gaat naar
scripts/play-unity.sh --game RabbidsHollywood
   ├─ python3 -I scripts/aagames-setup.py RabbidsHollywood    # view + prefix klaarzetten
   ├─ systemctl --user start ravengund                        # de daemon moet lopen
   ├─ ravengunctl arm                                         # de uitvoer (virtuele gun) wapenen
   ├─ systemd-run --user --unit=ravengun-tpui \
   │     --working-directory=<view> \
   │     <GE-Proton>/proton run Game.exe -popupwindow
   └─ ravengunctl aagames start RabbidsHollywood               # de daemon wordt TCP-client
```

* **Lader:** `GE-Proton11-7-x86_64` (DXVK), aangeroepen als `proton run`. Er is **geen** aparte
  lader-DLL en **geen** kern in het spelproces. **GEMETEN**.
* **In het spel:** BepInEx 5.4.21.0 (Mono, x64) met de doorstop (`winhttp.dll` +
  `doorstop_config.ini`), en onze plugin `RavenGun.RabbidsHollywood.dll`.
* **Draadrichting (§5):** de plugin zet een TCP-server op `127.0.0.1` en **onze daemon is de client**.

**Omgevingsvariabelen en argumenten (GEMETEN uit `play-unity.sh` en `aagames-setup.py`):**

| naam | waarde | waarom |
|---|---|---|
| `WINEDLLOVERRIDES` | `winhttp=n,b` | **verplicht**: zonder deze override wordt de doorstop (`winhttp.dll`) niet geladen en komt er dus geen plugin in het spel |
| `WINEDEBUG` | `-all` | geen ruis in het logboek tijdens het spelen |
| `STEAM_COMPAT_DATA_PATH` | `~/.local/share/ravengun/prefixes/unity-aagames` | het Wine-voorvoegsel van deze route |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | `~/.local/share/Steam` | nodig voor de Proton-aanroep |
| `RAVENGUN_UNITY_PLUGIN` | `ravengun` | welke plugin in de view komt; `demulshooter` is de terugval |
| `DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, `DBUS_SESSION_BUS_ADDRESS`, `XAUTHORITY` | uit de sessie | zodat een start vanaf een terminal of uit de app op het scherm van de eigenaar terechtkomt |
| argument van het spel | `Game.exe -popupwindow` | randloos schermvullend venster |

De resolutie wordt niet met een argument gezet maar door de plugin: `FORCE_RESOLUTION=1`, `WIDTH=1920`,
`HEIGHT=1080`, `FULLSCREEN=1` in zijn ini. **GEMETEN**.

**De view (GEMETEN).** `scripts/aagames-setup.py` bouwt
`~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood` opnieuw bij elke start: symlinks naar
alle bestanden van de NAS, `Game_Data` als echte map met koppelingen, en lokaal BepInEx + doorstop +
onze plugin. Het spelproces start met de **view** als werkmap, niet de NAS-map — dat is meteen de
oplossing voor de spatie in de mapnaam (§10.2).

**Drop-ins (GEMETEN, en één eerlijke beperking).** Onze lader heeft een algemeen drop-in-mechanisme:
`patches/<Spel>/dropin/dropin.ini` noemt per bestand `To` (doelmap in de view), `DllOverride`, `Env` en
`Preload`, en de lader kopieert die bestanden in de view en past de omgeving aan. **Op deze route is in
deze ronde geen drop-in nodig**: wat het spel nodig heeft, komt uit onze plugin. Twee dingen om te
weten als je hier zelf iets wilt plaatsen:
1. de drop-in-motor zit in het **directe** startpad (`scripts/launch-direct.py`); `play-unity.sh` leest
   `dropin.ini` **niet** en zet alleen `WINEDLLOVERRIDES=winhttp=n,b`. Een drop-in wordt op deze route
   dus nog niet toegepast. **GEMETEN** in de bron.
2. `Game_Data` is in de view een echte map met koppelingen, dus een drop-in met
   `To = Game_Data/Plugins` wijst op een **koppeling naar de NAS**. Wie daar iets plaatst, schrijft op de
   NAS. De motor hoort die map eerst lokaal te maken; onze eigen startscripts doen dat op deze route nog
   niet. **GEMETEN**.

**Stoppen.** `scripts/play-halo.sh --stop`. De game draait in de transient unit `ravengun-tpui`, dus
stoppen is één `systemctl --user stop` op die unit.

**Zelf narekenen (GEMETEN commando's).**

```bash
# view klaarzetten (en zien welk pad hij kiest)
python3 -I scripts/aagames-setup.py RabbidsHollywood --print-view

# starten
scripts/play-halo.sh --profile RabbidsHollywood

# de plugin in het spel zien laden
grep -a -E 'Loading \[RavenGun|luistert op 127.0.0.1|Daemon verbonden|Invoer van de daemon' \
  ~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/BepInEx/LogOutput.log

# de verbinding en het tempo
ravengunctl status          # regel "Unity-game": VERBONDEN met de game, ~120 pakketten/s

# stoppen
scripts/play-halo.sh --stop
```

---

## 5. Invoer

Er zijn voor deze titel **twee** invoerwerelden, en het is belangrijk ze niet te verwarren:

1. **Het kastdialect** — hoe de echte kast het geweer aan de game geeft. Het TeknoParrot-profiel
   `WartranTroopers` beschrijft dat als een bitveld plus per speler een X- en een Y-byte (1 byte per as).
   **GEMETEN** uit het profiel. Wij gebruiken dit op onze route **niet**.
2. **Het `unity-tcp`-dialect** — wat wij in de praktijk gebruiken: een klein binair protocol tussen een
   plugin **in** het spel en onze daemon. Dat is wat iemand nodig heeft die dit spel zonder TeknoParrot
   wil aansturen, dus dat beschrijven we hier volledig.

### 5a. Het `unity-tcp`-protocol (GEMETEN, byte voor byte)

Rolverdeling: de **plugin in het spel is de TCP-server** op `127.0.0.1`, standaard **poort 33610**; de
**daemon is de client**. Er is één client tegelijk. Alles is little-endian, zonder padding.

**Invoer, client → plugin.** Per bericht precies één pakket van vaste lengte, **zonder lengte en zonder
kop**. De velden staan in **alfabetische volgorde van de veldnaam** en arrays hebben vier elementen
(speler 1 tot 4). Voor **Rabbids Hollywood is het pakket 38 bytes**:

| offset | veld | type | betekenis |
|---|---|---|---|
| 0-15 | `Axis_X` | `float32[4]` | richtpunt X per speler, in **schermpixels** van het spelvenster: `0 .. breedte-1` (bij 1920: `0..1919`) |
| 16-31 | `Axis_Y` | `float32[4]` | richtpunt Y per speler, in schermpixels, **Y-oorsprong onderaan**: `0 .. hoogte-1` |
| 32 | `EnableInputsHack` | `u8` | **moet 1 zijn**; staat hij uit, dan valt de plugin terug op muis en toetsenbord |
| 33 | `HideCrosshairs` | `u8` | vizier van het spel verbergen |
| 34-37 | `Trigger` | `u8[4]` | trekker per speler, 1 = ingedrukt |

Twee dingen die je hier moet weten:

* **Er is geen `Reload`-veld** bij dit spel. De zustertitel in hetzelfde protocol (Tomb Raider Arcade)
  heeft dat wel en komt daardoor op **42 bytes**: daar staat `Reload u8[4]` op 34-37 en `Trigger u8[4]` op
  38-41. Wie beide games wil ondersteunen, moet dus op de lengte of op het speltype schakelen.
  **GEMETEN** in onze implementatie en in de bron van de plugin.
* **De Y-oorsprong is onderaan** (Unity's `Input.mousePosition`). Reken dus om: is `v` je richtpunt als
  fractie **vanaf de bovenkant** (0 = boven, 1 = onder), dan is `Axis_Y = (1 - v) * (hoogte - 1)`. Het
  spel accepteert dat en zet het vizier op `hoogte - 1 - Axis_Y ≈ v * hoogte` pixels vanaf de bovenrand.
  **GEMETEN**: een richtpunt van `u=0,3 v=0,3` gaf het vizier op **576/324** pixels van linksboven
  (0,3 × 1920 = 576; 0,3 × 1080 = 324). De rechterrand is `breedte - 1`.

**Uitvoer, plugin → client (GEMETEN).** Elk bericht: `int32` lengte (= inhoud + 1, dus **61**), dan `u8`
koptype (**2** = uitvoer), dan **60 bytes** inhoud. Ook hier alfabetisch op veldnaam, en ook hier is de
volgorde per spel anders:

| volgorde | veld | type | betekenis |
|---|---|---|---|
| 1 | `Ammo` | `i32[4]` | munitie per speler |
| 2 | `Credits` | `i32[4]` | munten per speler (kwartmunten) |
| 3 | `Damaged` | `u8[4]` | eenmalige vlag: speler is geraakt |
| 4 | `Life` | `float32[4]` | levensbalk |
| 5 | `Recoil` | `u8[4]` | **eenmalige vlag: dit bericht is een schot** |
| 6 | `StartLamp` | `u8[4]` | startlamp per speler — **bij dit spel staat dit veld achteraan** |

Let op het laatste: bij de zustertitel staat op die plek `IsPlaying` en staat `StartLamp` er niet. Wie
de uitvoer leest, moet de veldvolgorde dus per spel kennen; de plugin meldt hem zelf in zijn logboek.
`Recoil` en `Damaged` zijn **eenmalige vlaggen**: elk bericht met `Recoil[p] != 0` is een schot. Wij
koppelen dat aan de trilling van de Move.

**Tempo en bron (GEMETEN).** Wij sturen **vast 120 pakketten per seconde** (`send_hz`, instelbaar per
spel) in plaats van op gebeurtenissen: een verloren pakket kost dan niets, en de plugin heeft herhaalde
waarden (trekker vast) toch nodig. De inhoud van `Axis_X`/`Axis_Y` is de absolute asstand van de
virtuele gun van die speler, als fractie **0..1**, opgehaald op het moment van verzenden — dus ná onze
eigen transformatie en voorspelling, en met dezelfde viewport en inversie die een TeknoParrot zou zien.
Een speler zonder gewapende gun krijgt het midden en geen knoppen.

### 5b. Wat wij gemeten hebben aan het richten en aan de knoppen

* **Het vizier volgt het gevraagde punt.** `u=0,3 v=0,3` → vizier op **576/324** px; de Y-oorsprong
  onderaan is voor dit spel **gemeten**, niet aangenomen. **GEMETEN**.
* **Onze eigen uitvoerlaag in het groot.** `ravengunctl test-pattern point 0.3 0.3` gaf
  `Positie: aim 0.300,0.300 -> abs 9830,9830` en `0.7 0.7` gaf `0.700,0.700 -> abs 22937,22937`. Onze
  absolute as loopt dus over **0..32767** (0,3 × 32767 = 9830; 0,7 × 32767 = 22937) en de omzetting naar
  schermpixels gebeurt pas in de client. **GEMETEN**.
* **Munt en start, met de cijfers.** Munten worden in **kwarten** geteld en de standaardtoetsen zijn:
  **5** = munt speler 1 (en 6/7/8 voor spelers 2-4), **1** = start speler 1 (2/3/4 idem), **0** = test,
  **Esc** = afsluiten. In de 10-minutenrun, met de toetsen op gamescope's eigen Xwayland:

  ```
  [23:12:27] Stand: credits 2,0 ammo 0 leven 100 speelt 0
  [23:12:27] Stand: credits 3,0 ammo 0 leven 100 speelt 0
  [23:12:27] Stand: credits 4,0 ammo 0 leven 100 speelt 0
  [23:12:27] Stand: credits 4,0 ammo 0 leven 100 speelt 1     <- start aangeslagen
  [23:12:27] Stand: credits 0,0 ammo 0 leven 100 speelt 1     <- munt verbruikt
  ```

  `speelt` is onze lezing van `StartLamp`; vier munten en dan start werkt dus precies zoals op de kast.
  **GEMETEN**.
* **Er is geen `Reload`.** Dit spel herlaadt niet via het kanaal; het leest ook geen `Reload`-byte.
  **GEMETEN** in het protocol en de plugin.
* **Wat wij NIET gemeten hebben:** de **trekker en de trilling van een echte Move** (er was geen Move
  aangesloten in de 10-minutenrun), en het vizier **visueel** op een scherm (§7 en §8).
* **Spelers 2-4.** Het protocol en het pakket ondersteunen vier spelers, en de game accepteert "Insert
  credits" per speler, maar wij hebben alleen speler 1 uitgeoefend. **AFGELEID** dat 2-4 werken.

---

## 6. Timing

Alle tijden zijn **seconden na het starten van de unit**, op een Steam Deck onder Proton. **GEMETEN**,
tenzij er AFGELEID bij staat:

| moment | waarde | bron |
|---|---|---|
| eerste keer een schone prefix aanmaken | **~30 s** (GE-Proton maakt het voorvoegsel) | gedocumenteerde procedure |
| unit start → `Game.exe` zichtbaar (run van 288 s) | **~3 s** (21:59:34 → 21:59:37) | levenslijnlogboek |
| unit start → `Game.exe` zichtbaar (run van 608 s) | **~16 s** (AFGELEID: 23:21:10 min 608 s = 23:11:02, en de unit startte 23:10:46) | zelfde logboek, teruggerekend |
| `Game.exe` → munten en start aangeslagen | **~78 s** (AFGELEID: 23:12:20 min 23:11:02) | idem |
| spel naar het titelscherm en de attractlus | seconden, geen aparte meting | — |

**Waarschuwing over koud en warm — dit is geen detail.** Bovenstaande twee runs doen hetzelfde
(unit starten, wachten op `Game.exe`) en geven **3 s** en **16 s**: een factor vijf op een handeling die
je als "de opstarttijd" zou willen opschrijven. Het verschil zit in de toestand van de machine
(voorvoegsel, caches, de plugin die al dan niet in de view staat) en niet in het spel. **AFGELEID** voor
de oorzaak, **GEMETEN** voor het verschil zelf.

Twee regels die daaruit volgen, en die wij als projectregel hebben overgenomen:

1. **Schrijf er altijd bij of een run koud of warm was.** Een tijdvergelijking zonder die vermelding
   zegt niets.
2. **Vergelijk nooit een instrumenteerde run met een niet-geinstrumenteerde.** Uit een andere titel in
   deze verzameling is gemeten dat een volledige bestandstrace (`WINEDEBUG=+file`) een proces dat
   normaal 5-7 s leefde **25-30 s** in leven hield. Het meetinstrument verandert wat je meet.

---

## 7. Wat we op het scherm zagen

Hier moet een eerlijk onderscheid: **de tien-minutenrun waarop §8 rust, liep headless.** Het spel kreeg
een Xwayland-display zonder fysieke uitvoer, zodat het scherm van de eigenaar vrij bleef. In die run is
er dus **geen scherm** en zijn er **geen schermafbeeldingen**.

**Wat er in eerdere runs op de tv is gezien (GEMETEN in die runs, niet in de 10-minutenrun):**

1. het **titelscherm**, in het Engels;
2. **"Insert credits" per speler**, met munten in kwarten ("1/4 credits");
3. na **vier munten en Start**: het **laadscherm**;
4. daarna het **spel**, met **vizier**, **wapen** en **levensbalk**.

**Wat de 10-minutenrun zelf opleverde (GEMETEN, maar uit toestandsuitlezingen en het logboek, niet uit
beeld):** de volgorde in de tijd.

| tijdstip | wat er gebeurde | hoe wij het weten |
|---|---|---|
| 23:10:46 | de unit start; de view is klaargezet en de plugin is uitgerold | levenslijnlogboek |
| ~23:11:02 | `Game.exe` verschijnt (AFGELEID, zie §6) | teruggerekend uit de spelklok |
| 23:11:02-23:12:20 | attractlus; het spel staat op het titelscherm en wacht op munten | de daemon leest `credits 2` bij de eerste blik |
| 23:12:20-23:12:27 | vier munten en start aangeslagen: `credits 2 → 3 → 4`, `speelt 1`, munt verbruikt naar 0 | toestandsregels in het logboek |
| 23:12:27 → 23:21:10 | **een echt level**, geen attractlus: de levensbalk liep leeg `100 → 44 → 35 → 25 → 15 → 4 → -6`, de speler ging dood, en het spel keerde netjes terug naar attract (`speelt 0`, leven weer 100) | toestandsregels in het logboek |
| 23:21:10 | stopmoment: `Game.exe` (pid 504342) **leeft nog**, daarna pas gestopt | levenslijnlogboek |

De toestandsmachine liep daarbij **menu → demo → spelen → dood → attract**, zichtbaar als
`ArcadeGamePlayManager ::: OnDemoStateExit` / `OnMenuStateEnter` / `OnDemoStateEnter` in het logboek.
**GEMETEN.**

Dat is het verschil dat de eigenaar bedoelde: niet "het proces leeft", maar **het spel is te spelen**.
Maar let op wat we daarmee **niet** weten: of het **vizier** goed staat op een echt scherm. Wat we wel
meten is dat het richten live is en dat een expliciet punt er exact uitkomt (§5b); het **beeld** zelf is
niet bevestigd. Een run op een echte display moet dat samenbrengen.

---

## 8. Stand van zaken en open vragen

**De lat, letterlijk van de eigenaar:**

> "...en Luigi's Mansion en Rabbids als voorbeelden 5 en 10 minuten draaien."

Voor deze titel betekent dat: **tien minuten draaien op onze eigen route**. Dat is **gehaald**.

### 8.1 Wat er precies gemeten is, en waarmee

| | |
|---|---|
| Harnas | script `/home/deck/rg_10min.sh`; systemd-user-unit **`rg-10min`** |
| Logboek | `/home/deck/rg-10min/main.log` (levenslijnen elke 30 s) |
| Beeld | **headless** via `gamescope --backend headless`, 1920×1080 — het scherm van de eigenaar bleef vrij |
| Stopcriterium | **600 s spelerstijd**, met een harde limiet van 720 s |
| Venster | start **2026-10-12 23:10:46**, einde **23:21:10** |

### 8.2 De cijfers (**GEMETEN**)

| meting | uitkomst |
|---|---|
| **spelerstijd** | **608 s** (10 min 8 s) — het logboek meldt `t=608s STOPMOMENT (600s) bereikt` |
| **einde** | **zelf gestopt**, niet uitgevallen: `Game.exe` (pid 504342, `-popupwindow`) leefde nog op het stopmoment; daarna pas `Game gestopt, uitvoer ontwapend.` |
| **threads** | **115** in het spelproces op het stopmoment |
| `Application.Quit` | **0** |
| verbinding | **120 pakketten/s op alle 20 levenslijnen**, nooit weggevallen |
| procesomgeving | **geen enkel TeknoParrot-proces** in de cgroup (alleen Wine/Proton-infrastructuur en `Game.exe`) |
| NAS | over het hele venster **0 vermeldingen** nieuwer dan 23:10:46 — er is niets naar de NAS geschreven |
| spelinhoud | munt en start aangeslagen, munt verbruikt, **een echt level gespeeld** (§7) |

De 8 s boven de 600 s komt van de 30 s-tik van de levenslijn: het spel is niet te vroeg en niet te laat
gestopt. **GEMETEN**.

### 8.3 Waarom dit getal iets betekent

* Het spel **leefde door** zonder enige tussenkomst van buiten: geen herstart, geen invoer nodig om in
  leven te blijven, en de verbinding viel in 10 minuten nooit weg.
* Het spel werd **gespeeld**, niet alleen aangezet: er is een level gestart en verloren, en het spel
  herstelde zelf naar attract. Dat is precies wat een speler op de kast ook zou zien.
* De hele run gebeurde **zonder enig gesloten onderdeel in het proces**: geen lader-DLL, geen kern, geen
  injectie — alleen Proton, BepInEx en onze plugin.

### 8.4 Wat deze run niet dekt

* **Trekker en trilling van een echte Move zijn niet uitgeoefend**: er was geen Move aangesloten. De
  muismodus kan wel een trekker geven, maar dan moet er een fysieke muis bewogen worden, en dat kan
  headless niet.
* **Het vizier is niet visueel bevestigd** (headless, geen scherm). Wat wél gemeten is: de aim van de
  daemon is live (vóór elke ingreep las onze uitvoer `aim 0.543,0.498`, licht drijvend naar 0.546/0.535)
  en een expliciet punt komt er exact uit (§5b).
* **Eén run.** Een tweede run van tien minuten zou het resultaat sterker maken; de bench ging daarna
  naar een andere titel.
* **Speler 1.** Spelers 2-4 zijn niet uitgeoefend.
* **Geluid en framerate** zijn nooit beoordeeld of gemeten.

### 8.5 Open vragen

1. **Het vizier op een echt scherm.** De volgende run hoort op de tv te gebeuren, met munt, start en een
   expliciet testpunt, zodat "start en leeft" en "munt/start/vizier" in **één** meting naast elkaar staan.
2. **Trekker en trilling met een echte Move.** Dat is het eigenlijke doel van het project: richten met
   de Move. Op deze titel is dat nog niet gebeurd; de Move-pijplijn (Bluetooth → hidraw → onze fusie →
   virtuele gun → pakket) is hier niet end-to-end uitgeoefend.
3. **Spelers 2 tot 4.** Het protocol ondersteunt vier spelers en het spel vraagt munten per speler; wij
   hebben alleen speler 1 gedaan.
4. **Spelstatus blijft niet bewaard.** De status van het spel (metrics, operator-menu, opties) wordt bij
   elke start als ongeldig gezien en **gereset**. Dat is nog niet opgelost, en het is een van de weinige
   dingen waarin deze titel zich nog anders gedraagt dan een kast. Wat er precies misgaat is nog niet
   vastgesteld.
5. **Eén lange sessie met invoer erbij.** De 10-minutenrun bleef in attract en één level; of er na lang
   spelen iets afwijkt (geheugen, een tweede level, een langzame levelwissel) is niet gemeten.

---

## 9. Wat we geprobeerd hebben en wat niet werkte

Dit zijn de dingen die ons tijd hebben gekost of die een verkeerde conclusie in de hand werkten. Ze staan
hier zodat je dezelfde omweg overslaat.

1. **Wij zochten een spel dat zichzelf afsloot, en het was een oude binary in de view.** Onze eigen route
   hield het spel eerst ongeveer een minuut vol, en wij hebben dat lang als een eigenschap van het spel
   behandeld. **GEMETEN** bleek het een uitrolprobleem: in de view die wij uitrollen stond een oude
   plugin (32.768 B) zonder het antwoord dat het spel nodig had, terwijl de verse (34.304 B) wel goed
   was. Dezelfde run ging van ~70 s naar **288 s** door alleen de plugin in de view te verversen, en
   daarna naar 608 s (§8). Wie de view niet controleert, meet de oude binary.
   **Wat je hieruit meeneemt:** vergelijk **grootte en md5** van wat er in de view staat met wat je hebt
   gebouwd — niet de tijdstempel, want die liegt.
2. **Ons eigen diagnosegereedschap naast de daemon werkte niet.** Het is geen injector maar een **tweede
   TCP-client** van de plugin, en de plugin houdt één client aan. Zodra het gereedschap verbond,
   wisselden verbinden en loskoppelen elkaar af: 10× verbinden / 10× verbreken, met als fout "de game
   sloot de verbinding". **GEMETEN.**
   **Wat je hieruit meeneemt:** draai een tweede client nooit naast een verbonden daemon; stop de daemon
   eerst, of stuur de test via de daemon zelf.
3. **Toetsen naar de sessie-compositor kwamen niet aan.** In een headless gamescope zijn de virtuele
   toetsen van onze eigen bediening niet in het spel aangekomen: er was precies één toestandsregel
   (`credits 0`). **GEMETEN.** Wat **wel** werkte, was ze naar gamescope's eigen Xwayland sturen
   (`DISPLAY=:1 xdotool key …`) — daarmee zijn munt en start in de tien-minutenrun aangeslagen (§5b).
   **Wat je hieruit meeneemt:** zonder dat verschil concludeer je ten onrechte dat "munt en start niet
   werken".
4. **Een uitkomst die je niet vooraf hebt opgeschreven, kun je achteraf niet beoordelen.** Wij schrijven
   daarom de verwachting op vóór de run, en een meting die niets vindt moet eerst laten zien dat hij
   gezocht heeft. Dat heeft deze week drie keer een verkeerde conclusie tegengehouden.
5. **Een tijdmeting zonder "koud" of "warm" is onbruikbaar** — zie §6, waar hetzelfde moment in twee runs
   3 s en 16 s kostte.

---

## 10. Wat is Linux-specifiek voor deze game (en wat je op Windows anders doet)

Wij draaien alles op **Linux** (SteamOS op een Steam Deck) onder **Wine/Proton**. Voor deze titel is dat
goed te overzien — er is geen lader en geen injectie — maar er zijn vijf dingen die je op Linux moet
regelen en die een Windows-bouwer niet tegenkomt. Per punt staat erbij of het **GEMETEN** is of een
**aanname**.

**10.1 Het Wine-voorvoegsel en de doorstop (GEMETEN).** Het spel draait in een **eigen** Wine-prefix
(`~/.local/share/ravengun/prefixes/unity-aagames`, gemaakt door GE-Proton met DXVK). Die prefix delen wij
bewust niet met andere titels: de doorstop-override hoort niet in de prefix van een andere game te
lekken. De eerste keer maakt Proton het voorvoegsel aan (~30 s); daarna is het warm.
**`WINEDLLOVERRIDES=winhttp=n,b` is verplicht** — zonder die override laadt Wine de doorstop
(`winhttp.dll`) niet en komt er geen plugin in het spel.
**Windows-verschil:** daar staat `winhttp.dll` + `doorstop_config.ini` gewoon naast de exe en werkt de
doorstop zonder enige override. Die omgevingsvariabele is dus puur Wine.

**10.2 De mapnaam met een spatie aan het eind (GEMETEN).** De map heet
`Rabbids Hollywood ` — **met een spatie achter "Hollywood"**. Wine weigert dat:
`Directory name invalid (0x10B)`. Onze oplossing is een **lokale view met symlinks** in plaats van de
NAS-map direct te starten (§2 en §4). Dat is geen cosmetische omweg: zonder de view start het spel niet.
**Windows-verschil:** Windows tolereert die spatie gewoon, dus daar is de view/symlinklaag niet nodig.
Wie op Linux een game toevoegt, moet hier op controleren — het kostte ons een sessie om te vinden.

**10.3 De "kern" in het spelproces (GEMETEN).** Er is voor deze titel **geen** geïnjecteerde kern. Het
enige dat in het spelproces komt is BepInEx zijn preloader plus onze **managed** plugin, geladen door de
doorstop. Wij hebben dus geen equivalent van de gesloten arcadekern nodig — en dat is precies waarom
deze titel als eerste de lat haalde.
**Windows-verschil:** het mechanisme is identiek; alleen de weg waarlangs de bestanden in de spelmap
komen verschilt (bij ons via de view, op Windows zou je ze naast de exe zetten).

**10.4 Drop-ins: `WINEDLLOVERRIDES` en `LD_PRELOAD` (GEMETEN, met een aanname).** Onze lader heeft een
algemeen drop-in-mechanisme (§4). Op **Linux** vertaalt het zich in Wine-specifieke middelen: een
`DllOverride` wordt een regel in `WINEDLLOVERRIDES` en een `Preload` wordt `LD_PRELOAD` van het
Proton-proces. Beide concepten bestaan op Windows niet: daar zet je het bestand naast de exe, of je
gebruikt de DLL-search order.
**Aanname:** dat dit de bedoelde werking van het mechanisme is, is uit de scripts en de documentatie
gelezen, niet op deze titel uitgeoefend — op deze route is geen drop-in nodig en `play-unity.sh` leest
`dropin.ini` niet (§4). Wie hier een drop-in wil plaatsen, moet dus eerst die twee punten oplossen.

**10.5 Virtuele apparaten en udev (GEMETEN).** Onze "gun" is op Linux een **virtueel absoluut
muisapparaat via `/dev/uinput`** (evdev), plus een **virtueel toetsenbord** voor munt, start en test:

| apparaat | id | waar het voor is |
|---|---|---|
| virtuele lightgun speler 1 | `1209:7202` | het richtpunt dat het spel als muis leest |
| virtuele lightguns spelers 2-4 | `1209:7203` … `1209:7205` | bestaan alleen als die speler een Move heeft |
| virtueel toetsenbord | `1209:72f0` | munt (5-8), start (1-4), test (0), Esc — de plugin leest toetsen |

In `/dev/input/by-id/` verschijnen die als `usb-RavenGun_Virtual_Lightgun…`. Het richtapparaat
(PS Move) hangt via **Bluetooth op hidraw**; de master-adressering gaat met een HID-`SET_FEATURE`, en
SteamOS geeft hidraw-toegang via zijn **eigen** udev-regel (`60-steam-input.rules` met `uaccess`) — wij
hebben daarvoor geen eigen regel hoeven te plaatsen. Onze virtuele gun zetten wij met
`LIBINPUT_IGNORE_DEVICE` bewust **onzichtbaar** voor de desktop en gamescope, anders beweegt de
muiscursor van de eigenaar mee tijdens het spelen.
**Windows-verschil:** Windows kent geen `uinput`. Je zou een virtueel invoerstuurprogramma nodig hebben
(zoals ViGEm) of een echte lichtgeweer-driver, en er is geen udev- of `uaccess`-laag. Wat je daar
tegenover staat: geen compositor om mee te vechten (zie 10.6).

**10.6 Gamescope en het scherm van de eigenaar (GEMETEN).** Wij gebruiken **gamescope** om een sessie te
maken zonder dat het scherm van de eigenaar bezet is: `gamescope --backend headless -W 1920 -H 1080`. De
game krijgt dan een Xwayland-display (`DISPLAY=:1`). Twee dingen die je hier tegenkomt:
* de virtuele toetsen van onze eigen `ravengunctl key` komen daar **niet** aan (ze gaan naar de
  compositor van de sessie, en die heeft geen venster); via gamescope's eigen Xwayland met `xdotool`
  **wel** (§7, §9);
* er is dus geen beeld om naar te kijken — een headless run kan het spel **niet** visueel beoordelen.
**Windows-verschil:** niet van toepassing; een Windows-sessie heeft altijd een desktop. Maar het is ook
de reden dat "op de tv" en "headless" bij ons twee verschillende soorten bewijs zijn.

**10.7 De instellingenlaag (GEMETEN).** Onze lader kent op Linux twee lagen: `patches/<Spel>/game.ini`
(meegeleverd) en `~/.local/share/ravengun/patches/<Spel>/game.ini` (gebruiker), waarbij de
gebruikerslaag **per sleutel** over de meegeleverde heen gaat. Zo pas je één game aan zonder de
meegeleverde waarden te raken, en één bestand weghalen zet het terug.
**Windows-verschil:** op Windows zou je met kopieën van profielbestanden werken of met de
gebruikersprofielen van de UI zelf; de gelaagdheid-per-sleutel is onze Linux-vorm daarvan.

---

### Herkomst en grenzen van dit verslag

Alles hierboven is gemeten op een Steam Deck (SteamOS) met Wine/Proton, op een NAS die alleen gelezen
is. Waar "GEMETEN" staat, is er een concreet cijfer, tijdstip, pad of offset bij; waar "AFGELEID" of
"aanname" staat, is het een gevolgtrekking uit die metingen. De tien-minutenrun is één run; de punten in
§8.4 en §9 zeggen precies wat daarin **niet** is uitgeoefend. Er staat niets in dit verslag over
hardwarematige beveiligings- of licentielagen; dat hoort niet in een publieke pagina en is er ook niet in
verwerkt.
