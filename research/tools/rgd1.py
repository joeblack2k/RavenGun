#!/usr/bin/env python3
"""RGD1 — de dongle-container: maken, lezen en verifieren.

Dit is de referentie-implementatie van het formaat dat in
findings/dongles/RGD1-formaat.md beschreven staat.

Gebruik:
    python rgd1.py pack   <dongle-map> <uitvoer.rgd>
    python rgd1.py unpack <bestand.rgd> [<uitvoermap>]
    python rgd1.py info   <bestand.rgd>
    python rgd1.py test          # maakt en leest een testvector

-------------------------------------------------------------------------------
FIX (ravengun-patches/tools/rgd1.py): the password is now an argument.

The version above carried a hardcoded default password that did NOT match the
password of its own shipped example (findings/dongles/halo-fireteam.rgd), so the
tool could not open its own example file.  The password is now resolved from the
command line, a file or the environment, so the tool can never again silently
disagree with the file it is pointed at.

Usage now:
    python rgd1.py [-p PASSWORD] pack     <dongle-dir> <output.rgd>
    python rgd1.py [-p PASSWORD] unpack   <file.rgd> [<output-dir>]
    python rgd1.py info <file.rgd>
    python rgd1.py [-p PASSWORD] selftest [<file.rgd>]
    python rgd1.py --selftest
    python rgd1.py --help

Password resolution order (first one wins):
    1. -p / --password PW
    2. --password-file FILE
    3. the RGD1_PASSWORD environment variable
    4. the built-in default, with a loud warning on stderr

The format functions below are unchanged: same constants, same cipher, same
header, same JSON payload, same error strings as documented in the spec.  The
only additions are the password plumbing, an optional salt argument to pack()
for the selftest, and this CLI.
-------------------------------------------------------------------------------
"""
import argparse, hashlib, json, os, shutil, struct, sys, tempfile

MAGIC = b'RGD1'
VERSIE = 1
KOP = 0x40                      # 64 bytes

# De standaardsleutel. Reist mee met de tool, dus dit geeft GEEN geheimhouding --
# het is voor integriteit en scheiding. Zie de spec.
STANDAARD_WACHTWOORD = b'RavenGun dongle container v1'

# The password of the shipped example, from RGD1-formaat.md section 7.  It is
# NOT the built-in default above; that mismatch is the defect this file fixes.
VOORBEELD_WACHTWOORD = b'1234-5678-90'

# The test vector from RGD1-formaat.md section 7.
SPEC_ZOUT = bytes.fromhex('5a0ff5a2c19929a6182a95a374a139fe')
SPEC_SLEUTEL = '2da613edd2ec8aa36f7fb669d599b672eccca5b9f32d5dc54ecdadb28d23b51d'
SPEC_BLOK0 = '0ed9375eca911d5a22df38a0d5ed104d508ff2c7b7b61022e6f6ba08d0fdf9b8'
SPEC_BLOK1 = '7797183d94a1157cea941c525a9d744961660ac3c1860e0217b735a0b3862efb'
SPEC_IMAGE_SHA = '3932b3aa7b0d062607e27919859247b9b5647afd6c12aab0067b4aa21d57a80b'
SPEC_IMAGE_NAAM = '00000000000000001111111111110100'
VOORBEELD_NAAM = 'halo-fireteam.rgd'

# ---------------------------------------------------------------- het cijfer
#
# SHA-256 in tellermodus. Bewust GEEN ChaCha20 of AES: dit is in elke taal met
# een SHA-256 in vijf regels na te bouwen, en het is aantoonbaar correct. De
# functie is integriteit en scheiding, geen geheimhouding -- zie de spec.

def _blok(sleutel, teller):
    """Eén blok van 32 bytes: SHA-256(sleutel || teller als 64-bits little-endian)."""
    return hashlib.sha256(sleutel + struct.pack('<Q', teller)).digest()

def stroom(sleutel, lengte):
    """De sleutelstroom van de gevraagde lengte."""
    uit = bytearray()
    teller = 0
    while len(uit) < lengte:
        uit += _blok(sleutel, teller)
        teller += 1
    return bytes(uit[:lengte])

def versleutel(sleutel, data):
    """Versleutelen en ontsleutelen zijn dezelfde bewerking (XOR met de stroom)."""
    ks = stroom(sleutel, len(data))
    return bytes(a ^ b for a, b in zip(data, ks))

