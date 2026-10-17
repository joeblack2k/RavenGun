# Point Blank X zonder TeknoParrot (kandidaat, 2026-10-17)

Volledige uitleg: `docs/research/namco/README.md` in de loader-repo. Hier alleen wat bij deze map hoort.

* `game.kandidaat.ini`: `game.ini` met `[Loader] Core = ravengun` (onze OpenParrot-kern, generieke route, schijfletters F:/G: via patch 0004).
* `dropin.kandidaat.d/`: `dropin.ini` + `bnusio.dll`, de USIO-bibliotheek van het spel (`Plugins/bnusio.dll`, 48 cdecl-exports) als eigen werk
  (`tools/rg-usio`, bouwen met `tools/dev/build-usio.sh`), gevoed uit het gedeelde geheugen `TeknoParrot_JvsState` (indeling `PointBlankX`).
* Activeren: `game.kandidaat.ini` -> `game.ini` en `dropin.kandidaat.d/` -> `dropin/` (of dezelfde bestanden in
  `~/.local/share/ravengun/patches/PointBlankX/`). Terug: de oorspronkelijke `game.ini` en `dropin/` weg.
* Bewijs dat het draait: `Plugins/rg-usio.log` in de view ("rg-usio (bnusio.dll) geladen", "gedeeld geheugen ... geopend", "eerste aanroep:
  ...") en geen `TeknoParrot.dll` in `/proc/<pid>/maps`.
* Open: richting van y is afgeleid (Unity telt van onderen); fout? `[Env] RG_USIO_FLIP_Y = 0`.
