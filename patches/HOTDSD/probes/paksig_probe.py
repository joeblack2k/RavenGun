#!/usr/bin/env python3
"""Read-only: does this build use UE4 pak signing / encryption?

The folder has no .sig file.  Whether that matters depends on whether the shipping
build enables pak signing at all.  This looks for the strings UE4 emits when it does,
and reports the pak footer fields that would carry a signing/encryption key.

Writes nothing.  Usage: python paksig_probe.py
"""
import re
import struct

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
EXE = (G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64"
       r"\Hodzero-Win64-Shipping.exe")
PAK = G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Content\Paks\Hodzero-WindowsNoEditor.pak"

NEEDLES = [b"PakSigning", b"PakSignatures", b"SigningPublicKey", b"PakSignature",
           b"bEnablePakSigning", b"bEnablePakIndexEncryption", b"EncryptionKeyGuid",
           b"SecureHash", b".sig", b"SigningKey", b"PakFile", b"FPakFile",
           b"PakReader", b"Failed to verify", b"signature", b"Signature",
           b"chunk", b"Chunk", b"optional segment", b"OptionalSegment",
           b"pakchunk", b"MountPak", b"bSigned"]


def main():
    with open(EXE, "rb") as f:
        data = f.read()
    asc = re.findall(rb"[\x20-\x7e]{4,}", data)
    u16 = b"".join(re.findall(rb"(?:[\x20-\x7e]\x00){4,}", data)).replace(b"\x00", b"")
    blob = b"\n".join(asc) + b"\n" + u16
    print("game exe size=%d  ascii=%d  wide=%d" % (len(data), len(asc), len(u16)))
    for n in NEEDLES:
        c = blob.count(n)
        if c:
            print("  %-28s %d" % (n.decode(), c))
    print("\n-- sample lines --")
    for n in (b"PakSigning", b"Signature", b"bEnablePak", b"EncryptionKeyGuid",
              b"MountPak", b"pakchunk", b"Failed to verify", b"chunk"):
        hits = sorted({x for x in asc + re.split(rb"\n", u16) if n in x})
        for h in hits[:6]:
            print("  [%s] %s" % (n.decode(), h[:150].decode("latin1")))

    with open(PAK, "rb") as f:
        f.seek(0)
        head = f.read(8)
        import os
        size = os.path.getsize(PAK)
        f.seek(size - 44)
        foot = f.read(44)
    magic, ver = struct.unpack_from("<Ii", foot, 0)
    ioff, isize = struct.unpack_from("<qq", foot, 8)
    sha = foot[24:44]
    print("\npak: size=%d" % size)
    print("  footer (last 44 bytes): magic=%#010x version=%d" % (magic, ver))
    print("  index_offset=%d index_size=%d  -> index_end=%d" % (ioff, isize, ioff + isize))
    print("  footer starts at %d; index ends %d bytes before the footer"
          % (size - 44, (size - 44) - (ioff + isize)))
    print("  index SHA1 = %s" % sha.hex())
    print("  -> footer is the 44-byte form: Magic, Version, IndexOffset, IndexSize,"
          " IndexHash.")
    print("     There is NO bEncryptedIndex byte and NO EncryptionKeyGuid field, so this"
          " pak declares no index encryption and no signing key.")


if __name__ == "__main__":
    main()
