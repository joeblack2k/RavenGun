# GoGoStrike (`bowling_usa`) in de native route — waarom sterft hij?

**Meting op:** SteamOS-Deck `192.168.2.247` (hostnaam niet beschikbaar; `hostname` ontbreekt op deze machine), gebruiker `deck` (uid 1000).
**Route:** `~/projects/ravengun/scripts/play-native.sh --game GoGoStrike --headless`, dus bubblewrap-sandbox, **geen Wine, geen BudgieLoader, geen TeknoParrotUi**.
**Tijdstempel runs:** 2026-10-12 18:53:07 t/m 18:58:38.
**Binaire:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Go Go Strike/bowling_usa`, md5 `fe2f3613434ab92fbde26a6591528c50`, 476 176 bytes, `ELF 32-bit LSB executable, Intel i386, dynamically linked, interpreter /lib/ld-linux.so.2, stripped`, `DT_NEEDED`: `libstdc++.so.5`, `liballeg.so.4.0`, `libm.so.6`, `libNL.so.1.6`, `libc.so.6` (**gemeten**).

In dit document is **gemeten** iets wat een commando op deze machine opleverde; **afgeleid** is mijn gevolgtrekking daaruit.

---

## 0. Samenvatting — de exacte fout en de backtrace

**De exacte fout (gemeten):** `SIGSEGV`, `si_code = SEGV_MAPERR`, `si_addr = NULL`, op **`0x08071915`**, dat is **`uwSramCheckFormatCode+5`**, met foutinstructie

```
0x8071915 <uwSramCheckFormatCode+5>:   cmpl  $0x69dc065,(%eax)      ; eax = 0
```

Procesafsluiting: **exitcode 139**, logboekregel `Shutting down Allegro due to signal #11` (Allegro's eigen signaalafhandelaar).

**De backtrace (gemeten met `--gdb`, volledige batchuitvoer):**

```
Thread 1 "bowling_usa" received signal SIGSEGV, Segmentation fault.
0x08071915 in uwSramCheckFormatCode ()
#0  0x08071915 in uwSramCheckFormatCode ()
#1  0x0805ac5b in _mangled_main(int, char**) ()
#2  0xf7eadfd8 in main () from /rg/lib/liballeg.so.4.0
#3  0xf7ad3640 in ?? () from /usr/lib32/libc.so.6
#4  0xf7ad379e in __libc_start_main () from /usr/lib32/libc.so.6
#5  0x08059431 in _start ()

eax            0x0                 0
eip            0x8071915           0x8071915 <uwSramCheckFormatCode+5>
eflags         0x210282            [ SF IF RF ID ]
=> 0x8071915 <uwSramCheckFormatCode+5>:	cmpl   $0x69dc065,(%eax)
   0x807191b <uwSramCheckFormatCode+11>:	sete   %al
   0x807191e <uwSramCheckFormatCode+14>:	and    $0xff,%eax
   0x8071923 <uwSramCheckFormatCode+19>:	ret
   0x8071924:	lea    0x0(%esi),%esi
   0x807192a:	lea    0x0(%edi),%edi
   0x8071930 <vSramClearAll>:	sub    $0xc,%esp
   0x8071933 <vSramClearAll+3>:	cmpl   $0xffffffff,0x80c2ba4
```

**De oorzaak (gemeten, niet afgeleid):** de crash is **niet** het gevolg van een mislukte grafische initialisatie in de gewone route — `set_gfx_mode` wordt in deze route **nooit aangeroepen**. De keten is:

1. `vSystemDeviceInit` roept `install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, argv[0])` aan; dat **mislukt** (`allegro_error = "Insufficient digital voices available"`), omdat de sandbox geen OSS-apparaten heeft (`/dev/dsp`, `/dev/mixer`, `/dev/sequencer` → `ENOENT`).
2. De game gaat bij een geluidsfout naar zijn **eigen vroege uitgang** (`0x805963f → jmp 0x8059716`) en slaat daarmee **de hele rest van de apparaatinitialisatie over**: `set_color_depth`, `set_gfx_mode(…,640,480,…)`, `install_mouse`, **`lpSramDeviceOpen`**, `vQlDeviceOpen`, `vKeyIoDeviceOpen`, `vCoinDeviceInit`, `vPayoutDeviceInit`, `iRS232OpenPort`.
3. `_mangled_main` loopt daarna gewoon door en roept `uwSramCheckFormatCode()` aan, dat de globale variabele `0x80c2ba8` leest — die is **nooit geschreven** (hij wordt alleen in het mmap-succespad van `lpSramDeviceOpen` gezet) en dus `0`. `cmp [eax],…` met `eax = 0` → SIGSEGV.

De aantekening in `packaging/native-games.json` ("crasht op een NULL-aanwijzer; **geen bruikbare grafische modus**; een `.allegrorc` met XWIN hielp niet") beschrijft dus het **symptoom** goed maar de **eerste oorzaak** verkeerd: het is het **geluid**, niet het beeld. De planregel "de game is al verder gekomen dan die SRAM-hobbel" is in deze route **onjuist**: de crash ís de SRAM-hobbel (via de overgeslagen open).

---

## 1. Profiel en droogloop

**Commando**

```
powershell -NoProfile -ExecutionPolicy Bypass -File re\rc.ps1 -Target deck \
  -Cmd "bash /home/deck/_gogostrike-dryrun.sh"
```

