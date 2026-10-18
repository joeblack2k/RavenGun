# Het native profiel (`patches/<Game>/native.json`)

Een game die op Linux draait zonder Wine en zonder BudgieLoader, zoals de Raw Thrills-, Play Mechanix- en Lindbergh-titels,
heeft een native profiel. Dat is één JSON-object per game in `patches/<Game>/native.json`, naast `game.ini`. De daemon leest het
bij elke start uit de app en bouwt er zelf het commando van: `env -i -> gamescope -> bwrap -> [rg-elfloader] -> game`, zonder
shell (`src/ravengun/launch/native.*`).

- **Lagen.** De gebruikerslaag (`~/.local/share/ravengun/patches/<Game>/native.json`) vervangt het meegeleverde bestand als
  geheel; velden worden niet samengevoegd. Een kapot bestand in de gebruikerslaag geeft geen profiel. De daemon valt dan niet
  stil terug op het meegeleverde bestand.
- **Wanneer de daemon het neemt.** Hij neemt de native route alleen bij `"enabled": true` en als de game geen camera in het spel
  gebruikt (`[Camera]` in `game.ini`). Een game zonder enige route start niet; de app meldt dan "nog geen eigen route".
- **Logboek.** Het logboek van de start (`logs/native-<profiel>-<tijd>.log`) noemt het gelezen bestand met sha256 (`gegevens:`).
- **Installatie.** De installatie neemt `native.json` mee (CMakeLists.txt, `tools/dev/patchlayout.py`).

Tot 2026-10-16 stonden alle profielen samen in `packaging/native-games.json`. Elk object daaruit is nu letterlijk zo'n
`native.json`. De standaardresolutie (`defaults.res`) is vast 1920x1080.

## Paden

In `src`, `libs`, `extra_ro`, `binds_rw` en `lindbergh.ini` mogen deze plaatsaanduidingen staan:

| | |
|---|---|
| `@nas` | de map Teknoparrot op de NAS (alleen lezend) |
| `@tpui` | de lokale TeknoParrotUi-map |
| `@data` | `~/.local/share/ravengun` |
| `@lib32` | `@data/runtime/lib32` (i386-bibliotheken) |
| `@build` | onze eigen bibliotheken (`<prefix>/lib` van de installatie, of `build/` in de repo) |
| `@patches` | de meegeleverde `patches/` |

## Velden

