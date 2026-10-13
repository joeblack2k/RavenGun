#!/usr/bin/env python3
"""Zet een Unity-game (Tomb Raider Arcade, Rabbids Hollywood Arcade) klaar om zonder TeknoParrot en zonder
DemulShooter.exe te draaien: lokale game-view + eigen Wine-prefix. Zie docs/decisions.md (route 1 en route 2).

Gebruik: python3 -I scripts/aagames-setup.py GAME [--plugin ravengun|demulshooter] [--version V] [--no-prefix]
                                              [--print-view] [--reset-config] [--no-build]
  GAME     sleutel uit packaging/aagames-games.json (TombRaider, RabbidsHollywood)
  --plugin  welke BepInEx-plugin in de game komt: "ravengun" (route 2, onze eigen plugin uit plugins/unity/, standaard)
            of "demulshooter" (route 1, terugval). Standaard: de omgevingsvariabele RAVENGUN_UNITY_PLUGIN, anders het
            veld "plugin" van de game in het JSON-bestand, anders "ravengun".
  --version  map onder ~/.local/share/ravengun/demulshooter/ (standaard uit het JSON-bestand; alleen demulshooter)
  --no-prefix  alleen de view (geen Wine-prefix aanmaken)
  --print-view  druk alleen het pad van de view af (voor play-unity.sh)
  --reset-config  (ravengun) schrijf het plugin-configuratiebestand opnieuw uit het JSON-bestand
  --no-build  (ravengun) bouw de plugin niet automatisch als hij ontbreekt of verouderd is

Route 2 (ravengun): BepInEx 5.4.21 komt van github.com/BepInEx (LGPL-2.1) uit ~/.local/share/ravengun/bepinex/<versie>/
(zie PROVENANCE.txt daar), de plugin-DLL uit build/unity-plugin/<GAME>/ (scripts/build-unity-plugin.sh). Het configuratie-
bestand BepInEx/config/nl.studiodamon.ravengun.<game>.cfg wordt alleen aangemaakt als het ontbreekt (de waarden komen dan uit
het JSON-bestand: poort, resolutie, beeldvullend); daarna beheert de eigenaar het zelf (of --reset-config).

Route 1 (demulshooter): de DemulShooter-release staat NIET in de repository (geen licentie): download hem apart naar
~/.local/share/ravengun/demulshooter/<versie>/ (zie PROVENANCE.txt daar). Dit script voert geen enkel .exe uit de
release uit en haalt alleen de plugin-bestanden uit Unity/<plugin_dir>.

De regel (besluit van de eigenaar, 2026-10-12): de games WONEN op de NAS en worden daarvandaan gestart; wat een game zelf
schrijft (saves, instellingen, NVRAM, logs in zijn eigen spelmap) mag daar staan en is geen fout. Onze EIGEN artefacten
komen nooit stilzwijgend in een spelmap op de NAS: drop-ins, patchbestanden, onze plugin-DLL's met hun config/cache/log
(de BepInEx-boom), hulpbestanden. Die staan in de lokale view, en dit script stopt als zo'n plek toch op de NAS uitkomt.

De view (~/.local/share/ravengun/runtime/views-aagames/<GAME>) bevat symlinks naar de grote, alleen-lezen bestanden van de
game op de NAS (er wordt niets van de game gekopieerd, de NAS wordt nooit gewijzigd door dit script), behalve:
  - BepInEx/, winhttp.dll, doorstop_config.ini: lokaal, uit de plugin-release (een BepInEx/doorstop van de
    TeknoParrot-variant op de NAS wordt niet gebruikt). Dit is ONS artefact: BepInEx schrijft config, cache en LogOutput.log
    naast de game; de view is daarom de enige werkmap van de game (play-unity.sh start met de view als werkmap);
  - Game_Data/: een echte map met links naar elk onderdeel, zodat nieuwe bestanden van de game lokaal blijven. Daarin is
    Game_Data/Plugins/ (met submappen) een LOKALE kopie (eenmalig uit de NAS gekopieerd, bestaande lokale bestanden
    blijven staan): hier komen onze drop-ins (matrix64.dll, matrix/matrix.ini). Eerder was dit een symlink naar de NAS en
    dan zou een drop-in op de alleen-lezen NAS zijn beland. De rest van Game_Data (Managed, Mono, GI, Resources, de
    assets en levels, samen bijna 2 GiB) blijft symlink: alleen-lezen en groot. Een drop-in die daar toch in moet maakt de
    map pas dan lokaal (scripts/launch-direct.py: dropin_materialiseer);
  - eigen bestanden van de game die in de view lokaal staan, als gemak van deze route (niet omdat de NAS ze niet zou mogen
    bevatten): kleine configuratiebestanden in de gamemap (*.json, *.conf, *.cfg, minder dan 64 KiB) en lege vlagbestanden
    (Game.run, ForceReboot), kleine configuratiebestanden direct in Game_Data (boot.config, app.info, *.txt, *.ini, *.json,
    *.conf, *.cfg, *.config) en Game_Data/StreamingAssets/. Zo blijft de toestand van deze route (instellingen,
    ControllerBaseConfig.json) bij de view; *.log van de NAS komen niet in de view. De controle hieronder dwingt dit niet af;
  - teknoparrot.ini en TeknoParrot-onderdelen (Doorstop.dll, 0Harmony.dll in de gamemap): niet in de view.
Na het klaarzetten controleert pruef_schrijfpaden() alleen onze artefacten: de view zelf, BepInEx/ en Game_Data/Plugins met alles
erin mogen niet via een symlink op de NAS uitkomen; anders stopt het script met een foutmelding. Dat de spelmap zelf
(de bron van de symlinks) op de NAS staat is de bedoeling en stopt niets. Bestaande lokale bestanden in de view blijven bij
een nieuwe aanroep staan (bijv. NVRAM, een geplaatste drop-in; de plugin-ini wordt opnieuw uit het sjabloon gemaakt met de
resolutie uit het JSON-bestand). Oude symlinks naar de NAS in de lokale Plugins-map (uit een eerdere versie of een
drop-in-materialisatie) worden vervangen.
"""
import glob, json, os, re, shutil, subprocess, sys

