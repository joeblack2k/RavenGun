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
