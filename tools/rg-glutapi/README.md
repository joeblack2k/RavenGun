# rg-glutapi — de GLUT-oppervlakte van BudgieLoader, zonder BudgieLoader

`librg-glutapi.so` is een **32-bits (i386) shared object** dat **exact de 161
`glut*`-namen** exporteert die `BudgieLoader.exe` exporteert:

* **122** daarvan zijn **sprongstubs** die de aanroep doorgeven aan de echte
  libglut (onze eigen 32-bits `~/.local/share/ravengun/runtime/lib32/libglut.so.3`);
* **39** maken wij zelf: de **31 `*Ucall`-varianten**, **`glutStrokeLengthf`** en
  **`glutStrokeWidthf`** (die doorverwijzen naar hun basis) en **6 kleine
  uitbreidingen** die de echte libglut niet heeft.

De verdeling is niet met de hand ingetypt maar komt uit twee gemeten lijsten en
wordt door `gen.py` gecontroleerd (zie `meet-namen.sh`).

## Bestanden

| bestand | wat |
|---|---|
| `gen.py` | genereert `rg_stubs.S` (122 stubs + 122 noodstubs) en `rg_gen.c` (slottabel + 33 wikkels). Stopt met een fout als de invoerlijsten niet meer de gemeten 161/171/122/39/31 geven. |
| `rg_shim.c` | de lader/resolver: zoekt de echte libglut, vult de slots, meldt wat ontbreekt, en weigert zichzelf (recursiebeveiliging). |
| `rg_ext.c` | de 6 functies die de echte libglut niet heeft en die geen doorverwijzing kunnen zijn. |
| `rg_shim.h` | afspraken tussen gegenereerd en handgeschreven; alles hierin is `hidden`. |
| `build.sh` | genereert + bouwt (`gcc -m32 -shared -fPIC`), met de `inc/gnu/stubs-32.h`-truc uit `scripts/build-elfloader.sh`. |
| `verify-exports.sh` | exportvergelijking met de 161 namen van BudgieLoader: 0 ontbrekend, 0 te veel. |
| `verify-vorm.py` | per naam: sprongstub (`jmp *%ecx`) of eigen C-functie — en of dat de bedoelde naam is. |
| `verify-load.sh` | drie laadproeven in kleine 32-bits processen. |
| `meet-namen.sh` | hoe `names_budgie.txt` (161) en `names_real_libglut.txt` (171) gemeten zijn. |
| `proef/` | de proefprogramma's (zie onder). |

`rg_stubs.S`, `rg_gen.c`, `librg-glutapi.so` en `inc/` zijn **uitvoer**; de rest
is bron.

## Bouwen

```sh
bash tools/rg-glutapi/build.sh
```

Op deze machine ontbreekt `/usr/include/gnu/stubs-32.h`; `build.sh` maakt dan
`inc/gnu/stubs-32.h` als kopie van `stubs-64.h` en geeft `-I inc` mee (hetzelfde
recept als `scripts/build-elfloader.sh`).

## Laden

De sonaam is met opzet **`libglut.so.3`**, zodat het bestand de `NEEDED`-regel
van het spel kan vervangen. Zet het dan wel onder die naam neer en houd de echte
libglut onder een andere naam, anders vindt de resolver zichzelf:

```sh
cp librg-glutapi.so            <runtime>/libglut.so.3
cp <echte libglut.so.3>        <runtime>/libglut.so.3.real
```

Of wijs de echte bibliotheek expliciet aan (dan maakt de naam niet uit):

```sh
RG_GLUT_REAL=/pad/naar/libglut.so.3 LD_PRELOAD=/pad/naar/librg-glutapi.so <spel>
```

Zoekvolgorde van de resolver: `$RG_GLUT_REAL`, `/rg/lib/libglut.so.3.real`,
`/rg/lib/libglut.so.3`, `$HOME/.local/share/ravengun/runtime/lib32/libglut.so.3`,
`/usr/lib32/libglut.so.3`, `/usr/lib/i386-linux-gnu/libglut.so.3`, `libglut.so.3`.
De echte libglut wordt met `RTLD_LAZY|RTLD_GLOBAL` geladen, want hij heeft zelf
`libXi.so.6` e.a. nodig; die moeten in het zoekpad staan (`LD_LIBRARY_PATH`).

`RG_GLUTAPI_LOG=1` zet één regel op stderr met welk bestand als echte libglut
gebruikt is en hoeveel van de 122 namen opgelost zijn.

## Hoe de doorverwijzing werkt (en waarom er geen recursie is)

Per naam staat er een **sprongstub** in `rg_stubs.S`:

```asm
glutInitWindowSize:
        call 1f
1:      popl %eax
        addl $_GLOBAL_OFFSET_TABLE_+[.-1b], %eax
        movl rg_slot_glutInitWindowSize@GOTOFF(%eax), %ecx
        jmp *%ecx
```

De stub **springt** naar de bij het laden opgezochte aanroep (nooit `call`), dus
de argumenten blijven ongemoeid op de stapel staan: de stub hoeft geen enkele
signatuur te kennen en kan zichzelf niet aanroepen. De aanroepwijzer
(`rg_slot_<naam>`, `hidden`) wordt bij het laden gevuld met `dlsym` op de echte
libglut. Zou de gezochte bibliotheek onszelf blijken te zijn (bijv. omdat de
sonaam ook `libglut.so.3` is en het zoekpad ons bestand vindt), dan wordt die
kandidaat geweigerd: `dladdr` op de gevonden `glutInit` moet buiten ons eigen
adresbereik vallen.

