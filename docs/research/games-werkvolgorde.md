# Werkvolgorde games (autonome ronde)

Opdracht eigenaar 2026-10-12: werk de games af, **Jurassic Park eerst**, daarna **nieuw → oud**, de
**Japanse games als laatste**. Blockers parkeren met een duidelijke md en doorgaan.

Let op: de TeknoParrot-profielen bevatten geen betrouwbare releasedatum (de "2001" is een constante).
Onderstaande volgorde is daarom **bij benadering** op basis van bekende releasejaren/uitgevers; corrigeer
gerust. Per game is de taak: draait hij op ONZE stack (eigen kern waar mogelijk, juist profiel, patches
voor crashes), headless gemeten; zo niet, blocker parkeren.

## 0. Eerst (uitzondering)
1. **JurassicPark** (Raw Thrills) — gepakt ELF (HASPE), houdt BudgieLoader; blocker-analyse eerst.

## A. Westerse games (nieuw → oud, bij benadering)
2. TransformersShadowsRising (Raw Thrills, ~2024)
3. NERF (Raw Thrills, ~2021) — leeft al op onze kern
4. GalagaAssault (Raw Thrills/BN, ~2019) — gepakt ELF
5. Halo (Raw Thrills, 2018) — gepakt ELF64
6. TombRaider (Adrenaline Amusements, 2018) — unity-tcp
7. RabbidsHollywood (AA, 2018) — unity-tcp; niet meer geparkeerd: hermeting 2026-10-12, start en leeft 288 s in attract (munt/start/vizier niet gemeten, headless); het eerdere "self-quit op eigen dongle" was een uitrol-fout
8. GhostBusters (Raw Thrills, 2011) — ongepakt ELF (eigen lader haalbaar)
9. Terminator (Raw Thrills, 2010) — ongepakt ELF (eigen lader haalbaar)
10. PacmanChomp (BN/Raw Thrills) — gepakt ELF
11. Onzeker (herkomst/jaar nog na te gaan): GoGoStrike (bowling_usa, ongepakt ELF), Rampage, InfinityBladeFX,
    RaccoonRampage, PigsMightFly

## B. Japanse games (als laatste, nieuw → oud, bij benadering)
- HOTDSD (Sega, 2018) — twee exe's (amdaemon), via TeknoParrotUi
- LuigisMansion (Capcom, 2015) — eindigt op onze kern en ook kernloos vroeg met `0x40415820` (kernloos gemeten: na 0,46 s) ("exit 32" is de lage byte), in de stub van de pakker; de COM-blokkade uit patch 0005 deed niets en is vervallen (2026-10-12); geparkeerd, zie `docs/research/luigismansion-kernloze-meting.md`
- TC5 (Namco, 2015) — houdt de gesloten kern (bngrw/iDmacDrv)
- GaiaAttack4 (Taito, 2012) — Type X2, op onze kern, leeft
- LGI3D (Sega, 2012) — jvs/pipe, speelbaar
- OG (Sega Operation Ghost, ~2012) — tp-shm, **leeft op onze kern** (0004), gemeten 2026-10-12; speelbaarheid pends tv
- HauntedMuseumII (Taito, 2011) — Type X2, op onze kern, leeft
- MusicGunGun2 (Taito, ~2011) — Type X2, op onze kern, leeft
- LGI (Sega, 2009) — jvs/pipe, speelbaar
- HauntedMuseum (Taito, 2009) — Type X2, op onze kern, leeft
- TankTankTank (Namco, 2009)
- Otomedius (Konami, 2007) — tp-shm, **geparkeerd met de gemeten reden**. De oude veger-"exit 0" is weg sinds 0004
  (schijfletter-herleiding van `E:\nvmem`), maar de uitkomst is **wisselend, en dat is het punt**: met volledige
  logboekuitvoer (`WINEDEBUG=+file,err+all`) verschijnt het spelproces pas rond 20 s en blijft het daarna leven, in
  een snelle run vertrekt het rond 2–5 s met exitcode 0. Het merkteken dat het verschil maakt is of `libacio.dll` en
  `libbms.dll` geladen worden — in de gesloten-kern-run staan ze in het log, in de snelle run niet, en de gesloten
  kern houdt hem ook op 20 s levend. Dat past bij ACIO op COM2 plus een BMS-service (`\\.\pipe\bmssvc`,
  `bms_GGG.exe`): de oorzaak is vermoedelijk geen exitcode maar een **ontbrekende tweede proces**. Daarnaast heeft het
  profiel de schakelaar `General/DisableFileCheckSum`, en 22 van de 191 datapakken op de NAS wijken af (bewerkt of
  vertaald), wat een Conflict Hash geeft; die schakelaar is **bewust niet aangezet** (eigenaar). De schone fix blijft
  de originele ongewijzigde paks (eigenaar). Speelbaarheid is niet gemeten — dat vraagt de tv. Volledige note in
  `patches/Otomedius/game.ini` `[Status]`.
- LethalEnforcers3 (Konami, 2005) — tp-shm, leeft
- Gashaaaan / Gashaaaan2 (Konami) — Konami-AVS, 0004 lost de schijfletter-crash op; leeft
- OmatsuriYasanGansoTakoyaki / -KingyoSukui (Konami) — schijfletter. **KingyoSukui: exitcode 8 op onze kern, en de eerdere verklaring "mist
  campaign*.bmp" is achterhaald** — in de `WINEDEBUG=+file`-trace opent het spelproces geen enkel bestand van zichzelf, terwijl de
  gesloten kern hem wel ziet leven (na 20 s nog). Zijn zusje Takoyaki leeft, met dezelfde lege herleidingsmappen; wat hen
  onderscheidt is een GMDATA-map die KingyoSukui niet heeft. Een drop-in kan hier niet landen: geen view. Zie
  `patches/OmatsuriYasanKingyoSukui/game.ini` `[Status]`.
- RainbowBombergirl (Konami) — tp-shm, hangt op PASELI/SYSTEM ENTRY (geparkeerd)
- MedaruNoGunman (Konami/FarCry) — 3001 RAM DATA BAD (geparkeerd)
- 3DCosplayMahjong (Taito) — geen exe gevonden
- LGS (Sega Let's Go) — jvs, speelbaar

## Aanpak
- Per game eerst read-only analyse (subagent) van de huidige stand + blocker; dan, als de bench vrij is,
  serieel headless meten. Blocker die alleen de eigenaar/tv kan oplossen → parkeren met een md en door.
- De grote infrastructuur (eigen kern/lader/prefix/launcher/JVS-brug, schijfletter-herleiding) staat al.
