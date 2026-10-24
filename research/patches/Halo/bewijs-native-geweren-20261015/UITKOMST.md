# Halo native met geweren -- uitkomst 2026-10-15

Route: eigen lader (ld.so in bwrap), geen Wine, geen TeknoParrot-lader. Gestart met een dubbelklik in de app (profiel
`Halo` in packaging/native-games.json, nu `enabled: true`).

## Gemeten

| wat | uitkomst | bewijs |
|---|---|---|
| geweren gevonden | twee IRTrack-geweren geopend, RVG00001 = speler 1, RVG00002 = speler 2 (logboek rg_irtrack) | -- |
| trekker | kalibratiescherm gaat door op de trekker; in het spel kills en munitie lopen | contact-ronde1.jpg |
| richtpunt | x klopt (0,70 -> 0,70); y was gespiegeld (0,25 -> 0,75), met `inverteer_y=ja` 0,25 -> 0,25 | cursor-y-gespiegeld.jpg, cursor-y-goed.jpg |
| fabrieksinstelling | zonder vlag 0x174a220: "Factory Setup Has Not Been Completed!" en START doet niets; met de vlag weg | attract-factory-setup-melding.jpg |
| meedoen | pas met adjustment hardware/p1gun (en p2gun) aan; daarna "CHOOSE A GAME MODE" | -- |
| WELCOME-wizard | komt bij elke start zolang de audits gun0/gun1Calibration ontbreken; met de seeds weg | welcome-wizard.jpg |
| bestandstest | met de uitgepakte `game` op /pm/g7/halo/game: "FileSystem integrity test failed!" en wachten op START; met `game_original` op dat pad en de game vanaf /pm/g7/bin/game: geslaagd | bestandstest-mislukt-uitgepakte-game.jpg |
| opstart zonder invoer | na de bestandstest (ca. 3 min, van de NAS) vanzelf de attract, FREEPLAY, geen melding | attract-schoon-zonder-invoer.jpg |
| spelen | ronde 1: Story Mode 5,5 min; ronde 2 (eindopzet): Mission 1 en de Warthog-sectie, samen 5,5 min | contact-ronde1.jpg, contact-ronde2.jpg |

## Open

- Geluid: stil (tools/fmod-stub; FMOD Studio 1.10 ontbreekt).
- De rechterspeler is in deze kast intern speler 3 (CabStart3, gun 1, p2gun); de START van speler 2 staat op bit 11 -> bord 0 k13
  (CabStart3). Op de bank niet met een tweede geweer bevestigd: `ravengunctl key 2` zet bit 11 niet en `--player 2` gaat bij
  output-button en test-pattern naar speler 1.
- "Failed to load shader: cov_shield/elite_shield/energy_sword/jackal_shield/spirit_energy" in het spellogboek (geen zichtbaar effect gezien).

Alle opnames: zie OPNAMES.txt.
