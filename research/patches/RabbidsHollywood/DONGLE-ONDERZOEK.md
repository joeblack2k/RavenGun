# Rabbids Hollywood: waarom de uitgeleverde route na ~70 s stierf — en wat de meting daarna liet zien

Onderzoek door de dongle-specialist, 2026-10-12. NAS alleen gelezen, niets gecommit (de eigenaar commit).
Bewijstags: **[M]** gemeten in deze ronde en vastgelegd in een bestand · **[R]** uit de bron/config gelezen ·
**[I]** gevolgtrekking, niet gemeten.

Kort: de oorzaak was een **uitrolprobleem**, geen verkeerde donglewaarde. De uitgeleverde view droeg een
plugin van vóór de Matrix-fix. `play-unity.sh` rolt de verse plugin bij elke start zelf uit, en met die verse
plugin **leeft de game 288 s op de uitgeleverde route** (was ~70 s).

---

## 1. Hoe de meting eindigde: zelf gestopt, niet uitgevallen

**[M]** Run `rg-rabbids-run.service` (systemd-user-unit), `scripts/play-halo.sh --profile RabbidsHollywood
--no-wait-move`, start 2026-10-12 21:59:34, einde 22:04:37.

| Moment | Feit | Bron |
|---|---|---|
| 21:59:37 | `Game.exe` voor het eerst gezien | `/home/deck/rg-run3/main.log:19` |
| t=0…278 s | elke 10 s: game-pid aanwezig, `matrix_regels=3`, `dongle_found=1`, `dongle_notfound=0` | `main.log:20-76` |
| t=288 s (22:04:25) | eindbewijs: `387832 Game.exe -popupwindow` **leeft nog** | `main.log:77-79` |
| 22:04:25 | `--- stoppen ---` / `Game gestopt, uitvoer ontwapend.` | `main.log:136-137` |
| 22:04:37 | `BENCH LEEG`; unit `inactive`, `Result=success` | `main.log:140-141` |

**Dus: ik heb hem zelf gestopt, op het gevraagde moment (≥180 s), en niet omdat hij uitviel.** Het spel leefde
**288 s** (4 min 48 s) vanaf `Game.exe`; de vorige meting op deze route was ~70 s. Er is geen uitval geweest:

* `grep -c 'Application.Quit'` → **0** **[M]**
* de laatste regels van `LogOutput.log` zijn de doorlopende attract-lus (`BotPlayer ::: CollectInfo`,
  `Enemy.Actions.SurpriseAttack`), niet een afsluitpad **[M]**
* de daemon meldde op het stopmoment nog `Rabbids Hollywood Arcade: VERBONDEN met de game, 120 pakketten/s,
  1 uitvoerberichten` **[M]** (`main.log:135`)

Het spel is **headless** gedraaid (`gamescope --backend headless`, Xwayland op `:1`) zodat het scherm van de
eigenaar vrij bleef. Dat is de enige afwijking van de uitgeleverde start: dezelfde `play-halo.sh`, dezelfde
view, dezelfde Proton, alleen een ander `DISPLAY`. Zie §6 voor wat dat kostte.

---

## 2. De aanvraagketen, met per stap het bewijs

De aanroepende code is `SBK.Matrix.MatrixManager.Awake` → `KDLGNPHHMKI` **[R]** (`docs/matrix-layer.md`,
uit de IL van `Assembly-CSharp.dll`; die IL-ontleding is in een eerdere ronde gedaan en is hier niet
overgedaan). Onze antwoorden komen op de **uitgeleverde route uit de plugin** (managed prefix op
`MatrixDLLCSharp.MatrixWrapper`), **niet** uit `matrix64.dll`.

| # | Aanroep (en args) | Eis van het spel | Ons antwoord | Bewijs dat het gebeurde |
|---|---|---|---|---|
| 0 | `MatrixWrapper.Init_MatrixAPI` | `< 0` = fout | **0** | log r. 37 (`Patch: MatrixWrapper.Init_MatrixAPI`) + r. 97 (`Matrix: Init_MatrixAPI -> 0 (onze eigen dongle-laag)`) |
| 1 | `MatrixWrapper.Dongle_Count(85)` | `> 0` | **2** | log r. 39 (patch toegepast) + r. 98 (`Matrix: Dongle_Count(85) -> 2`) |
| 2 | `MatrixWrapper.Dongle_ReadSerNr(dongle, aantal, 85)` | `> 0` | **69420** (`0x10F2C`) | log r. 40 (patch) + r. 99 (`Matrix: Dongle_ReadSerNr -> 69420`) |
| 3 | *(het spel zelf)* `MatrixManager+GJOEKNLLLHL` | logt `**** DONGLE FOUND: <getal>` | — | log r. 100: `[Info : Unity Log] **** DONGLE FOUND: 69420` |

Noot bij stap 0/1: de **argumenten** (`85`) en de **volgorde** blijken uit de IL en worden in de meting
bevestigd door de plugin-regels (`Dongle_Count(85)`); de **teruggegeven waarden** blijken uit de plugin-regels
én uit de acceptatie van het spel (r. 100). Dat het spel ons serienummer overneemt in zijn eigen regel
(`: 69420`) is het sterkste bewijs dat de keten klopt.

Twee dingen die **niet** gebeurden, en die de moeite van het vermelden waard zijn:

* `Dongle_Find` is wél gepatcht (log r. 38) maar **niet aangeroepen** — er is geen `Matrix: Dongle_Find -> 1`
  in het log. De patch is dus defensief; het opstartpad gebruikt hem niet. **[M]**
