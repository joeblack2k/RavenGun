#!/usr/bin/env python3
"""Voegt een game toe aan TeknoParrot zonder de UI: maakt UserProfiles/<PROFIEL>.xml uit de sjabloon in
GameProfiles/, met het pad naar de game (blijft staan waar hij staat, bv. op de NAS), Input API RawInput en
de bindingen naar de virtuele RavenGun-apparaten (gun 1209:7202, toetsenbord 1209:7201).

Gebruik: python3 -I scripts/tp-add-game.py PROFIEL PAD_NAAR_EXECUTABLE [--wine-runner PAD] [--view] [--fullscreen] [--force]
  bv.    python3 -I scripts/tp-add-game.py JurassicPark "/pad/naar/Jurassic Park/game" --wine-runner ~/.../bin/wine

--env K=V: (herhaalbaar) schrijft UserProfiles/<PROFIEL>.env voor play-halo.sh. Zonder --env krijgen Windows-games
(EmulatorType TeknoParrot) TP_REMOTETHREAD=off (standaardroute van de lader) als er nog geen .env is.
--gamescope: schrijft TP_GAMESCOPE=1 (+ pad) in de .env: TeknoParrotUI draait de game in gamescope (schermvullend).
--dxvk: voor Direct3D-11-games (UE4/Unity; Raccoon Rampage, NERF): maakt een eigen kopie van de gedeelde Wine-prefix
(~/.local/share/TeknoParrotUI/prefixes/ravengun-test-dxvk), zet daar de DXVK-bibliotheken (d3d11, d3d10core, dxgi) uit een
lokaal aanwezige GE-Proton in (niets gedownload) en laat de game die prefix gebruiken (wrappers/wine-prefix, RG_WINEPREFIX,
WINEDLLOVERRIDES). De gedeelde prefix blijft ongemoeid. Zonder DXVK valt wined3d (OpenGL) om op UE4.
--view-up N: de view-wortel is N mappen boven die van de exe (bv. 3 voor RSGame/Binaries/Win64/<exe>); de exe staat in de view op
hetzelfde relatieve pad. Standaard 0.
--empty-dir REL: (herhaalbaar, met --view) maakt in de view van REL (pad ten opzichte van de gamemap) een lege map in plaats
van een link naar de NAS; de rest van de boom blijft links. Voor Raccoon Rampage: --empty-dir RSGame/Content/Movies (de
intro-video's vragen een AAC-decoder die deze Wine mist; het spel stopt dan met exitcode 1).
--vdesktop WxH: voor games die in volledig scherm vastlopen onder Wine (Let's Go Island: ChangeDisplaySettingsEx wacht
eindeloos terwijl de hoofdthread niet pompt) en in vensterstand door de kern crashen (CreateWindowExW met een klasse-atoom als
venstertitel): maakt een eigen prefix-kopie ravengun-test-<profiel> (reflink-kopie van shared/wine, zonder de link C:/Sega),
zet daarin een Wine-virtueel bureaublad WxH (HKCU/Software/Wine/Explorer), zet Windowed=0 in het profiel en laat de game die
prefix gebruiken (wrappers/wine-prefix, RG_WINEPREFIX). De gedeelde prefix blijft ongemoeid.
--fullscreen: zet Windowed uit en (waar de sjabloon dat veld heeft) een eigen resolutie 1920x1080 aan.
--view: maakt een lokale map met alleen symlinks naar de bestanden van de game (er wordt niets gekopieerd en
de bron blijft ongewijzigd). Bij ElfLdr2-games komt er een link ElfLdr2 naar de loader van TeknoParrot bij:
nodig voor 32-bit ElfLdr2-games, want TeknoParrot 2.0.0.130 start daar "ElfLdr2/BudgieLoader.exe" relatief aan de
gamemap. Ook nodig als de gamemap een spatie aan het eind heeft (Wine: "Directory name invalid"). Let op: nieuwe
bestanden die de game in de view maakt blijven lokaal; bestaande bestanden en submappen zijn links naar de NAS.

Vaste afspraak voor de gun-knoppen: trekker = LeftButton, herladen = RightButton, granaat = MiddleButton;
toetsen: Coin = 5, Start P1 = 1, Test = F2, Service = F3. Bestaande profielen worden niet overschreven
zonder --force (dan eerst een back-up ernaast).
Speler 2..4 (P2 Light Gun, Trigger P2, Reload P2, Start P2, ...) gaan naar de gun van die speler
("RavenGun Virtual Lightgun P2", 1209:7203, by-id ..._P2-event-mouse) en Start Pn = toets n; Coin blijft 5.
"""
import os, re, shutil, sys, time

