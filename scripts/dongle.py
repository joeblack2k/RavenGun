#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Detectie van een echte, fysieke dongle (USB pass-through) en van de officiële runtime van de fabrikant.
# Zie docs/per-game-patches.md ("Dongle per game") en docs/decisions.md ("Dongle-optie per game: alleen USB pass-through").
#
# Wat dit doet: kijkt in /sys/bus/usb/devices/*/ naar idVendor, idProduct, manufacturer en product, en herkent een dongle aan het
# vendor-ID uit packaging/dongle-vendors.json (openbaar bekende ID's van Aladdin/SafeNet/Thales HASP en Sentinel, Wibu-Systems CodeMeter).
# Daarna controleert het of de officiële runtime van die fabrikant bestaat en draait (een proces, of een open poort op 127.0.0.1).
#
# Wat dit NIET doet, bewust:
#  - Het serienummer (het sysfs-attribuut `serial`) of enige andere unieke identificatie lezen, loggen of tonen. Er is een vaste
#    lijst attributen die gelezen mogen worden (ALLOWED_ATTRS); al het andere in sysfs wordt niet aangeraakt (tests/test_dongle.py).
#  - Een dongle nabootsen of dongle-aanroepen beantwoorden. Er is hier niets dat met de dongle praat: alleen kijken of hij er zit.
#  - Software van de fabrikant installeren, meeleveren of downloaden. Ontbreekt de runtime, dan staat er een verwijzing naar de
#    officiële installatie van de fabrikant in de melding.
#
# Gebruik:
#   scripts/dongle.py [--json]      status tonen (exit 0 = dongle gevonden en runtime in orde of onbekend, 1 = geen dongle of runtime niet actief)
# Voor tests: RAVENGUN_USB_SYSFS (standaard /sys/bus/usb/devices), RAVENGUN_PROC_ROOT (/proc), RAVENGUN_DONGLE_ROOT (/; voor "geïnstalleerd"),
# RAVENGUN_DONGLE_VENDORS (het gegevensbestand).
import json
import os
import re
import socket
import sys

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SYSFS = "/sys/bus/usb/devices"
DEFAULT_PROC = "/proc"

# De enige sysfs-attributen die gelezen worden. `serial` staat hier nooit in; zie de test die dat afdwingt.
ALLOWED_ATTRS = ("idVendor", "idProduct", "manufacturer", "product")

# Status van een runtime.
RUNNING = "running"
NOT_RUNNING = "not running"
NOT_INSTALLED = "not installed"
UNKNOWN = "unknown"

_HEX4 = re.compile(r"^[0-9a-f]{4}$")


def vendors_path():
    return os.environ.get("RAVENGUN_DONGLE_VENDORS") or os.path.join(ROOT, "packaging", "dongle-vendors.json")


def load_vendors(path=None):
    """(vendors {vendor_id: {...}}, runtimes {naam: {...}}); een ontbrekend of kapot bestand geeft twee lege dicts en een melding."""
    path = path or vendors_path()
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        return {}, {}, "kan %s niet lezen: %s" % (path, e)
    vendors = {}
    for v in data.get("vendors", []):
        vid = str(v.get("vendor_id", "")).strip().lower()
        if _HEX4.match(vid):
            vendors[vid] = v
    return vendors, dict(data.get("runtimes", {})), ""


def _clean(text, limit=64):
    """Leesbare tekst: geen stuurtekens, begrensde lengte."""
    t = "".join(ch for ch in text if ch.isprintable()).strip()
    return t[:limit]


def _read_attr(devdir, name):
    """Leest één toegestaan attribuut; None als het ontbreekt of niet te lezen is. Weigert alles buiten ALLOWED_ATTRS."""
    if name not in ALLOWED_ATTRS:
        raise ValueError("sysfs-attribuut %r mag niet gelezen worden" % name)
    try:
        with open(os.path.join(devdir, name), encoding="utf-8", errors="replace") as f:
            return f.read(256).strip()
    except OSError:
        return None


def scan_usb(vendors, sysfs=None):
    """Aangesloten USB-apparaten van een bekende dongle-fabrikant: [{vendor_id, product_id, manufacturer, product, vendor_name,
    runtime}], op volgorde van busaansluiting. Alleen vendor/product-ID en de leesbare namen; manufacturer en product worden alleen
    gelezen bij een apparaat dat al aan het vendor-ID als dongle herkend is."""
    sysfs = sysfs or os.environ.get("RAVENGUN_USB_SYSFS") or DEFAULT_SYSFS
    found = []
    try:
        names = sorted(os.listdir(sysfs))
    except OSError:
        return found
    for n in names:
        d = os.path.join(sysfs, n)
        vid = (_read_attr(d, "idVendor") or "").lower()
        if vid not in vendors:
            continue
        pid = (_read_attr(d, "idProduct") or "").lower()
        if not _HEX4.match(pid):
            pid = "????"
        v = vendors[vid]
        found.append({
            "vendor_id": vid,
            "product_id": pid,
            "manufacturer": _clean(_read_attr(d, "manufacturer") or ""),
            "product": _clean(_read_attr(d, "product") or ""),
            "vendor_name": v.get("name", ""),
            "runtime": v.get("runtime") or "",
        })
    return found


