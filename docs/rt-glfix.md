# rt-glfix — de cdecl-brug voor de 32-bits Raw Thrills-ELF's

*RavenGun, 2026-10-11. Bron: `tools/rt-glfix/`. Bouwen: `scripts/rt-glfix.sh`. Levering: een drop-in
per game (`patches/<Game>/dropin/`), zie [dropins.md](dropins.md) voor de indeling en het contract —
dat contract staat hier niet herhaald.*

Dit document gaat over **Jurassic Park** (gemeten), en over de twee andere 32-bits Raw
Thrills-dumps die dezelfde klasse vormen: **Pacman Chomp-Mania** en **Galaga Assault**. Die drie
hebben nul sectiekoppen, en daarom kan ElfLdr2 hun symbolen niet parsen.

---

## 1. Stand

| | |
|---|---|
| De cdecl-brug zelf | **werkt en is bewezen** (zelftest + 55 GOT-invoeren omgezet in het draaiende spel) |
| Het kanaal naar het spelproces | **LD_AUDIT werkt**; `LD_PRELOAD` is onbruikbaar |
| Het spel verder krijgen | **niet gelukt**: het loopt nu ongeveer 35 s in plaats van 14 s, de crash is verplaatst, maar er komt nog geen beeld |
| Pacman Chomp-Mania, Galaga Assault | dezelfde bibliotheek is meegeleverd; **niet gemeten** (die games starten hier niet eens) |

---

## 2. Wat er misgaat

De game is een Linux-ELF en is met **cdecl** gebouwd: de aanroeper haalt de argumenten van de
stapel. ElfLdr2 zet in de GOT van de game de adressen van Wine's `opengl32.dll`. Die exporten zijn
echter **stdcall**: ze eindigen op `ret <n>` en halen de argumenten zélf van de stapel. De game
ruimt ze daarna nog eens op, en de stapelpointer loopt dus per GL-aanroep `4*n` bytes op.

### Het bewijs dat het stdcall is

`objdump` op `proton-11.0-2/lib/wine/i386-windows/opengl32.dll`, epiloog vanaf het begin van elke
functie (`tools/rt-glfix/` bevat geen kopie van dit gereedschap; het is met de hand gedaan):

| functie | argumenten (GL-specificatie) | epiloog | |
|---|---|---|---|
| `glEnable`, `glDisable`, `glClear`, `glClearStencil`, `glFrontFace`, `glMatrixMode`, `glGetString`, `glShadeModel` | 1 | `ret 0x4` | goed |
| `glGetIntegerv`, `glHint`, `glLightModeli`, `glLightModelfv`, `glPixelStorei`, `glPolygonOffset`, `glBlendFunc` | 2 | `ret 0x8` | goed |
| `glTexEnvi` | 3 | `ret 0xc` | goed |
| `glViewport` | 4 | `ret 0x10` | goed |
| `glReadPixels` | 7 | `ret 0x1c` | goed |
| `glTexImage2D`, `glTexSubImage2D` | 9 | `ret 0x24` | goed |
| `glClearDepth` | 1 `GLclampd` (8 bytes) | `ret 0x8` | goed |
| `glOrtho` | 6 `GLdouble` (48 bytes) | `ret 0x30` | goed |

20 van 20 kloppen; de twee die op het eerste gezicht afwijken (`glClearDepth`, `glOrtho`) tellen
`double`-argumenten als twee woorden. **Wine's opengl32 exporteert GL dus als stdcall.**

### De uitkomst in het spel

Basismeting, zonder enige ingreep:

```
EXCEPTION_ACCESS_VIOLATION flags=0 addr=00058000
info[0]=00000000 info[1]=00058000
eip=00058000 esp=01a9c1c8 ebp=01a9c8cc
eax=00000000 ebx=613108a0 ecx=79e8791c edx=00000000
esi=00000550 edi=00000300
```

`eip` is een sprong naar niets, `ecx` staat **in** Wine's `glGetIntegerv` (die op
`0x79e50000+0x378d0 = 0x79e878d0` staat), en `esi`/`edi` zijn 1360 en 768 — de resolutie. Dit is de
epiloog van `VidStart` (`0x8357df0`), precies zoals eerder beschreven in `findings/JurassicPark.md`.

### Hoe de GOT eruitziet

`tools/rt-glfix/analyze.py` leest tijdens het draaien het spelbeeld en de GOT:

