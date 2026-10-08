#!/usr/bin/env python3
"""Voegt een game toe aan TeknoParrot zonder de UI: maakt UserProfiles/<PROFIEL>.xml uit de sjabloon in
GameProfiles/, met het pad naar de game (blijft staan waar hij staat, bv. op de NAS), Input API RawInput en
de bindingen naar de virtuele RavenGun-apparaten (gun 1209:7202, toetsenbord 1209:7201).

Gebruik: python3 -I scripts/tp-add-game.py PROFIEL PAD_NAAR_EXECUTABLE [--wine-runner PAD] [--view] [--force]
  bv.    python3 -I scripts/tp-add-game.py JurassicPark "/pad/naar/Jurassic Park/game" --wine-runner ~/.../bin/wine

--view: maakt een lokale map met alleen symlinks naar de bestanden van de game (er wordt niets gekopieerd en
de bron blijft ongewijzigd) plus een link ElfLdr2 naar de loader van TeknoParrot. Nodig voor 32-bit
ElfLdr2-games: TeknoParrot 2.0.0.130 start daar "ElfLdr2/BudgieLoader.exe" relatief aan de gamemap.

Vaste afspraak voor de gun-knoppen: trekker = LeftButton, herladen = RightButton, granaat = MiddleButton;
toetsen: Coin = 5, Start P1 = 1, Test = F2, Service = F3. Bestaande profielen worden niet overschreven
zonder --force (dan eerst een back-up ernaast).
"""
import os, re, shutil, sys, time

TP = os.path.expanduser('~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64')
GUN = '/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse'
KBD = '/dev/input/by-id/usb-RavenGun_Virtual_Keyboard-event-kbd'


def binding(name):
    n = name.lower()
    p1 = not re.search(r'\b(p[2-4]|player [2-4])\b', n)
    if n == 'test': return ('kbd', 'F2')
    if n == 'service': return ('kbd', 'F3')
    if n in ('coin', 'coin 1', 'coin1'): return ('kbd', 'D5')
    if not p1: return None
    if 'light gun' in n: return ('gun', None)
    if 'start' in n: return ('kbd', 'D1')
    if 'trigger' in n or n.endswith('vacuum button'): return ('gun', 'LeftButton')
    if 'reload' in n or n.endswith('vacuum lever'): return ('gun', 'RightButton')
    if 'grenade' in n or n.endswith('screen out'): return ('gun', 'MiddleButton')
    return None


def raw_xml(kind, key):
    if kind == 'kbd':
        bind = f'RavenGun Virtual Keyboard {key}'
        return (f'<RawInputButton><DevicePath>{KBD}</DevicePath><DeviceType>Keyboard</DeviceType>'
                f'<MouseButton>None</MouseButton><KeyboardKey>{key}</KeyboardKey></RawInputButton>', bind)
    bind = 'RavenGun Virtual Lightgun' + (f' {key}' if key else '')
    return (f'<RawInputButton><DevicePath>{GUN}</DevicePath><DeviceType>Mouse</DeviceType>'
            f'<MouseButton>{key or "None"}</MouseButton><KeyboardKey>None</KeyboardKey></RawInputButton>', bind)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    force = '--force' in sys.argv
    runner = None
    if '--wine-runner' in sys.argv:
        runner = os.path.expanduser(sys.argv[sys.argv.index('--wine-runner') + 1])
        args = [a for a in args if os.path.expanduser(a) != runner]
    if len(args) != 2:
        sys.exit(__doc__)
    prof, game = args[0], os.path.abspath(os.path.expanduser(args[1]))
    src = os.path.join(TP, 'GameProfiles', prof + '.xml')
    dst = os.path.join(TP, 'UserProfiles', prof + '.xml')
    if not os.path.isfile(src): sys.exit(f'geen sjabloon: {src}')
    if not os.path.isfile(game): sys.exit(f'game-bestand ontbreekt: {game}')
    if '--view' in sys.argv:
        gdir = os.path.dirname(game)
        view = os.path.expanduser(f'~/.local/share/ravengun/runtime/views/{prof}')
        os.makedirs(view, exist_ok=True)
        for e in os.listdir(view):
            pth = os.path.join(view, e)
            if os.path.islink(pth): os.unlink(pth)
        for e in os.listdir(gdir):
            os.symlink(os.path.join(gdir, e), os.path.join(view, e))
        os.symlink(os.path.join(TP, 'ElfLdr2'), os.path.join(view, 'ElfLdr2'))
        game = os.path.join(view, os.path.basename(game))
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
    if runner:
        if not os.path.isfile(runner): sys.exit(f'wine-runner ontbreekt: {runner}')
        s = re.sub(r'<WineRunnerPath>.*?</WineRunnerPath>|<WineRunnerPath\s*/>', '', s, flags=re.S)
        s = s.replace('</GameProfile>', f'<WineRunnerPath>{runner}</WineRunnerPath>\n</GameProfile>')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='utf-8').write(s)
    print(f'geschreven: {dst}\n  game: {game}\n  bindingen:\n    ' + '\n    '.join(bound))


if __name__ == '__main__':
    main()
