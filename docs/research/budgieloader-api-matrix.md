# BudgieLoader-API-matrix: welke API onze eigen ELF-lader moet leveren

*Statische meting. Er is alleen in bestanden gelezen: **geen game gestart, BudgieLoader niet
gestart, geen Wine**. Op de NAS is niets geschreven en er is geen git-commando uitgevoerd.
Getallen met het label **gemeten** komen uit een commando in bijlage A; **afgeleid** betekent:
een gevolgtrekking uit die meting. Waar deze meting afwijkt van `patches/ELF-LADER-SPEC.md`
staat dat er expliciet bij.*

## 0. Samenvatting

1. De lader exporteert **173** namen (i386) respectievelijk **174** (x64); het enige verschil is
   `ElfPreReserveCallback`. Daarvan zijn **161** `glut*` (de volledige freeglut-API inclusief alle
   `*Ucall`-varianten) en **12** eigen functies.
2. Van de zeven games noemt er **één** de exporttabel van de lader bij naam in zijn eigen
   imports: **Terminator**, met **15** `glut*`-functies (zowel in de UND-lijst als als
   letterlijke string). **JurassicPark** noemt daarnaast `glutBitmapCharacter` als losse string.
   De andere vijf games verwijzen met **nul** namen naar de 173 exports.
3. De vier HASPE-gepakte games hebben **naamloze** importslots: GalagaAssault **258**,
   PacmanChomp **248**, JurassicPark **472**, Halo **461**. Hun namen staan **niet** in het
   bestand. Voor Galaga en Jurassic Park sluit de som exact: `99 + 258 = 357` en
   `105 + 472 = 577` — precies het aantal PLT-stubs. Dat bevestigt de centrale stelling van
   `ELF-LADER-SPEC.md`, maar de juiste *vindplaats* is niet `st_name = 0` in de gewone
   symbooltabel: de gewiste entries liggen **voorbij `DT_HASH`'s `nchain`**, in het stuk
   `.dynsym` tussen de symbooltabel en de stringtabel (§1).
4. `libglut.so.3` staat niet in `ElfLdr2/libs/`; op de Deck bestaat `/lib/libglut.so.3` wel, maar
   dat is **ELF64** en dus onbruikbaar voor de vijf 32-bits games. Voor onze lader betekent dat:
   de freeglut-API moet door ons geleverd worden, niet van de host gehaald.
5. Het minst nodig heeft **GoGoStrike** (nul exportnamen, vijf `DT_NEEDED` die alle resolven).
   Het meest heeft **JurassicPark**: 472 naamloze imports, 16 `DT_NEEDED` waarvan 2 ontbreken.
   Het grootste totale importoppervlak heeft **Halo** (644 UND).

## 1. Methode, en één correctie op de eerdere meting

De vier gepakte games hebben `SHNUM = 0`. Daardoor geeft `readelf --dyn-syms` daar **niets**:
geen enkele regel. Wie de symbooltabel wil lezen moet via `PT_DYNAMIC`/`DT_HASH`/`DT_SYMTAB`.

Mijn eerste lezing nam `DT_HASH`'s `nchain` als het aantal symbolen. Dat getal is te klein, en
dat is geen ongeluk: bij Galaga is `nchain = 4719`, terwijl `DT_SYMTAB` (`0x8051a0c`) tot
`DT_STRTAB` (`0x806511c`) **4977** symbolen beslaat. Die **258** extra entries zijn allemaal
`st_name = 0`, `st_shndx = 0`, `st_value = 0` — een aaneengesloten blok gewiste imports.
`nchain` is met opzet verlaagd, zodat een gewone `ld.so` ze nooit ziet.

**Gemeten**, per game, met de ruimte tot de stringtabel als maat:

| game | `DT_HASH` nchain | ruimte tot strtab | verschil | inhoud van het verschil |
|---|---|---|---|---|
| GalagaAssault | 4719 | 4977 | 258 | 258 × UND met `st_name = 0` |
| PacmanChomp | 3986 | 4234 | 248 | 248 × UND met `st_name = 0` |
| JurassicPark | 18811 | 19283 | 472 | 472 × UND met `st_name = 0` |
| Halo | 220 | 681 | 461 | 461 × UND met `st_name = 0` |

Daarmee komt de eerdere meting in `ELF-LADER-SPEC.md` uit: Galaga **361** UND (102 met naam +
258 naamloos + het nulsymbool) en Jurassic Park **472** naamloze imports. Voor die twee games
klopt de arithmetiek tot op het symbool:

| game | echte imports (GLOBAL, niet zwak) | naamloos | som | PLT-stubs | |
|---|---|---|---|---|---|
| GalagaAssault | 99 | 258 | **357** | **357** | klopt |
| JurassicPark | 105 | 472 | **577** | **577** | klopt |
| PacmanChomp | 199 | 248 | 447 | 442 | wijkt 5 af |
| Halo | 179 | 461 | 640 | 24 | wijkt af (64-bits; andere PLT-vorm) |

De drie zwakke symbolen die er in de gepakte games afgaan zijn steeds
`_ITM_deregisterTMCloneTable`, `_ITM_registerTMCloneTable` en `_Jv_RegisterClasses`; bij de
ongepakte games zijn dat `_Jv_RegisterClasses` en `__gmon_start__`.

**Wat hieruit volgt voor de API-vraag:** het aantal *naamloze* imports is exact bekend, maar hun
**namen staan niet in het bestand**. In `.dynstr` staan bij Galaga, PacmanChomp en JurassicPark
slechts **2** ongerefereerde strings (allebei de HASPE-marker); bij Halo ook 2. De gewiste namen
zijn dus niet uit het bestand te herstellen — de lader moet ze uit zijn eigen tabel halen. Dat is
dezelfde conclusie als in `ELF-LADER-SPEC.md`, nu met een precieze vindplaats.

## 2. De exporttabel van beide laders

`objdump -p` op beide bestanden, naam en RVA uit de `[Ordinal/Name Pointer] Table`:

| | i386 `BudgieLoader.exe` | x64 `BudgieLoader_x64.exe` |
|---|---|---|
| bestand | PE32 i386, 11.152.552 B, 13 secties | PE32+ x86-64, 11.067.048 B, 15 secties |
| export directory | in `.brr4` op `0x1490058` | in `.brr4` op `0x101109550` |
| Export Address Table | `0xad` = **173** | `0xae` = **174** |
| naam-tabel | 173 namen | 174 namen |
| alleen in x64 | — | `ElfPreReserveCallback` |

**Let op één correctie op de spec.** De drie `ctype`-exports zijn **C++-gemangeld**:
`_Z13__ctype_b_locv`, `_Z19__ctype_tolower_locv`, `_Z19__ctype_toupper_locv` — niet
`__ctype_b_loc` c.s. Dat maakt uit voor de kruising: PacmanChomp en Halo vragen in hun UND-lijst
de **C**-naam `__ctype_b_loc`, en die matcht dus *niet* met de exportnaam van de lader.

### 2a. De twaalf eigen functies (geen `glut*`)

| ord | naam | RVA |
|---|---|---|
| 1 | `AmdPowerXpressRequestHighPerformance` | `0x15d020` |
| 2 | `GetElfBase` | `0x4430` |
| 3 | `GetElfSymbol` | `0x4540` |
| 4 | `GetMainInterfaceIP` | `0x4940` |
| 5 | `GetMainInterfaceSubnetMask` | `0x4a10` |
| 6 | `NvOptimusEnablement` | `0x15d024` |
| 7 | `TpRefreshMainInterface` | `0x4860` |
| 8 | `_Z13__ctype_b_locv` | `0x2b4c0` |
| 9 | `_Z19__ctype_tolower_locv` | `0x2b4e0` |
| 10 | `_Z19__ctype_toupper_locv` | `0x2b4f0` |
| 11 | `api` | `0x15d028` |
| 12 | `get_info` | `0x4280` |

### 2b. De 161 freeglut-exports

Volledige freeglut-API inclusief alle `*Ucall`-varianten:

