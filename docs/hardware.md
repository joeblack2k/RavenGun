# Hardware-inventaris (gemeten 7 okt 2026)

Host: SteamOS 3.9.2 (build 20260925.101), kernel 7.2.7-valve1, gebruiker `deck` (groepen deck, wheel, steamos-log-submitter; niet in `video` of `input`). MAC-adressen en serienummers zijn geredigeerd.

Legenda: "op echte hardware getest" is hier gemeten op deze host; "nog te bevestigen" is niet getest.

## Camera: Sony PlayStation Eye (op echte hardware getest)

- `lsusb`: `1415:2000`, USB 2.0 (480 Mb/s), USB-bus 9. Productstring "USB Camera-B4.09.24.1". Dit is een PS3 Eye (OmniVision OV534).
- Driver: `ov534` (kernelmodules `gspca_ov534`, `gspca_main`), V4L2-kaart "USB Camera-B4.09.24.1". Node `/dev/video0` (eigenaar root:video, mode 660, ACL `user:deck:rw`, dus toegankelijk via de lokale sessie/uaccess). Enkel capture-node, geen metadata-node.
- `/dev/v4l/by-id/usb-OmniVision_Technologies__Inc._USB_Camera-B4.09.24.1-video-index0` en twee `by-path`-links wijzen naar `video0`.
- Formaten (`v4l2-ctl --list-formats-ext`): `GRBG` (8-bit Bayer) en `YUYV`, elk met twee resoluties.
  - 640x480: 60, 50, 40, 30, 15 fps.
  - 320x240: 187, 150, 137, 125, 100, 75, 60, 50, 37, 30 fps.
- Regelaars: handmatig/auto gain en witbalans, belichting, helderheid, contrast, flips, enz. Standaard staan auto-gain en automatische belichting aan (belichting en gain zijn dan "inactive").

### Gemeten frametiming (`camprobe`, ca. 10 s per modus, alleen getallen)

Intervallen komen uit V4L2-buffertimestamps. Alle modi: 0 gemiste frames (geen gaten in `sequence`), 0 buffers met error-flag, 0 te korte frames.

- YUYV 640x480 gevraagd 60 fps: 60.02 fps, interval gem 16.660 ms, p95 16.679, p99 16.688, max 16.688 ms, sd 0.011 ms.
- GRBG 640x480 gevraagd 60 fps: 60.02 fps, gem 16.660 ms, p95 16.676, max 16.689 ms.
- YUYV 320x240 gevraagd 125 fps (5 s): 124.90 fps, gem 8.007 ms, p95 8.021, max 8.033 ms.
- GRBG 320x240 gevraagd 187 fps: 187.35 fps, gem 5.338 ms, p95 5.356, max 5.364 ms, 1873 frames.
- YUYV 320x240 gevraagd 120 fps: de driver meldt 120 fps maar levert 99.9 fps (gem 10.008 ms). 120 staat niet in de lijst van ondersteunde intervallen; gebruik 100 of 125.
- De bijna-perfecte regelmaat (sd ~0.01 ms) hoort bij een isochrone USB-bron. Dat zegt niets over de totale latentie tussen lichtbeeld en timestamp (belichting, sensor, USB); die is niet gemeten.
- Het dequeue-moment ligt gemiddeld 0.008 ms na de timestamp (p95 0.02 ms) bij een idle systeem. Onder belasting (bv. tijdens het spel) niet gemeten: nog te bevestigen.

### Betekenis van de timestamp-flags

`v4l2_buffer.flags` bevat twee velden. `V4L2_BUF_FLAG_TIMESTAMP_*` zegt welke klok: `MONOTONIC` is `CLOCK_MONOTONIC` (vergelijkbaar met `clock_gettime(CLOCK_MONOTONIC)`), `COPY` is overgenomen van de aanvrager (output-buffers), `UNKNOWN` is niet gedefinieerd. `V4L2_BUF_FLAG_TSTAMP_SRC_*` zegt wanneer in het frame de timestamp is genomen: `EOF` is het einde van het frame (laatste byte ontvangen), `SOE` is het begin van de belichting (alleen bij hardware die dat meldt). Gemeten op deze camera: type `monotonic`, bron `EOF`. De timestamp is dus het moment waarop het frame volledig binnen was, niet het moment van belichting; de overdrachtstijd van het frame (bij 640x480@60 ruwweg de hele frameperiode) moet bij latentieberekeningen worden meegerekend. 