```
spelbeeld 0x08048000-0x09128000 (17694720 bytes), rechten rwxp
PLT-stubs gevonden: 577, unieke GOT-invoeren: 577
verdeling per module:
  NIET IN EEN MODULE             269      <- ElfLdr2's nagebootste bibliotheken (anoniem, 0x71a41000-0x71eff000)
  msys-2.0.dll                   252
  opengl32.dll                    56
```

De PLT-stubs zijn te herkennen aan `ff 25 <adres> 68 ef be ad de` (`jmp [got]` gevolgd door de
gewiste relocatie-index `0xdeadbeef`). Van de 56 invoeren naar `opengl32.dll` zijn er 55 een
`gl*`-export; de 55 namen staan in het logboek van de bibliotheek.

In het **bestand** staat in 576 van de 577 GOT-invoeren een adres in het `0xb7xxxxxx`-gebied: de
game is een beeld uit een draaiend proces op de originele kast. 140 daarvan staan in
`0xb7500000-0xb75fffff`, waarvan de `gl*`-functies rond `0xb7500000` en `libdl` rond `0xb75f0000`.
`0x8add63c` bevat `0xb7500120`; `glGetIntegerv` staat in dat originele beeld dus op
`libGL + 0x120`. Van die 140 heeft ElfLdr2 er maar 55 naar een echte `opengl32`-export kunnen
omzetten; de rest komt op zijn nagebootste (lege) stub uit.

---

## 3. Wat de bibliotheek doet

`tools/rt-glfix/rt_glfix.c`, 32-bits (`gcc -m32 -shared -fPIC`).

1. Een kijker-thread wacht tot het spelbeeld er is (anonieme uitvoerbare mapping die met een
   32-bits ELF begint) en tot `opengl32.dll` gemapt is.
2. Ze zoekt de PLT-stubs van het spel en dus alle GOT-invoeren die de game via de PLT gebruikt.
3. Voor elke invoer die naar een `gl*`-export van `opengl32.dll` wijst zet ze een **eigen
   cdecl-stub**. De wachttijd is precies goed: de GOT wordt gevuld tijdens het laden, het spel
   begint pas seconden later.
4. De invoer van `glGetIntegerv` wordt vervangen door een eigen wacht die voor
   `GL_MAX_TEXTURE_SIZE` (`0xD33`) een veilige waarde teruggeeft (standaard 8192, dus > 2047) en de
   rest doorgeeft.
5. De invoer van `dlsym` wordt vervangen door een eigen wacht, zodat GLEW-lookups dezelfde
   cdecl-stubs krijgen. De GOT-invoer van `dlsym` is `0x8add6e0` (was `0x61101bfa`, Cygwin's
   `dlsym` in `msys-2.0.dll`).

### De brug: zonder het aantal argumenten te kennen

Dat is het hart. Een stub is tien bytes:

```asm
mov ecx, <echte functie>      ; b9 <imm32>
jmp common                    ; e9 <rel32>
```

`common` komt binnen met `esp = E`, `[E]` = het retouradres van de game en `[E+4..]` de argumenten:

```asm
mov edx, [&tabel]             ; de reddingsplek van deze thread
mov eax, gs:[8]               ; thread-sleutel (glibc zet %gs; zie §5)
shr eax, 4 / and eax, 63 / shl eax, 4 / add edx, eax
mov [edx], esp                ; bewaar E
mov eax, [esp] / mov [edx+4], eax   ; bewaar het retouradres
mov dword [esp], fixer        ; de echte functie keert straks naar `fixer` terug
jmp ecx                       ; naar de echte (stdcall-)functie, met de argumenten nog op hun plek
```

`fixer` wordt bereikt doordat de echte functie `ret <n>` uitvoert:

```asm
mov edx, [&tabel] ...         ; dezelfde plek (let op: via ecx, want eax is de retourwaarde)
mov [edx+8], eax              ; bewaar de RETOURWAARDE
mov esp, [edx]                ; esp terug naar E
mov eax, [edx+4] / mov [esp], eax   ; het retouradres terugzetten
add esp, 4                    ; naar E+4: de stand die cdecl na een `call` verwacht
mov eax, [edx+8]              ; retourwaarde terug
jmp dword [edx+4]             ; terug naar de game; die ruimt zijn argumenten zelf op
```

Omdat `common` `esp` bewaart en `fixer` hem terugzet, is **het aantal argumenten niet nodig**.
Dat is de winst: dezelfde brug werkt voor elke stdcall-functie, ook voor de functies waarvan we de
signatuur niet kennen. `eax` (de retourwaarde) en de callee-saved registers blijven ongemoeid;
alleen `eax`, `ecx` en `edx` worden gebruikt, en dat zijn vluchtige registers.