def sleutel_van(wachtwoord, zout):
    """De 32-byte sleutel uit het wachtwoord.

    PBKDF2-HMAC-SHA256 met 200.000 rondes. Dat is met opzet LANGZAAM: een kort of
    raadbaar wachtwoord wordt daardoor toch duur om te raden. Elke poging kost
    ongeveer een tiende seconde in plaats van een miljoenste.
    """
    return hashlib.pbkdf2_hmac('sha256', wachtwoord, zout, 200000, dklen=32)

ZOUT_LENGTE = 16

# ---------------------------------------------------------------- inpakken

def maak_payload(dongle_map):
    """Leest de dongle-map en maakt de payload: dongle.json + de geheugenbestanden."""
    with open(os.path.join(dongle_map, 'dongle.json'), encoding='utf-8') as f:
        desc = json.load(f)
    bestanden = {}
    mem = os.path.join(dongle_map, 'memory')
    if os.path.isdir(mem):
        for naam in sorted(os.listdir(mem)):
            pad = os.path.join(mem, naam)
            if os.path.isfile(pad):
                with open(pad, 'rb') as f:
                    bestanden[naam] = f.read().hex()
    return json.dumps({'dongle': desc, 'memory': bestanden},
                      ensure_ascii=False, sort_keys=True).encode('utf-8')

def pack(dongle_map, uitvoer, wachtwoord=STANDAARD_WACHTWOORD, met_versleuteling=True,
         zout=None):
    """Packs a dongle map into a container.

    NEW: `zout` is optional and exists only so --selftest can show that the
    random salt is the single reason two packs of the same map are not
    byte-identical.  Left as None the behaviour is exactly as before: the salt
    is a fresh os.urandom(16).
    """
    payload = maak_payload(dongle_map)
    digest = hashlib.sha256(payload).digest()
    vlaggen = 1 if met_versleuteling else 0
    if zout is None:
        zout = os.urandom(ZOUT_LENGTE)
    elif len(zout) != ZOUT_LENGTE:
        raise ValueError('zout moet %d bytes zijn' % ZOUT_LENGTE)
    if met_versleuteling:
        body = versleutel(sleutel_van(wachtwoord, zout), payload)
    else:
        body = payload
    kop = bytearray(KOP)                 # alles nul
    kop[0:4] = MAGIC
    struct.pack_into('<H', kop, 0x04, VERSIE)
    struct.pack_into('<H', kop, 0x06, vlaggen)
    struct.pack_into('<I', kop, 0x08, len(payload))
    struct.pack_into('<I', kop, 0x0C, len(body))
    kop[0x10:0x30] = digest
    kop[0x30:0x40] = zout                 # het zout voor de sleutelafleiding
    with open(uitvoer, 'wb') as f:
        f.write(bytes(kop) + body)
    return len(kop) + len(body)

# ---------------------------------------------------------------- uitlezen

def lees_kop(d):
    if len(d) < KOP:
        raise ValueError('bestand korter dan de kop')
    if d[0:4] != MAGIC:
        raise ValueError('geen RGD1-bestand (magic klopt niet)')
    versie, vlaggen, plen, blen = struct.unpack_from('<HHII', d, 0x04)
    return {'versie': versie, 'vlaggen': vlaggen, 'payload_lengte': plen,
            'body_lengte': blen, 'digest': d[0x10:0x30], 'zout': d[0x30:0x40]}

def unpack(pad, wachtwoord=STANDAARD_WACHTWOORD):
    with open(pad, 'rb') as f:
        d = f.read()
    k = lees_kop(d)
    if k['versie'] != VERSIE:
        raise ValueError('onbekende versie %d' % k['versie'])
    body = d[KOP:]
    if len(body) != k['body_lengte']:
        raise ValueError('lengte klopt niet: %d in het bestand, %d in de kop'
                         % (len(body), k['body_lengte']))
    if k['vlaggen'] & 1:
        payload = versleutel(sleutel_van(wachtwoord, k['zout']), body)
    else:
        payload = body
    if hashlib.sha256(payload).digest() != k['digest']:
        raise ValueError('controlesom klopt niet -- beschadigd of verkeerd wachtwoord')
    if len(payload) != k['payload_lengte']:
        raise ValueError('payload-lengte klopt niet')
    return json.loads(payload.decode('utf-8'))

