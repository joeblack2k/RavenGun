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
7. RabbidsHollywood (AA, 2018) — unity-tcp, self-quit op eigen dongle (geparkeerd)
8. GhostBusters (Raw Thrills, 2011) — ongepakt ELF (eigen lader haalbaar)
9. Terminator (Raw Thrills, 2010) — ongepakt ELF (eigen lader haalbaar)
10. PacmanChomp (BN/Raw Thrills) — gepakt ELF
11. Onzeker (herkomst/jaar nog na te gaan): GoGoStrike (bowling_usa, ongepakt ELF), Rampage, InfinityBladeFX,
    RaccoonRampage, PigsMightFly

## B. Japanse games (als laatste, nieuw → oud, bij benadering)
- HOTDSD (Sega, 2018) — twee exe's (amdaemon), via TeknoParrotUi
- LuigisMansion (Capcom, 2015) — exit 32 = Capcom anti-tamper (0005 lost dat niet op), geparkeerd
- TC5 (Namco, 2015) — houdt de gesloten kern (bngrw/iDmacDrv)
- GaiaAttack4 (Taito, 2012) — Type X2, op onze kern, leeft
- LGI3D (Sega, 2012) — jvs/pipe, speelbaar
- OG (Sega Operation Ghost, ~2012)
- HauntedMuseumII (Taito, 2011) — Type X2, op onze kern, leeft
- MusicGunGun2 (Taito, ~2011) — Type X2, op onze kern, leeft
- LGI (Sega, 2009) — jvs/pipe, speelbaar
- HauntedMuseum (Taito, 2009) — Type X2, op onze kern, leeft
- TankTankTank (Namco, 2009)
- Otomedius (Konami, 2007) — tp-shm, exit 0 = **waarschijnlijk de eigen `ChkSum.bin`-controle**: 22 van 191 data-paks
  op de NAS wijken af (bewerkte/vertaalde paks) → "Conflict Hash" → schone exit. Plus ACIO (COM2) + een BMS-service
  (`\\.\pipe\bmssvc`, `bms_GGG.exe`) voor speelbaarheid. 0004 (E:-redirect) lost het vermoedelijk niet op (E: geeft
  een foutscherm, geen exit). **Tegenargument:** de gesloten kern draaide dezelfde paks wél. Nodig: één bench-trace
  (`WINEDEBUG=err+all,+file,+process,+seh` + `+win` op DestroyWindow) om de oorzaak te bevestigen; daarna parkeren met
  reden "checksum + ACIO", niet "exit 0". De schone fix is de originele ongewijzigde paks (eigenaar); de
  checksum-skip-optie raken we niet aan.
- LethalEnforcers3 (Konami, 2005) — tp-shm, leeft
- Gashaaaan / Gashaaaan2 (Konami) — Konami-AVS, 0004 lost de schijfletter-crash op; leeft
- OmatsuriYasanGansoTakoyaki / -KingyoSukui (Konami) — schijfletter; KingyoSukui mist campaign*.bmp (eigenaar)
- RainbowBombergirl (Konami) — tp-shm, hangt op PASELI/SYSTEM ENTRY (geparkeerd)
- MedaruNoGunman (Konami/FarCry) — 3001 RAM DATA BAD (geparkeerd)
- 3DCosplayMahjong (Taito) — geen exe gevonden
- LGS (Sega Let's Go) — jvs, speelbaar

## Aanpak
- Per game eerst read-only analyse (subagent) van de huidige stand + blocker; dan, als de bench vrij is,
  serieel headless meten. Blocker die alleen de eigenaar/tv kan oplossen → parkeren met een md en door.
- De grote infrastructuur (eigen kern/lader/prefix/launcher/JVS-brug, schijfletter-herleiding) staat al.