### De zelftest, en wat die gevangen heeft

`rtg_selftest()` bouwt een stub voor een neppe stdcall-functie in de bibliotheek zelf
(`mov 4(%esp),%eax; add 8(%esp),%eax; ret $8`), roept die aan met de cdecl-verwachting van de game
en controleert daarna dat de stapelpointer **exact** op de cdecl-stand staat en dat de uitkomst
klopt. De uitslag staat bij elke start in het logboek.

Die zelftest heeft drie echte fouten in de brug gevangen, alle drie voordat de game er last van had:

| fout | gevolg | uitslag zelftest |
|---|---|---|
| `fixer` zette `esp` op `E` in plaats van `E+4` | de stapel verschoof 4 bytes **de andere kant op** bij elke GL-aanroep | `stapelpointer verschoven` |
| de retourwaarde in `eax` werd overschreven | functies met een uitkomst (`glGetError`, `glGetString`, …) gaven onzin | `verkeerde uitkomst` |
| de sleutelberekening gebruikte `eax` vóór het bewaren | idem, alleen subtieler | `verkeerde uitkomst` |

Draaien zonder game:

```sh
scripts/rt-glfix.sh --selftest
#   [rt-glfix zelftest] zelftest brug: goed
```

---

## 4. Het kanaal: LD_AUDIT, niet LD_PRELOAD

Dit is de belangrijkste praktische vondst, en hij is gemeten.

**`LD_PRELOAD` is onbruikbaar.** BudgieLoader is een **Cygwin**-programma en Cygwin leest
`LD_PRELOAD` zelf. Het probeert onze ELF dan als Windows-DLL te laden en sterft:

```
ERROR: ld.so: object '/tmp/rt-glfix/proef64.so' from LD_PRELOAD cannot be preloaded
       (wrong ELF class: ELFCLASS64): ignored.
      0 [main] BudgieLoader 1016 ... *** fatal error - error while loading shared libraries:
      /tmp/rt-glfix/proef64.so: cannot open shared object file: No such file or directory
[launch-direct] lader gestopt (exitcode 1)
```

De game komt dan niet eens aan zijn start toe. Dit is dezelfde meting die in
`scripts/launch-direct.py` bij `dropin_plan()` staat.

**`LD_AUDIT` werkt.** Dat kent Cygwin niet, glibc wel. Het proces dat BudgieLoader draait is
**32-bits**:

```
exe = .../wine/proton-11.0-2/lib/wine/i386-unix/wine-preloader
ELF 32-bit LSB executable, Intel i386, statically linked
hoogste adres in de kaart = 0xffff0000, regels boven 4 GB = 0
/usr/lib32/libgcc_s.so.1, /usr/lib32/libglib-2.0.so.0, ...
```

Dus een `gcc -m32`-bibliotheek past precies, en hij komt in **hetzelfde adresbeeld als het spel**:

```
[rt-glfix JurassicPark] geladen (pid 999164, mode 7, maxtex 8192, brug op 0xf7c28000)
[rt-glfix JurassicPark] spelbeeld gevonden op 0x08048000 (11091968 bytes)
[rt-glfix JurassicPark] PLT-invoeren: 577
[rt-glfix JurassicPark] opengl32.dll op 0x79e50000-0x79fa8000
[rt-glfix JurassicPark] ronde 17340: 54 GL-invoeren omgezet (54 gezien)
[rt-glfix JurassicPark] dlsym-wacht geplaatst: GOT 0x08add6e0 was 0x61101bfa
[rt-glfix JurassicPark] klaar: 55 GL-invoeren omgezet, 577 invoeren bekeken
[rt-glfix JurassicPark]   0x08add63c -> 0xf7f90af0  glGetIntegerv
[rt-glfix JurassicPark]   0x08add2e0 -> 0xf7c28125  glGetString
...
```

Eén kanttekening: de **64-bits** `wine`-startwrapper ziet dezelfde `LD_AUDIT` en klaagt

```
ERROR: ld.so: object '.../librt-glfix.so' cannot be loaded as audit interface:
       wrong ELF class: ELFCLASS32; ignored.
```

Dat is onschadelijk — de 32-bits `wine-preloader` laadt hem wel, en dat is de procesruimte waarin
het spel zit.

### Wat er nog moet gebeuren om dit te leveren

