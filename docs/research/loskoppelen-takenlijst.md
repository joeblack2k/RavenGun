# Loskoppelen van TeknoParrot — takenlijst

Doel (eigenaar): uiteindelijk alles wat nog TeknoParrot is vervangen door eigen onderdelen, zodat
helder is wat van RavenGun is en wat legacy. Autonoom afgewerkt, met een meting na elke stap.

Stand 2026-10-12.

## Al van ons (klaar)
- [x] Eigen lader + kern — OpenParrot uit de openbare bron, patches 0001-0005; `openparrot/current/`.
- [x] Eigen Wine/Proton — `~/.local/share/ravengun/wine/` (de Wine-binary is van ons).
- [x] Eigen spelprofielen — Dolphin-model, `patches/<Game>/game.ini`, i.p.v. TP's `GameProfiles`-XML.
- [x] Eigen launcher — `scripts/launch-direct.py` (start zonder TeknoParrotUi).
- [x] Generieke schijfletter-herleiding in de kern (0004) i.p.v. de gesloten kern.

## Nog te doen
- [x] **1. Wine-prefixes naar onze eigen map.** Gedaan 2026-10-12. De prefixes staan nu onder
      `~/.local/share/ravengun/prefixes/` (via same-filesystem `mv`, instant). Bijgewerkt: `launch-direct.py`
      (`RG_PREFIXES`), `gamesettings.py` (`PREFIXES`, bouwt `RG_WINEPREFIX` uit `[Wine] Prefix`), `tp-add-game.py`,
      `play-halo.sh`, en de tests; 12 `.env`'s (lokaal, geen repo). Gemeten: Gashaaaan leeft op
      `…/ravengun/prefixes/shared/wine`. `test_gamesettings.py` 22/22. **Rest:** `~/.local/share/TeknoParrotUI/prefixes`
      is nu een compat-symlink naar onze map (vangnet); kan weg zodra we zeker weten dat niets meer het oude pad
      noemt. `~/.local/share/TeknoParrotUI/` zelf blijft nog voor `activation/`.
- [x] **2. Audit TP-runtime-download.** Gedaan 2026-10-12. Wat we nog uit
      `runtime/TeknoParrotUi-2.0.0.130-linux-x64/` gebruiken:
      - **`pipehelper.exe` / `pipehelper32.exe`** — JVS-named-pipe (`\\.\pipe\TeknoParrot_JVS`) + `/dev/shm`-mirror,
        bij elke game. (→ item 3)
      - **`ElfLdr2/BudgieLoader.exe` + `BudgieLoader_x64.exe`, `TeknoParrot/TeknoMacaw(64).exe`** — laders voor de
        ElfLdr2/Cygwin-games (Jurassic Park, Galaga, …) en enkele andere. (→ eigen ELF-lader, zie
        `patches/ELF-LADER-SPEC.md`; `GoGoStrike` is de goedkoopste eerste proef.)
      - **`TeknoParrot/TeknoParrot(64).dll`** — de **gesloten kern**, nog voor de games die niet op onze eigen
        OpenParrot draaien (o.a. TC5: heeft `bngrw.dll`/`iDmacDrv` nodig). Type X2 + de generieke route zijn al van ons.
      - **`GameProfiles/*.xml` + `UserProfiles/*.env|.xml`** — profiel-sjablonen/instellingen die we *importeren*
        om onze `game.ini` te maken (`import-tp-profiles.py`). Build-time, geen runtime; onze `game.ini` is de bron.
- [~] **3. `pipehelper.exe` vervangen.** Gedaan + ingebouwd, 2026-10-12. Eigen clean-room Windows-brug
      `third_party/rg-jvsbridge/rg_jvsbridge.c` (uit de gedragsspec, niet uit TP's GPL-bron), gebouwd met
      `scripts/build-jvsbridge.sh` naar `~/.local/share/ravengun/jvsbridge/rg-jvsbridge.exe`. `launch-direct.py`
      (`RG_JVSBRIDGE`) kiest hem, met terugval op `pipehelper.exe`. **shm-pad bewezen** (Gashaaaan leeft op de brug,
      "created" + "shm mirror active"). **pipe-pad**: protocol volgt de spec, maar game↔TCP-bruggen nog niet headless
      bevestigd — de pipe-route-games (LGI/LGS) zijn kieskeurig en vragen de daemon + tv met lightgun (laatste stap,
      eigenaar). LGI's exit-5 bij een kale meting ligt niet aan de brug (idem met de oude pipehelper) en niet aan de
      prefix-verhuizing (Gashaaaan shared + NERF geïsoleerd leven): het is de bekende kieskeurige LGI-route.
- [~] **3b. Eigen ELF-lader** voor de ElfLdr2-familie i.p.v. `BudgieLoader` — **gedeeltelijk haalbaar** (spec:
      `patches/ELF-LADER-SPEC.md`). De familie valt in twee klassen:
      - **Ongepakt** (GoGoStrike, GhostBusters, Terminator): gewone glibc-ELF's. GoGoStrike is link-compleet met alleen
        `LD_LIBRARY_PATH=.../ElfLdr2/libs` → goedkope eerste proef (native of dunne lader). GhostBusters/Terminator
        missen alleen `libglut.so.3` (die `BudgieLoader` zelf via zijn exports levert) + een paar libs. Haalbaar.
      - **Gepakt (HASPE)** (GalagaAssault, JurassicPark, PacmanChomp, Halo): 0 sectiekoppen, PLT-stubs pushen
        `0xdeadbeef`, 72-82% van de importnamen gewist, de slot→naam-tabel staat **niet in het bestand** — alleen te
        oogsten uit een **lopende ElfLdr2-run**. Een eigen lader hiervoor = ElfLdr2's resolutietabel reverse-engineeren
        (= uitpakwerk). **Buiten bereik** zoals eerder besloten; die games houden `BudgieLoader`.
      Dus 3b dekt hooguit de 3 ongepakte games; de 4 gepakte blijven op `BudgieLoader`.
- [ ] **5. Gesloten `TeknoParrot(64).dll`** voor de games die niet op onze OpenParrot-kern draaien (o.a. TC5: heeft
      `bngrw.dll`/`iDmacDrv` nodig die wij niet bouwen). Per game; sommige houden de gesloten kern intrinsiek.
- [x] **4. `~/.local/share/TeknoParrotUI/activation/`** — opgelost 2026-10-12 (beslissing eigenaar): de Patreon-poort
      van TeknoParrot wordt op onze eigen stack **niet meer afgedwongen**. `launch-direct.py` roept `check_activation`
      niet meer aan (opt-in `--enforce-activation`, standaard uit); `needs_activation` blijft als info-vlag. Patreon is
      TP's verdienmodel, geen game-beveiliging; op onze onafhankelijke stack is er geen TP-software om voor te betalen.
      Game-beveiliging (dongle/keychip/integriteit) blijft gerespecteerd. Zie `docs/decisions.md`.
- [ ] **5. Regressiecheck** na elke stap: de levende games starten nog (serieel, headless meten).

## Blijft bewust TeknoParrot
- Keychip/dongle/integriteit/activering: nooit omzeilen (alleen USB-pass-through van een echte dongle).
- TC5 houdt de TeknoParrot-lader (heeft `bngrw.dll`/`iDmacDrv` nodig die wij niet bouwen).
