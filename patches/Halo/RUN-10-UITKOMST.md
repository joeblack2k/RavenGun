# RUN 10 — de camera bestaat WEL, en de tafel verandert niet door onze TEST

*RavenGun, 2026-10-13. Run 10 liep ongeveer 11:21:4x - 11:24:1x (150 s, beeld op de tv).*

> **LET OP — DEZE RUN IS NIET SCHOON.** Er liep een TWEEDE run naast: de Transformers-werker begon om
> **11:22:02** aan een Wine-run van ongeveer 10 minuten. Mijn TEST-reeks viel om **11:22:2x - 11:23:2x**,
> dus precies daarbinnen. Alles wat tijdgevoelig is (de TEST-herhaling) is daarmee **niet schoon**. De
> conclusie "TEST slaat de kalibratie niet over" rust daarom op run 7 en run 8, die WEL schoon waren —
> run 10 voegt alleen de camera-inventaris en de tafelmeting toe, en die zijn daar niet van afhankelijk.

## A. DE CAMERAKANT: er IS een camera, en de game ziet geen geweer

Buiten de run om, op de machine:

```
/dev/video0   naam = gspca main driver
v4l2-ctl: USB Camera-B4.09.24.1 (usb-0000:0f:00.0-1) -> /dev/video0
```

**Er hangt dus WEL een camera aan deze machine** (`gspca`, "USB Camera-B4.09.24.1"). Tijdens het
kalibratiescherm houdt de game **geen camera open** (0 camera-fds), en zijn eigen woorden zijn:

```
Webcam: Could not ID device for player 0 (0, 0, 0, 0)
Webcam: Aborting auto-id. All webcams reported brightness difference of 0.
```

Dat is de scherpste aanwijzing die we hebben: de game **heeft** een camera, meet **helderheidsverschil**
in het beeld — dat is het zoeken naar een lichtpunt — en vindt **niets** (verschil 0). Hij probeert dus
een **geweer met een lichtpunt in het camerabeeld** te vinden, en dat is er niet: wij bieden een
evdev-apparaat aan (muisklasse, `ABS_X`/`ABS_Y` + `BTN_LEFT`) dat voor een camera onzichtbaar is.

**Wat dat betekent:** de aim van deze game is (zeer waarschijnlijk) **optisch** — het geweer moet door
de camera GEZIEN worden — en de trekker komt van de kastkaart. Onze virtuele apparaten kunnen dat
principe niet vervangen. Dat is een **hardwarevraag voor de eigenaar** en geen fout van ons, en het
antwoord op "wat ontbreekt er" is dan: **een geweer dat de camera kan zien** (of de kast met zijn
getetherde geweer), niet een extra virtueel apparaat.

## B. DE TAFEL: onze TEST laat alleen zijn eigen tellers lopen

De hele tafel van bord 0, van buiten het proces gelezen (k = schakelaar, 8 woorden per ingang):

voor de reeks:
```
k=0    00000001 00000000 00000000 00000000 00000000 00000000 00000001 00000001
k=1..12  allemaal nul
```

na acht TEST-pulsen van 3 s:
```
k=0    00000001 00000000 00000000 00000000 00000000 00000000 00000001 00000001
k=4    00000001 00000000 00000000 00000000 00000000 00000000 00000020 00000020
k=1..3, k=5..12  allemaal nul
```

Dus: **alleen `k=4` (diag/test) verandert**, en alleen in zijn eigen tellers `[6]` en `[7]` (32 = 8
pulsen × 4 tabelregels). De game schrijft **niets terug** — geen toestandsveld, geen andere ingang — en
`[3]`/`[5]` (indruk/vastgehouden) staan weer op 0 omdat de puls voorbij is. Er is in de tafel dus
**geen spoor van een reactie van de game** op onze TEST.

## C. Wat de game schreef

Het logboek ging van 214 naar 230 regels, maar alle nieuwe regels zijn de gewone opstartreeks
(shaders, FMOD, webcam, netwerk) met tijdstempels **11:21:52 - 11:22:04**; **na 11:22:04 kwam er geen
enkele regel meer bij**, ook niet tijdens de acht TEST-pulsen. Het scherm bleef dus staan (foto
`run10-01-kalibratie` en `run10-02-na-testreeks`).

## D. Wat deze run oplevert, en wat niet

**Wel:** de camera-inventaris (er IS een camera; de game ziet er geen geweer in), en de tafelmeting die
laat zien dat onze TEST alleen zijn eigen tellers raakt.
**Niet:** geen schone TEST-meting (de tweede run liep mee), geen attract- of spelmodus (de lat is niet
gehaald), niets over het FTDI-protocol, en geen BEWIJS dat de aim optisch is — dat blijft een sterke
afleiding uit de webcamregels, het zoeken naar helderheidsverschil en het feit dat de game geen enkel
invoerapparaat opent.

## E. Voor de volgende keer: de bench-controle moet ook de Wine-kant zien

Mijn voorcontrole keek naar de native kant (`gamescope`, `play-native`, `/pm/g7/halo/game`). De andere
partijen draaien de **Wine-route**: `wine`, `wineserver` en `OpenParrotLoader.exe`. Die horen erbij:
**als een van die drie draait, is de bench bezet en wacht ik.** Dat is nu de regel, zodat we elkaar niet
meer kruisen.