## PlayStation Move (op echte hardware getest, via USB)

- Aangesloten via USB: `054c:03d5` = ZCM1 (originele Move met magnetometer), USB full-speed (12 Mb/s), `MaxPower 500mA`. Interface HID, `/dev/hidraw9` (root:root, mode 660, ACL `user:deck:rw`; udev-tags `seat:uaccess`). Kerneldriver `usbhid`, HID-laag `sony` (hid-sony geladen).
- PSMoveAPI (zie `docs/build.md`): `psmove_count_connected()` = 1, verbindingstype USB, batterij "charging". Via USB kwamen in 3 s 0 inputrapporten binnen: geen sensor- of knopdata via USB. Sensoren/knoppen moeten via Bluetooth worden getest (nog te bevestigen).
- Andere hidraw-apparaten (geen Move): Cooler Master ARES (3x), Steam Controller Puck (5x), ASUS AURA LED (1x), allen root:root met ACL voor `deck`.
- Bij het openen schrijft PSMoveAPI een calibratiebestand naar `~/.psmoveapi/` (lokaal).
- Geen Move gekoppeld bij BlueZ (er zijn ca. 80 bekende apparaten, 6 gekoppeld; geen "Motion Controller").

## Bluetooth (op echte hardware getest)

- Adapter `hci0` (USB `13d3:3571`, IMC Networks; modules btusb, btmtk, btrtl, btbcm, btintel), Powered=yes, Pairable=yes, Roles central+peripheral. Een adapter.
- BlueZ 5.87 (`bluetoothctl --version`). `bluetoothctl show` meldt discovering=yes (een andere partij of sessie scant).
- rfkill: `rfkill0` bluetooth en `rfkill1` wlan, beide niet geblokkeerd (soft/hard 0).
- `/etc/bluetooth/input.conf` (alleen gelezen): `[General]` met alle opties uitgecommentarieerd, dus ClassicBondedOnly staat niet ingesteld en de BlueZ-standaard geldt (`true`, "for security"); `UserspaceHID` standaard true. `bluetoothd` bevat de optie `ClassicBondedOnly`.
- `/etc` en `/var/lib/bluetooth` (root, 0700) zijn niet door de gebruiker schrijfbaar; de `/etc/bluetooth` is read-only op SteamOS.

## uinput (op echte hardware getest)

`/dev/uinput` bestaat (module `uinput` geladen), eigenaar root:root, mode 660, ACL `user:deck:rw` (en `brltty`), udev-tags `seat:uaccess`. De gebruiker `deck` kan dus virtuele apparaten maken zolang er een lokale sessie is (ook via SSH, zoals nu). Of dit blijft werken voor een service buiten de seat (bv. `systemd --user` met linger) is nog te bevestigen.

## ravengunctl doctor op deze host

Alles OK behalve waarschuwing "geen Move gekoppeld bij BlueZ" (exitcode 1).

## Hoe PSMoveAPI (`psmove pair`) op Linux koppelt (uit de broncode, versie 256b629)

1. Op de controller, via USB-hidraw, geen root nodig: leest het opgeslagen host-adres en schrijft het Bluetooth-adres van deze pc met HID-feature-report `0xF5` (`_psmove_set_btaddr`).
2. In BlueZ, root nodig (`psmove_port_check_pairing_permissions` eist euid 0): `systemctl stop bluetooth.service` (alle Bluetooth-verbindingen vallen kort weg), schrijft `/var/lib/bluetooth/<host>/<move>/info` (Name "Motion Controller", Class 0x002508, `Trusted=true`, HID-service, DeviceID 054c/03d5) en `/var/lib/bluetooth/<host>/cache/<move>`, en start `bluetooth.service` weer. Voor ZCM2 draait daarna een PIN-agent (0000) als SSP uit staat; voor ZCM1 niet.
3. Er wordt dus geen link key aangemaakt: de Move is "trusted" maar niet "bonded". Met de standaard `ClassicBondedOnly=true` verwacht ik dat bluetoothd de HID-verbinding van een ZCM1 weigert. Nog te bevestigen op hardware.

