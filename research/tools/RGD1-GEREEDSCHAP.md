# RGD1 container tool — fix and verification

**What this is.** `rgd1.py` packs, reads and verifies an **RGD1 dongle container**: one file
holding `dongle.json` plus the HASP memory files, with a 64-byte header, a SHA-256 checksum and
an XOR keystream over `PBKDF2-HMAC-SHA256(password, salt, 200000)`. The format is specified in
`findings/dongles/RGD1-formaat.md`.

This directory holds a **fixed copy** of the reference tool:

| | |
|---|---|
| fixed tool | `~/projects/ravengun-patches/tools/rgd1.py` — 20,832 B, sha256 `0ecbc572e82b7f0585f9c612512a908500c89708990844a8e3ec47e2f0091bfe` |
| original (read-only archive) | `findings/dongles/src/rgd1.py` — 7,390 B, sha256 `2270235f382275d0c549bfcdb0511c5771b564b8a5491024669a3e71dd009c2e` |

The archive was **only read**. Nothing there was created, changed or deleted.

---

## 1. The defect

The original tool hardcodes one password and its own shipped example was built with a different
one, so the tool cannot open its own example file. MEASURED, unmodified copy, clean shell:

```
$ python3 rgd1.py unpack halo-fireteam.rgd
Traceback (most recent call last):
  File "/tmp/rgd1work/rgd1-asis.py", line 173, in <module>
    d = unpack(sys.argv[2])
  File "/tmp/rgd1work/rgd1-asis.py", line 122, in unpack
    raise ValueError('controlesom klopt niet -- beschadigd of verkeerd wachtwoord')
ValueError: controlesom klopt niet -- beschadigd of verkeerd wachtwoord
exit=1
```

The hardcoded value is at `rgd1.py:21`:

```
STANDAARD_WACHTWOORD = b'RavenGun dongle container v1'
```

The example was built with **`1234-5678-90`**, recorded in `RGD1-formaat.md` §7 and confirmed by
opening the file (below). There was **no command-line way to supply the password**: `pack()` and
`unpack()` take `wachtwoord=` as a Python keyword only, and `__main__` never passes it. The tool
could only ever use its own wrong default.

### The correct password, and how it was established

`1234-5678-90`, from three independent places that all agree:

1. `RGD1-formaat.md` §4 and §7 name it as the example's password.
2. The example's header salt at `0x30` is `5a0ff5a2c19929a6182a95a374a139fe`, exactly the salt in
   the spec's test vector — so the vector is about *this* file.
3. Opening the file with it succeeds and the plaintext checksum at `0x10` matches. The spec's
   password **does** work; no other password had to be searched for.

Wrong passwords are still refused, and the error text is unchanged:

```
$ python3 rgd1.py -p '1234-5678-91' unpack halo-fireteam.rgd
FOUT: controlesom klopt niet -- beschadigd of verkeerd wachtwoord      (exit 1)
```

---

## 2. Test vector — reproduced

MEASURED, `halo-fireteam.rgd` (1,386 B, sha256 `2b956d07a4c84ba96b74c152fc79fe51c8c431969174789c81567fb082e67422`).
Every value below matches `RGD1-formaat.md` §7 exactly.

| item | spec | computed | |
|---|---|---|---|
| salt (`0x30`) | `5a0ff5a2c19929a6182a95a374a139fe` | same | OK |
| key | `2da613edd2ec8aa36f7fb669d599b672eccca5b9f32d5dc54ecdadb28d23b51d` | same | OK |
| `block(0)` | `0ed9375eca911d5a22df38a0d5ed104d508ff2c7b7b61022e6f6ba08d0fdf9b8` | same | OK |
| `block(1)` | `7797183d94a1157cea941c525a9d744961660ac3c1860e0217b735a0b3862efb` | same | OK |
| 112-byte image sha256 | `3932b3aa7b0d062607e27919859247b9b5647afd6c12aab0067b4aa21d57a80b` | same | OK |
| plaintext payload sha256 (header `0x10`) | — | `51be53301201052bc0934c33d5653bf03f6b276efea076f19eead09f118d0964` | — |
| `versleutel(versleutel(x)) == x` | true | true | OK |

Opened content: game `Halo Fireteam`, **3** memory files —
`00000000000000001111111111110100`, `00000000000000001111111111110101`, `fff4` — all 112 bytes,
all three with sha256 `3932b3aa…a80b`, all three byte-identical to the files in
`findings/dongles/halo-fireteam/memory/`.

One deviation, MEASURED: PBKDF2 took **0.023 s** here, not the spec's ~0.074 s. That is machine
speed, not round count — 200,000 rounds is what the code uses and what the vector confirms. The
spec's "20.6 hours for a million guesses" scales accordingly.

---

## 3. The fix

`~/projects/ravengun-patches/tools/rgd1.py`. **The format behaviour is unchanged** — verified two
ways: every format function (`_blok`, `stroom`, `versleutel`, `sleutel_van`, `maak_payload`,
`lees_kop`, `unpack`, `schrijf_uit`) has **byte-identical source** to the original, the constants
are identical, and the old and the new tool produce **byte-identical output** for the same map,
salt and password — including a byte-for-byte reproduction of the shipped `halo-fireteam.rgd`.

Changed / added:

