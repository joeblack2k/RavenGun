#!/bin/sh
# scripts/launch-direct.py (A10, directe start zonder TeknoParrotUi): startplan, teknoparrot.ini, activeringscontrole,
# NAS-bescherming en het uitlezen van JVS-pakketten, tegen een nep-runtime in een tijdelijke map (geen Wine, geen game).
# Ook de instellingen per game (patches/<Game>/game.ini met een gebruikerslaag, het oude .env als terugval) en de bestandspatches
# (Kind = file: Find precies Count keer, een kopie onder patched/, nooit het origineel of een bestand op de NAS).
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
shipped = os.path.join(tmp, "patches")          # nep-meegeleverde map (patches/ in de repo)
os.environ["RAVENGUN_PATCHES_DIR"] = shipped
os.environ["XDG_DATA_HOME"] = os.path.join(home, ".local/share")    # gebruikerslaag: <home>/.local/share/ravengun/patches
user = os.path.join(home, ".local/share/ravengun/patches")
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
    e = dict(os.environ, HOME=home, WAYLAND_DISPLAY="wayland-0", RG_GAMESCOPE_RES="1280x720", RAVENGUN_PATCHES_DIR=shipped,
             XDG_DATA_HOME=os.path.join(home, ".local/share"))
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
], extra="<WineRunnerPath>%s</WineRunnerPath><Patreon>true</Patreon>" % wine)
write(os.path.join(shipped, "Elf", "game.ini"), "[Game]\nType = ElfLdr2\n[Display]\nGamescope = true\nGamescopePath = /usr/bin/gamescope\n")
exe64 = os.path.join(tmp, "win", "Game", "TimeCrisisGame.exe")
write(exe64, "x")
profile("Win", "TeknoParrot", "TimeCrisis5", exe64, "true", [("General", "Windowed", "0", "Bool", [])],
        extra="<WineRunnerPath>%s</WineRunnerPath>" % wine)
profile("Oud", "ElfLdr2", "RawThrillsGUN", os.path.join(gamedir, "game"), "false", [], rev="2")
profile("Dolphin", "Dolphin", "Foo", os.path.join(gamedir, "game"), "false", [])

# Activering: alleen voor Patreon-profielen. Zonder Patreon start het zonder activering.
rc, out, err = run("Win", "--check")
check(rc == 0, "zonder Patreon geen activering nodig (kreeg %d: %s)" % (rc, err))
# Patreon-profiel zonder seed -> 3.
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
# JVS-bord achter de pipe (A10): antwoordpakket (E0 00 lengte status rapport(en) som) en de lader-kopie zonder FLOATING_POINT.
ans = ld.jvs_answer(bytes([0x11]))
check(ans == bytes([0x01, 0x01, 0x13]), "JVS-antwoord revisie: %r" % ans)
check(ld.jvs_pack(ans) == bytes([0xE0, 0x00, 0x04, 0x01, 0x01, 0x13, 0x19]), "JVS-pakket: %r" % ld.jvs_pack(ans))
check(ld.jvs_answer(bytes([0xF0, 0xD9])) is None, "JVS: reset zonder antwoord")
check(ld.jvs_answer(bytes([0x20, 0x02, 0x02]))[:2] == bytes([0x01, 0x01]) and len(ld.jvs_answer(bytes([0x20, 0x02, 0x02]))) == 2 + 1 + 4, "JVS: schakelaars 2 spelers x 2 bytes")
check(ld.jvs_answer(bytes([0x7F]))[-1] == 0x02, "JVS: onbekende opdracht = rapport 2")
# Instellingen per game: een eigen laag van de gebruiker wint per sleutel, een lege waarde zet een sleutel terug op niet gezet.
rc, out, err = run("Elf", "--plan", "--json")
check(json.loads(out)["profile_env"] == {"TP_GAMESCOPE": "1", "TP_GAMESCOPE_PATH": "/usr/bin/gamescope"},
      "game.ini levert de omgeving: %s" % json.loads(out)["profile_env"])
