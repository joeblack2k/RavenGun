#!/usr/bin/env python3
"""Voegt een game toe aan RavenGun zonder de UI: zet het profiel van de game uit het sjabloon in GameProfiles/ in de gebruikerslaag van game.ini
(~/.local/share/ravengun/patches/<PROFIEL>/game.ini; heeft het meegeleverde game.ini al een eigen profiel, dan alleen de keuzes van de
aanroeper), met Input API RawInput, en maakt UserProfiles/<PROFIEL>.xml als uitvoerformaat voor een start via TeknoParrotUi, met de bindingen naar de
virtuele RavenGun-apparaten (gun 1209:7202, toetsenbord 1209:7201; scripts/tpxml.py). Het pad naar de game wordt niet opgeslagen: de game wordt
herkend in de gamemappen van de app aan [Identify] (of in de view van --view); alleen als dat een ander bestand oplevert, komt het als
[Game] GamePath in de gebruikerslaag (docs/per-game-patches.md, "De game vinden").

Gebruik: python3 -I scripts/tp-add-game.py PROFIEL PAD_NAAR_EXECUTABLE [--wine-runner PAD] [--view] [--fullscreen] [--force]
  bv.    python3 -I scripts/tp-add-game.py JurassicPark "/pad/naar/Jurassic Park/game" --wine-runner ~/.../bin/wine

--env K=V: (herhaalbaar) zet een omgevingsvariabele voor de game in de gebruikerslaag
(~/.local/share/ravengun/patches/<PROFIEL>/game.ini; bekende variabelen komen onder hun eigen sleutel, de rest onder [Env]).
De standaarden per soort (TeknoParrot: standaardroute van de lader; OpenParrot: ook Wine's d3d9, JVS-bord achter de pipe en de
lader-patch) staan in patches/_defaults/<Type>.ini en hoeven niet herhaald te worden (docs/per-game-patches.md).
--gamescope: zet [Display] Gamescope = true in game.ini: de game draait in gamescope (schermvullend).
--dxvk: voor Direct3D-11-games (UE4/Unity; Raccoon Rampage, NERF): maakt een eigen kopie van de gedeelde Wine-prefix
(~/.local/share/TeknoParrotUI/prefixes/ravengun-test-dxvk), zet daar de DXVK-bibliotheken (d3d11, d3d10core, dxgi) uit een
lokaal aanwezige GE-Proton in (niets gedownload) en laat de game die prefix gebruiken (wrappers/wine-prefix, [Wine] Prefix,
DllOverrides). De gedeelde prefix blijft ongemoeid. Zonder DXVK valt wined3d (OpenGL) om op UE4.
--view-up N: de view-wortel is N mappen boven die van de exe (bv. 3 voor RSGame/Binaries/Win64/<exe>); de exe staat in de view op
hetzelfde relatieve pad. Standaard 0.
--empty-dir REL: (herhaalbaar, met --view) maakt in de view van REL (pad ten opzichte van de gamemap) een lege map in plaats
van een link naar de NAS; de rest van de boom blijft links. Voor Raccoon Rampage: --empty-dir RSGame/Content/Movies (de
intro-video's vragen een AAC-decoder die deze Wine mist; het spel stopt dan met exitcode 1).
--vdesktop WxH: voor games die in volledig scherm vastlopen onder Wine (Let's Go Island: ChangeDisplaySettingsEx wacht
eindeloos terwijl de hoofdthread niet pompt) en in vensterstand door de kern crashen (CreateWindowExW met een klasse-atoom als
venstertitel): maakt een eigen prefix-kopie ravengun-test-<profiel> (reflink-kopie van shared/wine, zonder de link C:/Sega),
zet daarin een Wine-virtueel bureaublad WxH (HKCU/Software/Wine/Explorer), zet Windowed=0 in het profiel en laat de game die
prefix gebruiken (wrappers/wine-prefix, [Wine] Prefix, VirtualDesktop). De gedeelde prefix blijft ongemoeid.
--fullscreen: zet Windowed uit en (waar de sjabloon dat veld heeft) een eigen resolutie 1920x1080 aan.
--view: maakt een lokale map met alleen symlinks naar de bestanden van de game (er wordt niets gekopieerd en
de bron blijft ongewijzigd). Bij ElfLdr2-games komt er een link ElfLdr2 naar de loader van TeknoParrot bij:
nodig voor 32-bit ElfLdr2-games, want TeknoParrot 2.0.0.130 start daar "ElfLdr2/BudgieLoader.exe" relatief aan de
gamemap. Ook nodig als de gamemap een spatie aan het eind heeft (Wine: "Directory name invalid"). Let op: nieuwe
bestanden die de game in de view maakt blijven lokaal; bestaande bestanden en submappen zijn links naar de NAS.

Vaste afspraak voor de gun-knoppen: trekker = LeftButton, herladen = RightButton, granaat = MiddleButton;
toetsen: Coin = 5, Start P1 = 1, Test = F2, Service = F3. Knoppen van een bepaalde game (Let's Go Safari: A/S/D/F, stuur) staan
als [Bindings] in patches/<PROFIEL>/game.ini ("Knopnaam = kbd:TOETS" of "gun:LeftButton"); die gaan voor de vaste afspraak.
Bestaande profielen worden niet overschreven
zonder --force (dan eerst een back-up ernaast).
Speler 2..4 (P2 Light Gun, Trigger P2, Reload P2, Start P2, ...) gaan naar de gun van die speler
("RavenGun Virtual Lightgun P2", 1209:7203, by-id ..._P2-event-mouse) en Start Pn = toets n; Coin blijft 5.
"""
import os, re, shutil, sys, time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee
import gamesettings as gs
import ownprofile as op
import tpxml

