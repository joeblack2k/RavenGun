# Webinterface

De daemon `ravengund` start zelf een kleine webserver in een eigen thread. Er is geen aparte applicatie, geen Electron en geen CDN. De pagina's staan in de binary. De web-API gebruikt dezelfde opdrachtafhandeling als de IPC-socket (`src/ravengund/commands.cpp`), dus filter, aim en recenter werken overal gelijk.

## Opstarten en opties

Standaard staat de webinterface aan en luistert hij alleen op `127.0.0.1:8765`. Open `http://127.0.0.1:8765/` in een browser op dezelfde machine. De daemon logt de URL bij het starten.

- `--web` en `--no-web` zetten de interface aan (standaard) of uit.
- `--web-port N` kiest een andere poort (1 tot 65535).
- `--config PAD` leest en schrijft een ander instellingenbestand; `--no-config` leest en schrijft er geen (zie "Instellingen bewaren").
- `--web-lan` luistert ook op het LAN (alle interfaces). Dit vereist een token, zie hieronder. Samen met `--no-web` is het een fout (exitcode 64).

Kan de webserver niet starten (poort bezet, token niet te maken), dan staat dat in de log en draait de daemon gewoon door. De live inputketen (HID, camera, tracker, IPC) hangt nooit van de browser af.

## LAN-toegang en token

Met `--web-lan` maakt de daemon bij de eerste start een willekeurig token van 64 hexadecimale tekens in `~/.config/ravengun/web-token` (map 0700, bestand 0600; `XDG_CONFIG_HOME` wordt gevolgd). Een bestaand token wordt hergebruikt, maar alleen als het bestand van de gebruiker zelf is en niet leesbaar voor anderen. Het token staat nooit in de log; de log noemt alleen de URL en het pad van het tokenbestand.

In LAN-modus is elke request zonder geldig token een 401. Het token kan op drie manieren worden meegegeven: de header `Authorization: Bearer TOKEN`, de header `X-RavenGun-Token`, of de cookie `rg_token`. Voor een browser opent u eenmalig `http://HOST:8765/?token=TOKEN`. De server zet dan een HttpOnly-cookie (SameSite=Strict) en stuurt door naar `/`. Het verkeer is niet versleuteld (gewoon HTTP). Gebruik LAN-modus dus alleen op een vertrouwd netwerk.

## Instellingen bewaren

Wat u in de webinterface wijzigt, wordt bewaard in `~/.config/ravengun/config.json` (`XDG_CONFIG_HOME` wordt gevolgd; map 0700, bestand 0600) en bij de volgende start toegepast. De daemon past een wijziging eerst live toe; het bewaren komt daarna. Lukt het bewaren niet, dan blijft de wijziging actief en toont de pagina de fout.

Voorrang: een optie op de opdrachtregel van `ravengund` gaat altijd voor het bestand, voor die ene run (bijvoorbeeld `--exposure 80`, `--led rood`, `--min-cutoff 2`, `--web-port`). De daemon schrijft bij het starten nooit zelf iets terug. Wijzigt u later een waarde in de interface, dan wordt die bewaard; de overige bewaarde waarden blijven zoals ze in het bestand stonden, ook als de opdrachtregel ze deze run overschreef. De pagina's tonen de werkelijke waarden uit de status van de daemon.

Schema (versie 1), alle secties optioneel:

