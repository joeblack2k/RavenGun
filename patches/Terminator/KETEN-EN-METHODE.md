# De keten van Terminator Salvation — en acht instrumentfouten

**Versie van dit dossier:** 2026-10-13, einde van de dag. Game: `game` (sha256
`798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a`, 7.764.284 B, ELF32 i386,
niet-PIE, `e_entry 0x08057240`). Lader: onze eigen `rg-elfloader` (`7f83edb2…`).

**Deze twee lijsten horen bovenaan**, want ze zijn het nuttigste dat deze titel heeft opgeleverd: de
**volgorde met namen** (die bestaat nergens anders), en het **register van instrumentfouten** (dat is
het enige dat overdraagbaar is naar een andere titel).

---

## 1. De conclusie in één alinea (GEMETEN, met de code eronder)

**Deze titel is niet stuk en mist geen data. Alle negenenveertig initialisatiestappen slagen, de negen
telemetrie-sub-stappen slagen, de twee telemetriebestanden worden gelezen en hun structuren worden
opgebouwd — en dan vraagt de gast naar een HULPBRON die niemand in onze omgeving aanmaakt, hoort
"nee", en loopt netjes weg met `exit 0` in plaats van te tekenen.**

De keten is **niet** afbrekend: elke stap meldt zijn eigen fout en **gaat verder** (failback). Daarom
crasht deze gast nooit en heeft hij altijd een schone exit — en daarom heeft elke fout in deze titel
een **naam**.

**Wat er dan gebeurt, in volgorde:**
1. de keten loopt, `DongleInitialize()` is **stap 1** (en het is de parkeerreden in het spelprofiel);
2. de telemetrie-init `0x810c9e0` doet **negen** sub-stappen, alle negen geslaagd;
3. hij leest **`telemLUT.xml`** en **`telemFields.xml`** — beide lezingen geven 0 terug, dus **geslaagd**;
4. hij zet de XML-structuur op (`0x81c79e0`);
5. hij vraagt naar de projector-hulpbron: `0x80aa4a0` (**de tweetallige mutex-tabel op `0x87c3460`**) +
   `0x81c6880(object, 4, &uitvoer)`, en publiceert het antwoord in **`0x88ccb88`**;
6. dat antwoord is niet-1 of de aanroep mislukt → de schakelaar staat **niet-nul**;
7. bij het tekenen leest `0x80c2a49` die schakelaar en springt met `jne 0x80c319d` **langs**
   `glutDisplayFunc` (`0x80c2a5e`) en `glutMainLoop` (`0x80c2a63`) — dus **geen venster, geen beeld**;
8. de gast neemt de projectorweg (strings: `"projectorsettings"`, `"Looking for a Mitsubishi RS232
   projector..."`, `"Could not init projector!"`) en eindigt met **exit 0**.

**De enige ontbrekende schakel: de MAKER van die hulpbron.** `0x810c9e0` publiceert alleen; de maker is
de functie die de kast had en onze omgeving niet.

---

## 2. De keten van negenenveertig stappen, met namen en adressen

Elke stap is `call <functie> ; test %eax,%eax ; js <eigen foutdoel>`; het foutdoel print zijn melding,
zet de vlag `-0x14(%ebp) = 1` en **springt terug naar de VOLGENDE stap**. De keten breekt dus nooit af.

De ketenfunctie: **`0x80c22d0`**. De eerste handeling is de supervisor:

