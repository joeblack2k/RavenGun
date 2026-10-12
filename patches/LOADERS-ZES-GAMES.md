# Loaders en patches voor zes games

Doel: per game onze **eigen** lader/patch in plaats van de gesloten TeknoParrot-kern. Volgorde van de
eigenaar, één game tegelijk. Dit document is het overzicht; de details staan in de patchkaart van elke
game.

Status op 2026-10-12. De zes games zijn op **volledigheid** gecontroleerd: alle zes compleet
(geen lege bestanden op wat voortgangs-, log- en bouwartefacten na, en bij de ELF-games vallen alle
`PT_LOAD` binnen het bestand en zijn alle `DT_NEEDED` vindbaar). Een mislukte start is dus niet de
collectie.

| # | game | platform / engine | route die het nu start | stand van onze eigen lader | waar het op staat |
|---|---|---|---|---|---|
| 1 | **Luigi's Mansion Arcade** | Sega Nu 1.1, x64 Windows (`VACUUM.exe`) | gesloten kern; **onze invoer** via `tp-shm` (`shared-luigi-mansion`) | niet gebouwd; er is een per-game hook nodig, geen generieke infrastructuur | **laag 1:** Capcom-pakker -- vijf secties met 0 bytes in het bestand, `.capcom1` is de ontpakker (entropie 7,9553) en het entry point zit erbinnen. **laag 2:** op onze kern **exitcode 32 = Capcom anti-tamper**; de gesloten kern blijft leven. Patch 0005 (speciaal voor dit spel, COM1 als aanwezig melden) loste dat niet op. **Grens van de eigenaar: documenteren wel, omzeilen niet.** Zie `patches/LuigisMansion/pakker-meting.md` |
| 2 | **Rabbids Hollywood** | Unity / Windows (Adrenaline) | onze **eigen** daemon-route (BepInEx + TCP) naast de TP-route | grotendeels al van ons | uitzoeken welk deel nog van TeknoParrot is (lader? injectie? doorstop?) en wat er nog moet voor "zonder enige TP-component". Vermoedelijk de snelste van de zes |
| 3 | **Halo Fireteam Raven** | Raw Thrills, **Linux x86-64 ELF** | ElfLdr2 | **onze lader is 32-bits en geldt hier dus niet**; de native route loopt via de kernel-`ld.so` | het **FTDI-IO-bord** is niet geëmuleerd en de herkomst van `halo.so` is onbekend; FMOD 1.10 wordt door onze eigen stub opgevangen (`runtime/lib-fmodstub/`) |
| 4 | **Terminator Salvation** | Raw Thrills, **32-bits i386 ELF** | ElfLdr2 | **onze eigen `tools/rg-elfloader/` is hier direct toepasbaar** -- het is dezelfde klasse als de game waarop die lader bewezen is | eerst: is `game` HASPE-gepakt (dan zijn importnamen vernietigd en is er een harde bovengrens)? En: het spel wordt geacht een donglebestand (`hasp/`) te vragen -- **documenteren, niet aanraken** |
| 5 | **Transformers Shadows Rising** | Sega RingEdge 2, Windows | TP plus onze HID-bordpoging | de TP-kern zit alleen in `Shell.exe`; `Transformers2.exe` laadt zelf `hid.dll`/`setupapi.dll`/`dinput8.dll` | een **virtueel HID-IO-bord** (`uhid`) na het meten van VID/PID en de rapportindeling. Let op: er staat een **tweede** Transformers-game op de NAS (`Transformers Human Alliance`, ander profiel) |
| 6 | **House of the Dead: Scarlet Dawn** | Sega ALLS, **Unreal Engine** (`Hodzero`) | TP plus de **AM Daemon** | niet gebouwd | de **AM Daemon** is hier de platformlaag, niet de lader; de kast start via `App/Startup/*.bat`. Het project heeft de amdaemon-laag al onderzocht (`docs/research/amdaemon-mechanisme.md`) |

## Vaste regels bij dit werk

* **Alleen lezen op de NAS**, tenzij de eigenaar expliciet om wijzigen vraagt.
* **Geen dongle-, HASP-, keychip-, integriteits- of checksumwerk.** Kom je zoiets tegen, dan documenteren
  en stoppen -- niet omzeilen, niet faken.
* **Eén game tegelijk op de bench**, en het scherm van de eigenaar blijft vrij.
* Per game: eerst nagaan **wat er nodig is en wat er al is**, dan bouwen, dan **verifiëren dat het spel
  start**, en gemeten van aangenomen onderscheiden.

## Waar de dossiers staan

Per game in `patches/<Game>/` (naast de bestaande kaart met `game.ini` en `README.md`). Van Luigi staat
het volledige pakker- en anti-tamperonderzoek er al; de andere vijf komen eraan zodra de per-game
uitzoekers hun rapport inleveren.