De launcher zet het pad van een `Preload = true`-drop-in in `RG_DROPIN_PRELOAD` en **niet** in
`LD_PRELOAD` (terecht, want Cygwin sterft eraan — zie `dropin_plan()` in `scripts/launch-direct.py`).
Op dit moment leest niemand `RG_DROPIN_PRELOAD`. Voor deze games moet dat in **`LD_AUDIT`** van het
Wine-proces terechtkomen. Dat is één regel in de launcher, en het is de enige ontbrekende schakel:
de drop-in zelf, het kopiëren naar de view en het doorgeven van de omgevingsvariabelen werken al.

Tot die regel er is, is de brug met de hand te starten:

```sh
cd ~/projects/ravengun
XAUTHORITY=/run/user/1000/xauth_NBglcp DISPLAY=:0 \
  python3 scripts/launch-direct.py JurassicPark --run \
  --env LD_AUDIT="$HOME/.local/share/ravengun/runtime/views/JurassicPark/librt-glfix.so"
```

**`patches/JurassicPark/patches.ini` blijft hier buiten.** Dat bestand is voor bytepatches in het
Find/Replace-formaat; deze fix is een drop-in (een bestand plus één sectie in `dropin.ini`) en geen
bytepatch. Ze door elkaar halen zou de motor van `docs/patchmotor.md` onnodig belasten.

---

## 5. Waarom `ElfLdr2/libs/` niet werkt

De gedachte was: `ElfLdr2/libs/` staat op het zoekpad van de ELF-games, dus een dunne
`libGLU.so.1` daar zou in het spelproces geladen worden en zijn constructor zou vóór `main` draaien.
**Dat is niet zo, en dat is te zien in de kaart van het spelproces.**

Proef: de echte `libGLU.so.1` opzijgeschoven naar `libGLU.so.1.ravengun-orig`, een dunne
`libGLU.so.1` neergelegd met een constructor die naar `/tmp/glu-proef.log` schrijft, Jurassic Park
gestart. Uitslag:

```
  exitcode 255
  --- proeflogboek ---
  (geen regel: de constructor liep NIET)
  --- wat de lader deed ---
Detected Linux game: Jurassic Park (1.08 US)
Starting Game
```

En de kaart van het spelproces bevat **geen enkele `.so` uit `ElfLdr2/libs/`** — alleen de
Windows-DLL's uit die map:

```
$ grep "ElfLdr2/libs/" /proc/<pid>/maps | ... | sort -u
  .../ElfLdr2/libs/SDL.dll
  .../ElfLdr2/libs/SDL_image.dll
  .../ElfLdr2/libs/SDL_mixer.dll
  .../ElfLdr2/libs/SDL_net.dll
  .../ElfLdr2/libs/SDL_ttf.dll
  .../ElfLdr2/libs/TPGfx.dll
  .../ElfLdr2/libs/libfreetype-6.dll
  .../ElfLdr2/libs/regal32.dll
  .../ElfLdr2/libs/zlib1.dll
```

`libGLU.so.1`, `libPhysXLoader.so.1`, `libtheora.so.0` enzovoort komen er **niet** in voor. De
`libs/`-map wordt dus niet als `ld.so`-zoekpad gebruikt: ElfLdr2 leest de Linux-bibliotheken
hoogstens om er symbolen uit te halen, en bootst de rest na in zijn eigen anonieme uitvoerbare
gebied (`0x71a41000-0x71eff000`, direct na `TeknoParrot.dll`). **Een constructor van een bestand in
die map draait nooit.**

Kanttekening bij de proef: onze dunne `libGLU.so.1` had geen enkele `glu*`-export. Als ElfLdr2 het
bestand om die reden heeft overgeslagen, is de proef niet volledig sluitend — maar de kaart hierboven
is dat wel: er is geen `.so` uit die map in het spelproces, dus er valt ook geen constructor te
draaien.

**Bijkomend bezwaar, ook zonder deze uitslag:** de view heeft geen eigen `ElfLdr2/`; het is een
symlink naar de gedeelde runtime. Een drop-in met `To = ElfLdr2/libs` schrijft dus in de **gedeelde**
map en raakt alle acht ElfLdr2-games. Voor een proef te doen (en daarna op te ruimen), maar niet als
eindoplossing: dan moet de launcher een view zijn eigen `libs/` geven. Deze proef is netjes
teruggedraaid; `libGLU.so.1` is weer 671072 bytes.

---

## 6. Wat het spel nu doet