TP = os.path.expanduser('~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64')
GUN = '/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse'
# Speler 2..4: een eigen gun (packaging/udev/70-ravengun.rules); speler 1 houdt GUN.
GUNS = {1: GUN, **{n: f'/dev/input/by-id/usb-RavenGun_Virtual_Lightgun_P{n}-event-mouse' for n in (2, 3, 4)}}
KBD = '/dev/input/by-id/usb-RavenGun_Virtual_Keyboard-event-kbd'


def binding(name):
    n = name.lower()
    m = re.search(r'\b(?:p|player )([2-4])\b', n)
    pl = int(m.group(1)) if m else 1
    if n == 'test' or n.startswith('test / '): return ('kbd', 'F2', 1)
    if n in ('service', 'service sw') or n.startswith('service / '): return ('kbd', 'F3', 1)
    if re.fullmatch(r'coins?( ?[1-4])?', n) or n == 'credit': return ('kbd', 'D5', 1)
    # Raccoon Rampage noemt de startknoppen "Credits P1".."Credits P4": start van die speler.
    if re.fullmatch(r'credits p[1-4]', n): return ('kbd', f'D{pl}', pl)
    # Aanraak- en trackballgames en de Proton Pack (GhostBusters): de gun is het aanwijsapparaat.
    if 'light gun' in n or n in ('touch', 'touch xy', 'touch 1 device') or re.fullmatch(r'p[1-4] (proton pack|trackball gun)', n):
        return ('gun', None, pl)
    # Let's Go Safari (LGS): vier gekleurde knoppen, de extra schakelaars en het stuur via het toetsenbord (nacht 2026-10-11;
    # de kern beantwoordt de JVS-pakketten via de pipe van TeknoParrotUi, die deze bindingen leest). Stuur: zet ook
    # "Use Keyboard/Button For Axis" op 1 in het profiel.
    lgs = {'button 1 (red)': 'A', 'button 2 (blue)': 'S', 'button 3 (yellow)': 'D', 'button 4 (green)': 'F',
           'change view': 'Q', 'window wipers': 'W', 'horn': 'E', 'fireworks': 'R',
           'wheel axis left': 'Left', 'wheel axis right': 'Right'}
    if n in lgs: return ('kbd', lgs[n], 1)
    if 'start' in n or n == 'enter sw': return ('kbd', f'D{pl}', pl)
    if n == 'gun trigger': return ('gun', 'LeftButton', pl)
    if n == 'gun button': return ('gun', 'RightButton', pl)
    if n == 'left pedal': return ('gun', 'MiddleButton', pl)
    if 'trigger' in n or n.endswith('vacuum button') or 'throw' in n or n in ('touch press', 'touch 1', 'trackball push/click'):
        return ('gun', 'LeftButton', pl)
    if 'reload' in n or n.endswith('vacuum lever') or 'shoulder' in n: return ('gun', 'RightButton', pl)
    if 'grenade' in n or n.endswith('screen out') or ' option' in n: return ('gun', 'MiddleButton', pl)
    return None