| ord | naam | RVA |
|---|---|---|
| 13 | `glutAppStatusFunc` | `0x33120` |
| 14 | `glutAppStatusFuncUcall` | `0x330a0` |
| 15 | `glutBitmapCharacter` | `0x351a0` |
| 16 | `glutBitmapLength` | `0x356f0` |
| 17 | `glutBitmapWidth` | `0x355f0` |
| 18 | `glutButtonBoxFunc` | `0x32280` |
| 19 | `glutButtonBoxFuncUcall` | `0x32200` |
| 20 | `glutCloseFunc` | `0x31c00` |
| 21 | `glutCloseFuncUcall` | `0x31b80` |
| 22 | `glutCopyColormap` | `0x3ca00` |
| 23 | `glutCreateSubWindow` | `0x3dfc0` |
| 24 | `glutCreateWindow` | `0x3df30` |
| 25 | `glutDestroyWindow` | `0x3e0e0` |
| 26 | `glutDeviceGet` | `0x3cf50` |
| 27 | `glutDialsFunc` | `0x32420` |
| 28 | `glutDialsFuncUcall` | `0x323a0` |
| 29 | `glutDisplayFunc` | `0x332d0` |
| 30 | `glutDisplayFuncUcall` | `0x33240` |
| 31 | `glutEnterGameMode` | `0x36180` |
| 32 | `glutEntryFunc` | `0x31a60` |
| 33 | `glutEntryFuncUcall` | `0x319e0` |
| 34 | `glutEstablishOverlay` | `0x3ca30` |
| 35 | `glutExit` | `0x3b8d0` |
| 36 | `glutExtensionSupported` | `0x3c6e0` |
| 37 | `glutFullScreen` | `0x3e7b0` |
| 38 | `glutFullScreenToggle` | `0x3e8f0` |
| 39 | `glutGameModeGet` | `0x362c0` |
| 40 | `glutGameModeString` | `0x35fd0` |
| 41 | `glutGet` | `0x3cc30` |
| 42 | `glutGetColor` | `0x3c9d0` |
| 43 | `glutGetModeValues` | `0x3d0b0` |
| 44 | `glutGetModifiers` | `0x3cff0` |
| 45 | `glutGetProcAddress` | `0x33c20` |
| 46 | `glutGetWindow` | `0x3e1c0` |
| 47 | `glutGetWindowData` | `0x3e960` |
| 48 | `glutHideOverlay` | `0x3ca90` |
| 49 | `glutHideWindow` | `0x3e270` |
| 50 | `glutIconifyWindow` | `0x3e2f0` |
| 51 | `glutIdleFunc` | `0x30930` |
| 52 | `glutIdleFuncUcall` | `0x308f0` |
| 53 | `glutIgnoreKeyRepeat` | `0x3c8b0` |
| 54 | `glutInit` | `0x3b740` |
| 55 | `glutInitContextFlags` | `0x3bb30` |
| 56 | `glutInitContextFunc` | `0x32f80` |
| 57 | `glutInitContextFuncUcall` | `0x32f00` |
| 58 | `glutInitContextProfile` | `0x3bb40` |
| 59 | `glutInitContextVersion` | `0x3bb10` |
| 60 | `glutInitDisplayMode` | `0x3b970` |
| 61 | `glutInitDisplayString` | `0x3b9b0` |
| 62 | `glutInitErrorFunc` | `0x3bb70` |
| 63 | `glutInitErrorFuncUcall` | `0x3bb50` |
| 64 | `glutInitWarningFunc` | `0x3bbc0` |
| 65 | `glutInitWarningFuncUcall` | `0x3bba0` |
| 66 | `glutInitWindowPosition` | `0x3b8f0` |
| 67 | `glutInitWindowSize` | `0x3b940` |
| 68 | `glutKeyboardFunc` | `0x30d60` |
| 69 | `glutKeyboardFuncUcall` | `0x30ce0` |
| 70 | `glutKeyboardUpFunc` | `0x30f00` |
| 71 | `glutKeyboardUpFuncUcall` | `0x30e80` |
| 72 | `glutLayerGet` | `0x3d040` |
| 73 | `glutLeaveFullScreen` | `0x3e880` |
| 74 | `glutLeaveGameMode` | `0x36260` |
| 75 | `glutLeaveMainLoop` | `0x3c6a0` |
| 76 | `glutMainLoop` | `0x3c500` |
| 77 | `glutMainLoopEvent` | `0x3c430` |
| 78 | `glutMotionFunc` | `0x31720` |
| 79 | `glutMotionFuncUcall` | `0x316a0` |
| 80 | `glutMouseFunc` | `0x313e0` |
| 81 | `glutMouseFuncUcall` | `0x31360` |
| 82 | `glutMouseWheelFunc` | `0x31580` |
| 83 | `glutMouseWheelFuncUcall` | `0x31500` |
| 84 | `glutMultiButtonFunc` | `0x32aa0` |
| 85 | `glutMultiButtonFuncUcall` | `0x32a20` |
| 86 | `glutMultiEntryFunc` | `0x32900` |
| 87 | `glutMultiEntryFuncUcall` | `0x32880` |
| 88 | `glutMultiMotionFunc` | `0x32c40` |
| 89 | `glutMultiMotionFuncUcall` | `0x32bc0` |
| 90 | `glutMultiPassiveFunc` | `0x32de0` |
| 91 | `glutMultiPassiveFuncUcall` | `0x32d60` |
| 92 | `glutOverlayDisplayFunc` | `0x31f40` |
| 93 | `glutOverlayDisplayFuncUcall` | `0x31ec0` |
| 94 | `glutPassiveMotionFunc` | `0x318c0` |
| 95 | `glutPassiveMotionFuncUcall` | `0x31840` |
| 96 | `glutPopWindow` | `0x3e730` |
| 97 | `glutPositionFunc` | `0x30bc0` |
| 98 | `glutPositionFuncUcall` | `0x30b40` |
| 99 | `glutPositionWindow` | `0x3e5a0` |
| 100 | `glutPostOverlayRedisplay` | `0x3ca60` |
| 101 | `glutPostRedisplay` | `0x33a20` |
| 102 | `glutPostWindowOverlayRedisplay` | `0x3ca70` |
| 103 | `glutPostWindowRedisplay` | `0x33be0` |
| 104 | `glutPushWindow` | `0x3e6b0` |
| 105 | `glutRemoveOverlay` | `0x3ca40` |
| 106 | `glutReportErrors` | `0x3c7a0` |
| 107 | `glutReshapeFunc` | `0x33480` |
| 108 | `glutReshapeFuncUcall` | `0x33400` |
| 109 | `glutReshapeWindow` | `0x3e490` |
| 110 | `glutSetColor` | `0x3c9a0` |
| 111 | `glutSetCursor` | `0x33920` |
| 112 | `glutSetIconTitle` | `0x3e400` |
| 113 | `glutSetKeyRepeat` | `0x3c930` |
| 114 | `glutSetOption` | `0x3caa0` |
| 115 | `glutSetVertexAttribCoord3` | `0x3b090` |
| 116 | `glutSetVertexAttribNormal` | `0x3b0b0` |
| 117 | `glutSetVertexAttribTexCoord2` | `0x3b0d0` |
| 118 | `glutSetWindow` | `0x3e140` |
| 119 | `glutSetWindowData` | `0x3e9d0` |
| 120 | `glutSetWindowTitle` | `0x3e370` |
| 121 | `glutSetupVideoResizing` | `0x3dd10` |
| 122 | `glutShowOverlay` | `0x3ca80` |
| 123 | `glutShowWindow` | `0x3e1f0` |
| 124 | `glutSolidCone` | `0x3a050` |
| 125 | `glutSolidCube` | `0x3a310` |
| 126 | `glutSolidCylinder` | `0x3a120` |
| 127 | `glutSolidDodecahedron` | `0x3a650` |
| 128 | `glutSolidIcosahedron` | `0x3a980` |
| 129 | `glutSolidOctahedron` | `0x3aa00` |
| 130 | `glutSolidRhombicDodecahedron` | `0x3acc0` |
| 131 | `glutSolidSierpinskiSponge` | `0x3afb0` |
| 132 | `glutSolidSphere` | `0x39f80` |
| 133 | `glutSolidTetrahedron` | `0x3b050` |
| 134 | `glutSolidTorus` | `0x3a250` |
| 135 | `glutSpecialFunc` | `0x310a0` |
| 136 | `glutSpecialFuncUcall` | `0x31020` |
| 137 | `glutSpecialUpFunc` | `0x31240` |
| 138 | `glutSpecialUpFuncUcall` | `0x311c0` |
| 139 | `glutStopVideoResizing` | `0x3dd20` |
| 140 | `glutStrokeCharacter` | `0x35920` |
| 141 | `glutStrokeLength` | `0x35e00` |
| 142 | `glutStrokeLengthf` | `0x35d20` |
| 143 | `glutStrokeWidth` | `0x35c70` |
| 144 | `glutStrokeWidthf` | `0x35bf0` |
| 145 | `glutSwapBuffers` | `0x33a80` |
| 146 | `glutTabletButtonFunc` | `0x32760` |
| 147 | `glutTabletButtonFuncUcall` | `0x326e0` |
| 148 | `glutTabletMotionFunc` | `0x325c0` |
| 149 | `glutTabletMotionFuncUcall` | `0x32540` |
| 150 | `glutTimerFunc` | `0x30ac0` |
| 151 | `glutTimerFuncUcall` | `0x309e0` |
| 152 | `glutUseLayer` | `0x3ca50` |
| 153 | `glutVideoPan` | `0x3dd40` |
| 154 | `glutVideoResize` | `0x3dd30` |
| 155 | `glutVideoResizeGet` | `0x3dd00` |
| 156 | `glutVisibilityFunc` | `0x33710` |
| 157 | `glutVisibilityFuncUcall` | `0x335a0` |
| 158 | `glutWMCloseFunc` | `0x31da0` |
| 159 | `glutWMCloseFuncUcall` | `0x31d20` |
| 160 | `glutWarpPointer` | `0x339a0` |
| 161 | `glutWindowStatusFunc` | `0x320e0` |
| 162 | `glutWindowStatusFuncUcall` | `0x32060` |
| 163 | `glutWireCone` | `0x3a0c0` |
| 164 | `glutWireCube` | `0x3a2c0` |
| 165 | `glutWireCylinder` | `0x3a190` |
| 166 | `glutWireDodecahedron` | `0x3a360` |
| 167 | `glutWireIcosahedron` | `0x3a940` |
| 168 | `glutWireOctahedron` | `0x3a9c0` |
| 169 | `glutWireRhombicDodecahedron` | `0x3aa40` |
| 170 | `glutWireSierpinskiSponge` | `0x3af40` |
| 171 | `glutWireSphere` | `0x39fe0` |
| 172 | `glutWireTetrahedron` | `0x3b010` |
| 173 | `glutWireTorus` | `0x3a1f0` |