* De **drop-in-route is in deze run niet gebruikt**: er staat geen
  `Matrix: de drop-in matrix64.dll beantwoordt de dongle-controle (patches uit)` in het log, en
  `views-aagames/RabbidsHollywood/Game_Data/Plugins/matrix64.dll` is nog steeds de originele van 93.184 B
  (md5 `c86908ab10b1d9249c896e42b7618f67`). **[M]**

### De zelfuitrol, bewezen

Dit was de kern: een correcte fix die de view nooit bereikt is niets waard.

| Moment | View-DLL | Build-DLL |
|---|---|---|
| 21:56:54, vóór de start **[M]** | 32.768 B, md5 `b4fe9d835a8e60c691b5637b77afc2f4`, mtime 2026-10-11 17:12 | 34.304 B, md5 `29fd3ddc31f14f526c32df8e7d5f3895` |
| 21:58:19, ná `play-halo.sh` **[M]** | 34.304 B, md5 `29fd3ddc31f14f526c32df8e7d5f3895`, mtime 2026-10-12 21:58:19 | idem |

De keten is `play-unity.sh:72-75` → `aagames-setup.py` → `install_ravengun` → `install_ravengun` kopieert
`build/unity-plugin/<Game>/RavenGun.<Game>.dll` naar de view. De view-DLL is na de start **byte-identiek** aan
de build (sha256 `b8f5f41a3855b88c8996d3706a95b59993a83e3436465f5988e029d380e7df91`). **[M]**

---

## 3. Waarom het eerst misging

* De plugin in de **uitgeleverde** view was 32.768 B, mtime 2026-10-11 17:12, md5 `b4fe9d83…` en bevatte
  **geen enkele Matrix-code**: geen `MatrixInit_Prefix`/`MatrixFind_Prefix`/`MatrixCount_Prefix`/
  `MatrixSerNr_Prefix` in de metadata, en geen `Dongle_Count`/`Dongle_ReadSerNr`-teksten (UTF-16). **[M]**
* De Matrix-fix kwam om 19:53 in `plugins/unity/src/rabbids/Rabbids.cs` en zat alleen in
  `build/unity-plugin/RabbidsHollywood/RavenGun.RabbidsHollywood.dll` (34.304 B, md5 `29fd3ddc…`) en in de
  **niet-uitgeleverde** view `views/RabbidsHollywood`. **[M]**
* Route B was sinds 17:13 **niet meer gestart** (het `LogOutput.log` van die view had mtime 17:13), dus
  **nooit met de dongle-fix**. **[M]**
* Dat oude log laat de fout zien: **nul `Matrix:`-regels**, `[Info : Unity Log] **** DONGLE NOT FOUND`
  (regel 92) en als laatste regel `OnChooseWorldEnter` — precies het symptoom "haalt de wereldkeuze en stopt
  binnen 50-70 s". **[M]**
* En er kon ook niets antwoorden: `views-aagames/<Game>/Game_Data/Plugins` is een **symlink naar de NAS**, dus
  de `matrix64.dll` daar (en in de spelmap) was de **originele** van 93.184 B, niet onze 25.614 B drop-in. **[M]**

---

## 4. Dekt onze laag nu de hele donglebehoefte? Nee, één stuk niet

**Het opstartpad: ja, volledig gedekt.** Drie aanroepen, drie antwoorden, spel accepteert (zie §2).

**Het savegame-pad: nee.** `SBK.Matrix.MatrixManager` roept `Dongle_EncryptData`/`Dongle_DecryptData` aan op
acht plaatsen (de savegame-versleuteling). De plugin patcht die **niet**, dus die gaan naar de **originele
NAS-`matrix64.dll`** — zonder dongle. Dat is in deze run **zichtbaar gevolg** geweest: **[M]**

```
103: [Info   : Unity Log] Bad PKCS7 padding. Invalid length 41.
105: [Warning: Unity Log] WARNING : Savegame corrupted : MetricManager will be resetted
109: [Info   : Unity Log] Bad PKCS7 padding. Invalid length 249.
111: [Warning: Unity Log] WARNING : Savegame corrupted : OperatorMenu will be resetted
114: [Info   : Unity Log] Bad PKCS7 padding. Invalid length 173.
116: [Warning: Unity Log] WARNING : Savegame corrupted : SBK.OptionsManager will be resetted
```

Het spel liep er niet op stuk (288 s), maar **bij elke start worden de savegames als corrupt gezien en
gereset**. Deze run was te kort, en in attract/wereldkeuze, om te zeggen of dat later bijt (operatorinstellingen,
metrics, een save/load tijdens het spelen). Dit is de enige aanroepfamilie die wij op de uitgeleverde route
**niet** beantwoorden. Twee manieren om het te dekken, beide nog te meten:

1. `Dongle_EncryptData`/`Dongle_DecryptData` in de plugin ook als no-op beantwoorden (0 terug, buffer
   ongemoeid) — precies wat onze `matrix64.dll` al doet. Let op: dat verandert het artefact dat hier gemeten
   is, dus dan is een nieuwe meting nodig.
2. De drop-in op de uitgeleverde route daadwerkelijk uitrollen (dan doet de DLL beide paden), wat meteen de
   `Game_Data/Plugins`-symlink naar de NAS moet oplossen (zie §5).

Verder niet nodig: `Dongle_ReadData`, `Dongle_WriteData` en `Dongle_MemSize` komen in de hele assembly
**niet** voor **[R]** (`patches/RabbidsHollywood/README.md:115-117`, dnlib-grep). Er is dus geen dongle-image
nodig.