| # | stap (aanroep) | foutdoel | melding |
|---|---|---|---|
| 0 | `call 0x82f2eda` (supervisor) | `0x80c32e5` | — |
| 1 | `0x80c2331` | `0x80c2cdb` | `DongleInitialize() FAILED!` |
| 2 | `0x80c233e` | `0x80c2cc3` | `localizationInit() FAILED!` |
| 3 | `0x80c234b` | `0x80c2cab` | `SysInit() FAILED!` |
| 4 | `0x80c235b` | `0x80c2c93` | `IntInit() FAILED!` |
| 5 | `0x80c236b` | `0x80c2c7b` | `MemInit() FAILED!` |
| 6 | `0x80c237b` | `0x80c2c63` | `ProcInit() FAILED!` |
| 7 | `0x80c238b` | `0x80c2c4b` | `FbInit() FAILED!` |
| 8 | `0x80c239b` | `0x80c2c33` | `WndInit() FAILED!` |
| 9 | `0x80c23ab` | `0x80c2c1b` | `InpInit() FAILED!` |
| 10 | `0x80c23bb` | `0x80c2c03` | `GunInit() FAILED!` |
| 11 | `0x80c23cb` | `0x80c2beb` | `CabInit() FAILED!` |
| 12 | `0x80c23db` | `0x80c2bd3` | `VidInit() FAILED!` |
| 13 | `0x80c23eb` | `0x80c2bbb` | `DrawInit() FAILED!` |
| 14 | `0x80c23fb` | `0x80c2ba3` | `FsInit() FAILED!` |
| 15 | `0x80c240b` | `0x80c2b8b` | `NetInit() FAILED!` |
| 16 | `0x80c241b` | `0x80c2b73` | `SndInit() FAILED!` |
| 17 | `0x80c242b` | `0x80c2b5b` | … (de reeks loopt door met eigen doelen 0x18 uit elkaar) |
| 18 | `0x80c243b` | `0x80c2b43` | … |
| 19 | `0x80c244b` | `0x80c2b2b` | … |
| 20 | `0x80c245b` | `0x80c2b13` | … |
| 21 | `0x80c246b` | `0x80c2afb` | … |
| 22 | `0x80c247b` | `0x80c2ae3` | … |
| 23 | `0x80c248b` | `0x80c2acb` | … |
| 24 | `0x80c249b` | `0x80c2ab3` | `0x8502bcd`-groep |
| 25 | `0x80c24a6` → `80c1e50` | `0x80c2a9b` | … |
| 26 | `0x80c24b6` → **`810c9e0`** | `0x80c2a83` | de **telemetrie-init** (zie §3) |
| 27+ | de reeks loopt door tot `0x80c2a43`; de doelen liggen in drie trappen: `0x80c2a83`–`0x80c2cdb`, `0x80c2e2e`–`0x80c2e65`, en `0x80c3290`–`0x80c3355` | | |

**Alle meldingen liggen in `.rodata` op `0x84feb00`–`0x84ff…`** (de `c2`-trap), dus alle namen zijn in
één keer leesbaar met `objdump -s --start-address=0x84feb00 --stop-address=0x84fec80`.

**Waarom dit de moeite is:** de volgorde *en* de namen staan alleen hier bij elkaar. Wie deze titel
opnieuw oppakt, weet met deze tabel **welke subsysteem waar in de reeks zit** en **welke melding bij
welke stap hoort** — zonder de keten opnieuw te hoeven uitzoeken.

---

## 3. De negen sub-stappen van de telemetrie-init (`0x810c9e0`)

Zelfde patroon, één niveau dieper: `call ; test %eax,%eax ; js <eigen doel>`, en het doel print zijn
melding en **probeert daarna de volgende stap alsnog**.

| # | aanroep | foutdoel | melding (adres in `.rodata`) |
|---|---|---|---|
| 1 | `0x810e690` | `0x810cdb6` | `GameLoadCnnf() failed!` (0x8502aba) |
| 2 | `0x81f0704` | `0x810cdd4` | `CardInit() failed!` (0x8502ad1) |
| 3 | `0x811a5b0` | `0x810cd20` | `GConInit() failed!` (0x8502ae4) |
| 4 | `0x80f2ca0` | `0x810cd40` | `AudInit() failed!` (0x8502af7) |
| 5 | `0x813c3f0` | `0x810cd60` | `SelInit() failed!` (0x8502b09) |
| 6 | `0x80ef460` | `0x810cd80` | `AtInit() failed!` (0x8502b1b) — **16-bits `test %ax,%ax`** |
| 7 | `0x81b5e40` | `0x810cda0` | `PrtsInfoInit() failed!` (0x8502b2c) — de **poorten** |
| 8 | `0x8165400` | `0x810cd05` | `RBInit() failed!` (0x8502b43) |
| 9 | `0x817eaa0` | `0x810ce01` | `GameSceneInit() failed!` (0x8502b54) — de **scène** |