## Voorgestelde hostwijzigingen (nog niet toegepast)

1. Pairing van de Move (root, eenmalig, door de gebruiker): `sudo ~/.local/share/ravengun/psmoveapi/bin/psmove pair` met de Move op USB. Reden: schrijft de bestanden in `/var/lib/bluetooth` die zonder root niet kunnen. Risico: bluetooth.service wordt ca. 1-2 s gestopt; verbonden BT-apparaten (Xbox-controller, AirPods) herverbinden daarna zelf. Rollback: `bluetoothctl remove <move-adres>` (alleen dat apparaat) of de map `/var/lib/bluetooth/<host>/<move-adres>` en het cache-bestand verwijderen en `systemctl restart bluetooth`.
2. Alleen als de gekoppelde Move niet verbindt: in `/etc/bluetooth/input.conf` onder `[General]` `ClassicBondedOnly=false` zetten en `bluetooth.service` herstarten. Reden: een ZCM1 doet geen versleutelde koppeling. Nadeel: ook andere niet-gebonden HID-apparaten mogen dan verbinden (beveiligingsafweging). Rollback: de regel weer uitcommentarieren en herstarten. Op SteamOS vereist dit een tijdelijk beschrijfbare rootpartitie (`steamos-readonly disable`) en kan een SteamOS-update de wijziging wissen.
3. Geen udev-regel voor hidraw of uinput nodig zolang de service in de lokale sessie draait; de meegeleverde `99-psmove.rules` (mode 0666 voor Move-hidraw) is dan niet nodig en wordt niet geadviseerd.

## Pairing-stappen voor de gebruiker (volgende stap, nog niet uitgevoerd)

1. Move blijft met een USB-datakabel (geen laadkabel) aan de pc.
2. Voer wijziging 1 uit (sudo psmove pair).
3. Koppel de USB-kabel los.
4. Druk kort op de PS-knop (middelste ronde knop vooraan). De controller knippert en verbindt.
5. Controle: `~/.local/share/ravengun/psmoveapi/bin/psmove list` toont Bluetooth, en `ravengunctl doctor`.
6. Verbindt hij niet: wijziging 2 en opnieuw stap 4.

## Meting met ravengund (op echte hardware getest, 7 okt 2026, ongeveer 15:40)

Methode: `ravengund` met de standaardinstellingen (1€ aan, h_edge 20 graden, v_edge 11,25 graden, automatisch midden bij het eerste rapport). Na 3 s is 85 s opgenomen met `ravengunctl watch --json --rate 250`, dus elk sample (6438 samples, gemiddeld 75,7 per s). Daarna volgde `ravengunctl status --json`, en tot slot is de daemon gestopt (exit 0). De Move lag gedurende de hele meting volgens de bias-detector stil (100 % van de vensters "still"; accelerometer-sd ongeveer 0,002 g). De houding was gekanteld: a ≈ (0,70; 0,07; 0,76) g, dus de Move lag waarschijnlijk schuin op tafel. Of hij helemaal onaangeraakt lag, is niet gecontroleerd: de gebruiker was aanwezig, maar is niets gevraagd. De camera is niet gebruikt.

Rapporten (daemonstatistiek, 88 s verbonden):
- 6742 rapporten, 76,6 per s sinds verbinden (75,6 per s over de laatste 5 s), twee IMU-subsamples per rapport (ongeveer 153 Hz).
- Intervallen (ontvangsttijd): p50 16,8 ms, p95 21,0 ms, p99 21,1 ms, max 36,8 ms. De verdeling is tweetoppig. Telling per bereik: 0–5 ms 377, 5–10 ms 2943, 10–15 ms 26, 15–20 ms 2205, 20–30 ms 1183, 30–50 ms 7, niets boven 50 ms. Het gemiddelde is ongeveer 13 ms. De korte en lange intervallen wisselen elkaar af (bijvoorbeeld 6 en 20 ms), wat past bij Bluetooth-planning. De stale-drempel (50 ms) is in deze meting nooit gehaald.
- Het 4-bitsvolgnummer sloeg 1010 keer een nummer over (ongeveer 15 %). De betekenis daarvan (verloren rapporten of firmwaregedrag) is nog te bevestigen.
- Verwerkingstijd van ontvangst tot publicatie: p50 1,9 µs, p95 2,6 µs, max 20,9 µs.
- Batterij: `Batt_MAX` ("vol, niet aan de lader").

