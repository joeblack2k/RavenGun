# Luigi's Mansion: trace van `InitializeASI` in onze kern

*RavenGun, 2026-10-12, op de bench gemeten. Ruwe sporen: `~/bw/luigi-kernloos/` (`init-trace*.log`,
`trace-run2-launch.log`, `trace-closed-launch.log`). Patch die de trace toevoegt:
`third_party/openparrot/patches/0006-init-trace.patch` (opt-in via `RG_INIT_TRACE=<bestand>`).*

**Kort, en het is een correctie op de werkhypothese.** De hypothese was: onze `InitializeASI` keert niet
terug, en het spel sterft in die wachttijd. **Gemeten klopt dat niet.** `InitializeASI` loopt in ~65 ms
volledig af, zet de originele entry-bytes terug en springt naar het entry point van het spel. De blokkade zit
dus NIET in onze initialisatie.

## 1. Opzet

- Kern met trace: eigen build met patches 0001–0006, `OpenParrot64.dll` sha256
  `80fed36f45cf62138aec22c4b39e8168a014ed8f8b773994ae61fe2ebc4ac651`, via de gewone route
  (`[Loader] Core = ravengun` in een tijdelijke gebruikerslaag; `OpenParrotLoader64` + onze kern + `VACUUM.exe`).
- Vergelijking: dezelfde route met de gesloten kern (`current`, `TeknoParrot64.dll` f5c7532d).
- Per run: `RG_INIT_TRACE` naar een logbestand en `WINEDEBUG=+pid,+tid,+timestamp,+process,+seh`.
- **Benchregel nageleefd:** vooraf gecontroleerd dat de bench vrij was en de runtime-kern f5c7532d; na afloop
  weer gecontroleerd (0 processen, kern nog f5c7532d). De trace-kern staat in een eigen map, niet op `current`.

## 2. De trace van onze kern (ondubbelzinnig)

```
DllMain attach (kern geladen)                       t0
Main_DoInit begin                                   +2296 ms   (= DLL laden, 1,9 MB)
RunMain begin
RunMain: teknoparrot.ini geladen=1
RunMain: DetectCurrentGame klaar, game=143          (= GameID::LuigisMansion; 0005 werkt)
 RunFunctions(game=0) begin
   init #0..#8  (9 Global-init-functies, elk 1–8 ms, elk met "begin" en "eind")
 RunFunctions(game=0) eind
 RunFunctions(game=143) begin
 RunFunctions(game=143) eind                         (leeg: we hebben geen Luigi-init, alleen detectie)
RunMain eind
Main_DoInit: originele entry-bytes teruggezet (eb 08 92 1b ...), nu de sprong naar het entry point
```

Van `RunMain begin` tot de teruggezette entry-bytes: **65 ms**, elke stap met een begin- én een eind-regel.
Er is geen "begin" zonder "eind", geen wachtstap, geen hangende hook. De 20 originele entry-bytes (`eb 08 …`)
worden teruggeschreven en de kern springt naar het spel. **`InitializeASI` keert dus terug.**

## 3. Wat er daarna gebeurt — en waar het nu echt op zit

- **Onze kern:** na de sprong loopt het spelproces nog ~0,2 s op zijn eigen draad, bereikt dan de
  single-step-val in `VACUUM.exe+0x122EA12` (de bekende beschermingstak van de pakker), en eindigt met
  `0x40415820` (lage byte 32). Eén draad de hele tijd.
- **Gesloten kern (zelfde trace):** de kern doet in deze fase eerst zijn eigen werk (meerdere single-steps
  binnen `TeknoParrot64.dll`), print `*INF* Game Start`, en bereikt daarna dezelfde `VACUUM.exe+0x122EA12`.
  **Ook deze run eindigde abnormaal** (een `c0000005` die door `winedbg` werd opgevangen, exit `0xDEADC0DE`).

**Belangrijke kanttekening bij de ijkmeting.** Mijn vergelijkingsrun op de gesloten kern kwam dus NIET tot
een stabiele attract; hij printte wel "Game Start" maar viel daarna om. Dat wijkt af van de eerdere stand
("speelbaar op de gesloten kern"). De meest waarschijnlijke oorzaak is dat de zware `+seh/+process`-trace de
timing van de gesloten kern verstoort (zijn eigen fase is juist de single-step-gevoelige code). De schone
vergelijking vraagt dus een run met lichtere trace. Dat laat ik hier als open punt staan.

## 3b. Het venster van 239 ms op bestandsniveau (lichte trace, `+file`)

Een aparte run op onze kern met alleen `WINEDEBUG=+pid,+tid,+file` (geen `+seh`, zodat de trace de timing
minder verstoort) laat zien wat het spel in dat venster van zichzelf opent:

- **Niets van zichzelf.** Het spelproces opent alleen `VACUUM.exe`, zijn werkmap, en — geschreven/gelezen
  door ONZE kern — de init-trace, `teknoparrot.ini` (39× gepolld) en `hints.dat` (de patroon-cache van de
  kern). Geen `COM`, geen `LMAIO.bin`, geen `Ram/`, geen `Data/`, geen bord.
- **Geen onderscheidende fout.** De enige mislukte opens zijn `c0000034` (STATUS_OBJECT_NAME_NOT_FOUND) op
  DLL's in de exe-map — dat is de normale DLL-zoekvolgorde van Wine (eerst de exe-map, dan `system32`), geen
  echte fout.

Conclusie van dit deel: **het vroege einde wordt niet veroorzaakt door een mislukte bestands- of
apparaat-open.** Het verschil tussen onze kern en de gesloten kern zit niet op bestandsniveau maar in het
geheugen/de draden — en dat is `.text`-vulling en draad-aantal, die met een root-sampler te meten zijn
(`scratchpad/sampler.sh`, nog te draaien zodra de bench vrij is).

## 4. Conclusie en overdracht

1. **De blokkade zit niet in onze `InitializeASI`.** Die komt terug (gemeten, 65 ms, elke stap gelogd). De
   eerdere tijdlijn "trampoline blijft tot de dood, 1 draad, `.text` nul" is met deze trace niet te rijmen
   met onze kern; in mijn run werd de trampoline teruggezet en liep het spel verder tot zijn eigen val.
2. **Het verschil zit ná de sprong**, in wat het spel doet rond de single-step in `VACUUM.exe+0x122EA12` —
   dat is beschermings-/anti-tamper-code van de pakker. Per de taakverdeling van de eigenaar is het mechanisme
   zelf werk voor een eigen baan met een subagent; ik documenteer alleen waar het zit.
3. **Wat onze kern mist t.o.v. de gesloten kern:** de gesloten kern voert in deze fase substantieel eigen werk
   uit (de single-steps in `TeknoParrot64.dll`) voordat het spel "Game Start" bereikt; onze `RunFunctions`
   voor deze game is leeg (alleen detectie). Of dat werk nodig is om de pakker-tak te overleven, is niet
   vastgesteld.
4. **Methodische vondst:** de ijkmeting op de gesloten kern is onder zware trace niet stabiel. Een volgende
   meting moet met lichtere trace (`RG_INIT_TRACE` alleen, geen `+seh`) om de twee kernen eerlijk naast elkaar
   te leggen.

De trace-voorziening (`0006`) blijft als opt-in gereedschap in de patchset; standaard uit, geen gedrag zonder
`RG_INIT_TRACE`.
