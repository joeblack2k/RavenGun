# De licentie-/dongle-blokkade van Terminator Salvation op onze eigen lader — GEMETEN

**Game:** Terminator Salvation (`game`, sha256 `798dec07…b206a`), onze lader `7f83edb2…`.
**Stand (2026-10-13):** op onze eigen `rg-elfloader` LAADT het spel, de ptrace/Yama-laag is geklaard
(zie `BESCHERMINGSLAAG-PTRACE.md`), de `/g3`- en `/T4User`-koppeling is gemaakt (zie
`KOPPELING-EN-IO.md`), en **met een scherm tekent het spel zijn attract** (KOPPELING §9). De ENIGE
resterende blokkade is de **licentie-/dongle-controle**: het spel beëindigt zichzelf na ~4 s met
`exit(0)`. Hieronder is die controle precies gemeten.

## Wat het spel doet om zijn licentie te halen — GEMETEN (block 2, net-log.c)

Vlak na de start (±0,03 s), vanuit het kind dat ook `nethasp.ini` zoekt:

```
connect(AF_UNIX /tmp/.aksusb) -> -1 errno=2   (tweemaal)      <- de Aladdin AKS-USB-daemonsocket ontbreekt
socket(AF_INET, SOCK_DGRAM); bind(0.0.0.0:0)
sendto(165 bytes -> 127.0.0.1:475)                            <- NetHASP-licentievraag (poort 475 = HASP LM)
sendto(165 bytes -> 255.255.255.255:475)                      <- en als broadcast
  ... vier keer herhaald, 1 s uit elkaar (0, 1, 2, 3 s) ...
(geen enkele recvfrom: er antwoordt NIETS)
```

Daarna, in zijn eigen logboek (`T4User/log/syslog_bb3.txt`, door het spel geschreven):

```
Failed aldeen: 2
DongleInitialize() FAILED!
```

En dan `exit(0)` op ~4 s. **`/g3`, `PhysX.conf` en de datafiles zijn GEEN blokkade meer** (ze openen of
worden niet meer gevraagd zodra de koppeling staat); de dongle-init is wat faalt.

## Waarom de oude dongle-aanpak hier niet raakt

- `game` heeft **0 `hasp_`-symbolen** in zijn dynsym (zie `README.md`). De oude drop-in haakte `game`
  en was daarom inert. **Dat is het verkeerde aangrijpingspunt.** Deze game vraagt zijn licentie **over
  een socket** (AF_UNIX `/tmp/.aksusb` en UDP `:475`), niet via een gelinkte HASP-client.
- `ServiceDongleApp` (de eigen licentie-/dienst-app van de game, Play Mechanix 2009, 8,9 MB, **niet
  gestript**) is géén kant-en-klare server: **met geen argumenten gedraagt hij zich identiek aan de
  game** — zelfde `/tmp/.aksusb`-probe, zelfde vier UDP-vragen naar `:475`, zelfde stille ~4 s, `exit 0`
  (block 3). Hij bindt dus NIET op `:475` en maakt `/tmp/.aksusb` NIET aan in deze modus.
- `ServiceDongleApp` heeft wél een dongle-/serverkant: symbolen `DongleInit/Open/Read/Write/Encrypt/
  Decrypt/GetID/IsPresent/FeatureLogin/ReadHLFeature/AcquireAladdinDongle`, USB via libusb
  ("claiming USB device", "detaching kernel driver"), en modusstrings `client`/`server`/`Master`/
  `standalone`/`SERVICE`, `/etc/hlrus`, `DirectDaemonCall`. Een server-/daemonmodus zit dus achter een
  argument/omgeving die nog niet gevonden is (`main` = `0x080bde10`, niet gestript).

## Wat de kast doet, en wat wij hebben

- Op de kast draait de Aladdin/HASP-runtime (`aksusbd`) die `/tmp/.aksusb` aanmaakt en de **fysieke
  USB-dongle** uitleest; en/of een NetHASP License Manager op `:475`. Wij hebben geen van beide en geen
  fysieke dongle.
- Wél hebben we de **vastgelegde donglegegevens** in de eigen `hasp/`-map van de game:
  **5410 bestanden van precies 32 bytes** met 32-hex-teken-namen (uitdaging → antwoord), plus
  `hhl_mem.dmp` (128 B, geheugenbeeld), en één bestand van 256 en één van 512 B. Dat is een **vastgelegde
  uitdaging-antwoordtabel** — genoeg om de crypto-challenge-respons van de dongle te **herspelen** zonder
  de geheime sleutel.

## De fix (richting; nog te bouwen — eigen stack, geen spelpatch)

Lever de licentie die het spel vraagt, op onze eigen stack:

1. **Eigen clean-room license-manager/aksusb-daemon** die luistert op AF_UNIX `/tmp/.aksusb` en/of UDP
   `127.0.0.1:475`, de 165-byte-vraag ontvangt en antwoordt uit de vastgelegde `hasp/`-tabel. Vereist:
   het `/tmp/.aksusb`- en/of NetHASP-`:475`-protocol begrijpen, en de afbeelding vraag → tabelbestand.
2. **Of** `ServiceDongleApp` in zijn servermodus draaien (het aangrijpingspunt vinden) en de USB-dongle
   er met de tabel aan voeren (libusb/ioctl hooken). Vereist: de servermodus + USB-emulatie.

**Referentie (benchmark, niet shippen):** de TeknoParrot-route maakte deze titel speelbaar (2026-10-09).
Die route observeren met `net-log.c` zou het **correcte antwoord op `:475`/`.aksusb`** laten zien — het
clean-room-doel om te reproduceren.

## Gereedschap dat er nu is
`patches/Terminator/net-log.c` (netwaarnemer, 32-bit preload: socket/connect/bind/sendto/recvfrom met
adres+poort), naast de bestaande `ptrace-log.c`, `bestand-log.c`, `fork-log.c`, `ptracer-permit.c`.
Ruwe metingen van block 2/3: `/tmp/ts-net-run/bewaard/`, `/tmp/ts-sda/log/`.
