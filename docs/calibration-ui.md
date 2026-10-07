# Kalibratie-UI op de tv

De kalibratie draait niet op een telefoon of in het beheerscherm, maar fullscreen op de tv van de game, en wordt bediend met het geweer zelf. De gebruiker heeft geen toetsenbord of muis. De pagina is `/calibrate`, geserveerd door dezelfde webserver van `ravengund` (zelfde poort, ingebakken bestanden, geen CDN, zelfde Host-, Origin-, CSRF- en tokenregels als de rest, zie `docs/web-ui.md`).

## Starten en sluiten

    ravengunctl calibrate-ui            # opent het kalibratiescherm op de tv
    ravengunctl calibrate-ui --status   # draait het?
    ravengunctl calibrate-ui --close    # sluit alleen het kioskproces

In de web-UI staat hetzelfde als knoppen op de pagina Kalibratie ("Kalibreren op de tv" en "Kalibratiescherm sluiten"). Beide gaan via het IPC-commando `calibrate_ui` (open, close, status) naar de daemon, die de browser start. Er gaat nooit een pad, programma of URL van buiten mee: de URL is `http://127.0.0.1:<webpoort>/calibrate`.

De browser is Brave als flatpak (`com.brave.Browser`), gestart met `--kiosk --app=URL` en een eigen, apart profiel in `~/.local/state/ravengun/kiosk` (via `--user-data-dir`, met `--filesystem` voor alleen die map). Het profiel van de gebruiker wordt niet geraakt. Bij een nieuw profiel worden de meldingsbalken van Brave (analyse, vertalen) uitgezet en staat de taal op Nederlands, zodat de pagina de hele tv vult. De sessievariabelen komen van de kiosk zelf, omdat de daemon niet per se in de grafische sessie draait: `WAYLAND_DISPLAY=wayland-0`, `XDG_RUNTIME_DIR=/run/user/<uid>`, `DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/<uid>/bus`, `DISPLAY=:0` en `XAUTHORITY`, dat uit de opdrachtregel van `kwin_wayland` (`--xwayland-xauthority`) wordt gelezen.

Sluiten zoekt in `/proc` naar processen van de eigen gebruiker waarvan de opdrachtregel exact `--user-data-dir=<kioskprofiel>` bevat, stuurt SIGTERM en na ongeveer 2,5 seconde SIGKILL voor wat blijft staan. Andere Brave-vensters en -profielen blijven dus draaien. Opstarten en sluiten duren enkele seconden; `ravengunctl` wacht daarom tot 10 seconden op het antwoord.

## Viewport en uiterlijk

De pagina tekent op een vast podium van 1920 bij 1080 beeldpunten en schaalt dat met letterbox naar het venster. De doelen staan dus op exacte tv-coördinaten, ongeacht wat de browser meldt. Het scherm is donker, de letters zijn groot (leesbaar op 2 tot 3 meter) en de muisaanwijzer is onzichtbaar. Bovenin staan de stap en twee statuspillen (Move verbonden, daemon bereikbaar), onderaan een voetbalk met wat elke knop nu doet. Valt de verbinding met de daemon weg, of ontbreekt de Move bij een stap die hem nodig heeft, dan komt er een overlay met uitleg.

## Bediening met het geweer

Trekker of Move-knop is volgende of bevestig. Cirkel is terug of opnieuw. Select slaat een stap over. Start breekt af (op het beginscherm sluit het de kalibratie). Alles gebeurt via de knopflanken in de SSE-stroom `/api/stream`. De pagina zet zelf nooit invoer om in game-acties: er wordt niets naar de uitvoer of de virtuele lightgun gestuurd.

