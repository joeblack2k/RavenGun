#!/usr/bin/env python3
"""List the dynamic symbols of a (possibly section-header-stripped) ELF64."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf_probe import parse

path = sys.argv[1]
r = parse(path)
print("file: %s" % path)
print("UNDEFINED (%d):" % len(r["undef"]))
for i, s in enumerate(r["undef"]):
    print("  %4d %s" % (i, s))
print("DEFINED (%d):" % len(r["defined"]))
for i, s in enumerate(r["defined"]):
    print("  %4d %s" % (i, s))