def schrijf_uit(d, map):
    os.makedirs(map, exist_ok=True)
    with open(os.path.join(map, 'dongle.json'), 'w', encoding='utf-8') as f:
        json.dump(d['dongle'], f, indent=2, ensure_ascii=False)
        f.write('\n')
    mem = os.path.join(map, 'memory')
    os.makedirs(mem, exist_ok=True)
    for naam, hexstr in d.get('memory', {}).items():
        with open(os.path.join(mem, naam), 'wb') as f:
            f.write(bytes.fromhex(hexstr))

# ============================================================================
# Everything below this line is new: password resolution, the selftest and the
# command-line interface.  Nothing above was changed except pack()'s optional
# `zout` argument.
# ============================================================================

def lees_wachtwoord(args):
    """Resolve the container password.  Returns (bytes, description)."""
    if getattr(args, 'password', None) is not None:
        return args.password.encode('utf-8'), 'command line (--password)'
    if getattr(args, 'password_file', None) is not None:
        with open(args.password_file, 'rb') as f:
            data = f.read()
        # a trailing newline added by an editor or `echo` is not part of the password
        if data.endswith(b'\n'):
            data = data[:-1]
        if data.endswith(b'\r'):
            data = data[:-1]
        return data, 'file %s (--password-file)' % args.password_file
    env = os.environ.get('RGD1_PASSWORD')
    if env is not None:
        return env.encode('utf-8'), 'environment RGD1_PASSWORD'
    print('WAARSCHUWING: geen wachtwoord opgegeven -- de ingebouwde '
          'standaardsleutel wordt gebruikt.', file=sys.stderr)
    print('WARNING: no password given -- using the built-in default key. It is '
          'probably NOT the password of this file.', file=sys.stderr)
    print('         Pass -p/--password (or --password-file, or $RGD1_PASSWORD). '
          'The shipped example uses "%s".' % VOORBEELD_WACHTWOORD.decode(),
          file=sys.stderr)
    return STANDAARD_WACHTWOORD, 'BUILT-IN DEFAULT (probably wrong for this file)'

def _kandidaat_voorbeelden(expliciet):
    """Places the shipped example may live, if it is reachable from here."""
    hier = os.path.dirname(os.path.abspath(__file__))
    kandidaten = []
    if expliciet:
        kandidaten.append(expliciet)
    kandidaten += [
        os.path.join(hier, VOORBEELD_NAAM),
        os.path.join(hier, os.pardir, VOORBEELD_NAAM),
        os.path.join(hier, os.pardir, os.pardir, 'dongles', VOORBEELD_NAAM),
        os.path.join(hier, os.pardir, os.pardir, VOORBEELD_NAAM),
        '/home/deck/mnt/spidernas_emulation/Teknoparrot/findings/dongles/' + VOORBEELD_NAAM,
        'Z:/Teknoparrot/findings/dongles/' + VOORBEELD_NAAM,
        'Z:\\Teknoparrot\\findings\\dongles\\' + VOORBEELD_NAAM,
    ]
    uit, gezien = [], set()
    for p in kandidaten:
        p = os.path.normpath(p)
        if p not in gezien and os.path.isfile(p):
            gezien.add(p)
            uit.append(p)
    return uit

def _probeer_wachtwoorden(pad, wachtwoorden):
    """Return (password, source, object) for the first password that opens pad."""
    for bron, pw in wachtwoorden:
        try:
            return pw, bron, unpack(pad, pw)
        except ValueError:
            continue
    return None, None, None

