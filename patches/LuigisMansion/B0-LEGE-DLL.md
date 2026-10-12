# B0: verdraagt Luigi's Mansion een in-process aanhechting?

RavenGun, 2026-10-12. Uitgevoerd op de Deck, alleen-lezen op de NAS, geen commit.
Script: `b0-run.sh`. DLL's: `b0-bouw.sh` -> `~/.local/share/ravengun/b0/{leeg,leeg0}.dll`.
Ruwe uitvoer: `/tmp/b0-luigi-nul/` (run 0 + het debug-paar), `/tmp/b0-luigi-eind/` (a t/m d),
`/tmp/b0-luigi-hermeting/` (de eerste, deels ongeldige reeks).

## Het antwoord in een regel

**Het spel geeft exitcode 32 OOK ZONDER ENIGE KERN.** 32 is dus de eigen reactie van het spel op
zijn omgeving. "Onze kern -> 32" en "een lege DLL -> 32" zijn daarom GEEN bewijs over aanhechting.
De enige vraag die overblijft: wat levert de GESLOTEN kern dat het spel laat leven?

## De runs

| # | kern | wat het is |
|---|---|---|
| 0 | geen | `wine VACUUM.exe`, geen lader, geen enkele injectie |
| a | `TeknoParrot64.dll` | de GESLOTEN kern -- de controle die MOET leven |
| b | `OpenParrot64.dll` | onze kern (patches 0001-0005) |
| c | `leeg.dll` | DllMain die TRUE teruggeeft, geen haak, geen thread, geen import, geen CRT |
| d | `leeg0.dll` | geen entry point, geen imports: alleen de module wordt gemapt |

## Vooraf opgeschreven uitkomsten

* (0) geeft 32 -> 32 is de EIGEN reactie van het spel; (b) en (c) zijn dan geen bewijs over
  aanhechting, en de vraag wordt: wat levert de gesloten kern? **<- dit is de uitkomst geworden.**
* (0) leeft -> de aanhechting is wel de variabele en (c) is beslissend.
* (a) sterft -> de OPSTELLING is fout; alle andere runs zijn ongeldig. STOP.
* (c) LEEFT -> de aanhechting is het probleem niet; onze kern doet iets dat het spel merkt.
* (c) STERFT terwijl (a) leeft -> elke in-process aanhechting struikelt over iets. STOP:
  eigenaarsbeslissing, geen technisch obstakel. Niet omheen werken.

---

## 1. De uitkomsten -- met de kern die launch-direct werkelijk gebruikte

| run | kern (sha256, uit het log van launch-direct) | exitcode | CreateFileW | uniek | eigen `Data/DataGlobal` |
|---|---|---|---|---|---|
| 0-zonder-kern | geen kern | **32** | 1034 | 51 | **0** |
| a-origineel | `f5c7532d` = gesloten kern | **124** = liep nog bij de limiet | 3736 | 1089 | **393** |
| b-onze-kern | `fe2ecf8c` = `OpenParrot64.dll` | 32 | 77 | 16 | 0 |
| c-lege-dll | `7054e9d8` = `leeg.dll` | 32 | 66 | 14 | 0 |
| d-lege-dll-0 | `1467bbee` = `leeg0.dll` | 32 | 66 | 14 | 0 |
| dbg-origineel | `f5c7532d` = gesloten kern | **124** | -- | -- | -- |
| dbg-onze | `fe2ecf8c` = onze kern | 32 | -- | -- | -- |

(a) en `dbg-origineel` zijn de enige twee die leven. Run (0) sterft na **1,5 s** met 1 thread;
onze kern rekt dat op tot ongeveer 25 s; de gesloten kern komt voorbij de 45 s. Elke kern die je
in het proces zet verlengt het leven dus, maar alleen de gesloten kern komt erdoor.

(a) laadt zijn eigen data: `../../DataGlobal/EN/Data/Model/chara/EN_bo_KingBoo/motion/...` en
honderden van zulke paden. (0), (b), (c) en (d) openen NUL eigen databestanden; hun 66-1034
`CreateFileW`-regels zijn systeembestanden (`stdole2.tlb`, `uiautomationcore.dll`,
`teknoparrot.ini`), dus OLE- en GUI-initialisatie.