HOME = os.path.expanduser('~')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(ROOT, 'packaging', 'aagames-games.json')
DS = os.path.join(HOME, '.local/share/ravengun/demulshooter')
BEPINEX = os.path.join(HOME, '.local/share/ravengun/bepinex')
PLUGIN_SRC = os.path.join(ROOT, 'plugins', 'unity', 'src')
# Uitvoer van de plugin: in de repo build/unity-plugin; bij een installatie (<prefix>/share/ravengun) onder de gebruikersdata.
if os.path.basename(os.path.dirname(ROOT)) == 'share' and os.path.isdir(os.path.join(ROOT, '..', '..', 'bin')):
    BUILD_OUT = os.path.join(os.environ.get('XDG_DATA_HOME') or os.path.join(HOME, '.local/share'), 'ravengun', 'unity-plugin')
else:
    BUILD_OUT = os.path.join(ROOT, 'build', 'unity-plugin')
BUILD_OUT = os.environ.get('RAVENGUN_UNITY_PLUGIN_OUT') or BUILD_OUT
VIEWS = os.path.join(HOME, '.local/share/ravengun/runtime/views-aagames')
PREFIXES = os.path.join(HOME, '.local/share/ravengun/prefixes')
PROTON = os.path.join(HOME, '.local/share/TeknoParrotUI/proton/GE-Proton11-7-x86_64')
SKIP = {'bepinex', 'winhttp.dll', 'doorstop_config.ini', 'teknoparrot.ini', 'doorstop.dll', '0harmony.dll',
        '.doorstop_version', 'game_data'}
COPY_EXT = ('.json', '.conf', '.cfg')
# Game_Data: Plugins is lokaal omdat onze drop-ins daar komen (verplicht, zie docstring); StreamingAssets is een eigen map van
# de game die om gemak lokaal staat (mag ook op de NAS).
LOCAL_DIRS = {'plugins', 'streamingassets'}
GAMEDATA_COPY_EXT = COPY_EXT + ('.config', '.info', '.txt', '.ini')


def die(msg):
    sys.exit('aagames-setup: ' + msg)


def load(game):
    cfg = json.load(open(CFG, encoding='utf-8'))
    if game not in cfg['games']:
        die(f'onbekende game {game!r}; kies uit: ' + ', '.join(cfg['games']))
    return cfg, cfg['games'][game]


def onder(pad, wortel):
    """Ligt `pad` (na het volgen van symlinks) in of onder `wortel`?"""
    p, w = os.path.realpath(pad), os.path.realpath(wortel)
    return p == w or p.startswith(w + os.sep)


