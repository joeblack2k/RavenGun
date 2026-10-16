# De patchmotor

*`tools/dev/patchfile.py` + `tools/dev/addrmap.py`, ronde 6 (2026-10-11). Dit is het stuk dat de
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
python3 tools/dev/patchfile.py --game JurassicPark --game-exe /pad/naar/game

# ook echt toepassen
python3 tools/dev/patchfile.py --game JurassicPark --game-exe /pad/naar/game --apply

# de patches van de kern overnemen (nodig zodra onze eigen kern die taak krijgt)
python3 tools/dev/patchfile.py --game GaiaAttack4 --game-exe ... --take-over-core --apply
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
3. **De spelversie-controle voor memory-patches.** `Find` werkt en staat sinds ronde 33 bij **acht van
   de twaalf** memory-patches van de vier Type X2-games. Die bytes zijn op de echte exe's gemeten
   (module-offset naar bestandsplaats via `addrmap`, sectie `.text`) en het zijn dezelfde bytes die de
   motor zelf print bij `--take-over-core`; de motor meldt ze nu dan ook als `gecontroleerd` in plaats
   van "geen Find, dus niet te controleren". De `Find` is precies zo lang als de patch schrijft, dus
   `Size` bepaalt de lengte. Bij de vier zonder `Find` zou hij niets zeggen: de 2101J-set van
   HauntedMuseumII hoort bij de **JP 1.01**-build terwijl onze dump de **USA 1.00**-build is (de CRC
   die de kern zelf over de eerste `0x400` bytes rekent is `0x5a7fa241` -> `GameID::HauntedMuseum2100`),
   en de crash-NOP van `0x1391` staat in onze dump al op `0x90`. Wie een nieuwe memory-patch schrijft,
   zet er een `Find` bij.

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

---

# De runtime-route (deel 2 van de motor)

*Bijgewerkt ronde 16. De generator staat en is getest; de koppeling met de launcher is de
eerstvolgende stap en staat onderaan als overdracht.*

## Wat de bestandsroute niet kan

`apply_game_obj()` dekt alles wat in het **bestand** van een module staat. Twee gevallen vallen
daarbuiten, en die horen bij de **geheugen-motor** (`memory/src/patchmem.c`, `docs/memory-engine.md`):

1. **Een DLL die pas later geladen wordt.** Die staat niet in het bestand van de exe, dus de
   bestandsroute kan er niet bij. De motor onderschept `dlopen` en heeft een achtergrondthread.
2. **Een gepakte exe.** De bytes in het bestand zijn dan niet de bytes die het spel uitvoert. De
   bestandsroute zou de verkeerde plek raken; de motor patcht het uitgepakte geheugen.

## De generator: `pf.runtime_list()`

Die zet `patches.ini` om in de lijst die de motor leest:

```
# module|offset|verwacht|nieuw
UnityPlayer.dll|0x1234||deadbeef
spul.dll|0x40|48895c2408|9090909090
```

De regels:

| Wat | Uitkomst |
|---|---|
| `Kind = memory` met `Module` **niet** de hoofdmodule | **in de lijst** |
| `Kind = memory` met `Module` **wel** de hoofdmodule | overgeslagen — dat doet de bestandsroute al |
| `Kind = file` | overgeslagen — hoort niet in de runtime-lijst |
| `AppliedBy = core` | overgeslagen, tenzij `take_over_core=True` |
| `Module` ontbreekt | geweigerd, met de reden |
| niet ingeschakeld | overgeslagen |

`Find` wordt `verwacht` (de motor controleert het en weigert als het niet klopt); zonder `Find` is
dat veld leeg en meldt het verslag "geen Find, dus niet te controleren". `schrijf_runtime_list()`
schrijft het bestand **alleen als er regels zijn** — een lege lijst maakt geen bestand, zodat een
drop-in niet uit zichzelf iets doet.

Negen tests in `tests/test_runtime_lijst.py`.

## Wat er nog moet: de koppeling in de launcher

Dit is de overdracht, met de twee dingen die je niet moet vergeten:

1. **De lijst en de bibliotheek in de view.** `run()` schrijft `patchmem.txt` in de view (naast de
   exe), kopieert `ravengun-patchmem{,32}.{so,dll}` uit `~/.local/share/ravengun/memory/` en zet
   `RAVENGUN_PATCHMEM_FILE`. Het schrijven hoort in `run()`, niet in `build_plan()` — `--plan` mag
   geen bijwerkingen hebben.
2. **Het pad dat het SPELPROCES begrijpt.** Onder Wine is `/home/deck/...` onbruikbaar; het moet
   `Z:\home\deck\...` zijn. De launcher kent de view, dus die rekent het om.
3. **Het laden.** Voor een **Linux-ELF onder Wine** (de acht ElfLdr2-games) gaat de bibliotheek mee
   als `LD_AUDIT`, precies zoals de andere drop-ins sinds ronde 13 — `LD_PRELOAD` is daar fataal.
   Voor een **echte native start** mag `LD_PRELOAD` wel.
4. **Onder Windows is er een keuze van de maker van de patch nodig.** `WINEDLLOVERRIDES` werkt
   alleen voor een module die de game ook werkelijk **importeert**, en `AppInit_DLLs` werkt onder
   Proton 11.0-2 **niet** (gemeten, `docs/memory-engine.md` §8). De uitweg is de DLL onder de naam
   van een DLL zetten die de game wél importeert — een proxy-DLL, zoals `ravengun-hasp.dll` en
   `matrix64.dll` dat al doen. **De launcher kan die naam niet verzinnen**: de maker van de patch zet
   hem in `patches/<Game>/dropin/dropin.ini`. De launcher hoort dat **duidelijk te melden** als er
   runtime-patches zijn voor een Windows-game en er geen proxy-sectie staat — niet te doen alsof het
   werkt.

Zolang punt 4 niet geregeld is, is de runtime-route bruikbaar voor de **Linux-games** en voor
**proeven** onder Wine met een expliciete `WINEDLLOVERRIDES`, maar niet automatisch voor de
Windows-games. Dat is de eerlijke stand.
