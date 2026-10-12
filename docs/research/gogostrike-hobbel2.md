# GoGoStrike (`bowling_usa`) in de native route — de drie hobbels, stap voor stap

**Meting op:** SteamOS-Deck `192.168.2.247`, gebruiker `deck`, 2026-10-12 19:05–19:20 CEST.
**Route:** `~/projects/ravengun/scripts/play-native.sh --game GoGoStrike --headless` (bubblewrap), **geen Wine,
geen BudgieLoader, geen TeknoParrotUi**. `scripts/play-native.sh` is **niet** gewijzigd
(`git diff -- scripts/play-native.sh` → leeg); de eenmalige proeven draaien op de kopie
`/home/deck/_gn-noshm.sh`. Vóór elke run: `pgrep -a wine` en `pgrep -af 'bowling_usa|play-native'` → alle
keren leeg (één spel tegelijk). Alleen `--headless`, geen `--gamescope`.

In dit document is **gemeten** iets wat een commando op deze machine opleverde; **afgeleid** is mijn
gevolgtrekking daaruit.

## 0. Samenvatting

| hobbel | stand | hardste bewijs |
|---|---|---|
| 1. geluid: `install_sound` faalt (`"Insufficient digital voices available"`) | **weg** | (a) libaoss + pipewire-socket: de crash verschuift van `uwSramCheckFormatCode` (`si_addr=NULL`) naar `set_gfx_mode+270` (`si_addr=0x24`); (b) eigen stub `tools/rg-oss-stub`: `openat("/dev/w040101sram0", O_RDWR) = 5`, spel leeft 30 s |
| 2. MIT-SHM: `BadAccess` op `MIT-SHM X_ShmAttach` | **weg op de proefkopie zonder `--unshare-ipc`** | geen enkele X-foutregel meer in het logboek; het spel loopt door tot het SRAM-apparaat |
| 3. `/dev/w040101sram0` schrijfbaar binden | **weg met `--bind-rw`** (eigen toevoeging op de kopie) | `openat(…, O_RDWR) = 17` en `/proc/<pid>/maps`: `rwxs … /dev/w040101sram0` |

**Succescriterium (uit het plan):**

1. `bowling_usa` start via onze eigen route, zonder BudgieLoader en zonder Wine — **ja** (gemeten, zie §2–§4).
2. Het proces is gezien én draait nog na minimaal 20 seconden — **ja**: `pgrep -x bowling_usa` → `303879 /pm/bowling_usa`,
   toestand `R`, CPU-tellingen 776 → 1178 ticks in 4 s (≈ 100 % van één kern, dus een echte lus), en het
   proces sterft pas op de tijdslimiet van de launcher (`== exit 143`, 128+15 = SIGTERM) na 30 s.
3. Een uitspraak over het beeld — **ja**, met opname: `_gogo-h2-snap-zero.png`, 1920×1080 RGBA, met daarop
   de **eigen assertscheur van het spel**: `ASSERT FAIL / COND: p100>=0 / FILE: sys/c/ql.c / LINE: 80`.

Het spel komt dus voorbij alle drie de hobbels; wat het daarna tegenhoudt staat in §5.

## 1. Stap 0 — recon (read-only, geen spel gestart)

```sh
bash /home/deck/_gogo-h2-recon.sh
```

Gemeten uitkomsten die de rest verklaren:

* **32-bits bouwen kan**: `printf 'int x;\n' > /tmp/_h2_t.c && gcc -m32 -shared -fPIC /tmp/_h2_t.c -o /tmp/_h2_t.so`
  → `ELF 32-bit LSB shared object, Intel i386` (gcc 16.1.1, binutils 2.46.1, `/usr/lib32/crt1.o` aanwezig).
* **De aoss-bibliotheken staan er**: `…/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs/libaoss.so.0.0.0`
  (26 092 B) en `…/libalsatoss.so.0.0.0` (46 872 B); `readelf -d libaoss` → `NEEDED libalsatoss.so.0`,
  `libc.so.6`; `readelf -d libalsatoss` → `NEEDED libasound.so.2`. `/usr/lib32/libasound.so.2.0.0` bestaat.
* **Geen OSS-apparaten**: `/dev/dsp`, `/dev/mixer`, `/dev/sequencer` → `ontbreekt`.
* **Wel `/dev/snd`** met ACL's; `deck` zit in `1000(deck),973(steamos-log-submitter),998(wheel)` (niet in `audio`),
  maar `controlC0` is voor `deck` rw te openen. De ALSA-standaard verwijst naar **pipewire**
  (`/etc/alsa/conf.d/50-pipewire.conf`, `99-pipewire-default.conf`).
