# Procedures voor de gebruiker (metingen fase 0 en fase 3)

Dit document beschrijft wat de gebruiker doet bij de opnamen en de magnetometerkalibratie. De commando's draaien op de SteamOS-host. De daemon `ravengund` is de enige eigenaar van de Move en de camera; alle commando's hieronder praten via de socket met die daemon. Opnamen bevatten alleen getallen (HID-rapporten, de positie en straal van de bol, LED-wissels en labels), nooit camerabeelden. Ze staan in `~/.local/state/ravengun/recordings/` (map 0700, bestanden 0600). Het Bluetooth-adres staat er alleen geredigeerd in.

## Voorbereiding (elke sessie, ongeveer 1 minuut)

1. Start de daemon in een terminal: `./build/ravengund`. Draait er al een `ravengund`, dan gebruik je die.
2. Druk op de PS-knop van de Move. Binnen 2 seconden verbindt hij via Bluetooth.
3. Controleer met `./build/ravengunctl status`:
   - "Move: verbonden (bluetooth), ZCM1";
   - "hidraw-tap: open" (dan zijn de interne tijdstempel en de temperatuur beschikbaar);
   - "Tijdbasis: ... gekoppeld" (na ongeveer 5 seconden);
   - "Camera: streaming" en, als de bol in beeld is, "bol geldig".
4. Leg de Move 10 seconden stil op tafel. Dan schat de daemon de gyro-bias ("Gyro-bias: estimated (bron still)" in de status). Is er een bewaarde bias van een eerdere sessie (`~/.local/state/ravengun/gyro-bias/<sleutel>.json`), dan staat de status meteen op "restored" en meldt het opstartlog "gyro-bias hersteld"; de 10 seconden rust vervangen hem daarna door een verse meting. Een koude Move (net aangezet, ruwe temperatuur `tc` rond 1850) heeft een andere z-bias dan een warme (rond 2050): leg hem daarom ook bij een nieuwe sessie even op tafel, en laat hem liefst de eerste minuut stil liggen terwijl hij opwarmt, zodat de daemon de temperatuurhelling kan leren. Tijdens het spelen volgt de daemon de bias verder met die helling en met rustige momenten (quasi-stil); de status toont de bron ("still", "quasi-still" of "restored"), de helling en hoeveel bewijs er is.

## Opname "richten met de hand" (fase 0; ongeveer 10 minuten)

Doel: een echt richtspoor met beweging, stilstand en snelle stops, waarop de oriëntatieschatters later vergeleken worden (Mahony tegenover ESKF, fase 4). De opname zelf duurt 5 minuten.

1. De speler staat op de gewone speelplek, ongeveer 2,5 m van de tv, met de Move los in de hand (of in de Sharp Shooter, als dat de bedoeling van de sessie is; noem dat dan in de tag, bijvoorbeeld `richten-sharpshooter`). De bol moet zichtbaar zijn voor de camera.
2. Start de opname: `./build/ravengunctl record --tag richten-hand --duration 300`. Het commando toont de voortgang en stopt zelf na 300 seconden. Ctrl-C stopt eerder; het bestand blijft dan geldig.
3. Wat de speler doet, in deze volgorde:
   - 10 seconden stil richten op het midden van het scherm;
   - langzaam naar de vier hoeken en terug naar het midden, op elke hoek 3 seconden stilhouden;
   - een minuut vrij richten zoals in een spel, met snelle zwaaien naar links en rechts en een harde stop;
   - een paar keer snel van richting wisselen (heen en weer);
   - 10 seconden stil op het midden;
   - de rest van de tijd vrij richten, met af en toe de trekker;
   - de laatste 10 seconden de Move stil op tafel leggen (een eindreferentie zonder hand).
4. Labels: wie de opname begeleidt, kan op elk moment een label zetten met `./build/ravengunctl record --mark "midden"` (of "hoeken", "snel", "op tafel"). Labels maken de analyse eenvoudiger, maar zijn niet nodig.
5. Wiebelopname voor de tijdsuitlijning van camera en gyro (1 minuut): `./build/ravengunctl record --tag wiebel --duration 60`. De speler houdt de bol in beeld en draait de pols snel heen en weer (ongeveer 2 keer per seconde, met het draaipunt in de hand), 3 keer 10 seconden, met 5 seconden pauze ertussen.
6. Na afloop toont het commando de bestandsnaam. Controle:
   - `./build/ravengun-analyze replay <bestand>` (deterministisch afspelen; de digest is bij elke herhaling gelijk);
   - `./build/ravengun-analyze timebase <bestand>` (stempel, klokkoppeling, bias per subsample).

