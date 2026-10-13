# Het PE-hookmechanisme voor Luigi's Mansion — stand, bewijs, en wat er nog moet

*RavenGun, ronde "loader-specialist Halo", 2026-10-13. Bench-vrij gebouwd en bewezen: er is geen spel
gestart, de NAS is alleen gelezen, de launcher en `packaging/native-games.json` zijn niet aangeraakt,
en het dossier van de Luigi-werker is alleen gelezen. Geen commit.*

**De am-laag is de leidende hypothese.** De dichte bemonstering van ONZE kern: `.text` gaat van 100
procent nul op t=4,5 s naar 11,20 procent nul op t=4,7 s — het spel ontpakt zichzelf volledig — de
prologue is dan origineel (`40 53 48 83 ec`) en het proces sterft op t=5,96 s, ongeveer 1,3 s later.
De gesloten kern ontpakt pas op t=9 tot 10 s en zet op datzelfde moment om. Het enige gemeten verschil
in het spelbeeld op het moment dat het uitgepakt is, is dat die 17 prologues origineel zijn.

---

## 1. Wat er ligt

| bestand | wat het is |
|---|---|
| `tools/rt-hasphook/rg_pe_hook.c` + `.h` | de generieke engine: PE-kop uit het beeld, keuring per adres, stubgeneratie, prologue-omzetting, en de wachtroute. Geen Luigi-naam of Luigi-adres erin. |
| `tools/rt-hasphook/rg_pe_hook_dll.c` | de Windows-kant: een DLL die in het spelproces het beeld van het hoofdprogramma pakt. |
| `patches/LuigisMansion/dropin/dongle-haken.ini` | de gegevens: 17 adressen, hun gemeten antwoord, de wachtroute. |
| `patches/LuigisMansion/dropin/rg-pe-hook.dll` | de gebouwde DLL (127.485 B, sha256 `efa40444846e2bee`, pei-x86-64, `.reloc`). |
| `patches/LuigisMansion/dropin/dropin.ini` | de drop-in: `To`, `Preload`, `Audit`, en de absolute paden. |
| `patches/Halo/probe/{pe-mechanisme-toets.sh,wacht-toets.sh,hook-pe-toets.c}` | de Linux-toetsen op een echt PE-geheugenbeeld. |

## 2. De drop-in komt nu AAN — en waarom dat eerst niet ging

De vijfminproef gaf `drop-in in proces 0/2` en in de view ontbraken beide bestanden, met de melding
*"de map van het doel … komt na het materialiseren nog buiten de view uit"*.

**De oorzaak, uit de code van de launcher** (`scripts/launch-direct.py`, `dropin_materialiseer`): het
doel moet binnen `base` liggen, en `base` is `dirname(game-exe)` = `…/views/LuigisMansion/exe/x64`.
De functie repareert een **symlink in de keten onder `base`** door hem te vervangen door een echte map
met symlinks naar de NAS-kinderen — maar hij loopt alleen de delen *onder* `base` af. Was `base` zelf
(een van `exe` of `exe/x64`) een symlink naar de NAS, dan kan hij dat niet repareren en faalt de
eindcontrole `_binnen(realpath(dirname(dest)), base)`: de NAS valt buiten de view.

**De stand nu, gemeten:** `views/LuigisMansion`, `…/exe` en `…/exe/x64` zijn alle drie **echte mappen**
(geen symlinks), en de materialisatietoets met de functie van de launcher zelf zegt voor beide
bestanden `ok=True — de map van … is lokaal`. De drop-in arriveert dus, en `To = .` is de juiste keuze:
een pad *buiten* `base` wordt door de launcher geweigerd, dus de wortel van de view als doel kan niet.

**Dit is wel fragiel:** komt `base` bij een nieuwe view als symlink terug, dan faalt het opnieuw — de
reparatie dekt `base` niet. Dat is een gaatje in de launcher (niet mijn bestand): laat de reparatie ook
`base` zelf behandelen, dan is het structureel opgelost.

