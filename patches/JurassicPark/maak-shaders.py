#!/usr/bin/env python3
# Maakt de vier glow-shaders van Jurassic Park (shader108, 108d, 108c_sk, 108d_sk) geschikt voor Mesa, uit de originelen op de
# NAS (alleen gelezen). Waarom: de game plakt include/frag_bloom.gls in dezelfde fragment-shader, en die verklaart met
# USE_BLOOM=1 een eigen uitvoer `out vec4 out_Bloom`; deze vier schrijven daarnaast de oude gl_FragData[]. GLSL verbiedt die
# menging. NVIDIA (de kast) liet het toe, Mesa niet: "fragment shader writes to both `gl_FragData' and `out_Bloom'" (gemeten
# 2026-10-14 op de bronnen die de game zelf aan Mesa gaf, MESA_SHADER_DUMP_PATH), en de game laat de vier dan vallen
# ("Failed to load shader: shader108 (ec=-9596)").
#
# Verandering: gl_FragData[0] -> out_Color (de game bindt die op uitvoer 0) en gl_FragData[1] -> out_Bloom (uitvoer 1, dezelfde
# buffer; zonder bloom een eigen out_Glow, ook uitvoer 1). shader108.shader zet frag_bloom.gls vooraan, zoals de andere drie al
# doen, zodat out_Bloom verklaard is voordat main() hem gebruikt. Verder niets.
#
# Gebruik: maak-shaders.py <map g5/shaders van de game> data/shaders
import os, sys

bron, uit = sys.argv[1], sys.argv[2]
DECL = ("out vec4 out_Color;\r\n"
        "#if (USE_BLOOM==1)\r\n"
        "#define RG_GLOW_OUT out_Bloom\r\n"
        "#else\r\n"
        "out vec4 out_Glow;\r\n"
        "#define RG_GLOW_OUT out_Glow\r\n"
        "#endif\r\n"
        "// RavenGun (patches/JurassicPark/data/shaders/maak-shaders.py): gl_FragData[0/1] -> out_Color / RG_GLOW_OUT\r\n\r\n")


def schrijf(rel, data):
    p = os.path.join(uit, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(data)
    print("gezet:", rel)


for naam in ("shader108", "shader108d", "shader108c_sk", "shader108d_sk"):
    t = open(os.path.join(bron, naam, "frag.gls"), "rb").read().decode("latin-1")
    proto = t.index("void ScaleOnIntensity(")
    assert t.count("gl_FragData[0]") >= 1 and t.count("gl_FragData[1]") >= 1, naam
    nl = "\r\n" if "\r\n" in t else "\n"
    t = t[:proto] + DECL.replace("\r\n", nl) + t[proto:]
    t = t.replace("gl_FragData[0]", "out_Color").replace("gl_FragData[1]", "RG_GLOW_OUT")
    schrijf(os.path.join(naam, "frag.gls"), t.encode("latin-1"))

s = open(os.path.join(bron, "shader108.shader"), "rb").read().decode("latin-1")
nl = "\r\n" if "\r\n" in s else "\n"
a, b = "FragShader: shader108/frag.gls" + nl, "FragShader: include/frag_bloom.gls" + nl
assert a in s and b in s and s.index(a) < s.index(b)
s = s.replace(a + b, b + a) if a + b in s else s.replace(b, "").replace(a, b + a)
schrijf("shader108.shader", s.encode("latin-1"))