## Magnetometerkalibratie in de Sharp Shooter (fase 3; ongeveer 5 minuten)

Doel: de hard- en soft-iron-correctie van de magnetometer, inclusief de vaste metalen delen van de Sharp Shooter. De kalibratie geldt per controller en per houder. Ze wordt alleen opgeslagen als ze de kwaliteitseisen haalt (`~/.config/ravengun/magnetometer/<sleutel>.json`, atomisch geschreven, met versie). De magnetometer wordt nog niet in de fusie gebruikt; dat volgt pas na de beslissing in fase 3.

1. De Move zit in de Sharp Shooter. Geen USB-kabel aan de Move. De LED staat op de speelkleur (cyaan, de standaard). De speler staat op de speelplek, minstens 1 meter van metalen tafels, radiatoren, speakers en de subwoofer.
2. Neem de ruwe data tegelijk op, zodat de fit later offline herhaald kan worden: `./build/ravengunctl record --start --tag magcal-sharpshooter --duration 600`.
3. Start de kalibratie: `./build/ravengunctl calibrate mag --holder sharp_shooter`. Het commando toont "dekking %" en het aantal samples.
4. Wat de speler doet (60 tot 90 seconden): het geweer langzaam in alle richtingen draaien. Achtjes in de lucht, de kolf omhoog en omlaag, de loop naar het plafond en naar de vloer, en rollen om de loop, ook helemaal ondersteboven. Langzaam draaien, zonder te schudden of te zwaaien, want versnelling verstoort de dipcontrole. Blijf zelf op dezelfde plek staan.
5. Het commando stopt zelf zodra de dekking minstens 80 % is en er minstens 300 samples zijn (anders na 180 seconden). Daarna fit de daemon en toont hij het resultaat:
   - "GESLAAGD, opgeslagen in ...": klaar;
   - "NIET GESLAAGD": de daemon noemt concreet wat er mis is, bijvoorbeeld "te weinig dekking" (meer richtingen, ook ondersteboven) of "veld niet homogeen" (verder van metaal en speakers draaien). Herhaal dan stap 3 en 4.

   Ctrl-C breekt af zonder iets op te slaan. Met `--start`, `--status`, `--stop` en `--abort` is de kalibratie ook in losse stappen te bedienen (bijvoorbeeld vanuit de web-UI; de voortgang staat in `GET /api/magnetometer` en in het veld `progress` van de SSE-stroom `/api/stream`).
6. Veldcheck (ongeveer 1 minuut). De speler richt op het midden van het scherm en houdt het geweer 3 seconden stil, terwijl de begeleider `./build/ravengunctl calibrate mag --check --duration 3` uitvoert. Dat gebeurt op drie plekken:
   - op de speelplek;
   - 0,5 meter naar links;
   - 0,5 meter naar rechts.

   Daarna op de speelplek nog twee keer: met de pomp naar achteren getrokken, en met de trekker ingedrukt. Elke check moet "IN ORDE" melden (de norm binnen 5 % en de dip binnen 2 graden van de kalibratie). "NIET IN ORDE" betekent dat de magnetometer op die plek of in die stand onbetrouwbaar is. Noteer welke.
7. Stop de opname: `./build/ravengunctl record --stop`.
8. Offline controle: `./build/ravengun-analyze magfit <bestand>` (dezelfde fit op de opname) en `./build/ravengun-analyze mag <bestand> --cal ~/.config/ravengun/magnetometer/<sleutel>.json` (de verstoringsdetector over de hele opname).

## Meetsessie magnetometerverstoringen (fase 3; ongeveer 10 minuten, optioneel)

Doel: meten wat de omgeving en de Sharp Shooter met de magnetometer doen. Dit vraagt een geslaagde kalibratie (hierboven). Leg het geweer steeds stil neer, of laat het stil vasthouden, met de loop naar het scherm.

