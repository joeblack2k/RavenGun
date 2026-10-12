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
- [ ] **3. `pipehelper.exe` vervangen.** De TP-binary die we bij elke game draaien (JVS-named-pipe + `/dev/shm`-mirror).
      Eigen Windows-brug cross-compilen (mingw, zoals de OpenParrot-build) die `\\.\pipe\TeknoParrot_JVS` maakt en naar
      `/dev/shm` spiegelt; launch-direct heeft al `JvsPipeServer`/`run_pty_bridge` aan de Linux-kant. Grote, aparte brok.
- [ ] **3b. Eigen ELF-lader** voor de ElfLdr2-familie i.p.v. `BudgieLoader.exe` — `patches/ELF-LADER-SPEC.md`.
- [ ] **4. `~/.local/share/TeknoParrotUI/activation/`** — TP-abonnement; alleen voor Patreon-profielen.
      Opruimen/negeren waar niet nodig; nooit de activering omzeilen.
- [ ] **5. Regressiecheck** na elke stap: de levende games starten nog (serieel, headless meten).

## Blijft bewust TeknoParrot
- Keychip/dongle/integriteit/activering: nooit omzeilen (alleen USB-pass-through van een echte dongle).
- TC5 houdt de TeknoParrot-lader (heeft `bngrw.dll`/`iDmacDrv` nodig die wij niet bouwen).