**De paden staan absoluut in Wine-vorm**, want het spel werkt in de map op de NAS en een relatief pad
komt daar niet uit:

```ini
[rg-pe-hook.dll]
To = .
Preload = true
Audit = false
Env = RG_HOOK_INI=Z:\home\deck\.local\share\ravengun\runtime\views\LuigisMansion\exe\x64\dongle-haken.ini;RG_HOOK_LOG=Z:\home\deck\.local\share\ravengun\runtime\views\LuigisMansion\exe\x64\rg-pe-hook.log

[dongle-haken.ini]
To = .
```

Gecontroleerd met `--plan` (exitcode 0): `'preload': ['…/exe/x64/rg-pe-hook.dll']`, beide `dest`'s
binnen de view, `'errors': []`, en de omgevingsvariabelen komen er als `'Z:\\home\\deck\\…'` uit.
`Audit = false` omdat dit een PE-DLL is en geen ELF: in `LD_AUDIT` weigert glibc hem met *"wrong ELF
class"* en dat is alleen ruis; `Preload = true` zet het pad in `RG_DROPIN_PRELOAD`, en de lader heeft
daarvoor de `TP_PRELOAD_DLL`-route (die repareert de laderbouwer — dat is het tweede, aparte punt).

## 3. WANNEER de hook omzet, en waarom dat het hele punt is

De prologues bestaan pas **nadat** de pakker de code heeft weggeschreven (t=4,7 s). Een hook die bij
het **laden** omzet, zet dus niets om. Daarom heeft de engine een **wachtroute**: een pagina die nog
nul is wordt niet als fout gemarkeerd maar als `NOG NIET UITGEPAKT`, en de hook blijft kijken (elke
`wacht_ms`) tot `wacht_s` verstreken is. In de lijst: `wacht_s = 10`, `wacht_ms = 20`.

Bewezen (`patches/Halo/probe/wacht-toets.sh`), met de 17 prologues eerst op nul en een gesimuleerde
ontpakker die ze na 600 ms schrijft:

```
wachtroute: 17 pagina('s) nog niet uitgepakt; ik blijf kijken (max 10 s, elke 20 ms)
[ontpakker] 17 prologues geschreven na 600 ms
wachtroute: de pagina's zijn uitgepakt na 602 ms; nu omzetten
totaal aangehaakt: 17 van 17   ->  GESLAAGD
```

En de **controle** met `wacht_s = 0` laat precies het falen zien: per adres
`NOG NIET UITGEPAKT: de prologue op RVA 0x… is nog nul`, 0 van 17.

**Vroeg genoeg?** Ja: omzetten gebeurt 2 ms nadat de pagina's gevuld zijn, en het gemeten venster is
1,3 s. Zelfs met een poll van 20 ms blijft er meer dan een seconde over.

## 4. Het bewijs van het omzetten zelf

`bash patches/Halo/probe/pe-mechanisme-toets.sh` op `image-gesloten.bin` (19.259.392 B, sha256
`11bb710b7c71686b`; ImageBase `0x140000000`, SizeOfImage `0x125e000`, entry `0xf23125`):

* **positief op de 17 ECHTE adressen**: `17 van 17 aangehaakt`, elk `GELDIG: sectie .text …`; de
  stubpagina komt op **0x13fff0000**, precies waar de gesloten kern zijn stubs legt;
* **weigering op de ONGEWIJZIGDE dump**: alle 17 `AFGEWEZEN: eerste bytes e9 … zijn geen prologue
  maar een sprong -- al door een andere kern omgezet?`, 0 van 17;
* **ongeldige adressen**: `.rdata` → *"is niet uitvoerbaar"*; buiten het beeld → *"valt in geen
  sectie"*; verkeerde `grootte` → hele lijst afgekeurd;
* **de stubs draaien**: `amDongleInit` geeft 0 terug, en het gemeten kastantwoord komt eruit —
  `buffer na de aanroep: c0 a8 01 00` (192.168.1.0) met teruggeefwaarde 0;