## 2. Waarom de aanhechting niet de verklaring is

Met (0) op 32 is de vraag verschoven. Wat het debug-paar (nu beide geldig) verder laat zien:

**CORRECTIE (2026-10-12, narekening door de cooerdinator).** De eerste versie van deze tabel was
fout: die mengde POGINGEN en GESLAAGDE loads. Bewijs uit de trace zelf, in de ONZE-run:

```
0108:trace:module:LdrGetDllHandleEx L"d3d8.dll" -> 0000000000000000 (load path (null))
0100:trace:module:LdrGetDllHandleEx L"mscoree"  -> 0000000000000000 (load path (null))
```

Dat zijn MISLUKTE pogingen (en `mscoree` zelfs rond `LdrShutdownProcess`, tijdens het afsluiten).
Onze kern heeft nooit .NET geladen en nooit d3d8. De eerdere bewering "onze kern laadt `d3d8.dll`
en `mscoree.dll` die de gesloten kern niet laadt" was dus geen verschil in geladen modules maar een
verschil in mislukte pogingen.

Schone hertelling met ALLEEN geslaagde loads (`MODULE_InitDLL ... PROCESS_ATTACH`, en
`LdrGetDllHandleEx ... -> <niet-nul>`), op dezelfde twee logs:

| | ONZE kern | GESLOTEN kern |
|---|---|---|
| `LdrGetDllHandleEx`-regels (totaal / mislukt / geslaagd) | 326 / 27 / 137 | 441 / 27 / 198 |
| unieke geslaagde namen | 14 | 18 |
| unieke `MODULE_InitDLL PROCESS_ATTACH`-namen | 54 | **65** |
| **verschil, ONZE min GESLOTEN** | **leeg** | -- |
| **verschil, GESLOTEN min ONZE** | -- | `d3d11.dll`, `dsound.dll`, `mmdevapi.dll`, `msdmo.dll`, `msvfw32.dll`, `qasf.dll`, `quartz.dll`, `winegstreamer.dll`, `wmvcore.dll`, `winealsa.drv`, `winepulse.drv` |

**ONZE kern voegt dus NIETS toe.** De gesloten kern initialiseert de media- en audio-stack:
DirectShow/WMV (`quartz`, `qasf`, `wmvcore`, `winegstreamer`, `msvfw32`), audio (`dsound`,
`mmdevapi`, `msdmo`, `winealsa.drv`, `winepulse.drv`) en `d3d11`.

**En dat is een GEVOLG, geen oorzaak.** Die modules horen bij het film- en geluidspad, en alleen de
gesloten kern komt zo ver dat het spel films gaat spelen en geluid opzet. Lees dit dus NIET als "de
kern laadt iets wat wij niet laden, dus dat moeten wij ook laden" -- dan zoek je de verkeerde kant
op. De oorzaak staat in Â§9b: onder onze kern wordt de entry-point-trampoline nooit teruggezet en
begint de pakker nooit.

## 3. De hypothese "de gesloten kern heeft een Luigi-tak" is in de binary niet terug te vinden

Gezocht in beide kernen, op naam EN op de drie CRC-vormen als little-endian dword:

| | `TeknoParrot64.dll` (56.261.288 B, 91.172 leesbare strings) | `OpenParrot64.dll` |
|---|---|---|
| `Luigi` | 0 | 1 |
| `LMAIO`, `SegaNu`, `BLOCKING`, `shared-` | 0 | 0 |
| dword `0xd3dc9603` | **0** | **1** (offset 492533) |
| dword `0x573d704b`, `0xfc9d6556` | 0 | 0 |

`BUILDINFO.txt` noemt `0005-luigismansion-com-block.patch` (1 treffer), dus de huidige build heeft
de Luigi-case. Onze kern herkent de game dus wel en neemt de tak die alleen COM blokkeert. De
gesloten kern bevat geen Luigi-naam en geen van de drie CRC-vormen -- en de string
`BLOCKING LUIGI COM PORT` waarop patch 0005 is gebaseerd staat er ook niet in.

