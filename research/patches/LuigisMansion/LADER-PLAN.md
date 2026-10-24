# Luigi's Mansion Arcade: de laderhelft -- plan, stand en volgende meting

RavenGun, 2026-10-12. Alleen-lezen op de NAS; niets aan het spel of de NAS gewijzigd.
Deze kaart hoort naast `pakker-meting.md` (laag 1: de pakker) en beschrijft **laag 2**
(de aanhechting van de kern) plus de COM1-vraag.

Marker: **[gemeten]** = zelf of in een eerdere ronde met een trace/voorwaarde vastgesteld,
**[afgeleid]** = gevolgtrekking uit die metingen, **[aanname]** = nog niet getoetst.

---

> ## CORRECTIE 2026-10-13 -- section 1 is wrong on the dongle and the keychip
>
> **(a) old claim, quoted -- section 1, line 16:** "Geen dongle, geen keychip, geen HASP."
>
> **(b) measured now:** only the **HASP** half still holds. The game **does** carry a dongle and a keychip
> layer, in the form of the Sega Nu `am` platform middleware: `oxGetHwInfo.exe` statically links it and its
> strings include "SystemError : amDongleInit failed.", "amDongle Build:Jan  9 2014 02:12:04 ($Rev: 2644 $)",
> "Error: amyDongleOpen() failed.", "SystemError : amSystemPropertyGetKeychipRegion failed." and
> "Error : Can't access keychip." The closed TeknoParrot core detours **17** `am`-layer entry points in
> `VACUUM.exe`'s `.text` (8 `amDongle*`, 5 `amHm*`, 4 `amMaster*`) and the game then survives 300 s.
> No `matrix32.dll`/`matrix64.dll`, no `hasp/`, no `.rgd` and no licence file is present -- so the
> "geen HASP" part of the old sentence is measured correct and stays.
>
> **(c) proof:** `patches/LuigisMansion/BESCHERMINGSLAAG.md` section 2.1 "The Sega `am` platform/dongle
> layer -- the answer" and section 2.2 "There is no HASP, no Matrix/SBK, no licence file and no daemon in
> this title".
>
> **(d) date:** 2026-10-13.
>
> The old sentence is kept in place below, struck through. Nothing else in this plan was re-measured today
> and the rest of section 1 is left untouched.


---

## 1. Wat de game is en welke route hij nodig heeft

Sega Nu 1.1, x64 Windows, `VACUUM.exe` (3.436.544 B), D3D11, DirectInput, `bcrypt`,
`setupapi`, `wlanapi`, `iphlpapi`. Capcom-gepakt (`.capcom0`/`.capcom1`, vijf secties met
0 bytes in het bestand, entry point in `.capcom1`). ~~Geen dongle, geen keychip, geen HASP.~~ **[CORRECTIE 2026-10-13: only "geen HASP" holds -- a dongle and keychip layer (Sega Nu `am`) IS present. See the CORRECTIE block at the top of this file and BESCHERMINGSLAAG.md section 2.1.]**

De route die werkt **[gemeten, ronde 57 en eerder]**:

| onderdeel | waarde |
|---|---|
| lader | `OpenParrotx64/OpenParrotLoader64.exe`, standaardroute (`RemoteThread = off`) |
| kern | **gesloten** `TeknoParrot/TeknoParrot64.dll` |
| werkmap | `views/LuigisMansion/exe/x64` (via `TP_GAME_WORKING_DIRECTORY`) |
| view | `Data -> DataGlobal/EN/Data`, `Movie/` naar de NAS-map, `exe` naar de NAS |
| films | `GST_PLUGIN_PATH=~/.local/share/ravengun/tools/gst-extra/plugins` (`libgstasf.so`, `libgstlibav.so`) |
| invoer | `TeknoParrot_JvsState`, profiel `shared-luigi-mansion`; onze daemon is de enige schrijver (`scripts/tp-shm-swap.sh`) |
| `sendFlash COM1 LMAIO.bin`, `oxGetHwInfo.exe`, `X:`/`Y:` | **niet nodig** |

De eigen kern valt om met **exitcode 32** **[gemeten]**.