- `schema`: geheel getal, nu 1. Een nieuwere versie wordt geweigerd; een oudere gaat via de migratiehaak (`builtin_migrations()` in `src/ravengun/config/settings.cpp`, nu leeg).
- `led`: `enabled` (bool, waar zodra een kleur gekozen is; pas dan stuurt de daemon de LED ook zonder camera aan), `preset` (cyaan, magenta, rood, groen, blauw, wit of custom) en `r`, `g`, `b` (0 tot 255).
- `tracker`: `hue_deg` (-180 tot 180), `hue_tol_deg` (1 tot 90) en `min_chroma` (1 tot 180). De tint volgt de LED-kleur.
- `camera`: `mode` (`640x480@60`, `320x240@125` of `320x240@187`), `auto_exposure`, `auto_gain`, `auto_awb` (bool), `exposure` (0 tot 255) en `gain` (0 tot 63).
- `filter`: `enabled`, `min_cutoff_hz`, `beta`, `d_cutoff_hz` (zelfde bereiken als `set_filter`).
- `aim`: `h_edge_deg` en `v_edge_deg` (1 tot 89).
- `web`: `port` (1 tot 65535, geldt na een herstart).
- `sharp_shooter`: `buttons`, een object van fysieke Sharp Shooter-knop (trekker, pomp, driehoek, cirkel, kruis, vierkant, start, select, move, ps) naar Move-knop (triangle, circle, cross, square, select, start, ps, move, t), gemeten door de kalibratie-UI. LAN-toegang staat bewust niet in het bestand en niet in de interface: alleen `--web-lan` met een token.

Schrijven en fouten: het bestand wordt atomisch vervangen (tijdelijk bestand 0600 in dezelfde map, fsync, rename), dus een crash laat nooit een half bestand achter. Vóór het vervangen wordt een geldig bestaand bestand de laatst-goede kopie `config.json.bak`. Bij het laden wordt alles gevalideerd (bereiken, typen, schemaversie; onbekende sleutels worden genegeerd met een waarschuwing). Een ongeldig bestand wordt nooit half toegepast: de daemon gebruikt de kopie (toestand `recovered`) of anders de standaardwaarden (`invalid`), meldt dat in de log en op de pagina Instellingen, en zet het corrupte bestand bij de eerstvolgende wijziging opzij als `config.json.corrupt`.

## Beveiligingsmodel

Alle beveiliging zit in `src/ravengun/web/web_core.cpp` en is los van sockets getest.

- Host-controle bij elke request, tegen DNS-rebinding. In localhost-modus zijn alleen `127.0.0.1`, `localhost` en `[::1]` met de juiste poort toegestaan. In LAN-modus ook IP-adressen en de eigen hostnaam.
- Voor elke POST: een `Origin` (als die er is) moet bij de toegestane hosts horen en `Sec-Fetch-Site` mag niet cross-site zijn. Het `Content-Type` moet `application/json` zijn, en de header `X-CSRF-Token` moet gelijk zijn aan het CSRF-token van dit proces. Dat token zit in de pagina (`<meta name="csrf-token">`) en is ook op te halen via `GET /api/csrf`, zodat curl het kan gebruiken. Een andere website kan dat antwoord niet lezen, omdat er geen CORS-headers worden gestuurd.
- Er zijn nooit `Access-Control-*`-headers en `OPTIONS` wordt geweigerd. De pagina's krijgen een strenge Content-Security-Policy (alleen eigen scripts en stijlen, geen frames) en `no-store`.
- Geen shell-endpoint, geen paden of programma's als argument: ook `/api/camera` kent geen apparaatpad, alleen een van drie vaste modi. Een poort of LED-kleur gaat door dezelfde bereikcontrole als de IPC. Elk endpoint wordt omgezet in een getypeerd IPC-commando, waarvan de waarden worden gecontroleerd door dezelfde parser als de socket. Onbekende velden worden geweigerd.
- Grenzen: header 8 KiB, body 4 KiB, 32 verbindingen, 8 streams, een verzoek moet binnen 10 seconden compleet zijn.
- De web-API toont het controller-ID altijd geredigeerd en nooit camerabeelden.
- De server draait als dezelfde gebruiker als de daemon, nooit als root.

## Endpoints

Alle antwoorden zijn JSON in dezelfde envelop als de IPC: `{"v":1,"ok":true,"result":...}` of `{"v":1,"ok":false,"error":{"code":...,"message":...}}`. De HTTP-status volgt de foutcode: 400 voor ongeldige invoer, 503 als iets tijdelijk niet kan (bijvoorbeeld recenter zonder verbonden Move), 500 voor interne fouten, 403 voor geweigerde Host, Origin of CSRF, 401 voor een ontbrekend LAN-token.