1. `./build/ravengunctl record --start --tag magstoring --duration 900`.
2. Per stap 30 seconden stil, met eerst een label (`./build/ravengunctl record --mark "<label>"`):
   - "referentie": tv aan, geen muziek;
   - "pomp": pomp naar achteren en vasthouden;
   - "trekker": trekker ingedrukt houden;
   - "muziek": muziek of een game met veel bas op normaal volume;
   - "tv uit": tv uit;
   - "dicht bij speaker": het geweer op 50 cm van de speaker of soundbar.
3. `./build/ravengunctl record --stop`, daarna `./build/ravengun-analyze mag <bestand> --cal <kalibratie>`. Het verschil per segment staat in counts en in % van de veldsterkte.

## Door de agent zelf uitgevoerd (ter referentie)

De opnamen "5 minuten stil" (tag `stil5min`) en "LED-reeks bij stilstand" (tag `ledmag`: steeds 20 seconden cyaan, uit, rood, uit, groen, uit, blauw, uit, cyaan, wit, uit en cyaan) vragen geen handeling van de gebruiker. De Move moet alleen verbonden zijn en stil liggen. `ravengunctl led <kleur>` wisselt de LED-kleur. Let op: de tracker blijft de tint van de opstartkleur zoeken, dus de bol is bij een andere kleur niet in beeld.

## Live proef fase 4: magnetometer-yaw en de Move als lightgun (ongeveer 15 minuten)

Voorwaarden: de daemon is na de merge opnieuw gebouwd en gestart (`ravengund`), de Move zit in de Sharp Shooter, de magnetometerkalibratie staat in `~/.config/ravengun/magnetometer/` (oude bestanden worden automatisch omgezet: de dip wordt dan positief, ongeveer +60°). De orchestrator bedient de commando's; de gebruiker richt.

1. Controle (`./build/ravengunctl status`): "Magnetometer: gekalibreerd ..., dip +60 graden (ref +60)" op de speelplek, toestand ok; "Yaw-correctie: mahony, schaduw". Op tafel mag "disturbed" staan (gemeten: het veld bij de tafel is anders).
2. Nulmeting IMU-only (3 minuten): `./build/ravengunctl record --start --tag fase4-mahony --duration 400`. De gebruiker richt 10 s stil op het midden van de tv (label `./build/ravengunctl record --mark stil_midden_1`, na 5 s `--mark stil_midden_1_einde`), richt 30 s vrij, weer 10 s midden (`stil_midden_2`), enzovoort tot 5 keer, en de laatste minuut vrij. Noteer per keer hoe ver de cursor van het midden staat (gevoel/schatting). Daarna `--stop`.
3. Omschakelen: `./build/ravengunctl fusion mahony_mag`. Status: "Yaw-correctie: mahony_mag, wacht op vergrendeling" en na 2 tot 10 s "actief"; `yaw_source` in de SSE/watch wordt `magnetometer`.
4. Zelfde opname als stap 2 met tag `fase4-mahony-mag`. Let op: geen zichtbare sprongen of een vizier dat in stilstand zelf wegglijdt. `correction_rate_deg_s` in stilstand hooguit 0,02 (0,08 vlak boven de bewegingsgrens), `vertical_bias_deg_s` binnen ±0,3.
5. Offline: `./build/ravengun-analyze fusion <opname> --cal ~/.config/ravengun/magnetometer/<sleutel>.json` voor beide opnamen. Winst = kleinere "afwijking van de start" en kleinere helling bij mahony_mag. Pas bij aantoonbare winst `fusion.mode` zo laten (het commando bewaart het al); anders `ravengunctl fusion mahony`.
6. Lightgun (alleen met de gun-binding in TeknoParrot; eerst de daemon, dan de game): `./build/ravengunctl arm`. Dit maakt ook het virtuele toetsenbord `RavenGun Virtual Keyboard` aan; een los draaiend `vinput kbd` moet dan uit. Controle zonder game: `./build/ravengunctl output --json` toont `live.positions` oplopend, `buttons_down` bij het overhalen, `keys_down` bij een toetsknop, en `map` (de actieve indeling). De standaardindeling (Halo):
   - T = trekker (analoog), Move-knop = grenade, Kruis = reload.
   - Driehoek en Select = toets 5 (coin), Vierkant en Start = toets 1 (start P1), Cirkel = niets.
   - PS = altijd recenter (nieuw midden, log "recenter met de PS-knop"), ook als de uitvoer niet gewapend is.
   De indeling staat in `config.json` onder `gun_buttons.map` (zie docs/architecture.md, "Knoppenindeling"); na een wijziging de daemon herstarten (orchestrator). Bol afdekken of Move uitzetten: na 250 ms zijn alle knoppen en toetsen los (`buttons_down` en `keys_down` leeg) en beweegt de cursor niet meer.
   - Sharp Shooter (alleen als EXT verbonden is): de trekker is het fire-bit van de uitbreiding (byte 0, 0x40; T-hysterese is dan niet meer de trekker); de pomp (`pump`) is T zonder fire-bit en doet standaard reload; `ss_rl` (het RL-bit, 0x80) doet ook reload maar is in de praktijk nooit gezien; `ss_fire`, `sel1`, `sel2`, `sel3` = niets. Gemeten in de Halo-opname van 2026-10-08: 131 schoten en 5 pompen, zie `./build/ravengun-analyze buttons <opname>` (schoten, volgorde van T en fire, pompen) en docs/architecture.md, "Uitbreidingsconnector". Controle: `./build/ravengunctl status` toont "Uitbreiding: ext verbonden: ja [..]" en `watch` de ruwe bytes (byte 0: 0x40 fire, 0x01/0x02/0x04 vuurstand) en de knoppen `pump` en `ss_fire`. Een pomp geeft geen schot en een schot geen pomp. Met `sharp_shooter.extension` = `none` wordt de uitbreiding genegeerd en is T met hysterese weer de trekker.
