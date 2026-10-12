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

## Wat de stub niet doet

Hij levert **geen geluid**: de DSP is `/dev/null` en de ioctl-antwoorden zijn vaste, plausibele waarden.
Voor de native route is dat genoeg — het spel moet zijn initialisatie afmaken, niet hoorbaar zijn.
De PAOKAI-apparaten (`/dev/w040101_*`) en de seriële poorten (`/dev/ttyS0`) vallen buiten deze stub; die
zijn de volgende hobbel (zie `/home/deck/_gogostrike-hobbel2.md`, sectie "Wat open blijft").