| veld | betekenis |
|---|---|
| `title`, `status`, `note` | Tekst voor mensen. Waarden voor `status`: `werkt`, `demo` (alleen de attract-lus, niet speelbaar), `geparkeerd` of `niet-getest`. De reden staat in `note`. |
| `enabled` | `true` = de daemon start de game native. |
| `arch` | `x86_64` of `i386`. |
| `exe`, `cwd`, `home` | Het programma en de werkmap in de sandbox (verplicht: `exe`). `home` is HOME in de sandbox (standaard `/tmp`). |
| `args` | Argumenten. `{res}` wordt de resolutie (`BxH`). |
| `res` | De resolutie van de game (standaard 1920x1080). gamescope schaalt naar de tv. |
| `launcher` | `gamescope` of leeg. Leeg betekent het X-scherm van de host. |
| `loader` | `rg-elfloader`: start de exe via onze eigen ELF-lader (`tools/rg-elfloader`). |
| `route` | De invoerroute van de IO-brug: `tp-shm`, `jvs`, `evdev`, ... (standaard `evdev`). |
| `mounts` | `[{src, dst, mode}]`. Waarden voor `mode`: `overlay` (de NAS-map als lezende onderlaag; wat de game schrijft komt in `@data/native/<profiel>/<n>/{upper,work}`), `ro` (alleen lezend) of `ro-try` (alleen lezend; ontbreekt de bron, dan start de game toch, zonder deze mount). |
| `files` | `[{src, dst}]`. Bestanden of mappen die in de sandbox over een pad heen liggen. Voorbeelden: de oorspronkelijke versie van een hulpbestand voor de FileTest van de game, of `data/udev-rules.d` als `/etc/udev/rules.d`. |
| `seed` | `[{src, dst}]`. Wordt EENMALIG in de schrijflaag van een overlay-mount gekopieerd, alleen als het pad nog niet bestaat (niet in de laag en niet op de NAS). Bedoeld voor toestand die de game daarna zelf bijhoudt, zoals een dongle met muntenteller of instellingen. |
| `links` | `[{target, dst}]`. Hetzelfde als `seed`, maar voor een symbolische koppeling. |
| `libs` | Losse bibliotheekbestanden. Ze verschijnen als `/rg/lib/<naam>` en staan in `LD_LIBRARY_PATH`. |
| `libdirs` | `[{src, dst}]`. Hele bibliotheekmappen; `dst` komt in `LD_LIBRARY_PATH`. |
| `preload` | `LD_PRELOAD` in de sandbox, bijvoorbeeld `/rg/lib/librg_rio_bridge.so`. |
| `extra_ro` | Paden van de host die alleen lezend in de sandbox verschijnen (als ze bestaan). Voorbeeld: het gedeelde geheugen van de IO-brug. |
| `binds_rw` | `[[host, sandbox], ...]`. Schrijfbaar gekoppeld. |
| `env` | Omgevingsvariabelen van het profiel. De keuzes van de gebruiker in `game.ini` gaan voor. |
| `share_ipc` | `true` = geen eigen IPC-namespace. |
| `fake_video` | 0..4 virtuele webcams `/dev/video0..N-1` (`tools/v4l2-fake`). |
| `lindbergh` | Een Sega Lindbergh-game: `{id, ini, jvs, libcg, stackalign}`, zie `docs/lindbergh.md`. |

Naast `native.json` leest de daemon de invoertabellen `rt-invoer.ini`, `rio-invoer.ini` en `irtrack.ini` uit dezelfde map. Ook
daar gaat de gebruikerslaag voor. Een tabel geldt alleen als de bijbehorende invoerhulp in `preload` staat
(`librg_rt_input.so`, `librg_rio_bridge.so`, `librg_irtrack.so`) en het profiel de variabele niet zelf in `env` zet.

## Heapstatus uit een gedumpt beeld

Een dump kan naast stale imports ook heapwijzers van de kast bevatten. `RG_ELF_HEAP_TABLE` wijst optioneel naar een
tekstbestand met per regel `slot stale bytes allocator` (adressen hexadecimaal, grootte decimaal). Alleen `malloc` en
`_Znwj` (i386 operator new) zijn toegestaan. De lader controleert een uitgelijnd slot in een schrijfbaar PT_LOAD en de exacte
oude waarde, maakt nulgevulde opslag en vervangt alleen die wijzer. Onjuiste gegevens of een ontbrekende tabel breken de
start af. Zonder variabele verandert niets; `--ontleed-alleen` valideert maar alloceert niet. Zet geen adressen in de ladercode.

Dit is uitsluitend voor gemeten, losse buffers die de gast daarna zelf initialiseert. Het herstelt geen willekeurige C++-objecten,
vtabellen of de volledige oude heap. Voor Walking Dead is de eerste gemeten buffer een timeval van acht bytes; de normale
constructor gebruikt operator new, en de eerste aanroep schrijft beide velden met gettimeofday. Alle constructors opnieuw
uitvoeren op de overige gedumpte toestand is geen vervanging voor dit gerichte herstel.

## Een nieuwe game

1. Maak `patches/<Game>/native.json` met `mounts`, `exe`, `cwd`, `args` en `enabled: false`. Zet het eerst in de gebruikerslaag;
   dan hoeft er niets in git.
2. Zet `enabled` op `true` en start de game uit de app. Het logboek in `~/.local/state/ravengun/logs/` toont het hele commando
   en de identiteit.
3. Werkt het, zet het bestand dan in de patches-repo met `status` en `note` (gemeten, met bewijs in de gamemap).
