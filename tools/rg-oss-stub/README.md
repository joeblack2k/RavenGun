# rg-oss-stub — i386 OSS-shim voor de Allegro-4-games in de native route

Een dunne `LD_PRELOAD`-bibliotheek die `/dev/dsp` en `/dev/mixer` nadoet, zodat
`install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, …)` van Allegro 4 slaagt in de bubblewrap-sandbox van
`scripts/play-native.sh`. Zonder deze shim (of een equivalent) faalt het geluid, springt GoGoStrike naar
zijn vroege uitgang (`0x805963f` → `jmp 0x8059716`), slaat het `lpSramDeviceOpen` over en crasht het
daarna op `0x08071915` (`uwSramCheckFormatCode+5`, `eax = 0`). Zie
`docs/research/gogostrike-native-meting.md` en `/home/deck/_gogostrike-hobbel2.md`.

## Bouwen

```sh
bash tools/rg-oss-stub/gen.sh     # gcc -m32 -shared -fPIC -O2 -o librg-oss-stub.so rg_oss_stub.c
```

De bron is **header-vrij** (alleen `<stdarg.h>` van de compiler, de rest zelf gedeclareerd, syscalls via
`syscall()`). Reden: op deze machine ontbreken de 32-bits glibc-ontwikkelheaders — met `-m32` stopt gcc op
`/usr/include/gnu/stubs-32.h: No such file or directory`, en dan bouwt een shim met `<dlfcn.h>`/`<stdio.h>`
niet. Zo bouwt het met kaal `gcc -m32 -shared -fPIC`.

## Laden

`play-native.sh` kent geen preload-vlag voor een los bestand, maar `--bind-ro` + `--env` zijn genoeg:

```sh
scripts/play-native.sh --game GoGoStrike --headless --seconds 30 \
  --bind-ro "$PWD/tools/rg-oss-stub/librg-oss-stub.so" /rg/lib/librg-oss-stub.so \
  --env LD_PRELOAD=/rg/lib/librg-oss-stub.so \
  --env RG_OSS_STUB_LOG=1
```

`RG_OSS_STUB_LOG=1` zet een regel per afgevangen ioctl in het logboek (max 3 per verzoeknummer).
`RG_OSS_STUB_SEQ=1` nebootst ook `/dev/sequencer` na — zie hieronder waarom dat standaard **uit** staat.

## Gemeten uitkomst (2026-10-12, Deck)

`bowling_usa` in de proefkopie van de launcher zonder `--unshare-ipc`, met de stub en een schrijfbare
SRAM-bind, **zonder** libaoss, **zonder** pipewire-socket en zonder echte geluidskaart:

* de stub meldt `nep-open mixer`, `nep-open dsp` en `ioctl dsp req=0x8004500b|0xc004500a|0xc0045005|0xc0045003|0xc0045002|0x8010500c`;
* in het strace-spoor staat **geen** `/dev/dsp` of `/dev/mixer` meer (alleen `/dev/sequencer`, met opzet doorgelaten);
* het spel opent daarna `/dev/w040101sram0` (O_RDWR) en blijft **30 s** in leven (exit 143 = SIGTERM van de
  tijdslimiet), toestand `R`, 405 CPU-ticks per 4 s;
* de schermopname is byte-identiek (md5 `4148d0e073084369258b2655bf027b7c`) aan dezelfde run met libaoss.

## Waarom `/dev/sequencer` standaard niet wordt nagebootst

Dat is een meting, geen luiheid:

* libaoss vangt alleen `/dev/dsp` en `/dev/mixer` af en laat `/dev/sequencer` met rust (ENOENT). Daarmee
  geeft `install_sound` **0** terug: Allegro's OSS-MIDI-driver kan de sequencer niet openen en Allegro valt
  terug op zijn eigen **digmid**-driver (software-MIDI over de digitale stemmen).
* Bootst de stub `/dev/sequencer` wél na, dan neemt Allegro's OSS-MIDI-driver het over en eindigt
  `install_sound` op **-1** met `allegro_error = "No support synth type found"` (gemeten met gdb op
  `0x805961c`). Het spel gaat dan alsnog naar zijn vroege uitgang en crasht op `0x08071915`.

De stub meldt de sequencer dus alleen en laat hem aan de kernel (`ENOENT`).

## Afspelen (RG_OSS_PLAY=1, 2026-10-18)

Zonder `RG_OSS_PLAY` levert de stub **geen geluid**: de DSP is `/dev/null` en de ioctl-antwoorden zijn vaste, plausibele
waarden (genoeg voor GoGoStrike, dat alleen zijn initialisatie moet afmaken). Met `RG_OSS_PLAY=1` speelt hij echt af, voor
de games met de Raw Thrills-geluidscode (`jpsdrvr_init`/`jpsdrvr_proc`: Target: Terror Gold, Jurassic Park):

* de shim onthoudt SPEED/SETFMT/CHANNELS/SETFRAGMENT (TTG: 22050 Hz, S16LE, mono, 3 x 4096) en negeert de mixer-ioctls
  (`'M'`) die het spel op de DSP-fd doet;
* `write()` op de DSP-fd gaat in een ring; een eigen draad speelt de ring af via `libpulse-simple.so.0` (dlopen, 32-bit;
  `PULSE_SERVER` wijst in de bwrap naar de pulse-socket van de host). De stroom heet `RG_OSS_NAME` (anders "OSS /dev/dsp");
* `GETOSPACE` meldt de werkelijk vrije ruimte van de ring, dus het tempo van het spel loopt op het afspeeltempo (TTG schrijft
  een fragment zodra er een vrij is). De ring houdt hoogstens `RG_OSS_FRAGS` (standaard 2) fragmenten vast: lage vertraging;
* is er geen PulseAudio, dan loopt de draad op een virtuele klok: de pacing klopt, er komt alleen geen geluid.

Toets: `tests/oss_stub_test.sh` (ctest `ravengun_oss_stub`) en `oss_probe.c` hier, dat de init- en schrijflus van TTG nabootst:
`gcc -m32 -O2 -o oss_probe oss_probe.c -lm && LD_PRELOAD=./librg-oss-stub.so RG_OSS_PLAY=1 RG_OSS_STUB_LOG=1 ./oss_probe 4 1500`
(seconden, amplitude); in `pactl list sink-inputs` staat dan een stroom van 1 kanaal / 22050 Hz.

## Wat de stub niet doet

De PAOKAI-apparaten (`/dev/w040101_*`) en de seriële poorten (`/dev/ttyS0`) vallen buiten deze stub; die
zijn de volgende hobbel (zie `/home/deck/_gogostrike-hobbel2.md`, sectie "Wat open blijft"). Opnemen (`/dev/dsp` lezen) en mmap-trigger zijn niet nagebootst.
De PAOKAI-apparaten (`/dev/w040101_*`) en de seriële poorten (`/dev/ttyS0`) vallen buiten deze stub; die
zijn de volgende hobbel (zie `/home/deck/_gogostrike-hobbel2.md`, sectie "Wat open blijft").