**Voorbehoud:** de CRC-tabel kan obfuscated of berekend zijn, en het project heeft ook de cores
1.0.0.3762 en 1.0.0.3763 in huis terwijl de runtime 1.0.0.3761 is, dus de decompilatie waar patch
0005 uit komt kan van een andere versie zijn. Maar met de binary die hier draait is de hypothese
"de gesloten kern heeft een Luigi-specifieke tak" niet houdbaar zoals hij was.

Wat er dan overblijft: **de gesloten kern levert iets algemeens dat het spel nodig heeft, en wij
niet.** Niet "onze aanhechting stoort iets" en niet "de gesloten kern kent de game".

## 4. De opstelling: wat er fout was, en waarom dat een les is

De EERSTE poging stierf ook met de GESLOTEN kern: exitcode **5** (`ERROR_ACCESS_DENIED`) na 11,5 s,
met `winedbg.exe` in de modulelijst. Dat is per de vooraf opgeschreven regel een fout in de
OPSTELLING. Vier verschillen met de bewaarde werkende run (`/tmp/rg-start3-LuigisMansion.log`, 11:13):

| | werkende run (11:13) | mijn eerste poging |
|---|---|---|
| WINEPREFIX | `~/.local/share/TeknoParrotUI/prefixes/shared/wine` | `~/.local/share/ravengun/prefixes/shared/wine` |
| werkmap van de LADER | `.../runtime/TeknoParrotUi-2.0.0.130-linux-x64` | `.../views/LuigisMansion/exe/x64` |
| hulpketen | `rg-jvsbridge`/`pipehelper.exe ... TeknoParrot_JvsState 64 Z:\dev\shm\TeknoParrot_JvsState`, `shm mirror active`, `TP_LAUNCH_SESSION_ID` | geen van beide |
| display | `DISPLAY=:0` | geen DISPLAY (kale ssh-sessie) |

**De les: doe de opstelling niet zelf na.** Laat de bestaande route hem zetten en verwissel alleen
de kern:

```bash
python3 scripts/launch-direct.py LuigisMansion --run
```

De kern is niets anders dan `argv[1]` van de lader (die doet er `+ ".dll"` achter), dus de enige
variabele per run is de INHOUD van die ene DLL. Plus headless gamescope voor de display. Daarmee
leeft (a) en is de bisectie geldig.

## 5. Drie fouten in mijn eigen gereedschap, en de reparaties

1. **`herstel()` met een kleefvlag.** `[[ $TERUG -eq 1 ]] && return 0; TERUG=1` wordt maar een keer
   waar, dus vanaf de tweede verwisseling zette het script de kern nooit meer terug. Run (d) en
   beide debug-runs draaiden met de DLL van de vorige run, en de runtime bleef met ONZE kern
   achter. Herstel is nu idempotent op het BESTAAN van de backup, en elke run controleert achteraf
   de sha256 die `launch-direct.py` ZELF in zijn log schrijft tegen de DLL die wij bedoelden; klopt
   dat niet, dan staat er `GELDIG NEE`. De runtime is met de hand hersteld en gecontroleerd op
   `f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`.
2. **De bench-controle blokkeerde zichzelf.** `pgrep -f 'TeknoParrotUi'` matcht de hele
   commandoregel, en mijn eigen ssh-aanroep eindigde op een pad met
   `.../runtime/TeknoParrotUi-2.0.0.130-linux-x64/...`. Het script zag dus zijn eigen proces als
   "de bench is bezet" en weigerde, met elke keer een nieuwe pid. Het bouwt nu eerst zijn eigen pid
   plus alle voorouders en sluit die uit.
3. **De bestandsteller zocht `Data/` met schuine streepjes** terwijl Wine backslashes schrijft,
   en meldde daardoor altijd 0. Nu geteld op `CreateFileW L"..."`-paden met `Data(Global)?[\\/]`.
   **IJkpunt op de ruwe trace van (a): 3736 CreateFileW-regels, 1089 unieke paden, 393 eigen
   Data/DataGlobal-paden.** Een teller die 0 zegt moet dat eerst laten zien.

## 6. Een schrijfpad naar de NAS dat dicht moet

`views/LuigisMansion/exe/x64` is een symlink naar de NAS, en `teknoparrot.ini` daar (mtime
2026-10-12 10:54) bevat `[Error] Unsupported Executable=... NEW CRC: D3DC9603!`. Die regel is door
ONZE kern geschreven, op een moment dat patch 0005 nog niet bestond (0005 is van 12:20). Onze kern
heeft dus naar de NAS geschreven. Hier is niets aan veranderd (alleen-lezen), maar het hoort dicht.

