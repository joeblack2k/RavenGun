#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""UserProfiles/<P>.xml als uitvoerformaat voor TeknoParrotUi (docs/per-game-patches.md, "Het profiel van een game"): de bindingen naar de
virtuele RavenGun-apparaten (gun 1209:7202, toetsenbord 1209:7201) en het schrijven van het profielbestand uit het eigen profiel in game.ini.
TeknoParrotUi wordt alleen gestart voor Mode = tpui (bv. HOTDSD) en als terugval; de directe start (launch-direct.py) leest dit bestand niet.

Vaste afspraak voor de gun-knoppen: trekker = LeftButton, herladen = RightButton, granaat = MiddleButton; toetsen: Coin = 5,
Start P1 = 1, Test = F2, Service = F3. Knoppen van een bepaalde game staan als [Bindings] in game.ini ("Knopnaam = kbd:TOETS" of
"gun:LeftButton"; "gun:" = het vizier); die gaan voor de vaste afspraak. Speler 2..4 gaan naar de gun van die speler."""
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee
import gamesettings as gs  # noqa: E402
import ownprofile as op  # noqa: E402

GUN = '/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse'
# Speler 2..4: een eigen gun (packaging/udev/70-ravengun.rules); speler 1 houdt GUN.
GUNS = {1: GUN, **{n: f'/dev/input/by-id/usb-RavenGun_Virtual_Lightgun_P{n}-event-mouse' for n in (2, 3, 4)}}
KBD = '/dev/input/by-id/usb-RavenGun_Virtual_Keyboard-event-kbd'


def binding(name, game_bindings=None):
    n = name.lower()
    # Knoppen die de game zelf vastlegt (patches/<Game>/game.ini, [Bindings]): "kbd:TOETS" of "gun:Knop" ("gun:" = het vizier).
    if game_bindings and n in game_bindings:
        kind, _, key = game_bindings[n].partition(':')
        m = re.search(r'\b(?:p|player )([2-4])\b', n)
        pl = int(m.group(1)) if m else 1
        if kind == 'kbd' and key: return ('kbd', key, 1)
        if kind == 'gun': return ('gun', key or None, pl)
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
    if 'start' in n or n == 'enter sw': return ('kbd', f'D{pl}', pl)
    if n == 'gun trigger': return ('gun', 'LeftButton', pl)
    # House of the Dead: Scarlet Dawn: hoofdtrekker = LeftButton, subtrekker (items wisselen) = RightButton, controllerkeuze = MiddleButton.
    if n.endswith('sub trigger'): return ('gun', 'RightButton', pl)
    if n.endswith('controller select'): return ('gun', 'MiddleButton', pl)
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


def apply_bindings(text, game_bindings=None):
    """Zet de bindingen in alle <JoystickButtons>-blokken van de profiel-XML; geeft (tekst, ["Knop -> apparaat", ...])."""
    bound = []

    def btn(m):
        blk = m.group(0)
        name = re.search(r'<ButtonName>(.*?)</ButtonName>', blk).group(1)
        b = binding(name, game_bindings)
        if not b: return blk
        raw, bind = raw_xml(*b)
        blk = re.sub(r'<RawInputButton>.*?</RawInputButton>|<BindNameRi>.*?</BindNameRi>|<BindName>.*?</BindName>', '', blk, flags=re.S)
        bound.append(f'{name} -> {bind}')
        return blk.replace('</ButtonName>', f'</ButtonName>{raw}<BindNameRi>{bind}</BindNameRi><BindName>{bind}</BindName>', 1)
    return re.sub(r'<JoystickButtons>\s*<ButtonName>.*?</JoystickButtons>', btn, text, flags=re.S), bound


def game_bindings_of(settings):
    return {k.lower(): v for k, v in settings.get('Bindings', {}).items()}


def write_user_xml(tp, name, settings, game_path, dry=False):
    """Schrijft UserProfiles/<P>.xml uit game.ini voor een start via TeknoParrotUi. Bestaat het bestand, dan worden alleen GamePath, WineRunnerPath
    en de waarden van de ConfigValues bijgewerkt (de knoppen en de rest blijven zoals ze zijn, ook wat TeknoParrotUi zelf wijzigde); anders wordt
    het gemaakt uit het sjabloon, met de bindingen van RavenGun. Geeft (status, [velden die het profiel niet kent], ["Knop -> apparaat"]); status: gemaakt |
    bijgewerkt | gelijk. Zonder eigen profiel of sjabloon verandert er niets."""
    dst, src = op.user_xml_path(tp, name), op.template_path(tp, name)
    if os.path.isfile(dst):
        base, status, bound = open(dst, encoding='utf-8-sig').read(), 'bijgewerkt', []
    elif os.path.isfile(src):
        base, bound = apply_bindings(open(src, encoding='utf-8-sig').read(), game_bindings_of(settings))
        status = 'gemaakt'
    else:
        return 'geen sjabloon', [], []
    text, unknown = op.render_user_xml(base, settings, game_path)
    if os.path.isfile(dst) and open(dst, encoding='utf-8-sig').read() == text:
        return 'gelijk', unknown, bound
    if not dry:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst + '.tmp', 'w', encoding='utf-8') as f:
            f.write(text)
        os.replace(dst + '.tmp', dst)
    return status, unknown, bound