def raw_xml(kind, key, pl=1):
    if kind == 'kbd':
        bind = f'RavenGun Virtual Keyboard {key}'
        return (f'<RawInputButton><DevicePath>{KBD}</DevicePath><DeviceType>Keyboard</DeviceType>'
                f'<MouseButton>None</MouseButton><KeyboardKey>{key}</KeyboardKey></RawInputButton>', bind)
    bind = 'RavenGun Virtual Lightgun' + (f' P{pl}' if pl > 1 else '') + (f' {key}' if key else '')
    return (f'<RawInputButton><DevicePath>{GUNS[pl]}</DevicePath><DeviceType>Mouse</DeviceType>'
            f'<MouseButton>{key or "None"}</MouseButton><KeyboardKey>None</KeyboardKey></RawInputButton>', bind)


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
    return dst, [f'RG_WINEPREFIX={dst}', 'WINEDLLOVERRIDES=d3d11,d3d10core,dxgi=n,b']


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
    s = open(src, encoding='utf-8-sig').read()
    esc = game.replace('&', '&amp;').replace('<', '&lt;')
    s, n = re.subn(r'<GamePath>.*?</GamePath>|<GamePath\s*/>', lambda m: f'<GamePath>{esc}</GamePath>', s, count=1, flags=re.S)
    if n != 1: sys.exit('GamePath niet gevonden in de sjabloon')
    # Input API -> RawInput
    def api(m):
        blk = m.group(0)
        if '<FieldValue>' in blk: return re.sub(r'<FieldValue>.*?</FieldValue>', '<FieldValue>RawInput</FieldValue>', blk, count=1)
        return blk.replace('</FieldName>', '</FieldName><FieldValue>RawInput</FieldValue>', 1)
    s = re.sub(r'<FieldInformation>(?:(?!</FieldInformation>).)*?<FieldName>Input API</FieldName>.*?</FieldInformation>', api, s, count=1, flags=re.S)
    if '--fullscreen' in sys.argv:
        for name, val in (('Windowed', '0'), ('Use Custom Resolution', '1'), ('Custom Resolution Width', '1920'),
                          ('Custom Resolution Height', '1080')):
            s = re.sub(r'(<FieldName>' + name + r'</FieldName>\s*<FieldValue>).*?(</FieldValue>)',
                       lambda m: m.group(1) + val + m.group(2), s, count=1, flags=re.S)
    bound = []
    def btn(m):
        blk = m.group(0)
        name = re.search(r'<ButtonName>(.*?)</ButtonName>', blk).group(1)
        b = binding(name)
        if not b: return blk
        raw, bind = raw_xml(*b)
        blk = re.sub(r'<RawInputButton>.*?</RawInputButton>|<BindNameRi>.*?</BindNameRi>|<BindName>.*?</BindName>', '', blk, flags=re.S)
        bound.append(f'{name} -> {bind}')
        return blk.replace('</ButtonName>', f'</ButtonName>{raw}<BindNameRi>{bind}</BindNameRi><BindName>{bind}</BindName>', 1)
    s = re.sub(r'<JoystickButtons>\s*<ButtonName>.*?</JoystickButtons>', btn, s, flags=re.S)
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
        envs = [e for e in envs if not e.startswith('RG_WINEPREFIX=')] + [f'RG_WINEPREFIX={pfx}']
        s = re.sub(r'(<FieldName>Windowed</FieldName>\s*<FieldValue>).*?(</FieldValue>)', lambda m: m.group(1) + '0' + m.group(2), s, count=1, flags=re.S)
        runner = wrapper
    if runner:
        if not os.path.isfile(runner): sys.exit(f'wine-runner ontbreekt: {runner}')
        s = re.sub(r'<WineRunnerPath>.*?</WineRunnerPath>|<WineRunnerPath\s*/>', '', s, flags=re.S)
        s = s.replace('</GameProfile>', f'<WineRunnerPath>{runner}</WineRunnerPath>\n</GameProfile>')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='utf-8').write(s)
    # Omgevingsvariabelen voor play-halo.sh (UserProfiles/<PROFIEL>.env). Windows-games met de TeknoParrot-kern krijgen de
    # standaardroute van de lader (TP_REMOTETHREAD=off: geparkeerd startpunt, geen remote thread). Met de oude keuze
    # TP_POSTSTART_REMOTETHREAD_MS=3000 kreeg de kern bij o.a. Raccoon Rampage en NERF geen invoer (twaalfde ronde,
    # 2026-10-11); de 32-bit kern brak ermee af (tiende ronde). Oudere profielen (TC5, Transformers) houden hun eigen .env. Niet voor OpenParrot
    # (Taito Type X2): daarmee verbindt de game de JVS-pipe niet (gemeten bij Gaia Attack 4, 2026-10-10); die krijgen de
    # standaardroute van de lader plus de Wine-omwegen van de elfde ronde.
    envf = dst[:-4] + '.env'
    if '--gamescope' in sys.argv:
        envs = envs + ['TP_GAMESCOPE=1', 'TP_GAMESCOPE_PATH=/usr/bin/gamescope']
    tp_core = '<EmulatorType>TeknoParrot</EmulatorType>' in s
    op_core = '<EmulatorType>OpenParrot</EmulatorType>' in s      # Taito Type X2: zie packaging/games-status.md (elfde ronde)
    if envs or (not os.path.exists(envf) and (tp_core or op_core)):
        lines = list(envs)
        if op_core:
            # Standaardroute van de lader (geen TP_REMOTETHREAD), Wine's ingebouwde d3d9, lader-kopie zonder
            # CONTEXT_FLOATING_POINT (Wine zet dan MXCSR op 0) en een standaard JVS-bord achter de pipe.
            for l in ('TP_REMOTETHREAD=off', 'WINEDLLOVERRIDES=d3d9=b', 'RG_LOADER_FPFIX=true', 'RG_JVS_BOARD=true'):
                if not any(e.split('=', 1)[0] == l.split('=', 1)[0] for e in lines):
                    lines.append(l)
        # De standaard injectiemethode voor de TeknoParrot-kern blijft staan tenzij de aanroeper er zelf een kiest.
        if tp_core and not any(l.startswith(('TP_POSTSTART_REMOTETHREAD_MS=', 'TP_ENTRYPOINT_REMOTETHREAD_MS=', 'TP_REMOTETHREAD=')) for l in lines):
            lines.insert(0, 'TP_REMOTETHREAD=off')
        open(envf, 'w', encoding='utf-8').write('# door tp-add-game.py; zie packaging/games-status.md\n' + '\n'.join(lines) + '\n')
        print(f'  omgeving: {envf}: ' + ' '.join(lines))
    print(f'geschreven: {dst}\n  game: {game}\n  bindingen:\n    ' + '\n    '.join(bound))


if __name__ == '__main__':
    main()
