"""dll.py <dll> <exportnaam> [n]  disassembleer een export van de UNIS-DLL met namen voor imports."""
import sys, bisect
import pefile, capstone

pe = pefile.PE(sys.argv[1], fast_load=False)
ib = pe.OPTIONAL_HEADER.ImageBase
ex = {e.name.decode(): e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
exr = {v: k for k, v in ex.items()}
imp = {}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for i in d.imports:
        if i.name:
            imp[i.address - ib] = d.dll.decode() + '!' + i.name.decode()

def run(rva, n, stop=True):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    data = pe.get_data(rva, n)
    for i in md.disasm(data, ib + rva):
        a = i.address - ib
        extra = ''
        if i.mnemonic in ('call', 'jmp') and i.operands:
            o = i.operands[0]
            if o.type == capstone.x86.X86_OP_IMM:
                t = o.imm - ib
                extra = '  ; sub_%x%s' % (t, ' ' + exr[t] if t in exr else '')
            elif o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RIP:
                t = a + i.size + o.mem.disp
                extra = '  ; ' + imp.get(t, '[%x]' % t)
        else:
            for o in i.operands:
                if o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RIP:
                    t = a + i.size + o.mem.disp
                    s = ''
                    try:
                        b = pe.get_data(t, 40)
                        import re
                        m = re.match(rb'[\x20-\x7e]{3,}', b)
                        m2 = re.match(rb'(?:[\x20-\x7e]\x00){3,}', b)
                        if m: s = ' "%s"' % m.group().decode()
                        elif m2: s = ' L"%s"' % m2.group().decode('utf-16le')
                    except Exception:
                        pass
                    extra = '  ; [%x]%s' % (t, s)
        print('%x: %s %s%s' % (a, i.mnemonic, i.op_str, extra))
        if stop and i.mnemonic == 'ret':
            break

if sys.argv[2].startswith('0x'):
    rva = int(sys.argv[2], 16)
else:
    rva = ex[sys.argv[2]]
n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x400
run(rva, n, '--all' not in sys.argv)
