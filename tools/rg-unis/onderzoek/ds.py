"""dis.py <pe> <pubfile|-> <rva hex> [nbytes] [--all]   disassembleer met namen voor call-doelen en tekenreeksen."""
import sys, re, bisect
import pefile, capstone

def load_pub(fn):
    rvas, names = [], []
    if fn == '-':
        return rvas, names
    for line in open(fn, errors='replace'):
        a, _, n = line.rstrip('\n').partition(' ')
        rvas.append(int(a, 16)); names.append(n)
    return rvas, names

def sym(rvas, names, rva):
    i = bisect.bisect_right(rvas, rva) - 1
    if i < 0:
        return None
    # alle namen op dit adres, kort
    d = rva - rvas[i]
    j = i
    while j > 0 and rvas[j - 1] == rvas[i]:
        j -= 1
    nm = names[j]
    return nm[:90] + ('+%x' % d if d else '')

def str_at(pe, rva):
    try:
        b = pe.get_data(rva, 80)
    except Exception:
        return None
    # ASCII
    m = re.match(rb'[\x20-\x7e]{4,}', b)
    if m:
        return 'A"%s"' % m.group().decode()
    m = re.match(rb'(?:[\x20-\x7e]\x00){3,}', b)
    if m:
        return 'W"%s"' % m.group().decode('utf-16le')
    return None

def run(pe, rvas, names, rva, n, stop=True):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ib = pe.OPTIONAL_HEADER.ImageBase
    data = pe.get_data(rva, n)
    out = []
    for i in md.disasm(data, ib + rva):
        a = i.address - ib
        extra = ''
        if i.mnemonic in ('call', 'jmp') and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
            t = i.operands[0].imm - ib
            extra = '  ; ' + (sym(rvas, names, t) or '?')
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                t = a + i.size + op.mem.disp
                s = str_at(pe, t) if i.mnemonic == 'lea' else None
                extra = '  ; [%x] %s' % (t, s or (sym(rvas, names, t) or ''))
        out.append('%x: %s %s%s' % (a, i.mnemonic, i.op_str, extra))
        if stop and i.mnemonic == 'ret':
            break
    return out

if __name__ == '__main__':
    exe, pub, rva = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    n = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x400
    pe = pefile.PE(exe, fast_load=True)
    rvas, names = load_pub(pub)
    print('\n'.join(run(pe, rvas, names, rva, n, '--all' not in sys.argv)))