Lukt het vullen niet (bijv. omdat de echte libglut pas later in het proces
komt), dan wijst de slot naar een **noodstub** die de argumenten met rust laat,
één keer opnieuw probeert en `0` teruggeeft. Die ene aanroep gaat dus verloren en
er komt een regel op stderr; alle namen die daarna wel opgelost worden, werken
vanaf de volgende aanroep. Dit is ook het vangnet voor een lader die zelf
`.so`'s mapt en geen `DT_INIT_ARRAY` afspeelt.

## De 39 die wij zelf leveren

**31 `*Ucall`-varianten** (`glutDisplayFuncUcall` enz.). Die zijn niet zomaar een
doorverwijzing naar de basisnaam: upstream freeglut geeft de callback een extra
`void *user_data` mee die de basis-signatuur niet heeft. Een rechte
doorverwijzing zou de callback dus met een verzonnen `user_data` aanroepen. De
wikkel bewaart daarom callback + `user_data` en installeert via de basisnaam een
**trampoline** met de basis-signatuur die de callback mét de bewaarde
`user_data` aanroept. De prototypes komen uit freeglut 3.4.0
(`include/GL/freeglut_ucall.h`).

**`glutStrokeLengthf` / `glutStrokeWidthf`**: rechte doorverwijzing naar
`glutStrokeLength` / `glutStrokeWidth`. Gemeten (disassembly van onze libglut,
functies op `0xe500` en `0xe5d0`): die twee rekenen met `flds/fadds/fldz` en
laten het resultaat in `st0` staan, dus het zijn de GLfloat-varianten van GLUT
3.8 — precies waar de f-varianten voor staan.

**6 uitbreidingen** in `rg_ext.c`: `glutAppStatusFunc`, `glutInitContextFunc`,
`glutPositionFunc`, `glutSetVertexAttribCoord3`, `glutSetVertexAttribNormal`,
`glutSetVertexAttribTexCoord2`. Zie "Wat open blijft".

## Verifiëren

```sh
bash tools/rg-glutapi/verify-exports.sh   # 0 ontbrekend, 0 te veel
python3 tools/rg-glutapi/verify-vorm.py   # 122 stubs, 39 eigen functies
bash tools/rg-glutapi/verify-load.sh      # drie laadproeven in 32-bits processen
```

`verify-load.sh` doet: (1) `dlopen` + `dlsym` van alle 161 namen met de echte
libglut als doel, plus één echte doorverwijzing (`glutInitWindowSize`); (2)
`LD_PRELOAD` in een 32-bits proces en het eigen `/proc/self/maps` als bewijs;
(3) een doorverwijzing naar een **neplibglut** met een waarneembaar
neveneffect, zodat de argumenten aantoonbaar ongeschonden aankomen; (4)
`proef_slots`, dat de 19-byte vorm van elke sprongstub uitleest, de slotwijzer
opzoekt en met `dladdr` laat zien dat alle 122 in de echte libglut liggen en
geen enkele in deze shim (recursie).
Er wordt geen spel en geen Wine gestart.

## Wat open blijft

1. **De 31 `*Ucall`-wikkels volgen upstream freeglut 3.4.0-semantiek**
   (trampoline met `user_data`) in plaats van de basisnaam kaal door te
   verwijzen. Reden: de basis-signatuur heeft geen `user_data`, dus een rechte
   doorverwijzing zou op i386 een verzonnen `void *` aan de callback geven. De
   wikkel roept wel de basisnaam aan, zoals bedoeld.
2. **`glutTimerFuncUcall` houdt één `user_data`** voor alle timers (de laatste
   wint). Upstream bewaart hem per timer, maar die lijst zit in de oude libglut
   en is van buiten niet te bereiken.
3. **`glutPositionFunc`, `glutAppStatusFunc`, `glutInitContextFunc`,
   `glutSetVertexAttrib*` zijn inert**: onze libglut is een oudere freeglut
   zonder die functionaliteit en zonder haak om erin te komen. De callbacks en
   attribuut-indexen worden bewaard (zie `rg_ext_status`), maar vuren nooit. De
   bijbehorende `*Ucall`-varianten installeren hun trampoline dus ook in het
   niets.
4. **`NULL` als callback** wordt aan de basisnaam doorgegeven (dus "wissen"),
   niet als trampoline geïnstalleerd.
5. **i386 cdecl aangenomen.** De stubs zijn verder signatuur- en
   conventie-onafhankelijk, maar clobberen `eax` en `ecx`; een `regparm`-
   aanroepconventie zou niet werken. Freeglut op i386 gebruikt cdecl.
6. **Niet in een spel getest** (geen spel, geen Wine). Ook de integratie met
   `tools/rg-elfloader/` is niet getest: die lader is van een andere agent en
   speelt mogelijk geen `DT_INIT_ARRAY` af — daarom bestaat het noodpad (punt
   hierboven) en daarom is de 32-bits bouw met `-m32` identiek aan die lader.
7. **De dongle-/HASP-/keychip-namen komen in deze oppervlakte niet voor**; er is
   niets aan integriteit of sleutels geraakt.
