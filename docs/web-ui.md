# Webinterface

De daemon `ravengund` start zelf een kleine webserver in een eigen thread. Er is geen aparte applicatie, geen Electron en geen CDN. De pagina's staan in de binary. De web-API gebruikt dezelfde opdrachtafhandeling als de IPC-socket (`src/ravengund/commands.cpp`), dus filter, aim en recenter werken overal gelijk.

## Opstarten en opties

Standaard staat de webinterface aan en luistert hij alleen op `127.0.0.1:8765`. Open `http://127.0.0.1:8765/` in een browser op dezelfde machine. De daemon logt de URL bij het starten.

- `--web` en `--no-web` zetten de interface aan (standaard) of uit.
- `--web-port N` kiest een andere poort (1 tot 65535).
- `--web-lan` luistert ook op het LAN (alle interfaces). Dit vereist een token, zie hieronder. Samen met `--no-web` is het een fout (exitcode 64).

Kan de webserver niet starten (poort bezet, token niet te maken), dan staat dat in de log en draait de daemon gewoon door. De live inputketen (HID, camera, tracker, IPC) hangt nooit van de browser af.

## LAN-toegang en token

Met `--web-lan` maakt de daemon bij de eerste start een willekeurig token van 64 hexadecimale tekens in `~/.config/ravengun/web-token` (map 0700, bestand 0600; `XDG_CONFIG_HOME` wordt gevolgd). Een bestaand token wordt hergebruikt, maar alleen als het bestand van de gebruiker zelf is en niet leesbaar voor anderen. Het token staat nooit in de log; de log noemt alleen de URL en het pad van het tokenbestand.

In LAN-modus is elke request zonder geldig token een 401. Het token kan op drie manieren worden meegegeven: de header `Authorization: Bearer TOKEN`, de header `X-RavenGun-Token`, of de cookie `rg_token`. Voor een browser opent u eenmalig `http://HOST:8765/?token=TOKEN`. De server zet dan een HttpOnly-cookie (SameSite=Strict) en stuurt door naar `/`. Het verkeer is niet versleuteld (gewoon HTTP). Gebruik LAN-modus dus alleen op een vertrouwd netwerk.

## Beveiligingsmodel

Alle beveiliging zit in `src/ravengun/web/web_core.cpp` en is los van sockets getest.

- Host-controle bij elke request, tegen DNS-rebinding. In localhost-modus zijn alleen `127.0.0.1`, `localhost` en `[::1]` met de juiste poort toegestaan. In LAN-modus ook IP-adressen en de eigen hostnaam.
- Voor elke POST: een `Origin` (als die er is) moet bij de toegestane hosts horen en `Sec-Fetch-Site` mag niet cross-site zijn. Het `Content-Type` moet `application/json` zijn, en de header `X-CSRF-Token` moet gelijk zijn aan het CSRF-token van dit proces. Dat token zit in de pagina (`<meta name="csrf-token">`) en is ook op te halen via `GET /api/csrf`, zodat curl het kan gebruiken. Een andere website kan dat antwoord niet lezen, omdat er geen CORS-headers worden gestuurd.
- Er zijn nooit `Access-Control-*`-headers en `OPTIONS` wordt geweigerd. De pagina's krijgen een strenge Content-Security-Policy (alleen eigen scripts en stijlen, geen frames) en `no-store`.
- Geen shell-endpoint, geen paden of programma's als argument. Elk endpoint wordt omgezet in een getypeerd IPC-commando, waarvan de waarden worden gecontroleerd door dezelfde parser als de socket. Onbekende velden worden geweigerd.
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
- `GET /api/stream`: Server-Sent Events. Per frame een `event: sample` met `{"sample":...,"skipped_frames":N}`. Hoogstens 30 frames per seconde, en alleen als er nieuwe data is; elke 15 seconden een keepalive-commentaarregel.

Voorbeeld met curl:

    CSRF=$(curl -s http://127.0.0.1:8765/api/csrf | sed 's/.*"csrf":"\([0-9a-f]*\)".*/\1/')
    curl -s -X POST -H 'Content-Type: application/json' -H "X-CSRF-Token: $CSRF" \
         -d '{"beta":0.8}' http://127.0.0.1:8765/api/filter

## Niet-blokkerend gedrag

De webthread leest alleen een momentopname (een korte lock, een kopie) en heeft per SSE-client een eigen uitvoerbuffer. Is een client traag en staat er meer dan 32 KiB te wachten, dan worden frames voor die client overgeslagen (teller `skipped_frames`). Boven 256 KiB of na 15 seconden zonder afname wordt de verbinding gesloten. De HID-, camera- en IPC-thread wachten nooit op de webthread. Alleen de webthread zelf wacht bij een POST of status-verzoek maximaal 500 ms op de HID-thread, net als de IPC.

## Pagina's

De pagina's zijn Nederlands, donker en bruikbaar op een telefoon. Er worden geen camerabeelden getoond.

- Overzicht: verbinding met de Move, batterij volgens de werkelijke resolutie (de tekst uit de daemon, bijvoorbeeld zes niveaus; er wordt geen percentage verzonnen), fps van IMU-rapporten en camera, of de bol is gevonden (met de reden als dat niet zo is), geldigheid van oriëntatie en aim, en daemoninformatie. Zonder Move staat er netjes "nee, geen Move gevonden".
- Live: een canvas met het aim-punt (ruw in grijs, gefilterd in blauw), een minikaart van de bolpositie in het camerabeeld (alleen coördinaten) en de trekker en knoppen. De pagina gebruikt `/api/stream` en sluit de stroom bij het verlaten van de pagina of als het tabblad verborgen is.
- Instellingen: het 1€-filter, de aim-schaal en recenter, met controle van de invoer in de browser (bereiken zoals hierboven) en daarna nogmaals door de daemon. Fouten van de daemon staan bij het formulier.
- Kalibratie, Apparaten (pairing) en Games (profielen): tijdelijke pagina's met uitleg dat ze later komen. De kalibratiedoelen komen op de televisie, niet op dit beheerscherm.

## Bouwen en testen

De bestanden in `src/ravengund/web/` worden door `cmake/EmbedWebAssets.cmake` bij het configureren in `build/web_assets.cpp` gebakken. Na een wijziging van HTML, CSS of JS is dus een nieuwe `cmake`-run en build nodig (de build doet dit vanzelf, want de bestanden staan als configure-afhankelijkheid).

`ravengun_web_tests` (`tests/test_web.cpp`) test de parser, de routing, de JSON-respons, de weigeringen (Host, Origin, CSRF, content-type, LAN zonder token), de tokenopslag en de SSE-throttling met een trage afnemer. `ravengund_web` (`tests/web_smoke.sh`) start de daemon zonder hardware en loopt met curl alle endpoints en weigeringen door, inclusief LAN-modus.