**Profiel (gemeten, `packaging/native-games.json` r.163-188):** `exe /pm/bowling_usa`, `cwd /pm`, `home /pm`, `args []`, `res 1360x768`, `preload ""`, één mount `overlay` van `@nas/Go Go Strike` naar `/pm`, `libs` = `liballeg.so.4.0`, `libstdc++.so.5`, `libNL.so.1.6` (uit `@tpui/ElfLdr2/libs`, naar `/rg/lib`).

**Droogloop buitenkant (gemeten):**

```
RG_NATIVE_INNER=1 gamescope --backend headless -W 1920 -H 1080 -w 1360 -h 768 -- \
  /home/deck/projects/ravengun/scripts/play-native.sh --game GoGoStrike --res 1360x768 --seconds 25 --headless
```

**Droogloop binnenkant (`bwrap`-commando, gemeten):**

```
bwrap --die-with-parent --unshare-pid --unshare-ipc --unshare-uts --unshare-net
  --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
  --ro-bind /etc /etc --proc /proc --dev /dev
  --dev-bind /dev/dri /dev/dri --dev-bind-try /dev/snd /dev/snd
  --ro-bind /sys /sys --ro-bind-try /run/udev /run/udev --tmpfs /tmp
  --dev-bind-try /dev/input /dev/input
  --overlay-src "/home/deck/mnt/spidernas_emulation/Teknoparrot/Go Go Strike"
  --overlay /home/deck/.local/share/ravengun/native/GoGoStrike/0/upper
            /home/deck/.local/share/ravengun/native/GoGoStrike/0/work /pm
  --ro-bind .../ElfLdr2/libs/liballeg.so.4.0 /rg/lib/liballeg.so.4.0
  --ro-bind .../ElfLdr2/libs/libstdc++.so.5 /rg/lib/libstdc++.so.5
  --ro-bind .../ElfLdr2/libs/libNL.so.1.6     /rg/lib/libNL.so.1.6
  --dir /tmp/.X11-unix --ro-bind /tmp/.X11-unix/X0 /tmp/.X11-unix/X0
  --dir /run/user/1000 --chmod 0700 /run/user/1000
  --chdir /pm --clearenv
  --setenv HOME /pm --setenv PATH /usr/bin --setenv DISPLAY :0
  --setenv XDG_RUNTIME_DIR /run/user/1000 --setenv LANG C.UTF-8
  --setenv LD_LIBRARY_PATH /rg/lib
  /pm/bowling_usa
```

**Wat de droogloop aantoont (gemeten):**

* Er is **geen enkele bind** voor `/dev/w040101sram0` of voor één van de `/dev/w040101_io*`-paden.
* Het voorbereide SRAM-bestand `~/.local/share/ravengun/native/GoGoStrike/w040101sram` (65 536 B) komt **nergens** in het commando voor: het profiel heeft geen `FILE`-regel en `play-native.sh` kent alleen `overlay`/`ro`-mounts van mappen. De voorbereiding is dus **los zand**.
* `--dev /dev` zet een **nieuw, kaal `/dev`** neer; alleen `/dev/dri`, `/dev/snd` en `/dev/input` worden erin gebonden.

**Overlay-inhoud (gemeten):** `~/.local/share/ravengun/native/GoGoStrike/0/upper/.allegrorc` (90 B) en `bowling.log` (0 B).

```
[graphics]
gfx_card = XWIN
gfx_cardw = XWIN
[system]
keyboard = XWINDOWS
mouse = XWINDOWS
```

**PAOKAI-apparaten op de host (gemeten):** `/dev/w040101*` bestaat **helemaal niet** — `ls` en `stat /dev/w040101sram0` geven `No such file or directory`. `native-games.json`/het plan spreken van "wil ook de PAOKAI-apparaten"; die zijn er op deze machine dus niet, ook niet buiten de sandbox.

---

## 2. Stap 1 — kale run met logboek

**Commando (in `_gogostrike-run.sh`, met een voorscan op andere processen):**

```
bash /home/deck/projects/ravengun/scripts/play-native.sh --game GoGoStrike --headless --seconds 25
```

**Uitvoer (gemeten, kern):**

```
play-native: GoGoStrike (Go Go Strike (bowling_usa)), res 1360x768, log /home/deck/.local/state/ravengun/logs/native-GoGoStrike-20261012-185307.log
play-native: LET OP: status geparkeerd (…)
play-native: GoGoStrike beëindigd met exitcode 139 (log: …/native-GoGoStrike-20261012-185307.log)
```

**Logboek `native-GoGoStrike-20261012-185307.log` (volledig, gemeten):**

```
== play-native GoGoStrike 20261012-185307
display=:1 res=1360x768 headless=1 preload=geen
cmd: /pm/bowling_usa
Shutting down Allegro due to signal #11
== exit 139
```

`display=:1`: de gamescope-wikkel zet voor het spel Xwayland op **`:1`** op (`[gamescope] … [xwayland/server.c:108] Starting Xwayland on :1`, gemeten in dezelfde uitvoer). Er is dus **een echte X-server binnen de sandbox** (zie §5).

Er was vóór elke run niets actief: `pgrep -a wine` → leeg, `pgrep -af 'bowling_usa|play-native'` → leeg (gemeten, vóór elke start herhaald).

---

## 3. Stap 2 — strace

**Commando:**

```
bash …/play-native.sh --game GoGoStrike --headless --seconds 25 --strace /home/deck/_gogostrike-strace.txt
```