- `GET /api/status`: de volledige daemonstatus, inclusief controller, batterij, rapportstatistiek, configuratie en camera.
- `GET /api/camera`: de camerastatus (tellers en toestanden, geen beeld).
- `GET /api/sample`: het laatste sample (oriëntatie, optische positie, aim, trekker, knoppen).
- `GET /api/csrf`: het CSRF-token.
- `POST /api/recenter`: lege body of `{}`.
- `POST /api/filter`: JSON met een of meer van `enabled` (bool), `min_cutoff_hz` (0,001 tot 100), `beta` (0 tot 1000) en `d_cutoff_hz` (0,001 tot 100).
- `POST /api/aim`: JSON met `h_edge_deg` en/of `v_edge_deg` (1 tot 89 graden tot de schermrand; kleiner is gevoeliger).
- `GET /api/info`: versies (daemon, git-commit, PSMoveAPI-pin, of PSMoveAPI gelinkt is), uptime, webpoort en de toestand van het instellingenbestand.
- `GET /api/settings`: de bewaarde waarden, de toestand van het bestand, de LED-presets met hun gemeten tint en de toegestane cameramodi.
- `GET /api/diagnostics`: een JSON-export (info, status, instellingen) met geredigeerd controller-ID en `~` voor de thuismap, als download (`Content-Disposition: attachment`). Nooit camerabeelden.
- `GET /api/output`: de uitvoerstatus (armed, apparaat, positie, patroon, transformatie).
- `POST /api/led`: `{"preset":"cyaan"}` (cyaan, magenta, rood, groen, blauw, wit) of `{"r":0,"g":255,"b":255}` (gehele getallen 0 tot 255). Zie "LED-kleur en tracker-tint".
- `POST /api/camera`: een of meer van `exposure` (geheel 0 tot 255), `gain` (geheel 0 tot 63), `auto_exposure`, `auto_gain`, `auto_awb` (bool) en `mode` (`640x480@60`, `320x240@125`, `320x240@187`). `GET /api/camera` blijft de status.
- `POST /api/web`: `{"port":N}` voor de volgende start. Een veld `lan` wordt geweigerd.
- `POST /api/arm` en `POST /api/disarm`: lege body of `{}`.
- `POST /api/test-pattern`: `pattern` (center, corners, grid, sweep, point, stop) met optioneel `dwell_ms`, `duration_s`, `margin`, `x`, `y`, `rate_hz`.
- `POST /api/output`: transformatie (`set_output`): `invert_x`, `invert_y`, `scale_x`, `scale_y`, `offset_x`, `offset_y`, `viewport_x0`, `viewport_y0`, `viewport_x1`, `viewport_y1`, `screen_aspect`, `content_aspect`.
- `GET /api/stream`: Server-Sent Events. Per frame een `event: sample` met `{"sample":...,"skipped_frames":N,"edges":{"press":[...],"release":[...]}}` (cumulatieve flankentellers per knopbit) en, zodra er opname of kalibratie loopt, `progress`. Standaard hoogstens 30 frames per seconde, en alleen als er nieuwe data is. Een client kan met `?hz=N` (geheel getal 1 tot 60) om een hogere snelheid vragen; alles daarbuiten geeft de standaard 30. Dit verandert alleen de throttle van die ene verbinding (elke SSE-verbinding heeft er een eigen), dus andere clients blijven op 30 Hz. De webthread leest per frame één momentopname (korte lock, kopie) en bouwt de JSON zelf; de HID- en tracker-thread merken er niets van, de maximale 8 streams blijft gelden en de buffergrenzen tegen trage clients ook. Nieuwe frames komen er alleen als de HID-thread ze maakt (de Move rapporteert op zijn eigen snelheid), dus 60 Hz is een bovengrens. De oefenstap van de kalibratie-UI gebruikt `?hz=60`; elke 15 seconden een keepalive-commentaarregel.

- Kalibratie-UI op de tv (zie `docs/calibration-ui.md`): de pagina `GET /calibrate` en `POST /api/calibrate/mag/start|stop|abort`, `POST /api/calibrate/rec/start|mark|stop`, `POST /api/calibrate/buttons`, `GET` en `POST /api/calibrate/ui` (open of close de kiosk-browser).

