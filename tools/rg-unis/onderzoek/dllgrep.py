"""dllgrep.py <dll> <regex>   lineair disassembleren van .text, regels tonen die de regex matchen (met functiestart)."""
import sys, re
import pefile, capstone
pe = pefile.PE(sys.argv[1], fast_load=False)
ib = pe.OPTIONAL_HEADER.ImageBase
t = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
data = t.get_data()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
rx = re.compile(sys.argv[2])
fn = None
prev = None
for i in md.disasm(data, ib + t.VirtualAddress):
    a = i.address - ib
    line = '%x: %s %s' % (a, i.mnemonic, i.op_str)
    if rx.search(line):
        print(line)