**Eén latente valstrik [I]:** `RAVENGUN_MATRIX_DLL=1` zet de Matrix-patches van de plugin **blind** uit. Op de
uitgeleverde route staat geen drop-in, dus als die variabele daar ooit in de omgeving belandt, valt de game
terug op de originele DLL en sterft hij weer na ~70 s, zonder duidelijke oorzaak. `play-unity.sh` zet hem niet
en `dropin.ini` wordt op deze route niet gelezen, dus in deze meting was hij niet gezet — maar de schakelaar
kan zichzelf niet controleren.

---

## 5. Was de symlink `Game_Data/Plugins` een echt risico in deze run?

**Nee, in deze run is er niets naar de NAS geschreven** — het spel heeft alleen gelezen. Hard bewijs:

* `find "$NAS/Game_Data/Plugins" -newermt '2026-10-12 21:50'` → **leeg** **[M]**
* `find "$NAS" -maxdepth 3 -newermt '2026-10-12 21:50'` → **leeg** **[M]**
* `Game_Data/Plugins`-map mtime nog steeds **2026-10-09 15:39:53**, `Game_Data` idem **[M]**
* `matrix64.dll` op de NAS én in `Game_Data/Plugins`: md5 `c86908ab10b1d9249c896e42b7618f67`, mtime
  **1996-12-24 23:32** — onveranderd **[M]**

