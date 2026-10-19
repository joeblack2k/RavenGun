# Dagdienst 19 oktober 2026 — stand voor Jeroen

# Einde van de dag: per game de stand en het bewijs (2026-10-19)

Alles op onze eigen lader en kern, nooit TeknoParrot. "Niets op de NAS geschreven" wordt sinds deze
ronde op TWEE manieren gecontroleerd: met `find` op de NAS en in de SCHRIJFLAAG van bwrap
(`~/.local/share/ravengun/writelayer/<Game>/.../upper`, `find -type c` voor whiteouts plus de
bestanden), want `find` ziet geen verwijderingen en de schrijflaag verbergt het schrijven.

| game | stand | bewijs | waarom niet verder |
|---|---|---|---|
| **MenInBlack** | **working** | GESPEELD, apart in MUISMODUS gemeten (15:05-15:08): munt en START via het virtuele toetsenbord -> "PLAYER 1 READY", de missie begint en de trekker laat de score naar 0024 en de tickets naar 001 lopen (`docs/screenshots/meninblack-muismodus-gereed-2026-10-19.png` en `-score-...png`) | -- (de meting van 17-10 blijft staan voor de MUISKNOPPEN: die hebben geen munt- of START-functie) |
| **HOTD4SP** | parked | attract op de NATIVE Lindbergh-route (geen Wine, geen TeknoParrot): spel-ID 0x7d6ab3e3, JVS naar de pty van de IO-brug, twee fotos | munt en START starten geen spel; de veronderstelling is dat de ride-versie op stoelsignalen wacht (meting van 15-10; op 19-10 NIET opnieuw gemeten) |
| **TNXK0OC** | parked | start en tekent op onze kern, foto `docs/screenshots/tnxk0oc-amilb-error-2026-10-19.png` | de game meldt zelf "ERROR 3005 / AMLIB INITIALZE FAILED"; de kaststap mxGetHwInfo.exe eindigt onder wine met status 1 (staat nu als commentaar in het profiel) |
| **SegaGameShell** | parked | de shell start en tekent op onze kern, foto `docs/screenshots/segamegameshell-game-error-2026-10-19.png` | de shell meldt zelf "GAME ERROR": in deze map staat geen spel voor hem. De logbestanden in shell/ waren bestands-koppelingen naar de NAS en Wine VERWIJDERT DOOR die koppelingen heen (alleen de schrijflaag van bwrap beschermde de NAS; een naakte --run was niet veilig). Pass 2 (review 18) lost dat op met `[Launch] ViewLocalFiles = shell`: bij de start worden alle bestands-koppelingen lokale kopieen en blijven alleen de map-koppelingen (Graphics, Sounds) staan. Live nagekeken: 264 bestanden lokaal, 3 koppelingen over (Shell.exe, Graphics, Sounds), schrijflaag 157 whiteouts/2 bestanden voor en na, NAS-find 0. |
| **LostLandAdventure** | parked | start op onze kern (Loading core, Have fun, JVS-pijp verbonden) | met gamescope ECHT aan (sleutel in [Display], 3,5 minuut gewacht) bleef het beeld zwart; of er een venster was is NIET vast te stellen (gamescope draait zijn eigen Xwayland, xdotool op het scherm van de host ziet daar niets van). Zonder gamescope was er wel een venster van 113x28 (RSLauncher). De AMAuth-oorzaak is een veronderstelling (de stappen stonden in die meting uit). |
| **HauntedMuseumII** | **working** | speelt (eerdere meting op onze kern); de calibratie wordt door de game zelf bijgewerkt | Correctie 2026-10-19 (review 17): OpenParrot was een map-koppeling naar de NAS en de calibratie (HM20/21.CFG/.CRC, HM2bk1/2.dat) werd daardoor heen geschreven -- de start van 10:50 zette zes bestanden in de schrijflaag. OpenParrot staat nu in ViewWritable; de nieuwste calibratie van 10:50 is in de lokale kopie gezet. Bewezen met een echte start: OpenParrot een echte map, de HM2-bestanden erin, 0 nieuwe bestanden of whiteouts in de schrijflaag (6/0 voor en na), NAS ongemoeid |
| **AfterDark** | parked | start op onze kern, foto `docs/screenshots/afterdark-dongle-failed-2026-10-19.png` | de game meldt zelf "Init Dongle Failed !" |
| **PigsMightFly** | parked | de game loopt op onze kern (gamescope, Shell.exe), de foto is het bureaublad | veronderstelling: zonder aangesloten en gekoppelde PS Move komt er geen venster; bevestiging zou een start met Move zijn, vijf keer |
| **3DCosplayMahjong** | parked | -- | de game staat niet op deze machine. Het profiel is NIET klaar om te proberen: het mist Mode = direct, [Loader] Core = ravengun en [Identify], en die zijn zonder bestanden niet toe te voegen |
| **AliensArmageddon** | parked | -- | de NAS-map is leeg. Openstaande punten staan in het profiel: de HASP-controle, het richten met de IR-camera en de schakelaar-id's |



## Mario Kart DX: het zwarte scherm, tweede ronde

### (1) Waar wacht het? (19:00, PID 1775676, 90 s na de start)

- 75 draden. Veel daarvan zijn van **DXVK** (`dxvk-frame`, `dxvk-queue`, `dxvk-submit`,
  `dxvk-descriptor`, `dxvk-cs`, twaalf `dxvk-shader-*`): de D3D9-apparaat is dus aangemaakt en DXVK
  staat volledig op.
- De **hoofddraad** zit niet vast: drie metingen met 6 s ertussen tonen hem steeds in
  `peek_message` -> `NtUserPeekMessage` -> `wine_server_call` (soms via `X11DRV_ClipCursor`), dus in
  de **berichtenlus van wine**. Het is geen wacht op de JVS, geen pijp, geen mutex en geen slaplus.
- De **dxvk-frame**-draad staat in `NtWaitForAlertByThreadId`: hij wacht op een frame. Er is dus
  **geen enkele Present**.
- **Geen enkele draad** voert code uit de spelimage uit: in de volledige `thread apply all bt` komt
  geen frame uit `0x004xxxxx`. Alle speeldraden zitten in wine-wachtdiensten (epoll/futex).
- `winedbg` kan niet aanhaken ("Can't attach process 1b183c: error 87"); de native backtraces hierboven
  komen van `gdb -p`.

### (2) Verschil met de werkende 15-10-meting

De 15-10-meting die met beeld werkte (`direct-MarioKartDX-20261015-002121.plan.json`) verschilt op
twee grote punten en acht omgevingsvariabelen:

| punt | 15-10 (werkte) | 19-10 (zwart) |
|---|---|---|
| gamescope | **aan** (`/usr/bin/gamescope -W 1920 -H 1080 -S fit -f --backend wayland -- ...`) | **uit** (`Gamescope = false` in het profiel) |
| kern | TeknoParrot (`runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot.dll`, `core_source: runtime`) | de onze (`openparrot/current/OpenParrotWin32/OpenParrot.dll`, `core_source: ravengun`) |
| omgeving | alleen `LIBVA_DRIVER_NAME`, `SANSO_MODE_FILE` | daar **acht** sleutels bij: `PULSE_PROP`, `RAVENGUN_PATCHMEM_FILE=patchmem.txt`, `RG_CAMERA_EXPOSURE=photo`, `RG_DRIVE_FIXUP=F`, `RG_JVS_COMPORT=COM3`, `RG_LOADER_CORE=ravengun`, `RG_MOD=english`, `RG_SHAREK_FAKE=1` |

De drop-ins verschillen ook (wij zetten rg-compat, ravengun-patchmem32 en rg-rhio in de view), maar
die horen bij onze eigen route en staan los van het beeld.

**Eerste proef: gamescope aan** (`direct-MarioKartDX-20261019-190049.log`). Gamescope start wel (67
regels), maar de game sterft met `X Error of failed request: BadWindow ... X_UnmapWindow` en
"gamescope: Primary child shut down!", waarna de lader stopt. Gamescope aan maakt het in deze ENE start erger (hypothese, geen bewijs: de run eindigde in een X-fout voordat er
ets te zien was); de instelling is daarna teruggezet op `false`.










### De USB-dongel: SetupAPI nagebootst, het spel opent hem (22:01)

Het spel zoekt de dongel via klasse `{3abf6f2d-71c4-462a-8a92-1e6861e6af27}` (trace met
`RG_DONGLE=trace`). In `tools/rg-mfcam/dongle_trace.h` staan nu de hooks op
`SetupDiGetClassDevsW`, `SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailW`,
`CreateFileW` en `DeviceIoControl`. Met `RG_DONGLE=fake`:

```
dongel: SetupDiGetClassDevsW {3abf6f2d-71c4-462a-8a92-1e6861e6af27}
dongel: eigen apparaat in de lijst gezet
dongel: SetupDiEnumDeviceInterfaces index 0 -> eigen dongel
dongel: apparaatpad \\?\rg-dongle#vid_0000&pid_0000#1
dongel: het spel opent de dongel -> eigen pijp
dongel: DeviceIoControl code 0x00220424 in 6 uit 6 (invoer aanwezig)
```

Het spel **opent onze dongel dus** en stuurt zijn eerste opdracht. Ons antwoord (zes nullen) wordt
daarna afgekeurd en het spel sluit af; dat antwoord is het laatste stukje.

### De USB-dongel is `muchacd` -- en onze eigen webinterface houdt zijn poort bezet (21:00)

Het spel zoekt zijn dongel niet via HID/SetupAPI (die imports heeft het niet); het **start
`AMCUS\MuchaBin\muchacd.exe`** op, de Mucha-dienst, volgens `AMCUS/AMConfig.ini`:
`syscfg-daemon_exe=.\MuchaBin\muchacd.exe`, `syscfg-daemon_pidfile=muchacd.pid`,
`syscfg-daemon_listen=tcp:0.0.0.0:8765`. Zijn logboek (`muchacd.log`) zegt:

```
INFO  MUCHA Daemon ident: 2.1-release-0088-9e7a6a10
INFO  Started pid 1040
INFO  SVM: Service started
ERROR SVM: muchaut_nipc_listen() failed: tcp:0.0.0.0:8765: MUCHA_ERROR_SYSTEM_NET
INFO  SVM: Service terminated
INFO  Daemon terminated, code 1
```

**Poort 8765 wordt bezet door onze eigen `ravengund`**: `ss -ltnp` geeft
`0.0.0.0:8765 users:(("ravengund",pid=...))` -- dat is de webinterface (`--web-port`, standaard
8765). De Mucha-dienst kan dus niet luisteren, sterft, en het spel meldt E19-10 "USB-dongel ontbreekt".
Dat verklaart ook waarom dit nooit eerder opviel: met de TP-kern start dezelfde dienst, maar daar was
onze daemon nog niet met de webinterface op 8765 bezig.

**Proef**: de daemon met `--web-port 18765` gestart -> 8765 is vrij, MK start met 6.334 JVS-pakketten
en een niet-zwarte opname (760.915 bytes), maar `muchacd` startte in die run nog niet vanzelf en met de
hand gestart stopt hij stil (verkeerde aanroep/cwd). Dat is de volgende stap.

### DE OPLOSSING: RG_WINDOWED=1 (20:08), vijf starts

Review 21 van de leiding vond het: de 18-10-metingen op ONZE kern (`direct-MarioKartDX-20261018-130004`
en `-133540`) hadden `Presenter: Actual swapchain 1360x768` en geen "Setting display mode", met
`RG_WINDOWED=1` in de omgeving. Die sleutel is nooit in de repo beland en was op 18-10 verdwenen.
Zonder `RG_WINDOWED` neemt DXUT de volledig-scherm-route ("Setting display mode" twee keer, geen
Presenter) -- precies de zwarte runs.