`play-native.sh` draait dan `/usr/bin/strace -f -tt -s 200 -o /rg-strace/_gogostrike-strace.txt -e trace=openat,open,ioctl,connect,execve`.

**Uitkomst (gemeten):** het volledige spoor is **55 regels** (`wc -l`), bewaard op `/home/deck/_gogostrike-strace.txt`. De laatste 49 regels, integraal:

```
4  18:53:20.044990 execve("/pm/bowling_usa", ["/pm/bowling_usa"], 0x7fff9ecd7348 /* 7 vars */) = 0
4  18:53:20.065182 openat(AT_FDCWD, "/rg/lib/libstdc++.so.5", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
4  18:53:20.065361 openat(AT_FDCWD, "/rg/lib/liballeg.so.4.0", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
4  18:53:20.065498 openat(AT_FDCWD, "/rg/lib/libm.so.6", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = -1 ENOENT (No such file or directory)
4  18:53:20.065531 openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
4  18:53:20.065615 openat(AT_FDCWD, "/usr/lib32/libm.so.6", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
4  18:53:20.065759 openat(AT_FDCWD, "/rg/lib/libNL.so.1.6", O_RDONLY|O_LARGEFILE|O_CLOEXEC) = 3
4  18:53:20.065863 openat(AT_FDCWD, "/rg/lib/libc.so.6", …) = -1 ENOENT
4  18:53:20.065890 openat(AT_FDCWD, "/usr/lib32/libc.so.6", …) = 3
4  18:53:20.066043 openat(AT_FDCWD, "/rg/lib/libgcc_s.so.1", …) = -1 ENOENT
4  18:53:20.066069 openat(AT_FDCWD, "/usr/lib32/libgcc_s.so.1", …) = 3
4  18:53:20.066200 openat(AT_FDCWD, "/rg/lib/libpthread.so.0", …) = -1 ENOENT
4  18:53:20.066225 openat(AT_FDCWD, "/usr/lib32/libpthread.so.0", …) = 3
4  18:53:20.066358 openat(AT_FDCWD, "/rg/lib/libXext.so.6", …) = -1 ENOENT
4  18:53:20.066385 openat(AT_FDCWD, "/usr/lib32/libXext.so.6", …) = 3
4  18:53:20.066513 openat(AT_FDCWD, "/rg/lib/libX11.so.6", …) = -1 ENOENT
4  18:53:20.066539 openat(AT_FDCWD, "/usr/lib32/libX11.so.6", …) = 3
4  18:53:20.066681 openat(AT_FDCWD, "/rg/lib/libdl.so.2", …) = -1 ENOENT
4  18:53:20.066706 openat(AT_FDCWD, "/usr/lib32/libdl.so.2", …) = 3
4  18:53:20.066853 openat(AT_FDCWD, "/rg/lib/libxcb.so.1", …) = -1 ENOENT
4  18:53:20.066878 openat(AT_FDCWD, "/usr/lib32/libxcb.so.1", …) = 3
4  18:53:20.067007 openat(AT_FDCWD, "/rg/lib/libXau.so.6", …) = -1 ENOENT
4  18:53:20.067032 openat(AT_FDCWD, "/usr/lib32/libXau.so.6", …) = 3
4  18:53:20.067159 openat(AT_FDCWD, "/rg/lib/libXdmcp.so.6", …) = -1 ENOENT
4  18:53:20.067184 openat(AT_FDCWD, "/usr/lib32/libXdmcp.so.6", …) = 3
4  18:53:20.069868 openat(AT_FDCWD, "bowling.log", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
4  18:53:20.069942 openat(AT_FDCWD, "/dev/mixer", O_RDWR) = -1 ENOENT (No such file or directory)
4  18:53:20.070078 openat(AT_FDCWD, "/pm/.allegrorc", O_RDONLY) = 3
4  18:53:20.070459 openat(AT_FDCWD, "/usr/local/lib/allegro//4.0/modules.lst", O_RDONLY) = -1 ENOENT
4  18:53:20.070492 openat(AT_FDCWD, "/usr/lib/allegro//4.0/modules.lst", O_RDONLY) = -1 ENOENT
4  18:53:20.070706 connect(3, {sa_family=AF_UNIX, sun_path=@"/tmp/.X11-unix/X1"}, 20) = -1 ECONNREFUSED
4  18:53:20.070790 connect(3, {sa_family=AF_UNIX, sun_path="/tmp/.X11-unix/X1"}, 110) = 0
4  18:53:20.071541 openat(AT_FDCWD, "/rg/lib/libXcursor.so.1", …) = -1 ENOENT
4  18:53:20.071565 openat(AT_FDCWD, "/etc/ld.so.cache", …) = 4
4  18:53:20.071634 openat(AT_FDCWD, "/usr/lib32/libXcursor.so.1", …) = -1 ENOENT
4  18:53:20.071686 openat(AT_FDCWD, "/rg/lib/libXcursor.so", …)   = -1 ENOENT
4  18:53:20.071712 openat(AT_FDCWD, "/etc/ld.so.cache", …) = 4
4  18:53:20.071773 openat(AT_FDCWD, "/usr/lib32/libXcursor.so", …) = -1 ENOENT
4  18:53:20.071811 openat(AT_FDCWD, "/rg/lib/libXcursor", …)    = -1 ENOENT
4  18:53:20.071832 openat(AT_FDCWD, "/etc/ld.so.cache", …) = 4
4  18:53:20.071891 openat(AT_FDCWD, "/usr/lib32/libXcursor", …)  = -1 ENOENT
4  18:53:20.072114 openat(AT_FDCWD, "/dev/dsp", O_WRONLY|O_NONBLOCK) = -1 ENOENT (No such file or directory)
4  18:53:20.072162 openat(AT_FDCWD, "/pm/patches.dat", O_RDONLY) = -1 ENOENT
4  18:53:20.072275 openat(AT_FDCWD, "/dev/sequencer", O_WRONLY) = -1 ENOENT (No such file or directory)
4  18:53:20.072331 openat(AT_FDCWD, "img/cfont/ascfont.15", O_RDONLY) = 4
4  18:53:20.078309 openat(AT_FDCWD, "img/cfont/stdfont.15", O_RDONLY) = 4
4  18:53:20.101015 openat(AT_FDCWD, "img/cfont/spcfont.15", O_RDONLY) = 4
4  18:53:20.114713 openat(AT_FDCWD, "img/cfont/spcfsupp.15", O_RDONLY) = 4
4  18:53:20.122200 openat(AT_FDCWD, "img/dat_sysdat.dat", O_RDONLY) = 4
4  18:53:20.133751 --- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_MAPERR, si_addr=NULL} ---
6  18:53:20.154779 +++ exited with 0 +++
5  18:53:20.155004 --- SIGRTMIN {si_signo=SIGRTMIN, si_code=SI_TKILL, si_pid=4, si_uid=1000} ---
5  18:53:20.155143 +++ exited with 0 +++
4  18:53:20.155336 --- SIGSEGV {si_signo=SIGSEGV, si_code=SI_TKILL, si_pid=4, si_uid=1000} ---
4  18:53:20.328848 +++ killed by SIGSEGV (core dumped) +++
```

