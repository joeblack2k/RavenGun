# De dynsym van de gepakte ElfLdr2-games — de meting

*RavenGun, ronde 12 (2026-10-11). Dit is het bewijs onder `docs/hasp-elf-hook.md` §1. Alle getallen
zijn gemeten op de bestanden op de NAS; de weg erheen is `tools/rt-hasphook/dynsym.py`.*

## Het probleem met de gewone gereedschappen

De `game`-bestanden zijn door de pakker herschreven en hebben **geen sectiekoppen** meer:

```
$ readelf -h "…/Jurassic Park/game" | grep -i 'section header'
readelf: Error: no section header
$ readelf --dyn-syms "…/Jurassic Park/game"
readelf: Error: no section header
```

BudgieLoader meldt hetzelfde bij het laden:

```
Warning SHT_DYNSYM not found in section headers
Warning aborting symbol parsing due to invalid name ptr
```

`nm -D` en `readelf --dyn-syms` werken dus niet. De dynamische tabel staat er echter nog: hij hangt
aan `PT_DYNAMIC`, en de koppeling van deze ronde leest hem op die manier — zowel van schijf (om de
adressen te vinden) als in het geheugen.

## De meting per game

`python3 tools/rt-hasphook/dynsym.py --hasp <game>`

### Jurassic Park (`game`, 32-bits, 17.694.720 bytes)

```
klasse 32-bits, type 2 (EXEC), machine 3 (i386), 7 program headers
  PT_LOAD 0x08048000 offset 0x000000 0xa94000 bytes RWE
  PT_LOAD 0x08adc000 offset 0xa94000 0x64c000 bytes RWE
  DT_HASH      0x8fc90c0 -> offset 0xf810c0
  DT_STRTAB    0x80b9bb0 -> offset 0x71bb0
  DT_SYMTAB    0x806e680 -> offset 0x26680
  DT_JMPREL    0x81384d8 -> offset 0xf04d8
  DT_PLTRELSZ  0x328   (808 bytes = 101 verplaatsingen)
  DT_PLTGOT    0x8add000
aantal dynamische symbolen: 18811
30 hasp-symbolen, alle FUNC, shndx 11, st_value != 0
```

De 30 namen met hun adres (dit zijn de adressen die de koppeling aanhaakt):

| naam | waarde | grootte |
|---|---|---|
| `hasp_login` | `0x08529650` | 103 |
| `hasp_login_scope` | `0x085282b0` | 144 |
| `hasp_login_ex` | `0x08528340` | 5 |
| `hasp_logout` | `0x085295f0` | 90 |
| `hasp_read` | `0x08528520` | 174 |
| `hasp_write` | `0x08528470` | 176 |
| `hasp_encrypt` | `0x08529550` | 125 |
| `hasp_decrypt` | `0x085294d0` | 125 |
| `hasp_get_size` | `0x085283d0` | 154 |
| `hasp_get_info` | `0x08528090` | 524 |
| `hasp_get_sessioninfo` | `0x08528670` | 649 |
| `hasp_free` | `0x085282a0` | 5 |
| `hasp_cleanup` | `0x085295d0` | 29 |
| `hasp_detach` | `0x08527ac0` | 336 |
| `hasp_echo` | `0x08529470` | 89 |
| `hasp_enable_trace` | `0x08527ab0` | 6 |
| `hasp_get_rtc` | `0x08528350` | 118 |
| `hasp_get_trace` | `0x08528660` | 15 |
| `hasp_update` | `0x08528900` | 2913 |
| `hasp_datetime_to_hasptime` | `0x085285d0` | 120 |
| `hasp_hasptime_to_datetime` | `0x08528650` | 5 |
| `hasp_legacy_decrypt` | `0x08534c10` | 137 |
| `hasp_legacy_encrypt` | `0x08534ca0` | 137 |
| `hasp_legacy_set_idletime` | `0x085349e0` | 138 |
| `hasp_legacy_set_rtc` | `0x08534a70` | 402 |
| `hasp_monster_get_lms_version` | `0x08531340` | 296 |
| `__hasp_enxml_ex` | `0x08529890` | 376 |
| `__hasp_monster_detach` | `0x0852e9d0` | 644 |
| `_hasp_version_string` | `0x08b38ea0` | 33 |
| `comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1` | `0x09048d30` | 123 |

### Galaga Assault (`game`, 32-bits, 7.278.592 bytes)

```
klasse 32-bits, type 2 (EXEC), machine 3 (i386), 7 program headers
  PT_LOAD 0x08048000 offset 0x000000 0x2c3000 bytes RWE
  PT_LOAD 0x0830b000 offset 0x2c3000 0x42e000 bytes RWE
  DT_HASH      0x86351c8 -> offset 0x5ed1c8
  DT_STRTAB    0x806511c -> offset 0x1d11c
  DT_SYMTAB    0x8051a0c -> offset 0x9a0c
  DT_JMPREL    0x807f840 -> offset 0x37840
  DT_PLTRELSZ  0x320   (800 bytes = 100 verplaatsingen)
  DT_PLTGOT    0x830d000
aantal dynamische symbolen: 4719
28 hasp-symbolen, alle FUNC, shndx 11
```