Debounce: één druk is één actie. De SSE-stroom is beperkt tot 30 frames per seconde en het sample bevat alleen de flanken van dat ene sample, dus een korte druk kon tussen twee frames verdwijnen. Daarom stuurt de daemon nu in elk frame het veld `edges` mee, met cumulatieve tellers per knopbit (`press` en `release`, 32 getallen). De pagina leidt de drukken af uit het verschil met het vorige frame en mist er dus geen. Na een geaccepteerde actie negeert de pagina 350 ms lang alle drukken (contactdender, dubbele druk). De eerste stand na het (her)verbinden is alleen een basislijn, zodat drukken die vóór het verbinden of tijdens een onderbreking gebeurden nooit alsnog worden uitgevoerd. Een niet-toegestane actie in een stap wordt genegeerd en start geen wachttijd. Bij meerdere knoppen in één frame geldt de voorrang afbreken, terug, overslaan, volgende. Deze logica staat in `src/ravengund/web/calibrate-core.js`, los van de weergave, en heeft eigen unittests.

In de stap Knoppen (waar elke knop zelf een meting is) bedient alleen lang indrukken (1,5 seconde) de pagina; korte drukken lichten alleen een tegel op. Een knop die nog ingedrukt is van de vorige stap telt pas na loslaten.

## De stappen

Het beginscherm toont de vier stappen. Daarna gaat het lineair; Cirkel gaat een stap terug en Select slaat een stap over.

### 1. Magnetometer

Uitleg: het geweer vasthouden zoals bij het spelen (de Move zit in de Sharp Shooter), minstens 1 meter van metaal, speakers en de tv, langzaam alle kanten op draaien (achtjes, loop omhoog en omlaag, rollen om de loop, ook ondersteboven), minstens 40 seconden en niet schudden. Trekker start de kalibratie (`POST /api/calibrate/mag/start` met houder `sharp_shooter`).

Tijdens het draaien staat links een bol, twee keer getekend (voorkant en achterkant), op een canvas zonder externe bibliotheken. De bol bestaat uit de 20 richtingen van `SphereCoverage` in de daemon. Een vlak is groen als het minstens 3 samples heeft (gedekt), oranje als het begonnen is, en donker als het nog leeg is. De bol draait langzaam; de ring is de huidige meetrichting, met een spoor van de laatste richtingen. De richtingen zijn genormaliseerd op de eigen omvang van de meting (zoals de dekking in de daemon) en dus niet de echte sensorassen.

Rechts staan het voortgangspercentage (de zwakste van dekking ten opzichte van 80 %, tijd ten opzichte van 40 seconden en samples ten opzichte van 300), de dekking, de tijd, het aantal metingen, een tempo-indicator en een Nederlandse hint. Het tempo komt uit de gyrosnelheid van de stroom (bias afgetrokken, glad gemaakt met een tijdconstante van 250 ms): onder 8 graden per seconde "Draai verder", tot 120 "Goed tempo", tot 220 "Iets rustiger" en daarboven "Veel te snel". De hints reageren op tempo, dekking en tijd (bijvoorbeeld "Draai ook ondersteboven" als de tijd om is maar de dekking niet).

Trekker rondt af zodra de daemon `ready` meldt (dekking en samples op doel en minstens 40 seconden); eerder krijgt de gebruiker de melding dat het nog niet genoeg is. Dan volgt `POST /api/calibrate/mag/stop` en het resultaat: geslaagd (groen), marginaal maar opgeslagen (oranje, restfout tot 4 procent en dip-spreiding tot 8 graden) of mislukt (rood). Onder het oordeel staan de `problems` van de daemon letterlijk, aangevuld met een korte handelingstip, plus de meetwaarden (dekking, restfout, dip-spreiding, duur). Cirkel is opnieuw, trekker is verder. Start tijdens het draaien doet `POST /api/calibrate/mag/abort`. Wordt de pagina herladen terwijl er een sessie loopt, dan gaat ze daar verder.

### 2. Stil richten (herhaalbaarheid)

De pagina toont een doelkruis in het midden van de tv (960 bij 540). Trekker start `POST /api/calibrate/rec/start` (tag `stil-herhaalbaarheid`, maximaal 900 s); daarna zeven metingen: vijf keer in het midden, een keer een halve meter naar links en een keer een halve meter naar rechts van het midden. Per meting: richt op het kruis, trek de trekker, houd 5 seconden stil met een zichtbare aftelling.