Dezelfde commando's bestaan op de IPC-socket: `set_led`, `set_camera`, `set_web`, `info`, `settings`, `diagnostics`, `set_buttons` en `calibrate_ui`. Alle nieuwe POST-endpoints zitten achter dezelfde Host-, Origin-, Sec-Fetch-Site-, Content-Type- en CSRF-controles als de bestaande, en in LAN-modus achter het token. Onbekende velden en verkeerde typen geven 400 voordat de daemon iets ziet.

## LED-kleur en tracker-tint

De boltracker zoekt op tint (de hoek van de chroma in het camerabeeld). Een LED-kleur zonder bijpassende tint betekent dat de bol niet meer gevonden wordt. `set_led` werkt daarom beide bij, in dezelfde stap:

- Presets met gemeten tint (docs/decisions.md): cyaan -75, blauw -31, groen -145, rood 119 en magenta 86 graden, tolerantie 30.
- Wit heeft geen tint: de LED gaat aan, de tracker-tint blijft ongewijzigd en de pagina waarschuwt dat de tracker blind is.
- Custom (eigen RGB): de tint is niet gemeten. De daemon gebruikt de nominale tint (BT.601) met tolerantie 45 en geeft een waarschuwing. Een kleur die exact een preset is, telt als die preset. Een bijna grijze kleur of uit (0,0,0) heeft geen tint; dan blijft de tracker-tint staan, met een waarschuwing.
- Het antwoord noemt de tint, de tolerantie, de bron (gemeten, nominaal, geen), of de tracker live is bijgewerkt (alleen met een actieve camera) en of het bewaard is. Zonder camera wordt de tint bewaard voor de volgende start.

## Camera-instellingen live

`set_camera` voert de wijziging uit in de capture-thread (de enige eigenaar van het V4L2-apparaat), leest de werkelijke waarden terug uit de driver en toont die. Wijkt een waarde af van wat gevraagd is, dan staat dat als waarschuwing in het antwoord. Een andere modus herstart de capture; mislukt dat, dan wordt de oude modus hersteld. Zonder camera (`--no-camera`, niet gestart of niet aangesloten) geeft de opdracht een nette fout (HTTP 503) en wordt er niets bewaard. De trackerstatus wordt na een moduswissel gewist.