def selftest(expliciet_pad=None, wachtwoord=None, wachtwoord_bron=None):
    """Check the format against the spec's test vector and the shipped example."""
    fouten = []
    print('== RGD1 selftest ==')
    print('tool      : %s' % os.path.abspath(__file__))
    print()

    # -- 1. the pure crypto vector from spec section 7 -----------------------
    print('-- 1. crypto vector (spec section 7) --')
    key = sleutel_van(VOORBEELD_WACHTWOORD, SPEC_ZOUT)
    b0 = _blok(key, 0).hex()
    b1 = _blok(key, 1).hex()
    print('  password          : %s' % VOORBEELD_WACHTWOORD.decode())
    print('  salt              : %s' % SPEC_ZOUT.hex())
    print('  key               : %s  %s' % (key.hex(), 'OK' if key.hex() == SPEC_SLEUTEL else 'MISMATCH'))
    print('  block(0)          : %s  %s' % (b0, 'OK' if b0 == SPEC_BLOK0 else 'MISMATCH'))
    print('  block(1)          : %s  %s' % (b1, 'OK' if b1 == SPEC_BLOK1 else 'MISMATCH'))
    print('  XOR is involutive : %s'
          % (versleutel(key, versleutel(key, b'\x00' * 64)) == b'\x00' * 64))
    if key.hex() != SPEC_SLEUTEL or b0 != SPEC_BLOK0 or b1 != SPEC_BLOK1:
        fouten.append('crypto vector does not match the spec')
    print()

    # -- 2. the shipped example, if reachable --------------------------------
    print('-- 2. the shipped example --')
    kandidaten = _kandidaat_voorbeelden(expliciet_pad)
    if not kandidaten:
        print('  NOT REACHABLE: no %s found near this tool.' % VOORBEELD_NAAM)
        print('  This part is skipped, not failed. Pass a path to check one:')
        print('      rgd1.py selftest /path/to/%s' % VOORBEELD_NAAM)
        print()
    else:
        pad = kandidaten[0]
        print('  file              : %s (%d bytes)' % (pad, os.path.getsize(pad)))
        volgorde = []
        if wachtwoord is not None:
            volgorde.append(('given via %s' % wachtwoord_bron, wachtwoord))
        volgorde.append(('spec example password "%s"' % VOORBEELD_WACHTWOORD.decode(),
                         VOORBEELD_WACHTWOORD))
        volgorde.append(('tool built-in default', STANDAARD_WACHTWOORD))
        pw, bron, obj = _probeer_wachtwoorden(pad, volgorde)
        if pw is None:
            print('  FAILED: none of the tried passwords opened %s' % pad)
            fouten.append('could not open the shipped example')
        else:
            print('  opened with       : %s' % bron)
            with open(pad, 'rb') as f:
                a = f.read()
            k = lees_kop(a)
            print('  header            : versie=%d vlaggen=%d payload=%d body=%d'
                  % (k['versie'], k['vlaggen'], k['payload_lengte'], k['body_lengte']))
            print('  header salt       : %s' % k['zout'].hex())
            print('  header digest     : %s' % k['digest'].hex())
            mem = obj.get('memory', {})
            print('  game              : %s' % obj['dongle'].get('game'))
            print('  memory files      : %d %s' % (len(mem), sorted(mem)))
            if SPEC_IMAGE_NAAM in mem:
                img = bytes.fromhex(mem[SPEC_IMAGE_NAAM])
                sha = hashlib.sha256(img).hexdigest()
                print('  image             : %d bytes' % len(img))
                print('  image sha256      : %s  %s'
                      % (sha, 'OK (matches spec)' if sha == SPEC_IMAGE_SHA else 'MISMATCH'))
                if sha != SPEC_IMAGE_SHA:
                    fouten.append('image sha256 does not match the spec vector')
            else:
                print('  image             : %s NOT PRESENT' % SPEC_IMAGE_NAAM)
                fouten.append('the example lacks the spec test image')

            # -- 3. re-pack: same plaintext, fresh random salt ---------------
            print()
            print('-- 3. re-pack --')
            map_pad = os.path.join(os.path.dirname(os.path.abspath(pad)), 'halo-fireteam')
            if not os.path.isdir(map_pad):
                print('  source map        : %s NOT REACHABLE -- re-pack skipped' % map_pad)
            else:
                with tempfile.TemporaryDirectory(prefix='rgd1-selftest-') as tmp:
                    met_zout = os.path.join(tmp, 'met-zout.rgd')
                    random_zout = os.path.join(tmp, 'random-zout.rgd')
                    pack(map_pad, met_zout, pw, zout=k['zout'])
                    pack(map_pad, random_zout, pw)
                    b = open(met_zout, 'rb').read()
                    c = open(random_zout, 'rb').read()
                    print('  source map        : %s' % map_pad)
                    print('  same salt         : %d bytes, byte-identical to the original: %s'
                          % (len(b), a == b))
                    print('  random salt       : %d bytes, byte-identical to the original: %s'
                          % (len(c), a == c))
                    print('  plaintext sha256 identical for all three: %s'
                          % (lees_kop(a)['digest'] == lees_kop(c)['digest']))
                    if a != b:
                        fouten.append('re-pack with the original salt is not byte-identical')
                    if lees_kop(a)['digest'] != lees_kop(c)['digest']:
                        fouten.append('re-pack with a random salt changed the plaintext')
                    print('  NOTE: a normal re-pack is NOT byte-identical, because the')
                    print('        salt is random per file (os.urandom(16)). The plaintext is.')
        print()

    print('== %s ==' % ('PASSED' if not fouten else 'FAILED: ' + '; '.join(fouten)))
    return 0 if not fouten else 1

