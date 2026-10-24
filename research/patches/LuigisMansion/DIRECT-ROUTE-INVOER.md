# Luigi's Mansion op de directe route in C++ (rg-direct): start ja, invoer nog niet

Datum: 2026-10-14. Route: dubbelklik in RavenGun -> ravengund -> systemd-run -> `rg-direct --run` -> gamescope -> Wine ->
OpenParrotLoader64 -> VACUUM.exe, met de drop-in `rg-pe-hook.dll`.

## Starten

| Instelling | Uitkomst |
|---|---|
| zoals meegeleverd tot vandaag: gesloten TeknoParrot-kern, geen gamescope | lader stopt na ~13 s met exitcode 9, de game is weg |
| `[Loader] Core = ravengun` (onze kern, sha256 fe2ecf8c..., dezelfde als in BEWIJS-5MIN.md) + `[Display] Gamescope = true` | de game draait, attract beeldvullend op de tv |

De tweede stand staat nu in game.ini.

## Invoer (gemeten met de muismodus en `ravengunctl key`)

- Munt (toets 5): geen credits. De indeling `LuigisMansion` zegt al: munten lopen niet via het bordbeeld.
- TEST (toets 0): staat in het bordbeeld (byte 9 = 0x01, bit 8 van offset 8), maar het spel opent geen testmenu.
- START zonder credits: niets (verwacht).

Conclusie: met onze kern komt het bordbeeld (nog) niet bij het spel, of de kern leest het voor dit spel niet. Te doen: in onze
OpenParrot-kern nagaan welke invoerweg het spel op SEGA Nu gebruikt (`inputProtocol shared-luigi-mansion`) en die vullen uit
`TeknoParrot_JvsState`; munten apart (niet via dit blok). Status blijft `playable` (attract), niet `working`.