def kopieer_lokaal(src, dst, nas):
    """Maak dst een ECHTE map met echte kopieen van src (recursief, eenmalig). Bestaande lokale bestanden blijven staan;
    een symlink naar de NAS op die plek (van een eerdere versie of een drop-in-materialisatie) wordt vervangen door een kopie.
    Schrijft nooit via een symlink: komt dst na het aanmaken toch op de NAS uit, dan stopt het script."""
    if os.path.islink(dst):
        os.unlink(dst)  # alleen de link zelf, nooit wat erachter staat
    os.makedirs(dst, exist_ok=True)
    if onder(dst, nas):
        die(f'{dst} komt op de NAS uit; daar wordt nooit geschreven')
    for e in sorted(os.listdir(src)):
        s, d = os.path.join(src, e), os.path.join(dst, e)
        if os.path.islink(d) and onder(d, nas):
            os.unlink(d)
        if os.path.isdir(s):
            kopieer_lokaal(s, d, nas)
        elif not os.path.lexists(d):
            shutil.copyfile(s, d)
            shutil.copymode(s, d)


def link_all(src, dst, skip, copy_small, local_dirs=(), copy_ext=COPY_EXT):
    """Symlink elke vermelding van src in dst (bestaande links worden vervangen, echte bestanden blijven).
    Mappen in `local_dirs` (kleine letters) worden niet gelinkt maar lokaal gekopieerd (kopieer_lokaal)."""
    os.makedirs(dst, exist_ok=True)
    for e in os.listdir(dst):
        p = os.path.join(dst, e)
        if os.path.islink(p):
            os.unlink(p)
    for e in sorted(os.listdir(src)):
        if e.lower() in skip:
            continue
        s, d = os.path.join(src, e), os.path.join(dst, e)
        if e.lower() in local_dirs and os.path.isdir(s):
            kopieer_lokaal(s, d, src)
            continue
        if os.path.lexists(d):
            continue
        if copy_small and e.lower().endswith('.log'):
            continue  # logbestanden van de game: nieuwe ontstaan lokaal, oude van de NAS niet bijschrijven
        if copy_small and os.path.isfile(s) and (os.path.getsize(s) == 0 or
                                                 (e.lower().endswith(copy_ext) and os.path.getsize(s) < 64 * 1024)):
            shutil.copyfile(s, d)  # lege vlagbestanden en kleine configuratie: lokaal, de game schrijft erin
        else:
            os.symlink(s, d)


def pruef_schrijfpaden(view, nas):
    """Geef de lijst van plekken van ONZE artefacten in de view die op de NAS uitkomen (leeg = goed): de view zelf,
    BepInEx/ met alles erin (onze plugin met config, cache en log) en Game_Data/Plugins met alles erin (drop-ins).
    Een pad is fout als het (of een map erboven) een symlink is die onder de NAS uitkomt. Wat de game zelf schrijft
    (eigen bestanden, StreamingAssets, saves) wordt NIET gecontroleerd: dat mag op de NAS staan."""
    fouten = []

    def check(p):
        if os.path.islink(p) and onder(p, nas):
            fouten.append(f'{p} -> {os.path.realpath(p)}')
        elif onder(p, nas):
            fouten.append(f'{p} ligt op de NAS')

    def boom(p):
        check(p)
        if os.path.isdir(p) and not os.path.islink(p):
            for e in sorted(os.listdir(p)):
                boom(os.path.join(p, e))

    if onder(view, nas):
        fouten.append(f'de view {view} ligt op de NAS')
        return fouten
    for rel in ('BepInEx', 'Game_Data'):
        if os.path.islink(os.path.join(view, rel)):
            fouten.append(f'{os.path.join(view, rel)} is een symlink')
    boom(os.path.join(view, 'BepInEx'))
    gd = os.path.join(view, 'Game_Data')
    for e in os.listdir(gd) if os.path.isdir(gd) else ():
        if e.lower() == 'plugins':
            boom(os.path.join(gd, e))
    return fouten


def plugin_ini(template, g):
    s = open(template, encoding='utf-8-sig').read()
    for key, val in (('FORCE_RESOLUTION', 1), ('WIDTH', g['width']), ('HEIGHT', g['height']), ('FULLSCREEN', 1)):
        s = re.sub(rf'^{key}=.*$', f'{key}={val}', s, flags=re.M)
    if g.get('language'):  # alleen Rabbids kent LANGUAGE
        s = re.sub(r'^LANGUAGE=.*$', 'LANGUAGE=' + g['language'], s, flags=re.M)
    return s