**Alle negen zijn geslaagd** — bewezen doordat geen enkele melding in `B.out` staat, op een kanaal dat
wij hebben geverifieerd (zie §4, punt 5 en de puts-analyse hieronder).

**En daarna leest hij twee bestanden:**
```
810cb8f:  sprintf(buf1, "%s/%s", 0x8502b6c, 0x8502b7d)   ; basis + "telemLUT.xml"
810cba7:  sprintf(buf2, "%s/%s", 0x8502b6c, 0x8502b8c)   ; basis + "telemFields.xml"
810cbdd:  movl $0x8502b7f,(%esp) ; call 80ac6b0 ; test ; jne 810cce0   ; LEZING 1
810cc0f:  movl $0x8502b8e,(%esp) ; call 80ac6b0 ; test ; jne 810ccf1   ; LEZING 2
```
**Beide lezingen geven 0 terug, dus BEIDE ZIJN GESLAAGD.** De bestanden bestaan:
`<spelmap>/T4Data/exml/telemLUT.xml` en `…/telemFields.xml`.

---

## 4. DE ACHT INSTRUMENTFOUTEN — één regel elk, één les

**De les, één keer:** *een instrument dat iets niet kan zien, kan de AFWEZIGHEID ervan niet melden.*
Acht keer is vandaag een leeg antwoord als resultaat doorgegaan. Wie hierna aan deze titel werkt: lees
deze lijst **voor** je iets concludeert.

| # | de fout | hoe het eruitzag | de les in één regel |
|---|---|---|---|
| 1 | **De teller die een kind van 0,4 ms niet kón zien** | "0 kinderen gezien zonder onze waarnemers" → bijna de conclusie "de lus is van onze instrumentatie" | **IJk je teller op een bekende uitkomst** voor je zijn stilte gelooft. |
| 2 | **De frames die een klok in de hoek waren** | "de reeks breekt → het spel tekent" — het was de taakbalkklok, 159 bytes verschil | **Een frame moet je BEKIJKEN**; grootte en hash zijn alleen een signaal om te kijken. |
| 3 | **De voorgrond die van iemand anders was** | "het spel tekende voor het eerst" — maar `steam_proton` had de voorgrond vanaf 13:44 | **Controleer wie de hulpbron bezit** voor je een resultaat aan je eigen run toeschrijft. |
| 4 | **De wikkel die niet kon zeggen of hij geladen was** | leeg logboek = "niet geladen" OF "nooit aangeroepen" | **Laat je instrument bij het laden een regel schrijven** (het laadbewijs). |
| 5 | **De bestandslogger die op `fopen` lette, terwijl de lezing via `0x80ac6b0` gaat** | "de telemetrie wordt nooit gelezen" — hij werd WEL gelezen | **Een instrument bewaakt één deur en zegt niets over de gang**; weet WELKE weg je meet. |
| 6 | **`gcc … \| head -4`** | "FOUT: niet gebouwd" — de compiler stierf aan SIGPIPE op zijn eigen waarschuwing | **Nooit compileruitvoer door `head`**; naar een bestand, en dan kijken. |
| 7 | **De run-regel zonder `"${BW[@]}"`** | "de koppelingen falen onder de display-route" — de run kwam nooit in de sandbox | **Bewijs IN de run dat hij in de sandbox zit** (`ns-mnt`, `/g3`, een knoop die opent). |
| 8 | **Een misuitgelijnde disassemblage** | "`jg 810cc0d` skipt de lezing" — die sprong bestond niet; ik begon midden in een instructie | **Begin bij een instructiegrens of functiekop**; disassemblage ziet eruit als grondwaarheid. |