### 2c. Het verschil met x64

| ord | naam | RVA (x64) |
|---|---|---|
| 2 | `ElfPreReserveCallback` | `0x3330` |

## 3. Wat de games vragen

Per game: `DT_NEEDED`, het aantal UND-symbolen, hoeveel daarvan gewist zijn, en hoeveel namen er
in de exporttabel van de lader voorkomen. Het aantal PLT-stubs van de HASPE-vorm
(`ff 25 <got> 68 ef be ad de e9`) en het aantal `DT_JMPREL`-relocaties staan erbij omdat die de
naamloze imports kwantificeren.

| game | klasse | bits | SHNUM | HASPE | dynsym | UND | met naam | gewist | zwak | echte imports | PLT-stubs | JMPREL | NEEDED | exportmatch |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GoGoStrike | ongepakt | 32 | 26 | nee | 1535 | 135 | 134 | 0 | 2 | 132 | 0 | 132 | 5 | 0 |
| GhostBusters | ongepakt | 32 | 30 | nee | 550 | 527 | 526 | 0 | 2 | 524 | 0 | 525 | 14 | 0 |
| Terminator | ongepakt | 32 | 29 | nee | 1020 | 498 | 497 | 0 | 2 | 495 | 0 | 496 | 13 | 15 |
| Halo | gepakt (HASPE) | 64 | 0 | ja | 681 | 644 | 182 | 461 | 3 | 179 | 24 | 178 | 13 | 0 |
| GalagaAssault | gepakt (HASPE) | 32 | 0 | ja | 4977 | 361 | 102 | 258 | 3 | 99 | 357 | 100 | 10 | 0 |
| PacmanChomp | gepakt (HASPE) | 32 | 0 | ja | 4234 | 451 | 202 | 248 | 3 | 199 | 442 | 195 | 12 | 0 |
| JurassicPark | gepakt (HASPE) | 32 | 0 | ja | 19283 | 581 | 108 | 472 | 3 | 105 | 577 | 101 | 16 | 0 |

Twee dingen vallen op.

**Het aantal UND-symbolen zegt niets over de API-oppervlakte.** GhostBusters heeft 527 UND en
vraagt de lader **niets**; het haalt zijn 128 `gl*`- en 2 `glX*`-functies uit `libGL.so.1` en
`libGLU.so.1`. GalagaAssault heeft maar **102** benoemde imports (99 echte plus 3 zwakke), maar
**357** PLT-slots.

**De HASPE-games vragen geen enkele `gl*`- of `glut*`-naam in hun UND-lijst.** Voor Galaga,
PacmanChomp en JurassicPark bestaat de UND-lijst uitsluitend uit libc/POSIX, X11, `dl*`,
`pthread*` en `sem_*`/`shm*`. Alle graphics lopen daar via de 357/442/577 PLT-slots waarvan de
GOT in het bestand al gevuld is met `0xb7xxxxxx`-adressen uit het oorspronkelijke proces
(356 van 357, 441 van 442, 576 van 577). Die slots hebben **geen** `DT_JMPREL`-relocatie: van de
357 Galaga-stubs loopt de JMPREL-tabel (`0x86f83f8+`, 100 entries) naar een **ander** gebied dan
de stub-doelen (`0x830d00c+`), met **nul** overlap. De koppeling slot → naam zit dus niet in het
bestand.

### 3a. Welke `DT_NEEDED` per game waar vandaan komt

`ElfLdr2/libs` = uit de map van de lader; *host* = een 32-bits (respectievelijk 64-bits)
systeembibliotheek; *ontbreekt* = nergens in de juiste bitbreedte.

**GoGoStrike** (32-bits, 5 NEEDED)

* uit `ElfLdr2/libs`: `libstdc++.so.5`, `liballeg.so.4.0`, `libNL.so.1.6`
* van de host: `libm.so.6`, `libc.so.6`
* **ontbreekt**: niets

**GhostBusters** (32-bits, 14 NEEDED)

* uit `ElfLdr2/libs`: `libGLU.so.1`
* van de host: `libGL.so.1`, `libpthread.so.0`, `librt.so.1`, `libz.so.1`, `libgcc_s.so.1`, `libc.so.6`, `libdl.so.2`, `libstdc++.so.6`, `libm.so.6`, `libX11.so.6`
* **ontbreekt**: `libglut.so.3`, `libesd.so.0`, `/usr/lib/libcsv.so`

**Terminator** (32-bits, 13 NEEDED)

* uit `ElfLdr2/libs`: `libGLU.so.1`, `libtheora.so.0`, `libogg.so.0`, `libPhysXLoader.so.1`
* van de host: `libGL.so.1`, `libpthread.so.0`, `librt.so.1`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, `libdl.so.2`, `libstdc++.so.6`
* **ontbreekt**: `libglut.so.3`

**Halo** (64-bits, 13 NEEDED)

