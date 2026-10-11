# De patchmotor

*`scripts/patchfile.py` + `scripts/addrmap.py`, ronde 6 (2026-10-11). Dit is het stuk dat de
patches in `patches/<Game>/patches.ini` ook echt toepast. Zie `docs/community-model.md` voor het
waarom en `docs/dropins.md` voor de mapindeling.*

## Wat hij doet

| Soort | Doel | Wat de motor doet |
|---|---|---|
| `Kind = file` | lader, kern **of de game zelf** | zoeken en vervangen van bytes, met `Find`/`Replace`/`Count` |
| `Kind = memory` | **de hoofdmodule** (de exe van de game) | het adres omrekenen naar een bestandsplaats en daar de bytes schrijven |

Dat tweede is het nieuwe: **een geheugenpatch op de hoofd-exe is in werkelijkheid een
bestandspatch.** `Module + Offset` is via de sectie- of segmenttabel terug te rekenen naar een plaats
in het bestand, dus er is geen injectie en geen procesgeheugen nodig.

## Het bewijs dat dit werkt

Twee proeven op echte binaries, beide op de Deck te herhalen met `re/test_patchfile2.sh`:

**1. Een PE waar adres en bestandsplaats níet samenvallen.** Rampage `Game.exe`, patch op
`Offset = 0x1000`:

```
gecontroleerd  proef op de PE-tekstselectie   Game.exe+0x1000   op Game.exe+0x1000 (bestand 0x400, .text)
bytes op bestandsplaats 0x400 voor: 48895c24
bytes op bestandsplaats 0x400 na:   deadbeef
```

Het adres is `0x1000`, de bestandsplaats is `0x400` — de motor schrijft op de goede plek.

**2. De bekende GOT-invoer van Jurassic Park.** Uit eerder onderzoek hoort op het adres `0x8add63c`
de waarde `0xb7500120` te staan (`glGetIntegerv`, `base(libGL) 0xb7500000 + 0x120`). De motor vindt
hem:

```
toegepast  proef op de GOT-invoer  game+0xa9563c  op game+0xa9563c (bestand 0xa9563c, LOAD1)  [200150b7 -> 00000000]
```

Daarmee is de hele aanpak in één keer bevestigd: de adresomrekening klopt, de `Find`-controle klopt,
en de eerdere GOT-analyse wordt er onafhankelijk door bevestigd.

En de omrekening zelf is gekruist met `objdump`: **alle 25 secties over vier PE-bestanden** komen
exact overeen (zie `re/verify_addrmap.sh`).

## De woordenschat van `Kind = memory`

| `Action` | Wat er op de plaats komt | Eis |
|---|---|---|
| `nop` | `Size` keer `0x90` | `Size` verplicht |
| `jmp-returns-true` | `mov eax,1 ; ret` en de rest opgevuld met `0x90` | `Size >= 6`; **de plaats moet het begin van een functie zijn** |
| `replace-bytes` | de bytes uit `Replace` | `Replace` verplicht; `Size` mag, en moet dan kloppen |
| `write-bytes` | hetzelfde als `replace-bytes` | idem |

**Over `jmp-returns-true`, eerlijk:** TeknoParrot's kern springt naar een hulpje dat 1 teruggeeft.
Stil in het bestand kan dat niet — we weten niet waar dat hulpje in het geheugen komt. Daarom zet de
motor op de plaats zelf `mov eax,1 ; ret`. Dat geeft hetzelfde waarneembare resultaat **als de
plaats het begin van een functie is**. Is het een aanroepplek, dan is het fout. Daarom vraagt de
motor `Size` en meldt hij in het verslag wat hij heeft gedaan; zet er altijd een `Find` bij zodat de
spelversie gecontroleerd wordt.

## Wat hij weigert, en waarom

De motor weigert liever dan dat hij iets onduidelijks doet. Alle vier de gevallen zijn getest:

