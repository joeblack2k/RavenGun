"""regrep.py <pe> <pub> <start> <end> <regex>  : lineair disassembleren van een RVA-bereik; toon matches met functienaam."""
import sys, re
import pefile, capstone
from ds import load_pub, sym
pe = pefile.PE(sys.argv[1], fast_load=True)
rvas, names = load_pub(sys.argv[2])
a0, a1 = int(sys.argv[3], 16), int(sys.argv[4], 16)
rx = re.compile(sys.argv[5])
ib = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
data = pe.get_data(a0, a1 - a0)
for i in md.disasm(data, ib + a0):
    line = '%s %s' % (i.mnemonic, i.op_str)
    if rx.search(line):
        a = i.address - ib
        print('%x: %s   [%s]' % (a, line, sym(rvas, names, a)))
