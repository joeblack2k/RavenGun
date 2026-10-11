# Terminator Salvation

Instellingen: `game.ini`. Dit bestand gaat over de **HASP-dongle** van deze game.
Bevindingen en meetgeschiedenis: `findings/Terminator.md` (op de NAS naast de games, niet in deze repo).

## Vraagt deze game een dongle? — JA, maar niet via een aanknoopbare HASP-client

Bewijs, gemeten op `Terminator Salvation/game` (ELF 32-bits, 7.764.284 bytes, **met**
sectiekoppen, gestript):

* **0 `hasp_`-symbolen in de dynsym** (1.019 dynamische symbolen, geen enkele met `hasp`), 0
  ongedefinieerde `hasp_`-symbolen, 0 verplaatsingen met `hasp`, en **geen** `@@HASPE`-export.
  Er is dus geen statisch gelinkte HASP-client om aan te haken; `docs/hasp-elf-hook.md` §5 zegt
  hetzelfde.
* De game gebruikt wél een Aladdin/HASP-dongle. Dat blijkt uit zijn eigen tekstregels:
  * in `game`: `DongleInitialize() FAILED!`, `ReInitAladdinDongles called: %d times`,
    `Too many consexutive dongle errs (%d), bailing`, en de losse tekenreeks `dongle`
  * in `t4-patch`: `failed to find aladdin dongle`, `failed to read dongle id`,
    `Unable to read seed from dongle`