write(os.path.join(user, "Elf", "game.ini"), "[Display]\nGamescope =\n[Loader]\nRemoteThread = off\n[Env]\nFOO = bar\n")
plan = json.loads(run("Elf", "--plan", "--json")[1])
check(plan["argv"][0] == wine and "TP_GAMESCOPE" not in plan["profile_env"] and plan["profile_env"].get("FOO") == "bar",
      "gebruikerslaag: gamescope uit, [Env]")
check("TP_REMOTETHREAD" not in plan["launch_env"], "RemoteThread = off laat de variabele weg: %s" % plan["launch_env"])
os.unlink(os.path.join(user, "Elf", "game.ini"))
# Terugval: een profiel zonder game.ini met alleen een oud .env (melding "legacy .env", RG_LOADER_FPFIX zet de gedeelde patch aan).
write(os.path.join(tp, "UserProfiles", "Win.env"), "# oud\nTP_REMOTETHREAD=off\nWINEDLLOVERRIDES=d3d9=b\n")
rc, out, err = run("Win", "--plan", "--json")
check(rc == 0 and json.loads(out)["profile_env"] == {"TP_REMOTETHREAD": "off", "WINEDLLOVERRIDES": "d3d9=b"} and "legacy .env" in err,
      "terugval op .env met melding (%s)" % err)
rc, out, err = run("Win", "--plan", "--json", env={"RAVENGUN_NO_LEGACY_ENV": "1"})
check(json.loads(out)["profile_env"] == {} and "legacy .env" not in err, "RAVENGUN_NO_LEGACY_ENV=1 slaat het .env over")
os.unlink(os.path.join(tp, "UserProfiles", "Win.env"))