* uit `ElfLdr2/libs`: `libGLU.so.1`, `libpthread.so.0`, `libdl.so.2`, `libudev.so.1`, `liblapack.so.3`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`
* van de host: `libGL.so.1`, `libiw.so.30`, `libstdc++.so.6`
* **ontbreekt**: `libfmod.so.10`, `libfmodstudio.so.10`

**GalagaAssault** (32-bits, 10 NEEDED)

* uit `ElfLdr2/libs`: —
* van de host: `libGL.so.1`, `libdl.so.2`, `libpthread.so.0`, `libudev.so.1`, `librt.so.1`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, `ld-linux.so.2`, `libX11.so.6`
* **ontbreekt**: niets

**PacmanChomp** (32-bits, 12 NEEDED)

* uit `ElfLdr2/libs`: —
* van de host: `libGL.so.1`, `libdl.so.2`, `libpthread.so.0`, `libudev.so.1`, `librt.so.1`, `libX11.so.6`, `libXext.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, `ld-linux.so.2`
* **ontbreekt**: `libXrandr.so.2`

**JurassicPark** (32-bits, 16 NEEDED)

* uit `ElfLdr2/libs`: `libtheora.so.0`, `libogg.so.0`, `libPhysXLoader.so.1`, `libGLU.so.1`
* van de host: `libstdc++.so.6`, `libdl.so.2`, `libGL.so.1`, `libpthread.so.0`, `librt.so.1`, `libm.so.6`, `libudev.so.1`, `libgcc_s.so.1`, `libc.so.6`, `libX11.so.6`
* **ontbreekt**: `libSDL-1.2.so.0`, `libglut.so.3`


**Correctie op de eerdere lezing.** In `ELF-LADER-SPEC.md` staat dat `libglut.so.3` niet aanwezig
is. Dat klopt voor `ElfLdr2/libs/`, maar de formulering suggereert dat de bibliotheek nergens is.
Gemeten: `/lib/libglut.so.3` **bestaat** op de Deck, en ook `/lib/libXrandr.so.2` en
`/lib/libiw.so.30`. Al die drie zijn **ELF64**. De 32-bits multilib-set staat in `/usr/lib32`
(443 namen) en bevat `libGL.so.1`, `libX11.so.6`, `libXext.so.6`, `libc.so.6` — maar **niet**
`libglut.so.3`, `libXrandr.so.2`, `libSDL-1.2.so.0` of `libesd.so.0`. De conclusie van de spec
blijft dus staan, maar de reden is bitbreedte, niet afwezigheid.

### 3b. De kruising UND × exporttabel

Per game welke UND-namen in de exporttabel van de lader staan, en welke exportnamen als
**zelfstandige** NUL-terminated string in het bestand staan (de `dlsym`-route). Een substring
telt hier niet mee: `hasp_get_info` bevat wel `get_info`, maar is een ander symbool.

| game | UND-namen in de exporttabel | exportnamen als zelfstandige string |
|---|---|---|
| GoGoStrike | — (geen) | — (geen) |
| GhostBusters | — (geen) | — (geen) |
| Terminator | `glutCreateWindow`, `glutDisplayFunc`, `glutEnterGameMode`, `glutGameModeString`, `glutGetModifiers`, `glutInit`, `glutInitDisplayMode`, `glutInitWindowPosition`, `glutInitWindowSize`, `glutMainLoop`, `glutPostRedisplay`, `glutReshapeFunc`, `glutSetCursor`, `glutSetWindow`, `glutSwapBuffers` | `glutCreateWindow`, `glutDisplayFunc`, `glutEnterGameMode`, `glutGameModeString`, `glutGetModifiers`, `glutInit`, `glutInitDisplayMode`, `glutInitWindowPosition`, `glutInitWindowSize`, `glutMainLoop`, `glutPostRedisplay`, `glutReshapeFunc`, `glutSetCursor`, `glutSetWindow`, `glutSwapBuffers` |
| Halo | — (geen) | — (geen) |
| GalagaAssault | — (geen) | — (geen) |
| PacmanChomp | — (geen) | — (geen) |
| JurassicPark | — (geen) | `glutBitmapCharacter` |

Dit is het scherpste resultaat van de meting. Alleen Terminator levert een **harde** koppeling:
15 `glut*`-namen staan zowel in zijn UND-lijst als als letterlijke string. Bij JurassicPark staat
`glutBitmapCharacter` alleen als string. Bij de overige vijf games is er **geen enkele**
naamverwijzing naar de 173 exports.

**Twee eerdere kandidaten vervallen.** Ik vond eerst `api` in vijf games en `get_info` in drie
games als letterlijke string, en dat leek de `dlsym`-route naar de lader. Na het opleggen van een
woordgrens blijkt het om substrings te gaan: de treffer is `hasp_get_info` (een
HASP-symbool — buiten scope, zie de harde regels) en bij `api` om het staartje van langere
strings. Er is dus **geen** gemeten bewijs dat een game `api` of `get_info` uit de lader haalt.
Die twee horen wel in de compatibiliteitslijst (§6), niet in de "gemeten nodig"-lijst.

**Eén gat in de API.** JurassicPark vraagt als string ook `glutBitmap8By13`. Die naam staat
**niet** in de 173 exports van BudgieLoader. Wie drop-in compatibel wil zijn moet hem toevoegen,
of accepteren dat die look-up `NULL` teruggeeft.

### 3c. Vereniging en doorsnede over de zeven games

**Op exportniveau.** Geen enkele export wordt door alle zeven games gevraagd: de doorsnede is
**leeg**. De vereniging is **16** namen — 15 × `glut*` (Terminator) plus `glutBitmapCharacter`
(JurassicPark), elk door precies **één** game gevraagd.

* 1/7 — `glutBitmapCharacter`
* 1/7 — `glutCreateWindow`
* 1/7 — `glutDisplayFunc`
* 1/7 — `glutEnterGameMode`
* 1/7 — `glutGameModeString`
* 1/7 — `glutGetModifiers`
* 1/7 — `glutInit`
* 1/7 — `glutInitDisplayMode`
* 1/7 — `glutInitWindowPosition`
* 1/7 — `glutInitWindowSize`
* 1/7 — `glutMainLoop`
* 1/7 — `glutPostRedisplay`
* 1/7 — `glutReshapeFunc`
* 1/7 — `glutSetCursor`
* 1/7 — `glutSetWindow`
* 1/7 — `glutSwapBuffers`

**Op UND-niveau** (alle geïmporteerde namen, dus inclusief libc/POSIX):

* **vereniging**: 805 unieke UND-namen over de zeven games
* **doorsnede**: 21 namen, door alle zeven gevraagd

