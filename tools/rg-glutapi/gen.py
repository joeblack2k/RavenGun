#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen.py -- genereert de mechanische delen van librg-glutapi (i386).

Invoer (allebei gemeten op de Deck, zie meet-namen.sh):
  names_budgie.txt         de 161 glut*-namen uit de exporttabel van BudgieLoader.exe
  names_real_libglut.txt   de 171 glut*-namen uit onze eigen libglut.so.3

Uitvoer:
  rg_stubs.S   122 jmp-stubs voor de namen die de echte libglut al heeft, plus
               122 noodstubs. De stub laadt de opgezochte aanroepwijzer en
               SPRINGT daarheen (jmp, geen call): de argumenten blijven op de
               stapel staan, dus de stub hoeft geen enkele signatuur te kennen
               en kan zichzelf niet aanroepen (geen recursie).
  rg_gen.c     de slotbeschrijvingstabel (naam -> aanroepwijzer) plus de 33
               wikkels die wij zelf leveren en die mechanisch te genereren
               zijn: de 31 *Ucall-varianten en glutStrokeLengthf /
               glutStrokeWidthf.

De 6 echt nieuwe functies staan met de hand in rg_ext.c, de lader/resolver in
rg_shim.c. Zie README.md voor de aannames.
"""

import os

HIER = os.path.dirname(os.path.abspath(__file__))

# De gemeten aantallen. Wijken de invoerlijsten hiervan af, dan stopt gen.py:
# dan is er iets veranderd aan BudgieLoader of aan onze libglut en moet de
# verdeling (en dus dit script) opnieuw bekeken worden.
AANTAL_BUDGIE = 161
AANTAL_REAL = 171
AANTAL_GEDEKT = 122
AANTAL_ZELF = 39
AANTAL_UCALL = 31

# De 8 namen die BudgieLoader exporteert, wij niet, en die geen *Ucall-variant zijn.
EXT_BEKEND = {
    "glutAppStatusFunc",
    "glutInitContextFunc",
    "glutPositionFunc",
    "glutSetVertexAttribCoord3",
    "glutSetVertexAttribNormal",
    "glutSetVertexAttribTexCoord2",
    "glutStrokeLengthf",
    "glutStrokeWidthf",
}

# De drie *Ucall-varianten waarvan de basisnaam ook niet in de echte libglut zit;
# die basis naam leveren wij zelf (rg_ext.c) en de Ucall-wikkel roept die direct aan.
ZELF_BASES = {
    "glutAppStatusFunc": "rg_set_appstatus",
    "glutInitContextFunc": "rg_set_initcontext",
    "glutPositionFunc": "rg_set_position",
}

# Vorm van de basis-callback per *Ucall-variant: de parametertypen ZONDER de
# user_data. Dit volgt de prototypes uit freeglut 3.4.0 (include/GL/freeglut_ucall.h,
# opgehaald van github.com/freeglut/freeglut tag v3.4.0); de Ucall-variant krijgt
# overal een extra void* user_data achteraan.
SHAPE_PARAMS = {
    "i0": [],
    "i1": ["int"],
    "i2": ["int", "int"],
    "i3": ["int", "int", "int"],
    "i4": ["int", "int", "int", "int"],
    "i5": ["int", "int", "int", "int", "int"],
    "kbd": ["unsigned char", "int", "int"],
    "va": ["const char *", "va_list"],
}

# De 31 *Ucall-namen van BudgieLoader -> de vorm van hun callback.
UCALL_SHAPE = {
    "glutAppStatusFuncUcall": "i1",
    "glutInitContextFuncUcall": "i0",
    "glutButtonBoxFuncUcall": "i2",
    "glutCloseFuncUcall": "i0",
    "glutDialsFuncUcall": "i2",
    "glutDisplayFuncUcall": "i0",
    "glutEntryFuncUcall": "i1",
    "glutIdleFuncUcall": "i0",
    "glutInitErrorFuncUcall": "va",
    "glutInitWarningFuncUcall": "va",
    "glutKeyboardFuncUcall": "kbd",
    "glutKeyboardUpFuncUcall": "kbd",
    "glutMotionFuncUcall": "i2",
    "glutMouseFuncUcall": "i4",
    "glutMouseWheelFuncUcall": "i4",
    "glutMultiButtonFuncUcall": "i5",
    "glutMultiEntryFuncUcall": "i2",
    "glutMultiMotionFuncUcall": "i3",
    "glutMultiPassiveFuncUcall": "i3",
    "glutOverlayDisplayFuncUcall": "i0",
    "glutPassiveMotionFuncUcall": "i2",
    "glutPositionFuncUcall": "i2",
    "glutReshapeFuncUcall": "i2",
    "glutSpecialFuncUcall": "i3",
    "glutSpecialUpFuncUcall": "i3",
    "glutTabletButtonFuncUcall": "i4",
    "glutTabletMotionFuncUcall": "i2",
    "glutTimerFuncUcall": "timer",
    "glutVisibilityFuncUcall": "i1",
    "glutWMCloseFuncUcall": "i0",
    "glutWindowStatusFuncUcall": "i1",
}

KOP = "/* GEGENEREERD door gen.py -- niet met de hand aanpassen. */\n"


def lees(naam):
    with open(os.path.join(HIER, naam), "r", encoding="utf-8", errors="replace") as fh:
        return sorted({r.strip() for r in fh if r.strip()})


def eis(conditie, tekst):
    if not conditie:
        raise SystemExit("gen.py: FATALE INCONSISTENTIE: " + tekst)


def tekst_van_types(types):
    return ", ".join(types)


def emit_stubs(gedekt):
    """De 122 jmp-stubs + 122 noodstubs."""
    uit = [KOP, "\t.text\n\n"]
    for idx, naam in enumerate(gedekt):
        uit.append(
            "\t.globl %(n)s\n"
            "\t.type %(n)s,@function\n"
            "%(n)s:\n"
            "\tcall 1f\n"
            "1:\tpopl %%eax\n"
            "\taddl $_GLOBAL_OFFSET_TABLE_+[.-1b], %%eax\n"
            "\tmovl rg_slot_%(n)s@GOTOFF(%%eax), %%ecx\n"
            "\tjmp *%%ecx\n"
            "\t.size %(n)s, .-%(n)s\n"
            "\n"
            "\t.globl rg_ontbrekend_%(n)s\n"
            "\t.hidden rg_ontbrekend_%(n)s\n"
            "\t.type rg_ontbrekend_%(n)s,@function\n"
            "rg_ontbrekend_%(n)s:\n"
            "\tpushl $%(i)d\n"
            "\tcall rg_herprobeer\n"
            "\taddl $4, %%esp\n"
            "\txorl %%eax, %%eax\n"
            "\tret\n"
            "\t.size rg_ontbrekend_%(n)s, .-rg_ontbrekend_%(n)s\n"
            "\n" % {"n": naam, "i": idx}
        )
    uit.append('\t.section .note.GNU-stack,"",@progbits\n')
    return "".join(uit)


def emit_ucall(naam, shape, basis, zelf):
    """Eén *Ucall-wikkel: bewaar de callback + user_data, installeer een
    trampoline met de basis-signatuur bij de echte (of onze eigen) setter."""
    types = SHAPE_PARAMS[shape]
    params = ", ".join("%s p%d" % (t, i) for i, t in enumerate(types))
    namen = tekst_van_types(types)
    cbtype = (namen + ", " if namen else "") + "void *"
    args = ", ".join("p%d" % i for i in range(len(types)))
    cbbasis = ("void (*)(%s)" % namen) if namen else "void (*)(void)"

    uit = ["/* ---- %s (basis %s) ---- */\n" % (naam, basis)]
    uit.append("static void *rg_uc_cb_%s;\n" % naam)
    uit.append("static void *rg_uc_ud_%s;\n\n" % naam)
    uit.append("static void rg_tramp_%s(%s)\n{\n" % (naam, params))
    uit.append("    void (*cb)(%s) = (void (*)(%s))rg_uc_cb_%s;\n" % (cbtype, cbtype, naam))
    uit.append("    if (cb) cb(%s%srg_uc_ud_%s);\n}\n\n" % (args, ", " if args else "", naam))
    uit.append("void %s(void (*cb)(%s), void *ud)\n{\n" % (naam, cbtype))
    if zelf:
        uit.append("    rg_uc_cb_%s = (void *)cb;\n    rg_uc_ud_%s = ud;\n" % (naam, naam))
        uit.append("    %s(cb ? rg_tramp_%s : 0);\n" % (zelf, naam))
    else:
        # De aanroepwijzer eerst in een lokale variabele: een cast van een void*
        # naar een functietype direct vóór de haakjes wordt door gcc verkeerd
        # geparseerd (hij leest de cast dan als een functiedefinitie).
        uit.append("    void (*basis)(%s) = (void (*)(%s))rg_slot_%s;\n" % (cbbasis, cbbasis, basis))
        uit.append("    rg_uc_cb_%s = (void *)cb;\n    rg_uc_ud_%s = ud;\n" % (naam, naam))
        uit.append("    basis(cb ? rg_tramp_%s : 0);\n" % naam)
    uit.append("}\n\n")
    return "".join(uit)


def emit_timer(naam):
    return (
        f"/* ---- {naam} (basis glutTimerFunc; één gedeelde user_data, zie README) ---- */\n"
        f"static void *rg_uc_cb_{naam};\n"
        f"static void *rg_uc_ud_{naam};\n"
        "\n"
        f"static void rg_tramp_{naam}(int p0)\n"
        "{\n"
        f"    void (*cb)(int, void *) = (void (*)(int, void *))rg_uc_cb_{naam};\n"
        f"    if (cb) cb(p0, rg_uc_ud_{naam});\n"
        "}\n"
        "\n"
        f"void {naam}(unsigned int tijd, void (*cb)(int, void *), int waarde, void *ud)\n"
        "{\n"
        "    void (*basis)(unsigned int, void (*)(int), int) =\n"
        "        (void (*)(unsigned int, void (*)(int), int))rg_slot_glutTimerFunc;\n"
        f"    rg_uc_cb_{naam} = (void *)cb;\n"
        f"    rg_uc_ud_{naam} = ud;\n"
        f"    basis(tijd, cb ? rg_tramp_{naam} : 0, waarde);\n"
        "}\n\n"
    )


def emit_gen_c(gedekt, ucall_namen):
    uit = [KOP]
    uit.append('#include <stdarg.h>\n\n#include "rg_shim.h"\n\n')
    uit.append("/* De 122 aanroepwijzers. Ze zijn verborgen (hidden): ze horen niet in\n")
    uit.append("   de exportlijst van de bibliotheek. Standaard wijzen ze naar de\n")
    uit.append("   noodstub van die naam, zodat een aanroep zonder geslaagde lading\n")
    uit.append("   niets stukmaakt maar wel laat horen wat er mist. */\n")
    for naam in gedekt:
        uit.append("extern void rg_ontbrekend_%s(void);\n" % naam)
    uit.append("\n")
    for naam in gedekt:
        uit.append(
            "void *rg_slot_%s __attribute__((visibility(\"hidden\"))) = (void *)rg_ontbrekend_%s;\n"
            % (naam, naam)
        )
    uit.append("\n/* De tabel die rg_shim.c bij het laden vult. */\n")
    uit.append("rg_slot_desc rg_slots[] __attribute__((visibility(\"hidden\"))) = {\n")
    for naam in gedekt:
        uit.append('    { "%s", &rg_slot_%s },\n' % (naam, naam))
    uit.append("};\n\n")
    uit.append(
        "const unsigned rg_nslots __attribute__((visibility(\"hidden\"))) =\n"
        "    sizeof(rg_slots) / sizeof(rg_slots[0]);\n\n"
    )
    uit.append("/* ---- de 31 *Ucall-wikkels ---- */\n\n")
    for naam in ucall_namen:
        shape = UCALL_SHAPE[naam]
        basis = naam[:-5]
        if shape == "timer":
            uit.append(emit_timer(naam))
        else:
            uit.append(emit_ucall(naam, shape, basis, ZELF_BASES.get(basis)))
    uit.append("/* ---- de twee stroke-f varianten ---- */\n\n")
    uit.append(
        "/* De echte libglut is een oudere freeglut waarin glutStrokeWidth en\n"
        "   glutStrokeLength de GLfloat-variant van GLUT 3.8 zijn (gemeten: de\n"
        "   functies eindigen met de float in st0). De f-varianten zijn daarom\n"
        "   een rechte doorverwijzing. */\n"
        "float glutStrokeWidthf(void *font, int teken)\n"
        "{\n"
        "    float (*basis)(void *, int) = (float (*)(void *, int))rg_slot_glutStrokeWidth;\n"
        "    return basis(font, teken);\n"
        "}\n\n"
        "float glutStrokeLengthf(void *font, const unsigned char *tekst)\n"
        "{\n"
        "    float (*basis)(void *, const unsigned char *) =\n"
        "        (float (*)(void *, const unsigned char *))rg_slot_glutStrokeLength;\n"
        "    return basis(font, tekst);\n"
        "}\n"
    )
    return "".join(uit)


def main():
    budgie = lees("names_budgie.txt")
    echt = set(lees("names_real_libglut.txt"))

    eis(len(budgie) == AANTAL_BUDGIE, "BudgieLoader-lijst heeft %d namen, gemeten was %d" % (len(budgie), AANTAL_BUDGIE))
    eis(len(echt) == AANTAL_REAL, "libglut-lijst heeft %d namen, gemeten was %d" % (len(echt), AANTAL_REAL))

    gedekt = [n for n in budgie if n in echt]
    zelf = [n for n in budgie if n not in echt]
    eis(len(gedekt) == AANTAL_GEDEKT, "%d namen gedekt, gemeten was %d" % (len(gedekt), AANTAL_GEDEKT))
    eis(len(zelf) == AANTAL_ZELF, "%d namen zelf te maken, gemeten was %d" % (len(zelf), AANTAL_ZELF))

    ucall = [n for n in zelf if n.endswith("Ucall")]
    ext = [n for n in zelf if not n.endswith("Ucall")]
    eis(len(ucall) == AANTAL_UCALL, "%d *Ucall-namen, gemeten was %d" % (len(ucall), AANTAL_UCALL))
    eis(set(ext) == EXT_BEKEND, "de niet-Ucall-uitbreidingen zijn niet de verwachte 8: verschil %s" % sorted(set(ext) ^ EXT_BEKEND))
    eis(set(ucall) == set(UCALL_SHAPE), "*Ucall-lijst en vormtabel verschillen: %s" % sorted(set(ucall) ^ set(UCALL_SHAPE)))

    for naam in ucall:
        basis = naam[:-5]
        eis(basis in echt or basis in ZELF_BASES, "%s heeft geen basis %s in de echte libglut en is geen eigen basis" % (naam, basis))
        if basis in ZELF_BASES:
            types = SHAPE_PARAMS[UCALL_SHAPE[naam]]
            eis(basis != "glutAppStatusFunc" or types == ["int"], "glutAppStatusFuncUcall heeft onverwachte vorm")
            eis(basis != "glutInitContextFunc" or types == [], "glutInitContextFuncUcall heeft onverwachte vorm")
            eis(basis != "glutPositionFunc" or types == ["int", "int"], "glutPositionFuncUcall heeft onverwachte vorm")

    eis("glutStrokeLengthf" in ext and "glutStrokeWidthf" in ext, "de stroke-f varianten missen")
    eis("glutStrokeLength" in echt and "glutStrokeWidth" in echt, "de echte libglut mist glutStrokeLength/Width")

    with open(os.path.join(HIER, "rg_stubs.S"), "w", encoding="utf-8") as fh:
        fh.write(emit_stubs(gedekt))
    with open(os.path.join(HIER, "rg_gen.c"), "w", encoding="utf-8") as fh:
        fh.write(emit_gen_c(gedekt, ucall))

    print("gen.py: BudgieLoader %d namen, libglut %d namen" % (len(budgie), len(echt)))
    print("gen.py: %d doorverwijzingen (rg_stubs.S), %d zelfgemaakt (%d *Ucall + %d uitbreidingen)"
          % (len(gedekt), len(zelf), len(ucall), len(ext)))
    print("gen.py: de 8 niet-Ucall-uitbreidingen: %s" % ", ".join(sorted(ext)))
    print("gen.py: geschreven: rg_stubs.S (%d stubs), rg_gen.c" % len(gedekt))


if __name__ == "__main__":
    main()
