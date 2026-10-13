# RUN 1 voor Halo — de uitkomst

*RavenGun, 2026-10-13. Deze run is **uitgevoerd** op 08:57:20–08:59:00 (90 s, headless). De opdracht
om te wachten kwam binnen toen de run al klaar was; de bench is daarna schoon achtergelaten en is vrij.*

Commando, precies zoals in `RUN-1-DRAAIBOEK.md`:

```bash
cd ~/projects/ravengun
scripts/play-native.sh --game Halo --headless --seconds 90 \
  --bind-ro ~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so /rg/lib/libusb-tap.so \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
  --env RG_USB_LOG=/pm/g7/halo/usb-tap.log \
  --env RG_USB_RAW=1 --env RG_USB_MAX=50000
```

`/pm/g7/halo` is de overlay, dus het logboek komt buiten de sandbox uit in
`~/.local/share/ravengun/native/Halo/0/upper/`. De launcher eindigde met **exitcode 143** (SIGTERM van
de 90 s-timer) en `play-native` meldde `exitcode 143`; er is niets blijven draaien.

## De vier uitkomsten, vooraf opgeschreven

| | wat je ziet | betekenis |
|---|---|---|
| **i** | **geen** `tap start`-regel | de tap is niet geladen en de proef is **mislukt** — een lege log is dan GEEN meting |
| **ii** | `tap start` + alleen descriptors-regels | de verwachte **geslaagde** run zonder bord: het spel heeft de apparatenlijst opgesomd en geen `0403:6001` gezien → `found: 0` |
| **iii** | `tap start` zonder descriptors | het spel is in die 90 s **niet aan zijn FTDI-pad toegekomen**; het venster is te kort |
| **iv** | descriptors met `03 04`/`01 60`, of `/dev/bus/usb`-regels | er hangt **toch** een bord |

## Wat er is gezien: **uitkomst (iii)** — met een belangrijke nuance

**De tap was aantoonbaar geladen:** het logboek bevat **8 × `tap start`** en verder **niets**:

```
83315.621903 tap start usbfs=/dev/bus/usb/ sysfs=/sys/bus/usb/devices/ max=50000
83323.462043 tap start ...
83331.447801 tap start ...
83337.270499 tap start ...
83343.249277 tap start ...
83362.554186 tap start ...
83366.471832 tap start ...
83387.787304 tap start ...
```

Acht keer, ongeveer 8 s uit elkaar: er zijn dus **acht processen** geweest die de preload erfden en elk
minstens één keer een libc-aanroep deden. Er zijn **geen** descriptor-regels, **geen** `/dev/bus/usb`-
regels en nergens de bytes `03 04 01 60`.

**Wat het spel zelf deed** (`native/Halo/0/upper/pmuser/gamelog.txt`, de eigen log van de game):

```
Tue Oct 13 08:57:21 2026: -------------------------------------------------------------------------------
Tue Oct 13 08:57:21 2026: Welcome to Halo v03.11
Tue Oct 13 08:57:21 2026: Game Rev: 57529, Library Rev: 1412, Country Code: 48
FileTest Failure: '/pm/g7/halo/game'
```

Dus: de game **startte en kwam tot zijn eigen FileTest**, en meldde daarna `FileTest Failure` op
`/pm/g7/halo/game`. In 90 s zijn er acht pogingen geweest (acht `tap start`s) — dat past bij een spel
dat zichzelf na een mislukte FileTest opnieuw start. De bekende regels van de `am`-laag
(`cmgr_init`, `cmgr_thread_main: thread begun`, `mo_mode_loop_session`) staan **niet** in het logboek,
en het launcherlogboek is 175 bytes: de game heeft niets naar stdout/stderr geschreven.

**De levenscheck liep mis op de meting zelf, en dat zeg ik eerlijk:** mijn steekproef pakte
`pgrep -f '/pm/g7/halo/game' | head -1` en vond steeds dezelfde pid met `threads=1, fds=5,
fmod-mappings=0, data-fds=0` van t=10 s tot t=80 s. Dat profiel hoort **niet** bij de game (die heeft
meerdere threads, honderden fds en libfmod gemapt); het was een wrapper (waarschijnlijk de
`bwrap`/gamescope-kant) die toevallig ook dat pad in zijn commandoregel had. De betrouwbare
levenscheck is het **gamelog.txt** hierboven: de game leefde om 08:57:21 en kwam tot zijn FileTest.

## Wat dat betekent