def _voeg_wachtwoord_opties(p, onderdruk_standaard=False):
    """Add -p/--password and --password-file.

    On the subparsers the defaults are suppressed, so a password given before
    the subcommand is not overwritten by the subparser's own default.
    """
    d = argparse.SUPPRESS if onderdruk_standaard else None
    p.add_argument('-p', '--password', metavar='PW', default=d,
                   help='the container password, as a literal string')
    p.add_argument('--password-file', metavar='FILE', default=d,
                   help='read the container password from FILE '
                        '(one trailing newline is stripped)')

def _bouw_parser():
    epiloog = """password resolution order (first one wins):
  1. -p / --password PW
  2. --password-file FILE
  3. the RGD1_PASSWORD environment variable
  4. the tool's built-in default, with a loud warning

The built-in default is "RavenGun dongle container v1". It is NOT the password
of the shipped example: findings/dongles/halo-fireteam.rgd was built with
"1234-5678-90". Always pass the password that belongs to the file you are
pointing at. `selftest` checks the spec's test vector and opens the shipped
example when it is reachable.
"""
    p = argparse.ArgumentParser(
        prog='rgd1.py',
        description='RGD1 -- pack, read and verify a dongle container.',
        epilog=epiloog,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    _voeg_wachtwoord_opties(p)
    p.add_argument('--selftest', action='store_true',
                   help='check the spec test vector and open the shipped example')
    sub = p.add_subparsers(dest='cmd', metavar='COMMAND')

    q = sub.add_parser('pack', help='pack a dongle directory into a container')
    q.add_argument('dongle_map', metavar='DONGLE-DIR')
    q.add_argument('uitvoer', metavar='OUTPUT.RGD')
    _voeg_wachtwoord_opties(q, True)

    q = sub.add_parser('unpack', help='read a container and unpack it')
    q.add_argument('bestand', metavar='FILE.RGD')
    q.add_argument('uitvoermap', metavar='OUTPUT-DIR', nargs='?')
    _voeg_wachtwoord_opties(q, True)

    q = sub.add_parser('info', help='show only the 64-byte header (no password needed)')
    q.add_argument('bestand', metavar='FILE.RGD')

    q = sub.add_parser('selftest', aliases=['test'],
                       help='check the spec test vector and open the shipped example')
    q.add_argument('bestand', metavar='FILE.RGD', nargs='?')
    _voeg_wachtwoord_opties(q, True)

    return p

def _gegeven_wachtwoord(args):
    """(password, source) if one was supplied explicitly, else (None, None)."""
    if getattr(args, 'password', None) is not None or \
            getattr(args, 'password_file', None) is not None or \
            os.environ.get('RGD1_PASSWORD') is not None:
        return lees_wachtwoord(args)
    return None, None

def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass
    parser = _bouw_parser()
    args = parser.parse_args(argv)

    if getattr(args, 'selftest', False):
        pw, bron = _gegeven_wachtwoord(args)
        return selftest(None, pw, bron)
    if not args.cmd:
        parser.print_help()
        return 2

    if args.cmd == 'info':
        with open(args.bestand, 'rb') as f:
            k = lees_kop(f.read())
        print(json.dumps({a: (b.hex() if isinstance(b, bytes) else b)
                          for a, b in k.items()}, indent=2))
        return 0

    if args.cmd in ('selftest', 'test'):
        pw, bron = _gegeven_wachtwoord(args)
        return selftest(args.bestand, pw, bron)

    wachtwoord, bron = lees_wachtwoord(args)
    if args.cmd == 'pack':
        n = pack(args.dongle_map, args.uitvoer, wachtwoord)
        print('ingepakt: %d bytes -> %s' % (n, args.uitvoer))
        print('password source: %s' % bron)
    elif args.cmd == 'unpack':
        d = unpack(args.bestand, wachtwoord)
        if args.uitvoermap:
            schrijf_uit(d, args.uitvoermap)
            print('uitgepakt naar %s' % args.uitvoermap)
            print('password source: %s' % bron)
        else:
            print(json.dumps(d['dongle'], indent=2, ensure_ascii=False))
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except ValueError as e:
        print('FOUT: %s' % e, file=sys.stderr)
        sys.exit(1)
