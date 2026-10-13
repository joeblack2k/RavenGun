#!/usr/bin/env python3
"""lm-meet.py -- leest de trampolinepagina en het dongle-object uit een lopend VACUUM.exe (root).

Drie dingen, op vaste momenten:
  1. de pagina 0x13fff0000 (4096 B) -- daar staan de 18 stubs waar de 17 am-prologues naartoe springen;
  2. het dongle-object achter [0x14049cc68] (0x200 B) en het amMaster-object achter [0x14049ccd8];
     met de velden +0x1c0 (5 B game-id), +0x1c8 (4 B netwerkadres), +0x1cc (17 B keychip-id);
  3. de 5 bytes op de prologues 0x1402b4170 / 0x1402b4270 / 0x1402b4320 / 0x1402b4740 / 0x1402b3b70,
     om te zien of de kern ze heeft omgezet (e9 ...) of niet.

Gebruik:  echo deck | sudo -S python3 lm-meet.py <uit> <tag> [max_seconden]
"""
import os, sys, time

BASE = 0x140000000
TRAMP_VA, TRAMP_LEN = 0x13FFF0000, 0x1000
DONGLE_SLOT = BASE + 0x49CC68          # [0x14049cc68] -> dongle-object
MASTER_SLOT = BASE + 0x49CCD8          # [0x14049ccd8] -> amMaster-object
OBJ_LEN = 0x200
MASTER_LEN = 0x420
PROLOGUES = [0x1402B4170, 0x1402B4270, 0x1402B4320, 0x1402B4740, 0x1402B3B70,
             0x1402B50F0, 0x1402B5CB0, 0x1402B6160]
AMHM_GLOBALS = [0x1404DE6B0, 0x1404DE6C0, 0x1404DE6D0]
# de doelen uit de trampolinepagina (GEMETEN in /tmp/lm-meet-gesloten/tramp-10.0.bin)
CORE_ADDRS = [
 ("amDongleBillingGetKeychipId", 0x6FFFF6CFDD80),
 ("amDongleGetGameId",           0x6FFFF6CFE040),
 ("amDongleGetModelType",        0x6FFFF6CFE0A0),
 ("amDongleGetNetworkAddress",   0x6FFFF6CFE0E0),
 ("amDongleInit",                0x6FFFF6CFE1F0),
 ("amDongleIsAvailableInternal", 0x6FFFF6C97D50),
 ("amDongleSetupKeychip",        0x6FFFF6CFE2A0),
 ("amDongleUpdate",              0x6FFFF6CFE3E0),
 ("amHmExit",                    0x6FFFF6D00520),
 ("amHmGetCautionFlag",          0x6FFFF6D00550),
 ("amHmInit",                    0x6FFFF6D00590),
 ("amHmResetCautionFlag",        0x6FFFF6D005C0),
 ("amHmUpdate",                  0x6FFFF6CAFF50),
 ("amMasterExitInternal",        0x6FFFF6D062E0),
 ("amMasterInit",                0x6FFFF6D063D0),
 ("amMasterSetBackupEventLogParam", 0x6FFFF6D06320),
 ("amMasterUpdate",              0x6FFFF6CC2130),
 ("niet-am 0x485a",              0x6FFFF6C979A0),
]
MOMENTEN = [float(x) for x in (os.environ.get("RG_LM_MOMENTEN") or
                               "2,3,4,5,6,8,10,12,16,20,25").split(",")]


def spelpid():
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            if open("/proc/%s/comm" % d).read().strip() != "VACUUM.exe":
                continue
            cmd = open("/proc/%s/cmdline" % d, "rb").read().decode("utf-8", "replace")
            if "LuigisMansion" in cmd:
                return int(d)
        except OSError:
            pass
    return None


def hexdump(b, per=16):
    out = []
    for i in range(0, len(b), per):
        out.append("      %04x  %s" % (i, b[i:i+per].hex(" ")))
    return "\n".join(out)