Met de brug actief, `WINEDEBUG=+seh`:

```
Detected Linux game: Jurassic Park (1.08 US)
Starting Game
...ongeveer 35 s...
[launch-direct] JVS-pipe: verbinding dicht (0 bytes van de game, ...)
[launch-direct] lader gestopt (exitcode 255)
```

De crash is verplaatst:

| | zonder brug | met de brug (eerste versie) | met de brug (na de zelftest) |
|---|---|---|---|
| looptijd na "Starting Game" | ~14 s | ~33 s | ~35 s |
| `eip` bij de fout | `0x00058000` | `0x00000550` | `0x819c4d80` |
| `esp` / `ebp` | `0x1a9c1c8` / `0x1a9c8cc` | `0x1a9c134` / `0x1a9c8cc` | `0x1a9c090` / `0` |
| laatste GL-aanroep | `glGetIntegerv` | — | — |

Wat dat zegt:

* **De cdecl-fout is weg.** De sprong naar `0x58000` kwam uit een verkeerd gelezen retouradres in
  de epiloog van `VidStart`; die sprong bestaat niet meer, en de looptijd is meer dan verdubbeld.
* **Er is nog een tweede fout.** `ebp = 0` en `eip = 0x819c4d80`, een adres dat in de gemeten run in
  een `---p`-gebied lag: de game raakt nog ergens de controle kwijt. Dat is niet dezelfde fout als
  de cdecl-verschuiving — daar is de aanwijzing (`ecx` in Wine's `glGetIntegerv`) nu afwezig.
* **Er komt nog geen beeld.** De schermafdruk met `ffmpeg -f x11grab` is elke keer 9066 bytes, dus
  egaal. Het venster `TeknoParrot - Jurassic Park Arcade` bestaat wel, en de JVS-brug verbindt.
* `pmuser/gamelog.txt` blijft leeg: de game komt nog niet aan zijn eerste eigen logregel.

Wat nog open is:

1. **Waar gaat het nu mis?** `ebp = 0` wijst op een aanroep door een functiewijzer die nul is, of op
   een frame dat niet is opgezet. De volgende stap is de instructie op `0x819c4d80` en de
   terugweg ernaartoe vastleggen (bijvoorbeeld met een hardware-breakpoint op het schrijven van het
   retouradres, of door de GOT-invoeren die ElfLdr2 op zijn lege stub laat staan te vullen).
2. **De ~85 GL-invoeren die ElfLdr2 niet oplost.** Van de 140 GOT-invoeren in het
   `0xb750xxxx`-gebied heeft ElfLdr2 er maar 55 naar een echte `opengl32`-export omgezet. De
   overige komen op `0x71bb3e80` uit, een gedeelde stub in het nagebootste gebied. Als de game die
   aanroept, gebeurt er niets — of iets verkeerds. Ze zijn te identificeren door de
   bestandswaarde (`libGL + offset`) te koppelen aan de naam die de 55 wél opgeloste invoeren
   opleveren.
3. **De GL_MAX_TEXTURE_SIZE-drempel is in deze omgeving geen probleem.** Wine's GL geeft op deze
   machine ruim meer dan 2047; de lockup-route in `VidStart` (`cmp [esp+0x18], 0x7ff` → `SysLockup`)
   werd alleen geraakt doordat de stapelverschuiving de verkeerde waarde op `[esp+0x18]` zette. De
   eigen `glGetIntegerv`-wacht blijft staan als vangnet, niet als hoofdoplossing.
4. **Pacman Chomp-Mania en Galaga Assault** hebben dezelfde bibliotheek in hun `dropin/`, maar zijn
   niet gemeten. De bibliotheek zoekt alles tijdens het draaien op, dus ze hoort te werken; bewijs
   is er niet.

---

## 7. De lege stubs van ElfLdr2 — onderzocht

De vraag was of de GOT-invoeren die ElfLdr2 niet oplost de tweede fout kunnen veroorzaken. Dat is
met eigen gegevens te beantwoorden.

**Hoeveel het er zijn.** Van de 93 GOT-invoeren met een `libGL`-adres in het bestand zet ElfLdr2 er
maar **22** om naar een echte `opengl32`-export. De andere **71** komen uit op één adres in zijn
nagebootste gebied. En het blijft niet bij GL: over de hele GOT-streek
(`0x08add000-0x08add900`, 576 invoeren) komt **180 keer** dezelfde waarde voor:

```
$ python3 tools/rt-glfix/probe.py --profile JurassicPark --dump-maps /tmp/f-maps.txt \
    --scan 0x8add000:2304
dwords gelezen: 576
waar de GOT-invoeren heen wijzen (rechten, module):
   r-xp   (anoniem)                        516
   rwxp   (anoniem)                         54
   r-xp   librt-glfix.so                     2
   r-xp   opengl32.dll                       1
invoeren naar een NIET-uitvoerbare mapping: 0
meest voorkomende waarden:
   0x71bb3e80  180x  r-xp
```

**Wat daar staat.** De bytes op `0x71bb3e80`:

```
55 8b ec 33 c0 5d c3      push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret
```

Dat is een **cdecl no-op die nul teruggeeft**. Hij zet zijn eigen frame op en breekt het weer af,
raakt geen geheugen aan, en laat de argumenten van de aanroeper staan.

**De conclusie.** Een aanroep naar zo'n invoer kan **geen stapelverschuiving, geen toegangsfout en
geen corrupte wijzer** veroorzaken; hij gedraagt zich als een functie die 0 teruggeeft en de game
loopt door. De lege stubs zijn dus **niet** de oorzaak van de tweede fout (`ebp=0`,
`eip=0x819c4d80`). Twee dingen steunen dat: de crash is een toegangsfout op een *wild* adres, en
`ecx=0x79f0f76c` staat bij die crash **in `opengl32.dll`** — de laatste beweging zit in het
GL-pad, niet in een no-op.

**Wat wél een echt gebrek is.** 180 van de 577 invoeren doen niets en geven 0 terug. Dat is geen
crash maar ontbrekende functionaliteit, en het kan de game later een verkeerde kant op sturen. Of
dat ook echt gebeurt, is **onbekend**: daarvoor moet je tellen of de game die invoeren aanroept.
Daar zit gereedschap voor in de bibliotheek (`RAVENGUN_GLFIX_MODE=8`, de stubteller: hij herkent de
handtekening hierboven, vervangt zulke invoeren door een wacht die telt en het retouradres logt, en
drukt elke 5 s de stand af). Die meting is **niet afgerond**: de launcher was tijdens de laatste
pogingen in bewerking door een andere agent (`AttributeError: module 'patchfile' has no attribute
'apply_game_obj'`, en daarvoor een `IndentationError`), dus de game kwam niet van de grond. De
teller zelf is wel gebouwd en de zelftest van de brug blijft `goed`.

---

## 8. Levering: twee dingen die een beslissing van de eigenaar vragen

1. **`*.so` staat in `.gitignore`.** Daardoor is `patches/<Game>/dropin/librt-glfix.so` **niet in
   Git** opgenomen, terwijl `dropin.ini` dat wel is:

   ```
   $ git check-ignore -v patches/JurassicPark/dropin/librt-glfix.so
   .gitignore:2:*.so	patches/JurassicPark/dropin/librt-glfix.so
   ```

   Een verse kloon heeft dus een `dropin.ini` die naar een bestand wijst dat er niet is. Drie
   mogelijkheden: een uitzondering toevoegen (`!patches/*/dropin/*.so`), of het bestand bij het
   installeren laten bouwen met `scripts/rt-glfix.sh`, of de drop-in-motor een ontbrekend bestand
   laten melden en bouwen. Dat is een keuze voor de eigenaar; `scripts/rt-glfix.sh --check` meldt nu
   al of de geplaatste kopie actueel is.

2. **`RG_DROPIN_PRELOAD` moet in `LD_AUDIT` terechtkomen** (zie §4). Zonder die ene regel in de
   launcher is de drop-in aanwezig maar doet hij niets.

## 9. Gereedschap in deze map

| bestand | wat het doet |
|---|---|
| `rt_glfix.c` | de bibliotheek (dit is wat de games gebruiken) |
| `gastheer32.c` | kleine 32-bits gastheer om de bibliotheek zonder game te laden (voor de zelftest) |
| `inc/gnu/stubs-32.h` | lokaal gemaakt; de 32-bits dev-header ontbreekt in lib32-glibc op SteamOS |

De meetprogramma's `probe.py` en `analyze.py` (een game als voorouder van het wine-proces starten, GOT-inventaris) zijn op
2026-10-16 verwijderd: ze startten de game via `scripts/launch-direct.py`, dat er niet meer is. Ze staan in de git-geschiedenis
(tot a951f81); de metingen hieronder zijn ermee gedaan. Een lopend spelproces lezen kan nu met `sudo gdb` (alleen lezen).