* The password is resolved from the command line: `-p/--password`, `--password-file FILE`, or
  `$RGD1_PASSWORD`, in that order. The built-in default is kept (so older containers stay
  readable) but using it now prints a loud warning on stderr naming the mistake.
* `pack()` gained one optional argument, `zout=None` (plus a length guard). With `zout=None` the
  behaviour is exactly as before: a fresh `os.urandom(16)`. It exists only so `--selftest` can
  prove the random salt is the *only* source of non-determinism.
* `--help`, and a `--selftest` flag / `selftest` subcommand. It checks the spec's crypto vector
  unconditionally, then opens the shipped example if it is reachable, then re-packs it.
* The old `test` subcommand is replaced by `selftest` (kept as an alias). The old one could not
  work: it looked for `halo-fireteam/` next to the tool in `src/`, where no such map exists, and
  it asserted 2 memory files where the example has 3.
* All exception strings are left exactly as the spec quotes them.

---

## 4. Exact commands

```sh
# password as an argument, either side of the subcommand
python3 rgd1.py -p '1234-5678-90' unpack findings/dongles/halo-fireteam.rgd ./uit
python3 rgd1.py unpack -p '1234-5678-90' findings/dongles/halo-fireteam.rgd ./uit

# password from a file, or from the environment (never on the command line / in ps)
python3 rgd1.py --password-file ~/.rgd1.pw unpack halo-fireteam.rgd ./uit
RGD1_PASSWORD='1234-5678-90' python3 rgd1.py unpack halo-fireteam.rgd ./uit

# header only, no password needed
python3 rgd1.py info halo-fireteam.rgd

# re-pack a directory (a fresh random salt each time)
python3 rgd1.py -p '1234-5678-90' pack ./halo-fireteam nieuw.rgd

# verification
python3 rgd1.py --selftest
python3 rgd1.py selftest /path/to/halo-fireteam.rgd
python3 rgd1.py --help
```

On the deck the archive is at
`/home/deck/mnt/spidernas_emulation/Teknoparrot/findings/dongles`; `--selftest` looks there
automatically, and also next to the tool and on `Z:`.

---

## 5. Re-pack: byte-identical?

**A normal re-pack is NOT byte-identical, and that is the format working as specified.** The salt
at `0x30` is `os.urandom(16)` — random per file — and a different salt means a different key,
therefore a different body. MEASURED:

```
original                      : 1386 bytes sha256=2b956d07a4c84ba96b74c152fc79fe51c8c431969174789c81567fb082e67422
re-pack, original salt forced : 1386 bytes sha256=2b956d07a4c84ba96b74c152fc79fe51c8c431969174789c81567fb082e67422
  byte-identical to the original : True
re-pack, fresh random salt     : 1386 bytes sha256=750468b1211a56b4922e93c7ca885e144980b0fead6c197a3377b06523df2a4c
  byte-identical to the original : False
  same size                      : True
  same plaintext sha256 (0x10)   : True
  same header bytes 0x00..0x30   : True
  body differs (new key)         : True
  random-salt re-pack unpacks to the identical object: True
```

So: re-packing the unpacked directory with the original salt forced reproduces the shipped file
**byte for byte**, which proves the fix changed nothing in the format. A re-pack with a fresh salt
is a *different but equivalent* file — same size, same plaintext, same checksum, unpacks to the
same object. Anyone comparing containers by hash alone will see them differ; compare the `0x10`
digest instead.

---

## 6. Template finding — CONFIRMED

The report is correct. `findings/dongles/_template/dongle.json`
(684 B, sha256 `a466fbecef39b7da97bfd1673889b7be39534200c2278a67fe742ab8f870fc0d`) breaks the rule
stated in `README.md` §2 and `schema.json`'s `$comment`: a memory file's name is the 32-bit file ID
written one character per bit, **MSB first**.

| entry | `fileid` | raw number | correct filename | template says | |
|---|---|---|---|---|---|
| `read/write` | 65524 | `0x0000FFF4` = `0000 0000 0000 0000 1111 1111 1111 0100` | `memory/00000000000000001111111111110100` | `memory/00000000000000001111111111110100` | **correct** |
| `read-only` | 65525 | `0x0000FFF5` = `0000 0000 0000 0000 1111 1111 1111 0101` | `memory/00000000000000001111111111110101` | `memory/00000000000000001111111111110100` | **WRONG** |

The `read-only` entry (fileid 65525) points at the fileid-65524 name. The correct filename is
`memory/00000000000000001111111111110101` — the last character is `1`, not `0`.

The same copy-paste error is in **`README.md` §3, line 135** (`"file": "memory/…100"` for fileid
65525). `halo-fireteam/dongle.json` gets it right, so the correct form is already in the archive.

Not a rule violation but worth knowing: `_template/dongle.json` carries a `_uitleg` key that
`schema.json` does not declare, and the template has no `memory/` directory — the two referenced
files do not exist (expected for a template, but it means the template cannot be packed as-is).

The archive is read-only, so this is reported, not fixed.

---

## 7. Open items (reported, not touched)

1. The archive copy `findings/dongles/src/rgd1.py` is still the broken version.
2. `README.md` line 135 and `_template/dongle.json` still name fileid 65525's file incorrectly.
3. The spec's timing table (0.074 s) is machine-specific; measured 0.023 s on the deck.
