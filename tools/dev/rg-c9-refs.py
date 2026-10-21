#!/usr/bin/env python3
"""Inventory every reference to the packer's runtime memory in Coopers's 9's game.exe (protection lane, step 1).

game.exe (Coopers's 9) is a flat memory image of a protected process (file offset == RVA, see
tools/dev/pe-flat-fix.py), so a virtual address in .text/.protect is just file offset address - ImageBase.
The packer left the game's code pointing at memory it builds at run time and the dump does not contain:

    0x00010000-0x0001FFFF   the packer's own stub area (XP address space, below the image)
    0x02B10000-0x02B1FFFF   packer memory (a few absolute operands)
    0x03130000-0x0313FFFF   packer memory (the 0x0313xxxx stubs)

The game reaches those addresses almost only through its own call table in .data at 0x512000 (`call [0x512xxx]`,
`jmp [0x512xxx]`, or a slot loaded into a register and called later); some sites use an immediate directly.

What this tool reports:

  * per slot of the call table: the dword in the dump, which area it points into, every site that uses it
    (call/jmp/register), the arguments the site pushes (count, and string constants among them), whether the
    caller reads eax afterwards, and whether the site is a jmp that leaves the function (stolen code) or a call
    that returns and continues (an API-like stub);
  * every direct call/jmp/push/mov immediate that points into one of the areas (outside the table).

It is the measurement behind step 1 of the Coopers's 9 protection lane and the input for step 2 (naming each
stub from the call site). Usage: rg-c9-refs.py [game.exe] [--json out.json] [--slots] [--verbose]
"""
import argparse
import json
import os
import struct
import sys
from collections import defaultdict

import capstone

BASE = 0x400000                      # ImageBase of game.exe
TABLE = 0x512000                     # the game's own call table in .data
CELLS = 256
AREAS = (
    (0x00010000, 0x0001FFFF, "stubs-0x10000"),
    (0x02B10000, 0x02B1FFFF, "packer-0x02B1"),
    (0x03130000, 0x0313FFFF, "table-0x0313"),
)
DEFAULT = os.path.expanduser("~/.local/share/ravengun/runtime/views/Coopers9/game.exe")

REGS = {
    capstone.x86.X86_REG_EAX: "eax", capstone.x86.X86_REG_EBX: "ebx",
    capstone.x86.X86_REG_ECX: "ecx", capstone.x86.X86_REG_EDX: "edx",
    capstone.x86.X86_REG_ESI: "esi", capstone.x86.X86_REG_EDI: "edi",
    capstone.x86.X86_REG_EBP: "ebp", capstone.x86.X86_REG_ESP: "esp",
}


def area_of(va):
    for lo, hi, name in AREAS:
        if lo <= va <= hi:
            return name
    return None


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    sh = opt + struct.unpack_from("<H", data, pe + 20)[0]
    out = []
    for i in range(nsec):
        o = sh + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("latin1")
        vs, va, rs, ra = struct.unpack_from("<IIII", data, o + 8)
        out.append((name, va, vs, ra))
    return out


def read_dword(data, va):
    off = va - BASE
    if 0 <= off <= len(data) - 4:
        return struct.unpack_from("<I", data, off)[0]
    return None


def read_cstring(data, va, limit=64):
    off = va - BASE
    if not (0 <= off < len(data)):
        return None
    out = data[off:off + limit].split(b"\0")[0]
    if not out or any(c < 9 or (13 < c < 32) or c > 126 for c in out[:8]):
        return None
    return out.decode("latin1")


