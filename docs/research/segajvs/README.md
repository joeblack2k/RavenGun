# Sega JVS-games zonder de gesloten TeknoParrot-kern

*2026-10-17. Doel van de eigenaar: elke game start zonder TeknoParrot (geen `TeknoParrot(64).dll`, geen bestand uit de TeknoParrot-runtime).
Dit stuk gaat over de zeven Sega-games die vandaag op de gesloten 32-bits kern draaien met invoer via JVS: LGI, LGI3D, Transformers
(Human Alliance), GG, OG, SDR en LGS. Alles is statisch uit de exe's, de OpenParrot-bron en de bestaande metingen gehaald: er is voor dit
stuk geen enkele game gestart. Per bewering staat erbij hoe zeker ze is: **[gemeten]** (eerdere run of tool-uitvoer), **[gelezen]**
(code of tekst in de bron/exe), **[TP-team]** (antwoord van het TeknoParrot-team, niet geverifieerd), **[afgeleid]**.*

## Het korte antwoord

De kern doet voor deze games drie dingen die ertoe doen; een daarvan is al van ons, een is alleen in de exe te zien en een is onbekend
tot er één meting is.

| # | Wat de gesloten kern doet | Heeft OpenParrot het? | Stand |
|---|---|---|---|
| 1 | `CreateFile("COM4")` omleiden naar de pipe `\\.\pipe\TeknoParrot_JVS` plus de comm-API's (`SetCommState`, `GetCommModemStatus`, ...) | **Ja**, `OpenParrot/src/Functions/Ring_amLib/amJvs.cpp`, voor elke game die geen Type X is **[gelezen]**; alle zeven exe's openen letterlijk `COM4` **[gemeten]** (scan hieronder) | gedekt door `Core = ravengun`; op onze kern antwoordt de JVS-pipe bij LGI **[gemeten]** (`patches/EIGEN-KERN.md`) |
| 2 | De AM Library van de game (amMaster, amDongle, amInstall, amStorage, amNetwork, ...) in-proces afhandelen, zodat er geen AM-daemon op `127.0.0.1:40100..40115` nodig is | **Nee** (`grep` op de bron: geen `amMaster`, `pcpa`, `40100`) **[gelezen]** | **onbekend of het blokkeert**; zie "De AM-daemon" |
| 3 | Een onvoorwaardelijke `CreateWindowExW`-hook (laat `Windowed=1` crashen, daarom draait elke game in een Wine-bureaublad of gamescope) | n.v.t. | verdwijnt vanzelf met onze kern **[TP-team]**; geen vervanging nodig |

Regio/PCB (`DongleRegion`, `PcbRegion`) hoeft niets: de 32-bits kern kent die sleutels niet eens (`docs/research/region-mechanisme.md`,
hoofdstuk 2, 0 treffers in het ontsleutelde beeld) en elke `dongle.json` van deze games staat op `enforced: false` **[gemeten]**.

**Conclusie van deze ronde: alle zeven staan klaar om op onze eigen kern geprobeerd te worden (`<Game>.game.ini` in deze map, een overlay
met `[Loader] Core = ravengun`), maar geen enkele is bewezen.** Ik heb de games niet mogen starten; de eerste TV-test beslist voor
elke game of punt 2 echt blokkeert. Wat ik wel heb gebouwd is het gereedschap om dat in één run te beslissen en, als het blokkeert,
de AM-daemon op gemeten verzoeken te bouwen in plaats van op raden.

## Wat er in de exe's staat (`tools/dev/segajvs-scan.py`)

Elke game heeft de volledige SEGA AM Library statisch in de exe. Het hulpmiddel (alleen lezen) levert per exe de versies, de poorten die
als constante in code staan, de letterlijke `COM4`/`\\.\mxjvs`/`127.0.0.1` en per functienaam een begin-schatting.

| Game (exe) | AM Library | amMaster / amDongle | libpcp | poorten in code | JVS |
|---|---|---|---|---|---|
| LGI (`LGI_RingW_F_safe.exe`) | oud (2010) | 1.03 / 1.06 | 1.02 | 40100, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| LGI3D (`LGI.exe`) | oud (dec 2010) | 1.06 / 1.08 | 1.02 | 40100, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| GG (`RingGunR_RingWide.exe`) | 1.07 (2010) | 1.03 / 1.06 | 1.02 | 40100, 40102-40104, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| OG (`gs2.exe`) | 1.08 (2011) | 1.07 / 1.09 | 1.02 | 40100, 40102, 40103, 40106-40108, 40115 | `COM4`, `\\.\mxjvs` |
| SDR (`Game.exe`) | 1.08 (2011) | 1.07 / 1.09 | 1.02 | 40100, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| Transformers (`TF_Gun_R_Ring_dumped.exe`) | 1.17 (2013) | 1.13 / 1.14 | 1.08 | 40100, 40102-40104, 40106, 40107 | `COM4`, `\\.\mxjvs` |
| LGS (`GameSampR_RingWide.exe`) | 1.17 (2012) | 1.13 / 1.12 | 1.08 | 40100, 40102, 40103, 40106, 40107, 40114, 40115 | `COM4`, `\\.\mxjvs` |

