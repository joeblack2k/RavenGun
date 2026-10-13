# Het PE-hookmechanisme voor Luigi's Mansion — wat er ligt, en wanneer het omzet

*RavenGun, ronde "loader-specialist Halo", 2026-10-13. Bench-vrij gebouwd en bewezen: er is geen spel
gestart, de NAS is alleen gelezen, de launcher en `packaging/native-games.json` zijn niet aangeraakt,
en het dossier van de Luigi-werker is alleen gelezen. Geen commit.*

**De am-laag is de leidende hypothese, niet een uitgesloten route.** De dichte bemonstering van ONZE
kern laat zien dat het spel zichzelf **volledig ontpakt**: `.text` gaat van 100 procent nul op t=4,5 s
naar 11,20 procent nul op t=4,7 s, en de prologue is op dat moment `40 53 48 83 ec` — de **originele**
prologue, dus uitgepakt maar **niet** omgezet. Het proces sterft op t=5,96 s, ongeveer **1,3 s na een
geslaagd uitpakken**. De gesloten kern ontpakt veel later (t=9 tot 10 s) en zet de prologues op
hetzelfde moment om als waarop het uitpakken klaar is. Het enige gemeten verschil in het spelbeeld op
het moment dat het uitgepakt is, is dus dat die 17 prologues origineel zijn in plaats van omgezet.

*(Een eerdere versie van dit document zei dat de am-laag "nooit bereikt" werd en dat het proces op
5,72 s stierf. Dat kwam van de eerste controlevlucht van de analist, die alleen t=0,1 tot 4,0 s
bemonsterde — dus vóór het uitpakken. Die bewering is hiermee vervangen.)*

---

## 1. Wat er ligt

| bestand | wat het is |
|---|---|
| `tools/rt-hasphook/rg_pe_hook.c` + `.h` | de **generieke engine**: PE-kop uit het beeld, keuring per adres, stubgeneratie, prologue-omzetting, en de wachtroute. Geen enkele Luigi-naam of Luigi-adres erin. |
| `tools/rt-hasphook/rg_pe_hook_dll.c` | de Windows-kant: een DLL die in het spelproces het beeld van het hoofdprogramma pakt en de engine erop loslaat. |
| `patches/LuigisMansion/dropin/dongle-haken.ini` | de **gegevens**: de 17 adressen met hun gemeten antwoord, plus de wachtroute. |
| `patches/LuigisMansion/dropin/rg-pe-hook.dll` | de gebouwde DLL (127.485 B, sha256 `efa40444846e2bee`, pei-x86-64, met `.reloc`). |
| `patches/LuigisMansion/dropin/dropin.ini` | de drop-in: `To`, **`Preload = true`**, `Audit = false`, en de omgevingsvariabelen. |
| `patches/Halo/probe/{rg_pe_hook.c,rg_pe_hook.h,hook-pe-toets.c,pe-mechanisme-toets.sh,wacht-toets.sh}` | de Linux-toetsen op een echt PE-geheugenbeeld (het bewijs in §3 en §4). |

De DLL is gebouwd met de mingw-image van het project (`podman run … localhost/ravengun-mingw`),
hetzelfde recept als `scripts/build-hasp.sh` voor de bestaande HASP-Windows-DLL.

## 2. Hoe het mechanisme de prologues omzet

1. **Het beeld.** De DLL pakt `GetModuleHandleW(NULL)` — het **spel**, niet zichzelf — en leest de
   PE-kop uit dat geheugen (`offset == RVA`). De Linux-toets doet hetzelfde op een dump.
2. **De lijst.** Lijstbrede sleutels (`beeld`, `basis`, `proloog`, `bewaar_args`, `wacht_s`,
   `wacht_ms`, `grootte`, `ingang`) en per sectie `rva`, `antwoord`, `bytes`, `notitie`.
3. **De keuring, per adres.** De RVA moet in een **uitvoerbare** sectie vallen, binnen het beeld, en
   de eerste bytes moeten de verwachte prologue zijn. `grootte` (SizeOfImage) en `ingang`
   (AddressOfEntryPoint) zijn versiecontroles: kloppen ze niet, dan wordt de hele lijst afgekeurd.
   Staat er al `e9 …`, dan weigert de hook met *"al door een andere kern omgezet?"* in plaats van een
   keten van twee vervangingen te bouwen.
4. **De stubs.** In één uitvoerbare pagina genereert de engine per regel een stub met het antwoord
   erin **gebakken**. Met `bewaar_args = 1` zet elke stub eerst de argumentregisters in de home space
   van de aanroeper (`[rsp+8]` … `[rsp+0x20]`), precies zoals élke kern-implementatie van Luigi doet.