# Bestandspatches (Kind = file) op een nep-lader.
import hashlib as _h
sys.path.insert(0, os.path.dirname(script))
gsm = ld.gs
PAT = bytes.fromhex("C705C09B42001F000100")
FIX = bytes.fromhex("C705C09B420017000100")
SHARED_PATCHES = """[Patch: Lader zonder FP]
Enabled = false
Kind = file
Category = loader
Version = 1.08
Target = loader
TargetName = OpenParrotLoader.exe
Find = C7 05 C0 9B 42 00 1F 00 01 00
Replace = C7 05 C0 9B 42 00 17 00 01 00
Count = 1
Reason = test
"""
write(os.path.join(shipped, "_defaults", "OpenParrot.patches.ini"), SHARED_PATCHES)
write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\n")
write(os.path.join(shipped, "Gx", "patches.ini"), "[Patch: Lader zonder FP]\nEnabled = true\n")
loader_src = os.path.join(tmp, "rt", "OpenParrotLoader.exe")
orig = b"MZ" + b"\x00" * 10 + PAT + b"\x00" * 10
write(loader_src, orig, "wb")
saved_home = ld.HOME
ld.HOME = os.path.join(tmp, "fpfix-home")
os.environ["HOME"] = ld.HOME
try:
    g = gsm.load_game("Gx")
    check([p.name for p in g.patches] == ["Lader zonder FP"] and g.patches[0].enabled, "gedeelde patch aangezet door de game")
    res, rep = ld.plan_file_patches(g, {"loader": loader_src, "core": None}, tmp, materialize=False)
    sha_o = _h.sha256(orig).hexdigest()
    sha_s = _h.sha256(("Lader zonder FP\0%s\0%s\0%d" % (PAT.hex(), FIX.hex(), 1)).encode()).hexdigest()
    want = os.path.join(ld.HOME, ".local/share/ravengun/patched", "%s-%s" % (sha_o[:16], sha_s[:16]), "OpenParrotLoader.exe")
    check(res.get("loader") == want, "cachepad %s (verwacht %s)" % (res.get("loader"), want))
    check(not os.path.exists(want), "plan (zonder materialize) maakt de kopie niet")
    res, rep = ld.plan_file_patches(g, {"loader": loader_src, "core": None}, tmp, materialize=True)
    data = open(res["loader"], "rb").read()
    check(data == orig.replace(PAT, FIX) and len(data) == len(orig), "kopie: alleen 0x1F -> 0x17")
    check(open(loader_src, "rb").read() == orig, "het origineel blijft onaangeroerd")
    check(rep[0]["status"] == "ok" and rep[0]["count"] == 1, "rapport: %s" % rep)
    res2, _ = ld.plan_file_patches(g, {"loader": loader_src, "core": None}, tmp, materialize=True)
    check(res2 == res, "tweede keer: zelfde kopie")
    # Weigeren: Find komt twee keer voor, of niet.
    for label, blob in (("twee keer", orig + PAT), ("niet", b"MZ geen patroon")):
        bad = os.path.join(tmp, "rt-bad", "OpenParrotLoader.exe")
        write(bad, blob, "wb")
        before = set(os.listdir(os.path.join(ld.HOME, ".local/share/ravengun/patched")))
        try:
            ld.plan_file_patches(g, {"loader": bad, "core": None}, tmp, materialize=True)
            check(False, "Find %s: moet weigeren" % label)
        except ld.PatchRefused as e:
            check("Lader zonder FP" in str(e) and "verwacht 1" in str(e), "melding noemt de patch: %s" % e)
            check("verwacht gameversie 1.08; de bytes kloppen niet" in str(e), "melding noemt de gameversie: %s" % e)
        check(open(bad, "rb").read() == blob, "origineel blijft bij weigering")
        check(set(os.listdir(os.path.join(ld.HOME, ".local/share/ravengun/patched"))) == before, "weigering maakt geen kopie")
    # Een gebruikerslaag zet de patch uit; TargetName filtert; een ander doel dan loader/core weigert.
    write(os.path.join(user, "Gx", "patches.ini"), "[Patch: Lader zonder FP]\nEnabled = false\n")
    check(ld.plan_file_patches(gsm.load_game("Gx"), {"loader": loader_src, "core": None}, tmp, materialize=True)[0] == {},
          "gebruiker zet de patch uit")
    os.unlink(os.path.join(user, "Gx", "patches.ini"))
    other = os.path.join(tmp, "rt", "BudgieLoader.exe")
    write(other, orig, "wb")
    res3, rep3 = ld.plan_file_patches(g, {"loader": other, "core": None}, tmp, materialize=True)
    check(res3 == {} and "niet van toepassing" in rep3[0]["status"], "TargetName: andere lader slaat de patch over")
    write(os.path.join(user, "Gx", "patches.ini"),
          "[Patch: Eigen]\nEnabled = true\nKind = file\nCategory = paths\nTarget = pipehelper.exe\nFind = 00\nReplace = 01\n")
    try:
        ld.plan_file_patches(gsm.load_game("Gx"), {"loader": loader_src, "core": None}, tmp, materialize=True)
        check(False, "Target dat geen lader of kern is moet weigeren")
    except ld.PatchRefused as e:
        check("Eigen" in str(e), "melding noemt de eigen patch: %s" % e)
    os.unlink(os.path.join(user, "Gx", "patches.ini"))
    # Nooit een doel op de NAS (~/mnt).
    nas_loader = os.path.join(ld.HOME, "mnt", "nas", "OpenParrotLoader.exe")
    write(nas_loader, orig, "wb")
    try:
        ld.plan_file_patches(g, {"loader": nas_loader, "core": None}, tmp, materialize=True)
        check(False, "een doel op de NAS moet weigeren")
    except ld.PatchRefused as e:
        check("NAS" in str(e), "NAS-melding: %s" % e)
    check(open(nas_loader, "rb").read() == orig, "NAS-bestand onaangeroerd")
    # ExeSha256: een andere exe-versie weigert.
    write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\nExeSha256 = " + "0" * 64 + "\n")
    exe = os.path.join(tmp, "rt", "game.exe")
    write(exe, b"exe", "wb")
    try:
        ld.plan_file_patches(gsm.load_game("Gx"), {"loader": loader_src, "core": None}, tmp, materialize=True, exe_path=exe)
        check(False, "ExeSha256 die niet klopt moet weigeren")
    except ld.PatchRefused as e:
        check("sha256" in str(e), "ExeSha256-melding: %s" % e)
    write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\nExeSha256 = " + _h.sha256(b"exe").hexdigest() + "\n")
    check(ld.plan_file_patches(gsm.load_game("Gx"), {"loader": loader_src, "core": None}, tmp, materialize=False, exe_path=exe)[0] != {},
          "ExeSha256 die klopt past")