**Wat dit meet:**

* **Nul** `openat`/`open` op `/dev/w040101sram0` en op `/dev/w040101_*` (gefilterd met `grep -E 'openat|open\(' … | grep w040101` → 0 regels). De eerdere aanname dat de game "de PAOKAI-apparaten wil" is in deze route dus **nog niet aan de orde**: de game komt er niet.
* **Nul** `ioctl`-regels in het hele spoor.
* Drie OSS-openingen falen met **`ENOENT`**: `/dev/mixer`, `/dev/dsp`, `/dev/sequencer`.
* De X-verbinding **lukt**: `connect(…, "/tmp/.X11-unix/X1", 110) = 0` (de abstracte naam `@"…"` geeft eerst `ECONNREFUSED`, daarna de padnaam-variant met succes).
* De laatste geslaagde openingen zijn `img/cfont/*.15` en `img/dat_sysdat.dat`. `img/img_bowling_usa.dat` (31 836 053 B) en `img/company_OP_640.suv` worden **niet** geopend.
* De crash is `SEGV_MAPERR` op **adres NULL** — geen "wilde sprong", maar een nullyzer.

Het logboek van deze run: `native-GoGoStrike-20261012-185320.log`, inhoud `== exit 139`.

---

## 4. Stap 3 — gdb, de echte backtrace

### 4a. Standaard `--gdb`

```
bash …/play-native.sh --game GoGoStrike --headless --seconds 25 --gdb
```

Opdrachtregel in het logboek (gemeten): `/usr/bin/gdb -batch -ex "set debuginfod enabled off" -ex "set pagination off" -ex "set startup-with-shell off" -ex run -ex bt -ex "info registers" -ex "x/8i $pc" --args /pm/bowling_usa`.

Volledige uitvoer staat in §0 (backtrace + registers + instructies). Logboek: `native-GoGoStrike-20261012-185338.log`.

### 4b. Met breekpunten — welke tak is genomen?

Commando:

```
RG_GDB_CMDS=/rg/gdb.cmds bash …/play-native.sh --game GoGoStrike --headless --seconds 25 --gdb \
  --bind-ro /home/deck/_gogostrike-gdb.cmds /rg/gdb.cmds
```

Breekpunten: `0x8059669` (direct ná de `set_gfx_mode`-aanroep), `0x80596a6` (succespad ná `set_gfx_mode`), `0x80596cb` (de aanroep van `lpSramDeviceOpen`), `0x8059716` (de epiloog = doel van de vroege uitgangen).

**Uitkomst (gemeten, letterlijk):**

```
Breakpoint 1 at 0x8059669
Breakpoint 2 at 0x8059716
Breakpoint 3 at 0x80596cb
Breakpoint 4 at 0x80596a6

[BP2] vroege uitgang vSystemDeviceInit GEHAALD (sprong naar epiloog)

Thread 1 "bowling_usa" received signal SIGSEGV, Segmentation fault.
0x08071915 in uwSramCheckFormatCode ()
```

Dus: **BP1, BP3 en BP4 zijn nooit geraakt; BP2 wel.** De vroege uitgang ligt dus **vóór** `set_gfx_mode`, `install_mouse` en `lpSramDeviceOpen`. Logboek: `native-GoGoStrike-20261012-185700.log`.

### 4c. De demontage die dit sluitend maakt (gemeten met `objdump -d -M intel`)

`vSystemDeviceInit` (`0x080595a6`), aangeroepen vanuit `_mangled_main` op `0x805ab72`:

