# De koppeling tussen lader en kern — gemeten

*Onderzoek door RavenGun, ronde 6 (2026-10-11). Alle cijfers komen uit `objdump -p` en `strings -a`
op de bestanden zoals ze op de Deck staan; de commando's staan erbij zodat je ze kunt narekenen.*

Dit document beantwoordt één vraag: **wat moet een eigen kern kunnen, wil de lader van RavenGun
hem in de plaats van `TeknoParrot.dll` laden?** Dat is de vraag die bepaalt of de 21
TeknoParrot-type games ooit zonder TeknoParrot kunnen draaien.

---

## 1. De bestanden

| Bestand | Grootte | Wat het is |
|---|---|---|
| `runtime/…/TeknoParrot/TeknoParrot64.dll` | 56.261.288 | de gesloten kern, 64-bits |
| `runtime/…/TeknoParrot/TeknoParrot.dll` | 68.442.280 | de gesloten kern, 32-bits |
| `openparrot/current/OpenParrotx64/OpenParrot64.dll` | 1.930.752 | onze eigen kern, 64-bits |
| `openparrot/current/OpenParrotWin32/OpenParrot.dll` | 1.996.288 | onze eigen kern, 32-bits |
| `openparrot/current/OpenParrotx64/OpenParrotLoader64.exe` | 1.227.264 | onze eigen lader, 64-bits |
| `openparrot/current/OpenParrotWin32/OpenParrotLoader.exe` | 1.278.464 | onze eigen lader, 32-bits |

Onze eigen bestanden komen uit de openbare OpenParrot-bron
(`github.com/teknogods/OpenParrot` @ `fc8ba632e1f67c5c340ff88f30efca743e8d38f5`, GPL-3.0),
gebouwd met mingw-w64; zie `openparrot/current/BUILDINFO.txt`.

---

## 2. De exportlijst van de kernen

```
$ objdump -p <kern> | sed -n '/\[Ordinal\/Name Pointer\] Table/,/^$/p'
```

| Kern | exports | aantal |
|---|---|---|
| `TeknoParrot64.dll` (64-bits) | `InitKonami`, `InitializeASI`, `TP_InitializeApmTest` | 3 |
| `TeknoParrot.dll` (32-bits) | `InitKonami`, `InitLinux`, `InitializeASI`, `_TP_InitializeApmTest@4` | 4 |
| `OpenParrot64.dll` (64-bits) | `InitializeASI`, `PrepareSafeInit` | 2 |
| `OpenParrot.dll` (32-bits) | `InitLinux`, `InitializeASI`, `PrepareSafeInit@4`, `_PrepareSafeInit@4` | 4 |

**Gemeenschappelijk aan alle vier: alleen `InitializeASI`.** Dat is de hele koppeling.

> **Let op bij het lezen van `objdump -p`.** De kopregel van de naam-tabel is letterlijk
> `Ordinal   Hint Name`. Wie met `sed -n '/Name Pointer/,/^$/p'` of `grep Name` leest, pakt dat
> kolomlabel mee en denkt dat er een export `Name` is. **Die bestaat niet.** Filter de kopregel weg
> (bijvoorbeeld met `tail -n +2`) voordat je de namen leest. Deze fout stond eerder in dit document
> en is hierbij hersteld.

---

## 3. Wat de lader werkelijk opzoekt

```
$ strings -a OpenParrotLoader64.exe | grep -x <naam>
```

| Naam | in de lader? |
|---|---|
| `InitializeASI` | **ja** |
| `TP_InitializeApmTest` | ja, maar alleen in de testmodus |
| `InitKonami` | nee |
| `PrepareSafeInit` | nee |

De lader laadt de kern met `LoadLibraryW` en zoekt de ingang op met `GetProcAddress`.

`TP_InitializeApmTest` staat wel in de tekenreeksen van de lader, maar de opzoeking zit in
`ApmTestLoader.cpp` en die wordt alleen bereikt via `main.cpp`:

```c
if (argc > 1 && _wcsicmp(argv[1], L"--apm-test") == 0) return RunApmTestLoader(argc, argv);
```