De `Env` van `patches/MarioKartDX/dropin/dropin.ini` is nu de 18-10-set:
`RG_JVS_COMPORT=COM3;RG_STR_COMPORT=COM1;RG_WINDOWED=1;RG_BNGRW=fake;RG_SHAREK_FAKE=1;RG_DRIVE_FIXUP=F;RAVENGUN_PATCHMEM_FILE=patchmem.txt`.

**Vijf starts via de daemon, op het logboek beoordeeld:**

| start | Presenter | Setting display mode | JVS-pipe verbonden | packets |
|---|---|---|---|---|
| 1 | 1 | 0 | 1 | 4008 |
| 2 | 1 | 0 | 1 | 1881 |
| 3 | 1 | 0 | 1 | 3988 |
| 4 | 1 | 0 | 1 | 3969 |
| 5 | 1 | 0 | 1 | 3902 |

Vijf van vijf dus. De TV-opname van de eerste start (`mk-win-3`, t=90 s) laat het venster van 1360x768
met het bureaublad eromheen zien -- het spel **tekent** -- met daarin het volgende foutscherm:
"Please call an attendant", "E19-10 USBドングルエラー1", "USBドングルがありません" (er is geen
USB-dongel). Dat is dus een **nieuw, verderop** probleem, niet meer het zwarte scherm.

### De STR-bord-nabootsing bleek het ontbrekende stuk (20:01)

De drop-in `rg-mfcam.dll` heeft al een nagebootst **STR-bord** (`str_board.h`, `RG_STR_COMPORT=COM1`),
maar die stond niet in de `Env` van `dropin.ini`. Toen ik hem aanzette en de DLL opnieuw bouwde, bleek
in de logboekregel: "STR-bord: 6997 statusmeldingen, 0 opdrachtbytes, 0 antwoorden" -- het bord is dus
actief. **Het foutscherm verdween**: de opname ging van 10.669 bytes (de rode banner) naar 9.790 bytes
(een effen zwart laadscherm). Daarna kwam de fout terug: de volgende controle is de **JVS/I-O**, en die
krijgt nog steeds **0 pakketten** (de brug blijft op 0 packets, 0 schakelaarpolls staan). Het spel opent
COM3 dus nog niet, ook al meldt de drop-in "JVS-omleiding aan: COM3 -> \\.\pipe\TeknoParrot_JVS".

Met de TP-kern loopt dezelfde brug wel vol (134/2.309/2.264 packets), dus het verschil zit in het pad
achter de COM3-open, niet in de brug zelf.

### VERGELIJKING (geen route): de TeknoParrot-kern, 19:47-19:52

Jeroen heeft de vergelijking met TeknoParrot expliciet toegestaan (het blijft bij een VERGELIJKING; de
route blijft de onze). De TP-map stond als `TeknoParrotUi-2.0.0.130-linux-x64.verborgen` in de runtime;
die is voor deze proef teruggezet en daarna weer verborgen. Instellingen als in de werkende
15-10-meting: `[Loader] Core = teknoparrot` en `Gamescope = true`.

**Drie starts, alle drie goed:**

| run | status | JVS-brug |
|---|---|---|
| 1 (`direct-MarioKartDX-*`, core_source runtime) | De game draait | **134 packets**, 1x identificatie, 127 schakelaarpolls, 126 analoogpolls |
| 2 | De game draait | **2.309 packets** |
| 3 | De game draait | **2.264 packets** |

De TV-opname (`docs/screenshots/mariokartdx-tp-vergelijking-io-ok-2026-10-19.png`, 300.649 bytes)
toont het **opstartcontrolescherm van MK zelf**: "マリオカート アーケードグランプリDX 基板立ち上げ",
met drive-bord-ID **1**, serienummer **271013-020000**, "**I/Oチェック OK JV I/O connected**",
ナムカム(カメラ)チェック OK, ステアリングチェック 反力OFF, ICカード OK, 認証チェック OK,
ゲームサーバー接続チェック チェック中. Er wordt dus beeld getekend en het bord is **verbonden**.

**Onze kern, zelfde moment**: "STR PCB 通信エラー" op het scherm en **0 packets** op de brug.

### Wat de TP-kern precies doet (statisch + runtime)

- **Statisch**: `GameProfiles/MKDX118.xml` vraagt `EmulationProfile = NamcoMkdx` en bevat **nul**
  patches. De ondersteuning zit dus volledig in de TP-kern zelf (de `NamcoMkdx`-klasse in
  TeknoParrot.dll), niet in het profiel. `Metadata/MKDX118.json` is alleen een beschrijving.