**Hoe je die twee precies tegen elkaar zet** (belangrijk, want dit is de hele isolatie): de
kern is niets anders dan het **eerste argument van de lader**. `OpenParrotLoader64.exe <DLL>
<EXE>` laadt die DLL in het spelproces. De werkende run is dus
`OpenParrotLoader64.exe TeknoParrot64 <...>\VACUUM.exe` en de falende is
`OpenParrotLoader64.exe OpenParrot64 <...>\VACUUM.exe`, met verder alles gelijk. Let op:
`launch-direct.py` weigert `[Loader] Core = ravengun` bij `Type = TeknoParrot`
(`own_build()`: "een eigen kern bestaat alleen voor EmulatorType OpenParrot"), dus voor
`Type = TeknoParrot` komt de **lader** uit de eigen build en blijft de **kern** die van
TeknoParrot -- zoals `patches/_defaults/TeknoParrot.ini` ook zegt. De eigen kern krijgt deze
game dus alleen via de handmatige aanroep hierboven, of via `EmulatorType = OpenParrot` in
`game.ini`. Noteer bij elke meting welke van de twee DLL's je meegaf.

---

## 2. Wat al bestaat

* **De invoerhelft is af.** `packaging/tp-shm-profiles.json` heeft het volledige profiel
  `LuigisMansion` (stuurwoord offset 8: start 0/3, Strobulb 1/4, hendel 2/5, test 8,
  select switch 9, service 10, screen-out 16/17; asbytes 12..15 = P1Y,P1X,P2Y,P2X;
  kalibratie X 3..252, Y 6..250). `scripts/tp-shm-swap.sh` maakt de daemon de enige
  schrijver. **Onze invoer, hun lader.**
* **De pakker is in kaart.** `pakker-meting.md`, en `re/_vacuum-pe2.py` om na te rekenen.
* **De kern is er.** `third_party/openparrot/` met patches 0001--0005; `scripts/build-openparrot.sh`
  bouwt lader en kern; `~/.local/share/ravengun/openparrot/current` is de actieve build
  (sha256 `fe2ecf8c...` voor `OpenParrot64.dll`, `1953d91a...` voor de lader).
* **De omgevingskant is groen** **[gemeten, pre-flight 2026-10-12]**: view compleet,
  `VACUUM.exe` 3.436.544 B, precies twee GStreamer-plug-ins, `winegstreamer.so` aanwezig.

---

## 3. De blokker, precies zoals gemeten

Wat de rondes 56--67 hebben vastgesteld **[gemeten]**, en wat daarvan interpretatie is:

| # | vaststelling | status |
|---|---|---|
| 1 | Zelfde lader, zelfde omgeving, gesloten kern: game **START** en opent 1.877 eigen bestanden. | gemeten (ronde 57, 65) |
| 2 | Zelfde lader, **onze** kern: exitcode **32**, en het spelproces opent **nul** eigen bestanden; de enige bestandsactie is onze kern die zijn eigen `teknoparrot.ini` leest. | gemeten (ronde 65, 67) |
| 3 | Geen Wine-fout, geen unhandled exception; alleen een `oleacc`-fixme. | gemeten (ronde 58) |
| 4 | 32 staat niet als constante in onze lader of kern; `launch-direct.py` kent 3/4/5. De code komt dus van het spel zelf, via `ExitProcess`. | gemeten (ronde 66) |
| 5 | `32` = `ERROR_SHARING_VIOLATION` zou bij een bestandsconflict passen. **Vervallen**: er is geen enkele `CreateFileW` van het spel. | weerlegd (ronde 67) |
| 6 | "exit 32 = Capcom anti-tamper" | **interpretatie**, niet gemeten |
| 7 | Een dood proces op deze game bleek **nooit ontpakt**: `.text` op `0x140001000` volledig nul, 1 thread, 100% CPU, PPid 1. | gemeten (2026-10-12) |
| 8 | Het entry point **in het proces** was `48 B8 <imm64>` in plaats van de `EB 08` van het bestand. | gemeten |
| 9 | Dat is "de spinlus `EB FE` van de lader" | **weerlegd** -- de bytes zijn geen `EB FE`. Blijft staan als mogelijkheid in het algemeen, niet als verklaring hier. |

**De kern van de blokker [afgeleid, goed onderbouwd]:** het spel sterft tijdens het
**aanhechten** van de kern, voordat het iets van zichzelf doet. Het is geen crash en geen
bestandsconflict. De variabele is uitsluitend de **kern-DLL**: met dezelfde lader en
dezelfde omgeving werkt de gesloten kern en sterft de onze.