* In de spelmap staat een eigen `hasp/`-map met 5.413 bestanden, vrijwel allemaal 32 bytes, met
  32- of 64-tekens hexnamen. De inhoud is per bestand uniek (5.413 unieke MD5's). Daarnaast staat
  er `hhl_mem.dmp` (128 bytes) — een HASP-HL-geheugendump.

## Wat er in de drop-in staat

```
dropin/
  dropin.ini                                   een sectie per bestand hieronder
  ravengun-hasp.so                             de HASP-emulator, 32-bits (nieuw)
  ravengun-hasp/Terminator/dongle.json         de donglebeschrijving (nieuw)
  ravengun-hasp/Terminator/memory/00000000000000001111111111110100   de image, 128 bytes (nieuw)
```

**Er is met opzet GEEN inline-hook.** Er valt in `game` niets aan te haken (0 `hasp_`-symbolen).
Daardoor is deze drop-in voorlopig **inert**: de bestanden worden in de view gezet, maar niets
spreekt de emulator aan. Hij staat er omdat de donglebeschrijving de enige vastgelegde bron van deze
dongle is en omdat een latere hook (of de eigen lader) hem wel kan gebruiken.

Er is **geen `[Dongle]`-sectie** in `game.ini`: de loader weet niets van dongles.

## De dongle

| veld | waarde | herkomst |
|---|---|---|
| `kind` | `hasp` | het soort dongle |
| `fileid` | **65524** (`0xFFF4`) | **AANNAME**: de standaard HASP-HL-hoofdgeheugen-fileid zoals in dit project bekend. Deze game geeft zelf geen fileid door |
| `size` | **128** | de bestandsgrootte van `hhl_mem.dmp` |
| `name` | `memory` | geen gemeten semantiek; de neutrale naam die `hasp/src/dongle.c` zelf als standaard gebruikt |
| `haspid`, `features`, `keycaps` | **weggelaten** | niet gemeten |

### De bytes komen WEL uit een echte bron

`Terminator Salvation/hasp/hhl_mem.dmp` is een HASP-HL-geheugendump van 128 bytes die in de eigen
`hasp`-map van de game staat. De image in deze drop-in is dat bestand **byte voor byte**:

* sha256 `759668aeac17ccae66385b365bbe9e19504cde93b4374fd966a8edf1d04a8a8e`
* eerste 8 bytes `da 36 7e a4 fc d0 ff 3d`

**Ik heb het bestand niet ingekort.** Het is 128 bytes, terwijl de image van Halo Fireteam 112 bytes
is. De laatste 16 bytes zijn `57 42 50 54` (`WBPT`) + `01 00 00 00` + `10 00 00 00` + `00 00 28 b1`,
wat op een staart of merk van een gereedschap lijkt — maar **niets in de bronnen zegt dat er iets
af moet**, dus is de hele file de image. Dat is de minst inventieve keuze: het bestand is de bron.

## Verifiëren

```sh
cd ~/projects/ravengun
python3 scripts/hasp.py --game Terminator --json     # ok: true, errors: []
python3 scripts/launch-direct.py Terminator --plan
```

De dongle wordt door de emulator geserveerd (gemeten met dezelfde 32-bits probe (een geparametriseerde kopie van `tools/rt-hasphook/hasp_zelftest.c`, zie de README van JurassicPark); de hook is de
generieke bibliotheek uit de drop-in van JurassicPark, want hier hoort geen hook):

```
hasp_login(0xffff0000) -> 0, greep = 0xe6f
hasp_get_size(65524) -> 0, grootte = 128
hasp_read(65524, 0, 16) -> 0, eerste bytes: da 36 7e a4 fc d0 ff 3d
echte_aanroepen = 0
RESULTAAT: alle aanroepen zijn door de EMULATOR beantwoord; de statisch gelinkte client is NIET aangeroepen
```

## Wat NIET is vastgesteld

* **Hoe deze game zijn dongle precies uitleest.** Er zit geen HASP-client in `game`. De 5.413
  bestanden in `hasp/` zijn nog niet uitgezocht; de namen zijn hashes en het opslagpatroon lijkt op
  een opzoektabel per aanroep, niet op het fileid-formaat van deze emulator.
* **De fileid.** 65524 is een aanname (de standaard HASP-HL-hoofdgeheugen-fileid); de game geeft er
  geen. Omdat er geen hook is, wordt er op dit moment ook niets gelezen.
* **Of de 16-byte staart van `hhl_mem.dmp` wel of niet bij het donglegeheugen hoort.**
* **Of de dongle de enige hindernis is.** Het spel is via TeknoParrot speelbaar (zie `game.ini`), dus
  in de praktijk wordt de dongle daar al door de core afgehandeld; native stopt het met
  `DongleInitialize() FAILED` (`findings/Terminator.md`).

## Herhalen: de keten end-to-end meten

De metingen hierboven komen van een 32-bits probe die zijn eigen `hasp_*`-functies **statisch in het
beeld** heeft — precies de situatie van `game`. Die probe is een geparametriseerde kopie van
`tools/rt-hasphook/hasp_zelftest.c` uit de loader-repo; het enige verschil is dat de fileid uit
`RAVENGUN_PROBE_FILEID` komt, zodat dezelfde test voor 65520, 65522 en 65524 kan draaien.

```sh
# 1. de probe bouwen (de -I is nodig: de dev-header gnu/stubs-32.h ontbreekt op SteamOS en
#    scripts/build-hasp.sh maakt hem lokaal in hasp/src/inc/gnu/ aan)
gcc -m32 -I ~/projects/ravengun/hasp/src/inc -O1 -rdynamic -o hasp_probe <de kopie>

# 2. draaien met de hook en de dongle van deze game
LD_PRELOAD=<dropin>/libravengun-hasphook.so \
RAVENGUN_HASP_VERBOSE=1 \
RAVENGUN_HASP_GAME=Terminator \
RAVENGUN_HASP_DIR=<dropin>/ravengun-hasp \
RAVENGUN_PROBE_FILEID=65524 \
HASP_ZELFTEST_WACHT=2000 ./hasp_probe
```

De probe wacht standaard 1500 ms (`HASP_ZELFTEST_WACHT`) omdat de hook vanuit een kijker-thread
werkt en het spelbeeld eerst moet vinden. Het logboek van de hook staat in `RAVENGUN_HASP_ELF_LOG`.