Wat het spel wél schreef, kwam allemaal **lokaal in de view** terecht (mtime's 21:59): **[M]**
`Gun_10_12_2026_0.log` (43 B), `ForceReboot` (0 B), `Game.run` (0 B), `ControllerBaseConfig.json` (957 B),
`BepInEx/LogOutput.log`. Dat is de bedoeling van `copy_small=True` in `aagames-setup.py`.

**Twee kanttekeningen, zodat dit niet te ruim wordt opgeschreven:**

1. De mount is **niet** read-only: `findmnt` toont CIFS `rw` (`//192.168.2.3/emulation`,
   `file_mode=0755,dir_mode=0755`). "Geen schade" is hier dus een **gedrags**uitspraak, geen
   bestandssysteemgarantie. **[M]**
2. Op de NAS heeft `ControllerBaseConfig.json` mtime **2026-10-12 01:46:40**. In de **andere** view
   (`views/RabbidsHollywood`, de `launch-direct`-route) is dat bestand een **symlink naar de NAS**; daar
   schrijft het spel dus wél door. De uitgeleverde `views-aagames`-view is veilig (lokale kopie), de oude
   view niet. **[M]** voor de mtime en de symlink; **[I]** voor de toeschrijving aan die eerdere run.

Het risico blijft dus staan voor de **drop-in**: `To = Game_Data/Plugins` wijst op deze route naar de NAS, en
`aagames-setup.py` (`link_all(..., copy_small=False)`) linkt elk kind. Wie de drop-in hier uitrolt, schrijft
op de NAS. Dat moet eerst lokaal gemaakt worden.

---

## 6. Twee gereedschapsfouten die deze meting aan het licht bracht

1. **De probe vecht met de daemon om dezelfde client.** `ravengun-aagames-probe` is geen injector op de
   daemon maar een **tweede TCP-cliënt** van de plugin. De plugin houdt één client aan, dus vanaf het moment
   dat de probe verbond (log r. 677) wisselen verbinden en loskoppelen elkaar af: 10× `connect` /
   10× `disconnect`, `last_error: "de game sloot de verbinding"`. Vóór de probe was er **één** verbinding
   (log r. 35) en bleef het stil. **[M]** Gevolg: de crosshair-controle uit het loaderrapport (§4 stap 5) kan
   **niet** naast een verbonden daemon draaien; die stap moet of met de daemon gestopt, of via de daemon zelf.
2. **Headless gamescope krijgt geen toetsen.** `ravengunctl key` stuurt via uinput naar de compositor van de
   **eigenaar**; gamescope met `--backend headless` heeft daar geen venster. De vier `key 5` en de `key 1`
   zijn dus niet in het spel aangekomen: er is precies **één** `Stand:`-regel in het hele log (credits 0). **[M]**
   Munten, start en het vizier zijn daarom in deze run **niet** uitgeoefend; dat vereist een run op de tv
   (of een andere toetsenweg). De levensduur en de dongle-antwoorden zijn er niet door geraakt.

---

## 7. Reproductie

```bash
# 0. bench vrij? (en niets van TeknoParrot)
pgrep -af 'wine|VACUUM|Game.exe|OpenParrot|TeknoParrot|pipehelper' || echo "bench leeg"

# 1. headless scherm (scherm van de eigenaar blijft vrij)
export XDG_RUNTIME_DIR=/run/user/1000
gamescope --backend headless -W 1920 -H 1080 -- sleep 900 &
#    wacht op "Starting Xwayland on :N" in de uitvoer -> N
export DISPLAY=:N WAYLAND_DISPLAY=gamescope-0

# 2. de uitgeleverde route, onveranderd
scripts/play-halo.sh --profile RabbidsHollywood --no-wait-move

# 3. meteen hierna: de zelfuitrol bewijzen
md5sum ~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/BepInEx/plugins/RavenGun.RabbidsHollywood.dll
md5sum ~/projects/ravengun/build/unity-plugin/RabbidsHollywood/RavenGun.RabbidsHollywood.dll   # gelijk

# 4. de antwoorden en de acceptatie van het spel
L=~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/BepInEx/LogOutput.log
grep -a -E 'Matrix:|DONGLE' "$L"
#   Matrix: Init_MatrixAPI -> 0 ... / Dongle_Count(85) -> 2 ... / Dongle_ReadSerNr -> 69420 ...
#   [Info : Unity Log] **** DONGLE FOUND: 69420        (en 0x 'DONGLE NOT FOUND', 0x Application.Quit)

# 5. geen TeknoParrot in de cgroup
cg=$(systemctl --user show -p ControlGroup --value ravengun-tpui.service)
for p in $(cat "/sys/fs/cgroup$cg/cgroup.procs"); do cat "/proc/$p/comm"; done | sort -u
#   python3 steam.exe wineserver services.exe winedevice.exe plugplay.exe svchost.exe
#   Game.exe explorer.exe rpcss.exe tabtip.exe xalia.exe   <- geen TeknoParrotUi/OpenParrotLoader64/pipehelper

# 6. stoppen
scripts/play-halo.sh --stop
```

Ruwe artefacten van de run staan op de Deck in `/home/deck/rg-run3/` (`main.log`, `00-old-LogOutput.log`,
`01-selfdeploy.txt`, `02-final-LogOutput.log`, `probe.log`, `gamescope.log`).

---

## 8. Wat nog open is

* **Herhalen.** Deze ronde had één geslaagde run van 288 s; de bench ging daarna naar Luigi B0, dus een tweede
  run kon niet. Eén geslaagde run is minder sterk dan twee.
* **Savegame-pad dekken** (§4) en daarna opnieuw meten — geïmplementeerd, zie §9; de proef staat klaar.
* **Munten/start/vizier** op de tv, waar de toetsenweg wél werkt.
* **`Game_Data/Plugins` lokaal maken** in `aagames-setup.py` (§5) — **gedaan**: op 2026-10-12 22:13 staat
  `views-aagames/RabbidsHollywood/Game_Data/Plugins` als een **echte lokale map** met kopieën (geen
  koppelingen meer naar de NAS), en `matrix64.dll` daarin is nog de originele van 93.184 B
  (`c86908ab…`). Een drop-in landt daar dus voortaan lokaal. Blijft staan: bij het uitrollen controleren
  dat het zo blijft.
* **`RAVENGUN_MATRIX_DLL`** niet blind laten vertrouwen (§4).
* Stale tekst: `game.ini` is bijgewerkt (`Status = playable`, met de gemeten waarheid en wat er niet
  gemeten is in `Note`/`NoteEN` — de statuswoordenschat is `playable | parked | untested` uit
  `src/app/core/GameData.h:11`, en `playable` staat hier bewust mét die Note, niet kaal), maar
  `packaging/games-status.md:271-272` en `findings/RabbidsHollywood.md:7` zeggen nog 50-70 s.

---

## 9. Vervolg: het savegame-pad beantwoorden (beslissing, uitvoering, stand van zaken)

**Beslissing: oplossing (a), niet (b).** De plugin beantwoordt `Dongle_EncryptData`/`Dongle_DecryptData`
zelf. (b) — de drop-in op de uitgeleverde route uitrollen — valt af omdat `Game_Data/Plugins` daar een
symlink naar de NAS is (§5): dat zou op de NAS schrijven. (a) is ook netter: onze eigen laag geeft het
antwoord, niet een DLL die toevallig in de buurt staat.

**Wat er is nagekeken voordat er iets gepatcht werd** (opnieuw gemeten met `re/dnlib.py` op de
Assembly-CSharp.dll van deze game, md5 `ad2813f95179f7b4df28bab3a7b56c69`):

* `MatrixDLLCSharp.MatrixWrapper` heeft **exact 27 methoden**, en daarin precies **twee** crypto-varianten:
  `short Dongle_EncryptData(int, int&, short, short)` en `short Dongle_DecryptData(int, int&, short, short)`.
  Er is **geen** `*Ex`-, `*Memory`-, `*File`- of size-uitvoervariant. Twee patches dekken dus alles.
* Er zijn **dertien aanroepen**, elk in een eigen methode: Encrypt uit `KPHIPOINDLP`, `BMNNMICCONF`,
  `EDNBLKCLMOI`, `NDIBJIHIEGD`, `NKLMBDBGNGM`, `MIBFIOJCFDO` (alle op `IL_007D`); Decrypt uit
  `AJPGJFKKFIB`, `EFOGIECKCJK`, `LKFFFBKBLFF`, `GDIPNEDGNPL`, `CBJBMICIDFJ`, `PGOAGOPNHGP`, `BABEGMOMBOG`
  (alle op `IL_0091`). (Het README zei "acht"; dat was een onvolledige telling.)
* De buffer gaat **per blok van 8 byte** naar binnen, als `ref int` (`KPHIPOINDLP` bouwt een `int[2]` uit
  8 bytes van de `byte[]`). Daarom is half beantwoorden erger dan niet beantwoorden.
* De aanroepers gaan alleen verder als het serienummer > 0 is (`KPHIPOINDLP` bij `IL_0017`,
  `AJPGJFKKFIB` bij `IL_001C`, waar anders `**** DONGLE NOT FOUND` volgt). Met onze 69420 loopt dit pad
  dus echt mee — het is niet dormant.

**Uitvoering.** `plugins/unity/src/rabbids/Rabbids.cs` (alleen dat bestand; bron-md5
`60d3a74d2ea5ad9bbba98364d95889bc`): `MatrixEncrypt_Prefix` en `MatrixDecrypt_Prefix`, die **geen enkel
argument** van het origineel opvragen — alleen `ref short __result` — en `0` teruggeven met de buffer
ongemoeid. Omdat ze geen argumenten binden, hangen ze niet van parameternamen of buffervorm af (precies
waar het eerder met `poort`/`Port` misging) en blijft de buffer gegarandeerd ongemoeid. Per richting wordt
alleen de eerste aanroep gelogd; die twee regels zijn tegelijk het bewijs dat geen van beide richtingen
vergeten is.

| Artefact | Grootte | md5 | sha256 |
|---|---|---|---|
| build `build/unity-plugin/RabbidsHollywood/RavenGun.RabbidsHollywood.dll` (2026-10-12 22:12) | 34.816 | `e0eaa4231475d0e5825a19e9df9fe3d7` | `8b011be1471a5ed5151290e0fa52120d6697a691cf9f2036fb83409e5b36c3b6` |
| view `views-aagames/RabbidsHollywood/BepInEx/plugins/…dll` (22:18, dus al uitgerold) | 34.816 | `e0eaa4231475d0e5825a19e9df9fe3d7` | idem |
| view daarvóór (de vorige build, 21:59) | 34.304 | `29fd3ddc31f14f526c32df8e7d5f3895` | `b8f5f41a3855b88c8996d3706a95b59993a83e3436465f5988e029d380e7df91` |

De view is dus inmiddels **wel** bij: op 22:18 stond `e0eaa423…` er, zonder dat er een spel mee gedraaid
heeft (het spellog is nog van 22:04, de run met de vorige build). De proef controleert dat op vier
momenten en breekt af als de view niet de build is — anders meet je per ongeluk `29fd3ddc…` en lijkt de
reparatie niet te werken.
Dat is niet aangenomen maar apart gemeten, en daarom staan beide hashes hier apart.

**De proef is gedraaid (2026-10-12 22:48-22:54).** `rg_savegame.sh`, twee starts, headless. Uitkomst en
wat eruit volgt staat in §10.

---

## 10. De twee-startproef: uitkomst, en een vondst die de aanname eronder omverhaalt

### 10.1 Wat de proef wel aantoonde (geldige meting)

De poort was op alle vier de momenten schoon: de view-DLL was `e0eaa423…` = de build, vóór de start, na de
zelfuitrol, tijdens het spel en in run 2 — **niet** de oude `29fd3ddc…`. Alle zes Matrix-patches zaten erin
en werden toegepast (`Init_MatrixAPI`, `Dongle_Find`, `Dongle_Count`, `Dongle_ReadSerNr`,
`Dongle_EncryptData`, `Dongle_DecryptData`), zonder overgeslagen of mislukte patches. Het spel leefde in
beide runs (170 s en 171 s game-tijd) met `**** DONGLE FOUND: 69420`, en de NAS is over het hele venster
onaangeroerd (0 vermeldingen nieuwer dan de start; beide `matrix64.dll` nog `c86908ab…`). Op procesnaam
stond er geen enkel TeknoParrot-achtig proces in de cgroup.

### 10.2 De uitkomst is MISLUKKING, niet "gedeeltelijk C"

Het script koos zelf "GEDEELTELIJK (C)" en dat is **fout gekozen**: zijn regelvolgorde test eerst
"geen padding-fout" en pas daarna "één richting ontbreekt". De gemeten cijfers zijn run 1 `PKCS7=3
corrupted=3 resetted=3` en run 2 `PKCS7=4 corrupted=3 resetted=3` — de padding-fout is er in **beide**
runs, dus de vooraf opgeschreven uitkomst die hoort is **MISLUKKING**: "run 2 reset nog; 0 terug met
ongemoeide buffer is niet genoeg". De C-vlag is een bijkomend feit, niet het verdict. (De regelvolgorde in
`rg_savegame.sh` is hierbij een fout van mij; het verdict moet op de zwaarste voorwaarde eerst testen.)