TP = os.environ.get('RAVENGUN_TP_DIR') or os.path.expanduser('~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64')


def link_tree(src, dst, empties):
    """Link elke ingang van src in dst; een pad uit `empties` (lijst van padonderdelen) wordt een lege map, en de mappen
    erlangs worden echte mappen met links voor de rest (alleen lokaal; de bron blijft ongewijzigd)."""
    os.makedirs(dst, exist_ok=True)
    first = {e[0] for e in empties}
    for e in os.listdir(src):
        sp, dp = os.path.join(src, e), os.path.join(dst, e)
        if e in first:
            sub = [x[1:] for x in empties if x[0] == e]
            if [] in sub:      # dit onderdeel zelf wordt leeg
                os.makedirs(dp, exist_ok=True)
            else:
                link_tree(sp, dp, [x for x in sub if x])
        else:
            if os.path.islink(dp): os.unlink(dp)      # een vorige keer
            os.symlink(sp, dp)


def prefix_env(path):
    """RG_WINEPREFIX-regel voor een prefix-pad (gamesettings.env_to_settings maakt er [Wine] Prefix = <naam> van)."""
    return f'RG_WINEPREFIX={path}'


def make_dxvk_prefix():
    """Eigen prefix-kopie met DXVK; geeft (prefixpad, [env-regels]). De gedeelde prefix wordt alleen gelezen."""
    home = os.path.expanduser('~')
    shared = f'{home}/.local/share/TeknoParrotUI/prefixes/shared/wine'
    dst = f'{home}/.local/share/TeknoParrotUI/prefixes/ravengun-test-dxvk'
    if not os.path.isdir(dst):
        if not os.path.isdir(shared): sys.exit(f'geen gedeelde prefix: {shared}')
        shutil.copytree(shared, dst, symlinks=True)
    import glob
    cands = sorted(glob.glob(f'{home}/.local/share/Steam/compatibilitytools.d/*/files/lib/wine/dxvk/x86_64-windows/d3d11.dll'))
    if not cands: sys.exit('geen lokale DXVK gevonden (GE-Proton in ~/.local/share/Steam/compatibilitytools.d)')
    base = os.path.dirname(os.path.dirname(cands[-1]))
    for arch, sysdir in (('x86_64-windows', 'system32'), ('i386-windows', 'syswow64')):
        for dll in ('d3d11', 'd3d10core', 'dxgi'):
            f = f'{base}/{arch}/{dll}.dll'
            if os.path.isfile(f):
                d = f'{dst}/drive_c/windows/{sysdir}/{dll}.dll'
                if os.path.lexists(d): os.unlink(d)     # de bron is alleen-lezen; een eerdere kopie ook
                shutil.copy2(f, d)
    return dst, [prefix_env(dst), 'WINEDLLOVERRIDES=d3d11,d3d10core,dxgi=n,b']