* **Wat liballeg zelf opent**: `strings liballeg.so.4.0 | grep '^/dev/'` → o.a. `/dev/dsp`, `/dev/mixer`,
  `/dev/sequencer`.
* **Wat de launcher kan**: `--bind-ro S D` en `--env K=V` (geen rw-bind), en de sandbox bevat
  `--unshare-ipc` (regel 171 van het origineel).

## 2. Stap 1 — het geluid laten slagen

### 2a. `libaoss` als goedkope proef (zonder pipewire-socket) — faalt, maar meetbaar

```sh
bash ~/projects/ravengun/scripts/play-native.sh --game GoGoStrike --headless --seconds 25 \
  --bind-ro $TP/ElfLdr2/libs/libaoss.so.0.0.0  /rg/lib/libaoss.so.0.0.0 \
  --bind-ro $TP/ElfLdr2/libs/libalsatoss.so.0.0.0 /rg/lib/libalsatoss.so.0 \
  --env LD_PRELOAD=/rg/lib/libaoss.so.0.0.0 \
  --strace /home/deck/_gogo-h2-aoss-strace.txt
```

Uitkomst (gemeten): `grep -c '/dev/dsp'` in het spoor = **0** (de preload werkt dus in het 32-bits spel),
maar ALSA lost zijn standaardapparaat op naar de pipewire-plugin:

```
openat(AT_FDCWD, "/etc/alsa/conf.d/50-pipewire.conf", O_RDONLY) = 4
openat(AT_FDCWD, "/usr/lib32/alsa-lib/libasound_module_ctl_pipewire.so", …) = 4
connect(11, {sa_family=AF_UNIX, sun_path="/run/user/1000/pipewire-0"}, 28) = -1 ENOENT
```

→ `== exit 139`, `SIGSEGV SEGV_MAPERR si_addr=NULL`: nog steeds de oude crash. Oorzaak: `--headless` bindt de
pipewire-socket niet (`play-native.sh` regel 286: alleen buiten `--headless` én niet bij `--mute`).

### 2b. `libaoss` + de socket gebonden — hobbel 1 wijkt (proefroute)

```sh
bash ~/projects/ravengun/scripts/play-native.sh --game GoGoStrike --headless --seconds 25 \
  --bind-ro $TP/ElfLdr2/libs/libaoss.so.0.0.0  /rg/lib/libaoss.so.0.0.0 \
  --bind-ro $TP/ElfLdr2/libs/libalsatoss.so.0.0.0 /rg/lib/libalsatoss.so.0 \
  --bind-ro /run/user/1000/pipewire-0 /run/user/1000/pipewire-0 \
  --env LD_PRELOAD=/rg/lib/libaoss.so.0.0.0 --strace /home/deck/_gogo-h2-aoss2-strace.txt
```

Uitkomst (gemeten): `connect(…, "/run/user/1000/pipewire-0", 28) = 0` — en de **crash verandert van plaats**:

```
X Error of failed request:  BadAccess … Major opcode: 130 (MIT-SHM) … Minor opcode: 1 (X_ShmAttach)
SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_MAPERR, si_addr=0x24}
== exit 134
```

`si_addr=0x24` is precies `mov 0x24(%edx),%edi` uit `set_gfx_mode+270` met `edx=0` (de crash die het
eerdere onderzoek al als hobbel 2 beschreef). **Afgeleid (sterk):** `install_sound` is nu geslaagd, want het
spel komt voorbij de geluidstak tot in `set_gfx_mode`; de oude crash (`uwSramCheckFormatCode`, `si_addr=NULL`)
is verdwenen. **Hobbel 1 = weg** langs deze route. Nadeel: dit eist de pipewire-socket van de eigenaar en er
komt echt geluid uit.

### 2c. Eigen i386-OSS-stub als blijvende oplossing — `tools/rg-oss-stub/`

* **Waar:** `~/projects/ravengun/tools/rg-oss-stub/rg_oss_stub.c` (+ `gen.sh`, `README.md`, gebouwd
  `librg-oss-stub.so`). Alles nieuw en **untracked**; er is niets gecommit.
* **Bouwen:** `bash tools/rg-oss-stub/gen.sh` → `gcc -m32 -shared -fPIC -O2` → `ELF 32-bit LSB shared object,
  Intel i386`; exporteert `open, open64, openat, openat64, ioctl, close`.
* **Laden** (met de bestaande opties van de launcher, geen scriptwijziging):
  `--bind-ro …/librg-oss-stub.so /rg/lib/librg-oss-stub.so --env LD_PRELOAD=/rg/lib/librg-oss-stub.so`.