```
080595a6 <_Z17vSystemDeviceInitv@@Base>:
 80595b3: call  vSystemMixerWrite(100,100)          ; probeert /dev/mixer
 80595d1: call  install_allegro@plt
 80595d9: call  vKeyDeviceInit
 80595de: call  install_timer@plt
 80595fe: call  reserve_voices@plt (16, -1)
 8059614: call  install_sound@plt (-1, -1, argv[0])
 805961c: test  eax,eax
 805961e: je    8059644                             ; geluid OK -> verder
 8059620: push  0x80bb910                            ; allegro_error (256 B)
 8059628: push  0x80a5fcc                            ; "Error initialising sound\n%s\n"
 805962d: call  allegro_message@plt
 8059635: mov   ds:0x80bc9f8,0xf                     ; MainHiState = 0xf
 805963f: jmp   8059716                             ; <<< VROEGE UITGANG (epiloog)
 8059644: … set_color_depth(16) … set_gfx_mode(0,640,480,0,0) … install_mouse …
 80596cb: call  lpSramDeviceOpen                    ; <<< wordt dus overgeslagen
 80596d9: call  vQlDeviceOpen
 80596e1: call  vKeyIoDeviceOpen                    ; /dev/w040101_io*
 80596e6: call  vCoinDeviceInit
 80596eb: call  vPayoutDeviceInit
 80596f7: call  iRS232OpenPort(0,0)                 ; /dev/ttyS0, /dev/ttyS1
 8059704: call  set_color_conversion(1)
 8059716: leave / ret                              ; BP2
```

Er zijn precies **twee** sprongen naar de epiloog `0x8059716`: op `0x805963f` (geluidsfout, vóór `set_gfx_mode`) en op `0x80596a4` (grafische fout, ná `set_gfx_mode`). Omdat BP1 (`0x8059669`, ná `set_gfx_mode`) niet geraakt is, kan alleen de **geluidstak** genomen zijn. Dat is geen gevolgtrekking op gevoel maar een uitsluiting op de gemeten breekpunten.

`uwSramCheckFormatCode` (`0x08071910`) en zijn voeding:

```
08071910 <uwSramCheckFormatCode@@Base>:
 8071910: mov  eax,ds:0x80c2ba8        ; de globale databasis
 8071915: cmp  DWORD PTR [eax],0x69dc065   ; <<< CRASH, eax = 0
 807191b: sete al
 807191e: and  eax,0xff
 8071923: ret

08071870 <lpSramDeviceOpen@@Base>:
 8071874: mov  ds:0x80c2ba4,0xffffffff      ; fd = -1
 8071880: push 0x80ad6d0                    ; "/dev/w040101sram0"
 8071885: call open@plt (O_RDWR)
 807188d: mov  ds:0x80c2ba4,eax
 8071892: test eax,eax
 8071894: js   80718f0                      ; open mislukt -> __ASSERT(43,"sys/c/sram.c","SramDevice>=0")
 80718ab: call mmap@plt (NULL, 0x10000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)
 80718b5: js   80718d7                      ; mmap mislukt -> fd=-1, close(-1)
 80718c2: mov  ds:0x80c2ba8,eax             ; <<< ENIGE schrijfactie op de globale
 80718c7: mov  ds:0x80c2bac,0x400
 80718d2: mov  eax,ebx                      ; geeft base+0x8000 terug
 80718d6: ret
```

`0x80c2ba8` wordt **uitsluitend** op `0x80718c2` geschreven (`grep '80c2ba8'` op de hele demontage: 11 regels, waarvan 1 schrijfactie). Omdat `lpSramDeviceOpen` nooit loopt, blijft hij `0`. Dat is de crash. Het binaire bestand heeft trouwens een volledige dynamische symbooltabel (1 080 `FUNC`-symbolen; 1 074 `GLOBAL`), vandaar de functienamen in de backtrace.

`_mangled_main` (`0x0805ab16`), de volgorde in het spel zelf:

```
 805ab5a: call InitLog("bowling.log")
 805ab72: call vSystemDeviceInit          ; <<< mislukt hier op geluid
 805ab9b: call allegro_ch_init
 805abac: call FONT_V(cfont)              ; img/cfont/*.15
 805abbd: call FONT_V(font)
 805abd5: call vRandInit
 805ac06: call vSystemInit
 805ac1b: call vSystemGameDataInit
 805ac30: call vSystemCommonDataLoad      ; load_datafile("img/dat_sysdat.dat")
 805ac3e: call FONT_V
 805ac4e: call LogMesg("SramCheck.......\r\n")
 805ac56: call uwSramCheckFormatCode      ; <<< CRASH (returnadres 0x805ac5b = frame #1)
```

Dat verklaart ook de volgorde in het strace-spoor (eerst de fonts, dán `dat_sysdat.dat`, dán de crash) volledig.

**Waarom `allegro_error` niet leeg is (gemeten in 4d):** `0x80bb910` is in het spel zelf `allegro_error` (256 B in de eigen bss).

Logboeken: `native-GoGoStrike-20261012-185338.log` (4a), `…-185700.log` (4b).

---

## 5. Stap 4 — de grafische modus

**Welk stuurprogramma vraagt het spel? (gemeten)**