def monster(pid, moment):
    regels = ["", "################ t = %.1f s  (pid %d) ################" % (moment, pid)]
    try:
        mem = open("/proc/%d/mem" % pid, "rb", buffering=0)
    except OSError as e:
        return ["", "t=%.1f  kon /proc/%d/mem niet openen: %s" % (moment, pid, e)]

    # 1. trampolinepagina
    try:
        mem.seek(TRAMP_VA)
        tramp = mem.read(TRAMP_LEN)
        regels.append("-- [1] pagina 0x%08x, %d B gelezen" % (TRAMP_VA, len(tramp)))
        nz = sum(1 for x in tramp if x != 0)
        regels.append("       niet-nul bytes: %d van %d" % (nz, len(tramp)))
        regels.append(hexdump(tramp[:0x100]))
        regels.append("      ... (volledige pagina in tramp-%s.bin)" % moment)
        open(os.path.join(sys.argv[1], "tramp-%s.bin" % moment), "wb").write(tramp)
    except OSError as e:
        regels.append("-- [1] pagina 0x%08x NIET LEESBAAR: %s" % (TRAMP_VA, e))

    # 2. dongle-object
    for naam, slot, lengte in (("dongle", DONGLE_SLOT, OBJ_LEN),
                               ("amMaster", MASTER_SLOT, MASTER_LEN)):
        try:
            mem.seek(slot)
            raw = mem.read(8)
            ptr = int.from_bytes(raw, "little")
            regels.append("-- [2] %s: [0x%08x] = 0x%016x" % (naam, slot, ptr))
            if not (0x10000 <= ptr < 0x7FFFFFFFFFFF):
                regels.append("       geen plausibele aanwijzer -- niets gelezen")
                continue
            mem.seek(ptr)
            obj = mem.read(lengte)
            regels.append("       object op 0x%016x, %d B gelezen" % (ptr, len(obj)))
            regels.append(hexdump(obj))
            open(os.path.join(sys.argv[1], "%s-%s.bin" % (naam, moment)), "wb").write(obj)
            if naam == "dongle" and len(obj) >= 0x1DD:
                gid = obj[0x1C0:0x1C0+5]
                net = obj[0x1C8:0x1C8+4]
                kid = obj[0x1CC:0x1CC+17]
                regels.append("       +0x1c0 game-id (5 B)      : %s  %r" % (gid.hex(" "), gid))
                regels.append("       +0x1c8 netwerkadres (4 B) : %s  (LE 0x%08x)"
                              % (net.hex(" "), int.from_bytes(net, "little")))
                regels.append("       +0x1cc keychip-id (17 B)  : %s  %r" % (kid.hex(" "), kid))
                regels.append("       +0x110 status = 0x%08x   +0x114 = 0x%08x   +0x1b4 = 0x%08x"
                              % (int.from_bytes(obj[0x110:0x114], "little"),
                                 int.from_bytes(obj[0x114:0x118], "little"),
                                 int.from_bytes(obj[0x1B4:0x1B8], "little")))
        except OSError as e:
            regels.append("-- [2] %s NIET LEESBAAR: %s" % (naam, e))

    # 3. de prologues
    regels.append("-- [3] de prologues van de 17 (5 bytes elk)")
    for p in PROLOGUES:
        try:
            mem.seek(p)
            b = mem.read(8)
            oordeel = "OMGEZET (e9 -> ...)" if b[:1] == b"\xe9" else "origineel / anders"
            regels.append("      0x%08x  %s   %s" % (p, b[:5].hex(" "), oordeel))
        except OSError as e:
            regels.append("      0x%08x  NIET LEESBAAR: %s" % (p, e))

    # 4. amHm-globalen
    regels.append("-- [4] amHm-globalen")
    for g in AMHM_GLOBALS:
        try:
            mem.seek(g)
            b = mem.read(16)
            regels.append("      0x%08x  %s" % (g, b.hex(" ")))
        except OSError as e:
            regels.append("      0x%08x  NIET LEESBAAR: %s" % (g, e))

    # 5. de vervangende functies IN de kern (uit de trampolinepagina gelezen doelen)
    regels.append("-- [5] de kern-implementaties waar de 18 stubs naartoe springen")
    for naam, doel in CORE_ADDRS:
        try:
            mem.seek(doel)
            b = mem.read(0xC0)
            regels.append("      %-34s 0x%08x  %s" % (naam, doel, b[:24].hex(" ")))
            open(os.path.join(sys.argv[1], "kern-%s-%s.bin" % (naam, moment)), "wb").write(b)
        except OSError as e:
            regels.append("      %-34s 0x%08x  NIET LEESBAAR: %s" % (naam, doel, e))

    try:
        mem.close()
    except OSError:
        pass
    return regels


def main():
    uit, tag = sys.argv[1], sys.argv[2]
    maxs = float(sys.argv[3]) if len(sys.argv) > 3 else 40.0
    os.makedirs(uit, exist_ok=True)
    pid = None
    t0 = None
    alle = []
    t = 0.0
    stap = 0.02
    while t < maxs:
        if pid is None:
            p = spelpid()
            if p:
                pid = p
                t0 = t
                alle.append("spelproces %d verschenen op t=%.2f" % (p, t))
        else:
            if not os.path.exists("/proc/%d" % pid):
                alle.append("PROCES WEG op t=%.2f" % t)
                break
            dt = t - t0
            while MOMENTEN and dt >= MOMENTEN[0]:
                m = MOMENTEN.pop(0)
                alle += monster(pid, m)
        time.sleep(stap)
        t += stap
    kop = "=== %s ===\n" % tag + "\n".join(alle) + "\n"
    open(os.path.join(uit, "meting-%s.txt" % tag), "w").write(kop)
    sys.stdout.write(kop)
    sys.stdout.flush()


if __name__ == "__main__":
    main()
