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
| 1 | **Luigi's Mansion Arcade** | Sega Nu 1.1, x64 Windows (`VACUUM.exe`) | gesloten kern + **onze lader**; **onze invoer** via `tp-shm` (`shared-luigi-mansion`) | plan klaar; beslissende meting B0 is gedraaid maar **ongeldig**, de controle met de gesloten kern stierf (exitcode 5) | **laag 1 (pakker):** vijf secties met 0 bytes in het bestand, `.capcom1` is de ontpakker (entropie 7,9553), entry point erbinnen. **Geen eigen image-loader nodig** -- de gesloten kern speelt het spel met een onaangeroerd entry point, dus de pakker ontpakt zichzelf al. **laag 2 (onze kern):** exitcode 32, en het spel opent *nul* eigen bestanden. "32 = Capcom anti-tamper" is **interpretatie**, niet gemeten. De kern is het **eerste argument van de lader**, dus de bisectie is een verwisseling: lege DLL / onze kern / gesloten kern, verder alles gelijk. Eerste poging: de gesloten kern gaf exit **5** in een andere Wine-prefix dan de werkende route, dus de opstelling was fout en (c)/(d) zijn nog niet te interpreteren. Hermeting loopt. Dit spel vraagt uiteindelijk een **per-game hook**, geen generieke infrastructuur. Zie `patches/LuigisMansion/pakker-meting.md` + `LADER-PLAN.md` |
| 2 | **Rabbids Hollywood** | Unity / Windows (Adrenaline) | **onze eigen route** (GE-Proton + BepInEx + onze plug-in) | **klaar en bewezen** | **Het spel start en blijft leven**: 288 s in de attract-lus, zelf gestopt (`Application.Quit` 0, `DONGLE NOT FOUND` 0), tegen ~70 s eerder. De oorzaak van het eerdere gedrag was een **uitrolprobleem**: de uitgeleverde view had een plug-in die achterliep op de build (32.768 B `b4fe9d83…` tegen 34.304 B `29fd3ddc…`); `play-unity.sh` rolt bij elke start de verse uit en dat is nu bewezen. **Niet gemeten:** munt, start en vizier -- de meting liep headless en daar komen toetsen niet aan; dat vraagt een run op de tv. **Open:** de Encrypt/Decrypt-familie wordt nog niet beantwoord, waardoor het spel elke start zijn savegames reset (niet fataal, wel een echt gat). **En:** `Game_Data/Plugins` is nog een **symlink naar de NAS** -- een drop-in daar zou op de NAS schrijven, dus dat wordt lokaal gemaakt. Zie `patches/RabbidsHollywood/` |
| 3 | **Halo Fireteam Raven** | Raw Thrills, **Linux x86-64 ELF** | ElfLdr2, maar de **kernel-`ld.so` is genoeg** | **geen eigen lader nodig**: `game` is `ET_EXEC` met `PT_INTERP=/lib64/ld-linux-x86-64.so.2` en een stock glibc `_start` | Onze i386-lader is hier niet "nog te porten": er is niets voor hem te doen. `LD_LIBRARY_PATH=runtime/lib-fmodstub ldd game` geeft **0 ontbrekend**. De FMOD-premise was fout: `halo.so` importeert **niets** van FMOD; de 60 FMOD-namen zitten in de eigen importresolver van `game` en zijn set-identiek aan onze stub -- dus alleen geluid, geen startblokker. De echte blokker is **hardware**: het FTDI-gunbord (`0403:6001`, precies één chip) en er hangt er nu geen aan de Deck. Onze eigen x64-hooklibs zijn gebouwd maar staan nog niet in het native recept (een verwisseling, geen sloop). Zie `patches/Halo/HALO-NATIVE-ROUTE.md` |
| 4 | **Terminator Salvation** | Raw Thrills, **32-bits i386 ELF** | ElfLdr2 | **onze eigen `tools/rg-elfloader/` is hier direct toepasbaar**, maar struikelt nu op een echt gat in onze eigen lader | **Ontpakt**, en schoon: `SHNUM=29`, 496 PLT-stubs zonder één `0xdeadbeef`, 0 van 496 GOT-slots voorgevuld, en **0 van 497 importnamen vernietigd** -- de eerder gevreesde bovengrens geldt hier niet. Alle 13 `DT_NEEDED` vinden een bibliotheek (`libglut.so.3` bestaat al als echte ELF32 in `runtime/lib32/`). **Het gat:** onze lader leest het gast-symboolsantal alleen uit `DT_HASH` en faalt hard op een bestand met alleen `DT_GNU_HASH`; `dynsym` 1020 = `(DT_STRTAB−DT_SYMTAB)/16` klopt hier precies. Dat ligt bij de laderbouwer. Verder: `game` draagt de HASP-client **statisch** en de controle loopt **na `main`**; de map `hasp/` wordt door de game **nergens** aangeraakt; `ServiceDongleApp` is ongestript met DWARF en levert een symbolenkaart voor de gestripte `game` (42 van 50). Zie `patches/Terminator/` |
| 5 | **Transformers Shadows Rising** | Sega RingEdge 2, Windows | **het spel is al speelbaar** | de TP-kern zit alleen in `Shell.exe`, niet in het spel | `Transformers2.exe` importeert **alleen `dinput8`** voor de invoer (geen `hid.dll`, geen `setupapi`): het geweer heeft dus **geen VID/PID** nodig. De `ioboard`-melding "Couldn't find IOBoard device" staat in **elke** sessie, ook in de sessies die volledig speelden -- die was nooit de blokker. De identiteitsvraag is **beslecht**: `POLLING FLAGS` is onbereikbare code (het naamveld kan die tekst nooit bevatten en de uitkomst wordt nergens gelezen), dus het bord is gewoon een **HID Game Pad** (`UsagePage 0x0001`/`Usage 0x0005`). `0x110000` was `HIDP_STATUS_SUCCESS`, `0x47ff50` een import-thunk. Het eerste rapport `00 74 E0` met 1000 ms venster is statisch achterhaald. Eén nieuw risico: of Wine `/dev/uhid`-apparaten überhaupt aan `SetupDi`/`HidD` blootstelt; zo niet, dan is er geen bord te vinden. Let op: er staat een **tweede** Transformers op de NAS (`Human Alliance`, ander profiel) |
| 6 | **House of the Dead: Scarlet Dawn** | Sega ALLS, **Unreal Engine** (`Hodzero`) | TP plus de **AM Daemon** | niet nodig als lader: de **AM Daemon** is hier de platformlaag | het spel is **zonder keychip speelbaar**; de daemon logt `Keychip Not Found` en probeert opnieuw. De keychip-aanroepen lopen allemaal door **één** `DeviceIoControl` op `\\.\FddDriver`, in hetzelfde proces als de schrijfbriefjes, dus één haak volstaat. SEGA levert zelf een meetinstrument mee (`AMDaemon/hwvalue.exe`, 202.240 B) dat modeltype/regio/dipsw uitleest; de regio is een bitveld met masker. Tien `amDongle*`-functies zijn dood (`not implemented` in libam) en hoeven niet geëmuleerd te worden. De AiMe-lezer staat al in zijn "afwezige" toestand (geen van de drie DLL's aanwezig in de spelmap). **Volledigheid is nu ook gemeten:** 2.081 bestanden, 13.095.511.155 B, nul leeg, vijf sha256's gelijk tussen spiegel en Deck. Zie `patches/HOTDSD/REPORT.md` |

## Vaste regels bij dit werk

* **Alleen lezen op de NAS**, tenzij de eigenaar expliciet om wijzigen vraagt. Ook indirect: een view die
  via een symlink naar de NAS wijst mag geen drop-in ontvangen -- dat is een schrijfpad naar de NAS.
  Gemeten op 2026-10-12: de Rabbids-run heeft **niets** op de NAS geschreven (nul bestanden gewijzigd,
  `Game_Data/Plugins` mtime onveranderd). De mount is wel CIFS **rw**, dus dat is een gedrags- en geen
  bestandssysteemgarantie.
* **Dongle-, HASP-, keychip-, integriteits- en checksumwerk wordt verdeeld, niet verboden.** De bouwer
  in de lader-repo (Claude) blijft erbuiten en draagt het over; **subagents doen het**. Zie
  `patches/LuigisMansion/pakker-meting.md` voor de rechtzetting van een eerdere, foutieve versie van
  deze regel.
* **Eén game tegelijk op de bench**, en het scherm van de eigenaar blijft vrij (dus headless).
* Per game: eerst nagaan **wat er nodig is en wat er al is**, dan bouwen, dan **verifiëren dat het spel
  start**, en gemeten van aangenomen onderscheiden. Een meting die niets vindt moet eerst bewijzen dat
  hij gezocht heeft (diepte, methode, controle), en een uitkomst hoort **vooraf** opgeschreven te zijn --
  anders is een mislukte controle achteraf te makkelijk weg te praten.

## Waar de dossiers staan

Per game in `patches/<Game>/` (naast de bestaande kaart met `game.ini` en `README.md`). Van Luigi staat
het pakkeronderzoek, het laderplan, de pre-flight, de B0-bisectie en het COM1-harnas er; van Rabbids het
dongleonderzoek en het lader/patchrapport; van Scarlet Dawn, Transformers, Halo en Terminator het
volledige dossier.
