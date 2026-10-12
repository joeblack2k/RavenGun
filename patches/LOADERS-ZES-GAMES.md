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
| 1 | **Luigi's Mansion Arcade** | Sega Nu 1.1, x64 Windows (`VACUUM.exe`) | gesloten kern + **onze lader**; **onze invoer** via `tp-shm` (`shared-luigi-mansion`) | plan klaar (`LADER-PLAN.md`); beslissende meting B0 klaar om te draaien | **laag 1 (pakker):** vijf secties met 0 bytes in het bestand, `.capcom1` is de ontpakker (entropie 7,9553), entry point erbinnen. **Geen eigen image-loader nodig** -- de gesloten kern speelt het spel met een onaangeroerd entry point, dus de pakker ontpakt zichzelf al. **laag 2 (onze kern):** exitcode 32, en het spel opent *nul* eigen bestanden. "32 = Capcom anti-tamper" is **interpretatie**, niet gemeten. De kern is het **eerste argument van de lader**, dus de bisectie is een verwisseling: lege DLL / onze kern / gesloten kern, verder alles gelijk. Dit spel vraagt een **per-game hook**, geen generieke infrastructuur. Zie `patches/LuigisMansion/pakker-meting.md` + `LADER-PLAN.md` |
| 2 | **Rabbids Hollywood** | Unity / Windows (Adrenaline) | **onze eigen route** (GE-Proton + BepInEx + onze plug-in) | **klaar en bewezen** | **Het spel start en blijft leven** in de attract-lus, ruim voorbij de ~70 s waarop het eerder stierf, met `**** DONGLE FOUND: 69420` uit onze eigen laag in het logboek. De oorzaak van het eerdere gedrag was een **uitrolprobleem**: de uitgeleverde view had een plug-in zonder die laag. Dat is gemeten en de zelfuitrol werkt nu (md5 van de view = md5 van de build, `29fd3ddc…`). Eén openstaand punt: `Game_Data/Plugins` is nog een **symlink naar de NAS**, dus een drop-in daar zou op de NAS schrijven -- dat wordt lokaal gemaakt. Zie `patches/RabbidsHollywood/` |
| 3 | **Halo Fireteam Raven** | Raw Thrills, **Linux x86-64 ELF** | ElfLdr2 | **onze eigen ELF-lader is 32-bits i386 en geldt hier dus niet** | het **FTDI-IO-bord** is niet geëmuleerd en de herkomst van `halo.so` is onbekend; FMOD 1.10 wordt door onze eigen stub opgevangen (`runtime/lib-fmodstub/`). Uitzoeker loopt |
| 4 | **Terminator Salvation** | Raw Thrills, **32-bits i386 ELF** | ElfLdr2 | **onze eigen `tools/rg-elfloader/` is hier direct toepasbaar** -- zelfde klasse als de game waarop die lader bewezen is | eerst: is `game` HASPE-gepakt (dan zijn importnamen vernietigd en is er een harde bovengrens)? En: het spel wordt geacht een donglebestand (`hasp/`) te vragen -- **documenteren, niet aanraken**. Uitzoeker loopt |
| 5 | **Transformers Shadows Rising** | Sega RingEdge 2, Windows | **het spel is al speelbaar** | de TP-kern zit alleen in `Shell.exe`, niet in het spel | `Transformers2.exe` importeert **alleen `dinput8`** voor de invoer (geen `hid.dll`, geen `setupapi`): het geweer heeft dus **geen VID/PID** nodig. De `ioboard`-melding "Couldn't find IOBoard device" staat in **elke** sessie, ook in de sessies die volledig speelden -- die was nooit de blokker. De `HIDioThread`-dialoog van de shell en de sleutelloze transform zijn uitgeschreven; de identiteitsvraag (`POLLING FLAGS` of Game Pad) wordt statisch beslist. Let op: er staat een **tweede** Transformers op de NAS (`Human Alliance`, ander profiel) |
| 6 | **House of the Dead: Scarlet Dawn** | Sega ALLS, **Unreal Engine** (`Hodzero`) | TP plus de **AM Daemon** | niet nodig als lader: de **AM Daemon** is hier de platformlaag | het spel is **zonder keychip speelbaar**; de daemon logt `Keychip Not Found` en probeert opnieuw. De keychip-aanroepen lopen allemaal door **één** `DeviceIoControl` op `\\.\FddDriver`, in hetzelfde proces als de schijfbriefjes, dus één haak volstaat. SEGA levert zelf een meetinstrument mee (`AMDaemon/hwvalue.exe`). Zie `patches/HouseOfTheDeadScarletDawn/REPORT.md` |

## Vaste regels bij dit werk

* **Alleen lezen op de NAS**, tenzij de eigenaar expliciet om wijzigen vraagt. Ook indirect: een view die
  via een symlink naar de NAS wijst mag geen drop-in ontvangen -- dat is een schrijfpad naar de NAS.
* **Dongle-, HASP-, keychip-, integriteits- en checksumwerk wordt verdeeld, niet verboden.** De bouwer
  in de lader-repo (Claude) blijft erbuiten en draagt het over; **subagents doen het**. Zie
  `patches/LuigisMansion/pakker-meting.md` voor de rechtzetting van een eerdere, foutieve versie van
  deze regel.
* **Eén game tegelijk op de bench**, en het scherm van de eigenaar blijft vrij.
* Per game: eerst nagaan **wat er nodig is en wat er al is**, dan bouwen, dan **verifiëren dat het spel
  start**, en gemeten van aangenomen onderscheiden. Een mislukte meting moet bewijzen dat hij gezocht
  heeft (diepte, methode, en waar mogelijk een controle).

## Waar de dossiers staan

Per game in `patches/<Game>/` (naast de bestaande kaart met `game.ini` en `README.md`). Van Luigi staat
het pakkeronderzoek, het laderplan, de pre-flight en het COM1-harnas er; van Rabbids het
dongleonderzoek en het lader/patchrapport; van Scarlet Dawn en Transformers het volledige dossier.
Halo en Terminator komen eraan.