## 7. COM1: het spel raakt de poort niet aan, met en zonder patch 0005

De premisse van patch 0005 is onbewezen (zie Â§3), dus is de patch zelf getoetst met de pty-harnas
op `dosdevices/com1` van de prefix die de werkende route gebruikt. De prefix is eerst voorverwarmd
(de wineserver starten), anders zet Wine `com1..com32` terug op `/dev/ttyS*` en is de link weg
voordat de game bestaat. Beide runs via `launch-direct.py --run`, headless gamescope, limiet 45 s.

| run | kern | sha256 | exit | COM1-blokken | CreateFileW | eigen data |
|---|---|---|---|---|---|---|
| com1-zonder-0005 | onze kern zonder 0005 | `90df073c` | 32 | **0** | 86 | 0 |
| com1-met-0005 | onze kern met 0005 | `fe2ecf8c` | 32 | **0** | 77 | 0 |

De kern zonder 0005 is gebouwd met `b0-bouw-kern.sh` via `build-openparrot.sh --out`, dus de
actieve build bleef ongemoeid (`fe2ecf8c` voor en na), en de nieuwe kern bevat de Luigi-CRC niet
(0 treffers op `0x03 96 dc d3`).

**Twee onafhankelijke instrumenten, en ze zijn het eens:**

1. **De pty zelf.** De logger heeft aantoonbaar gedraaid: de kop met de banner, 33 s looptijd en de
   afsluitregel `einde: 0 B van het spel, 0 B van ons`. Nul blokken van het spel.
2. **De `+file`-trace**, die elke `CreateFileA/W` logt: 86 en 77 regels, waarvan **0 met COM in de
   naam** en 0 unieke paden die op een poort lijken (`COM[0-9]`, `ttyS`, `ptmx`, `pts`).

De poort stond dus open en was van ons, en de game heeft hem nooit aangeraakt. En de A/B zegt er
iets bij: **0005 maakt voor deze game geen enkel meetbaar verschil** -- zelfde exitcode, zelfde
30 s, zelfde nul eigen databestanden, zelfde nul poortactiviteit.

**Gevolg: COM is de hefboom niet, en 0005 is een dood spoor.** Advies: laat de COM-blokkade
vervallen (de detectie zelf, `GameID::LuigisMansion` met de CRC-case, is wel bewezen werkend en
nuttig). Redenen: (a) de bewering waarop de blokkade rust is in deze build niet terug te vinden;
(b) met en zonder maakt geen verschil; (c) hij blokkeert COM voor een game die COM niet gebruikt,
dus hij beschermt niets en zou een toekomstige route waarin we de game juist een poort geven stil
saboteren.

## 8. Apparaatopsomming: onbeslist, en waarom dat geen "nul" is

Omdat het spel `cfgmgr32.dll` en `hid.dll` laadt, is de volgende vraag of het zijn bord via
SetupAPI/HID zoekt. Gemeten met `WINEDEBUG=-all,+setupapi,+hid,+cfgmgr32,+file` op drie runs:

| run | exit | looptijd | setupapi-regels | hid-regels | `+file`-regels |
|---|---|---|---|---|---|
| 0-zonder-kern | 32 | 1,5 s | 6330 | 2472 | 1094 |
| b-onze-kern | 32 | 30,0 s | **0** | **0** | 77 |
| a-origineel (gesloten kern) | 124 | 39,2 s | **0** | **0** | 4474 (762 eigen datapaden) |

De nul in de laatste twee kolommen is **geen bewijs dat het spel niet opsomt**: de setupapi- en
hid-regels die er in run (0) wel zijn komen van Wine's EIGEN apparaatinfrastructuur (het opsommen
van de `ROOT`-bus, en Wine's virtuele HID-apparaten met `vid 845e`), en die verschijnt in de
andere twee runs niet omdat daar een al warme wineserver/hulpketen het apparaatwerk deed voordat
mijn kanaal actief was. Dit is precies de valkuil "een leeg resultaat moet bewijzen dat het
gezocht heeft", en dat bewijs ontbreekt hier. **Deze meting is dus onbeslist, niet negatief.**