7. In Halo: richten op het midden, overhalen, zwaaien; let op vertraging (1€ met beta 10 is de referentie) en drift. `./build/ravengunctl disarm` aan het eind.

## Driftmeting tijdens het spelen

1. Neem op terwijl de gebruiker Halo speelt: `./build/ravengunctl record --start --tag halo-drift --duration 600`. Elke keer dat het vizier niet meer op het midden staat richt de gebruiker op het midden van het scherm en drukt PS (dat is gratis een meetpunt). Zet eventueel `record --mark pomp` bij het testen van de pomp.
2. Offline: `./build/ravengun-analyze drift <opname> [--mode mahony_mag --cal ~/.config/ravengun/magnetometer/<sleutel>.json]`. Lees per interval de drift in graden per minuut naast gedraaide hoek, maximale snelheid, verloren rapporten en gyro-verzadiging; de samenvatting geeft mediaan, p90, de cumulatieve drift van de eerste tot de laatste PS-druk en de correlaties. De opname gaat door de huidige biasschatter (modus new); vergelijk met `--bias-mode legacy` (oud gedrag, alleen stilstand) en, voor een warme start, `--bias-init X,Y,Z` (graden/s) of `--bias-init warm` (twee ronden, circulair). De eerste regel van de uitvoer ("Gyro-bias: ...") toont modus, start en eindbias. Parameters bijstellen zonder hercompileren: `--bias-set quasi_gate_dps=1.2`, `temp_min_range=25`, `temp_comp=0` enzovoort.
3. Een lage correlatie bij weinig intervallen zegt weinig: doe er minstens 10. De per-interval-mediaan wordt begrensd door het richten van de speler (1 tot 2 graden per PS-druk, bij intervallen van 13 s al 6 tot 8 graden/min); kijk daarom ook naar de cumulatieve drift en wissel de PS-drukken af met intervallen van minstens 30 s.
4. Begin de speelsessie met de Move 10 tot 60 s stil op tafel (de daemon leert de temperatuurhelling en de bias) en druk pas daarna PS; doe je dat niet, noteer het in de tag (bijvoorbeeld `halo-drift-koud`).
5. Controle van de bewaarde bias: `./build/ravengunctl status` ("Gyro-bias: ... bewaard: hersteld bij start (tc ..); N keer bewaard in deze run"). Het bestand staat in `~/.local/state/ravengun/gyro-bias/`; verwijderen mag altijd (de volgende sessie begint dan koud). `ravengund --no-bias-store` zet laden en bewaren uit, `--bias-mode legacy` schakelt de nieuwe schatter uit.

## Hulp voor het koppelen installeren (eenmalig, met sudo)

Nodig om vanuit de app of `ravengunctl pair` te koppelen. Niet nodig voor spelen of verwijderen. Zie `docs/decisions.md` ("Koppelen zonder terminal").

