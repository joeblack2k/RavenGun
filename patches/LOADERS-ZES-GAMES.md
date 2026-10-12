# Loaders en patches voor zes games

Doel: per game onze **eigen** lader/patch in plaats van de gesloten TeknoParrot-kern. Volgorde van de
eigenaar, één game tegelijk. Dit document is het overzicht; de details staan in de patchkaart van elke
game.

Status op 2026-10-12 (bijgewerkt na de eerste ronde per-game uitzoekers). De zes games zijn op
**volledigheid** gecontroleerd en alle zes zijn compleet: geen lege bestanden op voortgangs-, log- en
bouwartefacten na, bij de ELF-games vallen alle `PT_LOAD` binnen het bestand en zijn alle `DT_NEEDED`
vindbaar, en voor Scarlet Dawn is de hele map nagerekend (2.081 bestanden, 13.095.511.155 B, nul lege
bestanden, vijf sha256's die kloppen). Een mislukte start is dus niet de collectie.

| # | game | platform / engine | route die het nu start | stand van onze eigen lader | waar het op staat |
|---|---|---|---|---|---|
| 1 | **Luigi's Mansion Arcade** | Sega Nu 1.1, x64 Windows (`VACUUM.exe`) | gesloten kern + **onze lader**; **onze invoer** via `tp-shm` (`shared-luigi-mansion`) | onze eigen kern laat het spel nog niet leven; de beslissende meting is **klaar** en heeft de vraag verlegd | **De pakker is niet het probleem**: de gesloten kern speelt het spel met een onaangeroerd entry point, dus de pakker ontpakt zichzelf. **Exitcode 32 is de eigen reactie van het spel**: het spel geeft 32 ook ZONDER ENIGE KERN (1,5 s), met onze kern leeft het ~30 s, met de gesloten kern >45 s en laadt het 393 eigen databestanden. "Onze aanhechting verstoort iets" en "32 = anti-tamper" zijn dus **weerlegd**. **COM1 is geen hefboom**: met en zonder patch 0005 raakt het spel de poort nooit aan (0 blokken op een pty, 0 COM-namen in 77--86 CreateFileW-regels), dus 0005's premisse is onbewezen en zijn COM-blokkade vervalt. **Nieuwe richting:** het spel laadt `cfgmgr32` en `hid`, dus het zoekt zijn bord via **SetupAPI/HID-opsomming**. De vraag is nu: wat levert de gesloten kern dat het spel laat leven? Zie `patches/LuigisMansion/B0-LEGE-DLL.md` + `LADER-PLAN.md` |
| 2 | **Rabbids Hollywood** | Unity / Windows (Adrenaline) | **onze eigen route** (GE-Proton + BepInEx + onze plug-in) | **klaar en bewezen** | **Het spel start en blijft leven**: 288 s in de attract-lus, zelf gestopt (`Application.Quit` 0, `DONGLE NOT FOUND` 0), tegen ~70 s eerder. De oorzaak van het eerdere gedrag was een **uitrolprobleem**: de uitgeleverde view had een plug-in die achterliep op de build (32.768 B `b4fe9d83…` tegen 34.304 B `29fd3ddc…`); `play-unity.sh` rolt bij elke start de verse uit en dat is nu bewezen. **Niet gemeten:** munt, start en vizier -- de meting liep headless en daar komen toetsen niet aan; dat vraagt een run op de tv. **Open:** de Encrypt/Decrypt-familie wordt nog niet beantwoord, waardoor het spel elke start zijn savegames reset (niet fataal, wel een echt gat). **En:** `Game_Data/Plugins` is nog een **symlink naar de NAS** -- een drop-in daar zou op de NAS schrijven, dus dat wordt lokaal gemaakt. Zie `patches/RabbidsHollywood/` |
| 3 | **Halo Fireteam Raven** | Raw Thrills, **Linux x86-64 ELF** | ElfLdr2, maar de **kernel-`ld.so` is genoeg** | **geen eigen lader nodig**: `game` is `ET_EXEC` met `PT_INTERP=/lib64/ld-linux-x86-64.so.2` en een stock glibc `_start` | Onze i386-lader is hier niet "nog te porten": er is niets voor hem te doen. `LD_LIBRARY_PATH=runtime/lib-fmodstub ldd game` geeft **0 ontbrekend**. De FMOD-premise was fout: `halo.so` importeert **niets** van FMOD; de 60 FMOD-namen zitten in de eigen importresolver van `game` en zijn set-identiek aan onze stub -- dus alleen geluid, geen startblokker. De echte blokker is **hardware**: het FTDI-gunbord (`0403:6001`, precies één chip) en er hangt er nu geen aan de Deck. Onze eigen x64-hooklibs zijn gebouwd maar staan nog niet in het native recept (een verwisseling, geen sloop). Zie `patches/Halo/HALO-NATIVE-ROUTE.md` |
| 4 | **Terminator Salvation** | Raw Thrills, **32-bits i386 ELF** | ElfLdr2 | **onze eigen `tools/rg-elfloader/` is hier direct toepasbaar**, maar struikelt nu op een echt gat in onze eigen lader | **Ontpakt**, en schoon: `SHNUM=29`, 496 PLT-stubs zonder één `0xdeadbeef`, 0 van 496 GOT-slots voorgevuld, en **0 van 497 importnamen vernietigd** -- de eerder gevreesde bovengrens geldt hier niet. Alle 13 `DT_NEEDED` vinden een bibliotheek (`libglut.so.3` bestaat al als echte ELF32 in `runtime/lib32/`). **Het gat:** onze lader leest het gast-symboolsantal alleen uit `DT_HASH` en faalt hard op een bestand met alleen `DT_GNU_HASH`; `dynsym` 1020 = `(DT_STRTAB−DT_SYMTAB)/16` klopt hier precies. Dat ligt bij de laderbouwer. Verder: `game` draagt de HASP-client **statisch** en de controle loopt **na `main`**; de map `hasp/` wordt door de game **nergens** aangeraakt; `ServiceDongleApp` is ongestript met DWARF en levert een symbolenkaart voor de gestripte `game` (42 van 50). Zie `patches/TerminatorSalvation/` |
| 5 | **Transformers Shadows Rising** | Sega RingEdge 2, Windows | **het spel is al speelbaar en door de eigenaar zelf in fullscreen gespeeld** | de TP-kern zit alleen in `Shell.exe`, niet in het spel | De eigenaar heeft de disambiguatie beslecht: hij speelde "de nieuwste", dus **Shadows Rising** (2018; Human Alliance is 2013) -- dit dossier is dus op de juiste game van toepassing. `Transformers2.exe` importeert **alleen `dinput8`** voor de invoer (geen `hid.dll`, geen `setupapi`): het geweer heeft **geen VID/PID** nodig. De `ioboard`-melding "Couldn't find IOBoard device" staat in **elke** sessie, ook in de sessies die volledig speelden -- die was nooit de blokker. De identiteitsvraag is **beslecht**: `POLLING FLAGS` is onbereikbare code, dus het bord is een **HID Game Pad** (`UsagePage 0x0001`/`Usage 0x0005`). De hele handshake is statisch geoogst (vier uitwisselingen, `00 74 E0`→`0xB0` enz.) en de `SaeIoBoard` is generiek gebouwd met een **antwoordtabel als per-game gegeven**, met een validator die een onmogelijke respons weigert. **Wat de eigenaar bevestigt is de start, niet het bord**: lampen, toetsenpaneel en recoil zijn onbevestigd, en `Keypad Enabled OK` hoort zonder het `ExternalKeypad`-bit in `shell/Game.ini` niet te verschijnen. Let op: er staat een **tweede** Transformers op de NAS (`Human Alliance`, ander profiel) |
| 6 | **House of the Dead: Scarlet Dawn** | Sega ALLS, **Unreal Engine** (`Hodzero`) | TP plus de **AM Daemon** | niet nodig als lader: de **AM Daemon** is hier de platformlaag | het spel is **zonder keychip speelbaar**; de daemon logt `Keychip Not Found` en probeert opnieuw. De keychip-aanroepen lopen allemaal door **één** `DeviceIoControl` op `\\.\FddDriver`, in hetzelfde proces als de schrijfbriefjes, dus één haak volstaat. SEGA levert zelf een meetinstrument mee (`AMDaemon/hwvalue.exe`, 202.240 B) dat modeltype/regio/dipsw uitleest; de regio is een bitveld met masker. Tien `amDongle*`-functies zijn dood (`not implemented` in libam) en hoeven niet geëmuleerd te worden. De AiMe-lezer staat al in zijn "afwezige" toestand (geen van de drie DLL's aanwezig in de spelmap). **Volledigheid is nu ook gemeten:** 2.081 bestanden, 13.095.511.155 B, nul leeg, vijf sha256's gelijk tussen spiegel en Deck. Zie `patches/HouseOfTheDeadScarletDawn/REPORT.md` |

## Vaste regels bij dit werk

* **Het doel is spelen met een PlayStation Move als geweer.** Dat heeft de eigenaar zo gezegd: de Move is
  het hele doel van de lader. De invoerketen loopt van de Move via de daemon en het gedeelde geheugen naar
  de game; per game kan de laatste schakel verschillen (Unity/TCP bij Rabbids, een HID-bord bij
  Transformers). Het IO-bord van Transformers is voor de lampen, het toetsenpaneel en de recoil -- niet
  voor het vizier.
* **De games staan op de NAS en de lader start ze daar vandaan.** Dat is het ontwerp: het spel schrijft
  zijn eigen saves en instellingen in die map, en dat is normaal. Wat **niet** mag is dat *onze* artifacts
  (patches, drop-ins, onze eigen bestanden) ongevraagd in een speltmap op de NAS landen. De
  `teknoparrot.ini` die onze kern in de werkmap schrijft is een bekend grensgeval: gedocumenteerd en
  geaccepteerd, niet iets om omheen te bouwen.
* **Dongle-, HASP-, keychip-, integriteits- en checksumwerk wordt verdeeld, niet verboden.** De bouwer
  in de lader-repo (Claude) blijft erbuiten en draagt het over; **subagents doen het**. Zie
  `patches/LuigisMansion/pakker-meting.md` voor de rechtzetting van een eerdere, foutieve versie van
  deze regel.
* **Eén game tegelijk op de bench**, en het scherm van de eigenaar blijft vrij (dus headless).
* Per game: eerst nagaan **wat er nodig is en wat er al is**, dan bouwen, dan **verifiëren dat het spel
  start**, en gemeten van aangenomen onderscheiden. Een meting die niets vindt moet eerst bewijzen dat
  hij gezocht heeft (diepte, methode, controle), en een uitkomst hoort **vooraf** opgeschreven te zijn --
  anders is een mislukte controle achteraf te makkelijk weg te praten.

## Wat er als volgende op de bench moet, in de volgorde van de eigenaar

Eén game tegelijk, en het scherm van de eigenaar blijft vrij (alles headless via gamescope).

1. **Luigi's Mansion** (bezig): de bisectie met de gesloten kern als levende controle, daarna onze kern,
   `leeg.dll` en `leeg0.dll` -- en de vraag of het spel ook ZONDER kern op 32 uitkomt, want dan is 32 de
   eigen reactie van het spel en niet iets wat onze kern veroorzaakt.
2. **Rabbids Hollywood**: `rg_savegame.sh` -- twee starts, om te bewijzen dat de savegames niet meer
   gereset worden (ongeveer 9 minuten). Daarna is de vraag of munt/start/vizier op de tv geverifieerd
   kunnen worden; dat kan niet headless.
3. **Halo Fireteam Raven**: run 1 zonder FTDI-bord (90 s) met de USB-tap, om te onderscheiden of de tap
   niet laadt, of de game het bord netjes mist, of hij in die tijd niet aan zijn FTDI-pad toekomt.
4. **Terminator Salvation**: de eigen ELF-lader op `game`, met de DT_GNU_HASH-fix erin; verwacht bewijs
   is "shim geladen: N gast-symbolen" en daarna de eigen donglecontrole van de gast.
5. **Transformers Shadows Rising**: de `SaeIoBoard` als `uhid`-bord, om de handshake te bevestigen die
   statisch is geoogst (vier uitwisselingen) en om te zien of Wine `/dev/uhid` überhaupt aan `SetupDi`
   blootstelt.
6. **Scarlet Dawn**: geen benchrun nodig voor de start (het spel is zonder keychip speelbaar); de
   keychip- en AiMe-lagen zijn completeness-werk met een eigen plan.

## Waar de dossiers staan

Per game in `patches/<Game>/` (naast de bestaande kaart met `game.ini` en `README.md`). **De mapnaam is
de TeknoParrot-profielnaam**, want dat is wat de app opzoekt: `patches/<Game>/game.ini` met `<Game>` uit
het profiel (`HOTDSD`, `Terminator`, `LuigisMansion`, `RabbidsHollywood`, `Halo`,
`TransformersShadowsRising`). Een map met een andere naam wordt door geen enkel script gevonden -- daar
is dit document op gecorrigeerd: de dossiers van Scarlet Dawn en Terminator Salvation stonden eerst in
`HouseOfTheDeadScarletDawn/` en `TerminatorSalvation/` en zijn naar `HOTDSD/` en `Terminator/` verhuisd.

Van Luigi staat het pakkeronderzoek, het laderplan, de pre-flight, de B0-bisectie en het COM1-harnas er;
van Rabbids het dongleonderzoek en het lader/patchrapport; van Scarlet Dawn, Transformers, Halo en
Terminator het volledige dossier.