Wat WEL vaststaat, met een instrument dat aantoonbaar wel actief was: **geen enkele run opent ooit
een `\\?\hid#...`-apparaatpad of een COM-poort** -- ook de gesloten kern niet, die leeft en 762
eigen datapaden laadt. Het spel krijgt in dit venster dus geen apparaathandle, ook niet als het
leeft. Daarmee is het leveren van een openbaar apparaat niet de verklaring, en blijft instrument A
(de geheugenvergelijk) de volgende stap.

## 9. Instrument A: het spelbeeld op vaste momenten onder de gesloten kern

Twee runs via de werkende route met `WINEDEBUG=-all`, met `sudo` gedumpt op een vast moment na het
eerste verschijnen van het spelproces. `b0-geheugen.sh` + `b0-geheugen-dump.py`; per dump eerst
`beeldcheck.py`. Ruwe dumps: `/tmp/b0-geheugen-b/` (8 s), `/tmp/b0-geheugen-d/` (5 s),
`/tmp/b0-geheugen-c/` (4 s).

| moment | `.text` %nul | `.capcom1` %nul | entry point in het PROCES | anon. uitv. gebieden | threads/CPU |
|---|---|---|---|---|---|
| 4 s | 100,0 % | 2,6 % | `eb 08 92 1b ...` = als het bestand | 8 | 1 / 100 % |
| 5 s | 100,0 % | 2,6 % | `48 b8 80 e9 d4 f6 ff 6f 00 00 ff e0` | 11 | 1 / 80 % |
| 8 s | **11,2 %** | 2,6 % | `eb 08 92 1b ...` = als het bestand | 19 | 1 / 62 % |

**De tijdlijn van de gesloten kern, met de dump in de hand:** op 5 s staat er op het entry point
`48 B8 <imm64> FF E0` -- `movabs rax, 0x6ffff6d4e980; jmp rax`. Dat is exact de `entryJump` die
`InstallEntryPointInitializer()` in `OpenParrotLoader/src/main.cpp` schrijft. De game zit dus in de
**entry-point-trampoline van de lader** en er is nog niets ontpakt. Op 8 s staat het entry point
weer op de ongerepte `eb 08` van de Capcom-stub, en is `.text` voor 88,8 % gevuld: de trampoline
heeft de originele bytes teruggezet, de pakker heeft gelopen, en het spel laadt zijn data.

Daarmee is ook het dode proces van eerder (pid 363640, `48 B8 <imm64>` op het entry point) verklaard:
dat stierf in de trampoline-fase, voordat de pakker begon. "Nooit ontpakt" blijft dus staan; het
mechanisme was niet de `EB FE`-spinlus maar de trampoline die nooit werd teruggezet.

**Antwoord op de drie vragen, voor zover de gesloten kern ze kan beantwoorden:**

* (a) **De gesloten kern patcht het entry point NIET.** Op 8 s is het entry point byte-identiek aan
  het bestand (`eb 08 92 1b 19 00 00 00 00 00 e9 d0 2b 01 00 ff`), en de headers zijn `GELIJK`.
  Wat er op 5 s staat is de trampoline van de LADER, niet van de kern, en die is op 8 s weer weg.
* (b) Het spel is op 8 s volledig ontpakt en op 4-5 s nog niet. Het ontpakken gebeurt dus tussen
  5 en 8 s, direct nadat de trampoline de controle teruggeeft.
* (c) Het aantal anonieme uitvoerbare gebieden groeit mee: 8 (4 s) -> 11 (5 s) -> 19 (8 s).

**Het enige verschil tussen geheugen en BESTAND dat geen trampoline is:** een regio van 3494 bytes
op VA `0x14120d000 .. 0x14120dda5`, binnen `.capcom1`. Die staat er in de 5 s-dump EN in de 8 s-dump,
op precies dezelfde plek -- dus hij is geschreven VOORDAT de pakker liep. Of dat de pakker zelf is
(een eerste tabel of ontsleuteling), de lader, of een patch van de kern, is met alleen de gesloten
kern niet te beslissen. **De beslissende vraag is dus heel gericht geworden: staat diezelfde 3494
bytes ook in het geheugen onder ONZE kern op hetzelfde vroege moment?** Zo ja, dan is het niet
onderscheidend. Zo nee, dan schrijft de gesloten kern iets in het spel dat wij niet schrijven.