1. Controleren wat er gebeurt: `sudo ~/projects/ravengun/scripts/install-bt-helper.sh --dry-run`.
2. Installeren: `sudo ~/projects/ravengun/scripts/install-bt-helper.sh --apply`. Dit zet `scripts/ravengun-bt-helper` als `/opt/ravengun/bin/ravengun-bt-helper` (root:root 0755) en schrijft `/etc/sudoers.d/ravengun-bt-helper` (0440, eerst met `visudo -cf` gecontroleerd) die alleen dat pad zonder wachtwoord toestaat.
3. Controleren zonder sudo: `~/projects/ravengun/scripts/install-bt-helper.sh --check` (verwacht: "Hulp actueel" en "sudo: toegestaan zonder wachtwoord").
4. Terugdraaien: `sudo ~/projects/ravengun/scripts/install-bt-helper.sh --rollback`.

Na een wijziging van `scripts/ravengun-bt-helper` stap 2 opnieuw. De hulp logt naar de journal (`journalctl -t ravengun-bt-helper`) en naar `/var/log/ravengun-bt-helper.log`.

Daarnaast (nieuw voor speler 2 tot 4) moet de udev-regel opnieuw: `sudo ~/projects/ravengun/scripts/udev-ravengun.sh --apply` (controle zonder sudo: `scripts/udev-ravengun.sh --check`). Zonder die regel heeft de gun van speler 2 geen by-id-link en geen leesrecht, en beweegt hij de desktopcursor.

## Tweede Move koppelen en met twee spelers testen (ongeveer 15 minuten)

1. App, tabblad Controllers, Nieuwe controller koppelen; de nieuwe Move via USB aansluiten; de stappen volgen tot "Klaar". Of in een terminal: `ravengunctl pair`.
2. `ravengunctl controllers`: de nieuwe Move staat er als speler 2, verbonden, met LED 255,0,255 (magenta).
3. Knipperen (app) of `ravengunctl controller <sleutel> --blink`: de juiste Move knippert.
4. `ravengunctl status --player 2` en `ravengunctl watch --player 2`: de aim van speler 2 beweegt met de tweede Move, die van speler 1 niet (`ravengunctl watch`).
5. `ravengunctl arm`, dan `ls -l /dev/input/by-id/ | grep RavenGun`: drie links (toetsenbord, Lightgun, Lightgun_P2). `ravengunctl output --json`: `players[1].device.product` is 7203 en `active` is true.
6. `ravengunctl camera --json`: onder `tracker.players` zien speler 1 en 2 elk hun bol (`last.valid`) met beide Moves stil in beeld.
7. In een game met twee spelers (eerst met `scripts/tp-add-game.py --force` de bindingen van speler 2 laten zetten): Start speler 2 met toets 2 (Start of Vierkant op de tweede Move), schieten met elke Move op zijn eigen vizier.
8. Verwijderen testen kan met een Move die je daarna opnieuw koppelt: Controllers, Verwijderen, "instellingen bewaren"; daarna opnieuw koppelen: de eigen instellingen zijn terug.

## Tomb Raider en Rabbids Hollywood zonder TeknoParrot spelen (route 1; ongeveer 20 minuten, eerste keer)

Eenmalig (al gedaan op deze machine): de officiële DemulShooter-release staat in `~/.local/share/ravengun/demulshooter/v17.9/` (zip apart in `zip/`, `PROVENANCE.txt` met herkomst en SHA-256). Opnieuw nodig na een schone installatie: `curl -L -o DemulShooter_v17.9.zip https://github.com/argonlefou/DemulShooter/releases/download/v17.9/DemulShooter_v17.9.zip`, de SHA-256 uit `PROVENANCE.txt` controleren en alleen `Unity/TombRaider/*` en `Unity/RabbidsHollywood/*` uitpakken (geen enkel `.exe` uitvoeren). De plugin zit bewust niet in de repo (geen licentie).