| # games | namen |
|---|---|
| 7/7 | `_Jv_RegisterClasses`, `__errno_location`, `calloc`, `close`, `exit`, `fclose`, `fileno`, `fopen`, `fprintf`, `fread`, `free`, `ioctl`, `malloc`, `memset`, `mktime`, `open`, `sleep`, `strcpy`, `strerror`, `strlen`, `time` |
| 6/7 | `__fxstat`, `__xstat`, `bind`, `clock`, `closedir`, `connect`, `dladdr`, `dlerror`, `dlsym`, `fcntl`, `fwprintf`, `fwrite`, `getenv`, `getgid`, `gethostbyname`, `getpid`, `getppid`, `getpwuid_r`, `getservbyname`, `getsockname`, `gettimeofday`, `getuid`, `gmtime`, `gmtime_r`, `inet_addr`, `inet_ntoa`, `kill`, `localtime_r`, `memmove`, `nanosleep`, `opendir`, `poll`, `pthread_attr_destroy`, `pthread_attr_init`, `pthread_attr_setdetachstate`, `pthread_create`, `pthread_mutex_destroy`, `pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock`, `readdir`, `realloc`, `recv`, `recvfrom`, `select`, `send`, `sendto`, `setsockopt`, `sigaction`, `socket`, `strcasecmp`, `strcat`, `strncpy`, `usleep`, `waitpid` |
| 5/7 | `_exit`, `fgets`, `fork`, `gethostname`, `lseek`, `memcpy`, `pthread_cancel`, `pthread_sigmask`, `read`, `setvbuf`, `strcmp`, `strstr`, `strtoul`, `write` |
| 4/7 | `_ITM_deregisterTMCloneTable`, `_ITM_registerTMCloneTable`, `__ctype_b_loc`, `__gxx_personality_v0`, `__libc_current_sigrtmin`, `__stack_chk_fail`, `__strtol_internal`, `__strtoul_internal`, `atol`, `dlclose`, `dlopen`, `dlvsym`, `endutent`, `fgetc`, `fputc`, `freeaddrinfo`, `fseek`, `ftell`, `getaddrinfo`, `getlogin_r`, `getsockopt`, `getutent_r`, `inet_ntop`, `listen`, `mkdir`, `pipe`, `pthread_cond_broadcast`, `pthread_cond_init`, `pthread_cond_signal`, `pthread_cond_timedwait`, `pthread_cond_wait`, `pthread_equal`, `pthread_rwlock_destroy`, `pthread_rwlock_init`, `pthread_rwlock_rdlock`, `pthread_rwlock_unlock`, `pthread_rwlock_wrlock`, `pthread_self`, `ptrace`, `rename`, `rmdir`, `setrlimit`, `setutent`, `sigdelset`, `sigfillset`, `signal`, `snprintf`, `sscanf`, `strchr`, `strcoll`, `strncmp`, `strtol`, `strtoull`, `syscall`, `ttyname_r`, `uname`, `unlink` |
| 3/7 | `_ZdaPv`, `_Znaj`, `__cxa_atexit`, `__cxa_pure_virtual`, `__gmon_start__`, `__libc_start_main`, `__memcpy_chk`, `__snprintf_chk`, `__sprintf_chk`, `accept`, `access`, `acos`, `atan2`, `atoi`, `cfsetispeed`, `cfsetospeed`, `cos`, `endmntent`, `ferror`, `fflush`, `fopen64`, `fscanf`, `geteuid`, `getmntent`, `localtime`, `memcmp`, `perror`, `printf`, `pthread_cond_destroy`, `pthread_join`, `qsort`, `readlink`, `sem_init`, `sem_post`, `sem_wait`, `setmntent`, `shmat`, `shmctl`, `shmdt`, `shmget`, `shutdown`, `sin`, `sprintf`, `sqrt`, `sysconf`, `system`, `tcflush`, `tcgetattr`, `tcsetattr`, `vsprintf` |
| 2/7 | `XCloseDisplay`, `XFlush`, `XGetWindowAttributes`, `XOpenDisplay`, `_Unwind_Resume`, `_ZSt17__throw_bad_allocv`, `_ZSt20__throw_length_errorPKc`, `_ZdlPv`, `_Znwj`, `__assert_fail`, `__ctype_tolower_loc`, `__ctype_toupper_loc`, `__cxa_begin_catch`, `__cxa_end_catch`, `__cxa_rethrow`, `__h_errno_location`, `__lxstat64`, `__xstat64`, `_setjmp`, `abort`, `acosf`, `asctime`, `asctime_r`, `asinf`, `atanf`, `atof`, `atoll`, `ceil`, `ceilf`, `clock_getres`, `clock_gettime`, `cosf`, `dl_iterate_phdr`, `dup`, `expf`, `fdatasync`, `fdopen`, `feof`, `fgetpos`, `floor`, `fputs`, `freeifaddrs`, `fsetpos`, `getcwd`, `getifaddrs`, `getnameinfo`, `getpeername`, `getpwuid`, `glActiveTexture`, `glAlphaFunc`, `glAreTexturesResident`, `glAttachShader`, `glBindAttribLocation`, `glBindBuffer`, `glBindTexture`, `glBlendEquation`, `glBlendFunc`, `glBufferData`, `glClear`, `glClearColor`, `glClearDepth`, `glClearStencil`, `glClientActiveTexture`, `glColor3f`, `glColor4f`, `glColorMask`, `glCompileShader`, `glCompressedTexImage2D`, `glCreateProgram`, `glCreateShader`, `glCullFace`, `glDeleteBuffers`, `glDeleteLists`, `glDeleteProgram`, `glDeleteShader`, `glDeleteTextures`, `glDepthFunc`, `glDepthMask`, `glDepthRange`, `glDisable`, `glDisableClientState`, `glDisableVertexAttribArray`, `glDrawArrays`, `glDrawBuffer`, `glDrawBuffers`, `glDrawRangeElements`, `glEnable`, `glEnableClientState`, `glEnableVertexAttribArray`, `glFlush`, `glFrontFace`, `glGenBuffers`, `glGenTextures`, `glGetAttribLocation`, `glGetError`, `glGetIntegerv`, `glGetProgramInfoLog`, `glGetProgramiv`, `glGetShaderInfoLog`, `glGetShaderiv`, `glGetString`, `glGetTexLevelParameteriv`, `glGetUniformLocation`, `glHint`, `glIsTexture`, `glLightModelfv`, `glLightModeli`, `glLineWidth`, `glLinkProgram`, `glLoadIdentity`, `glLoadMatrixf`, `glMapBuffer`, `glMatrixMode`, `glOrtho`, `glPixelStorei`, `glPointSize`, `glPolygonMode`, `glPolygonOffset`, `glReadBuffer`, `glReadPixels`, `glShadeModel`, `glShaderSource`, `glStencilMask`, `glTexCoordPointer`, `glTexEnvi`, `glTexImage2D`, `glTexParameterfv`, `glTexParameteri`, `glTexSubImage2D`, `glTranslatef`, `glUniform1fv`, `glUniform1iv`, `glUniform2fv`, `glUniform3fv`, `glUniform4fv`, `glUniformMatrix3fv`, `glUniformMatrix3x4fv`, `glUniformMatrix4fv`, `glUniformMatrix4x3fv`, `glUnmapBuffer`, `glUseProgram`, `glVertexAttribPointer`, `glVertexPointer`, `glViewport`, `gluBuild2DMipmaps`, `gluErrorString`, `gnu_dev_major`, `gnu_dev_makedev`, `gnu_dev_minor`, `iconv`, `iconv_close`, `iconv_open`, `isalnum`, `isalpha`, `localeconv`, `log`, `log10`, `longjmp`, `mbstowcs`, `mmap`, `mprotect`, `open64`, `popen`, `pow`, `powf`, `prctl`, `pthread_attr_setstacksize`, `pthread_getspecific`, `pthread_key_create`, `pthread_kill`, `pthread_mutex_trylock`, `pthread_mutexattr_init`, `pthread_once`, `pthread_setspecific`, `putchar`, `puts`, `rand`, `rewind`, `sched_yield`, `sem_close`, `sem_open`, `sem_unlink`, `settimeofday`, `sigemptyset`, `sigqueue`, `sinf`, `sqrtf`, `srand`, `srandom`, `strftime`, `strncasecmp`, `strncat`, `strrchr`, `strtok`, `strtok_r`, `swprintf`, `symlink`, `tan`, `tanf`, `tolower`, `toupper`, `tzset`, `umask`, `vfprintf`, `vprintf`, `vsnprintf`, `wcscat`, `wcscmp`, `wcscpy`, `wcslen`, `wcsncpy`, `wcsstr`, `wcstod`, `wcstok`, `wcstol`, `wcstoll`, `wcstombs`, `wcstoull` |
| 1/7 | *390 namen, één per game — zie de per-game lijsten* |

## 4. De `libglut`-route

De eerdere meting stelde: `libglut.so.3` wordt niet als bibliotheek geleverd maar door de lader
zelf, via zijn exports. Dat is **bevestigd**, met twee preciseringen.

* **De namen staan in de exporttabel.** 161 van de 173 exports beginnen met `glut`, inclusief
  alle `*Ucall`-varianten (`glutDisplayFuncUcall`, `glutKeyboardFuncUcall`, …) en de freeglut
  game-mode- en uitbreidingsfuncties (`glutEnterGameMode`, `glutGameModeString`,
  `glutGetModeValues`, `glutSetVertexAttribCoord3`, …). Ter vergelijking: de echte freeglut 3.x
  exporteert er ongeveer 120; de lader heeft er 161.
* **Eén game vraagt ze ook echt.** Terminator importeert 15 `glut*`-namen, zowel in UND als als
  string. Dat is de enige game waarin de `libglut`-route direct meetbaar is.
