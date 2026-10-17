# Far Cry: Paradise Lost zonder TeknoParrot

Stand 2026-10-17. Alles uit de game (NAS, alleen gelezen) en de open TeknoParrotUI-bron; de gesloten kern is niet geopend, niets is gestart.
Bronnen: `objdump -d` van `Farcry_R.exe` (PE32, 6,2 MB) en van `Usbioextreme.dll` (69 KB, in de gamemap), TeknoParrotUI `Pipes/FarCryPipe.cs`.

## Wat TeknoParrot (EmulationProfile FarCry) hier doet

De UI schrijft per 15 ms het bordbeeld (`FarCryPipe`: Control op offset 8, vier asbytes op 12/16/20/24). Wat de kern daarmee doet is dicht;
afgeleid uit de game zelf zijn er drie dingen die hij voor de game moet regelen:

1. **De USB-IO van de kast.** `Farcry_R.exe` importeert zes namen van `Usbioextreme.dll` (klasse `CUSBIO`): ctor, dtor, `CUSBIO_Init`,
   `CUSBIO_Update`, `CUSBIO_GetLastError`, `CUSBIO_Close`. Het origineel praat via HID (hid.dll/setupapi) met een "USBIO Extreme"-bord. Zonder
   bord meldt `Init` 0 en valt de game terug op DirectInput (twee gamecontrollers + toetsenbord, functie op 0x7ae180). Waar de kern
   hier precies ingrijpt (hid-haken of de exports) is niet te zien; de drop-in hieronder maakt dat onnodig.
2. **Venstermodus.** `systemcfg.lua` staat op `r_Fullscreen = "1"` (1920x1080). De kern gaf met `[Config] Windowed = 1` een venster; zonder dat
   bleef de hoofddraad in `UpdateWindow` (onder gamescope) of `ChangeDisplaySettings` hangen (game.ini, meting 2026-10-15).
3. **De dongle: niet vastgesteld.** De game heeft de statische HASP-4-klassiek-bibliotheek (sectie `_TEXT_HA`: DeviceIoControl op de driver,
   HASPDOSDRV, NetBIOS, WinSock-NetHASP, "Terminal Server"-controle) en de Lua-eigenschappen `P_DongleCheck`, `IncorrectDongle`,
   `a_dongle`. Er staat geen dongle-bestand in de gamemap. Of de game zonder werkende HASP doorgaat, of dat de kern die controle onderschept,
   is statisch niet te beslissen (de aanroep loopt via een functiewijzer en Lua). Dit is het openstaande risico van `Core = none`.

## Wat we gemeten hebben aan de game: het record

De game bezit op +0xac van zijn invoerklasse een `CUSBIO` (8 bytes): +0 aantal borden, +4 wijzer naar 0x34 bytes per bord. `Init(this)` vult die
twee, `Update(this, i)` schrijft record i (het origineel kopieert uit zijn leesdraad; `0x10001900`). De game leest alleen record 0, in de functie
op 0x7af480, en de teller-lezer op 0x7ac7e0:

| Record | Betekenis | Bewijs |
|---|---|---|
| byte 7 / 6 | X / Y speler 1 | `0x7af601`: ruwe waarden naar `0x7acfa0` ((ruw - min)/(max - min)*2-1; Y wordt daarna omgekeerd) |
| byte 9 / 8 | X / Y speler 2 | idem |
| 7 = 6 = 0 | "geen volgsignaal" | `0x7af629` |
| 0xf bit 2, 0xe bit 0 | trekker, tweede knop speler 1 | `0x7af68b..0x7af6a1` (`[obj+0xa]`, `[obj+0xb]`), DirectInput-tak zet dezelfde velden uit knop 0/1 |
| 0xf bit 3, 0xe bit 1 | trekker, tweede knop speler 2 | `[obj+0xc]`, `[obj+0xd]` |
| 0xf bit 0/1, 0xe bit 2/3 | vier bedienknoppen (`[obj+8]` bit 4/8/1/2) | tabel op 0x7ac66c, acht gevallen; **welke knop welke is: niet gevonden** |
| dword 0x10 / 0x14 | muntteller 1 / 2 | `0x7ac7e0` |

Het bord telt de munt als teller (16 bit, b13:b14 van het HID-rapport); de game kijkt naar het verschil.
Er is **geen** startknop op het bord (de game zegt "PULL TRIGGER TO START").