* **Wat hij doet:** `open`/`openat` op `/dev/dsp` en `/dev/mixer` → een echte `/dev/null`-fd; de ioctl-verzoeken
  die deze liballeg werkelijk stuurt worden beantwoord (met `objdump -d` gemeten: `0x8004500b` GETFMTS,
  `0xc004500a` SETFRAGMENT, `0xc0045005` SETFMT, `0xc0045003` STEREO, `0xc0045002` SPEED, `0x8010500c`
  GETOSPACE, `0xc0044d00` MIXER_WRITE(volume)). **Geen geluid**: de DSP is stil — genoeg om de initialisatie
  af te maken.
* **De bron is header-vrij.** Nodig, want met `-m32` ontbreken de 32-bits glibc-ontwikkelheaders:
  `fatal error: gnu/stubs-32.h: No such file or directory`. (De stub die een parallelle agent in
  `tools/rg-osstub/` zette, stopt daar precies op; die heb ik daarom niet gebruikt maar wel gemeld.)

**Eerste stub-versie faalde, en dat is een meting waard.** Met een nagebootste `/dev/sequencer` gaf gdb:

```
H2-A: install_sound retour=-1  allegro_error='No support synth type found'
H2-B: GELUID-mislukt-tak gehaald (0x805963f)
H2-F: uwSramCheckFormatCode bereikt (0x8071910)   → SIGSEGV op 0x08071915
```

Dus de **digitale** kant was goed en de **MIDI**-kant niet: libaoss laat `/dev/sequencer` met rust (ENOENT) en
dan valt Allegro terug op zijn eigen `digmid`-driver; bootst de stub de sequencer wél na, dan neemt Allegro's
OSS-MIDI-driver over en faalt `install_sound` met `"No support synth type found"`. De stub laat
`/dev/sequencer` daarom met opzet aan de kernel (melden + `ENOENT`), met `RG_OSS_STUB_SEQ=1` als schakelaar.

**Gemeten uitkomst met de stub** (kopie zonder `--unshare-ipc`, rw-SRAM-bind, **geen** libaoss, **geen**
pipewire-socket, geen echte geluidskaart): zie §4 — het spel opent het SRAM-apparaat, leeft 30 s en levert
een opname die **byte-identiek** is (md5 `4148d0e073084369258b2655bf027b7c`) aan de libaoss-run. In het
spoor staat van de OSS-paden alleen nog `/dev/sequencer … = -1 ENOENT` (met opzet).

**Hobbel 1 = weg**, met twee onafhankelijk gemeten routes.

## 3. Stap 2 — de MIT-SHM-hobbel (eenmalige proef op een kopie)

```sh
cp scripts/play-native.sh /home/deck/_gn-noshm.sh
# daarna op de kopie: --unshare-ipc eruit, ROOT expliciet, en een eigen --bind-rw S D
bash /home/deck/_gogo-h2-maakcopy.sh       # doet precies dat, in 6 gecontroleerde stappen
diff scripts/play-native.sh /home/deck/_gn-noshm.sh
```

De gemeten verschillen (kopie staat **buiten** de repo, vandaar de expliciete `ROOT`):

```
43c43   BINDS=();            →  BINDS=(); RW=();
57a58                        →  --bind-rw) … RW+=("$2" "$3"); shift 2 ;;
171c172 --unshare-pid --unshare-ipc --unshare-uts   →  --unshare-pid --unshare-uts
219a221                      →  for … RW … BW+=(--bind "${RW[$i]}" "${RW[$((i+1))]}")
255a258                      →  zelfde doorgeving aan de gamescope-binnenkant
```

```sh
bash /home/deck/_gn-noshm.sh --game GoGoStrike --headless --seconds 25 \
  --bind-ro $TP/ElfLdr2/libs/libaoss.so.0.0.0  /rg/lib/libaoss.so.0.0.0 \
  --bind-ro $TP/ElfLdr2/libs/libalsatoss.so.0.0.0 /rg/lib/libalsatoss.so.0 \
  --bind-ro /run/user/1000/pipewire-0 /run/user/1000/pipewire-0 \
  --env LD_PRELOAD=/rg/lib/libaoss.so.0.0.0 --strace /home/deck/_gogo-h2-noshm-strace.txt
```

Uitkomst (gemeten): het logboek bevat **geen enkele** X-foutregel (`grep -E 'BadAccess|MIT-SHM|Shm|X Error'` →
niets), het spel loopt door en probeert daarna het SRAM-apparaat:

```
openat(AT_FDCWD, "/dev/w040101sram0", O_RDWR) = -1 ENOENT (No such file or directory)
== exit 143
```

`exit 143` = SIGTERM van de 25-seconden-timer: het proces **leefde nog**, het crashte niet.
**Hobbel 2 = weg** zodra `--unshare-ipc` weg is; daarmee is de eerdere afleiding (IPC-namespace → XShmAttach →
`BadAccess`) **bevestigd**. `scripts/play-native.sh` zelf is ongemoeid gelaten — dit is een bevinding voor de
eigenaar van dat bestand.

## 4. Stap 3 — een rw-bestandsbind voor `/dev/w040101sram0` (beide smaken)

```sh
bash /home/deck/_gogo-h2-s3.sh zero     # ~/.local/share/ravengun/native/GoGoStrike/w040101sram  (65 536 B, volledig nul)
bash /home/deck/_gogo-h2-s3.sh view     # ~/.local/share/ravengun/runtime/views/GoGoStrike/w040101sram (65 537 B)
```

Beide keren werd op een **kopie** van het bestand gebonden (`_gogo-h2-sram-{zero,view}.bin`), zodat het
voorbereide bestand en de view onaangeroerd blijven. Het `--bind-rw`-mechanisme is de eigen toevoeging aan de
kopie uit §3. De gebruikte run (identiek voor beide smaken):

```sh
bash /home/deck/_gn-noshm.sh --game GoGoStrike --headless --seconds 30 \
  --bind-ro …/librg-oss-stub.so /rg/lib/librg-oss-stub.so \
  --bind-rw /home/deck/_gogo-h2-sram-<smaak>.bin /dev/w040101sram0 \
  --env LD_PRELOAD=/rg/lib/librg-oss-stub.so --env RG_OSS_STUB_LOG=1 \
  --snap /home/deck/_gogo-h2-snap-<smaak>.png --strace /home/deck/_gogo-h2-sram-<smaak>-strace.txt
```

Gemeten, in beide smaken **identiek**:

| meting | zero | view |
|---|---|---|
| `openat("/dev/w040101sram0", O_RDWR)` | `= 17` | `= 17` |
| mapping in `/proc/<pid>/maps` | `f4f2d000-f4f3d000 rwxs … /dev/w040101sram0` | `f5101000-f5111000 rwxs … /dev/w040101sram0` |
| proces tijdens de run | `303879 /pm/bowling_usa`, `R`, 776 → 1178 ticks in 4 s | `304368 /pm/bowling_usa`, `R`, 747 → 1151 ticks in 4 s |
| proceseinde | `== exit 143` na 30 s | `== exit 143` na 30 s |
| opname | 1920×1080 RGBA, 21 306 B, md5 `4148d0e0…` | zelfde md5 `4148d0e0…` |
| SRAM-kopie na de run | md5 onveranderd (`fcd6bcb5…`) | md5 onveranderd (`518ad403…`) |

Het verschil tussen de twee smaken is in dit venster dus **niet meetbaar**: geen van beide bestanden wordt
beschreven, de uitkomst en het beeld zijn gelijk. **Afgeleid:** dat past bij de demontage — `uwSramCheckFormatCode`
vergelijkt `[base]` (offset 0 van de mapping) met `0x069dc065`, terwijl de indelingscode in de view op
`0xb000` staat (gemeten: `0000b000: 65c0 9d06`), dus de view ziet er voor díe vergelijking net zo goed
"ongeformatteerd" uit. Welke tak (`vSramClearAll` of niet) genomen is, is **niet gemeten** (geen breekpunt
gezet); wat wél gemeten is, is dat het spel de controle voorbij komt en blijft draaien.

**Hobbel 3 = weg** met een rw-bestandsbind.

## 5. Wat open blijft

1. **De vierde hobbel, precies benoemd (gemeten).** Met alle drie de hobbels weg toont het spel zijn eigen
   assertscheur: `ASSERT FAIL / COND: p100>=0 / FILE: sys/c/ql.c / LINE: 80`, direct nadat
   `openat("/dev/w040101_ctl", O_RDWR|O_NONBLOCK) = -1 ENOENT` mislukte. Een extra, begrensde proef met acht
   rw-gebonden nulbestanden op `/dev/w040101_{ctl,io0,io1,io2,io101,io104,io105,io106}` leert:
   * de ql-assert **wijkt** (alle acht opens slagen: `_ctl = 6`, `io101 = 7`, `io104 = 8`, `io105 = 10`,
     `io106 = 11`, daarna `io0 = 6`, `io1 = 7`, `io2 = 8`);
   * het spel loopt door de hele apparaatinitialisatie tot `openat("/dev/ttyS0", O_RDWR|O_NOCTTY|O_NONBLOCK) = -1 ENOENT`
     en sterft dan aan `SIGSEGV SEGV_MAPERR si_addr=0x6a238abf`, `== exit 139` (geen opname: de `--snap` viel
     na de dood, het PNG is 0 byte).
   → De volgende hobbel is dus de PAOKAI/RS232-kant: die apparaten moeten niet alleen *bestaan* (een gewoon
   bestand is genoeg voor de `>= 0`-controle) maar ook **op ioctl-niveau** antwoorden, en `/dev/ttyS0` moet
   er zijn. Dat is nieuw werk, buiten de drie hobbels van deze opdracht.