Gyro in stilstand (laatste subsample): gemiddeld (1,15; −2,17; 1,11) graden/s, sd (0,51; 0,48; 1,07) graden/s. Een aparte proef van 10 s met beide subsamples liet een systematisch verschil tussen `Frame_FirstHalf` en `Frame_SecondHalf` zien op de Z-as: gemiddeld 1,73 tegenover 1,25 graden/s. X en Y verschillen minder dan 0,05 graden/s. De biasschatter middelt over beide subsamples (eindwaarde Z 1,42 graden/s). Een bias per subsample is een mogelijke verbetering, nog niet gebouwd.

Drift over 60 s, na de biasschatting (venster 5,5 tot 65,5 s na de start van de opname; `bias_state` was al "estimated"):
- Yaw: +2,1 graden aan het eind van het venster, met tussentijdse uitslagen van −0,9 tot +1,75 graden t.o.v. het midden. De helling per 10 s varieerde van +3,1 tot −1,4 graden per minuut. De Z-bias zakte in de eerste 50 s nog van 1,50 naar 1,40 graden/s (het lopende gemiddelde convergeert). Daarbij komt de random walk van de gyroruis: met ongeveer 1 graad/s ruis per subsample is dat in theorie ongeveer 0,7 graden (1σ) per minuut.
- Pitch: +0,19 graden (helling 0,02 graden per minuut). De pitch wordt door de accelerometer gecorrigeerd.
- Omgerekend met de standaardschaal (1 graad yaw = 2,5 % van de breedte) is 2,1 graden yaw ongeveer 5 % van de schermbreedte per minuut. Dat haalt het doel (ongeveer 1 % over 5 minuten) duidelijk niet. IMU-only heeft yaw geen absolute referentie. Zie de open punten.

Stilstandsjitter van de aim, in procent van het genormaliseerde bereik (breedte of hoogte = 100 %). De p95 is die van de absolute afwijking van een gecentreerd voortschrijdend gemiddelde van 1 s, zodat langzame drift er niet in zit:
- Ongefilterd: x 0,11 %, y 0,19 %.
- Met 1€ (min_cutoff 1 Hz, beta 0,5): x 0,07 %, y 0,12 %.
- Ter vergelijking, de p95-afwijking van het gemiddelde over 60 s (drift telt hier wel mee): x 3,0 %, y 0,7 %.
- De jitter in procenten hangt rechtstreeks af van de schaal (h_edge en v_edge). Met een andere schaal veranderen deze getallen evenredig.


## Meting van de hele keten tot in het gedeelde geheugen (echte Move, 12 okt 2026)

Overgenomen uit de meting in `STATUS.md` (2026-10-12); de cijfers hieronder vervangen die van 7 oktober waar ze hetzelfde meten.

Rapporten en oriëntatie (Bluetooth, `hidraw4`):
- 88 rapporten per seconde, 0,66 % gemist volgens de volgnummers. Op 7 oktober was dat 76,6 per seconde met ongeveer 15 % overgeslagen volgnummers; de oorzaak van de verbetering is niet vastgesteld.
- Oriëntatie geldig; yaw binnen ±0,4° bij een minuut stilliggen.

Gedeeld geheugen (route tp-shm, Luigi-indeling, `/dev/shm/ravengun_JvsState`):
- Een richtpunt van 0,8/0,3 geeft byte 13 (x) = 202 en byte 12 (y) = 79.
- Trekker zet bit 0x4, granaat 0x2, test 0x100.
- Daarmee is de keten Move → `move_report` → `move_slot` → gedeeld geheugen gemeten en niet alleen uit de code afgeleid.

Grenzen van deze meting:
- Echte knoppen zijn niet gemeten: in het meetvenster drukte niemand iets in.
- Het kompas is afgewezen: de dip was 74–78° tegen 60° in de referentie, dus de yaw-correctie vergrendelt niet. De ±0,4° geldt voor een minuut stilliggen, niet voor spelen.
- De camera ziet de bol niet; het richtpunt komt alleen uit de oriëntatie.
- USB levert geen rapporten omdat de code alleen Bluetooth kiest. Dat is een keuze in de code, geen blokkade.