* **Maar de host heeft geen 32-bits `libglut.so.3`.** `/lib/libglut.so.3` is ELF64. Voor de vijf
  32-bits games is er dus geen alternatief: de freeglut-API moet van ons komen.
* **De gepakte games noemen geen enkele `glut*`-naam.** Hun `glut`-aanroepen lopen via de
  naamloze PLT-slots. Dat is precies waarom de lader de API onder *die* namen exporteert: niet
  omdat de game ze opzoekt, maar omdat de lader ze zelf in de slots schrijft.

**Afgeleid:** de 161 `glut*`-exports zijn de brug die de naamloze slots vult. De game ziet geen
naam, de lader wel. Wie de lader vervangt moet dus óf dezelfde 161 namen leveren, óf de
slot→naam-tabel uit een lopende run overnemen (fase 1B).

## 5. De nagebootste bibliotheken

`ElfLdr2/libs/` heeft **77** entries, `x64/libs/` **19**. Daarvan wordt maar een klein deel
werkelijk gevraagd. Welke game welke vraagt:

**Uit `ElfLdr2/libs/` (32-bits):**

* `libGLU.so.1` — GhostBusters, Terminator, JurassicPark
* `libNL.so.1.6` — GoGoStrike
* `libPhysXLoader.so.1` — Terminator, JurassicPark
* `liballeg.so.4.0` — GoGoStrike
* `libogg.so.0` — Terminator, JurassicPark
* `libstdc++.so.5` — GoGoStrike
* `libtheora.so.0` — Terminator, JurassicPark

**Uit `x64/libs/` (64-bits):**

* `libGLU.so.1` — Halo
* `libc.so.6` — Halo
* `libdl.so.2` — Halo
* `libgcc_s.so.1` — Halo
* `liblapack.so.3` — Halo
* `libm.so.6` — Halo
* `libpthread.so.0` — Halo
* `libudev.so.1` — Halo

**Gevraagd maar niet geleverd, in de juiste bitbreedte:**

| bibliotheek | gevraagd door | waar het misgaat |
|---|---|---|
| `libglut.so.3` | Terminator, JurassicPark, GhostBusters | alleen 64-bits op de host |
| `libXrandr.so.2` | PacmanChomp | alleen 64-bits op de host |
| `libSDL-1.2.so.0` | JurassicPark | nergens aanwezig |
| `libesd.so.0` | GhostBusters | nergens aanwezig |
| `libfmod.so.10`, `libfmodstudio.so.10` | Halo | nergens; `libs/` heeft alleen `.so.7` (32-bits) |
| `/usr/lib/libcsv.so` | GhostBusters | **absoluut** `DT_NEEDED`-pad; `ElfLdr2/libs/libcsv.so` bestaat wel, dus de lader moet een absoluut pad op de basename mappen |

**Afgeleid:** een eigen lader hoeft van de 77 entries in `libs/` dus maar een handvol echt te
leveren — de rest is bagage voor games buiten deze zeven.

## 6. De minimale API voor onze eigen lader

Er zijn twee lijsten, en het verschil ertussen is het belangrijkste inzicht van deze meting.

### 6a. Gemeten nodig: namen die een game werkelijk noemt

Dit is de harde kern. Gesorteerd op het aantal games dat de naam vraagt.

| # games | functie | welke game | hoe gemeten |
|---|---|---|---|
| 1/7 | `glutBitmapCharacter` | JurassicPark | letterlijke string |
| 1/7 | `glutCreateWindow` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutDisplayFunc` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutEnterGameMode` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutGameModeString` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutGetModifiers` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutInit` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutInitDisplayMode` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutInitWindowPosition` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutInitWindowSize` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutMainLoop` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutPostRedisplay` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutReshapeFunc` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutSetCursor` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutSetWindow` | Terminator | UND-import + letterlijke string |
| 1/7 | `glutSwapBuffers` | Terminator | UND-import + letterlijke string |

Alle zestien komen uit **één** game. Er is geen enkele functie die meer dan één game bij naam
vraagt — de doorsnede over de zeven is leeg. Dat is geen geruststelling: het betekent dat de
gepakte klasse zijn API **naamloos** gebruikt.

### 6b. Compatibiliteitslijst: de volledige 173

Omdat de 357/442/577/24 PLT-slots van de gepakte games naamloos zijn en de koppeling slot → naam
niet in het bestand zit, valt uit de bestanden **niet** af te leiden welke van de 161 `glut*`-
functies een gepakte game nodig heeft. De enige veilige ondergrens is: alles wat de lader zelf
exporteert. Wie BudgieLoader wil vervangen moet dus de **hele** tabel leveren:

* **161 × `glut*`** — de freeglut-API uit §2b. Dit is de grootste brok werk en het is de brug
  naar de naamloze slots.
* **12 × eigen functies** uit §2a:
  * `GetElfBase`, `GetElfSymbol` — geen enkele game in deze zeven noemt ze; **afgeleid** dat ze
    voor de lader zelf of voor `TeknoParrot.dll` zijn, niet voor de gast.
  * `GetMainInterfaceIP`, `GetMainInterfaceSubnetMask`, `TpRefreshMainInterface` — idem, geen
    gemeten consument in de games.
  * `AmdPowerXpressRequestHighPerformance`, `NvOptimusEnablement` — dit zijn **datasymbolen**
    (RVA `0x15d020`/`0x15d024` in `.data`), geen functies. Een Windows-driver leest ze; onder
    onze eigen lader zijn het waarschijnlijk niet meer dan twee geëxporteerde variabelen.
  * `_Z13__ctype_b_locv`, `_Z19__ctype_tolower_locv`, `_Z19__ctype_toupper_locv` — de
    C++-gemangelde `ctype`-varianten. Let op: PacmanChomp en Halo importeren de **C**-naam
    `__ctype_b_loc`, die de host-glibc gewoon levert. Deze drie exports lijken bedoeld voor een
    gast die met een C++-compiler is gebouwd.
  * `api`, `get_info` — **geen** gemeten consument (§3b). Meenemen voor compatibiliteit.
* **1 × `ElfPreReserveCallback`** — alleen in de x64-variant.

### 6c. Wat er verder geleverd moet worden

Naast de naamruimte:

1. **De ontbrekende bibliotheken uit §5**, in ieder geval `libglut.so.3` (32-bits) voor
   Terminator en JurassicPark, en de mapping van het absolute pad `/usr/lib/libcsv.so` naar
   `libcsv.so` (die staat wél in `ElfLdr2/libs/`, maar de game vraagt hem onder een absoluut pad).
2. **De zeven `libs/`-bibliotheken die de games echt vragen** (§5) — plus `libcsv.so`, dat dus om
   dezelfde reden als hierboven meekomt.
3. **De 21 namen uit de UND-doorsnede** die alle zeven games importeren — die komen normaal uit
   de host-glibc, maar een eigen lader moet ze kunnen resolven: `_Jv_RegisterClasses`, `__errno_location`, `calloc`, `close`, `exit`, `fclose`, `fileno`, `fopen`, `fprintf`, `fread`, `free`, `ioctl`, `malloc`, `memset`, `mktime`, `open`, `sleep`, `strcpy`, `strerror`, `strlen`, `time`.

## 7. Wat open blijft

1. **Welke functie achter een naamloos slot hoort.** Dit is de kern van fase 3 en met alleen
   bestanden niet op te lossen. De 258/248/472/461 gewiste entries hebben geen naam meer in
   `.dynstr`; het GOT-slot heeft in het bestand een `0xb7xxxxxx`-adres uit het oorspronkelijke
   proces. `hints.dat` (235.414 × 12 B) is de enige kandidaat-tabel en is in deze meting niet
   opengebroken. **Hangt aan fase 1B.**
2. **Waarom `DT_HASH`'s `nchain` verlaagd is.** Gemeten is dát het zo is (258/248/472/461);
   of de HASPE-pakker dat expres doet om `ld.so` te misleiden, of dat het een neveneffect is, is
   niet gemeten.
3. **Of de 16 gemeten namen de hele behoefte zijn.** Terminator en JurassicPark vragen `glut*`
   bij naam; de andere vijf niet. Of de naamloze slots in de praktijk dezelfde 161 functies
   gebruiken of een kleinere deelverzameling, is zonder lopende run niet te zeggen.
4. **`glutBitmap8By13`.** JurassicPark vraagt die naam, de lader exporteert hem niet. Of de game
   zonder die look-up verder komt, is niet gemeten.
5. **Waarom GhostBusters `libglut.so.3` in `DT_NEEDED` heeft staan terwijl het nul `glut*`-
   symbolen importeert.** Gemeten is dat het zo is (0 UND-matches, 0 `glut*`-strings, wel 128
   `gl*` uit `libGL.so.1`). Vermoedelijk een overblijfsel van de link.
6. **`GetElfBase`/`GetElfSymbol`.** Geen gemeten consument in de zeven games. Wie ze werkelijk
   aanroept — de lader zelf, `TeknoParrot.dll`, of de gast via een `dlsym` waarvan de string
   niet in het bestand staat — is open.
7. **Het x64-verhaal is dun.** Halo is de enige 64-bits game, heeft een andere PLT-vorm
   (`ff 25` is RIP-relatief; de 24 stub-doelen liggen op `0x1690b70+` en bevatten
   `0x7fd0...`-adressen), en de relatie `naamloos + imports = stubs` gaat daar niet op. De x64
   lader heeft dezelfde 161 `glut*` + 12 eigen + `ElfPreReserveCallback`, maar welke daarvan
   Halo nodig heeft is niet gemeten.
8. **Buiten scope gelaten.** In de bestanden komen `hasp_get_info` (in de `.dynstr` van Galaga,
   PacmanChomp en JurassicPark) en `libhasp_linux_*.so` (in de Jurassic Park-map) voor. Dat is
   dongle-werk en is hier verder niet aangeraakt.

## Bijlage A — de commando's

Alles read-only, geen game gestart. `R` is de ElfLdr2-map, `NAS` de NAS-mount:

```bash
R=~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2
NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot

# de exporttabellen (173 en 174): naam + RVA uit de [Ordinal/Name Pointer] Table
objdump -p "$R/BudgieLoader.exe" | grep -A400 'The Export Tables'
objdump -p "$R/x64/BudgieLoader_x64.exe" | grep -A400 'The Export Tables'

# klasse per game: SHNUM, entry, NEEDED
readelf -h -d "$NAS/Galaga Assault/game" | grep -E 'Class|Entry|Number of section|NEEDED'

# de HASPE-marker
strings -a "$NAS/Galaga Assault/game" | grep HASPE

# readelf --dyn-syms geeft NIETS bij de gepakte games (SHNUM=0) -> zelf parsen via PT_DYNAMIC
readelf --dyn-syms -W "$NAS/Galaga Assault/game" | head -2

# de bibliotheekresolutie (arch-bewust: /lib is 64-bits, /usr/lib32 is 32-bits)
ldconfig -p | grep -E 'libglut|libXrandr|libSDL-1.2'
file /lib/libglut.so.3 /usr/lib32/libGL.so.1
```

De meetscripts staan in de Windows-scratchmap `re/` onder deze werkmap:
`bg_games.py` (ELF-parser + resolutie), `bg_final.py` (per-game tabel en kruising),
`bg_res.py` (arch-bewuste `DT_NEEDED`-resolutie), `bg_table.py` (eindtabel),
`bg_gen.py` (deze tekst), `bg_diag2.py` (de gewiste entries), `bg_past_nchain.py`
(het `nchain`-bewijs), `bg_probe.py` (Halo RIP-relatief), `bg_check.py` (bitbreedte),
`bg_orphan2.py` (ongerefereerde `.dynstr`-strings).

## Bijlage B — de UND-namen die door precies één game gevraagd worden

De volledige restgroep uit §3c (390 namen). Dit zijn de imports die maar in één van de zeven
games voorkomen; de meeste zijn game-specifieke bibliotheek-API's (`allegro_*`, `nl*`, `theora_*`,
`ogg_*`, `gz*`, `glu*`) of zeldzame libc-varianten.

1. `NxCreatePhysicsSDK`
2. `NxGetCookingLib`
3. `NxGetUtilLib`
4. `XDefaultScreen`
5. `XDestroyWindow`
6. `XGrabPointer`
7. `XMapWindow`
8. `XUngrabPointer`
9. `XUnmapWindow`
10. `_ZNKSs4findEPKcjj`
11. `_ZNKSs4sizeEv`
12. `_ZNKSs7compareEPKc`
13. `_ZNSbIwSt11char_traitsIwESaIwEEC1EPKwRKS1_`
14. `_ZNSs4_Rep10_M_destroyERKSaIcE`
15. `_ZNSs6appendEPKcj`
16. `_ZNSs6appendERKSs`
17. `_ZNSs6assignERKSs`
18. `_ZNSs7reserveEj`
19. `_ZNSsC1EPKcRKSaIcE`
20. `_ZNSsC1ERKSs`
21. `_ZNSsC1ERKSsjj`
22. `_ZNSsC1Ev`
23. `_ZNSsD1Ev`
24. `_ZNSsaSEPKc`
25. `_ZNSt15_List_node_base4hookEPS_`
26. `_ZNSt15_List_node_base6unhookEv`
27. `_ZNSt8ios_base4InitD1Ev`
28. `_ZNSt9exceptionD2Ev`
29. `_ZSt18_Rb_tree_decrementPKSt18_Rb_tree_node_base`
30. `_ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base`
31. `_ZSt18_Rb_tree_incrementPKSt18_Rb_tree_node_base`
32. `_ZSt20__throw_out_of_rangePKc`
33. `_ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_`
34. `_ZSt9terminatev`
35. `__cxa_allocate_exception`
36. `__cxa_free_exception`
37. `__cxa_guard_abort`
38. `__cxa_guard_acquire`
39. `__cxa_guard_release`
40. `__cxa_throw`
41. `__finitef`
42. `__fpclassifyf`
43. `__fprintf_chk`
44. `__isoc99_fscanf`
45. `__isoc99_sscanf`
46. `__isoc99_swscanf`
47. `__lxstat`
48. `__memset_chk`
49. `__strcat_chk`
50. `__strdup`
51. `__strtod_internal`
52. `__strtok_r`
53. `__sysv_signal`
54. `__vfprintf_chk`
55. `_xdga_switch_bank`
56. `_xwin_unwrite_line`
57. `_xwin_write_line`
58. `allegro_exit`
59. `allegro_message`
60. `asin`
61. `atan`
62. `atan2f`
63. `backtrace`
64. `backtrace_symbols`
65. `bcopy`
66. `blit`
67. `bzero`
68. `clear_bitmap`
69. `clearerr`
70. `crc32`
71. `create_rgb_table`
72. `csv_write2`
73. `ctime`
74. `deflate`
75. `deflateEnd`
76. `deflateInit2_`
77. `deflateInit_`
78. `drawing_mode`
79. `execvp`
80. `exp`
81. `floorf`
82. `fmodf`
83. `fseeko64`
84. `fsync`
85. `ftello64`
86. `ftruncate`
87. `get_palette`
88. `gethostbyname2`
89. `getnetbyname`
90. `getpagesize`
91. `getpgid`
92. `glBegin`
93. `glBindBufferBase`
94. `glBindFragDataLocation`
95. `glBindFramebuffer`
96. `glBindFramebufferEXT`
97. `glBindRenderbuffer`
98. `glBitmap`
99. `glBlitFramebuffer`
100. `glBufferSubData`
101. `glCallList`
102. `glCheckFramebufferStatus`
103. `glCheckFramebufferStatusEXT`
104. `glColor3ub`
105. `glColor4fv`
106. `glColor4ub`
107. `glColorMaterial`
108. `glColorPointer`
109. `glCopyTexSubImage2D`
110. `glEnd`
111. `glEndList`
112. `glFinish`
113. `glFogf`
114. `glFogfv`
115. `glFogi`
116. `glFramebufferRenderbuffer`
117. `glFramebufferTexture2D`
118. `glFramebufferTexture2DEXT`
119. `glGenFramebuffers`
120. `glGenFramebuffersEXT`
121. `glGenLists`
122. `glGenRenderbuffers`
123. `glGenerateMipmap`
124. `glGenerateMipmapEXT`
125. `glGetActiveAttrib`
126. `glGetActiveUniform`
127. `glGetActiveUniformBlockName`
128. `glGetActiveUniformBlockiv`
129. `glGetActiveUniformName`
130. `glGetActiveUniformsiv`
131. `glGetDoublev`
132. `glGetFloatv`
133. `glGetInfoLogARB`
134. `glGetObjectParameterivARB`
135. `glGetRenderbufferParameteriv`
136. `glGetShaderSource`
137. `glGetTexParameterfv`
138. `glGetUniformBlockIndex`
139. `glLightf`
140. `glLightfv`
141. `glLoadMatrixd`
142. `glMaterialf`
143. `glMaterialfv`
144. `glMateriali`
145. `glMultMatrixd`
146. `glMultMatrixf`
147. `glNewList`
148. `glNormal3f`
149. `glNormalPointer`
150. `glPixelMapfv`
151. `glPopAttrib`
152. `glPopMatrix`
153. `glPushAttrib`
154. `glPushMatrix`
155. `glRasterPos2i`
156. `glRenderbufferStorageMultisample`
157. `glRotatef`
158. `glScalef`
159. `glTexCoord2f`
160. `glTexImage1D`
161. `glTexImage3D`
162. `glTexParameterf`
163. `glTexParameteriv`
164. `glUniform1f`
165. `glUniform1i`
166. `glUniform2f`
167. `glUniform2i`
168. `glUniform2iv`
169. `glUniform3f`
170. `glUniform3i`
171. `glUniform3iv`
172. `glUniform4f`
173. `glUniform4i`
174. `glUniform4iv`
175. `glUniformBlockBinding`
176. `glUseProgramObjectARB`
177. `glVertex2f`
178. `glVertex2fv`
179. `glVertex2i`
180. `glVertex3f`
181. `glVertex3fv`
182. `glVertexAttrib4fv`
183. `glXGetClientString`
184. `glXGetProcAddress`
185. `glXGetProcAddressARB`
186. `glXQueryExtensionsString`
187. `glXQueryVersion`
188. `glXSwapIntervalSGI`
189. `gluDeleteQuadric`
190. `gluDisk`
191. `gluLookAt`
192. `gluNewQuadric`
193. `gluProject`
194. `gluUnProject`
195. `glutCreateWindow`
196. `glutDisplayFunc`
197. `glutEnterGameMode`
198. `glutGameModeString`
199. `glutGetModifiers`
200. `glutInit`
201. `glutInitDisplayMode`
202. `glutInitWindowPosition`
203. `glutInitWindowSize`
204. `glutMainLoop`
205. `glutPostRedisplay`
206. `glutReshapeFunc`
207. `glutSetCursor`
208. `glutSetWindow`
209. `glutSwapBuffers`
210. `gzclose`
211. `gzdopen`
212. `gzeof`
213. `gzgets`
214. `gzopen`
215. `gzputs`
216. `gzread`
217. `gzrewind`
218. `gzwrite`
219. `htonl`
220. `htons`
221. `inet_aton`
222. `inflate`
223. `inflateEnd`
224. `inflateInit2_`
225. `inflateInit_`
226. `inflateReset`
227. `install_allegro`
228. `install_int_ex`
229. `install_mouse`
230. `install_sound`
231. `install_timer`
232. `ioperm`
233. `isspace`
234. `iswalpha`
235. `iswdigit`
236. `iswspace`
237. `klogctl`
238. `load_datafile`
239. `log10f`
240. `logf`
241. `lrint`
242. `main`
243. `makecol8`
244. `masked_blit`
245. `masked_stretch_blit`
246. `memchr`
247. `modf`
248. `modff`
249. `munmap`
250. `nlAcceptConnection`
251. `nlAddrToString`
252. `nlClose`
253. `nlConnect`
254. `nlGetAllLocalAddr`
255. `nlGetError`
256. `nlGetErrorStr`
257. `nlGetLocalAddr`
258. `nlGetRemoteAddr`
259. `nlGetString`
260. `nlGetSystemError`
261. `nlGetSystemErrorStr`
262. `nlGroupAddSocket`
263. `nlGroupCreate`
264. `nlGroupDeleteSocket`
265. `nlGroupDestroy`
266. `nlInit`
267. `nlListen`
268. `nlMutexDestroy`
269. `nlMutexInit`
270. `nlMutexLock`
271. `nlMutexUnlock`
272. `nlOpen`
273. `nlPollGroup`
274. `nlRead`
275. `nlSelectNetwork`
276. `nlSetLocalAddr`
277. `nlShutdown`
278. `nlStringToAddr`
279. `nlThreadCreate`
280. `nlThreadJoin`
281. `nlThreadYield`
282. `nlWrite`
283. `ntohs`
284. `ogg_page_bos`
285. `ogg_page_serialno`
286. `ogg_stream_clear`
287. `ogg_stream_flush`
288. `ogg_stream_init`
289. `ogg_stream_packetin`
290. `ogg_stream_packetout`
291. `ogg_stream_pagein`
292. `ogg_stream_pageout`
293. `ogg_sync_buffer`
294. `ogg_sync_clear`
295. `ogg_sync_init`
296. `ogg_sync_pageout`
297. `ogg_sync_wrote`
298. `packfile_password`
299. `pclose`
300. `play_sample`
301. `poll_mouse`
302. `position_mouse`
303. `pthread_attr_setinheritsched`
304. `pthread_attr_setschedparam`
305. `pthread_attr_setschedpolicy`
306. `pthread_detach`
307. `pthread_exit`
308. `pthread_mutexattr_destroy`
309. `pthread_mutexattr_setpshared`
310. `pthread_mutexattr_settype`
311. `pthread_setcanceltype`
312. `pthread_testcancel`
313. `pthread_timedjoin_np`
314. `putenv`
315. `raise`
316. `remove`
317. `remove_int`
318. `remove_timer`
319. `replace_filename`
320. `reserve_voices`
321. `sched_get_priority_max`
322. `sched_get_priority_min`
323. `sched_getaffinity`
324. `sched_setaffinity`
325. `select_palette`
326. `sem_destroy`
327. `sem_getvalue`
328. `sem_trywait`
329. `set_alpha_blender`
330. `set_color_conversion`
331. `set_color_depth`
332. `set_gfx_mode`
333. `set_mouse_speed`
334. `set_multiply_blender`
335. `setlocale`
336. `setpgid`
337. `setuid`
338. `sigaddset`
339. `sigprocmask`
340. `sincos`
341. `sincosf`
342. `statfs`
343. `statvfs`
344. `stdin`
345. `stop_sample`
346. `strcspn`
347. `stretch_blit`
348. `stretch_sprite`
349. `strnlen`
350. `strspn`
351. `strtod`
352. `sync`
353. `text_mode`
354. `textprintf`
355. `textprintf_centre`
356. `textprintf_right`
357. `theora_clear`
358. `theora_comment_add`
359. `theora_comment_clear`
360. `theora_comment_init`
361. `theora_comment_query`
362. `theora_decode_YUVout`
363. `theora_decode_header`
364. `theora_decode_init`
365. `theora_decode_packetin`
366. `theora_encode_YUVin`
367. `theora_encode_comment`
368. `theora_encode_header`
369. `theora_encode_init`
370. `theora_encode_packetout`
371. `theora_encode_tables`
372. `theora_granule_frame`
373. `theora_granule_time`
374. `theora_info_clear`
375. `theora_info_init`
376. `timegm`
377. `timerfd_create`
378. `timerfd_settime`
379. `towlower`
380. `towupper`
381. `uncompress`
382. `ungetc`
383. `unload_datafile`
384. `unselect_palette`
385. `unsetenv`
386. `vswprintf`
387. `wcscasecmp`
388. `wcschr`
389. `wcsncmp`
390. `wcstoul`