**Vijf eigen conclusies zijn daardoor ingetrokken** (de lus als artefact, het beeld als het onze, de
koppelingen als regressie van een ander, de instellingsroute als dood, de lezing als overgeslagen) —
**en elke intrekking maakte de titel kleiner in plaats van groter.**

---

## 5. Wat de volgende lezing is (afgebakend, geen run nodig)

**De maker van de hulpbron.** De tweetallige tabel op **`0x87c3460`** (twee sleuven van 24 bytes, elk
met een `pthread_mutex`) wordt op **ongeveer vijftien plaatsen** in het bereik **`0x80abxxx`** aangeraakt:
`0x80aa4e9`, `0x80aaecd`, `0x80ab12d`, `0x80abc63`, `0x80abef0`, `0x80abf92`, `0x80abfce`,
`0x80ac05d`, `0x80ac1ae`, `0x80ac1e5`, `0x80ac462`, `0x80ac568`, `0x80ac7ed`, `0x80ac934`, `0x80ac99e`.

**De vraag is welke van die plekken een sleuf MAAKT** (een mutex initialiseert, veld 0 op 1 of 2 zet) in
plaats van hem te **testen**. De tester kennen we: `0x82f2984` is een `pthread_mutex_trylock`-wikkel die
`EBUSY → 6` en `EINVAL → 4` vertaalt en `0` teruggeeft bij succes. **Wie `pthread_mutex_init` aanroept op
een adres in die tabel, is de maker** — en dat is de functie die de kast had en onze omgeving niet.

**Verwante feiten om niet opnieuw te zoeken:**
* `0x80aa4a0` is **geen opslag** maar een **twee-sleuven-kiezer**: `seed & 1` kiest sleuf 0 of 1, en het
  zaad `0x87a3fd0` wordt met `time(NULL)` opgehoogd (dus deels tijdafhankelijk).
* `0x81c6880(object, index, &uitvoer)` toetst veld 0 tegen 1..2 en bouwt een vaste vier-byte sleutel;
  **geen enkele PLT-aanroep** in dat venster.
* De schakelaar **`0x88ccb88`** heeft **twee** schrijvers in het hele bestand: `0x80c253e` (in de
  ketenfunctie, uit de retour van `0x810a3b0`) en `0x810cc94` (in de telemetrie-init, dezelfde predicaat
  geïnlined). Gelezen bij `0x80c2a49`, met `jne 0x80c319d` langs `glutDisplayFunc`/`glutMainLoop`.
* `telemLUT.xml` en `telemFields.xml` **bestaan** in de dump; ze zijn gelezen en het is **geen**
  onvolledige titeldata.

---

## 6. Wat de gast WEL en NIET doet (samengevat, alles GEMETEN)

| | |
|---|---|
| **WEL** | alles laden (13 bibliotheken), negenenveertig stappen doorlopen, negen telemetrie-sub-stappen, beide XML's lezen, de scène opzetten, de projectorwegen aanraken (`/dev/ttyS0` en `/dev/sierra2` openen met `0x902 = O_RDWR\|O_NONBLOCK`), netjes eindigen met 0 |
| **NIET** | **geen enkele GLUT-aanroep** — `glutInit` niet, `glutCreateWindow` niet, `glutEnterGameMode` niet, `glutMainLoop` niet, `glutSwapBuffers` niet (zes nullen, mét laadbewijs, **binnen de sandbox**); **geen enkel venster** (geen X11-venster, geen `windowAdded` op de Wayland-kant); **geen enkele byte naar de seriële poort** (geen `ioctl`, geen `write`, geen `read`) |
| **dus** | hij **faalt** niet, hij **vraagt** iets en krijgt "nee" |

**En de projectorwegen leiden niet naar het beeld:** met een werkende synthetische knoop op
`/dev/ttyS0` opent hij de poort (fd=5) en doet er **niets** mee — geen sonde, geen antwoord nodig.
De poort is een **aanwezigheidstoets**, geen schakelaar.