Reproduceren: `python3 -I tools/dev/segajvs-scan.py <exe>` (`--json` voor machineleesbaar, `--names REGEX` voor functienamen). De
functiebegin-adressen zijn een schatting (eerste byte na twee of meer `int3`); de poortadressen zijn constanten in code, geen
disassemblage. Controle op LGS tegen de handmatige lezing van het TeknoParrot-team: `amMasterInit` op `0x45D840` met `0x9CA4` (40100)
klopt, `amDongleInit` ligt in de schatting op de verkeerde buur (`0x45F000` i.p.v. `0x45F390`): gebruik de adressen als aanwijzing voor
`objdump`, niet als bewijs.

Wat daaruit volgt: LGI, LGI3D en SDR hebben de kleinste oppervlakte (alleen master en dongle); GG, OG en Transformers vragen ook install
(40102/40103) en netwerk (40104); LGS heeft daarnaast opslag (40114/40115).

## De AM-daemon

**Wat de game vraagt [gelezen].** `amMasterOpen` (`0x45D9C0` in LGS) belt `127.0.0.1:40100` met een time-out van 60 s; `amMasterOpenEx` herhaalt
bij fout `-0xE` zonder teller. `amDongleSendAndRecvEx` (`0x461800`) roept `amDongleOpenEx` aan voor elke vraag (`keychip.appboot.region`,
`.gameid`, `.modeltype`, `.platformid`, `.networkaddr`, `.systemflag`, `keychip.version`, `keychip.billing.*`, ...). Een fout in de
init wordt alleen gelogd (de functie op `0x417e00` formatteert in een buffer: een logger, geen afbreking); een wachtlus in `OpenEx` is dat niet.

**Het protocol (libpcp) is tekst [gelezen].** Het verzoek is `commando=argument\r\n` met daarna `&sleutel=waarde\r\n` per extra veld,
hoogstens 256 bytes (`pcppSetSendPacket`/`pcppAddSendPacket`, formaten `%s=%s\r\n` en `&%s=%s\r\n`). De ontvanger tokeniseert op `=`, `&` en
CRLF (`pcppCheckStr`); woordtekens zijn letters, cijfers en `. _ - : @ % / \ # { }`. De **daemon eindigt zijn antwoord met een prompt
`>`** (`pcppCheckPrompt` zoekt dat teken in de ontvangen bytes en wacht erop met `pcppRecvPrompt`). Dat verklaart waarom de oude proef
(luisteraar die nulbytes en `PCP\0` stuurde) de game niet verder hielp: er kwam nooit een `>`. Wat nog niet gelezen is: de exacte opbouw
van een antwoord (welk veld het commando herhaalt, welk resultaatveld), per vraag de waarde-indeling, en wat install/storage/network
verwachten.

**Of het blokkeert [onbekend].**
* LGS: ja, zonder kern hangt de game voor COM4 op 40100 (`docs/decisions.md`, IO-brug stap 3) **[gemeten]**.
* LGI: onze kern kwam tot een antwoordende JVS-pipe na 25 s **[gemeten]**, maar `amJvsInitEx` staat in de init-volgorde (`0x401acc` in LGS) vóór
  elke AM-vraag, dus dat bewijst niet dat de AM-daemon niet nodig is.
* GG, OG, SDR, Transformers, LGI3D: niet gemeten.

Het TeknoParrot-team zegt dat de kern dit in-proces nabootst ("amNetwork emulator ... cannot listen on TCP", `TP_INSTANCE`, `AmAuthPort`)
**[TP-team]**; die tekst komt uit de 64-bits kern. In het ontsleutelde 32-bits beeld zijn `amDongle`/`amEeprom`/`amSram`/`amJvs` niet
te vinden (`region-mechanisme.md`), dus voor de RingWide-kern is niet vastgesteld *hoe* hij het doet.

## De test per game (voor de TV)

Voorwaarde voor elke test: de eigen OpenParrot-build staat in `~/.local/share/ravengun/openparrot/current/` (Win32-kern en lader) en
`rg-jvsbridge.exe` in `~/.local/share/ravengun/jvsbridge/`; dat is wat `rg-direct --plan` voor de kandidaat al aanwijst (gecontroleerd,
zie "Plan-controle").

1. Overlay in de gebruikerslaag zetten: `mkdir -p ~/.local/share/ravengun/patches/LGI && cp docs/research/segajvs/LGI.game.ini ~/.local/share/ravengun/patches/LGI/game.ini`.
2. Optioneel, voor de meting van punt 2: in een terminal vóór de start `python3 -I tools/dev/pcp-listen.py --greet --prompt-after --log /tmp/pcp.log`.
   Het beantwoordt niets inhoudelijk; het laat zien of de game belt en wat hij vraagt. Zonder luisteraar is dat zichtbaar in
   `WINEDEBUG=+winsock`: start met `rg-direct --log-full` en zet `RG_WINEDEBUG_FULL='-all,+winsock,err+all'` in de omgeving van het spel.
