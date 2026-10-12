#!/usr/bin/env python3
"""Maakt `patches/EIGEN-KERN.md`: welke games onze eigen OpenParrot-kern kan herkennen.

De kern kiest zijn `EmulationProfile` op een CRC over de eerste `0x400` bytes van de
hoofdmodule van de game (GameDetect.cpp van de OpenParrot-bron waarmee onze eigen
kern gebouwd is, commit fc8ba632e1f67c5c340ff88f30efca743e8d38f5, GPL-3.0):

    crcResult = GetCRC32(moduleBase, 0x400);          // regel 48
    switch (crcResult) { case 0x...: ...; default:
        // "Craft CRC detection without virtual address"  (regel 452-463)
        memcpy(newCrc, moduleBase, 0x400);
        pePTR = *(DWORD*)(newCrc + 0x3C);
        *(DWORD*)(newCrc + pePTR + 0x18) = 0;
        *(DWORD*)(newCrc + pePTR + 0x1C) = 0;
      #ifdef _AMD64_
        *(DWORD*)(newCrc + pePTR + 50) = 0;
      #endif
        *(DWORD*)(newCrc + pePTR + 54) = 0;
        newCrcResult = GetCRC32(newCrc, 0x400);
        switch (newCrcResult) { ... }
    }

`GetCRC32` (Utility/Utils.cpp regel 9-17) is de gewone CRC-32 (zlib/PKZIP). De eerste
`0x400` bytes van het bestand zijn de PE-headers en die staan 1-op-1 in het image, dus
beide CRC's zijn uit het bestand te rekenen. De launcher zet `TP_CRC_NORMALIZE_LAA`
niet, dus die tak blijft buiten beschouwing.

Gebruik:  python3 patches/kern-herkenning.py        (schrijft patches/EIGEN-KERN.md)
          python3 patches/kern-herkenning.py --toon (alleen naar het scherm)
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HIER)
PATCHES = os.path.join(REPO, "patches")
LOADER = os.environ.get("RAVENGUN_REPO", os.path.expanduser("~/projects/ravengun"))
BASIS = os.path.expanduser(
    "~/.cache/ravengun/build/openparrot/fc8ba632e1f67c5c340ff88f30efca743e8d38f5"
    "/src/OpenParrot/src/Utility/GameDetect.cpp"
)
EERSTE = (51, 509)      # switch (crcResult)
TWEEDE = (510, 893)     # switch (newCrcResult)


def lees_switch(pad, van, tot):
    """{crc: (GameID, commentaar)} uit een switch-blok."""
    with open(pad, encoding="utf-8", errors="replace") as f:
        regels = f.read().split("\n")
    tabel = {}
    open_crcs = []

    def wijs(naam):
        for c in open_crcs:
            tabel[c] = (naam, tabel[c][1])
        open_crcs.clear()

    for regel in regels[van - 1:tot]:
        s = regel.strip()
        m = re.match(r"case (0x[0-9a-fA-F]+):(.*)$", s)
        if m:
            crc = int(m.group(1), 16)
            rest = m.group(2).strip()
            tabel[crc] = (None, rest[2:].strip() if rest.startswith("//") else "")
            open_crcs.append(crc)
            continue
        m = re.match(r"currentGame = GameID::(\w+);", s)
        if m:
            wijs(m.group(1))
            continue
        m = re.match(r"SetGameId\(GameID::(\w+),", s)
        if m:
            wijs(m.group(1))
    return {k: v for k, v in tabel.items() if v[0]}


def crc32(kop):
    return zlib.crc32(kop) & 0xFFFFFFFF


def genormaliseerd(kop, x64):
    b = bytearray(kop)
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if pe + 0x40 > len(b):
        return None
    for off in (pe + 0x18, pe + 0x1C):
        b[off:off + 4] = b"\0\0\0\0"
    if x64:
        b[pe + 50:pe + 54] = b"\0\0\0\0"
    b[pe + 54:pe + 58] = b"\0\0\0\0"
    return crc32(bytes(b))


def game_exe(game):
    r = subprocess.run([sys.executable, "scripts/launch-direct.py", game, "--plan"],
                       cwd=LOADER, capture_output=True, text=True)
    for regel in r.stdout.split("\n"):
        if regel.startswith("game: "):
            return regel[6:].strip()
    return ""


def uit_game_ini(game, sleutel):
    """`EmulationProfile` of `Type` uit patches/<Game>/game.ini."""
    pad = os.path.join(PATCHES, game, "game.ini")
    if not os.path.isfile(pad):
        return ""
    with open(pad, encoding="utf-8-sig", errors="replace") as f:
        for regel in f:
            s = regel.strip()
            if s.lower().startswith(sleutel.lower() + " =") or s.lower().startswith(sleutel.lower() + "="):
                return s.split("=", 1)[1].strip()
    return ""


def meet():
    gewoon = lees_switch(BASIS, *EERSTE)
    nieuw = lees_switch(BASIS, *TWEEDE)
    rijen = []
    for g in sorted(d for d in os.listdir(PATCHES)
                    if os.path.isdir(os.path.join(PATCHES, d)) and not d.startswith("_")):
        profiel = uit_game_ini(g, "EmulationProfile")
        soort = uit_game_ini(g, "Type")
        exe = game_exe(g)
        if not exe or not os.path.isfile(exe):
            rijen.append((g, None, None, None,
                          "geen exe gevonden (niet op de NAS of twee exe's)", profiel, soort))
            continue
        with open(exe, "rb") as f:
            kop = f.read(0x400)
        c1 = crc32(kop)
        c2 = genormaliseerd(kop, False)
        c3 = genormaliseerd(kop, True)
        if c1 in gewoon:
            naam = "**%s** (tabel 1%s)" % (gewoon[c1][0], " — " + gewoon[c1][1] if gewoon[c1][1] else "")
        elif c2 in nieuw:
            naam = "**%s** (tabel 2, 32-bits%s)" % (nieuw[c2][0], " — " + nieuw[c2][1] if nieuw[c2][1] else "")
        elif c3 in nieuw:
            naam = "**%s** (tabel 2, 64-bits%s)" % (nieuw[c3][0], " — " + nieuw[c3][1] if nieuw[c3][1] else "")
        else:
            naam = "— niet in de tabel —"
        rijen.append((g, c1, c2, c3, naam, profiel, soort))
    return len(gewoon), len(nieuw), rijen


def md(gewoon_n, nieuw_n, rijen):
    raak = [r for r in rijen if not r[4].startswith("—") and "geen exe" not in r[4]]
    uit = []
    uit.append("# Onze eigen kern: welke games herkent hij?\n")
    uit.append("*Gegenereerd door `patches/kern-herkenning.py`; niet met de hand bijwerken.*\n")
    uit.append(
        "De OpenParrot-kern kiest zijn `EmulationProfile` op een **CRC over de eerste `0x400` bytes** van de\n"
        "hoofdmodule van de game. Dit bestand rekent diezelfde CRC uit voor elke game in `patches/`, met\n"
        "precies het algoritme uit de bron waarmee onze eigen kern gebouwd is. Het antwoord op de vraag\n"
        "**\"kan deze game van de gesloten `TeknoParrot(64).dll` af?\"** begint hier: een game die onze kern\n"
        "niet herkent, kan er ook niet op draaien zonder eerst een eigen `EmulationProfile` te krijgen.\n"
    )
    uit.append(
        "**Bron:** `GameDetect.cpp` en `Utility/Utils.cpp` uit de OpenParrot-bron, commit\n"
        "`fc8ba632e1f67c5c340ff88f30efca743e8d38f5` (GPL-3.0). De kern rekent eerst de gewone CRC\n"
        "(`crcResult`, %d waarden in de tabel) en alleen als die niet matcht de **genormaliseerde** CRC\n"
        "(`newCrcResult`, %d waarden): daarbij worden in de PE-header 8 bytes op `pePTR+0x18` en 4 bytes op\n"
        "`pePTR+0x36` op nul gezet, zodat een andere `ImageBase` de herkenning niet breekt.\n" % (gewoon_n, nieuw_n)
    )
    uit.append("**Uitkomst: %d van de %d games worden door onze eigen kern herkend** — precies de vier die er\n"
               "al op draaien.\n" % (len(raak), len(rijen)))
    uit.append(
        "**Sinds ronde 41 weigert onze kern een onbekende game niet meer.** In `GameDetect.cpp` is de\n"
        "`default:`-tak van de CRC-switch veranderd (OpenParrot-patch\n"
        "`0003-unknown-game-generic-route.patch` in `third_party/openparrot/patches/`): geen `MessageBoxA`\n"
        "en geen `ExitProcess(0)` meer, maar `currentGame` blijft `GameID::Global` en `X2Type` blijft\n"
        "`X2Type::None`. Gevolg: alleen de init-functies die voor `GameID::Global` staan draaien, en elke\n"
        "per-game tak slaat zichzelf over. **Herkennen is dus niet meer nodig om te *draaien*** -- het is\n"
        "nog wel nodig om de *hooks* van die game te krijgen.\n"
    )
    uit.append(
        "Gemeten met die kern (in `~/.local/share/ravengun/openparrot/current/`, Win32-sha256\n"
        "`258aa9fc41c42be3a25305105f2f96123dc5b3368e8157dc12a825bbd40e3c5f`): **LGI** start en het\n"
        "spelproces leeft nog na 25 s, met de JVS-pipe die antwoordt, en **GaiaAttack4** idem na 20 s. LGI\n"
        "is een game die de kern **niet** herkent, dus die draait op de generieke route; GaiaAttack4 wordt\n"
        "wel herkend en houdt zijn eigen hooks.\n"
    )
    uit.append(
        "**Wat dat niet zegt.** Of die games ook *speelbaar* zijn is niet gemeten. In de generieke route\n"
        "ontbreekt de invoervertaling die de gesloten kern per game deed, dus `Type = OpenParrot` staat\n"
        "niet standaard in hun `game.ini`; het is een bewezen *route*, nog geen bewezen vervanging.\n"
    )
    uit.append("| Game | CRC (gewoon) | CRC 32-bits | CRC 64-bits | Onze kern zegt | Profiel in `game.ini` | Lader |")
    uit.append("|---|---|---|---|---|---|---|")
    for g, c1, c2, c3, naam, profiel, soort in rijen:
        if c1 is None:
            uit.append("| %s | — | — | — | %s | `%s` | %s |" % (g, naam, profiel or "—", soort or "—"))
        else:
            fmt = lambda c: "— (geen PE-header)" if c is None else "`0x%08x`" % c  # noqa: E731
            uit.append("| %s | `0x%08x` | %s | %s | %s | `%s` | %s |"
                       % (g, c1, fmt(c2), fmt(c3), naam, profiel or "—", soort or "—"))
    uit.append("")
    dubbel = {}
    for g, c1, c2, c3, n, profiel, soort in rijen:
        if c1 is not None:
            dubbel.setdefault((c1, c2, c3), []).append(g)
    for sleutel, namen in sorted(dubbel.items()):
        if len(namen) > 1:
            uit.append(
                "**%s hebben dezelfde drie CRC's** (`0x%08x`): hetzelfde stub-bestand, dus dezelfde\n"
                "hoofdmodule. Voor de herkenning maakt dat niet uit (geen van beide staat in de tabel),\n"
                "maar het betekent wel dat een profiel op deze exe beide games zou vangen.\n"
                % (" en ".join(namen), sleutel[0])
            )
    gesloten = [(g, profiel) for g, c1, c2, c3, n, profiel, soort in rijen
                if soort == "TeknoParrot" and not n.startswith("**")]
    if gesloten:
        per_profiel = {}
        for g, profiel in gesloten:
            per_profiel.setdefault(profiel or "(geen)", []).append(g)
        uit.append(
            "**Wat er per game nodig is.** De kolom *Profiel in `game.ini`* noemt het `EmulationProfile` dat de\n"
            "game daar heeft — voor de games met de gesloten kern (`Type = TeknoParrot`) is dat het profiel dat\n"
            "**onze eigen kern zou moeten implementeren**: eerst een nieuwe `case <CRC>` in `GameDetect.cpp` die\n"
            "op die `GameID` uitkomt, en dan de hooks van dat profiel. Voor de vier games die onze kern al\n"
            "herkent is de naam in `game.ini` precies de `GameID` uit de CRC-tabel, dus die koppeling is geen\n"
            "gok. **%d games wachten daar nog op:**\n" % len(gesloten)
        )
        uit.append("| Profiel dat nog van ons moet komen | Games |")
        uit.append("|---|---|")
        for profiel in sorted(per_profiel, key=lambda p: (-len(per_profiel[p]), p)):
            namen = per_profiel[profiel]
            uit.append("| `%s` | %d — %s |" % (profiel, len(namen), ", ".join(namen)))
        uit.append("")
    elf = [(g, profiel) for g, c1, c2, c3, n, profiel, soort in rijen if soort == "ElfLdr2"]
    if elf:
        uit.append(
            "**De %d games met `Type = ElfLdr2` staan hier wel in maar horen bij een andere vraag.** Hun\n"
            "`EmulationProfile` (%s) is van **BudgieLoader**, niet van de OpenParrot-kern; die games zijn ELF's\n"
            "zonder PE-header, dus de CRC-tabel zegt er niets over. Wat zij nodig hebben is de eigen\n"
            "`LD_PRELOAD`-kastnabootser voor de 64-bits ElfLdr2-familie, niet een profiel in `GameDetect.cpp`.\n"
            % (len(elf), ", ".join(sorted({p for g, p in elf})))
        )
    geen_pe = [g for g, c1, c2, c3, n, profiel, soort in rijen if c1 is not None and c2 is None]
    if geen_pe:
        uit.append(
            "**%d games hebben geen PE-header** (%s). Dat zijn de games die op de Cygwin-lader\n"
            "(ElfLdr2/BudgieLoader) draaien en dus ELF's zijn; de CRC-tabel van de kern gaat over PE's,\n"
            "dus over hun herkenning zegt deze meting niets. Ze staan er wel in omdat hun\n"
            "`EmulationProfile` dezelfde vraag oproept: ook die lader is van TeknoParrot.\n"
            % (len(geen_pe), ", ".join(geen_pe))
        )
    uit.append(
        "**Wat je hieruit leest.** De vier games met `kern van ons` in `tools/verify-all.py` zijn de enige\n"
        "die de kern zelf thuisbrengt. Voor de andere 32 zou de kern eerst **bij naam** bekend moeten zijn\n"
        "(een nieuwe `case` in `GameDetect.cpp`), en daarna moeten de hooks van dat `EmulationProfile` nog\n"
        "geschreven worden — dat tweede staat hier niet in. Dit bestand zegt dus alleen **of de kern de\n"
        "game herkent**, niet of er al een profiel voor is.\n"
    )
    uit.append(
        "**Eén meting die eruit volgt.** HauntedMuseumII is de **USA 1.00**-build (`0x5a7fa241` ->\n"
        "`GameID::HauntedMuseum2100`) en niet de JP 1.01 (`0x42d54499` -> `GameID::HauntedMuseum2101J`).\n"
        "Van de twee versiesets in `patches/HauntedMuseumII/patches.ini` geldt er dus maar één voor onze\n"
        "dump; de andere staat er als documentatie van een andere build.\n"
    )
    uit.append("## Hoe je dit vernieuwt\n")
    uit.append("```bash\ncd ~/projects/ravengun-patches\npython3 patches/kern-herkenning.py\n```\n")
    uit.append(
        "Het script leest de tabel uit de bron in `~/.cache/ravengun/build/openparrot/<commit>/` en haalt\n"
        "het pad van elke exe op met `scripts/launch-direct.py <Game> --plan` in de loader-repo. Staat die\n"
        "build er niet, zet dan `RAVENGUN_REPO` of bouw de kern opnieuw met `scripts/build-openparrot.sh`.\n"
    )
    return "\n".join(uit)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--toon", action="store_true", help="alleen naar het scherm, niet schrijven")
    args = ap.parse_args()
    gewoon_n, nieuw_n, rijen = meet()
    tekst = md(gewoon_n, nieuw_n, rijen)
    if args.toon:
        print(tekst)
        return 0
    doel = os.path.join(PATCHES, "EIGEN-KERN.md")
    with open(doel, "w", encoding="utf-8") as f:
        f.write(tekst)
    print("bijgewerkt: %s (%d games)" % (doel, len(rijen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
