# Pigs Might Fly op de directe route in C++ (rg-direct): start en speelt, daarna zwart beeld

Datum: 2026-10-14, dubbelklik in RavenGun om 17:00:59 (logboek `direct-PigsMightFly-20261014-170059.log`).
Schermafdrukken: `~/.local/share/ravengun/bench-captures/PigsMightFly/` (niet in git).

| Tijd na start | Gezien |
|---|---|
| 0:00 | Redemption Shell (SEGA), staand beeld in het midden; de KDE-taakbalk blijft over de onderkant zichtbaar |
| 1:25 | titel "Pigs Might Fly", FREE PLAY |
| 1:30 | START (toets 1): het spel begint ("10 TIME", richtpijl, varken) |
| 2:20 en later | het HELE scherm zwart, ook de taakbalk weg; START brengt niets terug |

Processen in de unit bleven leven tot het stoppen (5:01): `Shell.exe` (~60 % CPU) en het spelproces (comm `h=00010080`,
opdrachtregel `/h=00010080 /k=831ccadb`, gestart door de Shell na ~30 s). Een schermvullend zwart venster dus, geen crash.

Vermoeden (niet gemeten): na de eerste ronde wisselt het spel van venster/resolutie (staand beeld) en geeft Wine dan een zwart
schermvullend venster. Te doen: dezelfde proef met gamescope (`[Display] Gamescope = true`), en vergelijken met de route via
TeknoParrotUi. Status blijft `playable`.

## Vervolg 2026-10-14 (17:54 - 18:04)

| Opstelling | Na de eerste ronde |
|---|---|
| gamescope, wined3d (Direct3D 9 via OpenGL) | beeld bevriest: drie opnames over twee minuten identiek (`pmf9.png`), processen leven |
| gamescope + DXVK voor Direct3D 9 (d3d9.dll uit GE-Proton10-34 in syswow64 van `ravengun-test-pigs`, `DllOverrides = d3d9=n,b`) | beeld leeft: drie opnames verschillen (`pmf10.png`) |

Beide instellingen staan nu in game.ini. Het origineel van d3d9.dll staat in de prefix als `d3d9.dll.wined3d-bewaard`.
Niet verder getest (geen 5 minuten, geen ronde bekeken): de eigenaar speelt dit spel niet (staand beeld, 90 graden gedraaid).
