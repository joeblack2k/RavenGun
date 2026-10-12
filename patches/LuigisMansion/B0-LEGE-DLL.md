# B0: verdraagt Luigi's Mansion een in-process aanhechting?

RavenGun, 2026-10-12. Uitgevoerd op de Deck, alleen-lezen op de NAS, geen commit.
Script: `b0-run.sh`. DLL's: `b0-bouw.sh` -> `~/.local/share/ravengun/b0/{leeg,leeg0}.dll`.
Ruwe uitvoer: `/tmp/b0-luigi-eind/` (de geldige reeks) en `/tmp/b0-luigi-hermeting/` (de eerste,
deels ongeldige).

## De vraag

Het spel sterft met exitcode 32 op ONZE kern en op een LEGE DLL, terwijl de GESLOTEN kern leeft.
Is dat een aanhechtingsprobleem, of is 32 de eigen reactie van het spel op zijn omgeving -- en
levert de gesloten kern dus iets dat het spel nodig heeft om te blijven leven?

## De runs

| # | kern | wat het is |
|---|---|---|
| 0 | geen | `wine VACUUM.exe`, geen lader, geen enkele injectie |
| a | `TeknoParrot64.dll` | de GESLOTEN kern -- de controle die MOET leven |
| b | `OpenParrot64.dll` | onze kern (patches 0001-0005) |
| c | `leeg.dll` | DllMain die TRUE teruggeeft, geen haak, geen thread, geen import, geen CRT |
| d | `leeg0.dll` | geen entry point, geen imports: alleen de module wordt gemapt |

## Vooraf opgeschreven uitkomsten

* (0) geeft 32 -> 32 is de EIGEN reactie van het spel; "onze kern -> 32" en "lege DLL -> 32"
  zijn dan GEEN bewijs over aanhechting. De vraag wordt: wat levert de gesloten kern?
* (0) leeft -> de aanhechting is wel de variabele en (c) is beslissend.
* (a) sterft -> de OPSTELLING is fout; alle andere runs zijn ongeldig. STOP.
* (c) LEEFT -> de aanhechting is het probleem niet; onze kern doet iets dat het spel merkt.
* (c) STERFT terwijl (a) leeft -> elke in-process aanhechting struikelt over iets. STOP:
  eigenaarsbeslissing, geen technisch obstakel. Niet omheen werken.

---

## 1. De opstelling: wat er fout was, en waarom dat een les is

De EERSTE poging stierf ook met de GESLOTEN kern: exitcode **5** (`ERROR_ACCESS_DENIED`) na
11,5 s, met `winedbg.exe` in de modulelijst en `d3d11.dll`/`dxgidebug.dll` geladen. Dat is per de
vooraf opgeschreven regel een fout in de OPSTELLING, niet in de kern. Vier verschillen met de
bewaarde werkende run (`/tmp/rg-start3-LuigisMansion.log`, 11:13):

| | werkende run (11:13) | mijn eerste poging |
|---|---|---|
| WINEPREFIX | `~/.local/share/TeknoParrotUI/prefixes/shared/wine` | `~/.local/share/ravengun/prefixes/shared/wine` |
| werkmap van de LADER | `.../runtime/TeknoParrotUi-2.0.0.130-linux-x64` | `.../views/LuigisMansion/exe/x64` |
| hulpketen | `pipehelper.exe TeknoParrot_JVS ... TeknoParrot_JvsState 64 Z:\dev\shm\TeknoParrot_JvsState`, `shm mirror active`, `TP_LAUNCH_SESSION_ID` | geen van beide |
| display | `DISPLAY=:0` | geen DISPLAY (kale ssh-sessie) |