### 10.3 `Dongle_EncryptData` wordt nooit aangeroepen — en dat is geen toeval

In geen van beide runs verscheen `Matrix: Dongle_EncryptData -> 0`; `Dongle_DecryptData -> 0` verscheen wel.
Opnieuw geteld met `re/dnlib.py` op de assembly: van de **zes Encrypt-wrappers** in `SBK.Matrix.MatrixManager`
(`KPHIPOINDLP`, `BMNNMICCONF`, `EDNBLKCLMOI`, `NDIBJIHIEGD`, `NKLMBDBGNGM`, `MIBFIOJCFDO`) heeft **geen
enkele een verwijzing** in de hele IL. Ze zijn in deze build dood. Van de zeven Decrypt-wrappers worden er
zes aangeroepen, en alleen vanuit `MatrixManager` zelf (kleine thunks) plus twee keer vanuit
`NGBPMKLAEPB::MJDMKLODGIG`. Onze Encrypt-reparatie kan dus per definitie nooit vuren.
(Voorbehoud: een verwijzing via een niet-renderende methodebody zou buiten deze telling vallen.)

### 10.4 De savegame-versleuteling loopt NIET via de dongle — dit is de eigenlijke vondst

Het lees- en schrijfpad van de savegames komt **niet** bij `MatrixManager` uit:

```
SaveGameManager::WriteData        -> IJGPGJJKJAG::AJAKOIOBKPI , IJGPGJJKJAG::JEJBHONPKAL
SaveGameManager::ReadSaveGame     -> IJGPGJJKJAG::JEJBHONPKAL
SaveGameManager::ReadData         -> ReadSaveGame (2x) -> bij falen:
                                     "WARNING : Savegame corrupted : " + " will be resetted"
                                     -> SendSaveGameData -> InternalResetSaveGame
SaveGameManager::ReadSaveGame     -> MD5-controle: "Corrupted savegame, bad MD5 match. ..."
```