Over punt 8: `48 B8 <imm64>` is de vorm die `InstallEntryPointInitializer()` in
`OpenParrotLoader/src/main.cpp` op het entry point schrijft (`entryJump = 48 B8 <imm64> FF E0`).
Dat is de **parkeer-/ManagedInit-route** (`TP_ENTRYPOINT_REMOTETHREAD_MS` +
`TP_LOADER_MANAGED_INIT`), niet de standaardroute. Dat is een sterke aanwijzing dat dat
dode proces op een **andere injectieroute** liep dan de route die werkt -- maar het is
**[afgeleid]**: de pakker kan dezelfde vorm zelf schrijven, en het doeladres
(`0x6ffff6d4e980`, in een anoniem `r-xp`-gebied van dat proces) is zonder `sudo` niet na
te lopen. **Concreet gevolg:** zet voor deze game nooit
`TP_ENTRYPOINT_REMOTETHREAD_MS`, `TP_POSTSTART_REMOTETHREAD_MS` of
`TP_LOADER_MANAGED_INIT`. De `_defaults` en `launch-direct.py` doen dat al goed
(`RemoteThread = off` laat `TP_REMOTETHREAD` juist weg).

---

## 4. Het laderplan, in stappen

**Wat het plan NIET is:** een eigen image-loader die een gedumpt beeld mapt en naar een
OEP springt. De pakker ontpakt zichzelf in het proces, en de gesloten kern bewijst dat dat
met een onaangeroerd entry point werkt. Een image-loader zou de zelfgemodificeerde
toestand, de relocaties, de TLS en de imports van de pakker moeten nabouwen -- veel werk,
en het verandert juist alles wat het spel merkt. **Het dumpen van het beeld is nodig om te
lezen wat het spel doet (waar de invoer naartoe moet, waar de controle zit), niet om het
te laden.**

1. **Route kiezen.** `OpenParrotLoader64.exe <kern-DLL> <exe>`; geen `TP_*`-variabelen; werkmap
   `views/LuigisMansion/exe/x64`. Bewezen route: entry-point-trampoline op de
   **hoofdthread** (`RunTo` -> `LoadHookDLL` -> `SetThreadContext(RIP=OEP)` -> `ResumeThread`).
   Onze eigen kern is hier een **verwisseling van het eerste argument** (van
   `TeknoParrot64` naar `OpenParrot64`); dat maakt de bisectie hieronder goedkoop.
2. **Proces aanmaken** met `CREATE_SUSPENDED` + `CREATE_NEW_PROCESS_GROUP`, werkmap zoals
   stap 1. Zonder die werkmap: access violation.
3. **Onze eigen hook-DLL laden** in dat proces, en **niets anders**. Geen extra thread,
   geen console, geen logbestand, geen registersleutels, geen API die de gesloten kern niet
   ook aanraakt.
4. **Entry point ongemoeid laten** waar het spel het kan zien: de lader parkeert het even
   met `EB FE` en zet de originele bytes terug voordat de thread verdergaat. Dat is dezelfde
   lader als bij de werkende run, dus dit is geen variabele -- maar het is wel de reden om
   **geen** `EntryPointMs`-route te gebruiken.
5. **Invoer leveren.** De hook leest de 64 bytes van de mapping `TeknoParrot_JvsState`
   (onze daemon schrijft `/dev/shm/ravengun_JvsState`, `tp-shm-swap.sh` spiegelt die via
   `pipehelper.exe`), ontleedt de `LuigisMansion`-indeling, en schrijft die naar de plek
   waar het spel hem leest. **Die plek is precies wat de dump moet opleveren**: de gesloten
   kern doet het met directe procesgeheugenschrijfacties, en dat adres is de ontbrekende
   schakel. Zonder dat adres is er geen invoer, met of zonder kern.
6. **COM1 beantwoorden.** Zie Â§5. Kort: een pty op `dosdevices/com1` van de prefix, met
   `librg_comm_shim.so` voor de modemstatus, en een antwoord in de vorm van de I/O-kaart.
7. **Omgeving.** `Data`-symlink, `Movie/`, `GST_PLUGIN_PATH`, `wined3d`, geen DXVK.
   `sendFlash`, `oxGetHwInfo.exe` en `X:`/`Y:` blijven weg.
8. **Volgorde van bewijzen.** (a) titel, (b) attract met beeld, (c) start met invoer,
   (d) testmenu. Per stap de exitcode en het aantal threads noteren.

### Kan dit zonder iets te verstoren dat het spel controleert?