## Meting camera en boltracking in ravengund (op echte hardware getest, 7 okt 2026, ongeveer 15:50)

Methode: `ravengund` met de standaardinstellingen (camera automatisch gekozen via by-id, 640x480@60 YUYV, AE, AGC en AWB uit, exposure 50, gain 0, LED cyaan, tracker-tint −75 ± 30°). Na 3 s is 60 s opgenomen met `ravengunctl watch --json --rate 250`, gevolgd door `ravengunctl camera --json` en `status --json`. Daarna is de daemon gestopt (exit 0) en was de camera weer vrij. De Move lag stil, met de bol in beeld op ongeveer (359, 287) px. Er is geen beeld opgeslagen, alleen getallen.

- Camera: 3728 frames, 60,022 fps (totaal en over 5 s). Intervallen: gemiddeld 16,660 ms, p50 16,657, p95 16,685, p99 16,688 en max 16,689 ms, allemaal in de bak 12–18 ms. 0 gemiste frames (sequence), 0 fout- en 0 korte frames, 0 streamfouten.
- Wachtrij (capaciteit 2): 3728 erin en 3728 verwerkt, 0 weggegooid.
- Dequeue-vertraging (ontvangst min V4L2-timestamp): p50 0,008 ms, p95 0,021 ms. Leeftijd bij de start van de verwerking: p50 0,041 ms, p95 0,058 ms, max 0,23 ms. Dit telt vanaf EOF. De belichting en de USB-overdracht vóór EOF zitten er niet in.
- Verwerkingstijd per frame (tracker, binnen de ROI): p50 119 µs, p95 146 µs, p99 208 µs, max 962 µs. Doorzoeken van het volledige beeld (zonder bol) kost ongeveer 600 µs (p50).
- Bol gevonden: 3727 van de 3728 frames (99,97 %). Het ene ongeldige frame is het eerste (`acquire_pending`, bevestiging nodig). 3726 keer gevonden binnen de ROI, 2 keer is het volledige beeld doorzocht. In de watch-stroom (3603 unieke frames) was 100 % geldig.
- Straal: p5 19,07, p50 19,10, p95 19,12 px. Oppervlak 1144 px, 0 verzadigde pixels, gemiddelde Y van de bol 102, tint −74°, chroma 67, langgerektheid 1,004, vulling 0,999.
- Positiejitter in stilstand: sd x 0,018 px en y 0,015 px. Radiale afwijking van het gemiddelde p50 0,020 px, p95 0,040 px, max 0,071 px. Ten opzichte van een voortschrijdend gemiddelde van 1 s is p95 0,036 px. De drift tussen de eerste en de laatste 5 s is (+0,018; +0,024) px.
- De HID-verwerking draaide ongestoord door: 77,4 rapporten per s, verwerking p95 2,3 µs. Er waren 63 LED-updates en 0 mislukte.
- Scène bij deze belichting: gemiddelde Y 4, p99 16, 0 % verzadigd. 0,38 % van de pixels matcht de bolkleur, en dat is de bol zelf.

Kortere vergelijkingen (15 s, verder dezelfde instellingen): 320x240@187 gaf 187,35 fps, 0 gemist, straal 8,8 px, jitter p95 0,071 px en verwerking p95 40 µs. 320x240@125 gaf 124,90 fps, 0 gemist, straal 9,4 px, jitter p95 0,039 px en verwerking p95 44 µs. LED uit en een magenta LED bij een cyaan-instelling gaven allebei 0 kandidaten in ongeveer 300 frames (geen valse detecties).

Nog te bevestigen: het gedrag op de echte speelafstand (2 tot 3 m), met beweging (bewegingsonscherpte, de ROI-voorspelling), met een hand rond de controller, overdag of bij ander licht, en met tv-licht in beeld. Bij het lezen in de automatische modus gaf de gain-control waarde 241 terug, buiten het opgegeven bereik 0..63. Die waarde komt waarschijnlijk uit een sensorregister (de control is volatile) en wordt niet gebruikt.