| Geval | Melding |
|---|---|
| een patch in een DLL die niet de hoofdmodule is | "Module = UnityPlayer.dll is niet de hoofdmodule van deze game (Game.exe). Een patch in een DLL die pas later geladen wordt kan niet in het bestand; die vraagt de geheugen-motor" |
| `Find` klopt niet | "de bytes op Game.exe+0x1000 zijn deadbeef, Find zegt 11223344 (verkeerde spelversie?)" |
| het doel ligt op de NAS | "het doel … ligt op de NAS; daar wordt nooit een bestand gepatcht" |
| `AppliedBy = core` | "de kern past dit zelf toe" — over te nemen met `--take-over-core` |

Die laatste is belangrijk: de bestaande `patches.ini` van GaiaAttack4, HauntedMuseum,
HauntedMuseumII en MusicGunGun2 zeggen `AppliedBy = core`, wat betekent "de OpenParrot-kern doet dit
al". Zolang wij die kern niet vervangen mag de motor ze niet nog eens toepassen.

## Gebruik

```bash
# controleren (schrijft niets) -- dit is de standaard
python3 scripts/patchfile.py --game JurassicPark --game-exe /pad/naar/game

# ook echt toepassen
python3 scripts/patchfile.py --game JurassicPark --game-exe /pad/naar/game --apply

# de patches van de kern overnemen (nodig zodra onze eigen kern die taak krijgt)
python3 scripts/patchfile.py --game GaiaAttack4 --game-exe ... --take-over-core --apply
```

Als module:

```python
import patchfile
verslag = patchfile.apply_game("JurassicPark", exe, loader=..., core=..., materialize=True)
print(verslag.summary())
for rol, pad in verslag.written.items():
    print(rol, pad)
```

De lader en de kern worden als **kopie** weggeschreven (`<naam>.ravengun-patched`); de exe van de
game wordt in het bestand zelf aangepast, met een achterblijvend `.ravengun-orig` als reserve. Dat
moet een **lokale** kopie zijn — de motor weigert een pad op de NAS, ook met `--force`.

## Wat er nog niet is

1. **De echte geheugen-motor** voor wat niet in een bestand past: een patch in een DLL die pas later
   geladen wordt, een **gepakte** exe (Pigs Might Fly, Luigi's Mansion), of iets dat pas tijdens het
   draaien aangepast moet worden. Dat vraagt één DLL/`.so` die in het proces geladen wordt. Zie
   `docs/community-model.md`, punt 3.
2. **De koppeling met de launcher.** `scripts/launch-direct.py` heeft nog zijn eigen
   `plan_file_patches()`, dat alleen `Kind = file` op de lader en de kern doet. Die moet
   `patchfile.apply_game()` gaan aanroepen. Dat bestand is van een andere agent; dit is de
   overdracht.
3. **De spelversie-controle voor memory-patches.** `Find` werkt, maar de bestaande `patches.ini` van
   de vier Type X2-games hebben geen `Find`. Zonder `Find` kan de motor niet zien of je de juiste
   versie hebt — hij meldt dat dan ook ("geen Find, dus niet te controleren"). Wie een memory-patch
   schrijft, zet er een `Find` bij.

## Eén blokkade die je moet weten

`scripts/gamesettings.py` regel 50 laat alleen deze categorieën toe:

```python
PATCH_CATEGORIES = ("graphics", "input", "timing", "audio", "paths", "crash", "network", "loader")
```

Een patch met `Category = dongle` wordt **afgewezen**, met de melding dat dongle-, keychip-, regio-,
integriteits- en abonnementscontroles "geen onderdeel van het formaat" zijn. Dat is een oude
projectafspraak die haaks staat op de huidige richting: de dongle is nu juist een van de drie
werksoorten die per game in een map moeten kunnen. **Zolang die lijst niet wordt uitgebreid, kan een
bijdrager geen dongle-patch in het formaat kwijt** — en met `Enabled = true` weigert de launcher zelfs
de hele start. Dit is gemeld bij de eigenaar van `gamesettings.py`.