finally:
    ld.HOME = saved_home
    os.environ["HOME"] = home

# Het hele plan met een ingeschakelde patch: pad in argv, exitcode 5 bij een weigering (geen terugval op TeknoParrotUi).
profile("Gx", "OpenParrot", "Foo", exe64, "false", [("General", "Windowed", "0", "Bool", [])],
        extra="<WineRunnerPath>%s</WineRunnerPath>" % wine)
write(os.path.join(tp, "OpenParrotWin32", "OpenParrot.dll"), "x")
write(os.path.join(tp, "OpenParrotWin32", "OpenParrotLoader.exe"), orig, "wb")
write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\n")
write(os.path.join(shipped, "Gx", "patches.ini"), "[Patch: Lader zonder FP]\nEnabled = true\n")
rc, out, err = run("Gx", "--plan", "--json")
plan = json.loads(out) if rc == 0 else {}
check(rc == 0 and "/.local/share/ravengun/patched/" in plan["argv"][1] and plan["patches"][0]["status"] == "ok", "plan met patch (%s %s)" % (rc, err))
check(not os.path.exists(os.path.join(home, ".local/share/ravengun/patched")), "--plan schrijft geen kopie")
write(os.path.join(tp, "OpenParrotWin32", "OpenParrotLoader.exe"), b"MZ geen patroon", "wb")
rc, out, err = run("Gx", "--check")
check(rc == 5 and "Lader zonder FP" in err, "weigerende patch: exit 5 (kreeg %d: %s)" % (rc, err))
write(os.path.join(user, "Gx", "patches.ini"), "[Patch: Lader zonder FP]\nEnabled = false\n")
check(run("Gx", "--check")[0] == 0, "patch uit: de lader start gewoon")
os.unlink(os.path.join(user, "Gx", "patches.ini"))
c = ld.A11Collector("x")
for line in ["patch 'NoDongle' @0x0804A000", "patch 'X' @0x10 skipped: unexpected original bytes 90 90",
             "RUNNING Terminator 1.25 US", "[gamescope] RUNNING nee"]:
    c.feed(line)
check(len(c.hits["toegepast"]) == 1 and len(c.hits["overgeslagen"]) == 1 and len(c.hits["versie"]) == 1, "A11-regels: %s" % c.hits)

# Vaste logregel per patch (ook uitgeschakelde), Version in de weigering, Category verplicht, en de regiokeuzes van het profiel.
write(os.path.join(shipped, "Gl", "game.ini"), "[Game]\nType = OpenParrot\n")
write(os.path.join(shipped, "Gl", "patches.ini"),
      "[Patch: Lader zonder FP]\nEnabled = true\n"
      "[Patch: Gun uit]\nEnabled = false\nKind = memory\nCategory = input\nAppliedBy = core\nModule = game.exe\nOffset = 0x9E410\n"
      "[Patch: Geheugen aan]\nEnabled = true\nKind = memory\nCategory = audio\nAppliedBy = core\nModule = game.exe\nOffset = 0x10\n"
      "[Patch: Zonder versie]\nEnabled = true\nKind = file\nCategory = loader\nTarget = core\nFind = AA BB\nReplace = CC DD\n")