5. **De omzetting.** De 5-byte prologue wordt `e9 rel32` naar de stub; de pagina wordt eerst dicht bij
   het beeld gezocht zodat 5 bytes genoeg zijn, anders volgt een 12-byte absolute sprong.

## 3. WANNEER de hook omzet — en waarom dat het hele punt is

De prologues **bestaan pas** nadat de pakker de code heeft weggeschreven (t=4,7 s). Een hook die bij
het **laden** van de DLL omzet, zet dus **niets** om: de pagina staat dan nog op nul. Daarom heeft de
engine een **wachtroute**: `wacht_s` laat hem kijken (elke `wacht_ms`) tot de prologue er staat, en
dan omzetten. Een pagina die nog nul is wordt niet als fout gemeld maar als *"NOG NIET UITGEPAKT"*.

Bewezen met `patches/Halo/probe/wacht-toets.sh` op het echte beeld, met de 17 prologues op nul gezet en
een gesimuleerde ontpakker die ze na 600 ms wegschrijft:

```
1b. de ontpakker start (schrijft de prologues na 600 ms)
[rg-pe-hook] wachtroute: 17 pagina('s) nog niet uitgepakt; ik blijf kijken (max 5 s, elke 20 ms)
   [ontpakker] 17 prologues geschreven na 600 ms
[rg-pe-hook] wachtroute: de pagina's zijn uitgepakt na 602 ms; nu omzetten
[rg-pe-hook] totaal aangehaakt: 17 van 17; afgekeurd: 0
4. GESLAAGD: 17 van 17 aangehaakt (verwacht 17)
```

En de **controle** met `wacht_s = 0` (dus omzetten op het moment van laden) laat precies zien waarom
dat niet werkt:

```
amDongleGetGameId   0x002b3d30  NOG NIET UITGEPAKT: de prologue op RVA 0x2b3d30 is nog nul
totaal aangehaakt: 0 van 17
```

**Is dat vroeg genoeg?** Ja, met ruime marge: de hook zet om **2 ms** nadat de pagina's gevuld zijn, en
het gemeten venster tussen het einde van het uitpakken (t=4,7 s) en de dood (t=5,96 s) is **1,3 s**.
Zelfs met een poll-interval van 20 ms blijft er meer dan een seconde over.

**De tweede, robuustere plek** is het aftappen van `ntdll!NtProtectVirtualMemory`: de pakker maakt zijn
pagina's daar schrijfbaar en daarna uitvoerbaar, dus dat is het eerstvolgende moment waarop omzetten
kan — deterministisch in plaats van pollend. Dat is nog **niet gebouwd**; zie §5.

## 4. Het bewijs van het omzetten zelf

`bash patches/Halo/probe/pe-mechanisme-toets.sh` op `image-gesloten.bin` (19.259.392 B, sha256
`11bb710b7c71686b`; offset == RVA bij ImageBase `0x140000000`, SizeOfImage `0x125e000`, entry
`0xf23125`):

* **P1 — positief op de 17 ECHTE adressen** (met synthetische prologues, omdat de originele door de
  gesloten kern overschreven zijn): `17 van 17 aangehaakt`, elk met `GELDIG: sectie .text …, prologue
  55 48 89 e5`. De stubpagina komt op **0x13fff0000** — precies waar de gesloten kern zijn stubs legt.
* **P2 — weigering op de ONGEWIJZIGDE dump**: alle 17 `AFGEWEZEN: eerste bytes e9 … zijn geen prologue
  maar een sprong -- al door een andere kern omgezet?`, 0 van 17. Dat is de bescherming tegen een
  ketting van twee vervangingen.
* **P3 — ongeldige adressen**: `.rdata` → *"sectie .rdata is niet uitvoerbaar"*; buiten het beeld →
  *"valt in geen sectie"*; verkeerde `grootte` → hele lijst afgekeurd.
* **P4/P5 — de stubs DRAAIEN**: `amDongleInit` geeft 0 terug, en het enige gemeten kastantwoord komt
  er ook uit: `buffer na de aanroep: c0 a8 01 00 (verwacht c0 a8 01 00 = 192.168.1.0)`, met
  teruggeefwaarde 0.
* **P6 — de TODO-vorm** bestaat nog voor een lijst waarin iets nog niet gemeten is.

## 5. De API-route: ontwerp, en wat er nog niet is

De gesloten kern haakt `ntdll!NtProtectVirtualMemory` en `ZwProtectVirtualMemory` aan, onze kern niet
(gemeten). Het ontwerp dat daaruit volgt:

1. de aanroep **gewoon doorlaten** — het gedrag verandert niet;
2. kijken of de aanroep de `.text`-pagina's van het spel raakt (`0x140001000` en verder) en of het om
   **uitvoerbaar maken** gaat;
3. **op dat moment** de 17 prologues omzetten: de pagina schrijfbaar maken, de 5 bytes `e9 <rel32>`
   schrijven, en de oorspronkelijke bescherming terugzetten.

Dat vraagt **omhullen** in plaats van vervangen: de originele bytes bewaren, een trampoline bouwen, de
echte functie aanroepen, en het resultaat teruggeven — precies wat mijn ELF-hook al doet
(`hh_maak_trampoline` + `hh_koppel_echt`), en wat de PE-engine bewust nog niet heeft. De **bouwstenen
staan klaar in de kop** (`export =`, `module =`, `RG_PROLOOG_API`, `RG_ANTWOORD_OMHUL`, de
omhul-administratie en `rg_pe_herstel()`), maar de **uitvoering ervan is niet gebouwd en niet getest**.
Het is dus een tweede mechanisme, geen vinkje.

**Wat er nog uitgezocht moet worden:** of die protect-aanroep inderdaad in het venster t=4,6 tot 5,9 s
langskomt. De analist doet daarvoor de `WINEDEBUG=+virtual`-meting op beide kernen. Zonder die
bevestiging is §5 een ontwerp op papier — en de wachtroute van §3 werkt in de tussentijd zonder die
bevestiging, want die kijkt naar de bytes zelf.

## 6. De antwoorden, precies zoals gemeten

Uit `AM-LAAG.md` §4/§5: **alle 17 geven 0 terug**, en de enige echte waarde is

```
amDongleGetNetworkAddress  ->  *(uint32_t*)rcx = 0x1a8c0 = c0 a8 01 00 = 192.168.1.0
```

Er is **geen** game-id, **geen** modeltype en **geen** keychip-id. De lijst doet daarom exact dat:
16 × `ret0`, 1 × `buf=rcx:c0 a8 01 00`, met `bewaar_args = 1`. Er is niets verzonnen en er viel niets
te verzinnen.

De `e9`-bytes uit `AM-LAAG.md` §1 staan **niet** als `bytes =` in de lijst: dat is de toestand die de
gesloten kern achterlaat, en ze daar verwachten zou de keuring omkeren. De **originele** 5 bytes zijn nu
wél meetbaar (het spel ontpakt immers) — `40 53 48 83 ec` voor `amDongleInit` — en zodra ze per functie
gemeten zijn kan elke regel een `bytes = …` krijgen en weigert de hook een verkeerde spelversie.

## 7. De drop-in, en het kanaal

`patches/LuigisMansion/dropin/dropin.ini`:

```ini
[rg-pe-hook.dll]
To = .
Preload = true
Audit = false
Env = RG_HOOK_INI=dongle-haken.ini;RG_HOOK_LOG=rg-pe-hook.log

[dongle-haken.ini]
To = .
```

* **`Preload = true`** is het kanaal: de launcher zet het pad in `RG_DROPIN_PRELOAD` en de lader heeft
  daarvoor de `TP_PRELOAD_DLL`-route. Zonder deze regel rolt het bestand wel uit naar de view maar
  wordt het nooit geladen — en dan meet je niets.
* **`Audit = false`** omdat dit een Windows-PE-DLL is en geen ELF: in `LD_AUDIT` weigert glibc hem met
  *"wrong ELF class"* en dat is alleen ruis. Het patroon is overgenomen van de inline-hook van Halo en
  JurassicPark (`docs/dropins.md`, "Audit = false").
* **Gecontroleerd met de planning** (`python3 scripts/launch-direct.py LuigisMansion --plan`): de twee
  bestanden rollen uit naar `…/views/LuigisMansion/exe/x64/`, en bij `preload` staat
  `…/exe/x64/rg-pe-hook.dll`, met `RG_HOOK_INI=dongle-haken.ini` in `env` en `errors: []`.

## 8. Wat er niet is

* Geen benchrun: de DLL is niet onder Wine geladen. Bewezen is het mechanisme op een echt PE-beeld
  (omzetten, weigeren, wachtroute, het gemeten antwoord); niet bewezen is of de DLL in het proces komt
  en of de game daarna verder komt.
* Geen API-omhulling (§5): de bouwstenen staan in de kop, de uitvoering niet.
* Geen wijziging aan de launcher, aan `packaging/native-games.json`, of aan het dossier van de
  Luigi-werker.
