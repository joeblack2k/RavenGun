# Rainbow Bombergirl

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/RainbowBombergirl.md` op de NAS
naast de games, niet in deze repo.

## Wat deze game is

| | |
|---|---|
| Naam | Rainbow Bombergirl |
| Fabrikant | Konami Amusement (`findings/RainbowBombergirl.md`, kop) |
| Jaar | 2022 |
| Platform | KONAMI PC Based, Unity 2017.2 Mono 64-bit |
| Genre | Multiplayer (per `Metadata/RainbowBombergirl.json`) |
| Lader | `Type = TeknoParrot`, `EmulationProfile = WartranTroopers` → **eigen** `OpenParrotLoader64.exe` + de TeknoParrot-kern |
| Profiel | `GameProfiles/RainbowBombergirl.xml`, revisie 6, `Patreon = true` |
| Plugin | BepInEx 5.4.23.2 met de TeknoParrot-plugin `BBGTPPlugin` |

Naam, jaar, platform en genre komen uit `Metadata/RainbowBombergirl.json`; de engine, de plugin en de
fabrikant uit de kop van `findings/RainbowBombergirl.md`. De laderfamilie komt uit
`docs/loskoppelen-per-game.md` (rij 29) van de loader-repo.

Deze game heeft **twee schermen**: een staand hoofdscherm rechts en een aanraakscherm links onder.
`game.ini` zet daarvoor `[Display] CustomResolution = 1920x1080` en in het profiel
`SingleMonitorLayout = FakeLandscapeTouchBottom`; de invoer loopt via `[Input] Route = tp-shm` met
indeling `WartranTroopers`. `[Wine]` zet `DllOverrides = winhttp=n,b;d3d11,d3d10core,dxgi=b` (de
BepInEx-plugin moet laden) en `GStreamerPlugins` naar `~/.local/share/ravengun/tools/gst-extra/plugins`
— zonder die map stopt de game met exitcode 1 zodra de eerste film begint (winegstreamer mist de
ASF-demuxer).

## Stand van de start

**START, en blijft draaien.** In `patches/STARTTEST.md`: spelproces gezien op seconde 2, de lader
leefde nog, laatste logregel `[launch-direct …] klaar (exitcode gestopt)`.

**Maar de game komt niet verder dan het begin** — daarom staat `[Status] Status = parked`. De meting
uit `findings/RainbowBombergirl.md`: de game start tot de attract ("PRESS ANY BUTTON"), het testmenu
werkt (I/O-check toont alle knoppen: links = trekker, midden = herladen, rechts = granaat, EXT = aux1;
Test = toets 0, service = F3), het aanraakscherm en de coin-opties werken, munt loopt op — maar na
munt en start blijft het bij **SYSTEM ENTRY / de PASELI-melding** hangen en wordt de personagekeuze
nooit bereikt. Na de timer van 30 s herstart de game naar de titel.

De verklaring uit ronde 23 (vragen 74-76): het profiel heeft `ForceMouseAsTouchInput = 1` en alleen de
knop **Test**; **de game start met een aanraking (muisklik) op de startknop van het aanraakscherm, niet
met een knopbit**. Er is ook geen `FreePlay`-veld in het profiel, dus er zijn munten nodig (of
freeplay via het eigen testmenu). De volgende proef staat in de bevindingen: `DisplayMode = Fullscreen
Windowed` zonder onze `CustomResolution`, munt, dan een muisklik op de oplichtende Start-knop.

## Dongle en patches

**Regio/PCB, geen USB-sleutel.** Het profiel heeft een `Region`-veld en `game.ini` zet
`[Config] General/Region = EXPORT` (`GameProfiles/RainbowBombergirl.xml`; `docs/loskoppelen-per-game.md`
rij 29 noemt dit "regio/PCB (EXPORT)"). Regio is een **configuratiewaarde**, geen dongle-API:
`findings/WAT-ELK-SPEL-NODIG-HEEFT.md` zet de regio-games in een eigen laag, los van de HASP, en
`docs/region-layer.md` beschrijft die laag.

Er is **geen `dongle.json`**, geen `dropin/` en geen `patches.ini`. Volgens de afspraak
(`docs/dropins.md`) hoort een regiogame een `dongle.json` van het soort `region` te hebben; die
ontbreekt hier, en `patches/STAND.md` telt dat als gat. Er is geen `[Dongle]`-sectie en die hoort er
ook niet te komen: de loader kent geen dongles meer (`docs/geen-dongle-in-de-loader.md`).

**Wat niet is vastgesteld:** of deze regio-check de game tegenhoudt. Voor LGI, LGI3D en LGS is gemeten
dat de game terugvalt op zijn eigen `DefaultRegion` (`"enforced": false` in hun `dongle.json`); voor
Rainbow Bombergirl is dat **niet** gemeten (open vraag 14 in `docs/loskoppelen-per-game.md`). De
parkeerreden is ook niet de regio maar het aanraakscherm bij SYSTEM ENTRY.

## Wat er nog ontbreekt

1. **`dongle.json` van het soort `region` met de waarde `EXPORT`** — of de vastlegging dat de regio
   niet afgedwongen wordt, zoals bij de Let's Go-games.
2. **De start na SYSTEM ENTRY**: een muisklik op de Start-knop van het aanraakscherm, en de proef met
   `DisplayMode = Fullscreen Windowed` zonder eigen resolutie. Zolang dat niet lukt blijft de game
   geparkeerd.
3. **Werksoort 2 — de eigen kern.** De gesloten `TeknoParrot(64).dll` draait mee voor het
   `WartranTroopers`-profiel; dat is centrale arbeid, geen map (`docs/loskoppelen-per-game.md`).
4. **De `Patreon`-activering.** Het profiel heeft `Patreon = true`, dus de prefix-activering is nodig
   zolang de gesloten kern draait (open vraag 6 in `docs/loskoppelen-per-game.md`: of dat echt
   blokkeert is niet gemeten).
5. **README** — dit bestand; in `patches/STAND.md` stond deze game tot deze ronde zonder README.

## Verifiëren

```sh
cd ~/projects/ravengun-patches
python3 patches/stand-gen.py                        # de tabel en de tellingen in patches/STAND.md

cd ~/projects/ravengun
python3 tools/verify-all.py RainbowBombergirl
python3 scripts/launch-direct.py RainbowBombergirl --plan
```