write(os.path.join(tp, "OpenParrotWin32", "OpenParrot.dll"), "x")
write(loader_src, orig, "wb")
core = os.path.join(tmp, "rt", "core.dll")
write(core, b"\x00" * 8, "wb")
gl = gsm.load_game("Gl")
try:
    ld.plan_file_patches(gl, {"loader": loader_src, "core": core}, tmp, materialize=False)
    check(False, "patch zonder versie met een Find die niet past moet weigeren")
except ld.PatchRefused as e:
    check("geweigerd" in str(e) and "Zonder versie" in str(e) and "gameversie" not in str(e), "weigering zonder Version: %s" % e)
write(core, b"\x00\xAA\xBB\x00", "wb")
res, rep = ld.plan_file_patches(gl, {"loader": loader_src, "core": core}, tmp, materialize=False)
lines = ld.patch_log_lines(rep)
check(lines == ["toegepast 'Lader zonder FP' @loader:0xC",
                "overgeslagen 'Gun uit' @game.exe+0x9E410 [uit]",
                "overgeslagen 'Geheugen aan' @game.exe+0x10 [soort memory wordt niet door RavenGun toegepast (AppliedBy = core: de OpenParrot-kern doet dit zelf)]",
                "toegepast 'Zonder versie' @core:0x1"], "logregels: %s" % lines)
check([r["category"] for r in rep] == ["loader", "input", "audio", "loader"] and rep[0]["version"] == "1.08", "rapport: categorie en versie")
# Een ingeschakelde patch zonder Category (of met een onbekende) weigert de start, ook uit de gebruikerslaag; een uitgeschakelde wordt overgeslagen.
write(os.path.join(user, "Gl", "patches.ini"), "[Patch: Slecht]\nEnabled = true\nKind = file\nTarget = core\nFind = 00\nReplace = 01\n")
try:
    ld.plan_file_patches(gsm.load_game("Gl"), {"loader": loader_src, "core": core}, tmp, materialize=False)
    check(False, "patch zonder Category moet weigeren")
except ld.PatchRefused as e:
    check("Slecht" in str(e) and "Category ontbreekt" in str(e), "melding Category: %s" % e)
write(os.path.join(user, "Gl", "patches.ini"), "[Patch: Slecht]\nEnabled = false\nKind = file\nCategory = beveiliging\n")
res, rep = ld.plan_file_patches(gsm.load_game("Gl"), {"loader": loader_src, "core": core}, tmp, materialize=False)
check(any(l.startswith("overgeslagen 'Slecht' @- [afgewezen: ") for l in ld.patch_log_lines(rep)), "afgewezen patch staat in het log: %s" % ld.patch_log_lines(rep))
os.unlink(os.path.join(user, "Gl", "patches.ini"))

# Regiokeuzes (gewone profielopties DongleRegion en PcbRegion): alleen waarden uit de keuzelijst; ze komen in teknoparrot.ini.
profile("Reg", "TeknoParrot", "SegaJvsLetsGoIsland", exe64, "false",
        [("General", "DongleRegion", "JAPAN", "DropdownIndex", ["JAPAN", "USA", "EXPORT"]),
         ("General", "PcbRegion", "JAPAN", "DropdownIndex", ["JAPAN", "USA", "EXPORT"])], extra="<WineRunnerPath>%s</WineRunnerPath>" % wine)
write(os.path.join(tp, "OpenParrotWin32", "OpenParrotLoader.exe"), "x")
rc, out, err = run("Reg", "--plan", "--json")
ini = dict((k, v) for k, v in (l.split("=", 1) for l in json.loads(out)["files"][0][1].splitlines() if "=" in l))
check(ini.get("DongleRegion") == "0" and ini.get("PcbRegion") == "0", "zonder keuze: de waarde van het profiel (%s)" % ini)
write(os.path.join(shipped, "Reg", "game.ini"), "[Game]\nDongleRegion = USA\nPcbRegion = EXPORT\n")
rc, out, err = run("Reg", "--plan", "--json")
ini = dict((k, v) for k, v in (l.split("=", 1) for l in json.loads(out)["files"][0][1].splitlines() if "=" in l))
check(rc == 0 and ini.get("DongleRegion") == "1" and ini.get("PcbRegion") == "2", "game.ini kiest de regio: %s %s" % (rc, ini))
check("<FieldName>DongleRegion</FieldName><FieldValue>JAPAN</FieldValue>" in open(os.path.join(tp, "UserProfiles", "Reg.xml")).read(),
      "het profielbestand zelf blijft ongemoeid")