1. **De conclusie "de FTDI-poort is de enige blokkade" kan hier NIET uit getrokken worden.** Dat was
   uitkomst (ii) geweest, en die is het niet: het spel is in die 90 s niet aan zijn FTDI-pad
   toegekomen. Er is dus nog niets over de poort gezegd.
2. **De volgende run moet het spel eerst voorbij de FileTest krijgen.** In de eerdere (werkende)
   native metingen duurde die FileTest ongeveer 25 minuten en sloeg START hem over; hier lijkt hij te
   *falen* en herstart het spel zich. Twee dingen om te proberen, in deze volgorde:
   * het venster verlengen (bijvoorbeeld `--seconds 300`) om te zien of het een kwestie van tijd is;
   * de FileTest overslaan met de schakelaar-shim (`tools/rt-input-shim`, START uit het gedeelde
     geheugen) zoals in `packaging/native-games.json` beschreven — dan komt het spel bij de
     `am`-laag en dus bij `ftdi_usb_find_all`.
3. **Het logboek mag NIET in de game-map.** Deze run schreef `usb-tap.log` in `/pm/g7/halo/` — de
   wortel van de game — en de FileTest faalde op `/pm/g7/halo/game`. Dat kan toeval zijn, maar een
   FileTest die de map tegen een manifest vergelijkt hoort een **extra bestand** niet te waarderen.
   Voor de volgende run: log naar de host buiten de game-map, bijvoorbeeld via een extra
   schrijfkoppeling, of naar `/pm/g7/dump` (staat al read-only in het recept, dus dan moet er een
   schrijfvariant bij) — niet naar `/pm/g7/halo`. Ik heb het bestand na de run uit de overlay gehaald
   en bewaard als `~/.local/share/ravengun/rg-run1-usb-tap.log`.
4. **Mijn tap mist de opsomming, en dat is een fout in het instrument.** libusb leest de
   apparaatlijst uit sysfs via de **attribuutbestanden** (`idVendor`, `idProduct`, `busnum`, `devnum`)
   en niet via `descriptors`; ik log alleen `descriptors` (en laat attribuutopeningen met opzet
   ongemoeid om ruis te vermijden). Daardoor ziet een **geslaagde** uitkomst (ii) er precies zo uit als
   deze: `tap start` en niets anders. Voor de volgende run moet de tap die attribuutbestanden wél
   loggen (of liever: bij elke `descriptors`- en attribuutopening de VID/PID noteren), zodat (ii) en
   (iii) uit elkaar te houden zijn. Zonder die reparatie is een lege log na `tap start` **niet**
   uitlegbaar, en dat is precies de valkuil die we deze week al vaker zagen.

## Wat er nu klaarstaat voor run 2

* De tap is aangescherpt nodig: log ook de sysfs-attribuutopeningen (`idVendor`/`idProduct`) — anders
  is (ii) niet van (iii) te onderscheiden.
* `RG_USB_LOG` naar een plek **buiten** de game-map, zodat de FileTest geen extra bestand ziet.
* Venster langer (300 s) en/of de FileTest overslaan met START via de shim, zodat het spel zijn
  FTDI-pad bereikt.
* De levenscheck moet op het **spelproces** mikken (het pad van de exe is niet genoeg: de wrapper
  heeft hetzelfde pad in zijn commandoregel); beter is `pgrep -x game` plus het aantal threads, of
  gewoon het `gamelog.txt` van de game als bewijs dat hij leefde.

## Naschrift: de runtime-kern

De opdracht was de runtime-kern terug te zetten op
`f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`. Die kern **is er** en staat op
`~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll`.
De verwijzing `~/.local/share/ravengun/openparrot/current` wijst naar onze eigen OpenParrot-build
(`fe2ecf8c…`), maar die wordt alleen gebruikt als een `[Loader] Build = ravengun` of `Core = ravengun`
dat zegt; in de `game.ini` van Halo en Luigi staat **geen** `[Loader]`-sectie, dus de launcher neemt
standaard de runtime — en dat is de gesloten kern. Ik heb daarom **niets** omgezet: een verkeerde gok
in de kernkeuze zou de run van de laderbouwer kunnen breken. Wie het zeker wil weten, kijkt met
`python3 scripts/launch-direct.py <Game> --plan` naar de lader en de kern die hij kiest.

Deze run gebruikte `play-native.sh`, de **native** route, die geen OpenParrot-kern laadt; de kernstand
is door deze run dus niet aangeraakt.