**Eerlijk antwoord: het laderplan kan, maar de vraag die het blokkeert is nog niet
beslist, en er zijn twee uitkomsten waarvan er een buiten de grens valt.**

* **(L) Onze kern doet te veel.** De gesloten kern is een kleinere indruk in het proces dan
  onze generieke OpenParrot-kern met zijn brede MinHook-set (in `amJvs.cpp` alleen al
  `CreateFileA/W`, `GetCommState`, `SetCommState`, `GetCommTimeouts`, `SetCommTimeouts`,
  `GetCommMask`, `SetCommMask`, `SetupComm`, `PurgeComm`, `EscapeCommFunction`,
  `ClearCommError`, `GetCommModemStatus`). Als de pakker een daarvan opmerkt, is de
  oplossing **minder** doen, niet de controle verslaan: een minimale per-game hook in plaats
  van de generieke kern. Dat is precies wat het eerdere oordeel al zei ("een eigen per-game
  hook is hier nodig, geen generieke infrastructuur"), en het is legitiem: het is het
  wegnemen van onze eigen indruk.
* **(A) De pakker tolereert geen enkele in-process aanhechting** -- bijvoorbeeld omdat hij
  zijn eigen secties of de prologues van aangehaakte API's narekent. Dan is er geen route
  die het spel ongemoeid laat, en is dit een beslissing voor de eigenaar, geen
  technisch obstakel om omheen te werken.

### De meting die dit in EEN run beslist

**Zet een minimale DLL in plaats van `OpenParrot64.dll` op de werkende route: leeg
`DllMain`, geen haken, geen threads, geen bestanden.** Verder alles hetzelfde.

* Komt het spel tot de titel -> de aanhechting zelf is het probleem niet, en de schuld
  zit in de initialisatie van onze kern. Ga door met de bisectie hieronder; het plan in
  Â§4 is dan de route.
* Sterft het ook met een lege DLL met exitcode 32 -> **elke** in-process aanhechting
  struikelt over iets, en dan geldt (A): melden, niet omzeilen.

Bisectieladder daarna, elk een run, in deze volgorde:

| # | wat | als het spel dan LEEFT |
|---|---|---|
| B0 | lege DLL in plaats van `OpenParrot64` (tweede argument van de aanroep) | aanhechting is ok -> B1 |
| B1 | onze kern met `jvsInit` (`amJvs.cpp`) dat meteen terugkeert | het zit in de COM/CreateFile-haakset -> B2 |
| B2 | `jvsInit` wel, maar alleen de `CreateFileA/W`-haken | het zit in de comm-haakset |
| B3 | `jvsInit` volledig, maar zonder patch 0005 | het zit in de COM-blokkade zelf |

Daarnaast, zonder code te wijzigen en dus gratis: **twee runs met `WINEDEBUG=+thread,+module,+file`,
een met de gesloten kern en een met de onze, en het eerste seconde na de start vergelijken.**
Dat laat zien welke threads en modules onze kern toevoegt die de hunne niet heeft. Dat is
de directe invulling van "wat verandert onze aanpak dat het spel merkt".

---

## 5. De COM1-vraag

**Gemeten:**

* `game.bat` doet `start /wait sendFlash COM1 LMAIO.bin` en daarna `VACUUM.exe`.
  `SendFlash.exe` is x64-console en importeert `CreateFileA/W`, `GetCommState`,
  `SetCommState`, `GetCommTimeouts`, `SetCommTimeouts`, `SetupComm`, `PurgeComm`,
  `GetCommProperties`, `ClearCommError`; strings: `OPEN COM PORT (%s):`, `FLASH ERASE:`,
  `FLASH WRITING:`, `FLASH WRITE:OK`, `COMPLETED`.
* De prefix heeft `dosdevices/com1 -> /dev/ttyS0`, en die is `root:uucp 0660`; `deck` zit
  niet in `uucp`. Wine geeft de game dus **`ERROR_ACCESS_DENIED` (5)** in plaats van
  "bestaat niet" (2). Dat is het verschil waar patch 0005 op mikte.
* Patch 0005 maakt van elke `COM*`-naam `INVALID_HANDLE_VALUE` + `ERROR_FILE_NOT_FOUND`,
  nagebouwd van de enige Luigi-tak van de gesloten kern (`BLOCKING LUIGI COM PORT`).
  **De game viel daarna nog steeds om met 32.**
* `sendFlash`, `oxGetHwInfo.exe` en `X:`/`Y:` zijn voor het spelen **niet nodig**.

**Wat nog niet gemeten is: wat het spel zelf op COM1 doet.** Dat is het gat dat de harness
vult. Twee uitkomsten, en beide zijn bruikbaar:

1. **De game raakt de poort niet aan** (of alleen een probe) -> COM1 is niet waar hij op
   stukloopt, en de blokker van Â§3 staat er los van. Dan is 0005 een dood spoor en moet de
   oorzaak in de aanhechting worden gezocht.
2. **De game opent de poort en wacht op antwoord** -> dan hoort ons antwoord de **I/O-kaart**
   te zijn, opgebouwd uit de gemeten bytes (te beginnen met de `sendFlash`-meting, die
   zonder game en zonder scherm kan). Dat is geen omzeiling: het is de hardware bieden die
   de kast ook biedt.

**Wat een antwoord op de poort legitiem maakt:** het komt uit een meting van wat de kaart
stuurt, niet uit "de controle moet slagen". En de goedkoopste eerste stap is er een zonder
het spel: `SendFlash.exe` alleen op de pty.

De harness staat in `com1-harness/` (zie de README daar). Uitvoeren:

```bash
bash ~/projects/ravengun-patches/patches/LuigisMansion/com1-harness/luigi-com1-selftest.sh
bash ~/projects/ravengun-patches/patches/LuigisMansion/com1-harness/luigi-com1-run.sh sendflash
```

---

## 6. Pre-flight voor een dure poging

`luigi-preflight.sh` in deze map. Een minuut, alleen lezen, exitcode 1 bij een blokkerend
punt. Het controleeert de view (`Data`-symlink, 10 films, `title_movie.wmv`), de exe en zijn
grootte, de werkmap en schrijfbaarheid van `Ram/`+`Log/`, de twee GStreamer-plug-ins en
`winegstreamer.so`, de prefix en de stand van `dosdevices/com1`, of de bench vrij is (en of
er een spinnende rest staat), of er `TP_*`-variabelen lekken, welke kern er werkelijk
gebruikt wordt (met sha256's), en of de NAS beschrijfbaar is.

```bash
bash ~/projects/ravengun-patches/patches/LuigisMansion/luigi-preflight.sh --kern
```

Uitkomst 2026-10-12: **0 blokkerende punten, 5 waarschuwingen.** Een mislukte start ligt dus
aan de kern, niet aan de omgeving -- precies de isolatie die nodig was.

`beeldcheck.py` hoort erbij: draai het met `sudo` op een lopend `VACUUM.exe` **voordat** je
dumpt. Het zegt of het proces leeft (threads, CPU, looptijd), wat er in de maps staat, of
`.text` nul is, en of het entry point in het proces afwijkt van het bestand -- en dat laatste
als **informatie**, niet als oordeel.

---

## 7. Eerlijke schatting

**Zeer hoog, en de grootste kostenpost is niet de pakker.** Redenen:

* De pakker is laag 1 en is **al omzeild door de pakker zelf**: de gesloten kern speelt de
  game met een onaangeroerd entry point. Aan de pakker hoeft dus niets te gebeuren.
* Laag 2 is de echte klus: een **per-game hook** die de invoer naar het juiste adres schrijft
  en COM1 beantwoordt, plus het uitzoeken wat onze generieke kern doet dat het spel merkt.
  Dat laatste is een bisectie van een paar runs, geen onderzoek van weken -- **mits** B0
  (lege DLL) groen is. Is B0 rood, dan is er geen route zonder de grens te passeren en stopt
  het hier.
* De invoerhelft is af, de omgeving is groen, en de pakker is in kaart. Dat scheelt veel.
* Onbekend en niet te schatten zonder meting: **het COM1-protocol** (mogelijk een echt
  I/O-bord nodig, en dat is hardware-emulatie), en **waar het spel zijn invoer leest** (de
  dump moet dat geven).

Ruwe band, met de kennis van nu: **B0 + de bisectie: 1 dag. De hook met invoer: 1--2 dagen
als het invoeradres uit de dump komt. COM1: een halve dag meten, en daarna onbekend -- kan
meevallen (de game negeert de poort) of tegenvallen (een bord emuleren).** De eerdere
beoordeling "zeer hoog" blijft dus staan voor het geheel, maar de eerste beslissende stap is
klein en goedkoop.