De prefix is sindsdien verhuisd (`resolve_prefix`: "onze eigen gedeelde prefix onder
`~/.local/share/ravengun/prefixes/`, voorheen onder `TeknoParrotUI/prefixes`"), dus het verschil
is nu kleiner dan het lijkt -- maar de andere drie zijn echt.

**De les, en de oplossing die het probleem helemaal wegnam: doe de opstelling niet zelf na.**
Laat de bestaande route hem zetten en verwissel alleen de kern:

```bash
python3 scripts/launch-direct.py LuigisMansion --run
```

`launch-direct.py` zet prefix, werkmap van de lader, hulpketen (`rg-jvsbridge` +
`TP_LAUNCH_SESSION_ID`) en omgeving goed. De kern is niets anders dan `argv[1]` van de lader
(en de lader doet daar `+ ".dll"` achter), dus de enige variabele in deze bisectie is de INHOUD
van die ene DLL. Dat is precies een variabele per run.

## 2. Een fout in de eerste hermeting, en de reparatie

De eerste hermeting (`/tmp/b0-luigi-eind`) had een fout in MIJN script, niet in de meting:

```bash
herstel() { [[ $TERUG -eq 1 ]] && return 0; TERUG=1; ... }   # FOUT
```

Die vlag wordt maar een keer waar, dus vanaf de tweede verwisseling zette `herstel()` de kern
nooit meer terug. Gevolg: run (d) en de twee debug-runs draaiden met de DLL van de vorige run, en
de runtime bleef met ONZE kern achter in plaats van de gesloten kern.

**Reparaties:**

1. `herstel()` is nu idempotent op het BESTAAN van de backup in plaats van op een vlag.
2. Elke run controleert ACHTERAF de sha256 die `launch-direct.py` ZELF in zijn logboek schrijft
   (`kern (runtime): ... sha256 ...`) tegen de DLL die wij bedoelden. Klopt dat niet, dan schrijft
   het verslag `GELDIG NEE`. Dat is de controle die deze fout zou hebben gevangen.
3. De runtime is met de hand hersteld uit `TeknoParrot64.dll.b0-bewaard-403724`; de sha256 is
   daarna gecontroleerd: `f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`,
   gelijk aan het origineel. Er staat geen backupbestand meer.
4. De bestandsteller zocht `Data/` met SCHUINE streepjes terwijl de Wine-trace Windows-paden met
   BACKSLASHES schrijft (`..\..\DataGlobal\EN\Data\...`). Hij meldde dus altijd 0. Nu telt hij
   `CreateFileW L"..."`-paden en matcht op `Data(Global)?[\\/]`. **IJkpunt op de ruwe trace van
   (a): 3736 CreateFileW-regels, 1089 unieke paden, 393 eigen Data/DataGlobal-paden.**

## 3. De uitkomsten -- met de kern die launch-direct werkelijk gebruikte

| run | kern (sha256, uit het log van launch-direct) | exitcode | CreateFileW | uniek | **eigen Data/DataGlobal** |
|---|---|---|---|---|---|
| a-origineel | `f5c7532d` = gesloten kern | **124** = liep nog bij de limiet | 3736 | 1089 | **393** |
| b-onze-kern | `fe2ecf8c` = `OpenParrot64.dll` | 32 | 77 | 16 | **0** |
| c-lege-dll | `7054e9d8` = `leeg.dll` | 32 | 66 | 14 | **0** |
| d-lege-dll-0 | `1467bbee` = `leeg0.dll` | 32 | 66 | 14 | **0** |
| dbg-origineel | `1467bbee` -- **ONGELDIJK**, moest `f5c7532d` zijn | 32 | 0 | 0 | 0 |
| dbg-onze | `fe2ecf8c` | 32 | 0 | 0 | 0 |

(a) is de enige run die leeft, en hij laadt zijn eigen data:
`../../DataGlobal/EN/Data/Model/chara/EN_bo_KingBoo/motion/EN_bo_KingBoo_laugh_untethered.mot`
en honderden van zulke paden.

(b), (c) en (d) openen NUL eigen databestanden. Wat ze wel openen zijn 66-77 systeembestanden:
`C:\windows\system32\stdole2.tlb`, `uiautomationcore.dll`, `teknoparrot.ini`. Dat is **OLE- en
GUI-initialisatie**, geen speldata. Dat komt overeen met wat de laderbouwer meldde: het spel
initialiseert (OLE, GUI, I/O-threads), sluit zijn I/O netjes af en gaat er met 32 uit.

`(c)` en `(d)` doen dat met een DLL waarin GEEN ENKELE instructie van ons draait -- (d) is alleen
maar gemapt.

## 4. Wat dit wel en niet betekent

**Wel:** met dezelfde route, dezelfde prefix, dezelfde hulpketen en dezelfde display is de
GESLOTEN kern de enige die het spel laat leven. Onze kern en een lege DLL geven allebei exitcode
32, en geen van beide raakt een eigen databestand aan.

**Niet:** dit is nog geen bewijs dat 32 de eigen reactie van het spel is. Daarvoor ontbreekt run
(0), waarin het spel ZONDER ENIGE kern start. (c) en (d) zijn een sterke aanwijzing in die
richting -- er is geen code van ons actief -- maar er is wel een module bijgekomen, en dat is
precies het verschil dat (0) wegneemt.

**Dus:** de conclusie "(c) STERFT, dus elke in-process aanhechting struikelt over iets" wordt hier
NIET getrokken. Zodra (0) meet, is de vraag of het aan de aanhechting ligt of aan wat de gesloten
kern levert.

## 5. Wat nog open is

1. **Run (0): het spel zonder enige kern.** `bash b0-run.sh 0` (ongeveer 45 s). Dit is de
   beslissende meting. Geeft (0) 32, dan is de volgende stap NIET de bisectieladder B1-B3 maar
   het vergelijken van wat de gesloten kern doet: welke bestanden en mappings opent hij, welke
   modules laadt hij in het spel, zet hij de JVS-pipe of het gedeelde geheugen op, en maakt hij
   threads aan die de game nodig heeft. Gereedschap daarvoor is de WINEDEBUG-vergelijking.
2. **Het debug-paar opnieuw**, nu met de kerncontrole: `bash b0-run.sh debug`. De huidige
   `dbg-origineel` is ongeldig (hij draaide `leeg0`).
3. **B1-B3** (onze kern met `jvsInit` dat meteen terugkeert, dan alleen de `CreateFileA/W`-haken,
   dan zonder patch 0005) -- alleen zinvol als (0) LEEFT.

## 6. Uitvoeren

```bash
bash b0-bouw.sh                 # de twee DLL's, in de MinGW-container
bash b0-run.sh 0                # EERST: het spel zonder kern
bash b0-run.sh a                # de controle; moet 124 geven (leeft)
bash b0-run.sh b c d            # de bisectie
bash b0-run.sh debug            # de WINEDEBUG-vergelijking
```

Het script weigert te starten als er een game, lader, wineserver of gamescope draait, start zelf
een headless gamescope (het scherm van de eigenaar blijft vrij), zet de kern-DLL altijd terug en
controleert dat, en veegt na elke run resten weg. Een orphan vergiftigt de volgende meting.