* **de TODO-vorm** bestaat nog voor een lijst waarin iets niet gemeten is.

## 5. De API-route: NIET gebouwd, en dat is nu onderbouwd

Eerder leek het aftappen van `ntdll!NtProtectVirtualMemory` de robuustere plek, omdat de gesloten kern
die API aanhaakt en wij niet. **Dat is niet nodig, en het wordt niet gebouwd.** De `WINEDEBUG=+virtual`
meting op beide kernen is **byte voor byte identiek**: vier aanroepen (`.text` naar RWX, dan naar RX;
de pakkersectie idem), geen enkele fout in beide logboeken, en de laatste virtuele regels van onze run
zijn de afsluitende eindpass van de ontpakker. Onze kern maakt `.text` dus net zo goed schrijfbaar als
de gesloten kern; de protect-aanroep is **niet** het verschil. Een tweede mechanisme (omhullen +
trampoline + terugzetten) zou geen gedrag toevoegen en alleen onderhoud vragen.

De bouwstenen die ik daarvoor in de kop van de engine had gezet (`export =`, `module =`,
`RG_PROLOOG_API`, `RG_ANTWOORD_OMHUL`, `rg_pe_herstel()`) blijven staan als **ongebruikte
voorbereiding**; ze doen niets en er is geen code die ze aanroept.

## 6. De antwoorden, en de stand van de `bytes`-keuring

Uit `AM-LAAG.md` §4/§5: **alle 17 geven 0 terug**, en de enige echte waarde is
`amDongleGetNetworkAddress` → `*(uint32_t*)rcx = 0x1a8c0 = c0 a8 01 00 = 192.168.1.0`. Er is **geen**
game-id, **geen** modeltype en **geen** keychip-id. De lijst doet exact dat: 16 × `ret0`,
1 × `buf=rcx:c0 a8 01 00`, met `bewaar_args = 1` (elke stub zet eerst de argumentregisters in de home
space, zoals élke kern-implementatie doet).

**De `bytes = …`-keuring per regel is er nog NIET, en dat is met opzet.** De dichte meting bemonstert
**8 van de 17** adressen, en op t=4,7 s zijn die nog niet allemaal klaar: vier keer
`40 53 48 83 ec`, twee keer `48 83 ec 28 48`, één keer `48 83 ec 28 83`, en één keer `cc cc cc cc cc`
(`amDongleIsAvailableInternal` staat dan nog op padding). Dat is een momentopname **midden in het
uitpakken** en geen betrouwbare bron per functie. Een halve of verkeerde `bytes`-regel is erger dan
geen; daarom blijft `proloog = standaard` staan (bewezen, en het weigert de gesloten kern al correct).
Wat het afmaakt: één dump van **alle 17** op een moment dat het uitpakken klaar is (t≈5,5 s, vóór de
dood op 5,96 s) — dan krijgt elke regel zijn `bytes` en weigert de hook ook een verkeerde spelversie.

## 7. Wat er nog moet

1. **De laderbouwer:** `Preload` van een drop-in-DLL onder Wine op de `TP_PRELOAD_DLL`-route zetten
   (nu zet de launcher alleen `RG_DROPIN_PRELOAD`, en dat kanaal werkt onder Wine niet).
2. **De launcher:** de symlink-reparatie ook op `base` zelf toepassen (nu alleen eronder), dan is de
   drop-in niet afhankelijk van de toevallige stand van de view.
3. **De bench (vrij):** één dump van alle 17 prologues ná het uitpakken (t≈5,5 s) voor de
   `bytes`-keuring; de analist kan dat met zijn bestaande dichte bemonstering.
4. **Daarna:** de vijfminproef van de Luigi-werker om te bevestigen dat de DLL in het proces staat
   (`rg-pe-hook.log` in de view) en of de game verder komt dan t=5,96 s.