**Wat nog NIET gelukt is:** de dump onder ONZE kern. Drie pogingen (8, 5 en 4 s) gaven "geen
proces": met `WINEDEBUG=-all` sterft het spel onder onze kern al rond 5-7 s, terwijl het met de
`+file`-trace (die het proces vertraagt) 25-30 s leefde. Het script dumpt niet als het proces weg
is -- dat is goede discipline, maar het betekent dat de VERGELIJKING nog niet gemaakt is. De
eerstvolgende stap is dus: onze kern dumpen op 2-3 s, en alleen twee adressen bekijken -- het
entry point (staat de trampoline er nog, en wordt hij ooit teruggezet?) en `0x14120d000` (die 3494
bytes). Dat is geen volledige vergelijking meer maar een vraag met twee adressen.

## 10. Wat nog open is

### 9b. De twee-adressenmeting: de tijdlijnen naast elkaar

`b0-momenten.sh` + `b0-momenten.py`: een run per kern via de werkende route, en EEN root-proces dat
het spelproces op 2/3/3,5/4/5/6/8/10/12 s bemonstert (effectieve resolutie ongeveer 1,5 s, want elke
monstername leest 3,5 MB). Ruwe uitvoer: `/tmp/b0-mom/`.

| t | ONZE kern `fe2ecf8c` | GESLOTEN kern `f5c7532d` |
|---|---|---|
| 1,2 | proces verschijnt | proces verschijnt |
| 2,0 | entry ongerept `eb 08`, `.text` 100 % nul, 1 thread | entry ongerept `eb 08`, `.text` 100 % nul, 1 thread |
| 3,0 | **TRAMPOLINE** `48 B8 -> 0x6ffffd36e410`, 1 thread | ongerept `eb 08`, 1 thread |
| 3,5 | TRAMPOLINE, 1 thread | ongerept `eb 08`, 1 thread |
| 4,0 | TRAMPOLINE, 1 thread | ongerept `eb 08`, 1 thread |
| 5,0 / 6,0 | -- | ongerept `eb 08`, 1 thread |
| **6,3** | **PROCES WEG** | -- |
| 8,0 | -- | **TRAMPOLINE** `48 B8 -> 0x6ffff6d4e980`, 1 thread |
| 10,0 | -- | ongerept `eb 08`, **`.text` 11,2 % nul**, **15 threads** |
| 12,0 | -- | ongerept `eb 08`, `.text` 11,2 % nul, 21 threads |

**Drie conclusies, alle drie met de dump in de hand:**

1. **De 3494 bytes op `0x14120d000` zijn GEEN patch van de kern.** Ze staan in ELKE monstername van
   BEIDE kernen, vanaf t=2,0 -- dus al voordat er ook maar een trampoline staat. Ze zijn dus van de
   lader of van Wine, niet van de kern. Deze kandidaat is daarmee af.
2. **De trampoline komt in BEIDE kernen, alleen op een ander moment en naar een ander adres.** Onze
   kern: t=3,0 naar `0x6ffffd36e410`. Gesloten kern: t=8,0 naar `0x6ffff6d4e980`. Dat past bij het
   verschil in laadtijd van de DLL: onze kern is 1,9 MB, de gesloten is 56 MB. De entry-point-
   trampoline wordt dus pas geinstalleerd als `LoadLibraryW` van de kern terug is.
3. **Het verschil zit in waar de aanhechtingsfase eindigt.** Onder onze kern sterft het proces op
   6,3 s; op de laatste bemonsterde momenten daarvoor (3,0 / 3,5 / 4,0) staat de trampoline nog op
   het entry point en is `.text` nog 100 % nul. Onder de gesloten kern wordt de trampoline tussen
   8,0 en 10,0 teruggezet, en pas daarna loopt de pakker (`.text` 11,2 % nul) en springt het aantal
   threads van 1 naar 15 en 21.

