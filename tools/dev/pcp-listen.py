#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Meetluisteraar voor de AM-daemonpoorten van de Sega Ring-spellen (127.0.0.1:40100..40115).

Wat de statische scan (tools/dev/segajvs-scan.py) en de leesbare libpcp-code van de spellen laten zien: de AM Library in de exe
praat tekst met een daemon die op een prompt `>` wacht (pcppCheckPrompt zoekt een `>` in de ontvangen bytes; pcppSetSendPacket
bouwt `cmd=arg\\r\\n` en `&sleutel=waarde\\r\\n`; het tokeniseren kent alleen `=`, `&` en CRLF als scheiding). Op een echte kast is
dat de AM-daemon. Dit hulpmiddel is geen daemon: het is een luisteraar die laat zien WAT de game vraagt, zodat een echte
vervanging op gemeten verzoeken gebouwd wordt en niet op raden.

Wat het doet, per verbinding: optioneel een prompt sturen bij het verbinden (`--greet`), alles wat binnenkomt loggen (tekst en
hex, met de poort), en optioneel na elk verzoek (stilte van `--idle` seconden) een prompt terugsturen (`--prompt-after`).
Het beantwoordt NIETS inhoudelijks; een game die op een antwoord wacht blijft wachten en dat is dan de meting.

Raakt geen daemon, geen Wine en geen bestand buiten de logregel op stdout (of `--log BESTAND`).

Gebruik:  python3 -I tools/dev/pcp-listen.py [--ports 40100,40106,40107] [--greet] [--prompt-after] [--idle 0.2] [--log F]
"""
import argparse
import socket
import sys
import threading
import time

DEFAULT_PORTS = (40100, 40102, 40103, 40104, 40106, 40107, 40108, 40114, 40115)
PROMPT = b">"


def printable(b):
    """Tekstweergave van bytes: CR/LF zichtbaar, niet-afdrukbare bytes als \\xNN."""
    out = []
    for c in b:
        if c == 13:
            out.append("\\r")
        elif c == 10:
            out.append("\\n")
        elif 32 <= c < 127:
            out.append(chr(c))
        else:
            out.append("\\x%02x" % c)
    return "".join(out)


def split_requests(buf):
    """Verdeel ontvangen bytes in complete regels (CRLF) en een rest zonder afsluiting."""
    lines = []
    while True:
        i = buf.find(b"\r\n")
        if i < 0:
            break
        lines.append(buf[:i])
        buf = buf[i + 2:]
    return lines, buf


class Listener:
    def __init__(self, ports, greet=False, prompt_after=False, idle=0.2, log=None, host="127.0.0.1"):
        self.greet, self.prompt_after, self.idle = greet, prompt_after, idle
        self.log = log or (lambda s: print(s, flush=True))
        self.socks = {}
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.threads = []
        for p in ports:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                s.bind((host, p))
                s.listen(8)
                s.settimeout(0.2)
            except OSError:
                s.close()
                self.close()          # niets half open laten staan als één poort bezet is
                raise
            self.socks[p] = s

    def emit(self, port, text):
        with self.lock:
            self.log("%s :%d %s" % (time.strftime("%H:%M:%S"), port, text))

    def serve(self, port, conn, addr):
        self.emit(port, "verbonden vanaf %s:%d" % addr)
        conn.settimeout(self.idle if self.prompt_after else 0.5)
        if self.greet:
            conn.sendall(PROMPT)
            self.emit(port, "-> prompt '>'")
        pending, rest = 0, b""
        try:
            while not self.stop.is_set():
                try:
                    data = conn.recv(4096)
                except socket.timeout:
                    if self.prompt_after and pending:
                        conn.sendall(PROMPT)
                        self.emit(port, "-> prompt '>' na %d byte(s) verzoek" % pending)
                        pending = 0
                    continue
                if not data:
                    break
                lines, rest = split_requests(rest + data)
                self.emit(port, "<- %d byte(s): %s" % (len(data), printable(data)))
                for ln in lines:
                    self.emit(port, "   regel: %s" % printable(ln))
                if rest:
                    self.emit(port, "   zonder afsluiting: %s" % printable(rest))
                pending += len(data)
        except OSError as e:
            self.emit(port, "fout: %s" % e)
        finally:
            conn.close()
            self.emit(port, "verbinding dicht")

    def accept_loop(self, port, s):
        while not self.stop.is_set():
            try:
                conn, addr = s.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            t = threading.Thread(target=self.serve, args=(port, conn, addr), daemon=True)
            t.start()

    def start(self):
        for p, s in self.socks.items():
            t = threading.Thread(target=self.accept_loop, args=(p, s), daemon=True)
            t.start()
            self.threads.append(t)

    def close(self):
        self.stop.set()
        for s in self.socks.values():
            s.close()


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ports", default=",".join(map(str, DEFAULT_PORTS)))
    ap.add_argument("--greet", action="store_true", help="stuur een prompt '>' direct na het verbinden")
    ap.add_argument("--prompt-after", action="store_true", help="stuur een prompt na elk verzoek (na --idle seconden stilte)")
    ap.add_argument("--idle", type=float, default=0.2)
    ap.add_argument("--log", help="schrijf de regels ook naar dit bestand")
    a = ap.parse_args(argv)
    ports = [int(x) for x in a.ports.split(",") if x.strip()]
    fh = open(a.log, "a") if a.log else None

    def log(s):
        print(s, flush=True)
        if fh:
            fh.write(s + "\n")
            fh.flush()

    try:
        lst = Listener(ports, a.greet, a.prompt_after, a.idle, log)
    except OSError as e:
        print("kan niet luisteren: %s (draait er al een daemon of een andere luisteraar?)" % e, file=sys.stderr)
        return 1
    lst.start()
    log("luistert op 127.0.0.1 poorten %s (greet=%s, prompt-after=%s); Ctrl-C stopt" % (ports, a.greet, a.prompt_after))
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    lst.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