Tijdens het aftellen kleurt het kruis groen als de gyro onder 5 graden per seconde blijft, anders oranje, met een balkje en de tekst "Stil genoeg" of "Te onrustig". De eerste seconde (trekkerdruk, uitzwaaien) telt niet mee. De meting slaagt als minstens 85 procent van de samples stil is en er geen uitschieter boven 20 graden per seconde is; anders volgt "Opnieuw" en wordt dezelfde meting herhaald. Cirkel doet de vorige meting opnieuw.

Labels in de opname (`record mark`, een punt in de tijd):
- bij de trekkerdruk: `stil_midden_1` tot `stil_midden_5`, `stil_links_1`, `stil_rechts_1`;
- aan het einde: dezelfde naam met `_einde` (geslaagd) of `_onrustig` (afgekeurd);
- bij een nieuwe poging komt `_poging2`, `_poging3` enzovoort tussen de naam en het achtervoegsel (`stil_midden_1_poging2`, `stil_midden_1_poging2_einde`).

Het segment loopt dus van het label zonder achtervoegsel tot het label met `_einde`. De eerste seconde van het segment is uitzwaaitijd en hoort niet bij de stilstandanalyse. Met `tools/mag_segments.py` zijn de segmenten per kompasrichting te analyseren. Aan het eind wordt de opname gestopt (`rec/stop`).

### 3. Knoppen

Live: negen tegels voor de Move-knoppen (Trekker, Move-knop, PS, Start, Select, Driehoek, Cirkel, Kruis, Vierkant) die oplichten zolang de knop ingedrukt is, een tegel blijft gemarkeerd als hij gezien is, en een balk voor de analoge trekker (0 tot 255). Staat er al een mapping in de configuratie, dan staat de Sharp Shooter-naam onder de tegel.

Vastleggen: lang indrukken van de trekker start de meting. Voor elke fysieke knop van de Sharp Shooter (trekker, pomp, driehoek, cirkel, kruis, vierkant, start, select, move, ps) staat er "Druk nu op ..." met een timer van 15 seconden. De pagina verzamelt 200 ms lang alle nieuwe drukken: precies één Move-knop wordt vastgelegd (de mapping); meerdere tegelijk geeft "probeer opnieuw"; geen druk is overgeslagen. Lang indrukken van Start breekt af (behalve als Start zelf aan de beurt is). Het resultaat gaat met `POST /api/calibrate/buttons` naar de daemon en wordt bewaard in `config.json` onder `sharp_shooter.buttons` (rol naar Move-knop, bijvoorbeeld `"pomp": "move"`). Een lege waarde wist de koppeling.

### 4. Scherm kalibreren (fase 5, plaatshouder)

De negen doelen van `docs/aim-fusion-design.md` 4.4 staan op de exacte tv-coördinaten: een 3 bij 3 raster op 10, 50 en 90 procent van de breedte en hoogte, dus op (192, 108), (960, 108), (1728, 108), (192, 540), (960, 540), (1728, 540), (192, 972), (960, 972) en (1728, 972) in het podium van 1920 bij 1080. Een rand rond het scherm laat overscan zien. De tekst zegt dat de berekening later komt. Dit is bedoeld om de layout op de echte tv te controleren; er wordt niets gemeten.

### Klaar

Een samenvatting en trekker sluit het kalibratiescherm (`POST /api/calibrate/ui` met `close`).

## Daemon-kant

Nieuw in de daemon:
- `progress.mag_calibration` (SSE, 5 Hz) heeft nu ook `cells` (20 tellingen per richting, indeling van `SphereCoverage::directions()`), `cell_min_count` (3), `spread` (0 tot 1), `dir` (laatste meetrichting) en `gyro_dps` (glad gemaakte rotatiesnelheid). `MagCollector::detail()` levert dit.
- Het SSE-frame heeft `edges` met cumulatieve flankentellers.
- IPC-commando's `set_buttons` (een of meer rollen, waarde een Move-knop of leeg) en `calibrate_ui` (action open, close of status).
- Het gedeelde kioskmodule `src/ravengun/kiosk/` (profiel, argumenten, het zoeken van `kwin_wayland`-xauthority, processen vinden en sluiten).