Voorbeeld met curl:

    CSRF=$(curl -s http://127.0.0.1:8765/api/csrf | sed 's/.*"csrf":"\([0-9a-f]*\)".*/\1/')
    curl -s -X POST -H 'Content-Type: application/json' -H "X-CSRF-Token: $CSRF" \
         -d '{"beta":0.8}' http://127.0.0.1:8765/api/filter

## Niet-blokkerend gedrag

De webthread leest alleen een momentopname (een korte lock, een kopie) en heeft per SSE-client een eigen uitvoerbuffer. Is een client traag en staat er meer dan 32 KiB te wachten, dan worden frames voor die client overgeslagen (teller `skipped_frames`). Boven 256 KiB of na 15 seconden zonder afname wordt de verbinding gesloten. De HID-, camera- en IPC-thread wachten nooit op de webthread. Alleen de webthread zelf wacht bij een POST of status-verzoek maximaal 500 ms op de HID-thread, net als de IPC.

## Pagina's

De pagina's zijn Nederlands, donker en bruikbaar op een telefoon. Er worden geen camerabeelden getoond. Tabs:

- Overzicht: batterij als balkje van vijf segmenten met tekst, de Move, camera en bol, LED en tracker-tint, tracking en daemon. De batterij toont de werkelijke resolutie van PSMoveAPI (`PSMove_Battery_Level`): zes niveaus (bijna leeg, minstens 20, 40, 60, 80 procent, vol) plus laden en vol aan de lader. Elk segment is een band van 20 procent; het kleinste niveau kleurt rood, het tweede oranje. Er wordt nooit een percentage verzonnen. Zonder Move staat er "Geen Move verbonden".
- Live: een canvas met het aim-punt (ruw in grijs, gefilterd in blauw), een minikaart van de bolpositie in het camerabeeld (alleen coördinaten) en de trekker en knoppen. De pagina gebruikt `/api/stream` en sluit de stroom bij het verlaten van de pagina of als het tabblad verborgen is.
- Apparaten: Move (model, verbinding, geredigeerd ID, kalibratie, magnetometer, aantal verbindingen), camera (model, stabiel pad, modus) en de lightgun-uitvoer (armed, apparaat, stabiele link). Koppelen (pairing) staat als tijdelijke uitleg.
- Camera: modus, automatische belichting, gain en witbalans, schuiven voor belichting en gain, de teruggelezen werkelijke waarden en de camerastatus (fps, gemiste frames, verwerkingstijden).
- LED: de zes presets met hun gemeten tint, een eigen kleur met RGB-velden en een waarschuwing, en het resultaat van de laatste wijziging (tint, tolerantie, waarschuwingen).
- Uitvoer: arm en disarm (inschakelen vraagt een bevestiging), de testpatronen midden, hoeken, raster, sweep en stop (uitgeschakeld zolang de uitvoer niet armed is), en de transformatie (spiegelen, schaal, offset, viewport).
- Instellingen: het 1€-filter, de aim-schaal, recenter, de webpoort voor de volgende start en de toestand van het instellingenbestand, met controle van de invoer in de browser (bereiken zoals hierboven) en daarna nogmaals door de daemon. Fouten van de daemon staan bij het formulier.
- Info: versies (daemon, git-commit, PSMoveAPI-pin), apparaten, uptime, de IMU-rapportfrequentie met intervallen en gaten, camera-fps en gemiste frames, verwerkingstijden en de knop "Diagnose exporteren".
- Kalibratie: een knop "Kalibreren op de tv", een knop "Oefenen op de tv" (opent direct de oefenstap) en een knop om het scherm weer te sluiten. De kalibratie zelf draait fullscreen op de televisie en wordt met het geweer bediend, zie `docs/calibration-ui.md`. Games: tijdelijke pagina met uitleg dat ze later komt.

## Bouwen en testen

De bestanden in `src/ravengund/web/` worden door `cmake/EmbedWebAssets.cmake` bij het configureren in `build/web_assets.cpp` gebakken. Na een wijziging van HTML, CSS of JS is dus een nieuwe `cmake`-run en build nodig (de build doet dit vanzelf, want de bestanden staan als configure-afhankelijkheid).

`ravengun_web_tests` (`tests/test_web.cpp`) test de parser, de routing, de JSON-respons, de weigeringen (Host, Origin, CSRF, content-type, LAN zonder token), de tokenopslag en de SSE-throttling met een trage afnemer. `ravengund_web` (`tests/web_smoke.sh`) start de daemon zonder hardware en loopt met curl alle endpoints en weigeringen door, inclusief LAN-modus.

Aanvullend: `ravengun_config_tests` (`tests/test_config.cpp`) test het schema, de validatie, de migratiehaak, het atomisch schrijven, het herstel na een corrupt bestand, de LED-presets met hun tint en de parser van de nieuwe IPC-commando's. `ravengund_command_tests` (`tests/test_commands.cpp`) draait de opdrachtlaag van de daemon met een nep-HID-context, zonder hardware: `set_led` inclusief de tracker-tint, `set_camera` (grenzen en de melding zonder camera), het bewaren van filter, aim en poort, `info`, `settings` en `diagnostics`. De live werking van `set_camera` op een echte camera is niet automatisch getest. `ravengun_web_tests` dekt ook de nieuwe endpoints en de beveiliging ervan, en `ravengund_web` loopt ze met curl door, inclusief een herstart waarbij bewaarde instellingen worden toegepast, de opdrachtregel voorrang krijgt en een corrupt bestand wordt gemeld.

JavaScript-syntaxis: er is geen `node` op de host. `tools/check-js.sh src/ravengund/web/app.js` installeert `nodejs` in een wegwerp-container (rootless podman, `localhost/ravengun-build`, geen mounts, de image blijft ongewijzigd) en draait `node --check`. Daarnaast voert een headless browser de pagina's echt uit, bijvoorbeeld `flatpak run com.brave.Browser --headless --virtual-time-budget=6000 --screenshot=uit.png URL`.
