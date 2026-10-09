#!/usr/bin/env python3
"""Zet een Unity-game (Tomb Raider Arcade, Rabbids Hollywood Arcade) klaar om zonder TeknoParrot en zonder
DemulShooter.exe te draaien: lokale game-view + eigen Wine-prefix. Zie docs/decisions.md (route 1).

Gebruik: python3 -I scripts/aagames-setup.py GAME [--version V] [--no-prefix] [--print-view]
  GAME     sleutel uit packaging/aagames-games.json (TombRaider, RabbidsHollywood)
  --version  map onder ~/.local/share/ravengun/demulshooter/ (standaard uit het JSON-bestand)
  --no-prefix  alleen de view (geen Wine-prefix aanmaken)
  --print-view  druk alleen het pad van de view af (voor play-unity.sh)

De DemulShooter-release staat NIET in de repository (geen licentie): download hem apart naar
~/.local/share/ravengun/demulshooter/<versie>/ (zie PROVENANCE.txt daar). Dit script voert geen enkel .exe uit de
release uit en haalt alleen de plugin-bestanden uit Unity/<plugin_dir>.

De view (~/.local/share/ravengun/runtime/views-aagames/<GAME>) bevat symlinks naar alle bestanden van de game op de NAS
(er wordt niets van de game gekopieerd, de NAS wordt nooit gewijzigd), behalve:
  - BepInEx/, winhttp.dll, doorstop_config.ini: lokaal, uit de DemulShooter-plugin (een BepInEx/doorstop van de
    TeknoParrot-variant op de NAS wordt niet gebruikt);
  - Game_Data/: een echte map met links naar elk onderdeel, zodat nieuwe bestanden van de game lokaal blijven;
  - kleine configuratiebestanden (*.json, *.conf, *.cfg, minder dan 64 KiB) en lege vlagbestanden (Game.run, ForceReboot):
    lokale kopie, want de game schrijft erin; *.log van de NAS komen niet in de view;
  - teknoparrot.ini en TeknoParrot-onderdelen (Doorstop.dll, 0Harmony.dll in de gamemap): niet in de view.
Bestaande lokale bestanden in de view blijven bij een nieuwe aanroep staan (bijv. NVRAM, de plugin-ini wordt opnieuw
uit het sjabloon gemaakt met de resolutie uit het JSON-bestand).
"""
import json, os, re, shutil, subprocess, sys

HOME = os.path.expanduser('~')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(ROOT, 'packaging', 'aagames-games.json')
DS = os.path.join(HOME, '.local/share/ravengun/demulshooter')
VIEWS = os.path.join(HOME, '.local/share/ravengun/runtime/views-aagames')
PREFIXES = os.path.join(HOME, '.local/share/ravengun/prefixes')
PROTON = os.path.join(HOME, '.local/share/TeknoParrotUI/proton/GE-Proton11-7-x86_64')
SKIP = {'bepinex', 'winhttp.dll', 'doorstop_config.ini', 'teknoparrot.ini', 'doorstop.dll', '0harmony.dll',
        '.doorstop_version', 'game_data'}
COPY_EXT = ('.json', '.conf', '.cfg')


def die(msg):
    sys.exit('aagames-setup: ' + msg)


def load(game):
    cfg = json.load(open(CFG, encoding='utf-8'))
    if game not in cfg['games']:
        die(f'onbekende game {game!r}; kies uit: ' + ', '.join(cfg['games']))
    return cfg, cfg['games'][game]


def link_all(src, dst, skip, copy_small):
    """Symlink elke vermelding van src in dst (bestaande links worden vervangen, echte bestanden blijven)."""
    os.makedirs(dst, exist_ok=True)
    for e in os.listdir(dst):
        p = os.path.join(dst, e)
        if os.path.islink(p):
            os.unlink(p)
    for e in sorted(os.listdir(src)):
        if e.lower() in skip:
            continue
        s, d = os.path.join(src, e), os.path.join(dst, e)
        if os.path.lexists(d):
            continue
        if copy_small and e.lower().endswith('.log'):
            continue  # logbestanden van de game: nieuwe ontstaan lokaal, oude van de NAS niet bijschrijven
        if copy_small and os.path.isfile(s) and (os.path.getsize(s) == 0 or
                                                 (e.lower().endswith(COPY_EXT) and os.path.getsize(s) < 64 * 1024)):
            shutil.copyfile(s, d)  # lege vlagbestanden en kleine configuratie: lokaal, de game schrijft erin
        else:
            os.symlink(s, d)


def plugin_ini(template, g):
    s = open(template, encoding='utf-8-sig').read()
    for key, val in (('FORCE_RESOLUTION', 1), ('WIDTH', g['width']), ('HEIGHT', g['height']), ('FULLSCREEN', 1)):
        s = re.sub(rf'^{key}=.*$', f'{key}={val}', s, flags=re.M)
    if g.get('language'):  # alleen Rabbids kent LANGUAGE
        s = re.sub(r'^LANGUAGE=.*$', 'LANGUAGE=' + g['language'], s, flags=re.M)
    return s


def build_view(game, g, version):
    nas = os.path.expanduser(g['nas_dir'])
    if not os.path.isfile(os.path.join(nas, g['exe'])):
        die(f'{g["exe"]} niet gevonden in {nas} (NAS gekoppeld?)')
    pl = os.path.join(DS, version, 'Unity', g['plugin_dir'])
    if not os.path.isfile(os.path.join(pl, 'winhttp.dll')):
        die(f'plugin-bestanden ontbreken in {pl}: download DemulShooter {version} (zie PROVENANCE.txt)')
    view = os.path.join(VIEWS, game)
    link_all(nas, view, SKIP, True)
    # Game_Data: echte map met links (nieuwe bestanden van de game blijven lokaal).
    link_all(os.path.join(nas, 'Game_Data'), os.path.join(view, 'Game_Data'), set(), False)
    # De plugin: BepInEx/ (kopie, de game of BepInEx schrijft daarin: logs, cache), winhttp.dll en doorstop_config.ini.
    for e in os.listdir(pl):
        s, d = os.path.join(pl, e), os.path.join(view, e)
        if os.path.isdir(s):
            shutil.copytree(s, d, dirs_exist_ok=True)
        else:
            shutil.copyfile(s, d)
    # BepInEx schrijft standaard geen logbestand (Disk-logging staat in de release uit): aanzetten, zodat
    # BepInEx/LogOutput.log in de view laat zien of BepInEx en de plugin laden.
    cfg = os.path.join(view, 'BepInEx/config/BepInEx.cfg')
    if os.path.isfile(cfg):
        t = open(cfg, encoding='utf-8-sig').read()
        t2 = re.sub(r'(\[Logging\.Disk\][^\[]*?^Enabled\s*=\s*)false', r'\1true', t, count=1, flags=re.S | re.M)
        if t2 != t:
            open(cfg, 'w', encoding='utf-8').write(t2)
    ini = [f for f in os.listdir(os.path.join(pl, 'BepInEx/plugins')) if f.endswith('.ini')]
    for f in ini:
        open(os.path.join(view, 'BepInEx/plugins', f), 'w', encoding='utf-8').write(
            plugin_ini(os.path.join(pl, 'BepInEx/plugins', f), g))
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
    view = build_view(game, g, version)
    if '--print-view' in a:
        print(view)
        return
    print(f'view: {view}')
    if '--no-prefix' not in a:
        print(f'prefix: {build_prefix("unity-aagames")}')


if __name__ == '__main__':
    main()
