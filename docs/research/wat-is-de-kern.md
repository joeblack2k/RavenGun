# Wat is de gesloten kern van 56 MB?

*Onderzoek door RavenGun, ronde 9 (2026-10-11). Alle cijfers zijn gemeten; de commando's staan erbij
zodat je ze kunt narekenen.*

## Het korte antwoord

**Het is geen database van 35 games.** Het is een **gepakte native C++-kern met een groot aantal
ingebakken bibliotheken**, plus de per-game logica als **gecompileerde machinecode die niet op naam
maar op bytepatronen wordt aangezet**. De namen van de games staan er **niet** in.

---

## 1. Op schijf: één gepakte sectie

```
$ objdump -h runtime/…/TeknoParrot/TeknoParrot64.dll
Idx Name          Size      VMA
  0 .text         00000000  0000000180001000     <- leeg
  1 .rdata        00000000  000000018027c000     <- leeg
  2 .data         00000000  0000000180387000     <- leeg
  3 .pdata        00000000  0000000180435000     <- leeg
  4 _RDATA        00000000  000000018044d000     <- leeg
  5 .teknoGo      00000000  000000018044e000     <- leeg
  6 .teknoGo      000008c8  00000001833e6000
  7 .teknoGo     035a3564  00000001833e7000     <- 56.243.556 bytes = 100% van het bestand
  8 .rsrc         0000080d  000000018698b000
```

Alle gewone secties hebben **grootte 0**; alles zit in één sectie die drie keer `.teknoGo` heet (bij
de 32-bits versie heet hij `teknoGod` en is hij 68.423.220 bytes). Dat is het handtekeningbeeld van
een **packer**.

**De sectie is versleuteld of gecomprimeerd.** Entropie **7,29 bits/byte** over 4 MB gemeten, tegen
6,23 voor de `.text` van onze eigen `OpenParrot64.dll` (gewone x86-code). 7,29 is bijna het maximum
van 8.

### De "ingebedde bestanden" zijn ruis

Ik vond 829 `MZ`-signaturen en 837 `{"` in die sectie. Dat lijkt op honderden ingebedde
programma's, maar het is toeval:

| signatuur | lengte | verwacht bij toeval in 56,2 MB | gemeten |
|---|---|---|---|
| `MZ` | 2 bytes | 858 | **829** |
| `BZh` (bzip2) | 3 bytes | 3,4 | **4** |
| `{"` | 2 bytes | 858 | **837** |
| 6 leesbare tekens | — | ~146.884 | **86.191** |

Alles op of onder wat willekeurige data geeft. **Er zit dus geen leesbare structuur in.**

---

## 2. Uitgepakt: 44 MB met de helft nullen

De repo heeft de kern al eens uitgepakt (`cores/dump/`, `docs/14_unpacking.md`):

| bestand | grootte | entropie | nulbytes |
|---|---|---|---|
| `TpHost64_5664_…_TeknoParrot64_dll.bin` | 44.208.128 | 5,05 | **49,2%** |
| `TpHost64_7612_…_TeknoParrot64_dll.bin` | 109.129.728 | — | — |
| `TpHost32_8296_…_TeknoParrot_dll.bin` | 137.261.056 | — | — |

De helft nullen betekent: een **ijle afbeelding** met grote lege gaten. En er zijn **nul
aaneengesloten niet-nul blokken groter dan 100 kB** — het vlees zit in veel kleine stukken.

---

## 3. Het is native C++, geen .NET

```
$ python3 -c "d=open('…TeknoParrot64_dll.bin','rb').read(); print(d.count(b'BSJB'))"
0
```

`BSJB` is de handtekening van .NET-metadata en komt **nul** keer voor. De `#~`-treffers die je zou
kunnen tegenkomen zijn `spdlog`-opmaakstrings (`.?AVpattern_formatter@spdlog@@`), geen metadata.

Wat er wél in zit, aan namen van onderdelen:

| onderdeel | treffers in de stringtabel |
|---|---|
| `Nu` | 958 |
| `Ring` | 48 |
| `d3d` / `D3D` | 42 |
| `ogg` | 26 |
| `jvs` / `JVS` | 22 |
| `SDL` | 16 |
| `hid` | 14 |
| `imgui` | 13 |
| `XInput` | 11 |
| `lua` | 10 |
| `ES3` | 10 |
| `msys` | 9 |
| `Sega` | 9 |
| `zlib` | 8 |
| `OpenGL` | 7 |
| `nesica` | 7 |
| `Taito` | 3 |
| `ALLS` | 3 |
| `DirectInput` | 3 |
| `Konami` | 2 |
| `amdaemon` | 1 |