* Het spel zelf bevat **geen** Allegro-drivernamen (`strings | grep -E 'XWIN|GFX_XDGA2|GFX_DIRECTX|SVGA|VBE/AF|GFX_VESA'` → niets in `bowling_usa`). De game vraagt `set_gfx_mode(0, 640, 480, 0, 0)` — letterlijk `0` = `GFX_AUTODETECT`, hardgecodeerd op `0x8059664` (gemeten in de demontage).
* De stuurprogrammakaart komt dus uit `.allegrorc` in de overlay: **`gfx_card = XWIN`**, `gfx_cardw = XWIN`, `keyboard = XWINDOWS`, `mouse = XWINDOWS` (gemeten, zie §1).
* `liballeg.so.4.0` (TP's eigen bouw) zoekt naar `modules.lst` in `/usr/local/lib/allegro//4.0/` en `/usr/lib/allegro//4.0/` — beide `ENOENT` (gemeten in het strace-spoor). Drivermodules worden dus niet nageladen; de ingebouwde XWIN-driver wordt gebruikt.

**Is er een echte X-server in de sandbox? (gemeten)**

* Buiten de sandbox: `DISPLAY` is leeg in een ssh-sessie; `xdpyinfo` faalt met `unable to open display ""`. De host heeft wel een socket `/tmp/.X11-unix/X0` (`srwxr-xr-x deck deck`).
* Binnen de wikkel zet gamescope Xwayland op **`:1`** en `play-native` bindt de bijbehorende socket; het spel **verbindt met succes**: `connect(…, "/tmp/.X11-unix/X1", 110) = 0` (gemeten, strace).
* Een `xdpyinfo` binnen de sandbox kon ik met `play-native.sh` niet draaien (het script start alleen het spel). De X-client die ik wél kon observeren is het spel zelf, en die komt tot binnen `set_gfx_mode` (zie de proef hieronder).

**Proef: wat gebeurt er als de geluidspoort wordt overgeslagen? (gdb-ingreep, geen wijziging op schijf)**

Commando: `--gdb` met als breekpunt `*0x805963f`, daar eenmalig `set $pc = 0x8059644` (dus alleen de geluidsuitgang overslaan), plus breekpunten op `0x8059669`, `0x80596a6`, `0x80596a4`, `0x80596cb`.

**Uitkomst (gemeten, letterlijk):**

```
[GELUID] vroege uitgang NA install_sound-fout bereikt
[GELUID] allegro_error = 0x80bb910 <allegro_error>:	"Insufficient digital voices available"
[GELUID] spelvlag 0x80bc9f8 = 0x80bc9f8 <MainHiState>:	0x0000000f
[PROEF] uitgang eenmalig overgeslagen; set_gfx_mode wordt nu geprobeerd
X Error of failed request:  BadAccess (attempt to access private resource denied)
  Major opcode of failed request:  130 (MIT-SHM)
  Minor opcode of failed request:  1 (X_ShmAttach)
  Serial number of failed request:  34
  Current serial number in output stream:  35

Thread 1 "bowling_usa" received signal SIGSEGV, Segmentation fault.
0xf7e75bde in set_gfx_mode () from /rg/lib/liballeg.so.4.0
#0  0xf7e75bde in set_gfx_mode () from /rg/lib/liballeg.so.4.0
#1  0xf7e39408 in ?? () from /rg/lib/liballeg.so.4.0
#2  0xf7e344f8 in ?? () from /rg/lib/liballeg.so.4.0
Backtrace stopped: previous frame inner to this frame (corrupt stack?)
eax            0x0                 0
=> 0xf7e75bde <set_gfx_mode+270>:	mov    0x24(%edx),%edi
   0xf7e75be1 <set_gfx_mode+273>:	call   *%edi
```

Dit is een **ingreep** (ik heb de processtroom met de hand omgeleid), dus geen gewone route-meting, maar wel een echte meting aan deze sandbox. Twee bevindingen:

1. **De exacte geluidsfout is `"Insufficient digital voices available"`** (Allegro's tekst; de letterlijke tekenreeks `"Insufficient %s voices available"` staat in `liballeg.so.4.0`, gemeten). Dat past bij de drie `ENOENT`-openingen op `/dev/dsp`, `/dev/mixer`, `/dev/sequencer`: er zijn **0** digital voices.
   *Aanvulling:* de game zet ook de vlag `MainHiState` (`0x80bc9f8`) op `0xf` (gemeten: `0x0000000f`).
2. Zodra je `set_gfx_mode` wél laat lopen, loopt de XWIN-driver tegen **MIT-SHM** aan: `XShmAttach` → `BadAccess`, en deze `liballeg`-bouw **crasht daarop** in `set_gfx_mode+270` (`mov 0x24(%edx),%edi; call *%edi`) met `eax = 0`.

**Waarom MIT-SHM hier niet kan (gemeten + *afgeleid*):**

* Gemeten: de sandbox draait met `--unshare-ipc` (droogloop, §1).
* Gemeten: `liballeg.so.4.0` importeert `shmget`, `shmat`, `XShmAttach`, `XShmCreateImage`, `XShmPutImage`, `XShmQueryExtension`, `XShmDetach` — de XWIN-driver gebruikt dus echt SysV-gedeeld geheugen.
* Gemeten: System V-gedeeld geheugen is per IPC-namespace gescheiden. Buiten bwrap kreeg een nieuw segment `shmid 61` (key `0x5138acff`); binnen `bwrap --unshare-ipc` kreeg het nieuwe segment `shmid 0` en het hostsgment `61` was daar **niet** zichtbaar, en omgekeerd.
* *Afgeleid (sterk):* de X-server (Xwayland van gamescope) leeft in de IPC-namespace van de host en kan het segment van het spel in de sandbox-namespace niet aankoppelen → `X_ShmAttach` → `BadAccess`.

**Dus:** in de **gewone** route wordt de grafische modus nooit geprobeerd; in een proef waarin je het geluid omzeilt, blijkt dat de grafische weg in deze sandbox een **tweede, zelfstandige hobbel** heeft (MIT-SHM/IPC-namespace), die in deze Allegro-bouw zelfs een SIGSEGV in `set_gfx_mode` oplevert.

---

## 6. Stap 5 — de PAOKAI-apparaten `/dev/w040101_*`

**Wat de game vraagt (gemeten, statisch):** padstrings in `bowling_usa`:

```
/dev/w040101sram0      (via lpSramDeviceOpen, met bronbestand "sys/c/sram.c")
/dev/w040101_ctl
/dev/w040101_io0  io1  io2
/dev/w040101_io101 io104 io105 io106
/dev/mixer  /dev/dsp  /dev/sequencer  /dev/ttyS0  /dev/ttyS1
```

De `_io*`-paden worden gepusht op `0x806f9b8`, `0x806f9d9`, `0x806f9fa` (uit `vKeyIoDeviceOpen`) en `0x807bf1a`, `0x807bf3a`, `0x807bf5a`, `0x807bf78` (gemeten via kruisverwijzingen op de `push`-instructies).

**Is dat fataal? (gemeten)** In deze route is het **niet aan de orde**: de game komt er niet, want `vKeyIoDeviceOpen` en `vQlDeviceOpen` liggen achter dezelfde vroege uitgang als `lpSramDeviceOpen`. Het strace-spoor bevat **nul** openingen op `/dev/w040101*`. De eerdere observatie "die opens zijn niet-fataal" kan ik dus niet bevestigen of weerleggen — ze vinden niet plaats. Wat ik wél meet: op deze host **bestaan** `/dev/w040101*` helemaal niet (§1), dus zonder synthetische apparaten of binds kan de game ze ook in geen enkele route openen.

---

## 7. Conclusie — de eerstvolgende blokkade

**Meetkundig (alles hierboven) is de eerstvolgende blokkade: `install_sound` in `vSystemDeviceInit`.**

Niet de grafische modus, niet de bibliotheken, niet het SRAM-bestand, niet de dll/hints-kant van de lader. De sandbox geeft het spel geen OSS-geluid (`/dev/dsp`, `/dev/mixer`, `/dev/sequencer` → `ENOENT`), de game breekt daarop zijn eigen apparaatinitialisatie af, en de daaropvolgende onvoorwaardelijke `uwSramCheckFormatCode()` loopt tegen een nulwijzer aan. De crashlocatie (`uwSramCheckFormatCode+5`) is dus **een gevolg**, geen oorzaak.

**Daarachter, in deze volgorde:**

1. **Geluid.** Er moet een werkende `/dev/dsp`-achtige laag komen. Drie sporen:
   * **alsa-oss via LD_PRELOAD.** In `@tpui/ElfLdr2/libs` staan `libaoss.so.0.0.0` (26 092 B) en `libalsatoss.so.0.0.0` (46 872 B); `libaoss` heeft `NEEDED libalsatoss.so.0` + `libc.so.6` (gemeten). Op de host is `/usr/lib32/libasound.so.2.0.0` aanwezig (gemeten). Let op: `play-native.sh` **slaat `LD_PRELOAD` over zodra `--strace` of `--gdb` meedoet** (regel 303) — voor een gdb-meting moet de preload dus via `--env LD_PRELOAD=…` of via een eigen bind. Ook moet `libalsatoss.so.0` onder díe naam in `/rg/lib` staan (de map bevat `libalsatoss.so.0.0.0`, geen `.so.0`); met `--bind-ro …/libalsatoss.so.0.0.0 /rg/lib/libalsatoss.so.0` kan dat.
   * **Eigen OSS-stub** (past bij de filosofie van het project): een i386-shim die `open`/`ioctl` op `/dev/dsp`, `/dev/mixer` en `/dev/sequencer` afvangt en `install_sound` laat slagen. De game reserveert eerst `reserve_voices(16, -1)`, dus de stub moet minstens 16 digital voices melden.
   * **Patch op het spel**: de vroege uitgang op `0x805963f` (`jmp 0x8059716`) of de `je` op `0x805961e` onschadelijk maken. Dat is de kleinste wijziging, maar het verbergt de geluidsfout in plaats van hem op te lossen; en het brengt je direct bij hobbel 2.
2. **MIT-SHM in de sandbox.** `--unshare-ipc` maakt dat de X-server het geheugensegment van het spel niet kan aankoppelen; deze `liballeg`-bouw crasht daarop (gemeten, §5). Een **eenmalige proef zonder `--unshare-ipc`** (of met een aangepast `--ro-bind`, of een XWIN zonder SHM) hoort hier de volgende meting te zijn. Let op: `--unshare-ipc` weghalen is een wijziging in `play-native.sh`, niet iets wat het profiel kan.
3. **Het SRAM-apparaat schrijfbaar binden op `/dev/w040101sram0`.** `lpSramDeviceOpen` doet `open(O_RDWR)` + `mmap(…, PROT_READ|PROT_WRITE, MAP_SHARED, …)`; een `--ro-bind` is dus onvoldoende. `play-native.sh` heeft (zoals het plan al meldde) **alleen** `overlay`/`ro`-mounts van mappen en geen **rw-bestandsbind** — dat is een echte uitbreiding van het script.
   Inhoudelijk: het voorbereide `~/.local/share/ravengun/native/GoGoStrike/w040101sram` (65 536 B) is **volledig nul** (gemeten); de indelingscode `0x069dc065` staat daar niet, maar in de oude BudgieLoader-versie in de view (`…/views/GoGoStrike/w040101sram`, 65 537 B) op offset `0xb000` — precies de plek die `uwSramCheckFormatCode` leest (`base+0xb000`). Bind je het nul-bestand, dan meldt de game "niet geformatteerd" en neemt hij de `vSramClearAll(); vSystemGameDataSramWrite(); vSystemStateDataSramWrite()`-tak. Dat is waarschijnlijk precies goed voor een eerste start, maar het is een keuze die je bewust moet maken.

---

## 8. Wat open blijft

* **Werkt XWIN echt op 640×480×16 bit in deze sandbox?** Ik heb alleen kunnen vaststellen dat de X-verbinding lukt, dat `set_gfx_mode` in de gewone route nooit loopt, en dat de driver in de proef op `X_ShmAttach`/`BadAccess` struikelt. Zonder MIT-SHM is het beeldgedrag ongemeten. Een `xdpyinfo`- of tekenproef *binnen* de sandbox heb ik niet kunnen draaien: `play-native.sh` start alleen het spel.
* **Of `BadAccess` op `XShmAttach` werkelijk door `--unshare-ipc` komt.** De namespace-scheiding en het gebruik van XShm zijn gemeten; het causale verband is een sterke afleiding, geen directe meting. Een proefrun zonder `--unshare-ipc` beslecht het.
* **Of de SIGSEGV in `set_gfx_mode` (proef) door de X-fout komt of door mijn `set $pc`-omleiding.** De omleiding gebeurde op een plek met gebalanceerde stack, maar dit blijft een ingreep; de crash zit in `liballeg` (`set_gfx_mode+270`, `call *%edi` met `eax=0`).
* **Wat `install_sound` precies nodig heeft om te slagen.** Bekend: `reserve_voices(16, -1)` en `install_sound(-1, -1, argv[0])`, en de OSS-openingen. Niet getest: of `libaoss` hier werkt, en of de rechten toereikend zijn (`deck` zit in de groepen `1000(deck), 973(steamos-log-submitter), 998(wheel)` — **niet** in `audio` of `video`; `/dev/snd/controlC0` is `crw-rw---- root:audio` en `/dev/dri/card0` is `crw-rw---- root:video`, beide met een ACL-vlag `+` in de `ls`-uitvoer, dus of de ACL `deck` toelaat is niet gemeten).
* **De PAOKAI-apparaten.** Niet bereikt in deze route (nul opens). Op deze host bestaan ze niet. Hun werkelijke gedrag (fataal of niet) blijft ongemeten tot de apparaatinitialisatie loopt.
* **In headless-modus wordt de pulse/pipewire-socket niet gebonden** (`play-native.sh` regel 286: `if (( ! MUTE && ! HEADLESS ))`). Dat is voor het geluidsspoor hierboven van belang: een oplossing via PipeWire in plaats van ALSA zou eerst die regel moeten raken.
* **`/pm/patches.dat`** wordt gevraagd en is afwezig (`ENOENT`); wat de game daarmee doet is niet onderzocht.
* De regel `play-native: GoGoStrike beëindigd met exitcode 139` en het Allegro-signaalbericht `Shutting down Allegro due to signal #11` zijn **niet** hetzelfde als de procesafsluiting van het spel: het spel wordt door Allegro's handler opgeruimd en sterft alsnog aan SIGSEGV (`+++ killed by SIGSEGV (core dumped) +++` in het strace-spoor).

---

## Bijlage — waar staat wat

| Bestand | Rol |
|---|---|
| `/home/deck/.local/state/ravengun/logs/native-GoGoStrike-20261012-185307.log` | kale run (stap 1) |
| `…-185320.log` | strace-run (stap 2) |
| `…-185338.log` | standaard `--gdb` (stap 3a) |
| `…-185700.log` | gdb met breekpunten (stap 3b) |
| `…-185838.log` | gfx-proef met `set $pc` (stap 4) — bevat twee gdb-sessies, want `play-native.sh` voegt na mijn `-x`-bestand nog `-ex run` toe |
| `/home/deck/_gogostrike-strace.txt` | volledig strace-spoor (55 regels) |
| `/home/deck/_gogostrike-objdump.txt` | volledige demontage van `bowling_usa` (103 560 regels) |
| `/home/deck/_gogostrike-gdb.cmds`, `_gogostrike-gfxproef.cmds` | gdb-opdrachtbestanden |
| `/home/deck/_gogostrike-*.sh` | meetcripts (recon, dryrun, strings, run, strace, gdb, xref, dis, sramwho, sramcallers, devinit, gfxproef, hostsound, ipcns, sraminhoud) |

**Harde regels nageleefd:** één spel tegelijk (vóór elke run `pgrep -a wine` en `pgrep -af 'bowling_usa|play-native'`, alle keren leeg); uitsluitend `--headless`, geen `--gamescope`; geen Wine, geen BudgieLoader, geen TeknoParrotUi gestart; niets naar de NAS geschreven (alle schrijfacties van het spel gaan naar de overlay `~/.local/share/ravengun/native/GoGoStrike/0/upper`; het `bowling.log` dat daar staat is 0 bytes omdat de stdio-buffer bij de SIGSEGV verloren gaat); geen `git commit/push/checkout/stash`; geen dongle-, HASP-, keychip- of integriteitswerk.
