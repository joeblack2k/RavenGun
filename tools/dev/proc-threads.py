#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Toon per draad van een (Wine-)proces waar hij in de kernel op wacht: toestand, wchan, de syscall met argumenten en het bestand achter de fd.

Voor een spel dat vastzit (zwart beeld, hoofddraad in een Windows-aanroep die niet terugkeert): dit laat zien of de draad wacht op een futex/ntsync
(een andere draad of de wineserver), op een socket (X11/Wayland, gamescope, wineserver) of rekent. Draai het MIDDEN in de hang.

Gebruik:  tools/dev/proc-threads.py [NAAM]      (standaard MK_AGP3_FINAL.exe; elk proces waarvan de opdrachtregel NAAM bevat)
Alleen lezen uit /proc (eigen processen). Het script zoekt zichzelf niet: het slaat zijn eigen pid over.
"""
import os
import sys

SYSCALLS = {0: 'read', 1: 'write', 3: 'close', 7: 'poll', 16: 'ioctl', 17: 'pread64', 23: 'select', 45: 'recvfrom', 46: 'sendmsg', 47: 'recvmsg',
            202: 'futex', 232: 'epoll_wait', 270: 'pselect6', 271: 'ppoll', 281: 'epoll_pwait', 230: 'clock_nanosleep', 35: 'nanosleep', 61: 'wait4',
            0x1b4: 'futex_waitv'}
FD_FIRST_ARG = {'read', 'write', 'recvfrom', 'recvmsg', 'sendmsg', 'ioctl', 'pread64'}


def rd(path):
    try:
        with open(path, 'rb') as f:
            return f.read().decode('utf-8', 'replace').strip()
    except OSError:
        return ''


def find(name):
    me = os.getpid()
    out = []
    for p in os.listdir('/proc'):
        if not p.isdigit() or int(p) == me:
            continue
        cmd = rd(f'/proc/{p}/cmdline').replace('\0', ' ')
        if name in cmd and 'proc-threads' not in cmd:
            out.append(int(p))
    return out


def fdlink(pid, fd):
    try:
        return os.readlink(f'/proc/{pid}/fd/{fd}')
    except OSError:
        return '?'


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'MK_AGP3_FINAL.exe'
    pids = find(name)
    if not pids:
        print(f'geen proces met "{name}" in de opdrachtregel')
        return 1
    for pid in pids:
        print(f'== pid {pid}: {rd(f"/proc/{pid}/cmdline").replace(chr(0), " ")[:120]}')
        for tid in sorted(int(t) for t in os.listdir(f'/proc/{pid}/task')):
            base = f'/proc/{pid}/task/{tid}'
            comm = rd(base + '/comm')
            stat = rd(base + '/stat')
            state = '?'
            cpu = 0
            if ')' in stat:
                rest = stat.rsplit(')', 1)[1].split()
                state = rest[0]
                cpu = int(rest[11]) + int(rest[12])   # utime + stime in klokticks
            wchan = rd(base + '/wchan')
            sc = rd(base + '/syscall').split()
            desc = ''
            if sc and sc[0].lstrip('-').isdigit():
                nr = int(sc[0])
                nm = SYSCALLS.get(nr, f'syscall {nr}')
                args = sc[1:7]
                desc = nm + '(' + ', '.join(args[:3]) + ')'
                if nm in FD_FIRST_ARG and args:
                    try:
                        desc += ' fd -> ' + fdlink(pid, int(args[0], 16))
                    except ValueError:
                        pass
            elif sc:
                desc = ' '.join(sc)
            print(f'  {tid:7d} {comm:<16.16} {state} cpu {cpu:6d}  wchan {wchan or "-":<22.22} {desc}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