En aan bestandsnamen: `SDL.dll`, `SDL2.dll`, `SDL3.dll`, `OPENGL32.dll`, `lua5.1.dll`, `zlib1.dll`,
`libeay32.dll`, `ssleay32.dll`, `libogg.dll`, `libvorbis.dll`, `imgui.ini`, `tpgfx.dll`,
`msys-2.0.dll`, `msys-stdc++-6.dll`, `regal32.dll`, `D3DCOMPILER_47.dll`, `d3d12.dll`,
`Assembly-CSharp.dll`, `UnityEngine.CoreModule.dll`, `FFBBlaster.dll`, `MAGIDEIO.dll`, `IndRun.dll`,
`USBIOPlugin/UIO_AMIC.dll`, `vf5fs-pxd-w64-Retail_APM3.dll`.

**Dat is een compleet emulatieframework plus een stapel meegebakken bibliotheken**: SDL 1/2/3,
OpenGL met Regal, lua5.1, zlib, OpenSSL, ogg/vorbis, imgui, spdlog, de MSYS/Cygwin-runtime,
D3DCompiler en d3d12. Daar zit een groot deel van de omvang.

---

## 4. En de spellennamen staan er niet in

De stringtabel heeft **425.756 unieke regels**. Ik heb er 34 spel- en bordnamen in gezocht — de
profielnamen uit deze repo plus de spellen zelf:

| naam | treffers |
|---|---|
| `WartranTroopers`, `RawThrillsGUN`, `Gashaaaan`, `Luigi`, `TimeCrisis`, `Rabbids`, `Rampage`, `LetsGo`, `Halo`, `Otomedius`, `Omatsuri`, `GaiaAttack`, `HauntedMuseum`, `MusicGunGun`, `SegaJvs`, `Transformers`, `FarCry`, `DeadHeat`, `InfinityBlade`, `TaitoTypeX`, `ALLSHOTDSD`, `RaccoonRampage`, `PigsMightFly`, `LethalEnforcers`, `MedaruNoGunman`, `TombRaider`, `TankTankTank`, `RainbowBomber`, `3DCosplay` | **0** |
| `NERF` | 5 |
| `Jurassic` | 1 |
| `Ghostbuster` | 1 |

**Nul.** En de tabel bestaat voor **421.402 van de 425.756 regels uit strings van 4 tot 11 tekens** —
symbolen, geen zinnen. Van de hele tabel lijken er maar **7** op een bytepatroon in tekstvorm
(`48 89 5C 24 08 …`).

---

## 5. Dus hoe herkent de kern een spel?

**Op bytepatronen, niet op namen.** Het bewijs:

- `hints.dat` komt in de kern voor. Dat is de patroon-scan-cache die eerder in dit project gevonden
  is (`Hooking.Patterns.cpp`): records van 16 bytes, `uint64 hash` + `uint64 offset`.
- Er zijn vrijwel geen hex-patronen als *tekst*, dus de patronen zijn **binaire blobs**.
- De namen van de spellen ontbreken, dus de selectie gebeurt op die patronen.

De werking is dus: **scan de executable van het spel op bekende bytepatronen → vind welke game het is
→ zet de bijbehorende hooks aan**, en die hooks zijn **gecompileerde machinecode** in dezelfde sectie.

---

## 6. Wat dat betekent voor het loskoppelen

Dit is het nuttige deel, en het is geen goed nieuws maar wel eerlijk:

1. **Er is geen lijst om af te werken.** Je kunt niet "de 35 profielen overzetten", want er staat geen
   lijst in. Er staat code die aangaat als een patroon past.
2. **Je kunt niet aflezen wat je moet herimplementeren.** De per-game logica is machinecode zonder
   index. De enige manier om te weten wat een spel nodig heeft is **meten** — precies wat dit project
   al doet (de GOT-adressen van Jurassic Park, de bordindelingen, het regiomechanisme).
3. **De omvang zegt niets over de moeilijkheid.** De 56 MB is vooral bibliotheken en framework; de
   export-oppervlakte is één functie (`InitializeASI`). Een eigen kern hoeft die bibliotheken niet
   over te nemen — alleen de delen die de games echt gebruiken.
4. **En het verklaart waarom het per game zoekwerk is.** De `patches/<Game>/patches.ini` van dit
   project is in feite de handmatige vervanging van wat daar als patroon+code in zit. Dat is de goede
   route, en het is de reden dat de per-game map het juiste ontwerp is.

---

## Hoe je dit narekent

```bash
RT=~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64
objdump -h "$RT/TeknoParrot/TeknoParrot64.dll"          # de nul-secties en .teknoGo
strings -a "$RT/TeknoParrot/TeknoParrot64.dll" | grep -c -i WartranTroopers   # 0
```
En voor het uitgepakte beeld: `cores/dump/TpHost64_*_TeknoParrot64_dll.bin` plus de bijbehorende
`.strings.txt` (gemaakt met `re/TpHost32.exe` + `re/dump_proc.py`, zie `docs/14_unpacking.md`).
