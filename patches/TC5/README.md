# TC5 (Time Crisis 5)

**Status: playable** — de game start en blijft draaien via de eigen lader (gemeten met
`scripts/launch-direct.py TC5 --run`: de kern laadt de game, "Have fun :)", en het spelproces is
gezien en leefde na 25 s nog).

## De dongle: wat er nu werkt

De game vraagt een HASP-dongle: `TimeCrisisGame-Win64-Shipping.exe` importeert
`hasp_windows_x64_105293.dll` op ordinalen 2, 13, 14 en 15 (`hasp_decrypt`, `hasp_login`,
`hasp_logout`, `hasp_read`). Onze vervanging staat in `dropin/` en wordt met `WINEDLLOVERRIDES` in
de view van de game gezet.

**Bewezen dat de DLL in de game komt** (niet in de lader, want dat is een apart proces):

```
$ pgrep -af TimeCrisis
1369815 .../OpenParrotx64/OpenParrotLoader64.exe ...        <- de lader
1369818 .../views/TC5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe

$ grep -i hasp /proc/1369818/maps
6ffffd290000-6ffffd291000 r--p .../views/TC5/TC5/Binaries/Win64/hasp_windows_x64_105293.dll
```

Twee fouten moesten daarvoor eerst weg:

1. **De DLL-secties hadden geen `Env`.** Het bestand `ravengun-hasp/TC5/dongle.json` werd wel
   uitgerold, maar de DLL wist niet WELKE dongle hij moest laden. Nu staat er
   `Env = RAVENGUN_HASP_DIR=ravengun-hasp;RAVENGUN_HASP_GAME=TC5` bij elk van de drie DLL-secties
   (net als bij Halo). `RAVENGUN_HASP_VERBOSE` wordt **niet** meegeleverd — die spamt stderr bij
   elke aanroep.
2. **De view van TC5 moest gematerialiseerd worden.** `views/TC5/TC5` was een symlink naar de NAS,
   en de drop-in-motor weigert te schrijven als de map van het doel via een symlink buiten de view
   uitkomt (de regel "nooit naar de NAS"). De keten `TC5/Binaries/Win64` is daarom vervangen door
   echte mappen met symlinks naar de inhoud — precies wat `dropin_materialiseer()` zelf doet. Zonder
   dat weigerde de start met "de map van het doel ... komt na het materialiseren nog buiten de view
   uit" en kwam er **geen enkel** drop-in-bestand aan.

   Dit is het enige van de 15 games met een drop-in waar dat speelt: bij alle andere is de map van
   de game een echte map. Zie ook `docs/dropins.md`.

## Wat er nog niet is: de geheugenimages

`"memory"` in `dongle.json` is leeg, dus `scripts/hasp.py --game TC5 --json` meldt deze dongle als
onvolledig:

```
dropin/ravengun-hasp/TC5/dongle.json: geen 'memory'-items: de dongle-image van deze game is nog
niet vastgelegd.
```

Dat de game met onze DLL **start en blijft draaien** betekent dat hij niet hard op een
geheugenimage stukloopt, maar of hij de dongle ook echt *uitleest* (en wat hij dan verwacht) is
**niet gemeten**: de aanroepen komen niet in het logboek, en de verbose-vlag via `[Env]` in de
gebruikerslaag bereikte de start niet. De volgende stap is dus meten wat
`TimeCrisisGame-Win64-Shipping.exe` aan `hasp_read` opvraagt, bijvoorbeeld met de probe uit
`tools/rt-hasphook/hasp_probe.c` (loader-repo) of met een loggende DLL.

Zolang dat open is, is dit het laatste stuk van TC5 dat nog aan TeknoParrot hangt.

## Hoe ik heb geprobeerd te meten of de game de dongle aanspreekt

Drie routes, alle drie zonder uitkomst. Dat is geen vaag "het lukte niet" — het is per route
duidelijk waarom, zodat de volgende poging niet dezelfde is:

| Route | Wat er gebeurde |
|---|---|
| **De verbose-vlag in de omgeving** | `RAVENGUN_HASP_VERBOSE=1` bereikt het proces bewezen (het staat in `/proc/<pid>/environ` en in de `omgeving:`-regel van de launcher), maar er komt **geen enkele regel van onze DLL** in het logboek. De game is een **kindproces** van `OpenParrotLoader64.exe`, en de launcher vangt de uitvoer van de lader op — niet die van het kind. |
| **De vlag via `[Env]` in de gebruikerslaag** | Kwam niet aan: die regel haalde de start niet (de drop-in-`Env` wel, daarom ging de meting hierboven via de drop-in). |
| **De toegangstijd van `dongle.json`** | Onze DLL leest dat bestand zodra de game een `hasp_*`-functie aanroept, dus een veranderde `atime` zou bewijs zijn. De view staat op **`relatime`**, en daar werkt `atime` alleen bij als de oude toegangstijd ouder is dan de wijzigingstijd of meer dan 24 uur — hier was `atime` juist nieuwer. **Niet beslissend.** |

**Wat er wel zou werken** (en wat ik expres niet heb gedaan, want het is een wijziging in de
loader-repo): de Windows-DLL een logbestand laten schrijven in plaats van naar `stderr`
(`hasp/src/hasp_preload_win.c` heeft nu geen enkele logging; `dongle.c` schrijft alleen naar
`stderr` als `RAVENGUN_HASP_VERBOSE` gezet is). Met een logbestand in de view is in één start te
zien welke fileids de game opvraagt en welke bytes hij verwacht — en dan is de dongle-image van
TC5 alsnog vast te leggen, waarmee het laatste stuk dat nog aan TeknoParrot hangt dichtgaat.