| naam | waarde | grootte |
|---|---|---|
| `hasp_login` | `0x08227970` | 124 |
| `hasp_login_scope` | `0x08225e00` | 186 |
| `hasp_login_ex` | `0x08225ec0` | 72 |
| `hasp_login_port` | `0x082278b0` | 177 |
| `hasp_logout` | `0x08227840` | 105 |
| `hasp_read` | `0x08226150` | 210 |
| `hasp_write` | `0x08226070` | 216 |
| `hasp_encrypt` | `0x08227770` | 153 |
| `hasp_decrypt` | `0x082276d0` | 153 |
| `hasp_get_size` | `0x08225fb0` | 190 |
| `hasp_get_info` | `0x08225bb0` | 592 |
| `hasp_get_sessioninfo` | `0x08226380` | 631 |
| `hasp_free` | `0x08224fd0` | 31 |
| `hasp_transfer` | `0x08224ff0` | 1230 |
| `hasp_update` | `0x08226670` | 3718 |
| `hasp_detach` | `0x08224c60` | 386 |
| `hasp_cleanup` | `0x08227810` | 37 |
| `hasp_echo` | `0x08227650` | 119 |
| `hasp_enable_trace` | `0x08224c30` | 6 |
| `hasp_get_rtc` | `0x08225f10` | 146 |
| `hasp_get_trace` | `0x08226350` | 37 |
| `hasp_datetime_to_hasptime` | `0x08226230` | 160 |
| `hasp_hasptime_to_datetime` | `0x082262d0` | 125 |
| `hasp_legacy_decrypt` | `0x08231ef0` | 155 |
| `hasp_legacy_encrypt` | `0x08231f90` | 155 |
| `hasp_legacy_set_idletime` | `0x08231cb0` | 155 |
| `hasp_legacy_set_rtc` | `0x08231d50` | 414 |
| `comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1` | `0x08659d30` | 123 |

### Halo Fireteam (`g7/halo/game`, 64-bits, 33.554.432 bytes)

```
klasse 64-bits, type 2 (EXEC), machine 62 (x86-64), geen sectiekoppen
aantal dynamische symbolen: 219
3 symbolen met @@HASPE_LINUX_LOADER_1.1:
  closeThread     0x01f6529c
  patchDtInitHook 0x01f65238
  patchInit       0x01f6526e
0 hasp_-namen in de dynsym
```

De HASP-cliënt zit er wel (de tekenreeksen `hasp_info`, `haspformat`, `/dev/aks/sentinelhid`,
`hasp_%d.ini` en de tekst `hasp_cleanup() was called (or the HASP API was unloaded) while a HASP call
was active in another thread.` staan in het bestand), maar de namen zijn niet geëxporteerd. Voor deze
game moeten de adressen dus anders gevonden worden — zie `docs/hasp-elf-hook.md` §6, punt 2.

Ter vergelijking: `game_original` (31.270.111 bytes, in de view) heeft **1** dynamisch symbool en
geen `@@HASPE`-export. De TeknoParrot-lader maakt van de originele game de gepakte `game` van 32 MB
met de envelope erin.

### Terminator Salvation (`game`, 32-bits, 7.764.284 bytes)

```
klasse 32-bits, type 2 (EXEC), machine 3 (i386), 13 NEEDED, RPATH /usr/lib/PhysX/v2.8.1/
  .rel.plt  0xf80 bytes = 496 verplaatsingen
  .plt      0x1f10 bytes
aantal dynamische symbolen: 1019
0 hasp-symbolen (ook geen ongedefinieerde)
0 treffers voor @@HASPE
strings: 0 treffers voor "hasp_login"; wel "ReInitAladdinDongles called: %d times"
```

Er zit **geen** HASP-cliënt in dit spel. Zie `docs/hasp-elf-hook.md` §5.

## Wat dit betekent

Voor Jurassic Park en Galaga Assault is de cliënt statisch gelinkt én draagt hij zijn namen in de
dynsym. Dat is precies wat een inline-hook nodig heeft: een naam en een adres. Voor Halo Fireteam is
de cliënt er wel maar zonder namen; voor Terminator is hij er niet.

De aanwezigheid van `comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1` in de eerste twee is het
bewijs dat de Sentinel LDK HASP Envelope de runtime in het programma heeft opgenomen. Daarmee is
"statisch gelinkt" geen vermoeden meer maar een gemeten eigenschap van de pakker.