def make_vdesktop_prefix(prof, size):
    """Eigen prefix-kopie met een Wine-virtueel bureaublad; geeft het prefixpad. De gedeelde prefix wordt alleen gelezen."""
    import subprocess
    home = os.path.expanduser('~')
    shared = f'{home}/.local/share/TeknoParrotUI/prefixes/shared/wine'
    dst = f'{home}/.local/share/TeknoParrotUI/prefixes/ravengun-test-{prof.lower()}'
    if not re.fullmatch(r'\d{3,5}x\d{3,5}', size): sys.exit('--vdesktop verwacht BREEDTExHOOGTE, bv. 1920x1080')
    if not os.path.isdir(dst):
        if not os.path.isdir(shared): sys.exit(f'geen gedeelde prefix: {shared}')
        subprocess.run(['cp', '-a', '--reflink=auto', shared, dst], check=True)
        link = f'{dst}/drive_c/Sega'
        if os.path.islink(link): os.unlink(link)   # wijst in de gedeelde prefix naar de map van een andere game
    wdir = f'{home}/.local/share/ravengun/wine/proton-11.0-2/bin'
    env = {**os.environ, 'WINEPREFIX': dst, 'WINESERVER': f'{wdir}/wineserver', 'WINEDEBUG': '-all'}
    for args in (['Explorer', 'Desktop', 'ravengun'], ['Explorer\\Desktops', 'ravengun', size]):
        subprocess.run([f'{wdir}/wine', 'reg', 'add', 'HKCU\\Software\\Wine\\' + args[0], '/v', args[1], '/d', args[2], '/f'],
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([f'{wdir}/wineserver', '-k'], env=env, check=False)
    return dst


def main():
    global argv_
    argv_ = sys.argv[1:]
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    force = '--force' in sys.argv
    runner = None
    if '--wine-runner' in sys.argv:
        runner = os.path.expanduser(sys.argv[sys.argv.index('--wine-runner') + 1])
        args = [a for a in args if os.path.expanduser(a) != runner]
    argv = sys.argv[1:]
    envs = [argv[i + 1] for i, a in enumerate(argv) if a == '--env' and i + 1 < len(argv)]
    empties_arg = [argv[i + 1] for i, a in enumerate(argv) if a == '--empty-dir' and i + 1 < len(argv)]
    up_arg = [argv[i + 1] for i, a in enumerate(argv) if a == '--view-up' and i + 1 < len(argv)]
    vd_arg = [argv[i + 1] for i, a in enumerate(argv) if a == '--vdesktop' and i + 1 < len(argv)]
    args = [a for a in args if a not in envs and a not in empties_arg and a not in up_arg and a not in vd_arg]
    if len(args) != 2:
        sys.exit(__doc__)
    prof, game = args[0], os.path.abspath(os.path.expanduser(args[1]))
    shipped_game = gs.load_game(prof)      # patches/<Game>/ (meegeleverd + gebruiker): knoppen en het eigen profiel van de game
    base_game = gs.load_game(prof, user=os.path.join(os.devnull, 'geen'))   # alleen het meegeleverde deel: wat de gebruikerslaag aanvult
    src = os.path.join(TP, 'GameProfiles', prof + '.xml')
    dst = os.path.join(TP, 'UserProfiles', prof + '.xml')
    if not os.path.isfile(src): sys.exit(f'geen sjabloon: {src}')
    if not os.path.isfile(game): sys.exit(f'game-bestand ontbreekt: {game}')
    if '--view' in sys.argv:
        gdir = os.path.dirname(game)
        up = int(argv_[argv_.index('--view-up') + 1]) if '--view-up' in argv_ else 0
        for _ in range(up): gdir = os.path.dirname(gdir)
        rel = os.path.relpath(game, gdir)          # pad van de exe ten opzichte van de view-wortel
        view = os.path.expanduser(f'~/.local/share/ravengun/runtime/views/{prof}')
        os.makedirs(view, exist_ok=True)
        for e in os.listdir(view):
            pth = os.path.join(view, e)
            if os.path.islink(pth): os.unlink(pth)
            elif os.path.isdir(pth) and e != 'TeknoParrot' and not os.listdir(pth): os.rmdir(pth)   # lege mappen van een vorige keer
        empties = [argv_[i + 1] for i, a in enumerate(argv_) if a == '--empty-dir' and i + 1 < len(argv_)]
        link_tree(gdir, view, [e.strip('/').split('/') for e in empties])
        if '<EmulatorType>ElfLdr2</EmulatorType>' in open(src, encoding='utf-8-sig').read():
            os.symlink(os.path.join(TP, 'ElfLdr2'), os.path.join(view, 'ElfLdr2'))
        game = os.path.join(view, rel)
    if os.path.exists(dst):
        if not force: sys.exit(f'bestaat al: {dst} (gebruik --force)')
        shutil.copy2(dst, dst + time.strftime('.bak-%Y%m%d-%H%M%S'))
        os.unlink(dst)                              # het uitvoerprofiel voor TeknoParrotUi wordt opnieuw uit het sjabloon gemaakt
    stock, stock_fields = op.read_xml_profile(src)
    s = open(src, encoding='utf-8-sig').read()
    # Het profiel van de game staat in game.ini (docs/per-game-patches.md). Heeft het meegeleverde game.ini er al een (importer), dan komt in de
    # gebruikerslaag alleen wat de aanroeper anders kiest; anders komt het hele profiel uit het sjabloon in de gebruikerslaag.
    final = {sec: dict(kv) for sec, kv in shipped_game.settings.items()}
    if not op.has_own_profile(base_game.settings):
        prof_part = op.static_sections(stock, stock_fields, os.path.basename(game))
        root, relp = op.locate_root(game, prof, op.library_folders())
        prof_part[op.IDENTIFY] = {'Path': relp, 'Size': str(os.stat(game).st_size)}
        for sec, kv in prof_part.items():
            final.setdefault(sec, {}).update(kv)

    def set_field(name, value):
        for k in list(final.get(op.CONFIG_TYPE, {})):
            if op.split_key(k)[1] == name:
                final.setdefault(op.CONFIG, {})[k] = value
                return True
        return False
    set_field('Input API', 'RawInput')
    if '--fullscreen' in sys.argv:
        for name, val in (('Windowed', '0'), ('Use Custom Resolution', '1'), ('Custom Resolution Width', '1920'),
                          ('Custom Resolution Height', '1080')):
            set_field(name, val)
    if '--dxvk' in sys.argv:
        wrapper = os.path.expanduser('~/.local/share/ravengun/wrappers/wine-prefix')
        if not os.path.exists(wrapper): sys.exit(f'wrapper ontbreekt: {wrapper} (link naar scripts/wine-prefix.sh)')
        _, denv = make_dxvk_prefix()
        for l in denv:
            k = l.split('=', 1)[0]
            same = [e for e in envs if e.startswith(k + '=')]
            if k == 'WINEDLLOVERRIDES' and same:      # bv. winhttp=n,b voor BepInEx (NERF): samenvoegen met ;
                envs = [e for e in envs if e not in same] + [same[0] + ';' + l.split('=', 1)[1]]
            elif not same:
                envs = envs + [l]
        runner = wrapper
    if '--vdesktop' in sys.argv:
        wrapper = os.path.expanduser('~/.local/share/ravengun/wrappers/wine-prefix')
        if not os.path.exists(wrapper): sys.exit(f'wrapper ontbreekt: {wrapper} (link naar scripts/wine-prefix.sh)')
        pfx = make_vdesktop_prefix(prof, argv_[argv_.index('--vdesktop') + 1])
        envs = [e for e in envs if not e.startswith('RG_WINEPREFIX=')] + [prefix_env(pfx)]
        vdesktop = argv_[argv_.index('--vdesktop') + 1]
        set_field('Windowed', '0')
        runner = wrapper
    if runner:
        if not os.path.isfile(runner): sys.exit(f'wine-runner ontbreekt: {runner}')
        final.setdefault('Wine', {})['Runner'] = op.runner_to_ini(runner) or gs.shrink_home(runner)
    # Instellingen van de game in de gebruikerslaag (patches/<PROFIEL>/game.ini, ~/.local/share/ravengun/patches). De standaarden per
    # soort (lader-route, Wine-omwegen, lader-patch voor Type X2) komen uit patches/_defaults/<Type>.ini: hier alleen wat de aanroeper
    # kiest. Zie docs/per-game-patches.md en packaging/games-status.md.
    if '--gamescope' in sys.argv:
        envs = envs + ['TP_GAMESCOPE=1', 'TP_GAMESCOPE_PATH=/usr/bin/gamescope']
    et = re.search(r'<EmulatorType>(.*?)</EmulatorType>', s)
    typ = et.group(1).strip() if et else ''
    nm = re.search(r'<GameNameInternal>(.*?)</GameNameInternal>', s)
    env_dict = {}
    for l in envs:
        k, _, v = l.partition('=')
        env_dict[k] = v
    settings, _legacy = gs.env_to_settings(env_dict, os.path.dirname(game))
    settings.setdefault('Game', {})['Profile'] = prof
    if typ: settings['Game']['Type'] = typ
    em = re.search(r'<EmulationProfile>(.*?)</EmulationProfile>', s)
    if em and em.group(1).strip(): settings['Game']['EmulationProfile'] = em.group(1).strip()
    if nm and nm.group(1).strip() and not shipped_game.get('Game', 'Name'): settings['Game']['Name'] = nm.group(1).strip()
    if '--vdesktop' in sys.argv: settings.setdefault('Wine', {})['VirtualDesktop'] = vdesktop
    # Een eigen injectiemethode (POSTSTART/ENTRYPOINT) vervangt de standaardroute van het soort: dan RemoteThread leegmaken.
    if any(k in env_dict for k in ('TP_POSTSTART_REMOTETHREAD_MS', 'TP_ENTRYPOINT_REMOTETHREAD_MS')) and 'TP_REMOTETHREAD' not in env_dict:
        settings.setdefault('Loader', {})['RemoteThread'] = ''
    for sec, kv in settings.items():
        for k, v in kv.items():
            if v == '': final.get(sec, {}).pop(k, None)
            else: final.setdefault(sec, {})[k] = v
    # Het pad naar de game wordt afgeleid (view of gamemappen, [Identify]); alleen als dat een ander bestand vindt komt het als override in
    # de gebruikerslaag.
    final.get('Game', {}).pop('GamePath', None)
    derived, _how = op.locate_game(prof, final)
    if not derived or not op.same_file(derived, game):
        final.setdefault('Game', {})['GamePath'] = gs.shrink_home(game)
    # Naar de gebruikerslaag: wat afwijkt van het meegeleverde deel (en een lege waarde waar de aanroeper iets van het meegeleverde deel opheft).
    updates = {}
    for sec, kv in final.items():
        if sec == 'Bindings': continue
        for k, v in kv.items():
            if base_game.settings.get(sec, {}).get(k) != v:
                updates.setdefault(sec, {})[k] = v
    for sec, kv in settings.items():
        for k, v in kv.items():
            if v == '':
                updates.setdefault(sec, {})[k] = ''
    patches = {}
    dflt, _w = gs.read_ini(os.path.join(gs.shipped_dir(), '_defaults', typ + '.ini')) if typ else ({}, [])
    for name in [x.strip() for x in dflt.get('NewGame', {}).get('EnablePatches', '').split(',') if x.strip()]:
        patches[name] = {'Enabled': 'true'}
    gs.write_user_settings(prof, updates, patches)
    shown = [f'{sec}.{k}={v}' for sec, kv in updates.items() if sec not in (op.CONFIG, op.CONFIG_TYPE, op.CONFIG_OPTIONS, op.IDENTIFY) for k, v in kv.items()]
    print(f'  instellingen: {os.path.join(gs.user_dir(), prof, "game.ini")}: ' + ' '.join(shown)
          + (f' (patches aan: {", ".join(patches)})' if patches else ''))
    # Uitvoerprofiel voor een start via TeknoParrotUi (Mode = tpui en de terugval): uit het eigen profiel, met de bindingen naar onze apparaten.
    final['Bindings'] = shipped_game.settings.get('Bindings', {})
    status, unknown, bound = tpxml.write_user_xml(TP, prof, final, game)
    if unknown: print('  let op: velden die het sjabloon niet kent: ' + ', '.join(unknown))
    print(f'geschreven: {dst} ({status})\n  game: {game}\n  bindingen:\n    ' + '\n    '.join(bound))


if __name__ == '__main__':
    main()