2. **De drie oplossingen zitten nog niet in de echte route.** `scripts/play-native.sh` heeft nog
   `--unshare-ipc`, kent geen rw-bestandsbind, en het profiel laadt de stub niet. De exacte wijzigingen die
   ik op de kopie heb gebruikt staan in §3; wie dat bestand bezit kan ze overnemen (of de IPC-isolatie
   anders oplossen — dat is een afweging over de sandbox, niet iets wat ik in de repo zet).
3. **Geluid is stil.** De stub levert geen audio; de libaoss-route levert wél echt geluid maar eist
   `--bind-ro $XDG_RUNTIME_DIR/pipewire-0` (die `--headless` niet bindt) en gebruikt de pipewire-sessie van
   de eigenaar.
4. **Wiring van de stub.** `librg-oss-stub.so` staat als nieuw, untracked artefact in
   `tools/rg-oss-stub/`; `gen.sh` bouwt hem opnieuw. Er is niets gecommit en niets naar de NAS geschreven.
5. **Dubbele stub.** In `tools/rg-osstub/` staat een tweede OSS-stub van een parallelle agent (untracked).
   Die bouwt hier **niet** met `-m32` (`gnu/stubs-32.h` ontbreekt) en is door mij niet gebruikt; de mijne is
   header-vrij en bouwt met kaal `gcc -m32 -shared -fPIC`.
6. **Niet gemeten:** of het spel écht speelt zodra de PAOKAI-apparaten en `/dev/ttyS0` er zijn; of er ooit
   meer dan de assertscheur op het scherm komt; het geluid van het spel zelf; en of de 640×480×16-modus van
   XWIN meer doet dan deze tekst tekenen. Wat wél vaststaat: `set_gfx_mode` **slaagt** nu (het spel tekent in
   zijn eigen venster), waarmee de open vraag uit het eerdere onderzoek ("werkt XWIN op 640×480×16 in deze
   sandbox?") bevestigend beantwoord is zodra MIT-SHM werkt.

## Bijlage — waar staat wat

| Bestand | Rol |
|---|---|
| `tools/rg-oss-stub/rg_oss_stub.c`, `gen.sh`, `README.md` | de blijvende geluidsoplossing (nieuw, untracked) |
| `/home/deck/_gn-noshm.sh` | eenmalige proefkopie van de launcher (zonder `--unshare-ipc`, met `--bind-rw`) |
| `~/projects/ravengun/scripts/play-native.sh` | **ongewijzigd** (`git diff` leeg) |
| `/home/deck/_gogo-h2-*.sh` | meetscripts (recon, s1a, s1a2, alleg, maakcopy, s2, s3, stub2, gdbc1, dev) |
| `/home/deck/_gogo-h2-*-strace.txt` | strace-sporen per stap |
| `/home/deck/_gogo-h2-snap-{zero,view,stub}.png` | schermopnames (assertscheur; `snap-dev.png` is 0 byte) |
| `/home/deck/_gogo-h2-gdbc1.cmds` | gdb-breekpunten op `0x805961c/0x805963f/0x8059644/0x80596a4/0x80596cb/0x8071910` |
| `/home/deck/_gogostrike-hobbel2.md` (dit bestand) | het verslag; kopie in `re/gogostrike-hobbel2.md` op de Windows-machine |

**Harde regels nageleefd:** één spel tegelijk (vóór elke run gecontroleerd, alle keren leeg); uitsluitend
`--headless`; geen Wine, geen BudgieLoader, geen TeknoParrotUi gestart; niets naar de NAS geschreven (het spel
schrijft in zijn overlay `~/.local/share/ravengun/native/GoGoStrike/0/upper`); geen `git commit/push/checkout/stash`
(`git status --porcelain` toont mijn map als `?? tools/rg-oss-stub/`); geen dongle-, HASP-, keychip- of
integriteitswerk.