def choose_plugin(g, override):
    p = override or os.environ.get('RAVENGUN_UNITY_PLUGIN') or g.get('plugin') or 'ravengun'
    if p not in ('ravengun', 'demulshooter'):
        die(f'onbekende plugin {p!r}; kies ravengun of demulshooter')
    return p


def enable_disk_log(view):
    """BepInEx schrijft in de DemulShooter-release standaard geen logbestand: aanzetten (BepInEx/LogOutput.log)."""
    cfg = os.path.join(view, 'BepInEx/config/BepInEx.cfg')
    os.makedirs(os.path.dirname(cfg), exist_ok=True)
    if not os.path.isfile(cfg):
        open(cfg, 'w', encoding='utf-8').write('[Logging.Disk]\n\nEnabled = true\n')
        return
    t = open(cfg, encoding='utf-8-sig').read()
    t2 = re.sub(r'(\[Logging\.Disk\][^\[]*?^Enabled\s*=\s*)false', r'\1true', t, count=1, flags=re.S | re.M)
    if t2 != t:
        open(cfg, 'w', encoding='utf-8').write(t2)


def link_game(game, g):
    nas = os.path.expanduser(g['nas_dir'])
    if not os.path.isfile(os.path.join(nas, g['exe'])):
        die(f'{g["exe"]} niet gevonden in {nas} (NAS gekoppeld?)')
    view = os.path.join(VIEWS, game)
    if onder(view, nas):
        die(f'de view {view} ligt op de NAS {nas}; de view moet altijd een lokale map zijn')
    link_all(nas, view, SKIP, True)
    # Game_Data: echte map met links (nieuwe bestanden van de game blijven lokaal). Plugins (onze drop-ins) is een echte, lokale
    # kopie; StreamingAssets en kleine configuratiebestanden van de game zijn dat uit gemak ook.
    link_all(os.path.join(nas, 'Game_Data'), os.path.join(view, 'Game_Data'), set(), True, LOCAL_DIRS, GAMEDATA_COPY_EXT)
    return view


def install_demulshooter(view, g, version):
    pl = os.path.join(DS, version, 'Unity', g['plugin_dir'])
    if not os.path.isfile(os.path.join(pl, 'winhttp.dll')):
        die(f'plugin-bestanden ontbreken in {pl}: download DemulShooter {version} (zie PROVENANCE.txt)')
    # onze eigen plugin eruit (route 1 kiest uitsluitend DemulShooter)
    for f in glob.glob(os.path.join(view, 'BepInEx/plugins/RavenGun.*')):
        os.unlink(f)
    # BepInEx/ (kopie, de game of BepInEx schrijft daarin: logs, cache), winhttp.dll en doorstop_config.ini.
    for e in os.listdir(pl):
        s, d = os.path.join(pl, e), os.path.join(view, e)
        if os.path.isdir(s):
            shutil.copytree(s, d, dirs_exist_ok=True)
        else:
            shutil.copyfile(s, d)
    enable_disk_log(view)
    ini = [f for f in os.listdir(os.path.join(pl, 'BepInEx/plugins')) if f.endswith('.ini')]
    for f in ini:
        open(os.path.join(view, 'BepInEx/plugins', f), 'w', encoding='utf-8').write(
            plugin_ini(os.path.join(pl, 'BepInEx/plugins', f), g))


def plugin_stale(dll):
    if not os.path.isfile(dll):
        return True
    newest = max(os.path.getmtime(p) for p in glob.glob(os.path.join(PLUGIN_SRC, '**', '*.cs'), recursive=True))
    return os.path.getmtime(dll) < newest


def ravengun_cfg_text(cfg_root, g):
    """Beginwaarden van het plugin-configuratiebestand (BepInEx-indeling), uit het JSON-bestand."""
    b = lambda v: 'true' if v else 'false'
    return ('[Kanaal]\n\nPoort = %d\n\n[Scherm]\n\nBreedte = %d\nHoogte = %d\nBeeldvullend = %s\nResolutieAfdwingen = true\n'
            % (cfg_root.get('port', 33610), g['width'], g['height'], b(g.get('fullscreen', True))))