def entry_point(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    opt = pe + 24
    return BASE + struct.unpack_from("<I", data, opt + 16)[0]


def recursive_code(data, start, end, seeds):
    """Follow the control flow from `seeds` so that data in .text (jump tables, strings) is not decoded as code.

    Every branch target is queued, a call falls through and queues its target, a jmp/ret ends the block. An
    immediate that points into .text is taken as a function pointer and queued as a seed too (the window class
    registration passes its WNDPROC that way), which is what makes thunks like 0x4024c0 reachable.
    """
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    out = []
    seen = set()
    queue = [a for a in seeds if start <= a < end]
    while queue:
        va = queue.pop()
        while start <= va < end and va not in seen:
            seen.add(va)
            got = None
            for ins in md.disasm(data[va - BASE:min(end - BASE, va - BASE + 16)], va):
                got = ins
                break
            if got is None:
                break
            out.append(got)
            va = got.address + got.size
            is_jmp = got.group(capstone.x86.X86_GRP_JUMP)
            is_call = got.group(capstone.x86.X86_GRP_CALL)
            for op in got.operands:
                if op.type == capstone.x86.X86_OP_IMM:
                    t = op.imm & 0xFFFFFFFF
                    if start <= t < end:
                        if is_call or is_jmp:
                            queue.append(t)
                        elif got.mnemonic in ("push", "mov"):
                            queue.append(t)      # a function pointer
            if is_call:
                continue
            if is_jmp or got.id in (capstone.x86.X86_INS_RET, capstone.x86.X86_INS_JMP):
                break
            if got.id == capstone.x86.X86_INS_INT3:
                break
    out.sort(key=lambda i: i.address)
    return out


def disasm(data, start, end):
    """Linear sweep with resync: on undecodable bytes skip one byte and continue."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    va = start
    insns = []
    while va < end:
        got = False
        for ins in md.disasm(data[va - BASE:min(end - BASE, va - BASE + 16)], va):
            insns.append(ins)
            va = ins.address + ins.size
            got = True
        if not got:
            va += 1
    return insns


def eax_read_after(insns, idx, lookahead=6):
    """Read of eax before it is overwritten: the caller uses the callee's return value."""
    for ins in insns[idx + 1:idx + 1 + lookahead]:
        ops = ins.operands
        if ins.id in (capstone.x86.X86_INS_TEST, capstone.x86.X86_INS_CMP):
            if any(o.type == capstone.x86.X86_OP_REG and o.reg == capstone.x86.X86_REG_EAX for o in ops):
                return True
        reads = writes = False
        for op in ops:
            if op.type == capstone.x86.X86_OP_REG:
                if op.reg == capstone.x86.X86_REG_EAX:
                    reads |= bool(op.access & capstone.CS_AC_READ)
                    writes |= bool(op.access & capstone.CS_AC_WRITE)
            elif op.type == capstone.x86.X86_OP_MEM:
                if capstone.x86.X86_REG_EAX in (op.mem.base, op.mem.index):
                    reads |= bool(op.access & capstone.CS_AC_READ)
        if reads:
            return True
        if writes:
            return False
        if ins.group(capstone.x86.X86_GRP_JUMP) or ins.group(capstone.x86.X86_GRP_RET):
            return False
    return False


def pushes_before(insns, idx, limit=24):
    """Arguments a call site sets up: walk back over the pushes, and note msvc's `mov [esp+N], v`.

    Stops at the previous call, a branch, a ret, or anything that changes esp other than push.
    """
    args = []
    for k in range(idx - 1, max(-1, idx - 1 - limit), -1):
        ins = insns[k]
        if ins.group(capstone.x86.X86_GRP_CALL) or ins.group(capstone.x86.X86_GRP_RET):
            break
        if ins.group(capstone.x86.X86_GRP_JUMP):
            break
        if ins.id == capstone.x86.X86_INS_PUSH:
            op = ins.operands[0]
            if op.type == capstone.x86.X86_OP_IMM:
                args.append(("imm", op.imm & 0xFFFFFFFF))
            elif op.type == capstone.x86.X86_OP_REG:
                args.append(("reg", REGS.get(op.reg, "?")))
            else:
                args.append(("mem", ins.op_str))
            continue
        if ins.id == capstone.x86.X86_INS_MOV and len(ins.operands) == 2:
            dst, src = ins.operands
            if dst.type == capstone.x86.X86_OP_MEM and dst.mem.base == capstone.x86.X86_REG_ESP:
                if src.type == capstone.x86.X86_OP_REG and src.reg == capstone.x86.X86_REG_EAX:
                    args.append(("eaxslot", dst.mem.disp))
                    continue
                if src.type == capstone.x86.X86_OP_IMM:
                    args.append(("immslot", src.imm & 0xFFFFFFFF))
                    continue
            continue
        if ins.id in (capstone.x86.X86_INS_SUB, capstone.x86.X86_INS_ADD) and \
                ins.operands[0].type == capstone.x86.X86_OP_REG and ins.operands[0].reg == capstone.x86.X86_REG_ESP:
            break
        if ins.id == capstone.x86.X86_INS_LEA and ins.operands[0].type == capstone.x86.X86_OP_REG \
                and ins.operands[0].reg == capstone.x86.X86_REG_ESP:
            break
        # anything else is setup work (mov/lea/rep stosd) that does not touch the stack
    args.reverse()
    return args


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", nargs="?", default=DEFAULT)
    ap.add_argument("--json")
    ap.add_argument("--slots", action="store_true", help="print every slot of the table and its sites")
    ap.add_argument("--verbose", action="store_true", help="print the instruction context of every site")
    ap.add_argument("--include-protect", action="store_true",
                    help="also sweep .protect (measured: structured data, entropy 4.8, not code; its sweep only "
                         "yields garbage sites, so it is off by default)")
    ap.add_argument("--linear", action="store_true",
                    help="sweep linearly instead of following the control flow from the entry point (more sites, "
                         "but data in .text is then decoded as code)")
    args = ap.parse_args()

    data = open(args.exe, "rb").read()
    seclist = sections(data)
    wanted = (".text", ".protect") if args.include_protect else (".text",)
    code_ranges = [(BASE + va, BASE + va + vs) for name, va, vs, ra in seclist if name in wanted and vs]
    if not code_ranges:
        sys.exit("no .text/.protect in %s" % args.exe)

    insns = []
    for start, end in code_ranges:
        if args.linear:
            insns.extend(disasm(data, start, end))
        else:
            # Seeds: the entry point, the registered WNDPROC thunk, and every immediate of a first pass that
            # points into the code range (a call/jmp target or a function pointer stored in the code). Starting
            # the descent at real targets is what keeps a misaligned jump table from being decoded as code.
            seeds = {entry_point(data), 0x4024C0}
            for ins in disasm(data, start, end):
                if ins.id == 0:
                    continue
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_IMM and start <= (op.imm & 0xFFFFFFFF) < end:
                        seeds.add(op.imm & 0xFFFFFFFF)
            insns.extend(recursive_code(data, start, end, sorted(seeds)))
            print("%-28s %6d instructions from %d seeds" % ("code map", len(insns), len(seeds)))
    insns = [i for i in insns if i.id != 0]      # skipdata entries carry no operands
    insns.sort(key=lambda i: i.address)

    # slot -> sites. A site is a call/jmp through the cell, or a load of the cell into a register that is then
    # called or jumped to (msvc loads the target first when it needs it twice).
    slots = defaultdict(list)
    immediates = defaultdict(list)               # area address -> sites that name it directly
    constants = [0]                              # immediates in an area that are plain arithmetic constants
    REF_MNEMS = {"call", "jmp", "push", "mov", "lea", "movzx", "movsx"}
    for n, ins in enumerate(insns):
        is_call = ins.group(capstone.x86.X86_GRP_CALL)
        is_jmp = ins.group(capstone.x86.X86_GRP_JUMP)
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_IMM and area_of(op.imm & 0xFFFFFFFF):
                if ins.mnemonic not in REF_MNEMS:
                    constants[0] += 1          # `and eax, 0x10000` and friends: a mask, not an address
                    continue
                kind = "call" if is_call else "jmp" if is_jmp else ins.mnemonic
                immediates[op.imm & 0xFFFFFFFF].append({
                    "site": ins.address, "kind": kind,
                    "mnem": "%s %s" % (ins.mnemonic, ins.op_str), "args": pushes_before(insns, n),
                })
        if not (is_call or is_jmp):
            continue
        target_reg = None
        cell = None
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                cell = op.mem.disp & 0xFFFFFFFF
            elif op.type == capstone.x86.X86_OP_REG:
                target_reg = op.reg
        if cell is None and target_reg is not None:
            for k in range(n - 1, max(-1, n - 6), -1):
                prev = insns[k]
                if prev.id == capstone.x86.X86_INS_MOV and len(prev.operands) == 2:
                    dst, src = prev.operands
                    if dst.type == capstone.x86.X86_OP_REG and dst.reg == target_reg and \
                            src.type == capstone.x86.X86_OP_MEM and src.mem.base == 0 and src.mem.index == 0:
                        cell = src.mem.disp & 0xFFFFFFFF
                        break
        if cell is None or not (TABLE <= cell < TABLE + CELLS * 4):
            continue
        slots[(cell - TABLE) // 4].append({
            "site": ins.address,
            "kind": ("call" if is_call else "jmp") + ("[%08x]" % cell if not target_reg else " via " + REGS.get(target_reg, "?")),
            "mnem": "%s %s" % (ins.mnemonic, ins.op_str),
            "uses_eax": eax_read_after(insns, n) if is_call else None,
            "args": pushes_before(insns, n),
            "ctx": ["%08x %s %s" % (x.address, x.mnemonic, x.op_str)
                    for x in insns[max(0, n - 4):n + 3]],
        })

    def area_of_slot(slot):
        v = read_dword(data, TABLE + slot * 4)
        return (v, area_of(v) if v else None)

    by_area = defaultdict(lambda: [0, 0, 0, 0])   # slots, sites, calls, jmps
    named = [0, 0]
    for slot, sites in slots.items():
        val, a = area_of_slot(slot)
        if a:
            e = by_area[a]
            e[0] += 1
            e[1] += len(sites)
            e[2] += sum(1 for s in sites if s["kind"].startswith("call"))
            e[3] += sum(1 for s in sites if s["kind"].startswith("jmp"))
        elif val:
            named[0] += 1
            named[1] += len(sites)
    print("call table %08x: %d of %d slots in use, %d sites" %
          (TABLE, len(slots), CELLS, sum(len(v) for v in slots.values())))
    for a, (ns, nsite, nc, nj) in sorted(by_area.items()):
        print("  %-16s %2d slots, %4d sites (%d call, %d jmp)" % (a, ns, nsite, nc, nj))
    print("  %-16s %2d slots, %4d sites" % ("XP-named", named[0], named[1]))

    if args.slots or args.verbose:
        for slot in sorted(slots):
            val, a = area_of_slot(slot)
            if not a and not args.verbose:
                continue
            print("slot %3d cell %08x value %08x %-16s %d sites" %
                  (slot, TABLE + slot * 4, val or 0, a or "", len(slots[slot])))
            for s in slots[slot]:
                argtxt = []
                for kind, v in s["args"]:
                    if kind == "imm":
                        txt = read_cstring(data, v)
                        argtxt.append('"%s"' % txt if txt else "%08x" % v)
                    else:
                        argtxt.append(str(v))
                print("    %08x %-18s eax=%-5s args[%d] %s" %
                      (s["site"], s["kind"], s["uses_eax"], len(s["args"]), " ".join(argtxt[:8])))
                if args.verbose:
                    for c in s["ctx"]:
                        print("        %s" % c)

    print("direct references into the areas (call/jmp/push/mov/lea with an area address): %d sites in %d targets;"
          " %d further immediates in those ranges are arithmetic constants (masks, sizes, timeouts)" %
          (sum(len(v) for v in immediates.values()), len(immediates), constants[0]))
    if immediates:
        for tgt in sorted(immediates):
            print("  %08x (%s) %d sites" % (tgt, area_of(tgt), len(immediates[tgt])))
            for s in immediates[tgt]:
                print("    %08x %-6s %s" % (s["site"], s["kind"], s["mnem"]))
    else:
        print("  none: every reference to the packer's memory goes through the 0x512000 table")

    if args.json:
        def slot_json(slot):
            val, a = area_of_slot(slot)
            return {"cell": TABLE + slot * 4, "value": val, "area": a, "sites": slots[slot]}
        out = {"table": TABLE, "slots": {str(k): slot_json(k) for k in slots},
               "immediates": {("%08x" % k): v for k, v in immediates.items()}}
        with open(args.json, "w") as f:
            json.dump(out, f, indent=1)
        print("wrote %s" % args.json)


if __name__ == "__main__":
    main()
