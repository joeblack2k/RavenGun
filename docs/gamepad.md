# Gamepad naast de lightgun

Een gamepad (Xbox-controller, Steam Controller, elke evdev-gamepad) is een extra bediening voor een speler, naast zijn lightgun
(PS Move of muis). Hij is bedoeld voor wat een lightgun mist: een stuur (Let's Go Safari, Mario Kart Arcade GP DX), gas en rem, en
extra kastknoppen (camera, wissers, claxon, schakelen). De knoppen van de gamepad en die van de lightgun werken allebei (OR).

In de app: **Controllers > Gamepads** (toewijzen per speler, live weergave, de standaardindeling) en **Properties > General >
Gamepad** (de indeling die een game gebruikt en waar elke regel vandaan komt).

## Toewijzen

- Hoogstens één gamepad per speler (1..4). De toewijzing staat in `config.json` (`"gamepads": {"1": "<id>", ...}`) en overleeft
  een herstart van de daemon (anders dan de muis).
- **Automatisch:** heeft speler 1 geen gamepad en is er precies één gamepad aangesloten (die niet aan een andere speler hangt), dan is
  dat de gamepad van speler 1. Met één pad hoeft er dus niets ingesteld te worden.
- CLI: `ravengunctl pads` (aangesloten pads met live stand, toewijzingen, de actieve indeling; `--all` toont per invoerapparaat waarom
  het wel of geen gamepad is) en `ravengunctl pad --player N --pad ID` of `--off`.
- De pad wordt alleen gelezen (`O_RDONLY`, niet gegrabd): de desktop en Steam houden hem ook. Alleen de pads die bij een speler
  horen worden geopend. Met `ravengund --no-hid` (tests) worden er geen pads geopend.

### Wat telt als gamepad

Een invoerapparaat met `EV_KEY` `BTN_SOUTH` (= `BTN_GAMEPAD`) en `EV_ABS` `ABS_X` en `ABS_Y`, behalve onze eigen virtuele
apparaten (vendor 0x1209 of een naam die met "RavenGun" begint), `libvirtualhid` en de PS Move ("Motion Controller"). De virtuele
pad van de Steam-client ("Steam Virtual Gamepad", "Microsoft X-Box 360 pad N") en de Steam Controller via `hid-steam` tellen wel.
De daemon logt bij elke verandering per invoerapparaat waarom het wel of geen gamepad is (`ravengunctl pads --all` toont hetzelfde).

Let op bij de Steam Controller: zolang de Steam-client hem vasthoudt, laat hij een virtuele Xbox-pad zien; zonder Steam maakt
`hid-steam` er zelf een gamepad van. Zijn beide er, dan zijn er twee pads en geldt de automatische regel niet (kies er dan een).
Met de controller uit laat de puck alleen muis- en toetsenbordinterfaces zien (geen gamepad).

Het id is de naam van de symlink in `/dev/input/by-id` (bv. `usb-Microsoft_Controller_..-event-joystick`), anders
`pad-<naam>-<8 hex>`: een hash van `Uniq` (Bluetooth: het adres van de pad) of anders `Phys`. Het adres zelf staat dus niet in het
id of in het logboek.

## De indeling: `[Gamepad]` in game.ini

Sleutels zijn bedieningen, waarden zijn signalen. Zonder `[Gamepad]` geldt de standaardindeling:

```ini
[Gamepad]
LeftX = wheel
RT = gas
LT = brake
A = trigger
B = reload
X = grenade
Y = aux1
LB = btn1
RB = btn2
Start = start
Back = coin
```

Een `[Gamepad]`-sectie gaat **per sleutel** over de standaardindeling heen (zoals elke laag in game.ini): `RightX = wheel` voegt een
tweede stuur toe en laat de rest staan; `LT = none` haalt een bediening weg. De lagen zijn de gewone (`_defaults/<Type>.ini`, het
meegeleverde `patches/<Game>/game.ini`, dan het game.ini van de gebruiker in `~/.local/share/ravengun/patches/<Game>/`); de
gebruikerslaag wint. Een lege waarde (`LB =`) haalt alleen jouw regel weg, zodat de meegeleverde of standaardwaarde weer geldt.

Welke indeling geldt: die van de game die de IO-brug het laatst startte (`iobridge start`, elke start vanuit de app), op elke route
(ook evdev). Voor de eerste start: de standaardindeling. De daemon logt de indeling bij elke start (`gamepad-indeling voor <game>`).

### Bedieningen

| Sleutel | Wat | Soort |
|---|---|---|
| `A`, `B`, `X`, `Y` | de vier knoppen (Xbox-namen; evdev `BTN_SOUTH`, `BTN_EAST`, `BTN_X`, `BTN_Y`) | knop |
| `LB`, `RB` | schouderknoppen | knop |
| `LT`, `RT` | triggers (analoog 0..1; ook `ABS_BRAKE`/`ABS_GAS` en digitaal `BTN_TL2`/`BTN_TR2`) | trigger |
| `Start`, `Back`, `Guide` | menuknoppen | knop |
| `LS`, `RS` | de sticks indrukken | knop |
| `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight` | d-pad (hat of `BTN_DPAD_*`) | knop |
| `LeftX`, `LeftY`, `RightX`, `RightY` | sticks, 0..1 met het midden op 0,5 (links/boven = 0), dode zone 0,08 | stick |

Hoofdletters maken niet uit (`leftx` mag).

### Signalen

| Waarde | Wat |
|---|---|
| `wheel` | stuur (0..1, midden 0,5). Alleen een stick of trigger, geen knop. |
| `gas`, `brake` | pedalen (0..1). Een knop geeft 0 of 1; een stick zijn uitslag (beide kanten). |
| `trigger`, `reload`, `grenade`, `aux1`, `aux2` | de knoppen van de virtuele lightgun van die speler (OR met de Move en de muis) |
| `start` | Start: toets 1 voor speler 1, 2 voor speler 2, ... (het virtuele toetsenbord, zoals `ravengunctl key`) |
| `coin` | munt: toets 5 (op de poort van die speler) |
| `btn1` .. `btn8` | extra kastknoppen; bestaan alleen in de IO-brug (`pN.btn1` .. `pN.btn8`) |
| `key:<naam>` | elke toets van het virtuele toetsenbord (namen uit `src/ravengun/output/key_table.hpp`, bv. `key:f2` = test) |
| `none` | niets |

Een trigger als knop is ingedrukt vanaf 0,5; een stick als knop vanaf de helft van zijn uitslag. Meerdere bedieningen op hetzelfde
signaal: knoppen OR, gas en rem het maximum, het stuur de grootste uitslag. Onbekende bedieningen of signalen worden overgeslagen
met een melding (logboek van de daemon, Properties).

Op de evdev-route (TeknoParrotUi leest het virtuele toetsenbord en de gun) werken de knoppen, `start`, `coin` en `key:`; `wheel`, `gas`,
`brake` en `btnN` bestaan alleen in de IO-brug (tp-shm, jvs, hid-board).

## In de IO-brug

`IoState` (`src/ravengun/iobridge/io_state.hpp`) heeft per speler `wheel`, `gas`, `brake` en `btn1..8`. De bordbestanden
(`packaging/tp-shm-profiles.json`, `jvs-boards.json`, `hid-boards.json`) kunnen ze gebruiken als `pN.wheel`, `pN.gas`, `pN.brake`
(assen) en `pN.btn1` .. `pN.btn8` (knoppen), op alle drie de bordsoorten.

- **Stuur zonder gamepad:** heeft een speler geen gamepad, of deelt zijn indeling het stuur niet in, dan is `pN.wheel` de aim-x
  (`pN.x`): een Move-speler stuurt door te richten.
- **Rustwaarde:** een as zonder levende bron krijgt de `idle` van het bord: `x`/`y` zonder geldige aim, `wheel` zonder gamepad-stuur
  en zonder aim, `gas`/`brake` zonder gamepad.
- Een speler met alleen een gamepad (geen Move of muis) krijgt geen eigen virtuele gun; zijn `wheel`/`gas`/`brake`/`btnN` gaan wel
  naar de IO-brug. Knoppen die op gun-knoppen vallen werken alleen voor een speler met een gun (speler 1 heeft er altijd een).

### Let's Go Safari: jvs plus tp-shm

Gemeten op de bench: Let's Go Safari (route jvs over de pipe `TeknoParrot_JVS`, `[Input] Layout = LGS`) stuurt alleen JVS `0x20`
(schakelaars), `0x21` (munten) en `0x32` (lampen), nooit `0x22` (analoog). Het stuur komt uit het gedeelde geheugen: de indeling `LGS`
is één byte op offset 12. Daarom schrijft de daemon op de route jvs met een `Layout` in `[Input]` ook die tp-shm-indeling (naast het
JVS-bord), en zet de directe start (`DirectInput::writes_tp_shm`, `play-halo.sh --shm-source`) het bestand van de daemon
(`/dev/shm/ravengun_JvsState`) als bron van de spiegel van de pipehelper. De as van `LGS` is `p1.wheel`.

## Code en tests

- `src/ravengun/sysinfo/gamepads.*`: opsomming met redenen.
- `src/ravengun/output/gamepad.*`: `PadReader` (evdev -> 0..1), `GamepadMap` en `apply_gamepad_map` (zonder apparaat).
- `src/ravengun/gamesettings/gamepad_settings.*`: `[Gamepad]` uit de lagen, met de herkomst per regel (daemon en app).
- `src/ravengund/gamepad_service.*`: thread, apparaten, toewijzingen; uitvoer via `OutputService::feed_live` (live bron 2, zonder aim)
  en `OutputService::set_pad_input` (IO-brug).
- IPC: `pads` (`{"pads":[{"id","name","event","vendor","product","player","connected","last_error","live":{"axes":{...},"buttons":[...]}|null}],
  "assignments":[{"player","pad","auto","connected","last_error"}],"game","mapping":[{"control","target"}],"warnings","devices":[...]}`)
  en `set_pad` (`player` 1..4 verplicht, `pad` = een id uit `pads` of `""`). `status` heeft het blok `gamepads`.
- Tests: `ravengun_gamepad_tests` (`tests/test_gamepad.cpp`: opsomming, `PadReader`, indeling en lagen, signalen op tp-shm/jvs/hid,
  live bronnen per toets, IPC, `config.json`, en de service met een FIFO als nep-apparaat), `ravengun_iobridge_tests` (jvs plus de
  tp-shm-indeling van Let's Go Safari) en de app-tests (`gamepadMapping`).