write(os.path.join(user, "Reg", "game.ini"), "[Game]\nDongleRegion = MARS\n")
rc, out, err = run("Reg", "--check")
check(rc == 5 and "Instelling geweigerd" in err and "DongleRegion = MARS" in err and "JAPAN, USA, EXPORT" in err, "onbekende regio weigert (%d: %s)" % (rc, err))
write(os.path.join(user, "Reg", "game.ini"), "[Game]\nDongleRegion =\n")
check(run("Reg", "--check")[0] == 0, "lege waarde in de gebruikerslaag zet de keuze terug")
os.unlink(os.path.join(user, "Reg", "game.ini"))
write(os.path.join(shipped, "Win", "game.ini"), "[Game]\nDongleRegion = USA\n")
rc, out, err = run("Win", "--check")
check(rc == 5 and "heeft die optie niet" in err, "profiel zonder die optie weigert (%d: %s)" % (rc, err))

# Eigen OpenParrot-lader en -kern ([Loader] Build / Core / Path, scripts/build-openparrot.sh): standaard de runtime van TeknoParrot,
# met Build = ravengun de lader (en bij EmulatorType OpenParrot de kern) uit de eigen build; de bytepatch voor de Wine-fix valt dan weg.
own = os.path.join(tmp, "own")
for rel in ("OpenParrotWin32/OpenParrotLoader.exe", "OpenParrotWin32/OpenParrot.dll", "OpenParrotx64/OpenParrotLoader64.exe"):
    write(os.path.join(own, rel), "x")
write(os.path.join(tp, "OpenParrotWin32", "OpenParrotLoader.exe"), orig, "wb")
write(os.path.join(shipped, "Gx", "patches.ini"), "[Patch: %s]\nEnabled = true\nKind = file\nCategory = loader\nTarget = loader\n"
      "TargetName = OpenParrotLoader.exe\nFind = C7 05 C0 9B 42 00 1F 00 01 00\nReplace = C7 05 C0 9B 42 00 17 00 01 00\n" % gsm.FPFIX_PATCH)
def wp(p):
    return "Z:" + p.replace("/", "\\")
def plan_of(name):
    rc, out, err = run(name, "--plan", "--json")
    check(rc == 0, "%s --plan: exit %d: %s" % (name, rc, err[-300:]))
    return json.loads(out) if rc == 0 else {"argv": ["", "", "", ""], "patches": []}
rc, out, err = run("Gx", "--plan", "--json")
plan = json.loads(out)
check(plan["argv"][1].startswith(home + "/.local/share/ravengun/patched/"), "zonder Build: lader uit de runtime, gepatcht (%s)" % plan["argv"][1])
check(plan["argv"][2] == wp(os.path.join(tp, "OpenParrotWin32/OpenParrot")), "zonder Build: kern uit de runtime")
write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\n[Loader]\nBuild = ravengun\nPath = %s\n" % own)
rc, out, err = run("Gx", "--plan", "--json")
plan = json.loads(out) if rc == 0 else {"argv": ["", "", ""], "patches": [{"result": "?"}]}
check(rc == 0 and plan["argv"][1] == os.path.join(own, "OpenParrotWin32/OpenParrotLoader.exe"), "Build = ravengun: eigen lader (%d %s %s)" % (rc, plan["argv"][1], err))
check(plan["argv"][2] == wp(os.path.join(own, "OpenParrotWin32/OpenParrot")), "Build = ravengun: eigen kern (%s)" % plan["argv"][2])
fp = [r for r in plan["patches"] if r["name"] == gsm.FPFIX_PATCH]
check(len(fp) == 1 and fp[0]["result"] == "overgeslagen" and fp[0].get("reden") == "uit", "de bytepatch is uit bij de eigen lader: %s" % plan["patches"])
check(not os.path.exists(os.path.join(home, ".local/share/ravengun/patched")), "eigen lader: geen gepatchte kopie")
write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\n[Loader]\nBuild = ravengun\nCore = teknoparrot\nPath = %s\n" % own)
plan = plan_of("Gx")
check(plan["argv"][1] == os.path.join(own, "OpenParrotWin32/OpenParrotLoader.exe") and plan["argv"][2] == wp(os.path.join(tp, "OpenParrotWin32/OpenParrot")),
      "Core = teknoparrot: eigen lader met de kern uit de runtime (%s)" % plan["argv"][1:3])