def _process_names(proc_root):
    """Namen (comm) van de lopende processen. Alleen `comm`: niet de opdrachtregel of de omgeving."""
    names = set()
    try:
        entries = os.listdir(proc_root)
    except OSError:
        return names
    for e in entries:
        if not e.isdigit():
            continue
        try:
            with open(os.path.join(proc_root, e, "comm"), encoding="utf-8", errors="replace") as f:
                names.add(f.read(64).strip())
        except OSError:
            continue
    return names


def _port_open(host, port, timeout=0.5):
    try:
        with socket.create_connection((host, int(port)), timeout=timeout):
            return True
    except (OSError, ValueError):
        return False


def _installed(spec, root):
    for p in spec.get("installed_paths", []):
        if os.path.exists(os.path.join(root, p.lstrip("/"))):
            return True
    # Ook in PATH (de mappen van de gebruiker); alleen de procesnamen, niets uitvoeren.
    for d in os.environ.get("PATH", "").split(os.pathsep):
        if not d:
            continue
        for proc in spec.get("processes", []):
            if os.path.exists(os.path.join(root, d.lstrip("/"), proc)):
                return True
    return False


def runtime_status(spec, proc_root=None, root=None):
    """{"name", "status": running|not running|not installed, "how": "process"|"port"|"", "install_url", "install_hint"}.
    Draaien = een proces met een van de namen, of de poort van de License Manager open op 127.0.0.1. Verder wordt er niets mee gedaan."""
    proc_root = proc_root or os.environ.get("RAVENGUN_PROC_ROOT") or DEFAULT_PROC
    root = root or os.environ.get("RAVENGUN_DONGLE_ROOT") or "/"
    out = {"name": spec.get("name", ""), "status": NOT_INSTALLED, "how": "",
           "install_url": spec.get("install_url", ""), "install_hint": spec.get("install_hint_en", "")}
    wanted = {p[:15] for p in spec.get("processes", [])}   # comm is op 15 tekens afgekapt
    if wanted & _process_names(proc_root):
        out.update(status=RUNNING, how="process")
    elif spec.get("tcp_port") and _port_open(spec.get("tcp_host", "127.0.0.1"), spec["tcp_port"]):
        out.update(status=RUNNING, how="port")
    elif _installed(spec, root):
        out["status"] = NOT_RUNNING
    return out


def detect(sysfs=None, proc_root=None, root=None, vendors_file=None):
    """{"dongles": [... + "runtime_status": {...} of None bij onbekende runtime], "error": ""}."""
    vendors, runtimes, err = load_vendors(vendors_file)
    dongles = scan_usb(vendors, sysfs)
    cache = {}
    for d in dongles:
        spec = runtimes.get(d["runtime"]) if d["runtime"] else None
        if not spec:
            d["runtime_status"] = None
            continue
        if d["runtime"] not in cache:
            cache[d["runtime"]] = runtime_status(spec, proc_root, root)
        d["runtime_status"] = dict(cache[d["runtime"]])
    return {"dongles": dongles, "error": err}


def dongle_label(d):
    """Weergavenaam: fabrikant en product zoals het apparaat zich noemt, anders de naam uit het gegevensbestand."""
    parts = [p for p in (d.get("manufacturer"), d.get("product")) if p]
    return " ".join(parts) if parts else (d.get("vendor_name") or "USB dongle")


def runtime_text(d):
    rs = d.get("runtime_status")
    return "unknown" if not rs else rs["status"]


def describe(d):
    """Eén regel zonder serienummer: 'Aladdin HASP (0529:0001) - runtime: running'. Wat de app toont staat in dezelfde vorm."""
    return "%s (%s:%s) - runtime: %s" % (dongle_label(d), d["vendor_id"], d["product_id"], runtime_text(d))


def evaluate(info):
    """(ok, bericht, dongle). ok = er is een dongle waarvan de runtime draait of onbekend is (dan staat dat in het bericht).
    Het bericht is Engels en noemt bij een ontbrekende runtime de officiële installatie van de fabrikant."""
    if info.get("error"):
        return False, "Cannot read the dongle vendor list: %s" % info["error"], None
    dongles = info.get("dongles", [])
    if not dongles:
        return False, "No dongle found (no USB dongle of a known maker is plugged in).", None
    best = None
    for d in dongles:
        rs = d.get("runtime_status")
        if rs is None:
            return True, "Dongle found: %s (the runtime of this maker is unknown, not checked)." % describe(d), d
        if rs["status"] == RUNNING:
            return True, "Dongle detected: %s" % describe(d), d
        best = best or d
    rs = best["runtime_status"]
    if rs["status"] == NOT_RUNNING:
        msg = "Dongle detected: %s. The %s is installed but not running; start its service." % (describe(best), rs["name"])
    else:
        msg = "Dongle detected: %s. The %s is not installed. RavenGun does not install it; use the official installation: %s" % (
            describe(best), rs["name"], rs["install_url"] or rs["install_hint"])
    return False, msg, best


def main(argv):
    as_json = "--json" in argv
    unknown = [a for a in argv if a not in ("--json",)]
    if unknown:
        print("Gebruik: %s [--json]" % sys.argv[0], file=sys.stderr)
        return 2
    info = detect()
    ok, msg, _ = evaluate(info)
    if as_json:
        info = dict(info, ok=ok, message=msg, lines=[describe(d) for d in info["dongles"]])
        print(json.dumps(info, indent=1, ensure_ascii=False))
    else:
        print(msg)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