`IJGPGJJKJAG` is een **andere klasse** dan `SBK.Matrix.MatrixManager`, en `WriteData` schrijft
`ServerRequestManager::GetDeviceID` in de stream. De `Bad PKCS7 padding`-meldingen komen uit die eigen
laag, niet uit de dongle. Dat weerlegt de aanname in `docs/matrix-layer.md`, in de README en in
`dongle.json` ("acht methoden van MatrixManager roepen Dongle_EncryptData/DecryptData aan; dat is de
savegame-versleuteling"): de Encrypt-kant is dood en het savegame-pad raakt `MatrixManager` niet.

### 10.5 Waar de versleutelde data vandaan komt: nergens vreemds

De vraag was of het spel iets ontsleutelt dat wij niet schrijven. Gemeten: **nee**.

* Het spel noemt in het log uitsluitend `<naam>.svg` en `<naam>.bkg` onder
  `C:/users/steamuser/AppData/LocalLow/Sarbakan/RabbidsShooter/savegame/`.
* Dat is precies de map die de proef had leeggemaakt. Run 1 begon dus met een lege map en logde **toch**
  padding-fouten, inclusief `Bad PKCS7 padding. Invalid length 0.` — een leeg buffer. Die meldingen horen
  dus bij het eerste-keer/reset-pad en zijn geen bewijs van een vreemde bron.
* Run 1 heeft daarna zelf drie bestanden geschreven (7-8 s in de run, mtimes 22:48:20-21) en run 2 las die
  terug en zakte alsnog door de padding-controle.
* Er is wel een tweede plek: `views-aagames/RabbidsHollywood/NVRAM/savegame/` bevat een **volledige** set
  van zes bestanden (drie `.svg` + drie `.bkg`). Die staan in geen enkele logregel en zijn resten van de
  eerdere TeknoParrot-route (`SAVE_TO_GAME_FOLDER=1`, 9 oktober), niet de bron van de fout.

**Conclusie.** De savegame-fout is geen dongleprobleem. Onze Encrypt/Decrypt-reparatie is onschadelijk maar
kan het niet oplossen, en de volgende vraag is niet "moet onze laag de bytes transformeren" maar "waarom
kan het spel zijn eigen savegame niet teruglezen": dat zit in `IJGPGJJKJAG` en in wat
`ServerRequestManager::GetDeviceID` per run oplevert. §11 zet die vraag recht.

---

## 11. Waar `GetDeviceID` vandaan komt — en waar de versleuteling dan wél zit

### 11.1 De apparaat-ID is onze eigen dongle-serie, geen systeemwaarde

IL van `ServerRequestManager::GetDeviceID` (dnlib, deze assembly):

```
IL_0000: ldfld m_DeviceID ; IsNullOrEmpty
IL_000B: brfalse -> return m_DeviceID        ; al gevuld: geef terug
IL_0010: get_Instance (MatrixManager)
IL_0015: callvirt MatrixManager::get_GetDongleSerialNumber
IL_001B: ldc.i4.0 ; ble -> IL_003A
IL_0022: m_DeviceID = serial.ToString()      ; > 0: de dongle-serie
IL_003A: m_DeviceID = "0"                    ; <= 0: de letterlijke nul
```

**Dus `GetDeviceID()` is `MatrixManager.GetDongleSerialNumber().ToString()`, of `"0"` als die ≤ 0.**
Daarmee vervalt de hypothese `SystemInfo.deviceUniqueIdentifier`: `SystemInfo` wordt in deze assembly
alleen voor graphics en capaciteiten gebruikt (`processorCount`, `supportsInstancing`,
`SupportsRenderTextureFormat`, …) en `GetDeviceID` raakt het nergens aan. Er is ook geen bestand, geen
register en geen serverwaarde in het pad — het is onze eigen 69420.

Twee gevolgen:

1. **De apparaat-ID is stabiel zolang wij hetzelfde antwoorden** (69420), dus hij is niet de bron van een
   per-run verschil. De reparatie "zorg dat die waarde vastligt" is niet nodig — hij ligt al vast, door ons.
2. **Hij is wél veranderd toen wij de dongle repareerden**: vóór de fix gaf `Dongle_ReadSerNr` 0, dus was de
   apparaat-ID `"0"`; daarna 69420. Elk savegame dat vóór de fix is geschreven hoort dus onleesbaar te zijn
   ná de fix — een eenmalige overgang. In de twee-startproef hadden **beide** runs 69420 en tóch zakte run 2
   op het bestand van run 1, dus dit verklaart de proef niet.

### 11.2 De versleuteling zit niet in managed code

`IJGPGJJKJAG` is geen crypto maar de **IO-providerlaag**: velden `NDJDMMJENJI` (een lijst providers),
`IOKGHLCPFNF` (naam), `DKEGNOOGGAB`; methoden als `NOLCDCGJPEI(string,string,byte[])`, `OAPNBGOKPDD(...)`,
`HFKHCEGMMNI(string,string)`, `HJMHALLLBON()`, en de logregel `"IO Provider '<naam>' was used to load empty
data: file will be resetted."` Daar zit geen sleutel.

Een zoekactie over de hele assembly naar `Rijndael|Aes|CryptoServiceProvider|CreateDecryptor|CreateEncryptor|
get_Key|set_Key|get_IV|set_IV` levert **geen enkele crypto-aanroep** op; alleen `SaveGameManager::
GetMD5HashFromFile` gebruikt `MD5CryptoServiceProvider`, en dat is de integriteitscontrole
(`"Corrupted savegame, bad MD5 match"`), niet de versleuteling.

**De `Bad PKCS7 padding`-melding komt dus uit een native module, niet uit `Assembly-CSharp.dll`.** De game
levert daarvoor precies één kandidaat mee in `Game_Data/Plugins`: **`sxCore.dll` (181.248 B) met
`sxCoreSettings.txt` (6.051 B)**, naast `matrix64.dll`. Dat is de volgende plek om te kijken, en het is geen
donglelaag — de dongle raakt er hooguit indirect aan via de apparaat-ID hierboven.

### 11.3 Wat dit betekent voor de 10-minutenrun

"Waarom leest het spel zijn eigen savegame niet terug" is niet met de apparaat-ID te verklaren, dus de run
legt het volgende vast zodat die vraag in één keer meegaat:

* een snapshot van de savegame-map vóór en na de run (namen, groottes, md5, mtimes);
* md5 + mtime van `Game_Data/Plugins/sxCoreSettings.txt` en `sxCore.dll` vóór en na — **herschrijft het spel
  die settings per run?** Dat is de goedkoopste test van "de sleutel verandert per run";
* het aantal `Bad PKCS7 padding`, `Savegame corrupted` en `will be resetted`-regels in het log;
* of `Dongle_DecryptData` wel en `Dongle_EncryptData` niet vuurt (dat hoort zo te zijn, §10.3).

### 11.4 Correcties op bestaande documenten (met de meting erbij)

Drie plekken beweren dat de savegame-versleuteling via `SBK.Matrix.MatrixManager` en de dongle loopt. Dat is
met deze meting weerlegd; ze zijn gecorrigeerd:

| Bestand | Wat er stond | Wat er nu staat |
|---|---|---|
| `docs/matrix-layer.md` (§4, bullet `Dongle_EncryptData`/`DecryptData`) | "komen op dertien plaatsen voor (de versleuteling van de savegame)" | de Encrypt-wrappers zijn dood; het savegame-pad loopt via `IJGPGJJKJAG` en `GetDeviceID`, en de crypto is native (`sxCore.dll`) |
| `patches/RabbidsHollywood/README.md` ("Let op: deze game gebruikt de dongle ook voor zijn versleuteling") | idem | idem, met de IL-bewijzen |
| `patches/RabbidsHollywood/dropin/ravengun-hasp/RabbidsHollywood/dongle.json` (`notes`) | "acht methoden van `SBK.Matrix.MatrixManager` roepen `Dongle_EncryptData` of `Dongle_DecryptData` aan (de savegame-versleuteling)" | de Encrypt-kant is dode code; het savegame-pad raakt `MatrixManager` niet |

Onze `MatrixEncrypt_Prefix`/`MatrixDecrypt_Prefix` blijven staan: bewezen onschadelijk (schone poort, spel
leefde in beide runs, NAS onaangeroerd) en ze dekken de semantiek die de drop-in al had. Ze kunnen dit
probleem alleen **per definitie niet oplossen**, omdat het savegame-pad `MatrixManager` nooit aanraakt.

---

## 12. De 10-minutenrun: GEHAALD (2026-10-12 23:10:46-23:21:10)

Eigenaarseis: Rabbids moet **10 minuten** draaien op onze eigen lader. Dat is nu gemeten. Script
`/home/deck/rg_10min.sh`, log `/home/deck/rg-10min/main.log`, systemd-user-unit `rg-10min`, headless via
gamescope (`--backend headless`), stopmoment 600 s game-tijd met een harde limiet van 720 s.

### 12.1 Het verdict, met de cijfers

| | |
|---|---|
| Game-tijd | **608 s** (10 min 8 s) — `t=608s STOPMOMENT (600s) bereikt` |
| Einde | **zelf gestopt**, niet uitgevallen: `Game.exe` nog in leven op het stopmoment (`504342 Game.exe -popupwindow`), daarna pas `Game gestopt, uitvoer ontwapend.` |
| Threads | 115 |
| `Application.Quit` | **0** |
| `DONGLE NOT FOUND` | **0** |
| `DONGLE FOUND` | 1 (`**** DONGLE FOUND: 69420`) |
| Verbinding | **120 pakketten/s op alle 20 levenslijnen** (`VERBONDEN met de game, 120 pakketten/s`), `uitvoerberichten` tot 22 — nooit weggevallen |
| TeknoParrot in de cgroup | **geen enkel** (alleen python3, steam.exe, wineserver, services/plugplay/svchost/winedevice, Game.exe, explorer, rpcss, tabtip, xalia) |
| NAS over het hele venster | 0 vermeldingen nieuwer dan 23:10:46; beide `matrix64.dll` nog `c86908ab…` |
| Poort | view-DLL == build == `e0eaa423…` vóór de start |

De 8 s boven de 600 s is de 30 s-tik van de levenslijn; het spel is niet te vroeg en niet te laat gestopt.

### 12.2 Munt en start aangeslagen, en een ECHT level gespeeld

In headless gamescope komen de virtuele toetsen van `ravengunctl key` niet aan, maar de game draait op
gamescope's eigen Xwayland, en `DISPLAY=:1 xdotool key …` komt daar **wel** binnen. Daarmee is voor het
eerst op deze route munt én start uitgeoefend:

```
[23:12:20] --- munt/start via xdotool (DISPLAY=:1) ---
[23:12:27] Stand: credits 2,0 ammo 0 leven 100 speelt 0
[23:12:27] Stand: credits 3,0 ammo 0 leven 100 speelt 0
[23:12:27] Stand: credits 4,0 ammo 0 leven 100 speelt 0
[23:12:27] Stand: credits 4,0 ammo 0 leven 100 speelt 1     <- start aangeslagen
[23:12:27] Stand: credits 0,0 ammo 0 leven 100 speelt 1     <- credit verbruikt
```

En daarna speelde het spel een **level**, geen attract-lus — de levensbalk liep leeg en de speler ging dood,
waarna het spel netjes terugkeerde naar attract:

```
leven 100 -> 44 -> 35 -> 25 -> 15 -> 4 -> -6 -> speelt 0 -> leven 100 speelt 0
```

De state-machine liep daarbij door menu -> demo -> spelen -> dood -> attract
(`ArcadeGamePlayManager ::: OnDemoStateExit` / `OnMenuStateEnter` / `OnDemoStateEnter`). Dit is het verschil
dat de eigenaar bedoelde: niet "levend", maar **speelbaar**.

### 12.3 De eerlijke grenzen van deze run

* **Trekker en trilling van een echte Move zijn niet uitgeoefend** — er is geen Move. De muismodus kan een
  trekker geven (`BTN_LEFT`), maar dan moet er een fysieke muis bewogen worden, en dat kan headless niet.
* **Het vizier is op het scherm niet visueel bevestigd** — headless is er geen scherm. Wat wél gemeten is:
  de aim van de daemon is live (`ravengunctl output` las vóór elke ingreep `aim 0.543,0.498`, licht
  drijvend naar 0.546 / 0.535 — de Steam Controller Puck als richt-muis), en een expliciet punt komt er
  exact uit: `test-pattern point 0.3 0.3` gaf `Positie: aim 0.300,0.300 -> abs 9830,9830`, en `0.7 0.7` gaf
  `0.700,0.700 -> abs 22937,22937`. Dat de daemon stuurt en het spel op invoer reageert staat dus vast; het
  beeld zelf niet.

### 12.4 Twee nieuwe savegame-feiten

1. **Het spel HERSCHRIJFT zijn savegames tijdens het spelen** — alle drie de bestanden kregen een andere md5
   in deze run (`MetricManager.svg`, `OperatorMenu.svg`, `SBK.OptionsManager.svg`). Het schrijfpad werkt dus
   en wordt veel gebruikt.
2. **`sxCoreSettings.txt` en `sxCore.dll` zijn ONVERANDERD**: md5 `0cd773e4612b9c6e73ca9f6503d827ff` resp.
   `bdf048494c2b7320e8efdd53727bfbd4`, en de mtime van `sxCoreSettings.txt` bleef `2026-10-12 22:13:16`
   (het kopieermoment). **De hypothese "de sleutel verandert per run via `sxCoreSettings`" is daarmee
   weerlegd.**
3. En de padding-fouten lopen **door tijdens het spelen**: `pkcs7=10 corrupted=3 resetted=3` in 10 minuten,
   tegen 4 in de 2-minutenproef. Het is dus niet één slechte lezing bij het opstarten maar iets dat blijft
   misgaan terwijl het spel speelt.

### 12.5 Wat dit betekent voor de volgende stap

De sleutel zit dus elders, of de **schrijfwijze** zelf is fout. Mijn twee beste kandidaten, met de reden:

**Kandidaat 1 — het schrijf- en leesformaat verschillen per route (sterkste kandidaat, want al deels
gemeten).** De oude TeknoParrot-route liet een **leesbaar** bestand achter:
`NVRAM/savegame/OperatorMenu.svg` (10 oktober) begint met `efbeadde`, dan een 32-teken hex-id
(`02a88957bf12d652be797e53e76d3ade`), dan `OperatorMenu`, dan `OperatorMenu+OperatorMenuSaveInfo` en pas
daarna een grote blob — een gestructureerd bestand met platte tekst in de kop. De bestanden die onze route
nu schrijft zijn **volledig hoog-entropisch**: geen enkele leesbare tekst, 80/112/8784 byte. Er is dus een
component die toen een leesbaar bestand maakte en nu niet meedoet. Waarom: dat is de vraag, en het is
precies het verschil tussen de twee routes. Zoeken in `IJGPGJJKJAG` (de IO-providerlaag) naar welke provider
toen actief was — de logregel `"IO Provider '<naam>' was used to load empty data: file will be resetted."`
noemt die naam, dus die staat in het logboek.

**Kandidaat 2 — een sleutel of IV per sessie die nergens bewaard wordt.** De apparaat-ID ligt vast (69420,
§11.1) en `sxCoreSettings` verandert niet (§12.4), dus elke resterende variatie per run moet bij het
opstarten gegenereerd worden of uit een bron komen die de lader niet levert (server/cabinet-config,
`RabbidsHollywood_Operator.conf`, `ControllerBaseConfig.json`). Als de sleutel per run nieuw is en niet in
het bestand staat, is elk bestand van een vorige run per definitie onleesbaar — precies wat de twee-startproef
liet zien. **Goedkoopste test:** twee runs met een leeggemaakte savegame-map en dan de **eerste 16 byte** van
hetzelfde bestand vergelijken. Zijn die per run random, dan is dat een IV en zit de sleutel elders; zijn ze
gelijk, dan is het niet de IV. Daarnaast: `strings` en de exportlijst van `sxCore.dll` (staat in de view,
dus zonder bench te lezen) om te zien of die sleutelmateriaal of een KDF aanbiedt.