write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\n[Loader]\nCore = ravengun\nPath = %s\n" % own)
plan = plan_of("Gx")
check(plan["argv"][2] == wp(os.path.join(own, "OpenParrotWin32/OpenParrot")) and plan["argv"][1].startswith(home + "/.local/share/ravengun/patched/"),
      "alleen Core = ravengun: kern eigen, lader uit de runtime (gepatcht) (%s)" % plan["argv"][1:3])
# Standaardmap: ~/.local/share/ravengun/openparrot/current
write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\n[Loader]\nBuild = ravengun\n")
rc, out, err = run("Gx", "--check")
check(rc == 5 and "scripts/build-openparrot.sh" in err and "openparrot/current" in err, "eigen build ontbreekt: exit 5 met bouwhint (%d: %s)" % (rc, err))
for rel in ("OpenParrotWin32/OpenParrotLoader.exe", "OpenParrotWin32/OpenParrot.dll"):
    write(os.path.join(home, ".local/share/ravengun/openparrot/current", rel), "x")
plan = plan_of("Gx")
check(plan["argv"][1] == os.path.join(home, ".local/share/ravengun/openparrot/current/OpenParrotWin32/OpenParrotLoader.exe"), "standaardmap current")
write(os.path.join(shipped, "Gx", "game.ini"), "[Game]\nType = OpenParrot\n[Loader]\nBuild = mijn\n")
rc, out, err = run("Gx", "--check")
check(rc == 5 and "Build = mijn" in err, "onbekende Build weigert (%d: %s)" % (rc, err))
# 64-bit TeknoParrot-profiel: eigen lader, kern van TeknoParrot (er is geen eigen TeknoParrot-kern); een eigen kern weigert.
write(os.path.join(shipped, "Win", "game.ini"), "[Game]\nType = TeknoParrot\n[Loader]\nBuild = ravengun\nPath = %s\n" % own)
plan = plan_of("Win")
check(plan["argv"][1] == os.path.join(own, "OpenParrotx64/OpenParrotLoader64.exe") and plan["argv"][2] == wp(os.path.join(tp, "TeknoParrot/TeknoParrot64")),
      "TeknoParrot-profiel: eigen lader, TeknoParrot-kern (%s)" % plan["argv"][1:3])
write(os.path.join(shipped, "Win", "game.ini"), "[Game]\nType = TeknoParrot\n[Loader]\nCore = ravengun\nPath = %s\n" % own)
rc, out, err = run("Win", "--check")
check(rc == 5 and "alleen voor EmulatorType OpenParrot" in err, "eigen kern bij EmulatorType TeknoParrot weigert (%d: %s)" % (rc, err))
os.unlink(os.path.join(shipped, "Win", "game.ini"))
write(os.path.join(shipped, "Elf", "game.ini"), "[Game]\nType = ElfLdr2\n[Loader]\nBuild = ravengun\nPath = %s\n" % own)
rc, out, err = run("Elf", "--check")
check(rc == 5 and "alleen voor OpenParrotLoader" in err, "BudgieLoader kent geen eigen build (%d: %s)" % (rc, err))


if fails:
    for f in fails:
        print("FAIL:", f)
    sys.exit(1)
print("launch-direct: alle controles geslaagd")
PY