- **Runtime**: met de TP-kern loopt het aantal pakketten op de JVS-brug van 0 naar duizenden. De kern
  legt dus de verbinding tussen het spel (dat zijn bord op COM3 opent) en onze brug -- precies wat
  onze eigen drop-in `rg-mfcam` met `RG_JVS_COMPORT=COM3` ("JVS-omleiding aan: COM3 ->
  \\.\pipe\TeknoParrot_JVS") hoort te doen.
- **Ons probleem zit dus in de drop-in, niet in de brug**: de brug werkt (met de TP-kern telt hij
  duizenden pakketten), maar bij onze eigen starts komt er niets binnen. De drop-in schrijft zijn
  logboek (`views/MarioKartDX/rg-mfcam.log`) en dat stopte bij 19:37: de herbouwde `rg-mfcam.dll`
  werd door het spel **niet geladen**. Dat is de eerstvolgende reparatie.

### Doorbraak: het scherm is NIET zwart (19:31)

Ik heb het venster van AMAuthd naar voren gehaald en het scherm opnieuw opgenomen
(`docs/screenshots/mariokartdx-str-pcb-fout-2026-10-19.png`). Wat er dan te zien is, is niet zwart:
achter de console van AMAuthd staat het **eigen foutscherm van het spel**:

- "**... call an attendant.**" (geel op rood),
- "**STR PCB 通信エラー**" -- STR PCB-communicatiefout,
- "**STR PCB との通信に異常が発生しました**" (de communicatie met de STR PCB is verstoord).

De zwarte opnames van 10.669 bytes waren NIET de achtergrond van dat foutscherm: dat scherm werd pas 1,5 minuut
in run `192129` gezien -- de enige run met een Presenter-regel -- terwijl de zwarte runs helemaal nooit
presenteerden. De verklaring "zwart is de achtergrond voor de banner" houdt dus geen stand (correctie review 21);
de echte oorzaak was `RG_WINDOWED` (zie de oplossing hierboven). De oorzaak is nu concreet: het spel krijgt geen
antwoord van zijn **STR PCB** (het stuur-bord). Dat past precies bij de meting uit (3): de brug ziet
**0 pakketten** en het spel verbindt nooit met de `TeknoParrot_JVS`-pijp.

De console van AMAuthd laat ondertussen zien dat die op zijn beurt op een netwerknaam wacht
("hostname=tenporouter.loc", "name resolution failed", "getaddrinfo() err=11001", "DHCP Renew"), wat
voor het beeld zelf niet uitmaakt maar wel verklaart waarom AMAuthd blijft draaien.

**Volgende stap**: uitzoeken waarom de COM3/pipe-omleiding van de drop-in (`rg-mfcam`) het bord niet
bereikt -- het profiel vraagt `Route = jvs`, `Board = MKDX`, `JvsTransport = pipe`, en de brug staat op
`route jvs ... /dev/pts/0`.

### Nog een proef: de resolutie (19:25)

Het profiel vraagt 1360x768 en het venster is 1920x1080. Met `General/ResolutionWidth/Height` op
1920x1080 gezet (alleen de geinstalleerde kopie) is de start herhaald: opname weer **10.669 bytes**
(zwart), geen verschil. Daarna teruggezet.

### De kern van het verschil: de JVS-pijp deed het toen wel (19:23)

Het laatste regeltje van de werkende 15-10-meting is de sleutel:

```
[rg-direct 00:29:32] JVS-pipe: verbinding dicht (2646415 bytes van de game, 49102 pakketten,
                                             1497729 bytes van de daemon terug)
```

Toen praatte het spel dus **49.102 pakketten** met zijn bord. Vandaag zegt de brug bij een lopende
start: "route jvs ... **0 packets, 0x identificatie, 0 schakelaarpolls, 0 analoogpolls**", en het
spel-logboek (`direct-MarioKartDX-20261019-191133.log`) blijft staan op "pipe TeknoParrot_JVS
created, waiting for game...": het spel **verbindt nooit** met de pijp. Het spel komt dus niet eens
aan zijn bordinitialisatie toe.

En de 15-10-meting liep, volgens zijn eigen plan.json, op `core_source: runtime` met
`core_dll: .../TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot.dll` -- dat is de
**TeknoParrot-kern**, niet de onze. Er is dus voor zover de logboeken teruggaan **geen enkele meting
waarin Mario Kart DX op ONZE kern beeld gaf**. Dat is een vraag voor de leiding: of de TP-kern hier
als vergelijkingsbasis mag (alleen om te kijken wat die doet), of dat we het verschil in onze eigen
kern moeten zoeken.

### Proeven met de omgevingsverschillen (19:04-19:21), alle drie geneutraliseerd

Telkens een start via de daemon, ~65 s wachten en een opname met `rg-tv-env.sh --no-hold --
spectacle`. De opname van een zwart scherm is steeds **10.669 bytes** (byte-identiek aan de lege
opname); een desktop-opname is ~1,46 MB.

| proef | wat | opname | logboek | uitslag |
|---|---|---|---|---|
| gamescope aan | `Gamescope = true` | 1.463.488 (bureaublad) | `direct-MarioKartDX-20261019-190049.log` | game sterft op `X Error ... BadWindow (X_UnmapWindow)`, gamescope "Primary child shut down" |
| `RG_SHAREK_FAKE=0` | in `dropin.ini` | 10.669 (zwart) | `direct-MarioKartDX-20261019-190755.log` | geen crash, geen beeld; de 15-s-toegangsfout uit de oude notitie treedt met de huidige lader niet op |
| `RG_DRIVE_FIXUP` weg | in `dropin.ini` | 10.669 (zwart) | `direct-MarioKartDX-20261019-190930.log` | geen verschil, maar in `191133` waren DRIVE_FIXUP en COMPORT samen veranderd: geen zuivere proef |
| `RG_JVS_COMPORT` weg | in `dropin.ini` | 10.669 (zwart) | (zelfde sessie) | geen verschil; de brug blijft **0 packets, 0 schakelaarpolls** |

In alle gevallen eindigt het spel-logboek op dezelfde regel: de D3D9-aanmaak
(`Windowed: true`, `Swap effect: 1`, `DXGI: MakeWindowAssociation: Ignoring flags`) en daarna niets.

**Wat dit uitsluit**: gamescope, ShareK-fake, de drive-fixup en de COM3-route zijn niet de oorzaak.
**Wat overblijft als hoofdverdachte**: het spel komt niet voorbij zijn eerste D3D-presentatie, terwijl
het venster (`mkart3`, 1920x1080, `IsViewable`, `WM_STATE: Normal`) er wel is. Opvallend: het profiel
vraagt 1360x768 (`resolutionwidth/height`) en het venster is 1920x1080; dat is de volgende proef.

### (3) Het bord tijdens de start

`ravengunctl iobridge status` tijdens een lopende start: "IO-brug: route jvs voor MarioKartDX,
dialect jvs, lijn /dev/pts/0 (modemstatus 16), **0 packets**". De brug staat dus, maar er komt geen
enkel pakket van de game binnen.

### (4) Het venster

Met de omgeving van `tools/dev/rg-tv-env.sh` (DISPLAY=:0, Wayland-0) opgezocht. Er is **geen
spelvenster**: het enige venster dat bij de start hoort is het consolevenster van **AMAuthd.exe**
(697x562 op 6,43). Een venster van MK zelf bestaat niet. (Mijn eerste poging met `DISPLAY=:0` uit deze
shell gaf "Authorization required" en zei dus niets.)

## Pass 2 van review 18: ViewLocalFiles, live nagekeken (17:59 en 18:01)

De nieuwe sleutel `[Launch] ViewLocalFiles` maakt bij de start elke BESTANDSkoppeling naar de NAS in die
map lokaal en laat map-koppelingen staan; een naakte `rg-direct --run` weigert (status 2) als er nog zo
een koppeling staat. Allebei via de daemon gestart (nooit een naakte --run).

**SegaGameShell** (`direct-SegaGameShell-20261019-175918.log` en `.plan.json`): het plan heeft
`view_local_files: [.../views/SegaGameShell/shell]`, het logboek zegt "264 bestands-koppelingen naar de
NAS nu lokale kopieen", en in de view staan daarna nog 3 koppelingen: Shell.exe (het plan-exe) plus de
map-koppelingen Graphics en Sounds. De schrijflaag was 157 whiteouts en 2 bestanden voor de start en
daarna nog precies zo (0 nieuw), en `find` op de NAS sinds 06:30 geeft 0. (Eerste poging om 17:57
mislukte omdat de daemon nog de binary van voor de samenvoeging gebruikte: het plan had daar
`view_local_files: null`. Na een herstart van de daemon klopte het.)

**TransformersShadowsRising** (regressieproef, `direct-TransformersShadowsRising-20261019-180143.log`):
de schrijflaag was 105 whiteouts en 60 bestanden voor en is dat erna nog; ShellData, temp en
shell/variables zijn nu echte mappen, in shell/ staan nog 3 koppelingen (het plan-exe plus de
map-koppelingen), het logboek heeft de "lokale bestanden"-regel en `find` op de NAS sinds 06:30 geeft 0.
De attract kwam in beeld: `docs/screenshots/transformersshadowsrising-attract-pass2-2026-10-19.png`
(2.203.780 bytes, "PRESS START BUTTON / FREE PLAY"), dus de game werkt nog.

**HauntedMuseumII** (R2): staat een schrijfmap in DEZE start nog als NAS-koppeling, dan wint het
zaaigoed van de NAS-kopie. Een verse view krijgt daardoor de meegeleverde calibratie in plaats van de
12-10-versie van de NAS; een map die al echt was houdt wat de game er zelf in schreef.

**Openstaande punten.** MenInBlack en PigsMightFly hebben ook bestands-koppelingen naar de NAS in hun
shell-map. Hun schrijflagen laten daar geen schrijfacties zien, maar een naakte `--run` is er niet
beschermd. Kandidaten voor `ViewLocalFiles`.

Wat deze ronde verder opleverde: de (A)-(G)-lijst met tijdvakken staat verderop; de A/B-proef van de
rg-rhio-varianten is afgerond en zegt nu "in deze proef niet bevestigd" in plaats van "weerlegd", met
de weggegooide starts erbij; de mutex-haken laten het spel niet stoppen (5 tegen 5); de prefixstappen
van Mario Kart DX komen nooit op de NAS uit en de stempel wordt vlak voor het schrijven opnieuw
gecontroleerd; de gouden proef legt SegaGameShell en TNXK0OC nu echt vast (geen fallback meer); en de
hele proefreeks is **97/97 groen**.

Wat er in de laatste ronde nog bij kwam (review 16): **MenInBlack** is apart in MUISMODUS gemeten
(munt en START via het virtuele toetsenbord, de missie begint, score 0024/tickets 001) -- de oude
meting blijft staan voor de muisknoppen; **LostLandAdventure** is opnieuw gemeten met gamescope ECHT
aan (de sleutel stond in [Launch] en hoort in [Display]): het beeld bleef zwart en of er een venster
was is NIET vast te stellen (zie de correctie verderop); **SegaGameShell**: alleen shell/variables is
lokaal; de logbestanden in shell/ zijn nog steeds bestands-koppelingen, en Wine VERWIJDERT DOOR die
koppelingen heen (alleen de schrijflaag van bwrap beschermt de NAS; de leiding repareert dit in pass 2);
**TNXK0OC** heeft gs2.ini en
mxRingwide.ini nu als ViewCopies in het profiel, met een Sega-JVS-profiel in plaats van een
Taito-profiel; de stempelronde van de leiding is samengevoegd en live nagelopen (eerste start: vier
keer "klaar (status 0)", de wineserver -w-regel, de stempelregel, 0 weigert; tweede start: vier keer
"al gedaan"); en in dat gebied zijn drie kleine punten gerepareerd -- het wachten op wineserver -w
kijkt nu naar een stopsignaal, een foutstatus van wineserver -w staat in het logboek, en een
betrekkelijke prefix wordt bij de stempel geweigerd (alle drie met een proef die omvalt als je de
clausule weghaalt).

# Stand 2026-10-19 — de volledige lijst (A)-(G) met tijdvakken

Deze lijst staat bewust bovenaan: hij is eerder in een bericht genoemd en daarna zoekgeraakt. Per punt
staat wat het is, hoeveel tijd het had, en waar het nu staat. Alle tijdvakken zijn van de leiding.

| punt | wat | tijdvak | stand |
|---|---|---|---|
| **(A)** | De schrijflaag (writelayer): mappen waar de game in schrijft worden echte mappen in de view, nooit een koppeling naar de NAS | 90 min | **klaar** en op het echte pad bewezen (8819695 en verder) |
| **(B)** | De hele proefreeks (`ctest`) groen | 60 min | **klaar**: 94/94, en later uitgebreid met `ravengun_markers` en `ravengun_prefix_setup` |
| **(C)** | De open punten in de RobinHood I/O-kant (`rg-rhio`) | 90 min | **klaar**: C1-C5, met de bordlezers als bewijs; C4b/C4c zijn teruggedraaid |
| **(D)** | Robin Hood: wie houdt de sluis, waar wacht de hoofddraad op, en de mutex-haken | 90 min | **laatste 30 min** in deze ronde: mutex-hermeting (5 met haken tegen 5 zonder), daarna parkeren met een notitie |
| **(E)** | Coopers 9 op onze eigen lader krijgen | 60 min | **geparkeerd**: de crash is van het spel zelf (access violation op 0x4fd8aed0, ook zonder onze kern); de beveiligingslaag is de volgende stap |
| **(F)** | Mario Kart DX: per-prefix IAuth automatiseren en het zwarte scherm | 90 min | **klaar op de automatisering** ([Launch] PrefixSetup + SecondBare, AMAuthd blijft draaien, WritableConfig.ini komt in de view); het zwarte scherm is gemeten maar niet opgelost -- de renderkant is een eigen ronde |
| **(G)** | Zes ongeteste profielen, elk 30 min: HOTD4SP, MenInBlack, SegaGameShell, 3DCosplayMahjong, AliensArmageddon, TNXK0O~C. Daarna, als er tijd is: AfterDark, PigsMightFly, LostLandAdventure | 6 x 30 min | **deze ronde**: per spel starten op onze eigen lader en kern (nooit TeknoParrot, nooit op de NAS schrijven), 1 minuut attract met een foto als bewijs, de stand eerlijk in de game.ini, een commit per spel. Vastgelopen na 30 min = parkeren met een notitie en door |

Twee dingen die voor elk punt gelden: een conclusie van de vorm "variant X laat het spel stoppen"
vraagt minstens vijf starts per variant tegen een basislijn, en wat met één meting of met een
afleiding is opgeschreven heet een veronderstelling met de meting erbij die hem zou bevestigen.

Besluiten van de eigenaar (ochtend): privégegevens blijven in de release-tarball (geen `--public`),
mappen waar een game in schrijft horen in de **schrijflaag** (nooit een koppeling naar de NAS), er
komen geen dongle-dumps of kast-logs (zelf uitzoeken met statische analyse en meten), `iauthdll`
registreren en `PreprocessAmAuth` per prefix automatiseren, en de opruimactie van de oude worktrees,
buildah-mounts en `claude/pass2-*`-takken doet de leiding zelf.

## (A) De schrijflaag — klaar (8819695)

* Nieuwe profielsleutel **`[Launch] ViewWritable`** (komma-gescheiden, relatief aan de **wortel van de
  view** = de werkmap van de game, niet de map van de exe — Robin Hood start uit de view en schrijft
  in `log/` naast `data/`).
* Bij het bouwen van de view wordt zo'n map een **echte map met een kopie** van de NAS-inhoud; staat
  er al een echte map, dan blijft die staan (dat is de stand van de game). Er wordt nooit naar de NAS
  geschreven.
* **Harde controle:** komt een opgegeven schrijfmap na het plaatsen nog op de NAS uit
  (`readlink -f`), dan **weigert de engine de start** met een duidelijke fout. `..` en absolute paden
  worden al bij het lezen van het profiel geweigerd.
* Ingesteld: **Mario Kart `AMCUS`** (schrijft `WritableConfig.ini`) en **Robin Hood `log`** (schrijft
  `PR0/PR1.CFG` en `.CRC`).
* Proef: `ravengun_direct_launch_tests` — 460 controles (echte map, kopie, NAS ongemoeid na schrijven
  in de view, tweede keer niets doen, weigeren als het pad op de NAS uitkomt, weigeren van `../x` en
  `/abs`).
* De bestaande NAS-bestanden (`AMConfig.ini`, `PR0/PR1.CFG`, `.CRC`, `PRbk*.dat`) zijn niet
  verplaatst, gewijzigd of verwijderd. De eerste start na deze wijziging zet de twee mappen om.
* Terloops gevonden en gerepareerd: mijn ronde-9-herschrijving had de **`[Launch]`-kop van Mario
  Kart** weggehaald (de sleutels `Mode`/`StackReserve` stonden onder `[Game]`).

## (B) ctest 94/94 — klaar

De oorzaak van het niet-vinden was diezelfde ontbrekende `[Launch]`-kop: `direct_golden.py` zag het
profiel niet meer als "eigen profiel met `[Launch] Mode = direct`". Met de kop terug vindt de proef
(en `--update`) het profiel weer: **44 profielen**, `tests/golden/direct/RobinHood.json` ververst (in
de proefopstelling wordt Robin Hood nu eerder geweigerd omdat de schrijfmap daar niet in een view
ligt — precies het gedrag dat we willen). **ctest: 94 van 94.**

## (C) De open punten in de RobinHood I/O-kant — klaar

* **Geen spookdata meer:** `ClearCommError` meldt nu **0** zodra het antwoord helemaal gelezen is
  (vlag `g_antwoord_op`), ook bij stand `niks`; `PurgeComm` wist het wachtende antwoord. Zonder dit
  las het spel na een volledig kader opnieuw 7 bytes en begon het rondje opnieuw — een van de
  kandidaten voor de hang.
* **Slot vóór de haken:** `InitializeCriticalSection` gebeurt nu als eerste in `DllMain`.
* **Herprobeer-draad alleen als het echt nodig is:** per import staat nu of hij verplicht is; de
  optionele (PurgeComm, Get/SetCommState, SetCommTimeouts, EscapeCommFunction, SetCommMask,
  WaitCommEvent, GetOverlappedResult) laten de draad niet meer starten en worden als "optioneel"
  gelogd.
* **`OpenBordbeeld`:** herhaalpoging bij elke aanroep, het handvat blijft bewaard (geen lek) en de
  melding komt één keer.
* **Geen `LoadLibrary` in `DllMain`:** de meetdraad van rg-compat wordt nu lazy geladen bij de eerste
  bord-aanraking (`std::call_once`).
* **`RG_RHIO_TOETS`** wordt één keer gelezen, niet per kader.
* **De AMAuth-proefariteit:** de proef gebruikte stdcall met een vaste ariteit, dus de conclusie "het
  spel roept slot 9 aan" is **niet betrouwbaar** en wordt hierbij **ingetrokken**; met de echte
  AMAuthd draait het spel door, dat is de meting die telt.
* De dubbele TC5-noot was al weg.

## Let op: de schrijflaag vuurt (nog) niet op de echte view

De nieuwe `[Launch] ViewWritable` is ingebouwd en de proef bewijst hem (460 controles, via
`apply_direct`), maar op de **echte** view bleef `views/RobinHood/log` een koppeling: met
`rg-direct --plan` staat `view_writable` goed in het plan en de tekst "schrijfmap" zit in de binary,
maar bij `--run` gebeurt er niets. Dat is één open punt met een scherpe volgende stap (de
`--run`-weg van plan naar uitvoering nalopen, of het loggen van de schrijflaag naar de console
zetten). **Stopgap voor vandaag:** de twee mappen zijn met de hand één keer omgezet (een kopie in de
view, de NAS alleen gelezen), dus `views/RobinHood/log` en `views/MarioKartDX/AMCUS` zijn nu echte
mappen en de games schrijven daar lokaal. De NAS-bestanden (PR0/PR1.CFG, .CRC, AMConfig.ini,
PRbk*.dat) zijn niet verplaatst, gewijzigd of verwijderd.

## Nog te doen (op volgorde van de leiding)

* **(C)** de open `rg_rhio`-punten (spookdata in `ClearCommError`, slot vóór de haken,
  herprobeer-draad, `OpenBordbeeld`, geen `LoadLibrary` in `DllMain`, `RG_RHIO_TOETS` één keer lezen,
  de AMAuth-proefariteit en de dubbele TC5-noot).
* **(D)** Robin Hood 90 min: de wachtende hoofddraad volgen (op welk handvat wacht hij, waar wordt dat
  gemaakt, welk pad hoort het te seinen).
* **(D)** Robin Hood 90 min (09:50-10:47): de meetdraad **loopt** nu (markerbestand) en de meting
  wijst uit dat de hoofddraad in `NtWaitForSingleObject` wacht terwijl geen enkele draad in de
  lock-helper zit; de volgende stap is de handvatwaarde. Zie `docs/research/wartran/README.md`.
  De mutex-haken zijn gebouwd; het vermoeden dat ze de game laten stoppen is daarna **niet bevestigd**
  (vijf starts met de haken tegen vijf zonder: alle tien bleven draaien, zie de A/B-proef in de
  wartran-README). Wel bleek het uitbreiden van de meetdraad met stapelwoorden fataal (teruggedraaid;
  de meetdraad is weer precies zoals in de repo). De volgende stap staat in de wartran-README: het handvat van de wacht lezen zonder te
  haken. Er is nu ook een **wachthandvat-lezer** in rg_rhio (draden even stilzetten, `Eip`/`Esp` en
  de stapelwoorden lezen, geen import-haak): de game blijft er goed van draaien, maar de aanroep wordt
  nog niet bereikt -- zie de wartran-README voor de exacte plek. **Eerlijk: I/O CHECKING is nog niet
  voorbij.**
  **Oudere notitie:** de wachtende hoofddraad -- **deels gedaan**: de mutex is gevonden en
  gedocumenteerd (`docs/research/wartran/README.md`), en de valkuilen bij het meten staan er ook
  (controller voor de daemon, `pgrep` die je eigen shell matcht). De meetdraad zelf moet nog
  lopen: `RG_COMPAT_SAMPLE=200` in de drop-in en dan de draad in 0x580fa0-0x5810a0 zoeken.
* **(E)** Coopers 9 60 min: eerst het RobinHood-kaderprotocol, dan de vroege fout op 0x4fd8aed0.
* **(F)** Mario Kart 90 min: `iauthdll` + `PreprocessAmAuth` idempotent automatiseren in de
  prefix-opzet (in de schrijflaag), dan het zwarte beeld (welke IAuth-methodes roept het spel aan).
* **(G)** de nog niet geteste profielen: HOTD4SP, MenInBlack, SegaGameShell, 3DCosplayMahjong,
  AliensArmageddon, TNXK0O~C — 30 min per stuk, 1 min attract + foto + eigen commit, of parkeren met
  een noot. AfterDark, PigsMightFly en LostLandAdventure alleen als er tijd over is.

## (D) Robin Hood: waar de hoofddraad op wacht — eerste uur gedaan

Met statische analyse (objdump) is de wacht van de hoofddraad gevonden: hij wacht met
`WaitForSingleObject(mutex, INFINITE)` op een **wereldwijde mutex** die lazy gemaakt wordt
(`CreateMutexA` + `InterlockedCompareExchange`, samen in één helper op 0x580fa0-0x580fde), met de
bijbehorende `ReleaseMutex` op 0x580ff6. **`SetEvent` wordt in de hele exe nooit aangeroepen** -- dus
"de I/O-draad moet een event seinen" is een dood spoor; de hoofddraad wacht op een **slot dat een
andere draad vasthoudt**.

Dat maakt de volgende stap scherp: welke draad houdt dat slot, en welke van onze haak-aanroepen laat
hem wachten? Elke haak (`ReadFile`, `WriteFile`, `ClearCommError`, `GetCommTimeouts`,
`WaitCommEvent`) moet snel terugkeren. De volledige tabel met sleuven, aanroepen en het patroon staat
in `docs/research/wartran/README.md`; de volgende drie stappen staan daar ook.

## (A) hard gemaakt volgens reviewronde 10

* **A1**: de koppeling gaat pas weg als de kopie **volledig** gelukt is. Er wordt eerst naar
  `<pad>.ravengun-tmp` gekopieerd met `directory_iterator` + `increment(ec)` (geen worp in de
  daemon-draad, die zou `std::terminate` zijn), elke fout wordt gemeld, en pas daarna volgt
  `unlink` + `rename`. Een NAS-hapering laat zo nooit een lege of halve map achter die als spelstand
  bewaard blijft.
* **A2**: de uitslag van `unlink` en `rename` wordt gecontroleerd; vóór elke aanraking wordt de
  **ouder** getest (`on_nas(realpath_(dirname(pad)))`) zodat een koppeling in een koppelingsmap niet
  alsnog op de NAS schrijft.
* **A3**: ook als het pad niet bestaat wordt de ouder gecontroleerd, en een **case-variant** die naar
  de NAS wijst (Wine is niet hoofdlettergevoelig) wordt behandeld.
* **A5**: kinderen die naar de NAS wijzen worden **lokaal gemaakt** (bestand kopiëren, koppeling
  weg, echt bestand terug); een kind dat een NAS-**map** is wordt geweigerd met uitleg.
* **A4**: `rg-direct --run PLAN.json` weigert nu ook een schrijfmap die een koppeling is of op de NAS
  uitkomt (dezelfde controle als de daemon-route).
* **A6**: `refresh_game_view` zet **nooit** een link naar de NAS binnen een schrijfmap (nieuw veld
  `writable_dirs` in `GameViewSpec`, gevuld uit `[Launch] ViewWritable`). `InfinityBladeFX`
  (`SwordGame/Logs`), `BugBusters` (`config`) en `HauntedMuseumII` (`data`) hebben de sleutel nu ook.
* **A7**: `on_nas` kiest bij twee regels met hetzelfde koppelpunt (autofs boven cifs op deze Deck) de
  **laatste**; eerst bleef de autofs-regel staan en werkte het alleen door de `~/mnt`-voorwaarde.
* **A8**: vier nieuwe proeven in `ravengun_direct_launch_tests` (mislukte kopie laat de koppeling
  staan en niets halfs achter; ontbrekend pad onder een koppeling naar de NAS wordt geweigerd; een
  kind dat naar een NAS-bestand wijst wordt lokaal; een mislukte stap laat niets halfs achter) --
  **475 controles, 0 fout**. De gouden vergelijking laat nu `view_writable` zien en **RobinHood staat
  weer op `ok`** (het volle plan), doordat de proefopstelling RobinHood en MarioKartDX in een view
  zet. MarioKartDX blijft daar geweigerd op `[Launch] Mod = english` (de fan-vertaling is een
  download die de proefopstelling niet heeft); de tweede exe wordt in de C++-proeven gedekt.
* **Installatiestap (voor de volgende keer)**: de daemon komt uit **`build/`** (niet `build-app/`;
  die heeft geen `ravengund`-doel). Na een bouw: `cmake --install build --prefix
  ~/.local/opt/ravengun` en daarna `systemctl --user restart ravengund`, anders draait de oude
  engine. `build-app` bouwt hier niet (geen `archive.h`); alleen `ravengund`/`ravengunctl` komen uit
  `build/`.

### OPGELOST: de schrijflaag werkt, en de oorzaak was de controller

De aanleiding was niet de schrijflaag maar de **start**: na een reeks daemon-herstarten was de
muis-geweerinstelling van speler 1 verdwenen, en de daemon bleef bij het starten wachten op een
controller ("Zet de PS Move van speler 1 aan"). Er werd dus **geen game gestart**, en daarom zag ik
geen drop-in in het proces, geen `rg-rhio.log` en geen omgezette map. Met
`ravengunctl mouse-gun --player 1 --mouse ...` er weer in, start de game wel en is de schrijflaag
**bewezen op het echte pad**:

* `views/RobinHood/log` is na een gewone start een **echte map met de volledige kopie**
  (PR0.CFG, PR0.CRC, PR1.CFG, PR1.CRC, PRbk1.dat, PRbk2.dat);
* `ls --full-time` van de NAS-map `Robin Hood/log` is **voor en na identiek**
  (02:09:35 en 10-15, geen enkele regel veranderd);
* de spelstand is daarna teruggezet in de echte map; de NAS is niet aangeraakt.

Daarmee is ook de **runtime-controle van (C)** gedaan: `rg-rhio.log` bestaat weer en de
`cbInQue`-waarden zijn nu 0 (19920 keer, als er niets klaarstaat), 6, 5 en 7 -- precies wat C1 beoogde
(het protocol loopt door in plaats van na frame 1 te sterven).

### (Historisch) de eerste verklaring die niet klopte

De functie is proefondervindelijk goed (475 controles, inclusief de volle `apply_direct`), maar in de
**daemon** wordt `place_view_writable` niet bereikt: met een `fprintf(stderr, ...)` op de eerste regel
(het daemon-stderr gaat naar `logs/ravengund.log`) kwam er bij een gewone start **geen enkele regel**,
terwijl `games.cpp` `L::apply_direct` wel aanroept. Drie routes zijn geprobeerd: `rg-direct --run`
(blijkt volgens de review alleen te *starten*, niet te plaatsen), de daemon met een verse binary (de
geïnstalleerde `ravengund` bevat de nieuwe code, gecontroleerd met `strings`) en instrumentatie in de
functie zelf (geen uitvoer). De volgende stap is dus *vóór* de functie: uitzoeken welke weg de daemon
voor RobinHood werkelijk loopt (`direct_route` → `plan_direct` → `apply_direct`, of de write_layer-
route) en of `pl.view_writable` daar gevuld is. **Stopgap**: `views/RobinHood/log` en
`views/MarioKartDX/AMCUS` zijn met de hand echte mappen met een kopie; de NAS is onaangeroerd
(`ls --full-time` van `Robin Hood/log` is vóór en na de proef identiek: 02:09:35 en 10-15).

## (C) De regressies van reviewronde 10 in de RobinHood-bordkant

* **C1 (hoog)**: `BouwAntwoord` zet `g_antwoord_op` weer op `false` als er een nieuw antwoord klaarstaat.
  Zonder dat meldde `ClearCommError` na het eerste volledige kader voor altijd `cbInQue = 0` en ging
  het bordprotocol na frame 1 dood.
* **C2**: bij stand `niks` staat er niets klaar, dus `g_antwoord_op = true` (cbInQue 0) en draait de
  leeslus niet warm.
* **C3**: de oude `InitializeCriticalSection(&g_bord_cs)` onderaan `DllMain` is weg; het slot wordt nu
  één keer gezet, vóór de haken.
* **C4**: `GetOverlappedResult` leest de teller onder het slot en meldt het aantal bytes van de
  laatste `ReadFile`/`WriteFile` (niet de lengte van het hele kader); `RG_RHIO_EERSTEBYTE` en
  `RG_RHIO_BORD` worden niet meer per frame gelezen maar één keer bij het laden (`LeesOmgeving()`, in
  een globale `char g_bord[64]` met lengtecontrole; de oude `char bord[8]` is weg; dat een `= {0}` het
  spel liet stoppen is niet aangetoond, dus meet het starten met 5+ starts per variant); de kop van
  het bestand noemt alle omgevingsvariabelen en markerbestanden met hun echte betekenis;
  `std::call_once` is vervangen door een `InterlockedCompareExchange`-vlag (geen winpthreads meer);
  `build-rhio.sh` had `-s` weer verloren en stript opnieuw (pass 2): de DLL gaat van 2,37 MB naar
  **299 kB** (het eerder genoemde 197 kB klopte niet voor de DLL in de repo); het logboek is begrensd
  op 20.000 regels (`Log` en `LogVan` samen) met één afsluitende regel, begint bij elke start schoon
  (het vorige blijft als `rg-rhio.vorige.log`) en `ClearCommError` logt een wijziging van cbInQue plus een levensteken per ~1 s.
* **C1 (aanvulling, pass 2)**: `g_antwoord_op` wordt nu in `rg_WriteFile` zelf teruggezet, ook bij een
  frame korter dan 7 bytes (`BouwAntwoord` keert dan eerder terug).
* **C5**: de conclusie over "slot 9" is in `docs/research/namco/README.md` en
  `docs/status-2026-10-19-nacht.md` ingetrokken (de proef gebruikte stdcall met een vaste ariteit).

**Eerlijk punt:** de *runtime*-controle van C1 kon ik niet doen omdat `rg-rhio.log` niet verschijnt —
het plan bevat `RG_RHIO_LOG` wel (nagekeken in `rg-direct --plan`) en de DLL staat in de view, maar er
komt geen logboek. Dat hoort bij hetzelfde raadsel als de schrijflaag hierboven (de daemon-route), en
het is de eerste vraag voor de volgende ronde.

## Review 11: mijn C-ronde liet Robin Hood meteen stoppen (gevonden en verholpen)

Tijdens (D) bleek de game na de C-ronde niet meer te starten (de lader stopte met 0, de game sloot
zich meteen, geen scherm, geen spelstand). Bisect met een keurscript (`/tmp/rh-keur.sh`: bron bouwen,
naar de drop-in EN de view kopieren, starten, foto) gaf:

* pre-C bron: **draait** (foto 23 kB = I/O CHECKING);
* C1+C2+C3: draait; +C4a: draait; +C4b of C4c: **stopt**; C1+C2+C3+C4a+C4e+C4f: draait.

Twee wijzigingen waren fataal, allebei rond de 8-byte buffer `bord` in `BouwAntwoord`:

1. **C4b** `char bord[8] = {0};` in plaats van `char bord[8];`
2. **C4c** dezelfde buffer via een `static const bool met_bord = []{ char v[8] = {0}; ... }();`

Elke vorm die die buffer anders neerzet laat de game stoppen. Dat wijst op een **latente fout elders
in het bestand** (de framelayout van `BouwAntwoord` verandert en een bestaande overschrijding wordt
fataal); de eerlijke conclusie is dat C4b en C4c hier niet veilig te doen zijn zonder die fout eerst
te vinden. Ze zijn daarom **teruggedraaid** naar de oude vorm, met een waarschuwende opmerking erbij.
De rest van de C-ronde (C1, C2, C3, C4a, C4e, C4f) is gebleven en werkt: het logboek stopt nu bij
20.000 regels (722 kB in plaats van 20 MB) en het protocol loopt door.

Daarnaast bleek het installeren van de **mutex-haken** (WaitForSingleObject/ReleaseMutex/CreateMutexA)
veel logboekruimte te kosten: ze worden door de CRT al heel vroeg en heel vaak aangeroepen. Het
vermoeden dat ze een start fataal maken is later **niet bevestigd** (vijf starts met de haken tegen
vijf zonder: alle tien bleven draaien). Ze zijn **opt-in** met `rg-rhio-mutex.aan` naast de DLL of
`RG_RHIO_MUTEX=1`.

**Les voor het keuren**: de drop-in in `~/.local/opt/ravengun/patches/...` is de bron die de engine
materialiseert; een herbouw van alleen `patches/...` in de repo verandert de start niet. Alle
keurproeven hierboven kopieren naar **beide**.

## (E) Coopers 9 -- eerste minuten (10:32-10:35)

De start gaat via onze eigen route (kern `openparrot/current/OpenParrotWin32/OpenParrot.dll`, de lader
stopt met 0, `rg-direct` volgt de game), maar **de game sluit zich meteen**: geen scherm, geen
spelstand. Dat is hetzelfde beeld als bij Robin Hood toen het misging, dus de volgende stap is
dezelfde: kijken wat de game zelf vindt (dongo-controle, config, `[Config]`-bestanden in de view) en
het drop-in-logboek van die start lezen (`direct-Coopers9-20261019-103208.log`). De box van (E) is
hiermee begonnen maar nog lang niet af; er is geen minuut attract geweest.

**Stand van de lijst op 10:55**: (A) klaar en bewezen, (B) ctest 94/94, (C) klaar en op het echte pad
gecontroleerd, (D) meetdraad loopt en de meting staat er (I/O CHECKING nog niet voorbij, volgende stap
in de wartran-README), (E) is opnieuw gemeten en **geparkeerd** met een precieze notitie (de volgende stap vraagt de bron van
de kern of een debugger; zie de wartran-README), (F) en (G) nog niets.

## (F) Mario Kart: per-prefix IAuth -- recept gevonden en de volgende blokkade benoemd (10:42-10:55)

Wat nu **gemeten en werkend** is (alles in de schrijflaag; de NAS is met `find -newermt` gecontroleerd
en onaangeroerd):

1. `PreprocessAmAuth.exe -wait`, gedraaid **vanuit de view-wortel**, zegt
   `Checking.ErrorCode : 0 / outPath : WritableConfig.ini / Ok!` en schrijft het bestand in
   `views/MarioKartDX/TeknoParrot/E/` (een echte map in de view).
2. Dat bestand moet naar **`views/MarioKartDX/AMCUS/`** (de map van AMAuthd, ook een echte map in de
   schrijflaag): daar zoekt AMAuthd het. Zonder dat bestand zegt hij
   `ERROR: file not found: WritableConfig.ini` en doet niets; met het bestand **blijft hij draaien**
   (direct onder wine gemeten, vier processen, geen fout).
3. De klasse staat in de prefix: `HKLM\Software\Classes\CLSID\{4603BB03-...}` met `LocalServer32`
   (nagekeken in `system.reg`); registreren kan met `wine regsvr32.exe AMCUS/iauthdll.dll`.

Wat **nog blokkeert**: met onze kern erin stopt `AMAuthd.exe` meteen en meldt het laderlogboek
`Failed to Load DLL! (Error 4)`; de loper breekt de start dan af ("start 1/2 stopte voordat start 2/2
begon", exitcode 7) en het spel komt nooit in beeld. De tweede exe is een gewone server die **niet met
onze kern** geladen hoort te worden; de motor heeft geen sleutel om per exe een andere kern (of geen
kern) te kiezen. Dat is de volgende stap -- en die vraagt een motorwijziging, dus die hoort bij de
leiding of in een volgende ronde. Automatiseren van stap 1 en 2 hoort daar meteen bij: de motor kent
geen "een keer per prefix"-haak.

## Pass 2 samengevoegd (10:47-10:55) en de drie echte-pad-controles

`git merge --no-ff origin/claude/pass2-writelayer` (7 commits, kop 0beb643) is schoon samengevoegd
(geen conflicten, geen overlap met mijn rg_rhio/profielwerk), `build` is opnieuw gebouwd, de motor is
geinstalleerd naar `~/.local/opt/ravengun` en `ravengund` is herstart (met daarna opnieuw de
muis-geweerinstelling van speler 1, anders wacht de daemon op een controller). **ctest: 94/94.**

`build-app` bouwt hier **niet**: `src/app/core/ArchiveExtract.cpp` vraagt `archive.h` en libarchive
staat niet op deze machine (geen `pkg-config libarchive`, geen `/usr/include/archive.h`). Dat is een
omgevingsgat, geen codeprobleem; de daemon komt uit `build` en draait dus de nieuwe motor.

Drie controles op het echte pad, elk met een foto:

| spel | resultaat |
|---|---|
| HauntedMuseumII | **start en draait**: foto toont het attractiescherm "WELCOME TO HAUNTED LAND / INSERT COINS / CREDIT 0" |
| InfinityBladeFX | **start en draait** ("0 / 4 CREDITS"); `SwordGame/Logs` (8 bestanden), `SwordGame/Config` (30) en `SwordGame/SaveData` (1) zijn **echte mappen**, geen koppelingen |
| BugBusters | **start en draait**: foto toont het attractiescherm (explosie/level); `config/` is een echte map |

De NAS is bij InfinityBladeFX **voor en na identiek**: `ls --full-time` van `SwordGame/Logs`,
`SwordGame/Config` en `SwordGame/SaveData` geeft dezelfde md5 (8d86580996ab27f2, 78d59f3007f8d07f,
5e72cde272b6466a) en `find <NAS> -newermt '-8 minutes'` vindt **niets**. Niets weigert.

## (F) automatisering: [Launch] PrefixSetup en [Launch] SecondBare (11:00-11:35)

Jeroen wil de IAuth-stappen **automatisch**, dus het zijn nu motorfuncties:

* **`[Launch] PrefixSetup`** -- stappen die eenmalig per prefix gebeuren, met per stap een
  stempelbestand in `<prefix>/.ravengun-prefixsetup/<hash>.done` (idempotent; verandert de regel, dan
  verandert de hash en loopt de stap opnieuw). Vorm, met `|` tussen de stappen (geen `;`, dat begint
  in game.ini een opmerking):
  `run <map> <bestand> [args]` (via de wine van de lader, zonder WINEARCH) en
  `copy <van> <naar>`; paden ten opzichte van de view-wortel, `prefix:<pad>` wijst in de prefix zelf.
  Een kale naam zonder schuine streep is een programma uit de prefix (regsvr32.exe). `..` en absolute
  paden worden geweigerd. Een stap die blijft hangen wordt na 180 s afgebroken en laat de start
  mislukken (regsvr32 zonder /s wacht op een venster).
* **`[Launch] SecondBare = true`** -- de tweede exe start met **alleen wine**, zonder onze lader en
  kern. Nodig voor AMAuthd: met onze kern stopt hij meteen met "Failed to Load DLL! (Error 4)".

Het profiel van Mario Kart DX gebruikt het nu:
`PrefixSetup = run . PreprocessAmAuth.exe -wait | copy prefix:drive_c/windows/WritableConfig.ini WritableConfig.ini | copy prefix:drive_c/windows/WritableConfig.ini AMCUS/WritableConfig.ini | run AMCUS regsvr32.exe /s iauthdll.dll`
en `SecondBare = true`.

**Gemeten op het echte pad** (view schoon, stempels weg, daarna een gewone start): de vier stappen
lopen, er staan vier stempels, `WritableConfig.ini` (226 bytes) staat **in de view** (wortel en
`AMCUS/`, allebei echte mappen in de schrijflaag) en **AMAuthd blijft draaien** -- hij wordt pas door
de loper gestopt als het spel stopt. Zonder `SecondBare` stopte hij meteen. De NAS is niet aangeraakt.

Wat er daarna gebeurt: het spel start wel (lader + kern, `start 2/2`), maar sluit na een paar seconden
met een **X11-fout** in gamescope ("Serial number of failed request: 156 / Current serial number in
output stream: 156", daarna "Primary child shut down"). Dat is de zwarte-scherm-stap van (F) en de
volgende om uit te zoeken.

Proeven: `ravengun_direct_launch_tests` heeft er twee blokken bij (SecondBare: de tweede start heeft
alleen wine, het spel houdt lader en kern, plus de JSON-rondgang; PrefixSetup: de drie stappen, de
stempels, `prefix:` en vier weigeringen) -- **568 controles, 0 fout** -- en **ctest 94/94**.

### Het zwarte scherm van Mario Kart: gemeten, en waar het nu op wacht

Met de automatisering op zijn plaats is het spel **echt gaan lopen**:

* de vier prefixstappen lopen, `WritableConfig.ini` staat in de view, **AMAuthd blijft draaien**;
* `start 1/2` (AMAuthd) en `start 2/2` (het spel) lopen; het spel blijft in leven;
* in deze sessie kan gamescope niet starten ("Failed to bind socket @/tmp/.X11-unix/X0: Address already
  in use" -- de Xwayland van het bureaublad heeft X0), en dan sluit het spel met een X11-fout
  (`BadWindow`, `X_UnmapWindow`, serienummer 156). Met gamescope uit blijft het spel draaien;
* **het scherm is dan helemaal zwart** (foto 10.669 bytes, egaal zwart) en het eigen logboek van het
  spel eindigt na het aanmaken van het D3D9-apparaat:
  `Format: D3D9Format::A8R8G8B8`, `Windowed: true`, `Swap effect: 1`, `DXGI: MakeWindowAssociation:
  Ignoring flags` -- daarna niets meer. Het spel leeft dus, maar er wordt niets getoond.

**VERONDERSTELLING (nog niet gemeten)**: DXVK maakt voor het zwarte scherm geen verschil. Wat we
weten is dat DXVK aanstaat (`[Launch] Dxvk = true`, `WINEDLLOVERRIDES` d3d8/d3d9/d3d10core/d3d11/dxgi
= n, de DXVK-DLL's in de view) en dat het scherm met die instelling zwart blijft. Er is **geen enkele
start zonder DXVK** opgeschreven, dus de uitspraak "het maakt geen verschil" is niet gedekt. WAT DAT
ZOU BEVESTIGEN: minstens vijf starts met `Dxvk = false` in hetzelfde profiel, en dan vergelijken met
de vijf starts met DXVK aan (zelfde foto-uitkomst, zelfde logboek-einde). De volgende stap aan de
renderkant blijft: `DXVK_HUD`/`DXVK_LOG_LEVEL=debug`, `WINEDEBUG=+d3d`, of kijken wat de camera-drop-in
(`rg-mfcam.dll`, die het spel ook laadt) met de presentatie doet.

### Waar het zwarte scherm vandaan komt: de ShareK-fake, slot 7

Het logboek van onze eigen camera-drop-in (`views/MarioKartDX/rg-mfcam.log`, 227 regels) laat zien hoe
ver het spel komt. De laatste regels, in volgorde:

```
camerabestand nu: 640x480 met beeld
geheugen-motor: ravengun-patchmem32.dll geladen (lijst patchmem.txt)
CoCreateInstance({4603BB03-058D-43D9-B96F-639BE908C1ED}, ctx 0x4, {045A5150-...}) -> hr 0x00000000
d3dx10: D3DX10CreateEffectFromFileA liet pHResult ongemoeid; nu 0x00000000 (1, 2, 3 keer)
CoCreateInstance({4C9B6DDE-6809-46E6-A278-9B6A97588670}, ctx 0x1, {8BCF1F58-...}) -> hr 0x00000000
ShareK-object: QueryInterface({C2116F7F-CF46-3C68-B187-9D0FE9F70ED0}) -> hetzelfde object
ShareK: CoCreateInstance({CCC44B2A-709A-33A8-A1B0-5DBE25B19C2C}, ctx 0x17, {C2116F7F-...}) -> ...
ShareK-object: slot 7 aangeroepen (beller 00000000)
```

Twee dingen zijn hier de winst van deze ronde:

1. **De IAuth van AMAuthd geeft `hr 0x00000000`** -- de automatisering (`PrefixSetup` met
   PreprocessAmAuth, de kopie naar `AMCUS/` en `regsvr32 /s`) doet dus precies wat de handmatige
   stappen deden. Zonder die stappen gaf dezelfde aanroep niets.
2. Het spel loopt daarna door tot in de **ShareK**-laag (`RG_SHAREK_FAKE=1` in de drop-in):
   `QueryInterface`, `CoCreateInstance({CCC44B2A-...})` en **`slot 7`** (= `StartIpcClient`) worden
   aangeroepen. Die fake **antwoordt daar wel op** -- `tools/rg-mfcam/sharek_fake.h` beschrijft slot 7
   als `StartIpcClient([out] VARIANT_BOOL*)` en de fake zet hem op -1 ("Startup ShareK-Client"), en
   het logboekregeltje `slot 7 aangeroepen (beller 00000000)` is alleen de laatste regel die *onze*
   DLL schrijft. **Correctie op mijn eerste conclusie**: het spel wacht daar niet op een antwoord; het
   gaat daarna zijn eigen weg en roept onze DLL niet meer aan. **VERONDERSTELLING**: het zwarte scherm
   zit daarmee **na** de ShareK-afhandeling. Dat is een gevolgtrekking uit één logboek (het stopt met
   regels schrijven), niet uit een meting van het scherm; de fake zou ook iets kunnen doen dat onze
   DLL niet logt. WAT DAT ZOU BEVESTIGEN: een start met `RG_SHAREK_FAKE=0` (of de fake die het
   ShareK-object weigert) en kijken of het scherm dan wél iets toont, vijf starts.

Stand van de renderkant: het spel maakt zijn D3D9-apparaat (logboek: `Windowed: true`, `Swap effect:
1`), DXVK staat aan met de juiste `WINEDLLOVERRIDES` (d3d8/d3d9/d3d10core/d3d11/dxgi = n), `DXVK_HUD=1`
staat in de omgeving -- en er verschijnt geen HUD en niets anders op het scherm. Met `WINEDEBUG` in de
drop-in-omgeving lukt het niet om de d3d-kanalen aan te zetten (de motor zet zelf `-all`); dat moet via
de `--log-full`-weg van rg-direct of door de motor een `WINEDEBUG`-sleutel te geven.

**Volgende stap voor (F)**: de presentatie-kant. Twee goedkope kandidaten: (a) de motor een
`WINEDEBUG`-sleutel geven zodat `+d3d9,+dxgi` echt doorkomt, en (b) kijken of het spel zijn venster wel
presenteert maar niets tekent (het spel gebruikt `RG_JVS_COMPORT=COM3`; **VERONDERSTELLING**: net als
bij Robin Hood kan het op bordgegevens wachten voordat het zijn eerste frame tekent -- dat is een
vermoeden naar analogie en nog niet gemeten voor Mario Kart DX).

## Review 12: vier punten in het (D)-gereedschap van rg-rhio (12:00-12:15)

De vier punten zijn doorgevoerd in `tools/rg-rhio/rg_rhio.cpp`; de DLL bouwt schoon met de
mingw-image (304.128 bytes tegen 299.008 voor de ongewijzigde versie).

**(1) `LeesWachthandvatten` logde en maakte geheugen aan terwijl een speldraad stil stond.** Dat is
een echte doodloop-kandidaat: `Log` pakt de sluis van het `FILE*`, `std::string` de heap en
`GetModuleHandleExA` de loader-sluis. Als de stilgezette draad net een van die drie vasthield, stond
alles stil. Nu gebeurt tijdens de stilstand **alleen** `GetThreadContext` en het lezen van de vier
stapelwoorden in een **lokale array** (`ReadProcessMemory` op het eigen proces, met een grenscheck via
`VirtualQuery`, zodat een ongeldig adres een foutcode geeft in plaats van een uitzondering). Elke
draad wordt daarna meteen weer losgelaten en pas **na** de lus worden de regels gemaakt en
weggeschreven. Verder: de tempo-poort is nu atomair (`InterlockedCompareExchange`) en onze eigen
draden worden overgeslagen -- de start van een draad wordt opgevraagd met
`NtQueryInformationThread(ThreadQuerySetWin32StartAddress)` en vergeleken met de omvang van
`rg-compat.dll` en `rg-rhio.dll` (buiten de stilstand om, want `GetModuleHandleA` pakt de
loader-sluis). Een controle na het bouwen laat zien dat er tussen `SuspendThread` en `ResumeThread`
geen enkele verboden aanroep meer staat.

**(2) De stackregels konden door het logmaximum wegvallen.** De teller is nu per soort: gewone regels
20.000, stackregels een eigen 4.000. Een stortvloed `ReadFile`-regels kan de regels waar het om gaat
dus niet meer wegduwen. (In de A/B-meting hieronder is het effect meteen te zien: dezelfde start geeft
met de nieuwe DLL 2.919 regels waar de oude 21.143 schreef.)

**(3) De mutex-haken hadden drie races.** (a) `PatchIat` zette het importtabel-slot om en pas
daarna werd `g_echte_*` gevuld: in dat venster gaf de haak `WAIT_FAILED` of `NULL`. (b) De tweede
ronde van `installeer_haken` (via `Herprobeer`) las het slot dat inmiddels **ons eigen haak-adres**
bevatte en zette dat in `g_echte_*`, waarna de haak zichzelf aanriep -- eindeloze herhaling. (c) Er
werd pas na de wachtdienst gelogd, dus juist een wachter die nooit terugkomt schreef niets weg. Nu
haalt `onthoud_sloten()` de originelen **uit kernel32 zelf** (`GetProcAddress`, met dezelfde
`if (!g_echte_*)`-wacht als `onthoud_originelen` voor de comm-haken), is er een `LeesIat` die eerst
leest en een wachter die een slot dat al van ons is overslaat, en logt `rg_WaitForSingleObject`
**voor** het blokkeren ("GAAT wachten") en daarna nog eens met de uitkomst. Dit is een concrete
alternatieve verklaring voor "de mutex-haken laten het spel stoppen", dus die haken worden hierna
opnieuw gemeten met minstens vijf starts.

**(4) Markers.** Een expliciet gezette omgevingsvariabele wint nu van een markerbestand; eerder zette
`LaadMeetdraad` de variabele overschrijvend, waardoor een marker in de view een bewust gekozen waarde
stil veranderde. `MeldMarkers()` schrijft bij het laden een keer op welke markers aanwezig zijn, of de
omgeving ze expliciet zet en wie er dan wint. En er kan geen marker meer in de kluis belanden:
`.gitignore` sluit `*.aan` uit **helemaal achteraan** (na `patches/**` en `patches/*/dropin/**`, want
in .gitignore wint de laatste regel die past), met de nieuwe proef `tests/markers_test.sh`
(ctest-doel `ravengun_markers`) die beide kanten controleert: geen enkel `.aan` in git, markers in een
drop-in-map genegeerd, en de DLL's en `dropin.ini` nog steeds mee.

## Review 13: de prefixstappen van de NAS af (P1-P3, P5, P6)

**P1 (NAS-veiligheid).** De kopieerstap deed `create_directories` + `copy_file(overwrite_existing)`:
dat volgt koppelingen bij het doel en in de ouders, en er was geen `on_nas`- of schrijfmapcontrole.
Een `rg-direct --run` zonder view, of een game die niet in een view staat (`view_wortel()` geeft dan de
spelmap zelf, en die kan op de NAS liggen), schreef zo rechtstreeks op de NAS. Nu:

* de motor weigert `[Launch] PrefixSetup` als de game niet in een view staat of als de view op de NAS
  ligt;
* een kopieerdoel moet in de view liggen, de map moet bestaan (de motor maakt hem niet aan -- dat zou
  door een koppeling kunnen lopen), er mag geen koppeling in de weg zitten, de map mag niet op de NAS
  liggen, en staat het doel niet in een schrijfmap (`ViewWritable`) dan mag de map zelf geen koppeling
  zijn;
* de werkmap van een run-stap moet bestaan, niet op de NAS liggen en niet door een koppeling lopen;
* `schoon()` wordt nu ook op het deel **na** `prefix:` toegepast (`prefix:/etc/x` en `prefix:../..`
  kwamen er eerst door);
* de loper doet dat allemaal **opnieuw** met `lstat` en `on_nas` en schrijft met `write_atomic`
  (tijdelijke naam met `O_EXCL|O_NOFOLLOW`, dan `rename` -- dat vervangt de naam en volgt geen
  koppeling), niet met `copy_file(overwrite_existing)`;
* de `--run`-controle kijkt nu ook naar de kopieerdoelen en de werkmappen van de stappen.

**Een echte fout die de nieuwe proef ving**: in de loper gebruikte ik eerst `schrijfmap_problemen()` op
de *doelmap*. Die kijkt ook naar alle kinderen, en het doel mag de **wortel van de view** zijn -- dan
maakt elke koppeling naar de NAS in de hele view de stap onklaar. Mario Kart DX werd zo geweigerd
(".../ipccl.dll is een koppeling naar de NAS"). Voor een kopieerdoel telt de weg ernaartoe, niet de
kinderen; dat is nu een componentcontrole.

**P2 (de time-out was dode code).** `read_lines(c.out)` wacht op EOF en liep vóór de `waitpid`-lus:
een hangende stap hing de hele start (mijn eigen logboeken van 11:07 en 11:10 laten dat zien). De stap
draait nu in een **eigen procesgroep**, de pijp wordt met `poll()` geleegd en `waitpid(WNOHANG)` loopt
tegen één deadline; bij een time-out gaat de hele groep dood en daarna `wineserver -k` voor die prefix.
`g_stop` (SIGTERM) wordt tijdens de stap geëerd. De tijd is met `RG_PREFIXSTAP_TIMEOUT` te verzetten
(proeven).

**P3 (stempels).** Er was een stempel per stap: faalde een latere stap, dan stonden de eerdere al
"klaar" (11:03: PreprocessAmAuth gestempeld, daarna mislukte de kopie), en de stempel is per prefix
terwijl het kopieerdoel per view is (na een view-verversing liep de kopie nooit meer). Nu is er **één
stempel voor de hele lijst** (sha256 van de genormaliseerde stappen), die pas na de laatste geslaagde
stap wordt geschreven -- en het schrijven wordt gecontroleerd. Een kopieerstap loopt opnieuw als het
doel ontbreekt.

**P5.** Een kale naam is alleen nog `regsvr32(.exe)`; al het andere wordt geweigerd in plaats van stil
een programma uit de prefix te worden. `PrefixSetup` en `SecondBare` worden op de Unity- en
native-route nu **geweigerd** in plaats van stil genegeerd (in `writable_route_refusal`).

**P6 (proeven).** Nieuw: `tests/prefix_setup_test.sh` (ctest `ravengun_prefix_setup`) drijft de
**loper** aan met `rg-direct --alleen-stappen PLAN.json` op een echt plan waarvan alleen de
prefixvelden vervangen zijn, en dekt: doel door een koppeling (ook een losse), doel op een nagebootste
NAS (cifs in een nagebootste koppelpuntentabel), een mislukte stap (stopt, latere stap loopt niet,
géén stempel), een hangende stap (na de tijd afgebroken), de stempel (tweede keer overslaan, ontbrekend
doel opnieuw kopiëren) en "er staat niets op de nagebootste NAS". In `ravengun_direct_launch_tests`
staan er proeven bij voor `prefix:` met `/` en `..`, de whitelist, een koppeling in de weg en de
stempelsleutel (zelfde lijst = zelfde stempel, andere lijst = andere): **577 controles, 0 fout**.

### P4: waar PreprocessAmAuth echt schrijft (gemeten op een verse prefix)

De bron van de kopieerstap (`prefix:drive_c/windows/WritableConfig.ini`) had mtime 04:55 en was door de
proefrondes van 11:03-11:14 niet aangeraakt, dus het vermoeden was dat de kopie een vers bestand door
een oud overschreef -- of dat de bron op een verse prefix helemaal niet bestaat. Gemeten met een
**kopie** van de prefix (niet de echte), waarin de oude `WritableConfig.ini` eerst weggehaald is, en
precies de vier stappen van het profiel tegen die verse prefix:

* `PreprocessAmAuth.exe -wait` meldt `Checking.ErrorCode : 0 / outPath : WritableConfig.ini / Ok!` en
  schrijft het bestand naar **`<prefix>/drive_c/windows/WritableConfig.ini`** -- niet naar de spelmap en
  niet naar `TeknoParrot/E/`. Op de verse prefix bestond het daar nog niet: het wordt dus echt
  aangemaakt (en als het er wel staat, laat het programma het staan -- vandaar de oude mtime).
* Daarna komen de twee kopieën in de view (`AMCUS/WritableConfig.ini` en `WritableConfig.ini`, allebei
  nieuw) en registreert `regsvr32.exe /s iauthdll.dll` de klasse. Buiten de prefix is niets
  verschenen.

**Reparatie**: de bron van een `prefix:`-stap werd bij het PLANNEN in elkaar gezet. Daardoor kon de
loper een bestand uit een andere prefix lezen dan die van deze start (in de meting was dat zichtbaar:
de kopie las uit de echte prefix terwijl de stappen in de verse liepen). Het plan onthoudt nu dat de
bron in de prefix ligt (`from_prefix` + `from_rel`) en de loper zet hem opnieuw in elkaar met
`plan.prefix`; wijkt dat af, dan schrijft hij het verschil in het logboek. Opnieuw gemeten: de kopie
leest nu uit de verse prefix.

### P7: profielen en notities kloppend gemaakt

* `patches/MarioKartDX/game.ini`: de uitleg bij de sleutel zegt nu dat de stappen een motorfunctie zijn
  met EEN stempel voor de hele lijst, en noemt de gemeten plek (`<prefix>/drive_c/windows/`). Het
  verouderde blok met handwerk ("Draai ze EEN keer per prefix...") is vervangen door wat de motor nu
  doet, met de tijdmeting erbij.
* `patches/MarioKartDX/dropin/dropin.ini`: de diagnostische `DXVK_LOG_LEVEL`/`DXVK_HUD` zijn er nu
  echt uit (er stond nog een restje in `Env`).
* `patches/Coopers9/game.ini`: de `[Input] Note` zei dat het spel zonder kern "na ongeveer drie
  seconden met exitcode 0" sluit; dat is nu vervangen door de meting van 11:35 (dezelfde access
  violation op 0x4fd8aed0 **zonder** onze kern, met `+seh`). De losse notitie onder `[ConfigOptions]`
  is weggehaald -- dat blok wordt bij een nieuwe import opnieuw gemaakt -- en staat nu in die Note.

## A/B-proef rg-rhio: opzet en een les over de meting zelf

De proef loopt met `tools/dev/../ab-rhio/ab2.sh` (buiten de repo, in /home/deck/ab-rhio): per variant
tien starts, om en om, elke start met een foto, 90 s wachttijd, daemon herstarten en het muis-geweer
opnieuw zetten, de variant-DLL naar de engine-drop-in EN de view, en de `.aan`-markers voor alle
varianten gelijk (geen). Uitkomsten per start: `ok` (voorbij BOOTING naar de bordframes) en `draait`
(leeft de game nog).

**Twee dingen die de meting zelf bijna onbruikbaar maakten, en wat ertegen gedaan is:**

1. Het script werd na ~9,5 min door een externe limiet afgebroken. Het is nu **hervatbaar** (de telling
   per variant komt uit `resultaten.txt`) en stopt zelf na een tijdsbudget, zodat een afgebroken reeks
   geen werk verliest.
2. Er liepen op een gegeven moment **vier instanties** naast elkaar. Dan draaien er twee games
   tegelijk: de ene start wordt geweigerd ("er draait al een game") en de foto/logboekregels horen bij
   de game van een *andere* variant. Dat is precies gebeurd: één run kreeg het logboekprofiel van OLD.
   Er zit nu een `flock`-slot op het script, en elke run wordt achteraf gecontroleerd op het
   logboekprofiel van zijn eigen variant (de oude DLL schrijft ~21.000 regels, de nieuwe ~2.900); de
   vervuilde run is uit de gegevens gehaald en opnieuw gedaan.

Die controle op het logboekprofiel is meteen het bewijs dat de variantwissel echt werkt: elke run
draagt de signatuur van zijn eigen DLL.

## (D) geparkeerd: wat gemeten is, wat de volgende stap is (13:22)

De laatste dertig minuten van (D) zijn gegaan naar het (D)-gereedschap (review 14, punt 2) en de
mutex-hermeting. Stand:

**De mutex-haken laten het spel niet stoppen.** Vijf starts met de haken aan (`rg-rhio-mutex.aan`) tegen
vijf zonder, om en om, dezelfde 90 s en dezelfde foto:

| start | haken | DLL | foto (bytes) | draait nog | logregels | mutexregels |
|---|---|---|---|---|---|---|
| 1 | UIT | 01a873d1 | 23408 | ja | 2922 | 0 |
| 1 | AAN | 01a873d1 | 23581 | ja | 5939 | 3000 |
| 2 | UIT | 01a873d1 | 23205 | ja | 2940 | 0 |
| 2 | AAN | 01a873d1 | 23581 | ja | 5938 | 3000 |
| 3 | UIT | 01a873d1 | 23751 | ja | 2931 | 0 |
| 3 | AAN | 01a873d1 | 23581 | ja | 5889 | 3000 |
| 4 | UIT | 01a873d1 | 23751 | ja | 2931 | 0 |
| 4 | AAN | 01a873d1 | 23205 | ja | 5947 | 3000 |
| 5 | UIT | 01a873d1 | 23408 | ja | 2929 | 0 |
| 5 | AAN | 01a873d1 | 23581 | ja | 5560 | 2624 |

Alle tien bleven draaien. Dat is de tweede keer dat de bewering "de mutex-haken laten het spel meteen
stoppen" niet standhoudt (zie ook de A/B-proef hierboven, waar NEW met dezelfde haken hetzelfde deed
als OLD). De haken zijn dus geen kandidaat-oorzaak meer.

**Wat het gereedschap nu wel laat zien.** Met een eigen budget voor de mutex-regels blijft het algemene
maximum van 20.000 regels intact (eerst waren dat er 20.001 met 18.871 mutex-regels erin: het maximum
was op voordat de interessante regel er stond). De mutex-regels blijken vrijwel allemaal
`WaitForSingleObject(handvat, 0)` te zijn -- een poll van de CRT vanuit `game.exe+0x90bdd` -- en die
zijn nu uit het logboek gelaten, want een wachtdienst van 0 ms kan niet blijven hangen. In de
hermeting gaf dat precies één regel met een echte duur.

**Wat (D) nog niet heeft.** De hoofddraad wacht in code die tijdens het starten in de image wordt
aangevuld (`game.exe+0x9e7bc8`, aanroeper `+0x8ff9b`: beide buiten elke sectie, zie de wartran-README),
dus uit het bestand is die plek niet te ontleden. Daarvoor is `tools/dev/rh-dump-adressen.sh` klaar: dat
laat het spel draaien, kiest het proces met de grootste `game.exe`-image (niet het hulp-exe van 71
bytes), dumpt 192 bytes op elke gevraagde rva uit `/proc/<pid>/mem` en laat objdump er 32-bits code van
maken. De uitvoer komt in `tools/rg-rhio/out/rh-dump` (in de repo, maar genegeerd).

**Volgende stap voor (D)** (een eigen ronde): die dump draaien en de wachtdienst met zijn handvat
benoemen, daarna vijf starts met en vijf zonder bordantwoord vergelijken. Wat we nu weten: de game
leest het bord ongeveer 1140-1149 keer in 90 s en komt niet voorbij BOOTING, in geen van de varianten.

## Levende controle van de pass-2-samenvoeging op het echte pad (13:25 en 13:27)

De leiding vroeg een keer echt te kijken of de samenvoeging op het echte pad doet wat de proeven
beloven. Alleen de stempel is weggehaald:

```
rm -f ~/.local/share/ravengun/prefixes/MarioKartDX/.ravengun-prefixsetup/*.done
```

Er stonden daar vier oude stempels met een cijfernaam (uit de tijd dat elke stap zijn eigen stempel
kreeg); die zijn mee weggehaald en komen niet terug -- er wordt er nu een geschreven, voor de hele
lijst. Daarna een start via de daemon (`ravengunctl game start MarioKartDX`).

**Eerste start** (`~/.local/state/ravengun/logs/direct-MarioKartDX-20261019-132528.log`):

```
prefixstap: run . PreprocessAmAuth.exe -wait (eenmalig per prefix)
prefixstap: copy prefix:drive_c/windows/WritableConfig.ini WritableConfig.ini (eenmalig per prefix)
prefixstap: copy prefix:drive_c/windows/WritableConfig.ini AMCUS/WritableConfig.ini (eenmalig per prefix)
prefixstap: run AMCUS regsvr32.exe /s iauthdll.dll (eenmalig per prefix)
[13:25:28] prefixstap: .../proton-11.0-2/bin/wine 'Z:\...\views\MarioKartDX\PreprocessAmAuth.exe' -wait (cwd .../views/MarioKartDX)
[13:25:29] prefixstap: Checking.ErrorCode : 0
[13:25:29] prefixstap: outPath : WritableConfig.ini
[13:25:30] prefixstap: Ok!
[13:25:30] prefixstap: klaar (status 0)
[13:25:30] prefixstap: kopieren klaar: .../prefixes/MarioKartDX/drive_c/windows/WritableConfig.ini -> .../views/MarioKartDX/WritableConfig.ini
[13:25:30] prefixstap: kopieren klaar: .../prefixes/MarioKartDX/drive_c/windows/WritableConfig.ini -> .../views/MarioKartDX/AMCUS/WritableConfig.ini
[13:25:30] prefixstap: .../wine regsvr32.exe /s iauthdll.dll (cwd .../views/MarioKartDX/AMCUS)
[13:25:30] prefixstap: regsvr32: Successfully registered DLL 'iauthdll.dll'
[13:25:30] prefixstap: klaar (status 0)
[13:25:30] prefixstappen: stempel .../prefixes/MarioKartDX/.ravengun-prefixsetup/3b92f495bbfda327.done
```

En de schrijfmap-regel uit dezelfde log: `schrijfmap: .../views/MarioKartDX/AMCUS is al een echte map
(stand van de game)` -- dus geen koppeling naar de NAS. Het aantal regels met **"weigert"** in deze log
is **0**: de fout die de leiding in mijn P1 zag (`--run` weigerde elke start van MarioKartDX met code 2)
is met de pass-2-samenvoeging weg.

**Tweede start** (`.../direct-MarioKartDX-20261019-132751.log`): alle vier de stappen worden
overgeslagen.

```
[13:27:51] prefixstappen: stempel .../3b92f495bbfda327.done staat er; alleen ontbrekende kopieen lopen opnieuw
[13:27:51] prefixstap: al gedaan (run . PreprocessAmAuth.exe -wait)
[13:27:51] prefixstap: al gedaan (copy prefix:drive_c/windows/WritableConfig.ini WritableConfig.ini)
[13:27:51] prefixstap: al gedaan (copy prefix:drive_c/windows/WritableConfig.ini AMCUS/WritableConfig.ini)
[13:27:51] prefixstap: al gedaan (run AMCUS regsvr32.exe /s iauthdll.dll)
```

De game liep in beide gevallen (`ravengunctl game status`: "De game draait.").

**De foto** (`docs/screenshots/mariokart-prefixstappen-lege-opname-2026-10-19.png`, 1920x1080, 10.669
bytes) is een LEGE OPNAME: hij is byte-identiek aan de LostLandAdventure-opname van dezelfde dag
(sha256 707d1f84...), dus hij zegt niets over wat de game tekende. De zwarte toestand van (F) is hier
dus niet mee aangetoond; wat de stappen doen blijkt uit het logboek (de IAuth-klasse is geregistreerd en
AMAuthd blijft staan). Of het spel zelf al iets tekent is met deze opname NIET vast te stellen -- daar is
een echte opname of een vensterlijst voor nodig. Dat is de renderkant van (F), niet deze
stappen.

## De twee levende controles van review 15, punt 1 (14:35 en 14:38)

De geinstalleerde lader was nog mijn bouw van 13:32: daar zat 83f0314 in maar niet de reparatie voor de
verse prefix uit c45eef4, en de nieuwe rg-rhio.dll was nog niet geinstalleerd. Na het installeren van de
huidige bouw (rg-direct 2.803.600 bytes, rg-rhio.dll 306.176 bytes, sha1 a553e98e) zijn de twee
controles gedaan.

**(a) Een start van Mario Kart DX via de daemon** (`direct-MarioKartDX-20261019-143533.log`):

```
[14:35:33] prefixstappen: stempel .../prefixes/MarioKartDX/.ravengun-prefixsetup/3b92f495bbfda327.done staat er; alleen ontbrekende kopieen lopen opnieuw
[14:35:33] prefixstap: al gedaan (run . PreprocessAmAuth.exe -wait)
[14:35:33] prefixstap: al gedaan (copy prefix:drive_c/windows/WritableConfig.ini WritableConfig.ini)
[14:35:33] prefixstap: al gedaan (copy prefix:drive_c/windows/WritableConfig.ini AMCUS/WritableConfig.ini)
[14:35:33] prefixstap: al gedaan (run AMCUS regsvr32.exe /s iauthdll.dll)
```

De vier stappen worden overgeslagen en het aantal regels met **"weigert"** in deze log is **0**. De game
liep ("De game draait.").

**(b) Een start van Robin Hood via de daemon** (`direct-RobinHood-20261019-143738.log`, en
`views/RobinHood/data/rg-rhio.log` van 14:38):

```
rg-rhio geladen (32-bits): pad=\.\USBPDCOM, ioctl-gelukt=1
--- nieuw proces: de I/O-bordkant van RavenGun geladen ---
omgeving: RG_RHIO_EERSTEBYTE=afwezig, RG_RHIO_BORD=afwezig (bordtoestand uit gedeeld geheugen: ja)
marker rg-rhio-stack.aan: afwezig (stackregels (RG_RHIO_STACK))
marker rg-rhio-mutex.aan: afwezig (mutex-haken (RG_RHIO_MUTEX))
marker rg-compat.aan: afwezig (meetdraad (RG_COMPAT_SAMPLE))
```

De nieuwe kop van het logboek staat er, alle drie de markers zijn afwezig, en het aantal regels met
**"mutex: telling"** is **0** -- precies zoals het hoort: die telling komt alleen als de mutexmeting
aanstaat. De game liep ("De game draait.").

Let op: de eerste poging om Robin Hood te starten (14:36) maakte geen nieuw logboek aan en de game
liep niet; het logboek dat ik toen las was dat van de mutex-hermeting van 13:22, met daarin nog
"marker rg-rhio-mutex.aan: aanwezig". De tweede poging (14:38) is de geldige meting hierboven.

## De levende controle van de stempelronde (review 16, 15:26 en 15:29)

Alleen de stempel van Mario Kart DX is weggehaald (`rm -f <prefix>/.ravengun-prefixsetup/*.done`), er is
niets anders aangeraakt. Daarna twee starts via de daemon.

**Eerste start** (`direct-MarioKartDX-20261019-152643.log`):

```
[15:26:44] prefixstap: Checking.ErrorCode : 0
[15:26:44] prefixstap: outPath : WritableConfig.ini
[15:26:45] prefixstap: Ok!
[15:26:45] prefixstap: klaar (status 0)
[15:26:45] prefixstap: kopieren klaar: .../prefixes/MarioKartDX/drive_c/windows/WritableConfig.ini -> .../views/MarioKartDX/WritableConfig.ini
[15:26:45] prefixstap: kopieren klaar: .../prefixes/MarioKartDX/drive_c/windows/WritableConfig.ini -> .../views/MarioKartDX/AMCUS/WritableConfig.ini
[15:26:45] prefixstap: klaar (status 0)
[15:26:45] prefixstappen: /home/deck/.local/share/ravengun/wine/proton-11.0-2/bin/wineserver -w (WINEPREFIX=.../prefixes/MarioKartDX)
[15:26:46] prefixstappen: stempel .../prefixes/MarioKartDX/.ravengun-prefixsetup/3b92f495bbfda327.done
```

Alle vier de stappen eindigen met "klaar (status 0)", daarna komt de regel met `wineserver -w` (met de
prefix erbij) en dan de stempelregel; het aantal regels met **"weigert"** is **0** en de game start
gewoon ("De game draait."). De stempel staat daarna weer op schijf.

**Tweede start** (`direct-MarioKartDX-20261019-152919.log`):

```
[15:29:19] prefixstappen: stempel .../prefixes/MarioKartDX/.ravengun-prefixsetup/3b92f495bbfda327.done staat er; alleen ontbrekende kopieen lopen opnieuw
[15:29:19] prefixstap: al gedaan (run . PreprocessAmAuth.exe -wait)
[15:29:19] prefixstap: al gedaan (copy prefix:drive_c/windows/WritableConfig.ini WritableConfig.ini)
[15:29:19] prefixstap: al gedaan (copy prefix:drive_c/windows/WritableConfig.ini AMCUS/WritableConfig.ini)
[15:29:19] prefixstap: al gedaan (run AMCUS regsvr32.exe /s iauthdll.dll)
```

"al gedaan" voor alle vier, geen `wineserver -w` (die komt alleen NA een run-stap, en er liep er nu
geen), 0 regels met "weigert", en de game draait.

## De levende controle van review 17 (16:09 en 16:11)

Alleen de MK-stempel weggehaald, daarna twee starts met de nieuw geinstalleerde lader (die van 6b77dca was
nog nooit live gestart).

**Eerste start** (`direct-MarioKartDX-20261019-160933.log`): vier stappen (`prefixstap: run|copy` komt 4x
voor), waarvan de twee run-stappen met "klaar (status 0)" en de twee kopieerstappen met "kopieren klaar";
regel 53 is `wineserver -w (WINEPREFIX=.../prefixes/MarioKartDX)`, regel 54 is
`prefixstappen: stempel .../3b92f495bbfda327.done`, het aantal regels met "weigert" is **0**, de stempel
staat daarna op schijf en de game draait.

**Tweede start** (`direct-MarioKartDX-20261019-161115.log`): **"al gedaan" 4x**, 0 regels met "weigert",
de game draait.