Endpoints, allemaal met de standaardbeveiliging (Host, Origin, Sec-Fetch-Site, Content-Type `application/json`, CSRF-token; in LAN-modus het token; onbekende velden en verkeerde typen geven 400):
- `POST /api/calibrate/mag/start` (optioneel `holder`), `/mag/stop`, `/mag/abort`;
- `POST /api/calibrate/rec/start` (optioneel `tag`, `duration_s`, `max_mb`), `/rec/mark` (`label`), `/rec/stop`;
- `POST /api/calibrate/buttons` (rollen als sleutels);
- `GET /api/calibrate/ui` (status) en `POST /api/calibrate/ui` met `{"action":"open"}` of `{"action":"close"}`.

De rate-limits blijven gelden: de SSE-stroom blijft op hoogstens 30 frames per seconde, de voortgang van de daemon op 5 Hz, en de lichaamsgrens van 4 KiB per verzoek.

## Voorbeeldmodus

`/calibrate?demo=<stap>` toont een stap met vaste voorbeeldgegevens, zonder daemon-gegevens. De stappen heten `welcome`, `mag_intro`, `mag_run`, `mag_result`, `still_intro`, `still_aim`, `still_count`, `still_done`, `btn_live`, `btn_map`, `btn_done`, `screen` en `done`. Dit is bedoeld voor schermcontrole en screenshots (headless of op de tv); in de voorbeeldmodus start de pagina de stroom niet en stuurt ze niets naar de daemon.

## Tests

- `ravengun_web_tests`: de pagina en assets (CSRF-token ingevuld, geen externe bronnen, geen inline stijl of code, CSP-header), de omzetting van alle endpoints naar IPC-regels, ongeldige invoer die de backend nooit bereikt, en per POST-endpoint de weigeringen (zonder CSRF, vreemde Origin, cross-site, verkeerd Content-Type, vreemde Host, LAN zonder token). Ook de helpers van de kiosk (argumenten, `--xwayland-xauthority`, en dat sluiten alleen het eigen profiel raakt).
- `ravengun_config_tests`: `sharp_shooter.buttons` (rondgang, validatie) en de IPC-commando's `set_buttons` en `calibrate_ui`.
- `ravengund_command_tests`: `set_buttons` bewaart en wist, en `calibrate_ui` open zonder webpoort start nooit een browser.
- `ravengund_web` (curl): de pagina, de endpoints, het bewaren van de mapping, `edges` in de SSE-stroom.
- `ravengun_calibrate_js` (`tools/test-js.sh`): 22 unittests van `calibrate-core.js` in node (in een wegwerp-container): flanken en debounce, lang indrukken, mapping-verzameling, tempo, stilstand, labels, de bolgeometrie, het kwaliteitsoordeel en de schermdoelen. Zonder podman of netwerk wordt de test overgeslagen (exitcode 77). Hetzelfde script controleert ook de syntaxis van alle web-JS.

## Werking op de echte tv

Gecontroleerd op de tv (HDMI-A-1, 1920 bij 1080, KDE Wayland met Xwayland): `ravengunctl calibrate-ui` opent de pagina fullscreen zonder rand of balk, de pagina toont "Daemon: ok" via de echte SSE-stroom, en `--close` sluit alleen het kioskproces (een bestaand Brave-venster van de gebruiker bleef staan). De negen doelen staan op de verwachte plekken. De hele wizard met een echte Move en Sharp Shooter (de stroom van knoppen, de gyro en de dekking) is nog niet met hardware doorlopen; de onderdelen zijn met unittests en de voorbeeldmodus gecontroleerd.