3. Dubbelklik in de app. Wat te zien en te melden, in volgorde:
   * Venster/beeld binnen 2 minuten? (zo niet: staat er een `connect 127.0.0.1:401xx` in het logboek of de luisteraar, dan is de
     AM-daemon de blokkade; stuur `/tmp/pcp.log`)
   * Attract; START (toets 1 even vasthouden); schieten met muismodus; 5 minuten door (regel "spel gehaald").
   * Logboek: `~/.local/state/ravengun/logs/direct-<Game>-*.log`, let op `JVS-pipe: game verbonden` en het aantal pakketten.
4. Slaagt het: de drie regels uit de overlay in `patches/<Game>/game.ini` zetten (`[Loader] Core = ravengun`) en de overlay weghalen.

| Game | Wat anders is aan de test |
|---|---|
| LGI | Eerste kandidaat: de kern kwam al tot JVS. Let op de 70 s laadtijd; het kan een AM-time-out van 60 s zijn. |
| LGI3D | Zelfde als LGI; rood-cyaan 3D-beeld is normaal. |
| LGS | Beslissend voor punt 2. Zonder daemon: hang bij "Now Loading" en een `connect` naar 40100 in het logboek. Stuurwiel/knoppen via gamepad of Move zoals nu. |
| GG, SDR | Dream Raiders vraagt de kalibratie bij de eerste start (zie `patches/SDR/game.ini`). |
| OG | Films via Gst32 + registerbestand blijven zoals ze zijn; zie het experiment hieronder als de AM-daemon blokkeert. |
| Transformers | `Windowed = 1` blijft nodig onder gamescope. |

### Experiment OG (alleen als OG op de AM-daemon blijkt te hangen)

`gs2.ini` in de spelmap heeft `EnableAMLib=1` en `EnableJvs=1`; de exe kent `AMLIB NOT LOADED.` en `getAMLibInstance() is NULL`
**[gelezen]**. Een drop-in `gs2.ini` met `EnableAMLib=0` (via `patches/OG/dropin/dropin.ini`, sectie `[gs2.ini]` met `To = .`) zou de
AM Library overslaan. Of OG daarna nog JVS via `amJvs` opent is niet gelezen; alleen proberen als de gewone kandidaat op de daemon
hangt. Het origineel op de NAS blijft ongemoeid (de view krijgt de eigen kopie).

## Plan-controle (2026-10-17, `rg-direct --plan`, geen start)

Voor elke kandidaat (`RAVENGUN_PATCHES_DIR` = patches-repo, `RAVENGUN_USER_PATCHES_DIR` = tijdelijke map met de overlay): verdict `ok`, lader
`openparrot/current/OpenParrotWin32/OpenParrotLoader.exe`, kern `.../OpenParrot.dll` (`core_source: ravengun`), JVS-pipehulp
`jvsbridge/rg-jvsbridge.exe`, route `jvs`/pipe met het bord uit `game.ini`. In de opdrachtregel staat geen bestand uit de
TeknoParrot-runtime meer. Wat de centrale route nog uit die map leest is werk van de centrale ontkoppeling, niet van deze games:
de werkmap (`cwd`) en de `GameProfiles/<P>.xml`-gegevens (`tp`), en het oude `UserProfiles/<P>.env` (alleen LGI/LGI3D/LGS hadden er een;
`game.ini` wint).

## Wat ik niet heb gedaan, en waarom

* Geen eigen AM-daemon of in-proces nabootsing: zonder een meting van wat de game werkelijk vraagt zou dat raden zijn, en de games
  mochten niet gestart worden. De hooks en de server zijn goedkoop zodra `pcp-listen.py` de eerste verzoeken heeft gelogd.
* Geen `Type`-wijziging in `patches/<Game>/game.ini`: tot de test slaagt blijft elke game op de werkende gesloten kern.
* Geen drop-in-DLL voor de seriele/JVS-kant: die doet onze kern al (punt 1).

## Volgende stap als punt 2 blokkeert

1. `pcp-listen.py --greet --prompt-after` laat zien welke commando's en velden een game stuurt (eerst alleen master en dongle).
2. De antwoordopbouw lezen in `pcppRecvResponse` (`0x478CA0` in LGS) en `amDongleResponseCheck` (`0x4609C0`) en een echte server bouwen die per
   sleutel het antwoord uit `patches/<Game>/dongle.json` (regio, model, platform-id) geeft. Een native Linux-luisteraar is genoeg: Wine
   bereikt `127.0.0.1` rechtstreeks; er hoeft niets in de prefix.
3. Alleen voor LGS erbij: install/storage/network (40102-40104, 40114/40115), met `amInstallResponseCheck`/`amStorageResponseCheck` als bron.