**Een eigen kern hoeft die naam dus niet te leveren voor een gewone start.** Een stub kost niets,
maar is niet vereist. (Deze nuance is later toegevoegd; het eerdere besluit in dit document dat de
naam "dus moet" was te streng.)

---

## 4. Waarom de kernen zo verschillen in grootte

TeknoParrot's kern is **56 MB**, de onze **1,9 MB** — een factor 29. Dat verschil is geen code maar
**kennis**: de per-game hooks, profielen, patches en bordindelingen zitten in die 56 MB.

Dat blijkt ook direct: onze eigen kern kent **geen enkele** van de games uit deze repo.

```
$ strings -a OpenParrot64.dll | grep -ci <naam>     # WartranTroopers, RawThrillsGUN,
                                                    # Gashaaaan, NERF, Rampage, Rabbids,
                                                    # Lets Go, GaiaAttack, HauntedMuseum,
                                                    # MusicGunGun, LuigisMansion, TimeCrisis
```
→ **nul treffers voor alle twaalf.** Wat onze kern wél noemt is Type X2 / NESiCA-spul
(`Aime_acceptConfirm`, `AMAuthd for WMMT6`, …). De openbare OpenParrot-bron is de Type X2-kern.

---

## 5. De conclusie

1. **De koppeling tussen lader en kern is klein.** In essentie **alleen `InitializeASI`** — dat is de
   enige naam die alle vier de kernen exporteren en die de lader opzoekt. `TP_InitializeApmTest`
   zoekt de lader alleen op in de testmodus (`--apm-test`), dus die is voor een gewone start niet
   nodig.
2. **De lader is niet het probleem.** Voor 24 van de 35 games is de lader al van ons
   (`openparrot/current/OpenParrotLoader*.exe`).
3. **De kern is het probleem, en het probleem is kennis, niet interface.** Een eigen kern is
   architectonisch mogelijk; wat ontbreekt zijn de `WartranTroopers`- en `RawThrillsGUN`-hooks.
4. **Het begin van die kennis staat er al.** `patches/<Game>/patches.ini` beschrijft per game de
   bytepatches (met `Find`/`Replace`, de originele bytes en een genoemde spelversie). Alleen de
   motor die ze toepast ontbreekt nog.
5. **Wat een eigen kern minimaal moet doen** (uit `LOSKOPPELEN.md`, deel 1 en 2): het gedeelde
   geheugen `TeknoParrot_JvsState` vullen volgens de bordindeling van het spel, de bytepatches
   toepassen, en `CreateFileA` onderscheppen voor de schijfletter-herleiding.

**Eerlijke keerzijde:** de openbare OpenParrot-bron dekt de profielen van deze games niet (nul
treffers, zie §4). Die kennis moet opnieuw opgebouwd worden, en dat is het grootste stuk werk dat
nog openstaat. Voor de vier `OpenParrot`-games (GaiaAttack4, HauntedMuseum, HauntedMuseumII,
MusicGunGun2) is dat werk er niet: die draaien al volledig op eigen lader én eigen kern.

---

## Hoe je dit narekent

```bash
RT=~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64
OWN=~/.local/share/ravengun/openparrot/current
# Let op: tail -n +2 slaat de kopregel "Ordinal   Hint Name" over -- anders lees je een export
# die niet bestaat.
objdump -p "$RT/TeknoParrot/TeknoParrot64.dll"   | sed -n '/Name Pointer/,/^$/p' | tail -n +2
objdump -p "$OWN/OpenParrotx64/OpenParrot64.dll" | sed -n '/Name Pointer/,/^$/p' | tail -n +2
strings -a "$OWN/OpenParrotx64/OpenParrotLoader64.exe" | grep -x -e InitializeASI -e TP_InitializeApmTest
strings -a "$OWN/OpenParrotx64/OpenParrot64.dll" | grep -ci WartranTroopers
```

En de per-game uitkomst van de lader- en kernkeuze: `python3 ~/verify_all.py` (zie
`docs/loskoppelen-per-game.md` voor de tabel).
