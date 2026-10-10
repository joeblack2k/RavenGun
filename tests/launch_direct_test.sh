#!/bin/sh
# scripts/launch-direct.py (A10, directe start zonder TeknoParrotUi): startplan, teknoparrot.ini, activeringscontrole,
# NAS-bescherming en het uitlezen van JVS-pakketten, tegen een nep-runtime in een tijdelijke map (geen Wine, geen game).
# Skip (77) zonder python3.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
command -v python3 >/dev/null 2>&1 || { echo "geen python3"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
HOME="$T/home" python3 -I - "$ROOT/scripts/launch-direct.py" "$T" <<'PY'
import hashlib, importlib.util, json, os, subprocess, sys

script, tmp = sys.argv[1], sys.argv[2]
home = os.path.join(tmp, "home")
tp = os.path.join(tmp, "tp")
fails = []


def check(cond, what):
    if not cond:
        fails.append(what)


def write(path, data, mode="w"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, mode) as f:
        f.write(data)


def profile(name, emu_type, emu_profile, game, is64, fields, rev="3", extra=""):
    fx = "".join(
        "<FieldInformation><CategoryName>%s</CategoryName><FieldName>%s</FieldName><FieldValue>%s</FieldValue>"
        "<FieldType>%s</FieldType><FieldOptions>%s</FieldOptions></FieldInformation>"
        % (c, n, v, t, "".join("<string>%s</string>" % o for o in opts)) for c, n, v, t, opts in fields)
    xml = ('<?xml version="1.0"?><GameProfile><GamePath>%s</GamePath><EmulationProfile>%s</EmulationProfile>'
           '<GameProfileRevision>%s</GameProfileRevision><EmulatorType>%s</EmulatorType><Is64Bit>%s</Is64Bit>'
           '<ExecutableName>%s</ExecutableName>%s<ConfigValues>%s</ConfigValues></GameProfile>'
           % (game, emu_profile, rev, emu_type, is64, os.path.basename(game), extra, fx))
    write(os.path.join(tp, "UserProfiles", name + ".xml"), xml)
    write(os.path.join(tp, "GameProfiles", name + ".xml"),
          '<GameProfile><GameProfileRevision>3</GameProfileRevision><ResetHint>false</ResetHint></GameProfile>')


def run(*args, env=None):
    e = dict(os.environ, HOME=home, WAYLAND_DISPLAY="wayland-0", RG_GAMESCOPE_RES="1280x720")
    e.update(env or {})
    p = subprocess.run([sys.executable, "-I", script, *args, "--tp", tp], capture_output=True, text=True, env=e)
    return p.returncode, p.stdout, p.stderr


# Nep-runtime: laders, kernen, pipehelper, ParrotData, een wine.
for rel in ("ElfLdr2/BudgieLoader.exe", "ElfLdr2/x64/BudgieLoader_x64.exe", "OpenParrotx64/OpenParrotLoader64.exe",
            "OpenParrotWin32/OpenParrotLoader.exe", "TeknoParrot/TeknoParrot64.dll", "TeknoParrot/TeknoParrot.dll", "pipehelper.exe"):
    write(os.path.join(tp, rel), "x")
write(os.path.join(tp, "ParrotData.xml"),
      "<ParrotData><ExitGameKey>0x1B</ExitGameKey><PauseGameKey>0x13</PauseGameKey><Elfldr2LogToFile>false</Elfldr2LogToFile></ParrotData>")
wine = os.path.join(tmp, "wine", "bin", "wine")
write(wine, "#!/bin/sh\n")
gamedir = os.path.join(tmp, "views", "Elf")
write(os.path.join(gamedir, "game"), "x")
profile("Elf", "ElfLdr2", "RawThrillsGUN", os.path.join(gamedir, "game"), "false", [
    ("General", "Input API", "RawInput", "Dropdown", ["DirectInput", "RawInput"]),
    ("General", "Windowed", "1", "Bool", []),
    ("GUN", "Mode", "Two", "DropdownIndex", ["One", "Two"]),
], extra="<WineRunnerPath>%s</WineRunnerPath>" % wine)
write(os.path.join(tp, "UserProfiles", "Elf.env"), "# commentaar\nTP_GAMESCOPE=1\nTP_GAMESCOPE_PATH=/usr/bin/gamescope\n")
exe64 = os.path.join(tmp, "win", "Game", "TimeCrisisGame.exe")
write(exe64, "x")
profile("Win", "TeknoParrot", "TimeCrisis5", exe64, "true", [("General", "Windowed", "0", "Bool", [])],
        extra="<WineRunnerPath>%s</WineRunnerPath>" % wine)
profile("Oud", "ElfLdr2", "RawThrillsGUN", os.path.join(gamedir, "game"), "false", [], rev="2")
profile("Dolphin", "Dolphin", "Foo", os.path.join(gamedir, "game"), "false", [])

# Activering: zonder seed -> 3.
rc, out, err = run("Elf", "--check")
check(rc == 3, "zonder activering exit 3 (kreeg %d: %s)" % (rc, err))
seed = os.path.join(home, ".local/share/TeknoParrotUI/activation/teknoparrot-license.reg")
write(seed, "REGEDIT4\n")
prefix = os.path.join(home, ".local/share/TeknoParrotUI/prefixes/shared/wine")
write(os.path.join(prefix, "user.reg"), '[Software\\\\TeknoGods\\\\TeknoParrot] 1\n"PatreonSerialKey"="x"\n')
write(os.path.join(prefix, ".tpui-license-seed"), "OUD\n")
rc, out, err = run("Elf", "--check")
check(rc == 3 and "niet (actueel)" in err, "verouderde activering exit 3 (kreeg %d)" % rc)
write(os.path.join(prefix, ".tpui-license-seed"), hashlib.sha256(b"REGEDIT4\n").hexdigest().upper() + "\n")
rc, out, err = run("Elf", "--check")
check(rc == 0, "actuele activering exit 0 (kreeg %d: %s)" % (rc, err))
write(os.path.join(home, ".local/share/TeknoParrotUI/activation/teknoparrot-license.revoked"), "x")
rc, out, err = run("Elf", "--check")
check(rc == 3 and "gedeactiveerd" in err, "ingetrokken activering exit 3")
os.unlink(os.path.join(home, ".local/share/TeknoParrotUI/activation/teknoparrot-license.revoked"))

# Niet ondersteund -> 4.
check(run("Oud", "--check")[0] == 4, "afwijkende profielrevisie exit 4")
check(run("Dolphin", "--check")[0] == 4, "externe emulator exit 4")
check(run("Bestaatniet", "--check")[0] == 4, "onbekend profiel exit 4")

# ElfLdr2 32-bit met gamescope: lader uit de runtime (geen ElfLdr2 naast de game), werkmap = map van de game.
rc, out, err = run("Elf", "--plan", "--json")
plan = json.loads(out) if rc == 0 else {}
check(rc == 0, "plan Elf (%s)" % err)
check(plan.get("argv") == ["/usr/bin/gamescope", "-W", "1280", "-H", "720", "-S", "fit", "-f", "--backend", "wayland", "--",
                           wine, os.path.join(tp, "ElfLdr2/BudgieLoader.exe"), "Z:" + os.path.join(gamedir, "game").replace("/", "\\")],
      "argv Elf: %s" % plan.get("argv"))
check(plan.get("cwd") == gamedir, "werkmap Elf")
le = plan.get("launch_env", {})
check(le.get("tp_windowed") == "1" and le.get("TP_LOGTOFILE") == "0" and le.get("TP_REMOTETHREAD") == "1"
      and le.get("WINEDEBUG") == "-all" and le.get("WINEPREFIX") == prefix, "omgeving Elf: %s" % le)
ini = plan["files"][0] if plan.get("files") else ["", ""]
check(ini[0] == os.path.join(gamedir, "teknoparrot.ini"), "ini-pad")
check(ini[1] == "[GlobalHotkeys]\nExitKey=0x1B\nPauseKey=0x13\n[General]\nInput API=RawInput\nWindowed=1\n[GUN]\nMode=1\n",
      "ini-inhoud: %r" % ini[1])
check(plan.get("pipe_mode") == "pipe" and "game" in plan.get("game_comms", []), "pipe en game-processen")
# Met een ElfLdr2-symlink naast de game (zoals in de views) neemt de lader die, zoals TeknoParrotUi.
os.symlink(os.path.join(tp, "ElfLdr2"), os.path.join(gamedir, "ElfLdr2"))
plan = json.loads(run("Elf", "--plan", "--json")[1])
check(plan["argv"][-2] == os.path.join(gamedir, "ElfLdr2/BudgieLoader.exe"), "lader naast de game")
# --log-full: de kern schrijft naar zijn logbestand.
plan = json.loads(run("Elf", "--plan", "--json", "--log-full")[1])
check(plan["launch_env"].get("TP_LOGTOFILE") == "1", "--log-full zet TP_LOGTOFILE")

# TeknoParrot 64-bit: kern als eerste argument, werkmap = runtime, OpenSSL-omgeving van TimeCrisis5, geen gamescope.
plan = json.loads(run("Win", "--plan", "--json", "--shm-source", "/dev/shm/ravengun_JvsState")[1])
check(plan["argv"] == [wine, os.path.join(tp, "OpenParrotx64/OpenParrotLoader64.exe"),
                       "Z:" + os.path.join(tp, "TeknoParrot/TeknoParrot64").replace("/", "\\"), "Z:" + exe64.replace("/", "\\")],
      "argv Win: %s" % plan["argv"])
check(plan["cwd"] == tp and plan["launch_env"].get("OPENSSL_ia32cap") == ":~0x20000000", "werkmap/omgeving Win")
check("tp_windowed" not in plan["launch_env"] and plan["shm_source"] == "/dev/shm/ravengun_JvsState", "Win zonder tp_windowed")

# Losse functies: NAS-bescherming, JVS-pakketten, consoledecodering.
sys.dont_write_bytecode = True   # geen __pycache__ in scripts/
spec = importlib.util.spec_from_file_location("ld", script)
ld = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ld)
nas = os.path.join(home, "mnt", "nas", "spel", "teknoparrot.ini")
write(nas, "oud\n")
st, why = ld.place_file(nas, "nieuw\n")
check(st == "nas" and open(nas).read() == "oud\n", "NAS-bestand blijft onaangeroerd")
check(ld.place_file(nas, "oud\n")[0] == "gelijk", "gelijk NAS-bestand: niets te doen")
loc = os.path.join(tmp, "lokaal", "teknoparrot.ini")
os.makedirs(os.path.dirname(loc))
check(ld.place_file(loc, "a=1\n", dry=True)[0] == "schrijven" and not os.path.exists(loc), "droog: niets geschreven")
check(ld.place_file(loc, "a=1\n")[0] == "geschreven" and open(loc).read() == "a=1\n", "lokaal schrijven")
frames, rest = ld.jvs_frames(bytes([0xE0, 0xFF, 0x03, 0xF0, 0xD9, 0xF2, 0xE0, 0xFF, 0x03, 0xF1, 0x01]))
check(frames == [bytes([0xFF, 0x03, 0xF0, 0xD9, 0xF2])] and rest == bytes([0xE0, 0xFF, 0x03, 0xF1, 0x01]), "JVS: pakket en rest")
frames, rest = ld.jvs_frames(bytes([0xE0, 0x01, 0x03, 0xD0, 0xDF, 0x10, 0x00]))   # escape D0 DF = E0
check(frames == [bytes([0x01, 0x03, 0xE0, 0x10, 0x00])] and rest == b"", "JVS: escape")
frames, rest = ld.jvs_frames(bytes([0xE0, 0x01, 0x05, 0x10, 0xE0, 0xFF, 0x03, 0xF0, 0xD9, 0xF2]))   # afgebroken pakket
check(frames == [bytes([0xFF, 0x03, 0xF0, 0xD9, 0xF2])], "JVS: afgebroken pakket vervalt")
check(ld.decode_console("Loading game...\n".encode("utf-16-le")) == "Loading game...", "UTF-16-console")
c = ld.A11Collector("x")
for line in ["patch 'NoDongle' @0x0804A000", "patch 'X' @0x10 skipped: unexpected original bytes 90 90",
             "RUNNING Terminator 1.25 US", "[gamescope] RUNNING nee"]:
    c.feed(line)
check(len(c.hits["toegepast"]) == 1 and len(c.hits["overgeslagen"]) == 1 and len(c.hits["versie"]) == 1, "A11-regels: %s" % c.hits)

if fails:
    for f in fails:
        print("FAIL:", f)
    sys.exit(1)
print("launch-direct: alle controles geslaagd")
PY