def install_ravengun(view, game, g, cfg_root, reset_config, build):
    bver = cfg_root.get('bepinex_version', '5.4.21.0')
    bep = os.path.join(BEPINEX, bver, 'x64')
    if not os.path.isfile(os.path.join(bep, 'BepInEx/core/BepInEx.dll')):
        die(f'BepInEx {bver} ontbreekt in {bep}: zie plugins/unity/README.md (download van github.com/BepInEx)')
    dll = os.path.join(BUILD_OUT, game, f'RavenGun.{game}.dll')
    if plugin_stale(dll):
        if not build:
            die(f'{dll} ontbreekt of is verouderd; draai scripts/build-unity-plugin.sh {game}')
        print('plugin bouwen...', file=sys.stderr, flush=True)
        r = subprocess.run([os.path.join(ROOT, 'scripts', 'build-unity-plugin.sh'), game], stdout=sys.stderr)
        if r.returncode != 0 or not os.path.isfile(dll):
            die('bouwen van de plugin mislukt')
    # BepInEx zelf: core en doorstop uit de officiele release
    shutil.copytree(os.path.join(bep, 'BepInEx/core'), os.path.join(view, 'BepInEx/core'), dirs_exist_ok=True)
    for f in ('winhttp.dll', 'doorstop_config.ini'):
        shutil.copyfile(os.path.join(bep, f), os.path.join(view, f))
    for d in ('plugins', 'config', 'patchers', 'cache'):
        os.makedirs(os.path.join(view, 'BepInEx', d), exist_ok=True)
    # DemulShooter eruit (route 2 kiest uitsluitend onze plugin) en onze plugin erin
    for f in glob.glob(os.path.join(view, 'BepInEx/plugins/*DemulShooter*')):
        os.unlink(f)
    for f in glob.glob(os.path.join(view, 'BepInEx/plugins/RavenGun.*')):
        os.unlink(f)
    shutil.copyfile(dll, os.path.join(view, 'BepInEx/plugins', os.path.basename(dll)))
    enable_disk_log(view)
    cfgf = os.path.join(view, 'BepInEx/config', f'nl.studiodamon.ravengun.{game.lower()}.cfg')
    if reset_config or not os.path.isfile(cfgf):
        open(cfgf, 'w', encoding='utf-8').write(ravengun_cfg_text(cfg_root, g))
        print(f'plugin-configuratie geschreven: {cfgf}', file=sys.stderr)


def build_view(game, g, version, plugin='ravengun', cfg_root=None, reset_config=False, build=True):
    view = link_game(game, g)
    if plugin == 'demulshooter':
        install_demulshooter(view, g, version)
    else:
        install_ravengun(view, game, g, cfg_root or {}, reset_config, build)
    fouten = pruef_schrijfpaden(view, os.path.expanduser(g['nas_dir']))
    if fouten:
        die('schrijfpaden die op de NAS uitkomen (dat mag nooit):\n  ' + '\n  '.join(fouten))
    return view


def build_prefix(name):
    """Eigen prefix voor alle Unity-games; door GE-Proton (DXVK) aangemaakt als compatdata/<name>/pfx."""
    pfx = os.path.join(PREFIXES, name)
    if os.path.isdir(os.path.join(pfx, 'pfx', 'drive_c')):
        return pfx
    os.makedirs(pfx, exist_ok=True)
    env = dict(os.environ, STEAM_COMPAT_DATA_PATH=pfx, STEAM_COMPAT_CLIENT_INSTALL_PATH=os.path.join(HOME, '.local/share/Steam'),
               WINEDLLOVERRIDES='winhttp=n,b')
    print('prefix aanmaken (eerste keer, duurt even)...', flush=True)
    subprocess.run([os.path.join(PROTON, 'proton'), 'run', 'wineboot', '-u'], env=env, check=False, cwd=pfx,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return pfx


def main():
    a = [x for x in sys.argv[1:]]
    if not a or a[0].startswith('-'):
        sys.exit(__doc__)
    game = a[0]
    cfg, g = load(game)
    version = cfg['demulshooter_version']
    if '--version' in a:
        version = a[a.index('--version') + 1]
    override = a[a.index('--plugin') + 1] if '--plugin' in a else None
    plugin = choose_plugin(g, override)
    view = build_view(game, g, version, plugin, cfg, '--reset-config' in a, '--no-build' not in a)
    if '--print-view' in a:
        print(view)
        return
    print(f'view: {view} (plugin: {plugin})')
    if '--no-prefix' not in a:
        print(f'prefix: {build_prefix("unity-aagames")}')


if __name__ == '__main__':
    main()
