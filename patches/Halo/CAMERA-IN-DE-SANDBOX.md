# De camera in de sandbox: waarom het spel hem nooit zag, en de één-regel-reparatie (GEMETEN)

*RavenGun, 2026-10-13. Bench-vrij uitgezocht met bubblewrap en de camera zelf. Niets gestart, niets
geschreven, niets aan de daemon veranderd. Geen commit.*

## 1. De camera is er, en de daemon streamt hem (GEMETEN)

```
/dev/video0  crw-rw----+ root video 81, 0   driver ov534
             "USB Camera-B4.09.24.1", by-id usb-OmniVision_Technologies__Inc._USB_Camera-B4.09.24.1
ravengund:   fd 7 -> /dev/video0, TOESTAND STREAMING
             YUYV 640x480, gevraagd 60 fps, driver 60.00 fps
             325.218 frames, 60,02 fps gemeten, 0 gemist, 0 fout, p50 interval 16,658 ms
```

**En de driver laat een tweede opener toe** -- dat is de vraag die de coördinator stelde, en het antwoord
is gemeten: buiten de sandbox lukte `open("/dev/video0", O_RDWR)` **terwijl ravengund aan het streamen
was** (en ook `O_RDONLY`). De camera is dus niet bezet door de daemon.

## 2. Waarom het spel hem nooit zag (GEMETEN)

* in onze runlogboeken komt `/dev/video0` **nul** keer voor;
* in het recept (`packaging/native-games.json`) en in `scripts/play-native.sh` staat **geen enkele**
  verwijzing naar `/dev/video*` -- de sandbox geeft hem dus niet door;
* `play-native.sh` geeft `/dev/dri` door met **`--dev-bind`** (regel 207) en `/dev/input` met
  `--dev-bind-try`, maar **niets voor video**.

Daarmee is `Webcam: Could not ID device for player 0` in al onze runs **triviaal verklaard**: het spel
kreeg de camera niet, en zocht dus in het niets.

## 3. EN DE KOPPELING DIE WERKT -- `--dev-bind`, niet `--bind` (GEMETEN)

Ik heb vier bwrap-vormen naast elkaar gezet, met dezelfde koppelingen als `play-native`, en in elke vorm
geprobeerd `/dev/video0` te openen:

| vorm | eigenaar/mode binnen | `open(O_RDWR)` |
|---|---|---|
| `--bind /dev/video0 /dev/video0` (wat `--bind-rw` doet) | `20660 65534:65534` | **MISLUKT, errno 13 Permission denied** |
| **`--dev-bind /dev/video0 /dev/video0`** | `20660 65534:65534` | **GELUKT** |
| `--bind ... --chmod 0666` | -- | bwrap weigert: *"Can't chmod 0666: Operation not permitted"* |
| `--dev-bind /dev /dev` | `20660 65534:65534` | **GELUKT** |

**Dus: onze `--bind-rw` is voor een apparaat het verkeerde middel, en `--dev-bind` is het goede.** Dat is
ook logisch: de gebruiker `deck` zit **niet** in groep `video` (`id`: deck, steamos-log-submitter, wheel)
-- de toegang buiten de sandbox komt van een **ACL** op het apparaat, en die ACL overleeft de
`--bind`-koppeling niet. `--dev-bind` zet het apparaat zo neer dat de sandbox het wél mag gebruiken.
En dat is geen nieuwe truc: **`play-native.sh` gebruikt `--dev-bind` al voor `/dev/dri`.**

## 4. De reparatie, in één regel (bij de eigenaar van het script)

In `scripts/play-native.sh`, in de regel waar `/dev/dri` al met `--dev-bind` wordt doorgegeven:

```sh
--dev-bind /dev/dri /dev/dri --dev-bind-try /dev/video0 /dev/video0 --dev-bind-try /dev/snd /dev/snd
```

(`--dev-bind-try` zodat een machine **zonder** camera geen fout geeft -- precies zoals `/dev/snd` en
`/dev/input` dat al doen.) En eventueel als CLI-optie, net als `--bind-ro`/`--bind-rw`, zodat per game
gekozen kan worden welke camera's mee mogen.

**Wat ik NIET doe:** dat script aanpassen -- dat is niet mijn bestand. Ik lever de meting en de regel.

## 5. Wat dit betekent voor de fabrieksinstelling en de QR-meldingen

De binary noemt in dat menu de webcam-auto-identificatie (`AUTOID_CLEAR/INPROGRESS/ERROR/DONE`,
`Baseline`/`New lum ... (diff:)`) en `QRSetupProc`. **Onze runs konden die stap nooit halen, want er was
geen camera.** Met de camera erbij is de verwachting voor de volgende run:

1. **`Webcam: Could not ID device for player 0` hoort te verdwijnen** en vervangen te worden door een
   auto-ID met een **helderheidsmeting** (`Baseline ...`, `New lum ... (diff: ...)`) -- gemeten aan het
   logboek;
2. en mogelijk meldt het spel daarna een andere toestand op het scherm (de webcam- en QR-meldingen
   horen bij de webcamstap, dus die kunnen hiermee veranderen).

**Volgorde voor de volgende run:** (a) eerst de camera erbij (`--dev-bind`) en kijken wat het logboek nu
zegt -- dat is de goedkoopste en meest directe meting; (b) dan de diag-route (TEST, bit 0 -> k=4, lang en
herhaald, **in de attract-lus**) voor `Factory Setup` / `Gun Info` / de geweercamera-test; (c) en het
fabrieksinstellingsitem **niet uitvoeren** zonder akkoord -- het menu openen en fotograferen is lezen.
