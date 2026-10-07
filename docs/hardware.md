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
