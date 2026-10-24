#!/usr/bin/env python3
"""luigi-com1-pty.py -- pty-logger voor COM1 van Luigi's Mansion Arcade.

Wat het doet: maakt een pty-paar, zet de slave achter een vaste naam (standaard
~/.local/share/ravengun/run/luigi-com1) zodat de Wine-prefix  dosdevices/com1  daarnaar
kan wijzen, en schrijft ELKE byte die het spel (of SendFlash.exe) schrijft naar de poort
en ELKE byte die het terugleest weg met een tijdstempel. Optioneel antwoordt het ook,
zodat je kunt meten wat het spel nodig heeft in plaats van alleen wat het stuurt.

Waarom een pty en geen /dev/ttyS0: Wine maakt dosdevices/com1..com32 zelf aan en laat ze
naar /dev/ttyS* wijzen. Die zijn root:uucp 0660 en 'deck' zit niet in uucp, dus de game
krijgt ACCESS_DENIED (5) -- precies het verschil dat patch 0005 wilde wegnemen. Een pty
van ons is wel van deck en geeft ons de bytes.

Let op: een pty kent TIOCMGET niet. Wine's GetCommModemStatus faalt daarop met ENOTTY.
Draai daarom met LD_PRELOAD=librg_comm_shim.so (scripts/wine-prefix.sh doet dat met
RG_LD_PRELOAD); die beantwoordt de modemstatus vanuit /dev/shm/ravengun_jvs_modem, of
met CTS+DSR aan als dat bestand er niet is.

Alleen-lezen op de NAS; dit script raakt alleen de prefix en de pty aan.

Gebruik:
  python3 luigi-com1-pty.py --link ~/.local/share/ravengun/run/luigi-com1 \
                            --log /tmp/luigi-com1.log
  # (in een tweede venster de game/SendFlash starten, zie luigi-com1-run.sh)
"""
import argparse, errno, fcntl, os, pty, select, signal, sys, termios, time

START = time.time()
STOP = False


def ts():
    return time.strftime("%H:%M:%S") + ".%03d" % int((time.time() % 1) * 1000)


def opener(pad):
    return open(pad, "a", buffering=1, encoding="utf-8")


def schrijf(log, richting, data, noot=""):
    """Een regel per datablok: tijd, mono, richting, hex, ascii."""
    regel = "%s\t%8.3f\t%s\t%s\t%s%s\n" % (
        ts(), time.time() - START, richting, data.hex(" "),
        "".join(chr(b) if 32 <= b < 127 else "." for b in data),
        ("   # " + noot) if noot else "")
    log.write(regel)
    log.flush()
    sys.stdout.write(regel)
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--link", required=True,
                    help="vaste naam voor de pty-slave (hier wijst dosdevices/com1 naar)")
    ap.add_argument("--log", default="/tmp/luigi-com1.log")
    ap.add_argument("--reply-hex", default="",
                    help="hex die NA ELK ontvangen blok teruggeschreven wordt, bv. '06'")
    ap.add_argument("--reply-once", action="store_true",
                    help="antwoord maar een keer in plaats van na elk blok")
    ap.add_argument("--reply-delay-ms", type=int, default=0)
    ap.add_argument("--echo", action="store_true", help="alles terugsturen dat binnenkomt")
    ap.add_argument("--idle-timeout", type=float, default=0.0,
                    help="stoppen na N s zonder verkeer (0 = nooit)")
    ap.add_argument("--max-seconds", type=float, default=0.0, help="harde bovengrens")
    ap.add_argument("--banner", default="", help="regel die als commentaar in het log komt")
    args = ap.parse_args()

    reply = bytes.fromhex(args.reply_hex.replace(" ", "")) if args.reply_hex else b""
    if args.echo:
        reply = None  # signaal: echoot wat er binnenkomt

    master, slave = pty.openpty()
    # raw, geen echo, geen flow-control: wij zijn de I/O-kaart.
    attrs = termios.tcgetattr(slave)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CREAD | termios.CS8
    attrs[3] = 0
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    fcntl.fcntl(master, fcntl.F_SETFL, os.O_NONBLOCK)
    slaafnaam = os.ttyname(slave)

    pad = os.path.abspath(os.path.expanduser(args.link))
    os.makedirs(os.path.dirname(pad), exist_ok=True)
    if os.path.islink(pad) or os.path.exists(pad):
        os.unlink(pad)
    os.symlink(slaafnaam, pad)

    log = opener(os.path.expanduser(args.log))
    log.write("# luigi-com1-pty: %s -> %s\n" % (pad, slaafnaam))
    log.write("# start %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    if args.banner:
        log.write("# %s\n" % args.banner)
    if reply is not None and args.reply_hex:
        log.write("# antwoord na elk blok: %s (eenmalig: %s, vertraging %d ms)\n"
                  % (reply.hex(" "), args.reply_once, args.reply_delay_ms))
    elif args.echo:
        log.write("# echo-modus: alles wordt teruggeschreven\n")
    else:
        log.write("# geen antwoord: de poort zwijgt (dit meet wat de game STUURT)\n")
    print("pty-slaaf: %s   link: %s   log: %s" % (slaafnaam, pad, args.log))

    rx = tx = 0
    laatste = time.time()
    beantwoord = False

    def stoppen(*_):
        global STOP
        STOP = True
    signal.signal(signal.SIGINT, stoppen)
    signal.signal(signal.SIGTERM, stoppen)

    while not STOP:
        r, _, _ = select.select([master], [], [], 0.25)
        if r:
            try:
                data = os.read(master, 4096)
            except OSError as e:
                if e.errno in (errno.EIO, errno.EAGAIN):
                    continue
                raise
            if data:
                rx += len(data)
                laatste = time.time()
                schrijf(log, "<<< spel schrijft", data)
                if reply is None:
                    os.write(master, data)
                    tx += len(data)
                    schrijf(log, ">>> wij antwoorden", data, "echo")
                elif reply and not (args.reply_once and beantwoord):
                    if args.reply_delay_ms:
                        time.sleep(args.reply_delay_ms / 1000.0)
                    os.write(master, reply)
                    tx += len(reply)
                    beantwoord = True
                    schrijf(log, ">>> wij antwoorden", reply)
        if args.idle_timeout and (time.time() - laatste) > args.idle_timeout and rx:
            schrijf(log, "---", b"", "geen verkeer meer (%ds) -- stoppen" % args.idle_timeout)
            break
        if args.max_seconds and (time.time() - START) > args.max_seconds:
            schrijf(log, "---", b"", "tijdslimiet bereikt")
            break

    schrijf(log, "===", b"", "einde: %d B van het spel, %d B van ons" % (rx, tx))
    log.close()
    try:
        os.unlink(pad)
    except OSError:
        pass
    os.close(master)
    os.close(slave)


if __name__ == "__main__":
    main()