De calibratie (`Arcade_Calibration.txt`, 4x min/max; zonder bestand min 10, max = `m_arcadeAxisResolution` = 2000) is in de eenheid van de ruwe
assen. De kast-eenheid is 0..2000; het record heeft bytes. Daarom levert de drop-in `Arcade_Calibration.txt` met 1..255.

## Wat gebouwd is

| Bestand | Wat |
|---|---|
| `tools/rg-farcry/rg_usbio_logic.h` | de rekenregel (tp-shm bordbeeld -> record), puur C, getest |
| `tools/rg-farcry/rg_usbio.c`, `usbioextreme.def`, `build.sh` | de DLL (PE32, kernel32 alleen); zelfde 29 namen en ordinalen als het origineel; thiscall als fastcall met edx-schaduw, zelfde `ret N` |
| `tests/farcry_usbio_unit.c`, `tests/farcry_usbio_test.sh` | eenheidstest van de rekenregel + bouwproef van de DLL met controle op de 29 exports |
| `patches/FarCryParadiseLost/dropin/` | `Usbioextreme.dll`, `Arcade_Calibration.txt` (1..255), `systemcfg.lua` en `Systemcfgoverride.lua` (r_Fullscreen "0"), `docs/research/losse/FarCryParadiseLost.dropin.ini` |
| `docs/research/losse/FarCryParadiseLost.game.ini` | de kandidaat: `[Loader] Core = none` |

`rg-direct --plan` met de kandidaat (`Core = none`, `dropin.ini`): argv = gamescope + wrapper + `W:\Farcry_R.exe`, vier drop-ins naar de view, schijf W:,
pipehelper (de spiegel naar `TeknoParrot_JvsState`) aanwezig, `WINEDLLOVERRIDES=d3d9=n,b`.

## Waarom dit nog niet actief is

`patches/FarCryParadiseLost/game.ini` staat nog op de TeknoParrot-kern en werkt (5,5 min gespeeld, 2026-10-17). `dropin/dropin.ini` bestaat daarom
**niet**: de launcher zou onze DLL en de 1..255-kalibratie anders ook onder de TeknoParrot-kern plaatsen. Activeren = `docs/research/losse/FarCryParadiseLost.dropin.ini`
hernoemen naar `dropin.ini` + de `[Loader]`-sectie van de kandidaat in `game.ini`.

## TV-proef (Core = none + onze DLL)

1. Activeren (zie boven), game starten met dubbelklik, 60 s wachten. In de view moet `rg-usbio.log` staan met `geladen`, `Init: een bord gemeld` en
   `shm gekoppeld`. Ontbreekt de DLL-regel: de game laadt een andere `Usbioextreme.dll` (de symlink naar de NAS is niet vervangen).
   Staat er `Init` maar geen `shm`: de pipehelper-spiegel ontbreekt.
2. Beeld: attract "PULL TRIGGER TO START" (zoals nu). Hangt hij op een zwart scherm met 60% CPU: de dongle (zie boven) of de venstermodus.
3. Munt (toets 5): `ctl` in het log met `c=1` en credits in de game. Trekker: kaartkeuze schietend. Richten: het vizier volgt de muis; staat X of Y
   gespiegeld, `RG_USBIO_INVERT=x` of `y` in `Env` van de dropin.ini-sectie `[Usbioextreme.dll]` zetten. Eerste bedienknop (Test, toets 0): geeft
   het servicemenu? Zo niet, `RG_USBIO_OPS` omzetten.

## Open

* Dongle (zie 3): niet gemeten of de game zonder kern doorgaat. Zo niet, dan is de volgende stap de HASP-controle in `_TEXT_HA` vinden en met de
  generieke PE-haak (`tools/rt-hasphook`) beantwoorden.
* Welke van de vier bedienknoppen test/selecteren/omhoog/omlaag is; de richting van de Y-as; of het 1..255-bereik de randen haalt.
* Venstermodus via `systemcfg`: niet gemeten of `Systemcfgoverride.lua` na `systemcfg.lua` gelezen wordt (daarom staat de waarde in beide).
* Startknoppen (P1/P2 start van de app) hebben geen bord-bit en doen niets; de game start op de trekker.