**CORRECTIE (2026-10-12): "nooit teruggezet" is NIET vol te houden.** Mijn bemonstering had een gat:
de momenten waren 3,0 / 3,5 / 4,0 en daarna pas 6,3 -- **2,3 s waarin niet gekeken is**, precies
het venster waarin de terugzetting volgens een tweede meting valt. De juiste formulering is: *niet
teruggezet binnen de bemonsterde momenten*.

Er is een tweede, onafhankelijke meting van de laderbouwer met een trace IN `InitializeASI`:

| | mijn bemonstering | zijn trace |
|---|---|---|
| komt `InitializeASI` terug? | niet waargenomen (maar 4,0 -> 6,3 s niet gekeken) | **ja** |
| wanneer worden de entry-bytes teruggezet? | niet gezien | **2,36 s na het laden van de kern** (dus ongeveer 5,4 s als de kern op 3,0 s klaar was) |
| wat gebeurt er daarna? | proces weg op 6,3 s | spel springt naar zijn entry point en beeindigt zichzelf **239 ms later** met 32 |

Beide metingen kunnen tegelijk waar zijn: de terugzetting valt in mijn gat, en het spel gaat er kort
daarna uit. Wat dat betekent voor de vraag: het is dus niet "onze init komt niet terug" maar **"onze
init komt terug, het entry point wordt teruggezet, het spel springt naar zijn entry point -- en gaat
er 239 ms later uit met 32, terwijl het onder de gesloten kern doorloopt."** Dat is een andere vraag,
en een gerichtere.

**De dichte bemonstering (nog te doen, bench nodig):** 2,5 tot 7 s met stappen van 0,5 s, met
dezelfde 20-byte-vergelijking op het entry point:

```bash
RG_B0_MOMENTEN="2.5,3,3.5,4,4.5,5,5.5,6,6.5,7" bash b0-momenten.sh
```

Dan is te zien of mijn tijdlijn of zijn trace klopt, of dat beide kloppen en het verschil in de
laadtijd van de kern zit. **En het venster dat er dan toe doet is die 239 ms:** blijft `.text` nul,
wat doet het aantal threads, en welke bestanden opent het spel -- daar moet het verschil zitten.

Onder onze kern blijft het aantal threads de hele tijd **1**: er wordt niets meer opgestart. Dat is
geen "de kern patcht iets" en geen "de kern kent de game niet", maar **de aanhechtingsfase loopt
onder onze kern niet af**. De volgende vraag is dus wat er in dat venster gebeurt -- wat de
trampoline aanroept (`InitializeASI` in de kern-DLL) en waarom dat onder onze kern niet terugkomt.
Dat is werk in de kern, niet in deze repo.

## 10. Wat nog open is

1. **Wat de gesloten kern in het spel zet.** Geheugenvergelijk van het spelbeeld op hetzelfde vroege
   moment onder beide kernen (`beeldcheck.py` voor de levenscheck, sudo). Zoek op de entry-point-
   regio, de prologue van wat de pakker aanroept, en de plek waar het spel zijn I/O opzet.
2. **Run (0) met `WINEDEBUG=+thread,+module`** om te zien wat het spel in die 1,5 s precies doet.
3. **De aanroepen naar het spel toe vergelijken** in de eerste seconde van het nu geldige
   debug-paar: schrijfacties, `VirtualProtect`, `CreateRemoteThread`, mappings.
4. **B1-B3 zijn NIET meer de volgende stap.** Die ladder vroeg "wat van onze generieke kern stoort
   het spel", en dat is met (0) op 32 niet meer de vraag. De vraag is nu: welke voorziening moet
   onze eigen hook bieden die de gesloten kern blijkbaar biedt.

## 8. Uitvoeren

```bash
bash b0-bouw.sh                 # de twee DLL's, in de MinGW-container
bash b0-run.sh 0                # het spel zonder kern
bash b0-run.sh a                # de controle; moet 124 geven (leeft)
bash b0-run.sh b c d            # de bisectie
bash b0-run.sh debug            # de WINEDEBUG-vergelijking
```

Het script weigert te starten als er een game, lader, wineserver of gamescope draait (zonder zijn
eigen procesboom mee te tellen), start zelf een headless gamescope zodat het scherm van de eigenaar
vrij blijft, zet de kern-DLL altijd terug en controleert dat met sha256, en veegt na elke run
resten weg. Een orphan vergiftigt de volgende meting.