1. De daemon moet de nieuwe opdrachten kennen (`aagames`, `key`): na het mergen van deze branch `systemctl --user restart ravengund` (een oudere daemon zonder deze opdrachten laat de game wel starten, maar `play-unity.sh` meldt dan dat de route niet kon starten; gebruik zolang `build/ravengun-aagames-probe`).
2. Starten: in de app, tabblad Games, Tomb Raider of Rabbids Hollywood; of in een terminal `scripts/play-halo.sh --profile TombRaider` (of `RabbidsHollywood`; hetzelfde als `scripts/play-unity.sh --game TombRaider`). Het script zet de lokale view en de eigen prefix klaar (de eerste keer maakt GE-Proton de prefix, een halve minuut), wacht op de Move, wapent de uitvoer, start de game schermvullend op de tv en vertelt de daemon welke game actief is.
3. Munt en start: Tomb Raider en Rabbids tellen munten in kwarten ("1/4 credits"). Standaardtoetsen: 5 tot 8 = munt speler 1 tot 4, 1 tot 4 = start, 0 = test, Esc = afsluiten. Koppel Move-knoppen daaraan in de app (knoppenindeling, `key:5` en `key:1`), of druk ze vanuit een terminal: `ravengunctl key 5` (munt, viermaal bij Rabbids) en `ravengunctl key 1` (start).
4. Controle: `ravengunctl status` toont de regel "Unity-game: ... VERBONDEN met de game, 120 pakketten/s". Schiet met de trekker: de Move trilt bij elk schot van de game, `ravengunctl status` telt de schoten.
5. Het vizier in de game moet onder de bol/richting van de Move staan. Zit het er systematisch naast: `scale_x`, `scale_y`, `offset_x`, `offset_y` van die game in `packaging/aagames-games.json` (daemon herstarten, of `--aagames-config`), of de gewone `ravengunctl set-output` voor alles. De Y-oorsprong (`y_origin`) is voor beide games `bottom` en gemeten.
6. Stoppen: Esc in de game, of de Stop-knop in de app, of `scripts/play-halo.sh --stop` (zelfde als `scripts/play-unity.sh --stop`). De daemon stopt de route zelf een paar seconden nadat de game weg is.
7. Zonder daemonherstart proberen: `build/ravengun-aagames-probe --game TombRaider` (aim van de echte Move via de draaiende daemon; `--key 5@1 --key 1@3` voor munt en start; `--source fixed --u 0.5 --v 0.5 --trigger` voor een vast punt).
8. Bij een probleem: `~/.local/state/ravengun/logs/unity-<Game>-*.log` (Proton), `~/.local/share/ravengun/runtime/views-aagames/<Game>/BepInEx/LogOutput.log` (staat de regel "TCP Server is listening on Port 33610", dan werkt de plugin), `ss -ltnp | grep 33610`.

## Een game met de IO-brug starten (tp-shm) en de brug controleren

De route per game staat in `packaging/io-routes.json` (evdev, tp-shm, unity-tcp). `scripts/play-halo.sh --profile PROFIEL` kiest die route zelf; `--route R` overschrijft hem voor een start. De app (Games-lijst) start hetzelfde script.

- Starten met tp-shm: `scripts/play-halo.sh --profile TC5` (of `--route tp-shm` voor een game waarvan de standaard evdev is, bv. `--profile Terminator --route tp-shm`). Het script start de brug (`ravengunctl iobridge start PROFIEL --route tp-shm`), maakt `/dev/shm/ravengun_JvsState` en laat `scripts/tp-shm-swap.sh` in de unit de helper van TeknoParrotUi vervangen. Log: `~/.local/state/ravengun/logs/tpshm-*.log` ("pipehelper.exe pid N vervangen" en "shm mirror active ... [created fresh]").
- Controleren: `ravengunctl status` toont de regel "IO-brug" (route, game, dialect, indeling, laatst geschreven bytes 8..27, P1-aim en knoppen); `ravengunctl iobridge status` alleen die regel; `xxd /dev/shm/ravengun_JvsState` het bestand zelf. Testpunten houden: `ravengunctl test-pattern grid --dwell 20000` (een puntpatroon houdt zichzelf niet vast terwijl de Move live is). Knoppen: `output-button trigger|reload|grenade|aux1|aux2 down|up`, munt `key 5`, start `key 1`.
- Daemon herstarten tijdens een spel: `systemctl --user restart ravengund`. De brug schrijft neutrale invoer, de nieuwe daemon pakt de route weer op (`ravengunctl iobridge status` meldt "1x hervat na een herstart"), wapent de uitvoer opnieuw en het spel loopt door. Een vastgehouden knop laat los.
- Stoppen: `scripts/play-halo.sh --stop` stopt ook de brug (invoer neutraal, bestand weg).
